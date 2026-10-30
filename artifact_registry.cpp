// Language: C++17 (Win32 / Unicode)
// File:     artifact_registry.cpp
// Target:   Windows 10/11 x64 (MSYS2 UCRT64 g++, -std=c++17 -fms-extensions);
//           live registry collection for the Eye of God artifact pipeline.
//           Runs as a standard user: no elevation and no hive-loading APIs.
//
// Every emitted row has type "registry" and a JSON "data" object:
//   {"value_type": <number>, "value": <string>, "binary_hex": <hex or empty>}
// plus per-source extras (UserAssist counters, USBSTOR identity fields,
// NetworkList profile attributes, Uninstall product fields).
//
// Bounds: 1 MiB per value (larger values are reported by size only), 5000
// values per key, 5000 subkeys per key, recursion depth 8 under USBSTOR,
// 2000 visited USBSTOR keys, 100000 rows overall.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

#include "artifacts.hpp"

namespace {

using deep::ArtifactRow;
using deep::ArtifactSink;
using deep::art::filetime_iso;
using deep::art::json_escape;
using deep::art::to_utf8;

constexpr DWORD kMaxValueBytes = 1024u * 1024u;   // per-value read cap
constexpr std::size_t kMaxValuesPerKey = 5000;
constexpr std::size_t kMaxSubkeysPerKey = 5000;
constexpr std::size_t kMaxBinaryHexBytes = 4096;
constexpr std::size_t kMaxUsbStorKeys = 2000;
constexpr int kMaxUsbStorDepth = 8;
constexpr int kMaxRows = 100000;
constexpr DWORD kMaxSubkeyNameChars = 512;
constexpr DWORD kMaxValueNameChars = 16384;
constexpr DWORD kEnumerationSafetyLimit = 1000000u;

const wchar_t kHkcuSource[] = L"HKCU";
const wchar_t kHklmSource[] = L"HKLM";
const wchar_t kHkcuRoot[] = L"HKEY_CURRENT_USER";
const wchar_t kHklmRoot[] = L"HKEY_LOCAL_MACHINE";

const REGSAM kReadAccess = KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS;

// ---------------------------------------------------------------- primitives

std::string hex_encode(const unsigned char* data, std::size_t size) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out(size * 2, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        out[i * 2] = kDigits[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kDigits[data[i] & 0x0F];
    }
    return out;
}

std::string hex_encode(const std::vector<BYTE>& bytes) {
    const std::size_t count = std::min(bytes.size(), kMaxBinaryHexBytes);
    return hex_encode(bytes.data(), count);
}

std::wstring utf16le_to_wstring(const std::vector<BYTE>& bytes) {
    const std::size_t chars = bytes.size() / 2;
    std::wstring out;
    out.reserve(chars);
    for (std::size_t i = 0; i < chars; ++i) {
        wchar_t ch = 0;
        std::memcpy(&ch, bytes.data() + i * 2, sizeof(ch));
        out.push_back(ch);
    }
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring multi_sz_to_wstring(const std::vector<BYTE>& bytes) {
    const std::size_t chars = bytes.size() / 2;
    std::wstring joined;
    std::wstring current;
    bool have_entry = false;
    for (std::size_t i = 0; i < chars; ++i) {
        wchar_t ch = 0;
        std::memcpy(&ch, bytes.data() + i * 2, sizeof(ch));
        if (ch == L'\0') {
            if (current.empty()) break; // double NUL terminates the list
            if (have_entry) joined.push_back(L'\n');
            joined += current;
            current.clear();
            have_entry = true;
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        if (have_entry) joined.push_back(L'\n');
        joined += current;
    }
    return joined;
}

std::uint64_t load_uint(const std::vector<BYTE>& bytes, std::size_t offset,
                        std::size_t width, bool big_endian) {
    if (offset >= bytes.size()) return 0;
    const std::size_t count = std::min(width, bytes.size() - offset);
    std::uint64_t value = 0;
    if (big_endian) {
        for (std::size_t i = 0; i < count; ++i) {
            value = (value << 8) | static_cast<std::uint64_t>(bytes[offset + i]);
        }
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
        }
    }
    return value;
}

std::wstring ascii_lower(const std::wstring& text) {
    std::wstring out = text;
    for (wchar_t& ch : out) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
    }
    return out;
}

std::wstring rot13(const std::wstring& text) {
    std::wstring out = text;
    for (wchar_t& ch : out) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(L'A' + (ch - L'A' + 13) % 26);
        } else if (ch >= L'a' && ch <= L'z') {
            ch = static_cast<wchar_t>(L'a' + (ch - L'a' + 13) % 26);
        }
    }
    return out;
}

