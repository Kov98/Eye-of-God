// language: C++, file: artifact_recycle.cpp, target: Windows 10/11
// Recycle Bin parser: $I files carry the original name, size and deletion time;
// $R files carry the deleted content. This collector records both without
// moving anything.

#include "artifacts.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace deep {
namespace {

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

bool read_small_file(const std::wstring& path, std::vector<std::uint8_t>& data,
                     std::size_t limit) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > limit) {
        CloseHandle(file);
        return false;
    }
    data.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = data.empty() || (ReadFile(file, data.data(),
        static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size());
    CloseHandle(file);
    return ok;
}

void collect_bin(const std::wstring& bin_root, const ArtifactSink& sink, int& rows) {
    WIN32_FIND_DATAW sid_entry{};
    HANDLE sid_search = FindFirstFileW(art::join_path(bin_root, L"*").c_str(),
                                       &sid_entry);
    if (sid_search == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(sid_entry.cFileName, L".") == 0 ||
            wcscmp(sid_entry.cFileName, L"..") == 0 ||
            (sid_entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        const std::wstring sid_dir = art::join_path(bin_root, sid_entry.cFileName);
        WIN32_FIND_DATAW item{};
        HANDLE search = FindFirstFileW(art::join_path(sid_dir, L"$I*").c_str(),
                                       &item);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring i_path = art::join_path(sid_dir, item.cFileName);
            std::vector<std::uint8_t> data;
            if (!read_small_file(i_path, data, 1024 * 1024) || data.size() < 28)
                continue;
            const std::uint64_t version = read_u64(data.data());
            const std::uint64_t original_size = read_u64(data.data() + 8);
            const std::uint64_t deleted_ft = read_u64(data.data() + 16);
            const std::uint32_t name_chars = read_u32(data.data() + 24);
            if (name_chars == 0 || 28ull + static_cast<std::uint64_t>(name_chars) * 2 >
                data.size()) continue;
            std::wstring original_name(name_chars, L'\0');
            std::memcpy(&original_name[0], data.data() + 28,
                        static_cast<std::size_t>(name_chars) * 2);
            while (!original_name.empty() && original_name.back() == L'\0')
                original_name.pop_back();
            const std::wstring suffix = std::wstring(item.cFileName).substr(2);
            const std::wstring r_path = art::join_path(sid_dir, L"$R" + suffix);
            std::uint64_t r_size = 0;
            WIN32_FILE_ATTRIBUTE_DATA r_info{};
            if (GetFileAttributesExW(r_path.c_str(), GetFileExInfoStandard, &r_info)) {
                r_size = (static_cast<std::uint64_t>(r_info.nFileSizeHigh) << 32) |
                    r_info.nFileSizeLow;
            }
            ArtifactRow row;
            row.type = "recycle";
            row.source = art::to_utf8(bin_root);
            row.key = art::to_utf8(suffix);
            row.timestamp_utc = art::filetime_iso(deleted_ft);
            row.path = art::to_utf8(r_path);
            row.name = art::to_utf8(original_name);
            row.size = static_cast<std::uint64_t>(original_size);
            row.data = "{\"version\":" + std::to_string(version) +
                ",\"original_name\":" + art::json_escape_w(original_name) +
                ",\"original_size\":" + std::to_string(original_size) +
                ",\"recycle_path\":" + art::json_escape_w(r_path) +
                ",\"recycle_size\":" + std::to_string(r_size) +
                ",\"sid\":" + art::json_escape_w(sid_entry.cFileName) + "}";
            sink(row);
            ++rows;
        } while (FindNextFileW(search, &item));
        FindClose(search);
    } while (FindNextFileW(sid_search, &sid_entry));
    FindClose(sid_search);
}

std::wstring safe_component(const std::wstring& name) {
    std::wstring out;
    for (wchar_t c : name) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == 0)
            out.push_back(L'_');
        else
            out.push_back(c);
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.'))
        out.pop_back();
    return out.empty() ? L"recovered" : out;
}

