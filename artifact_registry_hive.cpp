// language: C++, file: artifact_registry_hive.cpp, target: Windows 10/11
// Offline regf hive parser: keys (nk), values (vk), subkey lists (lf/lh/li/ri),
// big data (db), and best-effort recovery of free nk/vk cells.

#include "artifacts.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace deep {
namespace {

constexpr std::uint32_t kHiveBinsBase = 0x1000;
constexpr std::uint32_t kNoOffset = 0xFFFFFFFF;
constexpr std::size_t kMaxRows = 300000;
constexpr std::size_t kMaxHex = 512;

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

std::string hex_bytes(const std::uint8_t* data, std::size_t size,
                      std::size_t cap = kMaxHex) {
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

struct Hive {
    std::vector<std::uint8_t> bytes;
    std::uint32_t root_offset = 0;

    bool load(const std::wstring& path, std::wstring& error) {
        HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = L"hive open failed, error " + std::to_wstring(GetLastError());
            return false;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0x1000 ||
            size.QuadPart > 1024ll * 1024 * 1024) {
            error = L"hive size invalid";
            CloseHandle(file);
            return false;
        }
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        const bool ok = ReadFile(file, bytes.data(),
            static_cast<DWORD>(bytes.size()), &read, nullptr) &&
            read == bytes.size();
        CloseHandle(file);
        if (!ok || std::memcmp(bytes.data(), "regf", 4) != 0) {
            error = L"not a regf hive";
            return false;
        }
        root_offset = read_u32(bytes.data() + 0x24);
        return true;
    }

    bool cell(std::uint32_t offset, const std::uint8_t*& data,
              std::uint32_t& size, bool& allocated) const {
        if (offset == kNoOffset) return false;
        const std::uint64_t absolute = static_cast<std::uint64_t>(kHiveBinsBase) + offset;
        if (absolute + 4 > bytes.size()) return false;
        const std::int32_t signed_size = static_cast<std::int32_t>(
            read_u32(bytes.data() + absolute));
        if (signed_size == 0) return false;
        const std::uint32_t cell_size = static_cast<std::uint32_t>(
            signed_size < 0 ? -static_cast<std::int64_t>(signed_size)
                            : static_cast<std::int64_t>(signed_size));
        if (cell_size < 4 || absolute + cell_size > bytes.size()) return false;
        data = bytes.data() + absolute + 4;
        size = cell_size - 4;
        allocated = signed_size < 0;
        return true;
    }
};

struct Key {
    std::uint32_t offset = 0;
    std::uint16_t flags = 0;
    std::uint64_t last_write = 0;
    std::uint32_t subkey_count = 0;
    std::uint32_t subkey_list = kNoOffset;
    std::uint32_t value_count = 0;
    std::uint32_t value_list = kNoOffset;
    std::wstring name;
};

bool parse_key(const Hive& hive, std::uint32_t offset, Key& key) {
    const std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
    bool allocated = false;
    if (!hive.cell(offset, data, size, allocated) || size < 0x4C) return false;
    if (std::memcmp(data, "nk", 2) != 0) return false;
    key.offset = offset;
    key.flags = read_u16(data + 2);
    key.last_write = read_u64(data + 4);
    key.subkey_count = read_u32(data + 0x14);
    key.subkey_list = read_u32(data + 0x1C);
    key.value_count = read_u32(data + 0x24);
    key.value_list = read_u32(data + 0x28);
    const std::uint16_t name_length = read_u16(data + 0x48);
    const bool ascii = (key.flags & 0x20) != 0;
    if (0x4C + static_cast<std::uint64_t>(name_length) * (ascii ? 1 : 2) > size)
        return false;
    if (ascii) {
        key.name.resize(name_length);
        for (std::uint16_t i = 0; i < name_length; ++i)
            key.name[i] = static_cast<wchar_t>(data[0x4C + i]);
    } else {
        key.name.assign(reinterpret_cast<const wchar_t*>(data + 0x4C),
                        name_length);
    }
    return true;
}

void unique_offsets(const Hive& hive, std::uint32_t list_offset,
                    std::vector<std::uint32_t>& offsets, int depth) {
    if (depth > 4 || offsets.size() > 100000) return;
    const std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
    bool allocated = false;
    if (!hive.cell(list_offset, data, size, allocated) || size < 4) return;
    if (std::memcmp(data, "ri", 2) == 0) {
        const std::uint16_t count = read_u16(data + 2);
        for (std::uint16_t i = 0; i < count && 4 + i * 4 + 4 <= size; ++i)
            unique_offsets(hive, read_u32(data + 4 + i * 4), offsets, depth + 1);
        return;
    }
    const bool wide = std::memcmp(data, "lf", 2) == 0 ||
        std::memcmp(data, "lh", 2) == 0;
    const std::uint16_t count = read_u16(data + 2);
    const std::uint32_t entry_size = wide ? 8 : 4;
    for (std::uint16_t i = 0; i < count && 4 + i * entry_size + 4 <= size; ++i) {
        offsets.push_back(read_u32(data + 4 + i * entry_size));
    }
}

std::wstring value_name(const std::uint8_t* data, std::uint32_t size,
                        std::uint16_t flags, std::uint16_t name_length) {
    const std::size_t char_size = (flags & 1) != 0 ? 1 : 2;
    if (name_length == 0 ||
        0x14 + static_cast<std::size_t>(name_length) * char_size > size)
        return {};
    if ((flags & 1) != 0) {
        std::wstring out(name_length, L'\0');
        for (std::uint16_t i = 0; i < name_length; ++i)
            out[i] = static_cast<wchar_t>(data[0x14 + i]);
        return out;
    }
    return std::wstring(reinterpret_cast<const wchar_t*>(data + 0x14),
                        name_length);
}

bool read_big_data(const Hive& hive, std::uint32_t offset, std::uint32_t total,
                   std::vector<std::uint8_t>& out) {
    const std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
    bool allocated = false;
    if (!hive.cell(offset, data, size, allocated) || size < 8 ||
        std::memcmp(data, "db", 2) != 0) return false;
    const std::uint16_t segments = read_u16(data + 2);
    const std::uint32_t list_offset = read_u32(data + 4);
    const std::uint8_t* list = nullptr;
    std::uint32_t list_size = 0;
    if (!hive.cell(list_offset, list, list_size, allocated)) return false;
    out.clear();
    out.reserve(total);
    for (std::uint16_t i = 0; i < segments && i * 4 + 4 <= list_size; ++i) {
        const std::uint32_t segment_offset = read_u32(list + i * 4);
        const std::uint8_t* segment = nullptr;
        std::uint32_t segment_size = 0;
        if (!hive.cell(segment_offset, segment, segment_size, allocated)) break;
        out.insert(out.end(), segment, segment + segment_size);
        if (out.size() >= total) break;
    }
    if (out.size() > total) out.resize(total);
    return true;
}

bool read_value_data(const Hive& hive, std::uint32_t data_size,
                     std::uint32_t data_offset,
                     std::vector<std::uint8_t>& value) {
    if ((data_size & 0x80000000u) != 0) {
        const std::uint32_t length = data_size & 0x7FFFFFFFu;
        value.resize((std::min)(length, 4u));
        for (std::uint32_t i = 0; i < value.size(); ++i)
            value[i] = static_cast<std::uint8_t>((data_offset >> (8 * i)) & 0xFF);
    } else if (data_offset != kNoOffset) {
        const std::uint8_t* value_data = nullptr;
        std::uint32_t value_size = 0;
        bool allocated = false;
        if (hive.cell(data_offset, value_data, value_size, allocated)) {
            if (value_size >= 2 && std::memcmp(value_data, "db", 2) == 0) {
                read_big_data(hive, data_offset, data_size, value);
            } else {
                value.assign(value_data, value_data +
                    (std::min)(data_size, value_size));
            }
        }
    }
    return true;
}

std::string value_json(const Hive& hive, const std::uint8_t* data,
                       std::uint32_t size, std::uint32_t data_size,
                       std::uint32_t data_offset, std::uint32_t type,
                       std::uint16_t flags) {
    (void)flags;
    (void)data;
    (void)size;
    std::vector<std::uint8_t> value;
    read_value_data(hive, data_size, data_offset, value);
    std::string text;
    std::string numeric;
    switch (type) {
    case 1:
    case 2: {
        const std::size_t chars = value.size() / 2;
        if (chars > 0) {
            std::wstring wide(chars, L'\0');
            std::memcpy(&wide[0], value.data(), chars * 2);
            while (!wide.empty() && wide.back() == L'\0') wide.pop_back();
            text = art::to_utf8(wide);
        }
        break;
    }
    case 4:
        if (value.size() >= 4)
            numeric = std::to_string(read_u32(value.data()));
        break;
    case 5:
        if (value.size() >= 4) {
            const std::uint32_t raw = read_u32(value.data());
            const std::uint32_t swapped =
                ((raw & 0xFF) << 24) | ((raw & 0xFF00) << 8) |
                ((raw >> 8) & 0xFF00) | ((raw >> 24) & 0xFF);
            numeric = std::to_string(swapped);
        }
        break;
    case 7: {
        std::wstring combined;
        for (std::size_t i = 0; i + 1 < value.size(); i += 2) {
            const wchar_t c = static_cast<wchar_t>(value[i] | (value[i + 1] << 8));
            if (c == L'\0') {
                if (!combined.empty()) combined += L"\n";
            } else {
                combined.push_back(c);
            }
        }
        text = art::to_utf8(combined);
        break;
    }
    case 11:
        if (value.size() >= 8)
            numeric = std::to_string(read_u64(value.data()));
        break;
    default:
        break;
    }
    std::string json = "{\"value_type\":" + std::to_string(type) +
        ",\"value\":";
    if (!text.empty()) json += art::json_escape(text);
    else if (!numeric.empty()) json += numeric;
    else json += art::json_escape("");
    json += ",\"binary_hex\":" + art::json_escape(hex_bytes(value.data(),
        value.size())) + "}";
    (void)size;
    (void)data;
    return json;
}

void walk_key(const Hive& hive, std::uint32_t offset, const std::wstring& path,
              const std::string& source, bool recover_deleted,
              const ArtifactSink& sink, const HiveValueSink* value_sink,
              std::unordered_set<std::uint32_t>& visited,
              std::size_t& rows) {
    if (rows >= kMaxRows || !visited.insert(offset).second) return;
    Key key;
    if (!parse_key(hive, offset, key)) return;
    const std::wstring key_path = path.empty() ? L"\\" : path;
    if (rows < kMaxRows) {
        ArtifactRow row;
        row.type = "registry_key";
        row.source = source;
        row.key = art::to_utf8(key_path);
        row.timestamp_utc = art::filetime_iso(key.last_write);
        row.path = art::to_utf8(key_path);
        row.name = art::to_utf8(key.name);
        row.data = "{\"subkeys\":" + std::to_string(key.subkey_count) +
            ",\"values\":" + std::to_string(key.value_count) +
            ",\"flags\":" + std::to_string(key.flags) +
            ",\"cell_offset\":" + std::to_string(key.offset) + "}";
        sink(row);
        ++rows;
    }

    std::vector<std::uint32_t> value_offsets;
    if (key.value_list != kNoOffset) {
        const std::uint8_t* list = nullptr;
        std::uint32_t list_size = 0;
        bool allocated = false;
        if (hive.cell(key.value_list, list, list_size, allocated)) {
            for (std::uint32_t i = 0; i < key.value_count &&
                 i * 4 + 4 <= list_size; ++i) {
                value_offsets.push_back(read_u32(list + i * 4));
            }
        }
    }
    for (std::uint32_t value_offset : value_offsets) {
        if (rows >= kMaxRows) break;
        const std::uint8_t* data = nullptr;
        std::uint32_t size = 0;
        bool allocated = false;
        if (!hive.cell(value_offset, data, size, allocated) || size < 0x14 ||
            std::memcmp(data, "vk", 2) != 0) continue;
        const std::uint16_t name_length = read_u16(data + 2);
        const std::uint32_t data_size = read_u32(data + 4);
        const std::uint32_t data_offset = read_u32(data + 8);
        const std::uint32_t type = read_u32(data + 12);
        const std::uint16_t flags = read_u16(data + 0x10);
        const std::wstring name = value_name(data, size, flags, name_length);
        if (value_sink != nullptr) {
            HiveValueRecord record;
            record.key_path = key_path;
            record.value_name = name;
            record.type = type;
            record.last_write = key.last_write;
            read_value_data(hive, data_size, data_offset, record.data);
            (*value_sink)(record);
        }
        ArtifactRow row;
        row.type = "registry_offline";
        row.source = source;
        row.key = art::to_utf8(key_path) + "\\" + art::to_utf8(name);
        row.timestamp_utc = art::filetime_iso(key.last_write);
        row.path = art::to_utf8(key_path);
        row.name = art::to_utf8(name);
        row.size = data_size & 0x7FFFFFFFu;
        row.data = "{\"key\":" + art::json_escape_w(key_path) +
            ",\"value_name\":" + art::json_escape_w(name) +
            ",\"value\":" + value_json(hive, data, size, data_size, data_offset,
                                        type, flags) + "}";
        sink(row);
        ++rows;
    }

    std::vector<std::uint32_t> subkeys;
    unique_offsets(hive, key.subkey_list, subkeys, 0);
    for (std::uint32_t child : subkeys) {
        Key child_key;
        if (!parse_key(hive, child, child_key)) continue;
        const std::wstring child_path = key_path == L"\\"
            ? L"\\" + child_key.name
            : key_path + L"\\" + child_key.name;
        walk_key(hive, child, child_path, source, recover_deleted, sink,
                 value_sink, visited, rows);
    }
}

void recover_deleted_cells(const Hive& hive, const std::string& source,
                           const ArtifactSink& sink, std::size_t& rows) {
    std::uint32_t position = kHiveBinsBase;
    while (position + 32 < hive.bytes.size() && rows < kMaxRows) {
        if (std::memcmp(hive.bytes.data() + position, "hbin", 4) != 0) break;
        const std::uint32_t block_size = read_u32(hive.bytes.data() + position + 8);
        if (block_size < 32 || position + block_size > hive.bytes.size()) break;
        std::uint32_t cell = position + 32;
        const std::uint32_t end = position + block_size;
        while (cell + 4 < end && rows < kMaxRows) {
            const std::int32_t signed_size = static_cast<std::int32_t>(
                read_u32(hive.bytes.data() + cell));
            if (signed_size == 0) break;
            const std::uint32_t cell_size = static_cast<std::uint32_t>(
                signed_size < 0 ? -static_cast<std::int64_t>(signed_size)
                                : static_cast<std::int64_t>(signed_size));
            if (cell_size < 8 || cell + cell_size > end) break;
            if (signed_size > 0) {
                const std::uint8_t* data = hive.bytes.data() + cell + 4;
                const std::uint32_t size = cell_size - 4;
                if (size >= 0x4C && std::memcmp(data, "nk", 2) == 0) {
                    Key key;
                    if (parse_key(hive, cell - kHiveBinsBase, key)) {
                        ArtifactRow row;
                        row.type = "registry_deleted";
                        row.source = source;
                        row.key = "[deleted]\\" + art::to_utf8(key.name);
                        row.timestamp_utc = art::filetime_iso(key.last_write);
                        row.path = "[deleted]";
                        row.name = art::to_utf8(key.name);
                        row.data = "{\"cell_offset\":" + std::to_string(key.offset) +
                            ",\"subkeys\":" + std::to_string(key.subkey_count) +
                            ",\"values\":" + std::to_string(key.value_count) +
                            ",\"recovered\":true}";
                        sink(row);
                        ++rows;
                    }
                } else if (size >= 0x14 && std::memcmp(data, "vk", 2) == 0) {
                    const std::uint16_t name_length = read_u16(data + 2);
                    const std::uint32_t data_size = read_u32(data + 4);
                    const std::uint32_t data_offset = read_u32(data + 8);
                    const std::uint32_t type = read_u32(data + 12);
                    const std::uint16_t flags = read_u16(data + 0x10);
                    const std::wstring name = value_name(data, size, flags,
                                                         name_length);
                    ArtifactRow row;
                    row.type = "registry_deleted";
                    row.source = source;
                    row.key = "[deleted]\\" + art::to_utf8(name);
                    row.path = "[deleted]";
                    row.name = art::to_utf8(name);
                    row.size = data_size & 0x7FFFFFFFu;
                    row.data = "{\"cell_offset\":" +
                        std::to_string(cell - kHiveBinsBase) +
                        ",\"value\":" + value_json(hive, data, size, data_size,
                        data_offset, type, flags) + ",\"recovered\":true}";
                    sink(row);
                    ++rows;
                }
            }
            cell += cell_size;
        }
        position += block_size;
    }
}

} // namespace