std::wstring extract_token(const std::wstring& name, const wchar_t* marker) {
    const std::wstring lower = ascii_lower(name);
    const std::wstring needle = ascii_lower(std::wstring(marker));
    if (needle.empty()) return {};
    const std::size_t pos = lower.find(needle);
    if (pos == std::wstring::npos) return {};
    const std::size_t start = pos + needle.size();
    std::size_t end = start;
    while (end < name.size() && name[end] != L'&') ++end;
    return name.substr(start, end - start);
}

// --------------------------------------------------------------- value access

struct ValueInfo {
    DWORD type = REG_NONE;
    std::uint64_t size = 0;   // full raw byte length reported by the registry
    bool too_large = false;   // data not read because it exceeds kMaxValueBytes
    std::vector<BYTE> bytes;  // empty when too_large or genuinely empty
};

bool read_value(HKEY key, const wchar_t* name, ValueInfo& info) {
    info = ValueInfo{};
    DWORD type = REG_NONE;
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS) return false;

    info.type = type;
    info.size = size;
    if (size > kMaxValueBytes) {
        info.too_large = true;
        return true;
    }

    std::vector<BYTE> buffer(size ? size : 1, 0);
    DWORD read = size;
    status = RegQueryValueExW(key, name, nullptr, &type, buffer.data(), &read);
    if (status == ERROR_MORE_DATA) {
        if (read > kMaxValueBytes) {
            info.type = type;
            info.size = read;
            info.too_large = true;
            return true;
        }
        buffer.assign(read ? read : 1, 0);
        DWORD retry = read;
        status = RegQueryValueExW(key, name, nullptr, &type, buffer.data(), &retry);
        read = retry;
    }
    if (status != ERROR_SUCCESS) return false;

    info.type = type;
    if (read > buffer.size()) read = static_cast<DWORD>(buffer.size());
    info.bytes.assign(buffer.begin(), buffer.begin() + read);
    info.size = info.bytes.size();
    return true;
}

std::wstring text_from_value(const ValueInfo& info) {
    switch (info.type) {
    case REG_SZ:
    case REG_EXPAND_SZ:
    case REG_LINK:
        return utf16le_to_wstring(info.bytes);
    case REG_MULTI_SZ:
        return multi_sz_to_wstring(info.bytes);
    case REG_DWORD:
    case REG_DWORD_BIG_ENDIAN:
        return std::to_wstring(
            load_uint(info.bytes, 0, 4, info.type == REG_DWORD_BIG_ENDIAN));
    case REG_QWORD:
        return std::to_wstring(load_uint(info.bytes, 0, 8, false));
    default:
        return {};
    }
}

struct RenderedValue {
    std::string value;
    std::string hex;
};

RenderedValue render_value(const ValueInfo& info) {
    RenderedValue out;
    switch (info.type) {
    case REG_SZ:
    case REG_EXPAND_SZ:
    case REG_LINK:
    case REG_MULTI_SZ:
    case REG_DWORD:
    case REG_DWORD_BIG_ENDIAN:
    case REG_QWORD:
        out.value = to_utf8(text_from_value(info));
        break;
    default: // REG_NONE, REG_BINARY and unknown types are shown as hex
        if (!info.bytes.empty()) out.hex = hex_encode(info.bytes);
        break;
    }
    return out;
}

