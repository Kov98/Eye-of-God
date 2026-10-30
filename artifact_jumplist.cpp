// language: C++, file: artifact_jumplist.cpp, target: Windows 10/11
// Jump List collector. Automatic and custom destinations are compound files;
// numbered streams carry embedded shell links and DestList carries the
// MRU metadata. This parses both without touching the live list.

#include "artifacts.hpp"

#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

namespace deep {
namespace {

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | p[i];
    return value;
}

std::string hex_bytes(const std::uint8_t* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    return out;
}

std::wstring stream_name_to_string(LPOLESTR name) {
    if (name == nullptr) return {};
    return std::wstring(name);
}

std::wstring get_link_text(HRESULT (IShellLinkW::*getter)(LPWSTR, int),
                           IShellLinkW* link) {
    std::vector<wchar_t> buffer(32768, L'\0');
    if (SUCCEEDED((link->*getter)(buffer.data(),
                                  static_cast<int>(buffer.size())))) {
        return std::wstring(buffer.data());
    }
    return {};
}

void process_stream(const std::wstring& file, const std::wstring& app_id,
                    const std::wstring& name, const std::uint8_t* data,
                    std::size_t size, std::uint64_t timestamp,
                    const ArtifactSink& sink, int& rows) {
    if (_wcsicmp(name.c_str(), L"DestList") == 0) {
        std::uint32_t version = 0, entries = 0, pinned = 0;
        if (size >= 12) {
            version = read_u32(data);
            entries = read_u32(data + 4);
            pinned = read_u32(data + 8);
        }
        ArtifactRow header;
        header.type = "jumplist_destlist_header";
        header.source = art::to_utf8(file);
        header.key = art::to_utf8(app_id + L":DestList");
        header.timestamp_utc = art::filetime_iso(timestamp);
        header.path = art::to_utf8(file);
        header.name = "DestList";
        header.size = size;
        header.data = "{\"app_id\":" + art::json_escape_w(app_id) +
            ",\"version\":" + std::to_string(version) +
            ",\"entries\":" + std::to_string(entries) +
            ",\"pinned\":" + std::to_string(pinned) +
            ",\"size\":" + std::to_string(size) + "}";
        sink(header);
        ++rows;

        std::size_t position = 32;
        std::uint32_t emitted = 0;
        while (position + 112 <= size && emitted < 10000 &&
               (entries == 0 || emitted < entries)) {
            const std::size_t entry_start = position;
            position += 8; // unknown
            if (position + 64 > size) break;
            const std::string volume_droid = hex_bytes(data + position, 16);
            const std::string file_droid = hex_bytes(data + position + 16, 16);
            const std::string volume_birth = hex_bytes(data + position + 32, 16);
            const std::string file_birth = hex_bytes(data + position + 48, 16);
            position += 64;
            if (position + 16 > size) break;
            std::string hostname(reinterpret_cast<const char*>(data + position), 16);
            while (!hostname.empty() &&
                   (hostname.back() == '\0' || hostname.back() == ' '))
                hostname.pop_back();
            position += 16;
            if (position + 4 > size) break;
            const std::uint32_t entry_number = read_u32(data + position);
            position += 4;
            position += 8; // unknown
            if (position + 8 > size) break;
            const std::uint64_t mtime = read_u64(data + position);
            position += 8;
            if (position + 4 > size) break;
            const std::uint32_t pin = read_u32(data + position);
            position += 4;
            if (version > 1) position += 16;
            if (position + 2 > size) break;
            const std::uint16_t path_chars = read_u16(data + position);
            position += 2;
            if (position + static_cast<std::size_t>(path_chars) * 2 > size) break;
            std::wstring path(path_chars, L'\0');
            if (path_chars > 0)
                std::memcpy(&path[0], data + position, path_chars * 2);
            position += static_cast<std::size_t>(path_chars) * 2;
            if (version > 1) position += 4;
            while (!path.empty() && path.back() == L'\0') path.pop_back();

            ArtifactRow row;
            row.type = "jumplist_destlist";
            row.source = art::to_utf8(file);
            row.key = art::to_utf8(app_id) + ":" + std::to_string(entry_number);
            row.timestamp_utc = art::filetime_iso(mtime);
            row.path = art::to_utf8(file);
            row.name = path.empty() ? "DestList entry" : art::to_utf8(path);
            row.size = position - entry_start;
            row.data = "{\"app_id\":" + art::json_escape_w(app_id) +
                ",\"version\":" + std::to_string(version) +
                ",\"entry_number\":" + std::to_string(entry_number) +
                ",\"path\":" + art::json_escape_w(path) +
                ",\"hostname\":" + art::json_escape(hostname) +
                ",\"pinned\":" + (pin == 0xFFFFFFFF ? "false" : "true") +
                ",\"mtime_utc\":" + art::json_escape(art::filetime_iso(mtime)) +
                ",\"volume_droid\":" + art::json_escape(volume_droid) +
                ",\"file_droid\":" + art::json_escape(file_droid) +
                ",\"volume_birth_droid\":" + art::json_escape(volume_birth) +
                ",\"file_birth_droid\":" + art::json_escape(file_birth) +
                ",\"entry_offset\":" + std::to_string(entry_start) + "}";
            sink(row);
            ++rows;
            ++emitted;
        }
        return;
    }

    IStream* memory = SHCreateMemStream(data, static_cast<UINT>(size));
    if (memory == nullptr) return;
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr,
        CLSCTX_INPROC_SERVER, IID_IShellLinkW,
        reinterpret_cast<void**>(&link));
    std::wstring target, arguments, working_directory, description;
    std::string status = "parsed";
    if (SUCCEEDED(result)) {
        IPersistStream* persist = nullptr;
        result = link->QueryInterface(IID_IPersistStream,
                                      reinterpret_cast<void**>(&persist));
        if (SUCCEEDED(result)) {
            result = persist->Load(memory);
            if (SUCCEEDED(result)) {
                std::vector<wchar_t> buffer(32768, L'\0');
                if (SUCCEEDED(link->GetPath(buffer.data(),
                    static_cast<int>(buffer.size()), nullptr, SLGP_RAWPATH))) {
                    target.assign(buffer.data());
                }
                arguments = get_link_text(&IShellLinkW::GetArguments, link);
                working_directory = get_link_text(
                    &IShellLinkW::GetWorkingDirectory, link);
                description = get_link_text(&IShellLinkW::GetDescription, link);
            } else {
                status = "lnk_load_failed";
            }
            persist->Release();
        } else {
            status = "persist_stream_unavailable";
        }
        link->Release();
    } else {
        status = "shell_link_unavailable";
    }
    memory->Release();

