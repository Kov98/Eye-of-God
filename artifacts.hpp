#pragma once

// Artifact collection interfaces shared by the registry, browser, EVTX,
// Recycle Bin and future parsers. Every collector emits rows through a sink;
// the orchestrator decides whether the destination is JSONL, SQLite or both.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <windows.h>

namespace deep {

struct ArtifactRow {
    std::string type;          // registry, browser_history, evtx, recycle, ...
    std::string source;        // hive path, browser DB, EVTX file, ...
    std::string key;           // record id, URL, value name, ...
    std::string timestamp_utc; // ISO-8601 or empty
    std::string path;          // original path / key path
    std::string name;          // display name
    std::uint64_t size = 0;
    std::string sha256;
    std::string data;          // JSON object (without outer row fields)
};

using ArtifactSink = std::function<void(const ArtifactRow&)>;

struct HiveValueRecord {
    std::wstring key_path;
    std::wstring value_name;
    std::uint32_t type = 0;
    std::vector<std::uint8_t> data;
    std::uint64_t last_write = 0;
};
using HiveValueSink = std::function<void(const HiveValueRecord&)>;

namespace art {

inline std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        &out[0], needed, nullptr, nullptr);
    return out;
}

inline std::wstring from_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        &out[0], needed);
    return out;
}

inline std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    out.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

inline std::string json_escape_w(const std::wstring& value) {
    return json_escape(to_utf8(value));
}

inline std::string filetime_iso(std::uint64_t ft) {
    if (ft == 0) return {};
    FILETIME file_time{};
    file_time.dwLowDateTime = static_cast<DWORD>(ft & 0xFFFFFFFFull);
    file_time.dwHighDateTime = static_cast<DWORD>(ft >> 32);
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&file_time, &st)) return {};
    const std::uint64_t fraction = ft % 10000000ull;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer),
        "%04u-%02u-%02uT%02u:%02u:%02u.%07lluZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        static_cast<unsigned long long>(fraction));
    return buffer;
}

inline std::string now_iso() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds);
    return buffer;
}

inline std::string utc_id() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04u%02u%02u_%02u%02u%02u_%03uZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds);
    return buffer;
}

inline std::wstring join_path(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

inline bool ensure_directory(const std::wstring& path) {
    if (path.empty()) return false;
    std::wstring current;
    for (std::size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == L'\\' || path[i] == L'/') {
            if (current.size() <= 3) continue;
            if (GetFileAttributesW(current.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(current.c_str(), nullptr) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) return false;
            }
        }
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(path.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    return true;
}

inline std::wstring extend_path(const std::wstring& path) {
    if (path.size() < 240 || path.rfind(L"\\\\?\\", 0) == 0) return path;
    if (path.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}

} // namespace art

// Each collector returns the number of rows emitted, or -1 on failure.
int collect_registry(const ArtifactSink& sink, std::wstring& error);
int collect_browser(const ArtifactSink& sink, std::wstring& error);
int collect_browser_secrets(const std::wstring& user_data_dir,
                            const ArtifactSink& sink, std::wstring& error);
int collect_recycle(const ArtifactSink& sink, std::wstring& error);
int recover_recycle(const std::wstring& out_dir, const ArtifactSink& sink,
                    std::wstring& error);
int collect_lnk(const ArtifactSink& sink, std::wstring& error);
int collect_jumplist(const ArtifactSink& sink, std::wstring& error);
int collect_shellbags(const ArtifactSink& sink, std::wstring& error);
int collect_prefetch_file(const std::wstring& file, const ArtifactSink& sink,
                          std::wstring& error);
int collect_prefetch_directory(const std::wstring& directory,
                               const ArtifactSink& sink, std::wstring& error);
int collect_hive_file(const std::wstring& file, bool recover_deleted,
                      const ArtifactSink& sink, std::wstring& error);
int collect_hive_directory(const std::wstring& directory, bool recover_deleted,
                           const ArtifactSink& sink, std::wstring& error);
int enumerate_hive_values(const std::wstring& file, const HiveValueSink& sink,
                          std::wstring& error);
int collect_shimcache_file(const std::wstring& file, const ArtifactSink& sink,
                           std::wstring& error);
int collect_shimcache_hive(const std::wstring& hive, const ArtifactSink& sink,
                           std::wstring& error);
int collect_amcache(const std::wstring& hive_file, const ArtifactSink& sink,
                    std::wstring& error);
int collect_srum(const std::wstring& database, const ArtifactSink& sink,
                 std::wstring& error);
int collect_bam(const std::wstring& hive_file, const ArtifactSink& sink,
                std::wstring& error);
int collect_evtx_file(const std::wstring& file, const ArtifactSink& sink,
                      std::wstring& error);
int collect_evtx_directory(const std::wstring& directory,
                           const ArtifactSink& sink, std::wstring& error);

struct ArtifactOptions {
    std::wstring out_dir;
    std::wstring db_path;
    bool registry = true;
    bool browser = true;
    bool browser_secrets = false;
    std::wstring browser_profile;
    bool evtx = true;
    bool recycle = true;
    bool lnk = true;
    bool jumplist = true;
    bool prefetch = true;
    bool shellbags = true;
    std::wstring prefetch_dir;
    std::wstring hive_file;
    std::wstring hive_dir;
    bool hive_deleted = true;
    std::wstring amcache_file;
    std::wstring shimcache_file;
    std::wstring shimcache_hive;
    std::wstring srum_db;
    std::wstring bam_hive;
    std::wstring evtx_file;
    std::wstring evtx_dir;
};

int run_artifacts(const ArtifactOptions& options);

} // namespace deep
