// language: C++, file: artifact_shimcache.cpp, target: Windows XP..11
// AppCompatCache (ShimCache) parser for the raw value exported from
// SYSTEM\ControlSet00X\Control\Session Manager\AppCompatCache.
//
// Supported families:
//   Win10/11 : 0x30/0x34 header, "10ts" entries, FILETIME after the path
//   Win8.x   : 128-byte header, "00ts"/"10ts", package + flags before FILETIME
//   Win7/2k8R2: 0xBADC0FEE, 128-byte header, 32/48-byte table entries
//   Vista/2k3/2k8: 0xBADC0FFE, 8-byte header, 24/32-byte table entries
//   WinXP    : 0xDEADBEEF, 400-byte header, 96 slots of 552 bytes

#include "artifacts.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace deep {
namespace {

constexpr std::uint32_t kWin7Magic = 0xBADC0FEE;
constexpr std::uint32_t kVistaMagic = 0xBADC0FFE;
constexpr std::uint32_t kXpMagic = 0xDEADBEEF;

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | p[i];
    return value;
}

std::wstring utf16_string(const std::uint8_t* data, std::size_t bytes,
                          std::size_t chars) {
    if (data == nullptr || chars == 0) return {};
    if (chars * 2 > bytes) chars = bytes / 2;
    std::wstring out(chars, L'\0');
    std::memcpy(&out[0], data, chars * 2);
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring basename(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::string hex_prefix(const std::uint8_t* data, std::size_t size,
                       std::size_t cap) {
    static const char* digits = "0123456789abcdef";
    const std::size_t count = (std::min)(size, cap);
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

void emit_row(const std::wstring& source, const std::wstring& format, int index,
              const std::wstring& path, std::uint64_t filetime,
              std::uint32_t flags, bool executed, const std::string& extra,
              const ArtifactSink& sink, int& rows) {
    if (path.empty()) return;
    ArtifactRow row;
    row.type = "shimcache";
    row.source = art::to_utf8(source);
    row.key = art::to_utf8(format) + ":" + std::to_string(index);
    row.timestamp_utc = art::filetime_iso(filetime);
    row.path = art::to_utf8(path);
    row.name = art::to_utf8(basename(path));
    row.size = path.size() * 2;
    row.data = "{\"format\":" + art::json_escape_w(format) +
        ",\"entry\":" + std::to_string(index) +
        ",\"path\":" + art::json_escape_w(path) +
        ",\"last_modified_utc\":" +
        art::json_escape(art::filetime_iso(filetime)) +
        ",\"flags\":" + std::to_string(flags) +
        ",\"executed\":" + (executed ? "true" : "false") + extra + "}";
    sink(row);
    ++rows;
}

struct TableLayout {
    std::size_t entry_size;
    std::size_t path_offset_at;
    std::size_t time_at;
    std::size_t flags_at;
    std::size_t data_at; // SIZE_MAX when absent
    bool offset64;
};

const TableLayout kW7x86{32, 4, 8, 16, 24, false};
const TableLayout kW7x64{48, 8, 16, 24, 32, true};
const TableLayout kVista32{24, 4, 8, 16, SIZE_MAX, false};
const TableLayout kVista64{32, 8, 16, 24, SIZE_MAX, true};

struct TableEntry {
    std::wstring path;
    std::uint64_t filetime = 0;
    std::uint32_t flags = 0;
    std::uint32_t data_size = 0;
    std::uint64_t data_offset = 0;
};

int read_table_entries(const std::vector<std::uint8_t>& data,
                       std::size_t start, std::uint32_t count,
                       const TableLayout& layout, std::vector<TableEntry>& out) {
    if (count > 100000) count = 100000;
    int valid = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = start + static_cast<std::size_t>(i) * layout.entry_size;
        if (at + layout.entry_size > data.size()) break;
        const std::uint16_t path_bytes = read_u16(data.data() + at);
        if (path_bytes == 0 || (path_bytes & 1) != 0) continue;
        const std::uint64_t path_offset = layout.offset64
            ? read_u64(data.data() + at + layout.path_offset_at)
            : read_u32(data.data() + at + layout.path_offset_at);
        if (path_offset > data.size() || path_bytes > data.size() - path_offset)
            continue;
        TableEntry entry;
        entry.path = utf16_string(data.data() + path_offset, path_bytes,
                                  path_bytes / 2);
        if (entry.path.empty()) continue;
        entry.filetime = read_u64(data.data() + at + layout.time_at);
        entry.flags = read_u32(data.data() + at + layout.flags_at);
        if (layout.data_at != SIZE_MAX) {
            entry.data_size = layout.offset64
                ? static_cast<std::uint32_t>(read_u64(data.data() + at + layout.data_at))
                : read_u32(data.data() + at + layout.data_at);
            entry.data_offset = layout.offset64
                ? read_u64(data.data() + at + layout.data_at + 8)
                : read_u32(data.data() + at + layout.data_at + 4);
        }
        out.push_back(std::move(entry));
        ++valid;
    }
    return valid;
}

int parse_table(const std::vector<std::uint8_t>& data, std::size_t start,
                std::uint32_t count, bool win7, const std::wstring& source,
                const ArtifactSink& sink, std::wstring& error) {
    std::vector<TableEntry> entries32, entries64;
    const int valid32 = read_table_entries(data, start, count,
        win7 ? kW7x86 : kVista32, entries32);
    const int valid64 = read_table_entries(data, start, count,
        win7 ? kW7x64 : kVista64, entries64);
    const bool use64 = valid64 > valid32;
    const std::vector<TableEntry>& entries = use64 ? entries64 : entries32;
    const std::wstring format = win7
        ? (use64 ? L"win7_x64" : L"win7_x86")
        : (use64 ? L"vista_x64" : L"vista_x86");
    int rows = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const TableEntry& entry = entries[i];
        std::string extra;
        if (entry.data_size > 0 && entry.data_offset < data.size()) {
            const std::size_t available = static_cast<std::size_t>(
                data.size() - entry.data_offset);
            const std::size_t size = (std::min)(
                static_cast<std::size_t>(entry.data_size), available);
            extra = ",\"data_size\":" + std::to_string(entry.data_size) +
                ",\"data_hex\":" + art::json_escape(hex_prefix(
                    data.data() + entry.data_offset, size, 128));
        }
        emit_row(source, format, static_cast<int>(i), entry.path,
                 entry.filetime, entry.flags, (entry.flags & 2) != 0, extra,
                 sink, rows);
    }
    if (rows == 0) {
        error = win7 ? L"no Win7 table entries found"
                     : L"no Vista table entries found";
        return -1;
    }
    return rows;
}

int parse_xp(const std::vector<std::uint8_t>& data, const std::wstring& source,
             const ArtifactSink& sink, std::wstring& error) {
    constexpr std::size_t kHeader = 400;
    constexpr std::size_t kSlot = 552;
    if (data.size() < kHeader) {
        error = L"XP AppCompatCache header truncated";
        return -1;
    }
    std::uint32_t slots = read_u32(data.data() + 4);
    std::uint32_t count = read_u32(data.data() + 8);
    (void)slots;
    if (count > (kHeader - 16) / 4) count = (kHeader - 16) / 4;
    int rows = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t slot = read_u32(data.data() + 16 + i * 4);
        const std::size_t at = kHeader + static_cast<std::size_t>(slot) * kSlot;
        if (at + kSlot > data.size()) continue;
        const std::wstring path = utf16_string(data.data() + at, 528, 264);
        const std::uint64_t modified = read_u64(data.data() + at + 528);
        const std::uint64_t updated = read_u64(data.data() + at + 544);
        const std::string extra = ",\"last_update_utc\":" +
            art::json_escape(art::filetime_iso(updated)) +
            ",\"slot\":" + std::to_string(slot);
        emit_row(source, L"xp", static_cast<int>(i), path, modified, 0, false,
                 extra, sink, rows);
    }
    if (rows == 0) {
        error = L"no XP slots found";
        return -1;
    }
    return rows;
}

