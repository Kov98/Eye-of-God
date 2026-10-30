// language: C++, file: wipe_browser.cpp, target: Windows 10/11
// Overwrites and removes browser profile state. Explicit profile paths are
// treated as offline copies; default Chrome/Edge profiles are refused while
// the browser process is running.

#include "wipe_browser.hpp"

#include "artifacts.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <cstdio>
#include <string>
#include <vector>

namespace deep {
namespace {

struct Stats {
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
};

bool browser_running() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"chrome.exe") == 0 ||
                _wcsicmp(entry.szExeFile, L"msedge.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool overwrite_and_delete(const std::wstring& path, Stats& stats,
                          std::wstring& error) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"open failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        error = L"size failed";
        return false;
    }
    std::vector<std::uint8_t> zeros(1024 * 1024, 0);
    std::uint64_t remaining = static_cast<std::uint64_t>(size.QuadPart);
    std::uint64_t written_total = 0;
    SetFilePointer(file, 0, nullptr, FILE_BEGIN);
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(
            (std::min)(remaining, static_cast<std::uint64_t>(zeros.size())));
        DWORD written = 0;
        if (!WriteFile(file, zeros.data(), chunk, &written, nullptr) ||
            written != chunk) {
            CloseHandle(file);
            error = L"overwrite failed";
            return false;
        }
        remaining -= chunk;
        written_total += chunk;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!DeleteFileW(art::extend_path(path).c_str())) {
        error = L"delete failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    stats.files++;
    stats.bytes += written_total;
    return true;
}

void wipe_tree(const std::wstring& path, Stats& stats, std::wstring& error) {
    const DWORD attributes = GetFileAttributesW(art::extend_path(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        overwrite_and_delete(path, stats, error);
        return;
    }
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(art::join_path(path, L"*").c_str(), &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(entry.cFileName, L".") == 0 ||
                wcscmp(entry.cFileName, L"..") == 0) continue;
            wipe_tree(art::join_path(path, entry.cFileName), stats, error);
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    RemoveDirectoryW(art::extend_path(path).c_str());
}

std::wstring default_audit_dir() {
    wchar_t cwd[32768] = {};
    if (GetCurrentDirectoryW(32768, cwd) == 0) return L".";
    wchar_t stamp[64];
    SYSTEMTIME st{};
    GetSystemTime(&st);
    std::swprintf(stamp, 64, L"wipe_browser_%04u%02u%02u_%02u%02u%02u",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return art::join_path(cwd, std::wstring(L"cases\\") + stamp);
}

} // namespace

int wipe_browser(const std::wstring& user_data_dir, const std::wstring& out_dir,
                 bool confirm) {
    if (!confirm) {
        std::cerr << "wipe-browser: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    std::vector<std::wstring> roots;
    const bool explicit_path = !user_data_dir.empty();
    if (explicit_path) {
        roots.push_back(user_data_dir);
    } else {
        if (browser_running()) {
            std::cerr << "wipe-browser: chrome.exe/msedge.exe is running; close it first\n";
            return 1;
        }
        wchar_t local[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) != 0) {
            roots.push_back(art::join_path(local, L"Google\\Chrome\\User Data"));
            roots.push_back(art::join_path(local, L"Microsoft\\Edge\\User Data"));
        }
    }
    Stats stats;
    std::wstring error;
    std::vector<std::wstring> targets;
    for (const auto& root : roots) {
        if (GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        targets.push_back(art::join_path(root, L"Local State"));
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW(art::join_path(root, L"*").c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(entry.cFileName, L".") == 0 ||
                wcscmp(entry.cFileName, L"..") == 0 ||
                (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            const std::wstring name = entry.cFileName;
            if (_wcsicmp(name.c_str(), L"Default") != 0 &&
                _wcsnicmp(name.c_str(), L"Profile ", 8) != 0)
                continue;
            const std::wstring profile = art::join_path(root, name);
            const wchar_t* files[] = {
                L"History", L"History-journal", L"Cookies", L"Cookies-journal",
                L"Login Data", L"Login Data-journal", L"Web Data",
                L"Web Data-journal", L"Top Sites", L"Top Sites-journal",
                L"Visited Links", L"Shortcuts", L"Shortcuts-journal",
                L"Preferences", L"Secure Preferences", L"Bookmarks",
                L"Bookmarks.bak", L"Network\\Cookies",
                L"Network\\Cookies-journal", L"Network\\TransportSecurity"
            };
            for (const wchar_t* file : files)
                targets.push_back(art::join_path(profile, file));
            const wchar_t* dirs[] = {
                L"Local Storage", L"Session Storage", L"IndexedDB", L"Cache",
                L"Code Cache", L"GPUCache", L"Service Worker", L"Media Cache",
                L"Extension State", L"Sync Data"
            };
            for (const wchar_t* dir : dirs)
                targets.push_back(art::join_path(profile, dir));
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    for (const auto& target : targets) wipe_tree(target, stats, error);
    const std::wstring audit_dir = out_dir.empty() ? default_audit_dir() : out_dir;
    art::ensure_directory(audit_dir);
    const std::wstring audit_path = art::join_path(audit_dir,
        L"browser_sanitize.jsonl");
    HANDLE audit = CreateFileW(audit_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit != INVALID_HANDLE_VALUE) {
        const std::string line = "{\"action\":\"wipe_browser\",\"roots\":" +
            std::to_string(roots.size()) + ",\"files\":" +
            std::to_string(stats.files) + ",\"bytes\":" +
            std::to_string(stats.bytes) + ",\"completed_utc\":" +
            art::json_escape(art::now_iso()) + "}";
        const std::string text = line + "\r\n";
        DWORD written = 0;
        WriteFile(audit, text.data(), static_cast<DWORD>(text.size()), &written,
                  nullptr);
        CloseHandle(audit);
    }
    std::cout << "wipe-browser: files=" << stats.files
              << " bytes=" << stats.bytes << "\n";
    std::cout << "wipe-browser: audit=" << art::to_utf8(audit_dir) << "\n";
    return stats.files == 0 ? 1 : 0;
}

} // namespace deep