std::wstring base_name(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool copy_tree(const std::wstring& from, const std::wstring& to) {
    const DWORD attributes = GetFileAttributesW(from.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return CopyFileW(from.c_str(), to.c_str(), FALSE) != FALSE;
    if (!art::ensure_directory(to)) return false;
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(art::join_path(from, L"*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return true;
    do {
        if (wcscmp(entry.cFileName, L".") == 0 ||
            wcscmp(entry.cFileName, L"..") == 0) continue;
        copy_tree(art::join_path(from, entry.cFileName),
                  art::join_path(to, entry.cFileName));
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    return true;
}

void recover_bin(const std::wstring& bin_root, const std::wstring& out_root,
                 const ArtifactSink& sink, int& rows) {
    WIN32_FIND_DATAW sid_entry{};
    HANDLE sid_search = FindFirstFileW(art::join_path(bin_root, L"*").c_str(),
                                       &sid_entry);
    if (sid_search == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(sid_entry.cFileName, L".") == 0 ||
            wcscmp(sid_entry.cFileName, L"..") == 0 ||
            (sid_entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        const std::wstring sid_dir = art::join_path(bin_root, sid_entry.cFileName);
        WIN32_FIND_DATAW item{};
        HANDLE search = FindFirstFileW(art::join_path(sid_dir, L"$I*").c_str(),
                                       &item);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::vector<std::uint8_t> data;
            if (!read_small_file(art::join_path(sid_dir, item.cFileName), data,
                                 1024 * 1024) || data.size() < 28)
                continue;
            const std::uint32_t name_chars = read_u32(data.data() + 24);
            if (name_chars == 0 || 28ull + static_cast<std::uint64_t>(name_chars) * 2 >
                data.size()) continue;
            std::wstring original_name(name_chars, L'\0');
            std::memcpy(&original_name[0], data.data() + 28,
                        static_cast<std::size_t>(name_chars) * 2);
            while (!original_name.empty() && original_name.back() == L'\0')
                original_name.pop_back();
            const std::wstring suffix = std::wstring(item.cFileName).substr(2);
            const std::wstring r_path = art::join_path(sid_dir, L"$R" + suffix);
            if (GetFileAttributesW(r_path.c_str()) == INVALID_FILE_ATTRIBUTES)
                continue;
            const std::wstring destination = art::join_path(
                art::join_path(out_root, sid_entry.cFileName),
                std::to_wstring(rows) + L"_" + safe_component(base_name(original_name)));
            art::ensure_directory(art::join_path(out_root, sid_entry.cFileName));
            if (!copy_tree(r_path, destination)) continue;
            ArtifactRow row;
            row.type = "recycle_recovered";
            row.source = art::to_utf8(r_path);
            row.key = art::to_utf8(suffix);
            row.path = art::to_utf8(destination);
            row.name = art::to_utf8(original_name);
            row.data = "{\"recycle_path\":" + art::json_escape_w(r_path) +
                ",\"recovered_path\":" + art::json_escape_w(destination) +
                ",\"sid\":" + art::json_escape_w(sid_entry.cFileName) + "}";
            sink(row);
            ++rows;
        } while (FindNextFileW(search, &item));
        FindClose(search);
    } while (FindNextFileW(sid_search, &sid_entry));
    FindClose(sid_search);
}
} // namespace

int collect_recycle(const ArtifactSink& sink, std::wstring& error) {
    (void)error;
    wchar_t drives[4096] = {};
    const DWORD length = GetLogicalDriveStringsW(4096, drives);
    if (length == 0 || length >= 4096) return 0;
    int rows = 0;
    for (const wchar_t* drive = drives; *drive != L'\0'; drive += wcslen(drive) + 1) {
        if (GetDriveTypeW(drive) != DRIVE_FIXED) continue;
        const std::wstring bin_root = std::wstring(drive) + L"$Recycle.Bin";
        if (GetFileAttributesW(bin_root.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        collect_bin(bin_root, sink, rows);
    }
    return rows;
}


int recover_recycle(const std::wstring& out_dir, const ArtifactSink& sink,
                    std::wstring& error) {
    if (out_dir.empty()) {
        error = L"recycle recover output is empty";
        return -1;
    }
    art::ensure_directory(out_dir);
    wchar_t drives[4096] = {};
    const DWORD length = GetLogicalDriveStringsW(4096, drives);
    if (length == 0 || length >= 4096) return 0;
    int rows = 0;
    for (const wchar_t* drive = drives; *drive != L'\0'; drive += wcslen(drive) + 1) {
        if (GetDriveTypeW(drive) != DRIVE_FIXED) continue;
        const std::wstring root = std::wstring(drive) + L"$Recycle.Bin";
        if (GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        recover_bin(root, out_dir, sink, rows);
    }
    return rows;
}
} // namespace deep
