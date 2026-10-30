// language: C++, file: wipe_hive.cpp, target: Windows 10/11
// Offline regf sanitizer: zeroes every free hbin cell so deleted keys, values
// and old data blobs do not survive in the hive slack. Never touches allocated
// cells. Intended for an offline copy, never for a mounted live hive.

#include "wipe_hive.hpp"

#include "artifacts.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <cstdio>
#include <string>
#include <vector>

namespace deep {
namespace {

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

bool read_file(const std::wstring& path, std::vector<std::uint8_t>& data,
               std::wstring& error) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"open failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0x1000 ||
        size.QuadPart > 1024ll * 1024 * 1024) {
        error = L"size invalid";
        CloseHandle(file);
        return false;
    }
    data.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()),
                             &read, nullptr) && read == data.size();
    CloseHandle(file);
    if (!ok) error = L"read failed";
    return ok;
}

bool write_file(const std::wstring& path, const std::vector<std::uint8_t>& data,
                std::wstring& error) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"write open failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()),
                              &written, nullptr) && written == data.size();
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) error = L"write failed";
    return ok;
}

std::wstring default_audit_dir() {
    wchar_t cwd[32768] = {};
    if (GetCurrentDirectoryW(32768, cwd) == 0) return L".";
    wchar_t stamp[64];
    SYSTEMTIME st{};
    GetSystemTime(&st);
    std::swprintf(stamp, 64, L"wipe_%04u%02u%02u_%02u%02u%02u",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return art::join_path(cwd, std::wstring(L"cases\\") + stamp);
}

} // namespace

int sanitize_hive(const std::wstring& hive_path, const std::wstring& out_dir,
                  bool confirm) {
    if (!confirm) {
        std::cerr << "wipe-hive: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    std::vector<std::uint8_t> data;
    std::wstring error;
    if (!read_file(hive_path, data, error)) {
        std::cerr << "wipe-hive: " << art::to_utf8(error) << "\n";
        return 1;
    }
    if (std::memcmp(data.data(), "regf", 4) != 0) {
        std::cerr << "wipe-hive: not a regf hive\n";
        return 1;
    }
    std::uint32_t position = 0x1000;
    std::uint64_t cells = 0;
    std::uint64_t bytes = 0;
    while (position + 32 < data.size()) {
        if (std::memcmp(data.data() + position, "hbin", 4) != 0) break;
        const std::uint32_t block_size = read_u32(data.data() + position + 8);
        if (block_size < 32 || position + block_size > data.size()) break;
        std::uint32_t cell = position + 32;
        const std::uint32_t end = position + block_size;
        while (cell + 4 < end) {
            const std::int32_t signed_size = static_cast<std::int32_t>(
                read_u32(data.data() + cell));
            if (signed_size == 0) break;
            const std::uint32_t cell_size = static_cast<std::uint32_t>(
                signed_size < 0 ? -static_cast<std::int64_t>(signed_size)
                                : static_cast<std::int64_t>(signed_size));
            if (cell_size < 8 || cell + cell_size > end) break;
            if (signed_size > 0) {
                std::memset(data.data() + cell + 4, 0, cell_size - 4);
                ++cells;
                bytes += cell_size - 4;
            }
            cell += cell_size;
        }
        position += block_size;
    }
    if (!write_file(hive_path, data, error)) {
        std::cerr << "wipe-hive: " << art::to_utf8(error) << "\n";
        return 1;
    }
    const std::wstring audit_dir = out_dir.empty() ? default_audit_dir() : out_dir;
    art::ensure_directory(audit_dir);
    const std::wstring audit_path = art::join_path(audit_dir,
        L"hive_sanitize.jsonl");
    HANDLE audit = CreateFileW(audit_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit != INVALID_HANDLE_VALUE) {
        const std::string line = "{\"action\":\"wipe_hive\",\"hive\":" +
            art::json_escape_w(hive_path) + ",\"free_cells\":" +
            std::to_string(cells) + ",\"bytes_zeroed\":" +
            std::to_string(bytes) + ",\"completed_utc\":" +
            art::json_escape(art::now_iso()) + "}";
        const std::string text = line + "\r\n";
        DWORD written = 0;
        WriteFile(audit, text.data(), static_cast<DWORD>(text.size()), &written,
                  nullptr);
        CloseHandle(audit);
    }
    std::cout << "wipe-hive: free_cells=" << cells << " bytes=" << bytes << "\n";
    std::cout << "wipe-hive: audit=" << art::to_utf8(audit_dir) << "\n";
    return 0;
}

} // namespace deep