// ------------------------------------------------------------------ JSON bits

struct JsonFields {
    std::string text;

    void add_raw(const char* key, const std::string& raw_json) {
        if (!text.empty()) text.push_back(',');
        text.push_back('"');
        text += key;
        text += "\":";
        text += raw_json;
    }
    void add_string(const char* key, const std::string& value) {
        add_raw(key, json_escape(value));
    }
    void add_text(const char* key, const std::wstring& value) {
        add_string(key, to_utf8(value));
    }
    void add_number(const char* key, std::uint64_t value) {
        add_raw(key, std::to_string(static_cast<unsigned long long>(value)));
    }
};

std::string build_data(DWORD value_type, const std::string& value,
                       const std::string& hex, const std::string& extra_fields) {
    std::string out;
    out.reserve(96 + value.size() + hex.size() + extra_fields.size());
    out += "{\"value_type\":";
    out += std::to_string(static_cast<unsigned long>(value_type));
    out += ",\"value\":";
    out += json_escape(value);
    out += ",\"binary_hex\":";
    out += json_escape(hex);
    if (!extra_fields.empty()) {
        out += ',';
        out += extra_fields;
    }
    out += '}';
    return out;
}

// ------------------------------------------------------------------- emitter

struct Collector {
    explicit Collector(const ArtifactSink& sink_in) : sink(sink_in) {}

    const ArtifactSink& sink;
    int rows = 0;
    std::size_t keys_opened = 0;
    std::size_t keys_denied = 0;
    bool cap_reached = false;

    bool full() const { return rows >= kMaxRows; }

    bool emit(const wchar_t* source, const std::wstring& key, const std::wstring& path,
              const std::wstring& name, const std::string& timestamp,
              std::uint64_t size, const std::string& data) {
        if (full()) {
            cap_reached = true;
            return false;
        }
        ArtifactRow row;
        row.type = "registry";
        row.source = to_utf8(source);
        row.key = to_utf8(key);
        row.path = to_utf8(path);
        row.name = to_utf8(name);
        row.timestamp_utc = timestamp;
        row.size = size;
        row.data = data;
        sink(row);
        ++rows;
        return true;
    }
};

bool emit_value_record(Collector& c, const wchar_t* source,
                       const std::wstring& key_path, const std::wstring& id_name,
                       const std::wstring& display_name, const ValueInfo& info,
                       const std::string& extra_fields, const std::string& timestamp) {
    const RenderedValue rendered = render_value(info);
    std::wstring id = key_path;
    if (!id_name.empty()) id += L"\\" + id_name;
    return c.emit(source, id, key_path, display_name, timestamp, info.size,
                  build_data(info.type, rendered.value, rendered.hex, extra_fields));
}

// -------------------------------------------------------------- registry keys

LSTATUS open_key(HKEY root, const std::wstring& subkey, HKEY& out) {
    out = nullptr;
    HKEY handle = nullptr;
    const LSTATUS status = RegOpenKeyExW(root, subkey.c_str(), 0, kReadAccess, &handle);
    if (status == ERROR_SUCCESS) out = handle;
    return status;
}

void note_key_result(Collector& c, LSTATUS status) {
    if (status == ERROR_SUCCESS) {
        ++c.keys_opened;
    } else if (status == ERROR_ACCESS_DENIED) {
        ++c.keys_denied;
    }
}

std::vector<std::wstring> enumerate_subkeys(HKEY key) {
    std::vector<std::wstring> names;
    std::vector<wchar_t> buffer(kMaxSubkeyNameChars, L'\0');
    for (DWORD index = 0; names.size() < kMaxSubkeysPerKey; ++index) {
        if (index >= kEnumerationSafetyLimit) break;
        DWORD length = kMaxSubkeyNameChars;
        const LSTATUS status = RegEnumKeyExW(key, index, buffer.data(), &length,
                                             nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status == ERROR_SUCCESS) {
            names.emplace_back(buffer.data(), length);
        } else if (status != ERROR_MORE_DATA) {
            break; // access denied, key deleted, ...
        }
    }
    return names;
}

