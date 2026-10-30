// language: C++, file: artifact_lnk.cpp, target: Windows 10/11
// Recent shortcut parser. Reads .lnk files with IShellLinkW without resolving
// targets over the network.

#include "artifacts.hpp"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace deep {
namespace {

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

bool read_file_bytes(const std::wstring& path, std::vector<std::uint8_t>& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0x4C ||
        size.QuadPart > 16ll * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    data.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()),
                             &read, nullptr) && read == data.size();
    CloseHandle(file);
    return ok;
}

// Resolve the LinkTargetIDList through the shell. This recovers targets that
// IShellLinkW::GetPath leaves empty (network paths, removed drives, some
// packaged apps) without touching the network itself.
std::wstring shell_target_from_lnk(const std::wstring& path) {
    std::vector<std::uint8_t> data;
    if (!read_file_bytes(path, data)) return {};
    if (std::memcmp(data.data(), "L\x00\x00\x00", 4) != 0) return {};
    const std::uint32_t flags = read_u32(data.data() + 0x14);
    if ((flags & 0x00000001) == 0) return {};
    std::size_t position = 0x4C;
    if (position + 2 > data.size()) return {};
    const std::uint16_t idlist_size = read_u16(data.data() + position);
    position += 2;
    if (idlist_size < 2 || position + idlist_size > data.size()) return {};
    void* pidl = CoTaskMemAlloc(idlist_size);
    if (pidl == nullptr) return {};
    std::memcpy(pidl, data.data() + position, idlist_size);
    std::wstring result;
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromIDList(
            static_cast<PCIDLIST_ABSOLUTE>(pidl), IID_IShellItem,
            reinterpret_cast<void**>(&item))) && item != nullptr) {
        LPWSTR name = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name)) &&
            name != nullptr) {
            result.assign(name);
            CoTaskMemFree(name);
        }
        item->Release();
    }
    CoTaskMemFree(pidl);
    return result;
}

std::wstring shortcut_target(IShellLinkW* link) {
    std::vector<wchar_t> buffer(32768, L'\0');
    if (SUCCEEDED(link->GetPath(buffer.data(), static_cast<int>(buffer.size()),
                                nullptr, SLGP_RAWPATH))) {
        return std::wstring(buffer.data());
    }
    return {};
}

} // namespace

int collect_lnk(const ArtifactSink& sink, std::wstring& error) {
    wchar_t recent[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_RECENT, nullptr,
                                SHGFP_TYPE_CURRENT, recent))) {
        error = L"CSIDL_RECENT unavailable";
        return -1;
    }
    const HRESULT initialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE) {
        error = L"COM initialization failed";
        return -1;
    }
    const bool uninitialize = SUCCEEDED(initialization);
    const std::wstring pattern = art::join_path(recent, L"*.lnk");
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        if (uninitialize) CoUninitialize();
        return 0;
    }
    int rows = 0;
    do {
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring path = art::join_path(recent, entry.cFileName);
        IShellLinkW* link = nullptr;
        HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
            CLSCTX_INPROC_SERVER, IID_IShellLinkW,
            reinterpret_cast<void**>(&link));
        std::wstring target, arguments, working_directory, description, icon_path;
        int icon_index = 0;
        int show_command = 0;
        WORD hotkey = 0;
        if (SUCCEEDED(result)) {
            IPersistFile* persist = nullptr;
            result = link->QueryInterface(IID_IPersistFile,
                                          reinterpret_cast<void**>(&persist));
            if (SUCCEEDED(result)) {
                result = persist->Load(path.c_str(), STGM_READ);
                if (SUCCEEDED(result)) {
                    target = shortcut_target(link);
                    std::vector<wchar_t> buffer(32768, L'\0');
                    if (SUCCEEDED(link->GetArguments(buffer.data(),
                        static_cast<int>(buffer.size())))) {
                        arguments.assign(buffer.data());
                    }
                    std::fill(buffer.begin(), buffer.end(), L'\0');
                    if (SUCCEEDED(link->GetWorkingDirectory(buffer.data(),
                        static_cast<int>(buffer.size())))) {
                        working_directory.assign(buffer.data());
                    }
                    std::fill(buffer.begin(), buffer.end(), L'\0');
                    if (SUCCEEDED(link->GetDescription(buffer.data(),
                        static_cast<int>(buffer.size())))) {
                        description.assign(buffer.data());
                    }
                    std::fill(buffer.begin(), buffer.end(), L'\0');
                    if (SUCCEEDED(link->GetIconLocation(buffer.data(),
                        static_cast<int>(buffer.size()), &icon_index))) {
                        icon_path.assign(buffer.data());
                    }
                    link->GetShowCmd(&show_command);
                    link->GetHotkey(&hotkey);
                }
                persist->Release();
            }
            link->Release();
        }
        const std::wstring shell_target = shell_target_from_lnk(path);
        const bool from_shell = target.empty() && !shell_target.empty();
        if (from_shell) target = shell_target;
        std::uint64_t target_size = 0;
        bool target_exists = false;
        WIN32_FILE_ATTRIBUTE_DATA target_info{};
        if (!target.empty() &&
            GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &target_info)) {
            target_exists = true;
            target_size = (static_cast<std::uint64_t>(target_info.nFileSizeHigh) << 32) |
                target_info.nFileSizeLow;
        }
        const std::uint64_t shortcut_ft =
            (static_cast<std::uint64_t>(entry.ftLastWriteTime.dwHighDateTime) << 32) |
            entry.ftLastWriteTime.dwLowDateTime;
        const std::size_t slash = std::wstring(entry.cFileName).find_last_of(L'\\');
        const std::wstring file_name = slash == std::wstring::npos
            ? entry.cFileName : std::wstring(entry.cFileName).substr(slash + 1);

        ArtifactRow row;
        row.type = "lnk";
        row.source = art::to_utf8(recent);
        row.key = art::to_utf8(entry.cFileName);
        row.timestamp_utc = art::filetime_iso(shortcut_ft);
        row.path = art::to_utf8(path);
        row.name = target.empty() ? art::to_utf8(file_name) : art::to_utf8(target);
        row.size = target_size;
        row.data = "{\"shortcut_name\":" + art::json_escape_w(file_name) +
            ",\"target_path\":" + art::json_escape_w(target) +
            ",\"shell_target\":" + art::json_escape_w(shell_target) +
            ",\"target_source\":" + art::json_escape(
                target.empty() ? "none" : (from_shell
                    ? "shell_item" : "lnk")) +
            ",\"target_exists\":" + (target_exists ? "true" : "false") +
            ",\"target_size\":" + std::to_string(target_size) +
            ",\"arguments\":" + art::json_escape_w(arguments) +
            ",\"working_directory\":" + art::json_escape_w(working_directory) +
            ",\"description\":" + art::json_escape_w(description) +
            ",\"icon_path\":" + art::json_escape_w(icon_path) +
            ",\"icon_index\":" + std::to_string(icon_index) +
            ",\"show_command\":" + std::to_string(show_command) +
            ",\"hotkey\":" + std::to_string(hotkey) +
            ",\"parse_status\":" +
            art::json_escape(SUCCEEDED(result) ? "parsed" : "parse_failed") + "}";
        sink(row);
        ++rows;
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    if (uninitialize) CoUninitialize();
    return rows;
}

} // namespace deep