int parse_win8x(const std::vector<std::uint8_t>& data, std::size_t start,
                const char* signature, const std::wstring& source,
                const ArtifactSink& sink, std::wstring& error) {
    int rows = 0;
    std::size_t position = start;
    while (position + 12 <= data.size()) {
        if (std::memcmp(data.data() + position, signature, 4) != 0) break;
        const std::uint32_t entry_size = read_u32(data.data() + position + 8);
        const std::size_t body = position + 12;
        if (body + entry_size > data.size()) break;
        const std::size_t end = body + entry_size;
        std::size_t cursor = body;
        if (cursor + 2 > end) break;
        const std::uint16_t path_bytes = read_u16(data.data() + cursor);
        cursor += 2;
        if (path_bytes > end - cursor) break;
        const std::wstring path = utf16_string(data.data() + cursor, path_bytes,
                                               path_bytes / 2);
        cursor += path_bytes;
        if (cursor + 2 > end) break;
        const std::uint16_t package_len = read_u16(data.data() + cursor);
        cursor += 2;
        if (package_len > end - cursor) break;
        const std::size_t package_offset = cursor;
        cursor += package_len;
        if (cursor + 4 + 4 + 8 + 4 > end) break;
        const std::uint32_t flags = read_u32(data.data() + cursor);
        cursor += 4 + 4; // insertion + shim flags
        const std::uint64_t modified = read_u64(data.data() + cursor);
        cursor += 8;
        const std::uint32_t data_size = read_u32(data.data() + cursor);
        cursor += 4;
        const std::size_t available = end - cursor;
        const std::size_t stored = (std::min)(
            static_cast<std::size_t>(data_size), available);
        const std::string extra = ",\"package_size\":" +
            std::to_string(package_len) + ",\"package_hex\":" +
            art::json_escape(hex_prefix(data.data() + package_offset,
                package_len, 64)) + ",\"data_size\":" +
            std::to_string(data_size) + ",\"data_hex\":" +
            art::json_escape(hex_prefix(data.data() + cursor, stored, 128));
        std::wstring format = L"win8x_";
        for (const char* c = signature; *c != 0; ++c)
            format.push_back(static_cast<wchar_t>(*c));
        emit_row(source, format, rows, path,
                 modified, flags, (flags & 2) != 0, extra, sink, rows);
        position = end;
    }
    if (rows == 0) {
        error = L"no Win8.x entries found";
        return -1;
    }
    return rows;
}