bool emit_key_values(Collector& c, const wchar_t* source, HKEY key,
                     const std::wstring& key_path,
                     const std::string& extra_fields = std::string()) {
    std::vector<wchar_t> name_buffer(kMaxValueNameChars, L'\0');
    std::size_t emitted = 0;
    for (DWORD index = 0;
         emitted < kMaxValuesPerKey && !c.full() && index < kEnumerationSafetyLimit;
         ++index) {
        DWORD name_length = kMaxValueNameChars;
        DWORD type = REG_NONE;
        const LSTATUS status = RegEnumValueW(key, index, name_buffer.data(), &name_length,
                                             nullptr, &type, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) {
            if (status == ERROR_MORE_DATA) continue; // over-long name: skip
            break;                                   // access denied or similar
        }
        const std::wstring value_name(name_buffer.data(), name_length);
        ValueInfo info;
        if (!read_value(key, value_name.c_str(), info)) continue;
        const std::wstring display =
            value_name.empty() ? std::wstring(L"(Default)") : value_name;
        if (!emit_value_record(c, source, key_path, value_name, display, info,
                               extra_fields, std::string())) {
            return false;
        }
        ++emitted;
    }
    return true;
}

void collect_flat_key(Collector& c, HKEY root, const wchar_t* source,
                      const wchar_t* root_name, const std::wstring& subkey) {
    if (c.full()) return;
    HKEY handle = nullptr;
    const LSTATUS status = open_key(root, subkey, handle);
    note_key_result(c, status);
    if (status != ERROR_SUCCESS) return;
    const std::wstring path = std::wstring(root_name) + L"\\" + subkey;
    emit_key_values(c, source, handle, path, std::string());
    RegCloseKey(handle);
}

// ------------------------------------------------------------ Run / RunOnce

void collect_run_keys(Collector& c) {
    const wchar_t* const user_keys[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
    };
    for (const wchar_t* subkey : user_keys) {
        collect_flat_key(c, HKEY_CURRENT_USER, kHkcuSource, kHkcuRoot, subkey);
    }

    const wchar_t* const machine_keys[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
        L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
    };
    for (const wchar_t* subkey : machine_keys) {
        collect_flat_key(c, HKEY_LOCAL_MACHINE, kHklmSource, kHklmRoot, subkey);
    }
}