    ArtifactRow row;
    row.type = "jumplist";
    row.source = art::to_utf8(file);
    row.key = art::to_utf8(app_id + L":" + name);
    row.timestamp_utc = art::filetime_iso(timestamp);
    row.path = art::to_utf8(file);
    row.name = target.empty() ? art::to_utf8(name) : art::to_utf8(target);
    row.size = size;
    row.data = "{\"app_id\":" + art::json_escape_w(app_id) +
        ",\"stream\":" + art::json_escape_w(name) +
        ",\"target_path\":" + art::json_escape_w(target) +
        ",\"arguments\":" + art::json_escape_w(arguments) +
        ",\"working_directory\":" + art::json_escape_w(working_directory) +
        ",\"description\":" + art::json_escape_w(description) +
        ",\"status\":" + art::json_escape(status) + "}";
    sink(row);
    ++rows;
}

void process_file(const std::wstring& path, const ArtifactSink& sink, int& rows) {
    const std::size_t name_start = path.find_last_of(L"\\/");
    const std::wstring file_name = name_start == std::wstring::npos
        ? path : path.substr(name_start + 1);
    const std::size_t dot = file_name.find(L'.');
    const std::wstring app_id = dot == std::wstring::npos
        ? file_name : file_name.substr(0, dot);

    IStorage* storage = nullptr;
    HRESULT result = StgOpenStorageEx(path.c_str(),
        STGM_READ | STGM_SHARE_DENY_WRITE, STGFMT_STORAGE, 0, nullptr, nullptr,
        IID_IStorage, reinterpret_cast<void**>(&storage));
    if (FAILED(result)) {
        result = StgOpenStorageEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
            STGFMT_FILE, 0, nullptr, nullptr, IID_IStorage,
            reinterpret_cast<void**>(&storage));
    }
    if (FAILED(result) || storage == nullptr) return;

    IEnumSTATSTG* enumerator = nullptr;
    if (FAILED(storage->EnumElements(0, nullptr, 0, &enumerator)) ||
        enumerator == nullptr) {
        storage->Release();
        return;
    }
    STATSTG stat{};
    while (enumerator->Next(1, &stat, nullptr) == S_OK) {
        if (stat.type == STGTY_STREAM && stat.pwcsName != nullptr) {
            const std::wstring stream_name = stream_name_to_string(stat.pwcsName);
            const std::uint64_t timestamp =
                (static_cast<std::uint64_t>(stat.mtime.dwHighDateTime) << 32) |
                stat.mtime.dwLowDateTime;
            const ULONGLONG size = stat.cbSize.QuadPart;
            if (size > 0 && size < 16ull * 1024ull * 1024ull) {
                IStream* stream = nullptr;
                if (SUCCEEDED(storage->OpenStream(stream_name.c_str(), nullptr,
                    STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stream)) &&
                    stream != nullptr) {
                    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
                    ULONG read = 0;
                    if (SUCCEEDED(stream->Read(data.data(),
                        static_cast<ULONG>(data.size()), &read))) {
                        data.resize(read);
                        process_stream(path, app_id, stream_name, data.data(),
                                       data.size(), timestamp, sink, rows);
                    }
                    stream->Release();
                }
            }
        }
        CoTaskMemFree(stat.pwcsName);
        stat = STATSTG{};
    }
    enumerator->Release();
    storage->Release();
}

} // namespace

int collect_jumplist(const ArtifactSink& sink, std::wstring& error) {
    wchar_t appdata[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr,
                                SHGFP_TYPE_CURRENT, appdata))) {
        error = L"CSIDL_APPDATA unavailable";
        return -1;
    }
    const HRESULT initialization = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE) {
        error = L"COM initialization failed";
        return -1;
    }
    const bool uninitialize = SUCCEEDED(initialization);
    const std::wstring recent = art::join_path(appdata,
        L"Microsoft\\Windows\\Recent");
    const std::wstring folders[] = {
        art::join_path(recent, L"AutomaticDestinations"),
        art::join_path(recent, L"CustomDestinations")
    };
    int rows = 0;
    for (const auto& folder : folders) {
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW(art::join_path(folder, L"*").c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = entry.cFileName;
            for (wchar_t& c : name) c = static_cast<wchar_t>(std::towlower(c));
            if (name.find(L"destinations-ms") == std::wstring::npos) continue;
            process_file(art::join_path(folder, entry.cFileName), sink, rows);
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    if (uninitialize) CoUninitialize();
    return rows;
}

} // namespace deep