int parse_win10(const std::vector<std::uint8_t>& data, std::size_t start,
                const std::wstring& source, const ArtifactSink& sink,
                std::wstring& error) {
    int rows = 0;
    std::size_t position = start;
    while (position + 14 <= data.size()) {
        std::size_t found = position;
        while (found + 4 <= data.size() &&
               std::memcmp(data.data() + found, "10ts", 4) != 0)
            ++found;
        if (found + 14 > data.size()) break;
        position = found;
        const std::size_t entry = position;
        const std::uint16_t path_bytes = read_u16(data.data() + entry + 12);
        const std::size_t path_offset = entry + 14;
        if (path_bytes == 0 || path_offset + path_bytes + 12 > data.size()) break;
        const std::size_t timestamp_offset = path_offset + path_bytes;
        const std::uint32_t data_length = read_u32(data.data() + timestamp_offset + 8);
        const std::size_t data_offset = timestamp_offset + 12;
        if (data_offset + data_length > data.size()) break;
        const std::wstring path = utf16_string(data.data() + path_offset,
            path_bytes, path_bytes / 2);
        const std::uint64_t modified = read_u64(data.data() + timestamp_offset);
        bool executed = false;
        if (data_length >= 4)
            executed = read_u32(data.data() + data_offset + data_length - 4) == 1;
        const std::string extra = ",\"data_size\":" +
            std::to_string(data_length) + ",\"data_hex\":" +
            art::json_escape(hex_prefix(data.data() + data_offset,
                data_length, 256));
        emit_row(source, L"win10", rows, path, modified, 0, executed, extra,
                 sink, rows);
        position = data_offset + data_length;
        if (rows > 100000) break;
    }
    if (rows == 0) {
        error = L"no 10ts entries found";
        return -1;
    }
    return rows;
}

} // namespace

int parse_shimcache_data(const std::vector<std::uint8_t>& data,
                         const std::wstring& source,
                         const ArtifactSink& sink, std::wstring& error) {
    if (data.size() < 4) {
        error = L"ShimCache data too small";
        return -1;
    }
    const std::uint32_t magic = read_u32(data.data());
    if (magic == kWin7Magic) {
        return parse_table(data, 128, read_u32(data.data() + 4), true, source,
                           sink, error);
    }
    if (magic == kVistaMagic) {
        return parse_table(data, 8, read_u32(data.data() + 4), false, source,
                           sink, error);
    }
    if (magic == kXpMagic) {
        return parse_xp(data, source, sink, error);
    }
    if ((magic == 0x30 || magic == 0x34) && data.size() >= 0x30) {
        return parse_win10(data, magic, source, sink, error);
    }
    if (data.size() >= 132) {
        if (std::memcmp(data.data() + 128, "00ts", 4) == 0)
            return parse_win8x(data, 128, "00ts", source, sink, error);
        if (std::memcmp(data.data() + 128, "10ts", 4) == 0)
            return parse_win8x(data, 128, "10ts", source, sink, error);
    }
    return parse_win10(data, 0, source, sink, error);
}

int collect_shimcache_file(const std::wstring& path, const ArtifactSink& sink,
                           std::wstring& error) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"ShimCache open failed, error " +
            std::to_wstring(GetLastError());
        return -1;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 64ll * 1024 * 1024) {
        error = L"ShimCache size invalid";
        CloseHandle(file);
        return -1;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()),
                             &read, nullptr) && read == data.size();
    CloseHandle(file);
    if (!ok) {
        error = L"ShimCache read failed";
        return -1;
    }
    return parse_shimcache_data(data, path, sink, error);
}

int collect_shimcache_hive(const std::wstring& hive_path,
                           const ArtifactSink& sink, std::wstring& error) {
    std::vector<std::uint8_t> value;
    std::wstring found_key;
    bool found = false;
    const int result = enumerate_hive_values(hive_path,
        [&](const HiveValueRecord& record) {
            if (record.value_name != L"AppCompatCache") return;
            if (record.key_path.find(L"Session Manager") == std::wstring::npos)
                return;
            value = record.data;
            found_key = record.key_path;
            found = true;
        }, error);
    if (result < 0) return -1;
    if (!found || value.empty()) {
        error = L"AppCompatCache value not found in hive";
        return -1;
    }
    return parse_shimcache_data(value, hive_path + L"\\" + found_key,
                                sink, error);
}

} // namespace deep