int collect_hive_file(const std::wstring& path, bool recover_deleted,
                      const ArtifactSink& sink, std::wstring& error) {
    Hive hive;
    if (!hive.load(path, error)) return -1;
    std::unordered_set<std::uint32_t> visited;
    std::size_t rows = 0;
    walk_key(hive, hive.root_offset, L"", art::to_utf8(path), recover_deleted,
             sink, nullptr, visited, rows);
    if (recover_deleted) recover_deleted_cells(hive, art::to_utf8(path), sink, rows);
    if (rows == 0) {
        error = L"no keys parsed";
        return -1;
    }
    return static_cast<int>(rows);
}

int collect_hive_directory(const std::wstring& directory, bool recover_deleted,
                           const ArtifactSink& sink, std::wstring& error) {
    int total = 0;
    std::vector<std::wstring> pending{directory};
    while (!pending.empty()) {
        const std::wstring current = pending.back();
        pending.pop_back();
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW(art::join_path(current, L"*").c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(entry.cFileName, L".") == 0 ||
                wcscmp(entry.cFileName, L"..") == 0) continue;
            const std::wstring path = art::join_path(current, entry.cFileName);
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                    pending.push_back(path);
                continue;
            }
            std::wstring name = entry.cFileName;
            for (wchar_t& c : name) c = static_cast<wchar_t>(std::towlower(c));
            if (name.find(L".log") != std::wstring::npos ||
                name.find(L".blf") != std::wstring::npos) continue;
            std::wstring file_error;
            const int rows = collect_hive_file(path, recover_deleted, sink,
                                               file_error);
            if (rows > 0) total += rows;
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
     return total;
}

int enumerate_hive_values(const std::wstring& path, const HiveValueSink& sink,
                          std::wstring& error) {
    Hive hive;
    if (!hive.load(path, error)) return -1;
    std::unordered_set<std::uint32_t> visited;
    std::size_t rows = 0;
    ArtifactSink discard = [](const ArtifactRow&) {};
    walk_key(hive, hive.root_offset, L"", art::to_utf8(path), false, discard,
             &sink, visited, rows);
    return 0;
}

} // namespace deep