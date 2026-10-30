// language: C++, file: artifact_shellbags.cpp, target: Windows 10/11
// ShellBag collector. BagMRU values contain shell item ID lists; this parses
// root GUIDs, file/folder short names and UTF-16 long names, and records the
// raw blob prefix for every value.

#include "artifacts.hpp"

#include <windows.h>
#include <objbase.h>
#include <cstring>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace deep {
namespace {

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::wstring guid_to_string(const std::uint8_t* bytes) {
    GUID guid{};
    std::memcpy(&guid, bytes, sizeof(guid));
    wchar_t buffer[64] = {};
    if (StringFromGUID2(guid, buffer, 64) <= 0) return L"{?}";
    return buffer;
}

std::string hex_prefix(const std::vector<std::uint8_t>& data, std::size_t cap) {
    static const char* digits = "0123456789abcdef";
    const std::size_t count = (std::min)(data.size(), cap);
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

std::wstring longest_utf16_run(const std::uint8_t* data, std::size_t size,
                               std::size_t start) {
    std::wstring best;
    std::wstring current;
    for (std::size_t i = start; i + 1 < size; i += 2) {
        const wchar_t c = static_cast<wchar_t>(data[i] | (data[i + 1] << 8));
        if (c >= 0x20 && c < 0x7F) {
            current.push_back(c);
        } else {
            if (current.size() > best.size()) best = current;
            current.clear();
        }
    }
    if (current.size() > best.size()) best = current;
    return best.size() >= 3 ? best : std::wstring();
}

std::wstring ascii_run(const std::uint8_t* data, std::size_t size,
                       std::size_t start) {
    std::wstring best;
    std::wstring current;
    for (std::size_t i = start; i < size; ++i) {
        const unsigned char c = data[i];
        if (c >= 0x20 && c < 0x7F) {
            current.push_back(static_cast<wchar_t>(c));
        } else {
            if (current.size() > best.size()) best = current;
            current.clear();
        }
    }
    if (current.size() > best.size()) best = current;
    return best.size() >= 2 ? best : std::wstring();
}

std::wstring parse_shell_items(const std::vector<std::uint8_t>& data,
                               std::string& items_json) {
    std::wstring path;
    std::string json = "[";
    std::size_t position = 0;
    bool first = true;
    while (position + 3 <= data.size()) {
        const std::uint16_t item_size = read_u16(data.data() + position);
        if (item_size == 0 || position + item_size > data.size()) break;
        const std::uint8_t type = data[position + 2];
        std::wstring name;
        if (type == 0x1F && item_size >= 20) {
            name = guid_to_string(data.data() + position + 4);
        } else if (type == 0x31 || type == 0x32) {
            const std::wstring short_name = ascii_run(data.data() + position,
                item_size, (std::min)(static_cast<std::size_t>(14), data.size() - position));
            const std::wstring long_name = longest_utf16_run(data.data() + position,
                item_size, 0);
            name = long_name.size() >= short_name.size() ? long_name : short_name;
            if (name.empty()) name = L"[file]";
        } else if (type == 0x2F) {
            name = ascii_run(data.data() + position, item_size, 3);
            if (name.empty()) name = longest_utf16_run(data.data() + position,
                item_size, 0);
            if (name.empty()) name = L"[volume]";
        } else {
            wchar_t buffer[16];
            std::swprintf(buffer, 16, L"[0x%02X]", type);
            name = buffer;
        }
        if (!path.empty()) path += L"\\";
        path += name;
        if (!first) json += ",";
        first = false;
        json += "{\"size\":" + std::to_string(item_size) +
            ",\"type\":" + std::to_string(type) +
            ",\"name\":" + art::json_escape_w(name) + "}";
        position += item_size;
    }
    json += "]";
    items_json = json;
    return path;
}

void walk_key(HKEY key, const std::wstring& key_path, const std::string& source,
              const ArtifactSink& sink, int depth, int& rows) {
    if (depth > 32 || rows >= 50000) return;
    DWORD value_count = 0;
    DWORD max_name = 0;
    DWORD max_data = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         &value_count, &max_name, &max_data, nullptr, nullptr) ==
        ERROR_SUCCESS) {
        std::vector<wchar_t> name(max_name + 2);
        std::vector<std::uint8_t> data(max_data + 2);
        for (DWORD index = 0; index < value_count && rows < 50000; ++index) {
            DWORD name_length = static_cast<DWORD>(name.size());
            DWORD data_length = static_cast<DWORD>(data.size());
            DWORD type = 0;
            const LONG status = RegEnumValueW(key, index, name.data(), &name_length,
                nullptr, &type, data.data(), &data_length);
            if (status != ERROR_SUCCESS) continue;
            if (name_length == 0) continue;
            bool numeric = true;
            for (DWORD i = 0; i < name_length; ++i) {
                if (name[i] < L'0' || name[i] > L'9') { numeric = false; break; }
            }
            if (!numeric) continue;
            data.resize(data_length);
            std::string items;
            const std::wstring decoded = parse_shell_items(data, items);
            ArtifactRow row;
            row.type = "shellbag";
            row.source = source;
            row.key = art::to_utf8(key_path) + "\\" + art::to_utf8(name.data());
            row.path = art::to_utf8(key_path);
            row.name = art::to_utf8(decoded);
            row.size = data_length;
            row.data = "{\"value_name\":" + art::json_escape_w(name.data()) +
                ",\"items\":" + items +
                ",\"hex_prefix\":" + art::json_escape(hex_prefix(data, 256)) + "}";
            sink(row);
            ++rows;
        }
    }

    for (DWORD index = 0;; ++index) {
        wchar_t subkey[512] = {};
        DWORD length = 512;
        const LONG status = RegEnumKeyExW(key, index, subkey, &length, nullptr,
            nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) continue;
        HKEY child = nullptr;
        if (RegOpenKeyExW(key, subkey, 0, KEY_READ, &child) != ERROR_SUCCESS)
            continue;
        walk_key(child, key_path + L"\\" + subkey, source, sink, depth + 1, rows);
        RegCloseKey(child);
    }
}

} // namespace

int collect_shellbags(const ArtifactSink& sink, std::wstring& error) {
    struct Root {
        HKEY key;
        const wchar_t* path;
        const char* source;
    };
    const Root roots[] = {
        {HKEY_CURRENT_USER,
         L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\BagMRU",
         "HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\BagMRU"},
        {HKEY_CURRENT_USER,
         L"Software\\Microsoft\\Windows\\Shell\\BagMRU",
         "HKCU\\Software\\Microsoft\\Windows\\Shell\\BagMRU"},
        {HKEY_CURRENT_USER,
         L"Software\\Microsoft\\Windows\\ShellNoRoam\\BagMRU",
         "HKCU\\Software\\Microsoft\\Windows\\ShellNoRoam\\BagMRU"}
    };
    int rows = 0;
    for (const auto& root : roots) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root.key, root.path, 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        walk_key(key, root.path, root.source, sink, 0, rows);
        RegCloseKey(key);
    }
    if (rows == 0) {
        error = L"no BagMRU values found or access denied";
        return 0;
    }
    return rows;
}

} // namespace deep