void collect_explorer_keys(Collector& c) {
    collect_flat_key(c, HKEY_CURRENT_USER, kHkcuSource, kHkcuRoot,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU");
    collect_flat_key(c, HKEY_CURRENT_USER, kHkcuSource, kHkcuRoot,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\TypedPaths");
}

// ------------------------------------------------------------- RecentDocs

void collect_recent_docs(Collector& c) {
    const std::wstring base =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RecentDocs";
    HKEY base_key = nullptr;
    const LSTATUS status = open_key(HKEY_CURRENT_USER, base, base_key);
    note_key_result(c, status);
    if (status != ERROR_SUCCESS) return;

    const std::wstring base_path = std::wstring(kHkcuRoot) + L"\\" + base;
    emit_key_values(c, kHkcuSource, base_key, base_path, std::string());

    const std::vector<std::wstring> subkeys = enumerate_subkeys(base_key);
    for (const std::wstring& subkey : subkeys) {
        if (c.full()) break;
        HKEY handle = nullptr;
        const LSTATUS child_status =
            RegOpenKeyExW(base_key, subkey.c_str(), 0, kReadAccess, &handle);
        if (child_status != ERROR_SUCCESS) {
            if (child_status == ERROR_ACCESS_DENIED) ++c.keys_denied;
            continue;
        }
        ++c.keys_opened;
        emit_key_values(c, kHkcuSource, handle, base_path + L"\\" + subkey, std::string());
        RegCloseKey(handle);
    }
    RegCloseKey(base_key);
}

// --------------------------------------------------------------- UserAssist

void emit_user_assist_values(Collector& c, HKEY key, const std::wstring& key_path) {
    std::vector<wchar_t> name_buffer(kMaxValueNameChars, L'\0');
    std::size_t emitted = 0;
    for (DWORD index = 0;
         emitted < kMaxValuesPerKey && !c.full() && index < kEnumerationSafetyLimit;
         ++index) {
        DWORD name_length = kMaxValueNameChars;
        DWORD type = REG_NONE;
        const LSTATUS status = RegEnumValueW(key, index, name_buffer.data(), &name_length,
                                             nullptr, &type, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) {
            if (status == ERROR_MORE_DATA) continue;
            break;
        }
        const std::wstring raw_name(name_buffer.data(), name_length);
        ValueInfo info;
        if (!read_value(key, raw_name.c_str(), info)) continue;

        const std::wstring decoded = rot13(raw_name);
        const std::uint32_t run_count =
            static_cast<std::uint32_t>(load_uint(info.bytes, 4, 4, false));
        const std::uint32_t focus_count =
            static_cast<std::uint32_t>(load_uint(info.bytes, 8, 4, false));
        const std::uint32_t focus_time_ms =
            static_cast<std::uint32_t>(load_uint(info.bytes, 12, 4, false));

        std::uint64_t last_execution = 0;
        std::string timestamp;
        if (info.bytes.size() >= 68) {
            last_execution = load_uint(info.bytes, 60, 8, false);
            timestamp = filetime_iso(last_execution);
        }

        JsonFields extra;
        extra.add_number("run_count", run_count);
        extra.add_number("focus_count", focus_count);
        extra.add_number("focus_time_ms", focus_time_ms);
        extra.add_number("raw_size", info.size);
        extra.add_string("decoded_name", to_utf8(decoded));
        if (last_execution != 0) extra.add_number("last_execution", last_execution);
        if (!timestamp.empty()) extra.add_string("last_execution_utc", timestamp);

        const std::wstring display = decoded.empty() ? raw_name : decoded;
        if (!emit_value_record(c, kHkcuSource, key_path, raw_name, display, info,
                               extra.text, timestamp)) {
            break;
        }
        ++emitted;
    }
}

void collect_user_assist(Collector& c) {
    const std::wstring base =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist";
    HKEY base_key = nullptr;
    const LSTATUS status = open_key(HKEY_CURRENT_USER, base, base_key);
    note_key_result(c, status);
    if (status != ERROR_SUCCESS) return;

    const std::wstring base_path = std::wstring(kHkcuRoot) + L"\\" + base;
    const std::vector<std::wstring> guids = enumerate_subkeys(base_key);
    for (const std::wstring& guid : guids) {
        if (c.full()) break;
        const std::wstring guid_path = base_path + L"\\" + guid;
        const std::wstring count_sub = guid + L"\\Count";

        HKEY count_key = nullptr;
        const LSTATUS count_status =
            RegOpenKeyExW(base_key, count_sub.c_str(), 0, kReadAccess, &count_key);
        if (count_status != ERROR_SUCCESS) {
            if (count_status == ERROR_ACCESS_DENIED) ++c.keys_denied;
            continue; // no Count subkey: nothing to collect for this GUID
        }
        ++c.keys_opened;
        emit_user_assist_values(c, count_key, guid_path + L"\\Count");
        RegCloseKey(count_key);
    }
    RegCloseKey(base_key);
}

// ----------------------------------------------------------------- USBSTOR

struct UsbStorInfo {
    std::wstring device_key;
    std::wstring vendor;
    std::wstring product;
    std::wstring revision;
    std::wstring instance;
    std::wstring device_id;
    std::wstring friendly_name;
    std::wstring device_desc;
};

std::string usbstor_extra(const UsbStorInfo& info) {
    JsonFields fields;
    fields.add_string("kind", "usbstor_device");
    if (!info.device_key.empty()) fields.add_text("device_key", info.device_key);
    if (!info.vendor.empty()) fields.add_text("vendor", info.vendor);
    if (!info.product.empty()) fields.add_text("product", info.product);
    if (!info.revision.empty()) fields.add_text("revision", info.revision);
    if (!info.instance.empty()) fields.add_text("serial", info.instance);
    if (!info.device_id.empty()) fields.add_text("device_id", info.device_id);
    if (!info.friendly_name.empty()) fields.add_text("friendly_name", info.friendly_name);
    if (!info.device_desc.empty()) fields.add_text("device_desc", info.device_desc);
    return fields.text;
}

void walk_usbstor(Collector& c, HKEY parent, const std::wstring& parent_path, int depth,
                  UsbStorInfo info, std::size_t& visited) {
    if (depth >= kMaxUsbStorDepth || visited >= kMaxUsbStorKeys || c.full()) return;

    const std::vector<std::wstring> children = enumerate_subkeys(parent);
    for (const std::wstring& child : children) {
        if (visited >= kMaxUsbStorKeys || c.full()) break;

        HKEY handle = nullptr;
        const LSTATUS status =
            RegOpenKeyExW(parent, child.c_str(), 0, kReadAccess, &handle);
        if (status != ERROR_SUCCESS) {
            if (status == ERROR_ACCESS_DENIED) ++c.keys_denied;
            continue;
        }
        ++visited;
        ++c.keys_opened;

        const int child_depth = depth + 1;
        const std::wstring child_path = parent_path + L"\\" + child;
        UsbStorInfo child_info = info;

        if (child_depth == 1) {
            child_info.device_key = child;
            child_info.vendor = extract_token(child, L"Ven_");
            child_info.product = extract_token(child, L"Prod_");
            child_info.revision = extract_token(child, L"Rev_");
        } else if (child_depth == 2) {
            child_info.instance = child;
            child_info.device_id = std::wstring(L"USBSTOR\\") + child_info.device_key +
                                   L"\\" + child;
        }

        ValueInfo value;
        if (read_value(handle, L"FriendlyName", value)) {
            const std::wstring text = utf16le_to_wstring(value.bytes);
            if (!text.empty()) child_info.friendly_name = text;
        }
        if (read_value(handle, L"DeviceDesc", value)) {
            const std::wstring text = utf16le_to_wstring(value.bytes);
            if (!text.empty()) child_info.device_desc = text;
        }

        if (child_depth <= 2) {
            const std::wstring& display =
                child_info.friendly_name.empty() ? child : child_info.friendly_name;
            const std::wstring id =
                child_info.device_id.empty() ? child_path : child_info.device_id;
            c.emit(kHklmSource, id, child_path, child, std::string(), 0,
                   build_data(REG_SZ, to_utf8(display), std::string(),
                              usbstor_extra(child_info)));
        }

        emit_key_values(c, kHklmSource, handle, child_path, usbstor_extra(child_info));
        walk_usbstor(c, handle, child_path, child_depth, child_info, visited);
        RegCloseKey(handle);
    }
}

void collect_usbstor(Collector& c) {
    const std::wstring subkey = L"SYSTEM\\CurrentControlSet\\Enum\\USBSTOR";
    HKEY base_key = nullptr;
    const LSTATUS status = open_key(HKEY_LOCAL_MACHINE, subkey, base_key);
    note_key_result(c, status);
    if (status != ERROR_SUCCESS) return;

    UsbStorInfo info;
    std::size_t visited = 0;
    walk_usbstor(c, base_key, std::wstring(kHklmRoot) + L"\\" + subkey, 0, info, visited);
    RegCloseKey(base_key);
}

// -------------------------------------------------------------- NetworkList

std::string systemtime_to_iso(const std::vector<BYTE>& bytes) {
    if (bytes.size() < 16) return {};
    WORD parts[8] = {};
    for (int i = 0; i < 8; ++i) {
        std::memcpy(&parts[i], bytes.data() + i * 2, sizeof(WORD));
    }
    if (parts[0] < 1601 || parts[0] > 9999) return {};

    SYSTEMTIME st{};
    st.wYear = parts[0];         // SYSTEMTIME layout: year, month, day-of-week,
    st.wMonth = parts[1];        // day, hour, minute, second, milliseconds
    st.wDayOfWeek = parts[2];
    st.wDay = parts[3];
    st.wHour = parts[4];
    st.wMinute = parts[5];
    st.wSecond = parts[6];
    st.wMilliseconds = parts[7];

    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return {};
    const std::uint64_t value =
        (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return filetime_iso(value);
}

std::wstring network_category_name(std::uint64_t category) {
    switch (category) {
    case 0: return L"Public";
    case 1: return L"Private";
    case 2: return L"Domain";
    default: return {};
    }
}

void collect_network_profiles_at(Collector& c, HKEY root, const wchar_t* source,
                                 const wchar_t* root_name, const std::wstring& base) {
    HKEY base_key = nullptr;
    const LSTATUS status = open_key(root, base, base_key);
    note_key_result(c, status);
    if (status != ERROR_SUCCESS) return;

    const std::wstring base_path = std::wstring(root_name) + L"\\" + base;
    const std::vector<std::wstring> profiles = enumerate_subkeys(base_key);
    for (const std::wstring& guid : profiles) {
        if (c.full()) break;
        HKEY handle = nullptr;
        const LSTATUS profile_status =
            RegOpenKeyExW(base_key, guid.c_str(), 0, kReadAccess, &handle);
        if (profile_status != ERROR_SUCCESS) {
            if (profile_status == ERROR_ACCESS_DENIED) ++c.keys_denied;
            continue;
        }
        ++c.keys_opened;

        const std::wstring path = base_path + L"\\" + guid;
        ValueInfo value;
        std::uint64_t total_bytes = 0;

        std::wstring profile_name;
        if (read_value(handle, L"ProfileName", value)) {
            total_bytes += value.size;
            profile_name = utf16le_to_wstring(value.bytes);
        }
        if (profile_name.empty() && read_value(handle, L"Description", value)) {
            total_bytes += value.size;
            profile_name = utf16le_to_wstring(value.bytes);
        }

        bool has_managed = false;
        std::uint64_t managed = 0;
        if (read_value(handle, L"Managed", value)) {
            total_bytes += value.size;
            has_managed = true;
            managed = load_uint(value.bytes, 0, 4, false);
        }

        bool has_category = false;
        std::uint64_t category = 0;
        if (read_value(handle, L"Category", value)) {
            total_bytes += value.size;
            has_category = true;
            category = load_uint(value.bytes, 0, 4, false);
        }

        std::string created;
        if (read_value(handle, L"ProfileDateCreated", value)) {
            total_bytes += value.size;
            created = systemtime_to_iso(value.bytes);
        }
        std::string connected;
        if (read_value(handle, L"ProfileDateLastConnected", value)) {
            total_bytes += value.size;
            connected = systemtime_to_iso(value.bytes);
        }

        JsonFields extra;
        extra.add_string("kind", "network_profile");
        extra.add_string("profile_guid", to_utf8(guid));
        extra.add_text("profile_name", profile_name);
        if (has_managed) extra.add_number("managed", managed);
        if (has_category) {
            extra.add_number("category", category);
            const std::wstring category_name = network_category_name(category);
            if (!category_name.empty()) extra.add_text("category_name", category_name);
        }
        if (!created.empty()) extra.add_string("date_created", created);
        if (!connected.empty()) extra.add_string("date_last_connected", connected);

        const std::wstring display = profile_name.empty() ? guid : profile_name;
        c.emit(source, path, path, display, connected, total_bytes,
               build_data(REG_SZ, to_utf8(display), std::string(), extra.text));
        RegCloseKey(handle);
    }
    RegCloseKey(base_key);
}

void collect_network_profiles(Collector& c) {
    collect_network_profiles_at(
        c, HKEY_LOCAL_MACHINE, kHklmSource, kHklmRoot,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\Profiles");
}

// ---------------------------------------------------------------- Uninstall

void collect_uninstall_branch(Collector& c, const std::wstring& base) {
    HKEY base_key = nullptr;
    const LSTATUS status = open_key(HKEY_LOCAL_MACHINE, base, base_key);
    note_key_result(c, status);
    if (status != ERROR_SUCCESS) return;

    const std::wstring base_path = std::wstring(kHklmRoot) + L"\\" + base;
    const std::vector<std::wstring> entries = enumerate_subkeys(base_key);
    for (const std::wstring& entry : entries) {
        if (c.full()) break;
        HKEY handle = nullptr;
        const LSTATUS entry_status =
            RegOpenKeyExW(base_key, entry.c_str(), 0, kReadAccess, &handle);
        if (entry_status != ERROR_SUCCESS) {
            if (entry_status == ERROR_ACCESS_DENIED) ++c.keys_denied;
            continue;
        }
        ++c.keys_opened;

        const std::wstring path = base_path + L"\\" + entry;
        ValueInfo value;
        std::uint64_t total_bytes = 0;
        std::wstring display_name;
        std::wstring display_version;
        std::wstring publisher;
        std::wstring install_date;

        if (read_value(handle, L"DisplayName", value)) {
            total_bytes += value.size;
            display_name = text_from_value(value);
        }
        if (read_value(handle, L"DisplayVersion", value)) {
            total_bytes += value.size;
            display_version = text_from_value(value);
        }
        if (read_value(handle, L"Publisher", value)) {
            total_bytes += value.size;
            publisher = text_from_value(value);
        }
        if (read_value(handle, L"InstallDate", value)) {
            total_bytes += value.size;
            install_date = text_from_value(value);
        }

        if (!display_name.empty() || !display_version.empty() || !publisher.empty() ||
            !install_date.empty()) {
            JsonFields extra;
            extra.add_string("kind", "uninstall_entry");
            extra.add_text("display_name", display_name);
            extra.add_text("display_version", display_version);
            extra.add_text("publisher", publisher);
            extra.add_text("install_date", install_date);
            const std::wstring display = display_name.empty() ? entry : display_name;
            c.emit(kHklmSource, path, path, display, std::string(), total_bytes,
                   build_data(REG_SZ, to_utf8(display), std::string(), extra.text));
        }
        RegCloseKey(handle);
    }
    RegCloseKey(base_key);
}

void collect_uninstall(Collector& c) {
    collect_uninstall_branch(
        c, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    collect_uninstall_branch(
        c, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
}

} // namespace

// ------------------------------------------------------------------- entry

int deep::collect_registry(const deep::ArtifactSink& sink, std::wstring& error) {
    error.clear();
    if (!sink) {
        error = L"registry: empty sink callback";
        return -1;
    }

    Collector collector(sink);

    collect_run_keys(collector);
    collect_explorer_keys(collector);
    collect_recent_docs(collector);
    collect_user_assist(collector);
    collect_usbstor(collector);
    collect_network_profiles(collector);
    collect_uninstall(collector);

    if (collector.keys_opened == 0) {
        error = L"registry: unable to open any registry key (hive unavailable or access denied)";
        return -1;
    }
    if (collector.rows == 0) {
        error = L"registry: no artifacts found in the live registry";
        return -1;
    }
    return collector.rows;
}
