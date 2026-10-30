// Deep acquisition / recovery / sanitization engine.
// language: C++, file: deep.cpp, target: Windows 10/11, g++ UCRT64 / MSVC
//
// Read paths never write to the source. Destructive paths require the exact
// token WIPE and audit every action to JSONL.

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winioctl.h>
#include <bcrypt.h>
#include <winevt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cwctype>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "deep.hpp"
#include "artifacts.hpp"
#include "case_db.hpp"
#include "imaging.hpp"
#include "manifest_sign.hpp"
#include "wipe_hive.hpp"
#include "wipe_browser.hpp"
#include "aff4.hpp"
#include "sanitize_disk.hpp"
#include "gui.hpp"
#include "kernel_client.hpp"
#include "e01.hpp"
#include "exposure.hpp"
#include "artifact_vss.hpp"
#include "cloud/cli/auth_flow.hpp"
#include "cloud/cli/collect_flow.hpp"
#include "cloud/core/manifest_verify.hpp"
#include "cloud/enterprise/secret_store.hpp"
#include "third_party/nlohmann/json.hpp"
#include "cloud/db/cloud_db_import.hpp"
#include "cloud/report/cloud_report.hpp"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace deep {
namespace {

constexpr uint32_t kAttrStandardInformation = 0x10;
constexpr uint32_t kAttrAttributeList = 0x20;
constexpr uint32_t kAttrFileName = 0x30;
constexpr uint32_t kAttrObjectId = 0x40;
constexpr uint32_t kAttrData = 0x80;
constexpr uint32_t kAttrIndexRoot = 0x90;
constexpr uint32_t kAttrIndexAllocation = 0xA0;
constexpr uint32_t kAttrBitmap = 0xB0;
constexpr uint32_t kAttrReparsePoint = 0xC0;
constexpr uint32_t kAttrEaInformation = 0xD0;
constexpr uint32_t kAttrEa = 0xE0;
constexpr uint32_t kAttrLoggedUtilityStream = 0x100;
constexpr uint32_t kAttrEnd = 0xFFFFFFFF;

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        &result[0], needed, nullptr, nullptr);
    return result;
}

std::wstring from_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        &result[0], needed);
    return result;
}

std::string json_escape(const std::string& value) {
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

std::string json_escape_w(const std::wstring& value) {
    return json_escape(to_utf8(value));
}

std::string hex64(uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%016llx",
        static_cast<unsigned long long>(value));
    return buffer;
}

std::string hex_bytes(const std::vector<uint8_t>& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.resize(data.size() * 2);
    for (std::size_t i = 0; i < data.size(); ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    return out;
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t read_u64(const uint8_t* p) {
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | p[i];
    return value;
}

std::string format_filetime(uint64_t ft) {
    if (ft == 0) return {};
    FILETIME file_time{};
    file_time.dwLowDateTime = static_cast<DWORD>(ft & 0xFFFFFFFFull);
    file_time.dwHighDateTime = static_cast<DWORD>(ft >> 32);
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&file_time, &st)) return {};
    const uint64_t fraction = ft % 10000000ull;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer),
        "%04u-%02u-%02uT%02u:%02u:%02u.%07lluZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        static_cast<unsigned long long>(fraction));
    return buffer;
}

std::string current_utc_id() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04u%02u%02u_%02u%02u%02u_%03uZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds);
    return buffer;
}

std::wstring current_utc_iso() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds);
    return buffer;
}

bool ensure_directory(const std::wstring& path) {
    if (path.empty()) return false;
    std::wstring current;
    for (std::size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == L'\\' || path[i] == L'/') {
            if (current.size() <= 3) continue;
            if (GetFileAttributesW(current.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(current.c_str(), nullptr) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                    return false;
                }
            }
        }
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(path.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    return true;
}

std::wstring join_path(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

std::wstring extend_path(const std::wstring& path) {
    if (path.size() < 240) return path;
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    if (path.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}

bool write_all(HANDLE file, const void* data, std::size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (size > 0) {
        const DWORD chunk = static_cast<DWORD>(
            (std::min)(size, static_cast<std::size_t>(1u << 20)));
        DWORD written = 0;
        if (!WriteFile(file, p, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        p += written;
        size -= written;
    }
    return true;
}

bool write_new_file(const std::wstring& path, const void* data, std::size_t size,
                    bool overwrite = false) {
    HANDLE file = CreateFileW(extend_path(path).c_str(), GENERIC_WRITE, 0, nullptr,
        overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool ok = write_all(file, data, size);
    FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

HANDLE create_jsonl(const std::wstring& path) {
    return CreateFileW(extend_path(path).c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool append_jsonl(HANDLE file, const std::string& line) {
    if (file == INVALID_HANDLE_VALUE) return false;
    const std::string with_newline = line + "\r\n";
    return write_all(file, with_newline.data(), with_newline.size());
}

bool sha256_file(const std::wstring& path, std::string& out_hex) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> object;
    std::vector<uint8_t> value;
    bool ok = false;
    HANDLE file = INVALID_HANDLE_VALUE;
    do {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm,
            BCRYPT_SHA256_ALGORITHM, nullptr, 0))) break;
        DWORD object_len = 0, result_len = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_len), sizeof(object_len), &result_len, 0)))
            break;
        DWORD hash_len = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_len), sizeof(hash_len), &result_len, 0)))
            break;
        object.resize(object_len);
        value.resize(hash_len);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(),
            object_len, nullptr, 0, 0))) break;
        file = CreateFileW(extend_path(path).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) break;
        std::vector<uint8_t> buffer(256 * 1024);
        ok = true;
        while (true) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                &read, nullptr)) {
                ok = false;
                break;
            }
            if (read == 0) break;
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0))) {
                ok = false;
                break;
            }
        }
        if (ok && !BCRYPT_SUCCESS(BCryptFinishHash(hash, value.data(), hash_len, 0)))
            ok = false;
        if (ok) out_hex = hex_bytes(value);
    } while (false);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool sha256_buffer(const void* data, std::size_t size, std::string& out_hex) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> object, value;
    bool ok = false;
    do {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm,
            BCRYPT_SHA256_ALGORITHM, nullptr, 0))) break;
        DWORD object_len = 0, result_len = 0, hash_len = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_len), sizeof(object_len), &result_len, 0)))
            break;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_len), sizeof(hash_len), &result_len, 0)))
            break;
        object.resize(object_len);
        value.resize(hash_len);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(),
            object_len, nullptr, 0, 0))) break;
        if (!BCRYPT_SUCCESS(BCryptHashData(hash,
            const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
            static_cast<ULONG>(size), 0))) break;
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, value.data(), hash_len, 0))) break;
        out_hex = hex_bytes(value);
        ok = true;
    } while (false);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

std::wstring sanitize_component(const std::wstring& name) {
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t c : name) {
        if (c < 0x20 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*') {
            out.push_back(L'_');
        } else {
            out.push_back(c);
        }
    }
    while (!out.empty() && (out.back() == L'.' || out.back() == L' '))
        out.pop_back();
    while (!out.empty() && (out.front() == L'.')) out.erase(out.begin());
    if (out.size() > 120) out.resize(120);
    if (out.empty()) out = L"unnamed";
    return out;
}

std::wstring volume_path(const std::wstring& source) {
    if (source.size() == 1 && std::iswalpha(source[0])) {
        return std::wstring(L"\\\\.\\") + source + L":";
    }
    if (source.size() == 2 && source[1] == L':') {
        return std::wstring(L"\\\\.\\") + source;
    }
    if (source.size() == 3 && source[1] == L':' &&
        (source[2] == L'\\' || source[2] == L'/')) {
        return std::wstring(L"\\\\.\\") + source[0] + L":";
    }
    return source;
}

// ---------------------------------------------------------------------------
// Raw source: image file or live volume. All reads are absolute from the
// start of the selected volume inside the source.

class RawSource {
public:
    ~RawSource() { close(); }

    bool open(const std::wstring& path, std::wstring& error) {
        close();
        path_ = path;
        if (path.rfind(L"\\\\.\\", 0) == 0 ||
            path.rfind(L"\\\\?\\GLOBALROOT\\Device\\", 0) == 0 ||
            (path.size() == 2 && path[1] == L':') ||
            (path.size() == 3 && path[1] == L':' &&
             (path[2] == L'\\' || path[2] == L'/'))) {
            const std::wstring device = volume_path(path);
            handle_ = CreateFileW(device.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
            if (handle_ == INVALID_HANDLE_VALUE) {
                error = L"volume read open failed, error " +
                    std::to_wstring(GetLastError()) + L" (admin required)";
                return false;
            }
            volume_ = true;
            LARGE_INTEGER length{};
            if (!GetFileSizeEx(handle_, &length) || length.QuadPart == 0) {
                GET_LENGTH_INFORMATION info{};
                DWORD returned = 0;
                if (!DeviceIoControl(handle_, IOCTL_DISK_GET_LENGTH_INFO,
                    nullptr, 0, &info, sizeof(info), &returned, nullptr)) {
                    error = L"volume size unavailable, error " +
                        std::to_wstring(GetLastError());
                    close();
                    return false;
                }
                length.QuadPart = info.Length.QuadPart;
            }
            raw_size_ = static_cast<uint64_t>(length.QuadPart);
        } else {
            handle_ = CreateFileW(extend_path(path).c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
            if (handle_ == INVALID_HANDLE_VALUE) {
                error = L"image open failed, error " +
                    std::to_wstring(GetLastError());
                return false;
            }
            LARGE_INTEGER length{};
            if (!GetFileSizeEx(handle_, &length)) {
                error = L"image size unavailable, error " +
                    std::to_wstring(GetLastError());
                close();
                return false;
            }
            raw_size_ = static_cast<uint64_t>(length.QuadPart);
        }
        base_ = 0;
        size_ = raw_size_;
        return true;
    }

    bool read_raw(uint64_t offset, void* buffer, std::size_t size) {
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        if (offset > raw_size_ || size > raw_size_ - offset) return false;
        uint8_t* p = static_cast<uint8_t*>(buffer);
        while (size > 0) {
            const DWORD chunk = static_cast<DWORD>(
                (std::min)(size, static_cast<std::size_t>(1u << 20)));
            LARGE_INTEGER pos{};
            pos.QuadPart = static_cast<LONGLONG>(offset);
            if (!SetFilePointerEx(handle_, pos, nullptr, FILE_BEGIN)) return false;
            DWORD read = 0;
            if (!ReadFile(handle_, p, chunk, &read, nullptr) || read != chunk)
                return false;
            p += chunk;
            offset += chunk;
            size -= chunk;
        }
        return true;
    }

    bool read(uint64_t offset, void* buffer, std::size_t size) {
        if (offset > size_ || size > size_ - offset) return false;
        return read_raw(base_ + offset, buffer, size);
    }

    bool set_base(uint64_t base) {
        if (base > raw_size_) return false;
        base_ = base;
        size_ = raw_size_ - base_;
        return true;
    }

    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        raw_size_ = 0;
        size_ = 0;
        base_ = 0;
        volume_ = false;
    }

    uint64_t raw_size() const { return raw_size_; }
    uint64_t size() const { return size_; }
    uint64_t base() const { return base_; }
    bool volume() const { return volume_; }
    const std::wstring& path() const { return path_; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring path_;
    uint64_t raw_size_ = 0;
    uint64_t base_ = 0;
    uint64_t size_ = 0;
    bool volume_ = false;
};

bool detect_volume_offset(RawSource& source, uint64_t& offset, std::wstring& error) {
    const uint64_t limit = (std::min)(source.raw_size(),
        static_cast<uint64_t>(8u * 1024u * 1024u));
    std::vector<uint8_t> buffer(static_cast<std::size_t>(limit));
    if (limit < 512 || !source.read_raw(0, buffer.data(), buffer.size())) {
        error = L"image too small or unreadable";
        return false;
    }
    for (uint64_t pos = 0; pos + 512 <= limit; pos += 512) {
        if (std::memcmp(buffer.data() + pos + 3, "NTFS    ", 8) != 0) continue;
        const uint16_t bps = read_u16(buffer.data() + pos + 11);
        const uint8_t spc = buffer[static_cast<std::size_t>(pos) + 13];
        if ((bps == 512 || bps == 1024 || bps == 2048 || bps == 4096) &&
            spc > 0 && (spc & (spc - 1)) == 0) {
            offset = pos;
            return true;
        }
    }
    error = L"NTFS boot sector not found in the first 8 MiB";
    return false;
}

// ---------------------------------------------------------------------------
// NTFS structures.

struct DataRun {
    uint64_t vcn = 0;
    uint64_t lcn = 0;
    uint64_t length = 0;
    bool sparse = false;
};

struct Attribute {
    uint32_t type = 0;
    std::wstring name;
    uint8_t nonresident = 0;
    uint16_t flags = 0;
    uint16_t id = 0;
    std::vector<uint8_t> resident;
    std::vector<DataRun> runs;
    uint64_t lowest_vcn = 0;
    uint64_t alloc_size = 0;
    uint64_t real_size = 0;
    uint64_t init_size = 0;
};

struct ParsedRecord {
    uint64_t number = 0;
    uint16_t sequence = 0;
    uint64_t base_ref = 0;
    uint16_t flags = 0;
    uint32_t used = 0;
    uint32_t alloc = 0;
    bool valid = false;
    std::vector<Attribute> attributes;
};

struct NtfsBoot {
    uint32_t bytes_per_sector = 0;
    uint32_t sectors_per_cluster = 0;
    uint32_t cluster_size = 0;
    uint32_t record_size = 0;
    uint64_t total_sectors = 0;
    uint64_t mft_lcn = 0;
    uint64_t mftmirr_lcn = 0;
    uint64_t serial = 0;
    uint64_t volume_bytes = 0;
    uint64_t cluster_count = 0;
};

bool parse_boot(const uint8_t* boot, NtfsBoot& out, std::wstring& error) {
    if (std::memcmp(boot + 3, "NTFS    ", 8) != 0) {
        error = L"not an NTFS boot sector";
        return false;
    }
    out.bytes_per_sector = read_u16(boot + 11);
    out.sectors_per_cluster = boot[13];
    out.total_sectors = read_u64(boot + 40);
    out.mft_lcn = read_u64(boot + 48);
    out.mftmirr_lcn = read_u64(boot + 56);
    const int8_t clusters_per_record = static_cast<int8_t>(boot[64]);
    out.serial = read_u64(boot + 72);

    if (out.bytes_per_sector < 512 || out.bytes_per_sector > 4096 ||
        (out.bytes_per_sector & (out.bytes_per_sector - 1)) != 0) {
        error = L"invalid bytes per sector";
        return false;
    }
    if (out.sectors_per_cluster == 0 ||
        (out.sectors_per_cluster & (out.sectors_per_cluster - 1)) != 0) {
        error = L"invalid sectors per cluster";
        return false;
    }
    if (clusters_per_record > 0) {
        out.record_size = static_cast<uint32_t>(clusters_per_record) *
            out.bytes_per_sector * out.sectors_per_cluster;
    } else if (clusters_per_record < 0 && clusters_per_record >= -16) {
        out.record_size = 1u << (-clusters_per_record);
    } else {
        error = L"invalid MFT record size";
        return false;
    }
    if (out.record_size < 512 || out.record_size > 65536) {
        error = L"MFT record size out of range";
        return false;
    }
    out.cluster_size = out.bytes_per_sector * out.sectors_per_cluster;
    out.volume_bytes = out.total_sectors * out.bytes_per_sector;
    out.cluster_count = out.volume_bytes / out.cluster_size;
    return out.cluster_size >= 512;
}

bool parse_runlist(const uint8_t* data, std::size_t size, std::vector<DataRun>& runs,
                   std::wstring& error) {
    runs.clear();
    std::size_t pos = 0;
    uint64_t vcn = 0;
    int64_t lcn = 0;
    while (pos < size) {
        const uint8_t header = data[pos++];
        if (header == 0) return true;
        const int len_bytes = header & 0x0F;
        const int off_bytes = (header >> 4) & 0x0F;
        if (len_bytes == 0 || len_bytes > 8 || off_bytes > 8) {
            error = L"invalid runlist header";
            return false;
        }
        if (pos + static_cast<std::size_t>(len_bytes + off_bytes) > size) {
            error = L"truncated runlist";
            return false;
        }
        uint64_t length = 0;
        for (int i = 0; i < len_bytes; ++i)
            length |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
        pos += len_bytes;
        int64_t delta = 0;
        if (off_bytes > 0) {
            uint64_t raw = 0;
            for (int i = 0; i < off_bytes; ++i)
                raw |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
            if (off_bytes < 8 && (raw & (1ull << (off_bytes * 8 - 1)))) {
                raw |= ~0ull << (off_bytes * 8);
            }
            delta = static_cast<int64_t>(raw);
        }
        pos += off_bytes;
        if (length == 0) {
            error = L"zero-length data run";
            return false;
        }
        lcn += delta;
        DataRun run;
        run.vcn = vcn;
        run.length = length;
        run.sparse = (off_bytes == 0);
        run.lcn = run.sparse ? 0 : static_cast<uint64_t>(lcn);
        runs.push_back(run);
        vcn += length;
        if (runs.size() > 1000000) {
            error = L"runlist too large";
            return false;
        }
    }
    return true;
}

bool apply_fixup(std::vector<uint8_t>& record, uint32_t bytes_per_sector,
                 std::wstring& error) {
    if (record.size() < 48) {
        error = L"truncated MFT record";
        return false;
    }
    if (std::memcmp(record.data(), "FILE", 4) != 0) return false; // not a FILE record
    const uint16_t usa_offset = read_u16(record.data() + 4);
    const uint16_t usa_count = read_u16(record.data() + 6);
    if (usa_count < 2 || static_cast<std::size_t>(usa_offset) +
        static_cast<std::size_t>(usa_count) * 2 > record.size()) {
        error = L"invalid update sequence array";
        return false;
    }
    const uint16_t sequence = read_u16(record.data() + usa_offset);
    for (uint16_t i = 1; i < usa_count; ++i) {
        const std::size_t sector_end =
            static_cast<std::size_t>(i) * bytes_per_sector;
        if (sector_end < 2 || sector_end > record.size()) continue;
        const uint16_t current = read_u16(record.data() + sector_end - 2);
        if (current != sequence) {
            error = L"update sequence mismatch";
            return false;
        }
        const uint16_t replacement = read_u16(record.data() + usa_offset + i * 2);
        record[sector_end - 2] = static_cast<uint8_t>(replacement & 0xFF);
        record[sector_end - 1] = static_cast<uint8_t>(replacement >> 8);
    }
    return true;
}

std::wstring utf16_name(const uint8_t* p, std::size_t chars) {
    if (chars == 0) return {};
    std::wstring out(chars, L'\0');
    std::memcpy(&out[0], p, chars * sizeof(wchar_t));
    return out;
}

bool parse_record(std::vector<uint8_t> record, uint32_t bytes_per_sector,
                  uint64_t expected_number, ParsedRecord& out, std::wstring& error) {
    out = ParsedRecord{};
    if (record.size() < 48) {
        error = L"record too small";
        return false;
    }
    if (std::memcmp(record.data(), "FILE", 4) != 0) {
        error = L"not a FILE record";
        return false;
    }
    const uint16_t usa_offset = read_u16(record.data() + 4);
    const uint16_t usa_count = read_u16(record.data() + 6);
    if (static_cast<std::size_t>(usa_offset) +
        static_cast<std::size_t>(usa_count) * 2 > record.size()) {
        error = L"usa outside record";
        return false;
    }
    if (!apply_fixup(record, bytes_per_sector, error)) return false;

    out.flags = read_u16(record.data() + 22);
    out.used = read_u32(record.data() + 24);
    out.alloc = read_u32(record.data() + 28);
    out.base_ref = read_u64(record.data() + 32) & 0x0000FFFFFFFFFFFFull;
    out.number = read_u32(record.data() + 44);
    out.sequence = read_u16(record.data() + 16);
    if (out.number == 0 && expected_number != 0) out.number = expected_number;
    if (out.used < 48 || out.used > record.size()) {
        error = L"invalid used size";
        return false;
    }

    std::size_t pos = read_u16(record.data() + 20);
    while (pos + 4 <= out.used) {
        const uint32_t type = read_u32(record.data() + pos);
        if (type == kAttrEnd) break;
        if (pos + 16 > out.used) {
            error = L"truncated attribute header";
            return false;
        }
        const uint32_t length = read_u32(record.data() + pos + 4);
        if (length < 16 || pos + length > out.used) {
            error = L"invalid attribute length";
            return false;
        }
        const uint8_t* attr = record.data() + pos;
        Attribute parsed;
        parsed.type = type;
        parsed.nonresident = attr[8];
        parsed.flags = read_u16(attr + 12);
        parsed.id = read_u16(attr + 14);
        const uint8_t name_len = attr[9];
        const uint16_t name_off = read_u16(attr + 10);
        if (name_len > 0 && static_cast<std::size_t>(name_off) +
            static_cast<std::size_t>(name_len) * 2 <= length) {
            parsed.name = utf16_name(attr + name_off, name_len);
        }
        if (parsed.nonresident == 0) {
            if (length < 24) {
                error = L"truncated resident attribute";
                return false;
            }
            const uint32_t value_len = read_u32(attr + 16);
            const uint16_t value_off = read_u16(attr + 20);
            if (value_off + value_len > length) {
                error = L"resident value outside attribute";
                return false;
            }
            parsed.resident.assign(attr + value_off, attr + value_off + value_len);
            parsed.real_size = parsed.resident.size();
            parsed.init_size = parsed.real_size;
            parsed.alloc_size = parsed.real_size;
        } else {
            if (length < 64) {
                error = L"truncated nonresident attribute";
                return false;
            }
            parsed.lowest_vcn = read_u64(attr + 16);
            parsed.alloc_size = read_u64(attr + 40);
            parsed.real_size = read_u64(attr + 48);
            parsed.init_size = read_u64(attr + 56);
            const uint16_t run_off = read_u16(attr + 32);
            if (run_off >= length) {
                error = L"runlist offset outside attribute";
                return false;
            }
            if (!parse_runlist(attr + run_off, length - run_off, parsed.runs, error))
                return false;
        }
        out.attributes.push_back(std::move(parsed));
        pos += length;
    }
    out.valid = true;
    return true;
}

struct MftStream {
    std::vector<DataRun> runs;
    uint64_t size = 0;
    uint32_t cluster_size = 0;

    bool read(RawSource& source, uint64_t offset, void* buffer, std::size_t size_in) {
        if (offset > size || size_in > size - offset) return false;
        uint8_t* out = static_cast<uint8_t*>(buffer);
        std::size_t remaining = size_in;
        uint64_t cursor = offset;
        while (remaining > 0) {
            const uint64_t vcn = cursor / cluster_size;
            const DataRun* run = nullptr;
            for (const auto& candidate : runs) {
                if (vcn >= candidate.vcn && vcn < candidate.vcn + candidate.length) {
                    run = &candidate;
                    break;
                }
            }
            if (!run) return false;
            const uint64_t run_offset = cursor - run->vcn * cluster_size;
            const uint64_t run_bytes = run->length * cluster_size - run_offset;
            const std::size_t chunk = static_cast<std::size_t>(
                (std::min)(remaining, static_cast<std::size_t>(run_bytes)));
            uint8_t* target = out + (cursor - offset);
            if (run->sparse) {
                std::memset(target, 0, chunk);
            } else {
                if (!source.read(run->lcn * cluster_size + run_offset, target, chunk))
                    return false;
            }
            cursor += chunk;
            remaining -= chunk;
        }
        return true;
    }
};

struct NameInfo {
    uint64_t number = 0;
    uint64_t sequence = 0;
    uint64_t parent = 0;
    uint16_t parent_sequence = 0;
    uint16_t flags = 0;
    bool in_use = false;
    bool directory = false;
    bool has_name = false;
    uint8_t name_space = 0;
    std::wstring name;
    uint64_t size = 0;
    uint64_t alloc_size = 0;
    uint64_t created = 0;
    uint64_t modified = 0;
    uint64_t mft_modified = 0;
    uint64_t accessed = 0;
};

int namespace_score(uint8_t ns) {
    switch (ns) {
    case 1: return 4; // Win32
    case 3: return 3; // POSIX
    case 0: return 2; // Win32 + DOS
    case 2: return 1; // DOS 8.3
    default: return 0;
    }
}

std::wstring resolve_path_map(const std::unordered_map<uint64_t, NameInfo>& names,
                              uint64_t number, bool& resolved) {
    resolved = false;
    std::vector<std::wstring> parts;
    std::set<uint64_t> seen;
    uint64_t current = number;
    bool parent_ok = true;
    for (int depth = 0; depth < 64; ++depth) {
        if (current == 5) break;
        const auto it = names.find(current);
        if (it == names.end() || !it->second.has_name) {
            parent_ok = false;
            break;
        }
        if (!seen.insert(current).second) {
            parent_ok = false;
            break;
        }
        if (!it->second.in_use) parent_ok = false;
        if (it->second.name != L".") parts.push_back(it->second.name);
        const uint64_t parent = it->second.parent;
        if (parent == 0 || parent == current) {
            parent_ok = false;
            current = 0;
            break;
        }
        const auto parent_it = names.find(parent);
        if (parent_it != names.end() && it->second.parent_sequence != 0 &&
            parent_it->second.sequence != it->second.parent_sequence) {
            parent_ok = false;
        }
        current = parent;
    }
    if (current != 5) parent_ok = false;
    std::wstring path = L"\\";
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        path += *it;
        path += L"\\";
    }
    if (!path.empty() && path.back() == L'\\' && number != 5) path.pop_back();
    resolved = parent_ok;
    return path;
}

class NtfsVolume {
public:
    bool open(const std::wstring& source_path, uint64_t offset,
              std::wstring& error) {
        if (!source_.open(source_path, error)) return false;
        if (offset == 0 && !source_.volume()) {
            uint64_t detected = 0;
            std::wstring detect_error;
            if (!detect_volume_offset(source_, detected, detect_error)) {
                error = detect_error;
                return false;
            }
            offset = detected;
        }
        if (!source_.set_base(offset)) {
            error = L"offset beyond end of source";
            return false;
        }
        std::vector<uint8_t> boot(512);
        if (!source_.read(0, boot.data(), boot.size())) {
            error = L"cannot read boot sector";
            return false;
        }
        if (!parse_boot(boot.data(), boot_, error)) return false;
        if (boot_.volume_bytes > source_.size())
            boot_.volume_bytes = source_.size();
        boot_.cluster_count = boot_.volume_bytes / boot_.cluster_size;
        if (!load_mft(error)) return false;
        return true;
    }

    bool load_index(std::wstring& error, uint64_t& record_count) {
        record_count = mft_.size / boot_.record_size;
        if (record_count == 0 || record_count > 20000000ull) {
            error = L"implausible MFT size";
            return false;
        }
        names_.clear();
        names_.reserve(static_cast<std::size_t>(record_count));
        for (uint64_t i = 0; i < record_count; ++i) {
            ParsedRecord record;
            if (!read_record(i, record, error)) continue;
            if (!record.valid) continue;
            NameInfo info;
            info.number = record.number;
            info.flags = record.flags;
            info.in_use = (record.flags & 1) != 0;
            info.directory = (record.flags & 2) != 0;
            info.sequence = record.sequence;
            int best_score = -1;
            for (const auto& attr : record.attributes) {
                if (attr.type == kAttrFileName && !attr.resident.empty() &&
                    attr.resident.size() >= 66) {
                    const uint8_t* value = attr.resident.data();
                    const uint64_t parent_ref = read_u64(value);
                    const uint8_t ns = value[65];
                    const uint8_t name_len = value[64];
                    if (66 + static_cast<std::size_t>(name_len) * 2 > attr.resident.size())
                        continue;
                    const std::wstring name = utf16_name(value + 66, name_len);
                    const int score = namespace_score(ns);
                    if (score > best_score) {
                        best_score = score;
                        info.has_name = true;
                        info.parent = parent_ref & 0x0000FFFFFFFFFFFFull;
                        info.parent_sequence = static_cast<uint16_t>(parent_ref >> 48);
                        info.name_space = ns;
                        info.name = name;
                        info.created = read_u64(value + 8);
                        info.modified = read_u64(value + 16);
                        info.mft_modified = read_u64(value + 24);
                        info.accessed = read_u64(value + 32);
                        info.alloc_size = read_u64(value + 40);
                        info.size = read_u64(value + 48);
                    }
                }
            }
            for (const auto& attr : record.attributes) {
                if (attr.type == kAttrStandardInformation && attr.resident.size() >= 36) {
                    const uint8_t* value = attr.resident.data();
                    info.created = read_u64(value);
                    info.modified = read_u64(value + 8);
                    info.mft_modified = read_u64(value + 16);
                    info.accessed = read_u64(value + 24);
                    break;
                }
            }
            bool has_default_data = false;
            for (const auto& attr : record.attributes) {
                if (attr.type == kAttrData && attr.name.empty()) {
                    has_default_data = true;
                    if (attr.real_size > 0) info.size = attr.real_size;
                    info.alloc_size = attr.alloc_size;
                    break;
                }
            }
            (void)has_default_data;
            names_.emplace(record.number, std::move(info));
        }
        return true;
    }

    bool read_record(uint64_t number, ParsedRecord& record, std::wstring& error) {
        std::vector<uint8_t> buffer;
        if (!read_record_raw(number, buffer)) return false;
        if (std::memcmp(buffer.data(), "FILE", 4) != 0) return false;
        return parse_record(std::move(buffer), boot_.bytes_per_sector, number,
                            record, error);
    }

    bool read_record_raw(uint64_t number, std::vector<uint8_t>& buffer) {
        const uint64_t offset = number * boot_.record_size;
        if (offset + boot_.record_size > mft_.size) return false;
        buffer.resize(boot_.record_size);
        return mft_.read(source_, offset, buffer.data(), buffer.size());
    }

    std::wstring resolve_path(uint64_t number, bool& resolved) const {
        return resolve_path_map(names_, number, resolved);
    }

    const std::unordered_map<uint64_t, NameInfo>& names() const { return names_; }
    const NtfsBoot& boot() const { return boot_; }
    RawSource& source() { return source_; }
    void close() { source_.close(); }

    bool mft_record_physical(uint64_t number, uint64_t& physical) {
        const uint64_t stream_offset = number * boot_.record_size;
        if (stream_offset >= mft_.size) return false;
        const uint64_t vcn = stream_offset / boot_.cluster_size;
        for (const auto& run : mft_.runs) {
            if (vcn < run.vcn || vcn >= run.vcn + run.length) continue;
            if (run.sparse) return false;
            const uint64_t within = stream_offset - run.vcn * boot_.cluster_size;
            physical = source_.base() + run.lcn * boot_.cluster_size + within;
            return true;
        }
        return false;
    }

    uint64_t record_count() const { return mft_.size / boot_.record_size; }

    bool read_stream(const Attribute& attr, uint64_t offset, void* buffer,
                     std::size_t size) {
        if (attr.nonresident == 0) {
            if (offset > attr.resident.size() ||
                size > attr.resident.size() - offset) return false;
            std::memcpy(buffer, attr.resident.data() + offset, size);
            return true;
        }
        if ((attr.flags & 0x0001) != 0 || (attr.flags & 0x4000) != 0)
            return false; // compressed or encrypted
        uint8_t* out = static_cast<uint8_t*>(buffer);
        std::size_t remaining = size;
        uint64_t cursor = offset;
        while (remaining > 0) {
            const uint64_t vcn = cursor / boot_.cluster_size;
            const DataRun* run = nullptr;
            for (const auto& candidate : attr.runs) {
                if (vcn >= candidate.vcn && vcn < candidate.vcn + candidate.length) {
                    run = &candidate;
                    break;
                }
            }
            if (!run) {
                std::memset(out, 0, remaining);
                return true;
            }
            const uint64_t run_offset = cursor - run->vcn * boot_.cluster_size;
            const uint64_t run_bytes =
                run->length * boot_.cluster_size - run_offset;
            const std::size_t chunk = static_cast<std::size_t>(
                (std::min)(remaining, static_cast<std::size_t>(run_bytes)));
            if (run->sparse) {
                std::memset(out, 0, chunk);
            } else if (!source_.read(run->lcn * boot_.cluster_size + run_offset,
                                     out, chunk)) {
                return false;
            }
            cursor += chunk;
            out += chunk;
            remaining -= chunk;
        }
        return true;
    }

private:
    bool load_mft(std::wstring& error) {
        const uint64_t offset = boot_.mft_lcn * boot_.cluster_size;
        std::vector<uint8_t> first(boot_.record_size);
        if (!source_.read(offset, first.data(), first.size())) {
            error = L"cannot read MFT record 0";
            return false;
        }
        ParsedRecord record;
        if (!parse_record(std::move(first), boot_.bytes_per_sector, 0, record, error)) {
            error = L"MFT record 0: " + error;
            return false;
        }
        const Attribute* data = nullptr;
        for (const auto& attr : record.attributes) {
            if (attr.type == kAttrData && attr.name.empty() &&
                attr.nonresident != 0) {
                data = &attr;
                break;
            }
        }
        if (data == nullptr) {
            error = L"MFT $DATA not found";
            return false;
        }
        mft_.runs = data->runs;
        mft_.size = data->real_size;
        mft_.cluster_size = boot_.cluster_size;
        if (mft_.size == 0) mft_.size = data->alloc_size;
        if (mft_.size == 0 || mft_.runs.empty()) {
            error = L"MFT stream is empty";
            return false;
        }
        return true;
    }

    RawSource source_;
    NtfsBoot boot_;
    MftStream mft_;
    std::unordered_map<uint64_t, NameInfo> names_;
};

// ---------------------------------------------------------------------------
// NTFS scan: inventory, deleted recovery, USN and carving.

struct ScanStats {
    uint64_t records = 0;
    uint64_t in_use = 0;
    uint64_t directories = 0;
    uint64_t deleted = 0;
    uint64_t recovered = 0;
    uint64_t recovery_failed = 0;
    uint64_t carved = 0;
    uint64_t carved_bytes = 0;
    uint64_t usn_records = 0;
    uint64_t slack_carved = 0;
};

const Attribute* find_attribute(const ParsedRecord& record, uint32_t type,
                                const std::wstring& name, bool require_nonresident) {
    for (const auto& attr : record.attributes) {
        if (attr.type != type) continue;
        if (attr.name != name) continue;
        if (require_nonresident && attr.nonresident == 0) continue;
        return &attr;
    }
    return nullptr;
}

std::wstring stream_status(const Attribute& attr) {
    if (attr.nonresident == 0) return L"resident";
    if ((attr.flags & 0x0001) != 0) return L"compressed_unsupported";
    if ((attr.flags & 0x4000) != 0) return L"encrypted_unsupported";
    if (attr.runs.empty() && attr.real_size > 0) return L"no_runs";
    return L"raw";
}

std::string streams_json(const ParsedRecord& record, const NtfsBoot& boot) {
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& attr : record.attributes) {
        if (attr.type != kAttrData) continue;
        if (!first) out << ", ";
        first = false;
        out << "{\"name\": " << json_escape_w(attr.name)
            << ", \"resident\": " << (attr.nonresident == 0 ? "true" : "false")
            << ", \"size\": " << attr.real_size
            << ", \"allocated\": " << attr.alloc_size
            << ", \"flags\": " << attr.flags
            << ", \"status\": " << json_escape_w(stream_status(attr))
            << ", \"runs\": [";
        for (std::size_t i = 0; i < attr.runs.size(); ++i) {
            if (i) out << ", ";
            out << "{\"vcn\": " << attr.runs[i].vcn
                << ", \"lcn\": " << (attr.runs[i].sparse ? 0 : attr.runs[i].lcn)
                << ", \"length\": " << attr.runs[i].length
                << ", \"sparse\": " << (attr.runs[i].sparse ? "true" : "false")
                << "}";
        }
        out << "]}";
    }
    out << "]";
    (void)boot;
    return out.str();
}

bool write_attribute_to_file(NtfsVolume& volume, const Attribute& attr,
                             const std::wstring& path, uint64_t max_bytes,
                             uint64_t& written, std::wstring& error) {
    const uint64_t size = attr.nonresident == 0
        ? static_cast<uint64_t>(attr.resident.size()) : attr.real_size;
    if (size > max_bytes) {
        error = L"file exceeds configured maximum";
        return false;
    }
    if ((attr.flags & 0x0001) != 0 || (attr.flags & 0x4000) != 0) {
        error = L"compressed or encrypted stream";
        return false;
    }
    HANDLE file = CreateFileW(extend_path(path).c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"cannot create output, error " +
            std::to_wstring(GetLastError());
        return false;
    }
    bool ok = true;
    written = 0;
    std::vector<uint8_t> buffer(1024 * 1024);
    while (written < size) {
        const std::size_t chunk = static_cast<std::size_t>(
            (std::min)(static_cast<uint64_t>(buffer.size()), size - written));
        if (!volume.read_stream(attr, written, buffer.data(), chunk)) {
            error = L"data run read failed";
            ok = false;
            break;
        }
        if (!write_all(file, buffer.data(), chunk)) {
            error = L"output write failed";
            ok = false;
            break;
        }
        written += chunk;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

int extract_image_evtx(const std::wstring& image, const std::wstring& out_dir,
                       std::uint64_t offset) {
    NtfsVolume volume;
    std::wstring error;
    if (!volume.open(image, offset, error)) {
        std::cerr << "image-evtx: " << art::to_utf8(error) << "\n";
        return 1;
    }
    std::uint64_t records = 0;
    if (!volume.load_index(error, records)) {
        std::cerr << "image-evtx: " << art::to_utf8(error) << "\n";
        return 1;
    }
    const std::wstring extracted = join_path(out_dir, L"evtx_from_image");
    ensure_directory(extracted);
    int files = 0;
    for (const auto& entry : volume.names()) {
        const NameInfo& info = entry.second;
        if (!info.in_use || info.directory || !info.has_name) continue;
        std::wstring lower = info.name;
        for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
        if (lower.size() < 5 || lower.substr(lower.size() - 5) != L".evtx")
            continue;
        ParsedRecord record;
        if (!volume.read_record(info.number, record, error)) continue;
        const Attribute* data = nullptr;
        for (const auto& attr : record.attributes) {
            if (attr.type == kAttrData && attr.name.empty()) {
                data = &attr;
                break;
            }
        }
        if (data == nullptr || (data->real_size == 0 && data->resident.empty()))
            continue;
        const std::wstring name = std::to_wstring(info.number) + L"_" +
            sanitize_component(info.name);
        const std::wstring path = join_path(extracted, name);
        std::uint64_t written = 0;
        std::wstring write_error;
        if (!write_attribute_to_file(volume, *data, path, 1024ull * 1024 * 1024,
                                     written, write_error)) {
            std::cerr << "image-evtx: " << art::to_utf8(info.name) << ": "
                      << art::to_utf8(write_error) << "\n";
            continue;
        }
        ++files;
        std::cout << "image-evtx: extracted=" << art::to_utf8(name)
                  << " bytes=" << written << "\n";
    }
    volume.close();
    if (files == 0) {
        std::cerr << "image-evtx: no .evtx files found in MFT\n";
        return 1;
    }
    ArtifactOptions options;
    options.out_dir = out_dir;
    options.db_path = join_path(out_dir, L"case.db");
    options.registry = options.browser = options.recycle = false;
    options.lnk = options.jumplist = options.prefetch = false;
    options.shellbags = false;
    options.evtx = true;
    options.evtx_dir = extracted;
    return run_artifacts(options);
}

int extract_image_prefetch(const std::wstring& image, const std::wstring& out_dir,
                           std::uint64_t offset) {
    NtfsVolume volume;
    std::wstring error;
    if (!volume.open(image, offset, error)) {
        std::cerr << "image-prefetch: " << art::to_utf8(error) << "\n";
        return 1;
    }
    std::uint64_t records = 0;
    if (!volume.load_index(error, records)) {
        std::cerr << "image-prefetch: " << art::to_utf8(error) << "\n";
        return 1;
    }
    const std::wstring extracted = join_path(out_dir, L"prefetch_from_image");
    ensure_directory(extracted);
    int files = 0;
    for (const auto& entry : volume.names()) {
        const NameInfo& info = entry.second;
        if (!info.in_use || info.directory || !info.has_name) continue;
        std::wstring lower = info.name;
        for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
        if (lower.size() < 3 || lower.substr(lower.size() - 3) != L".pf")
            continue;
        ParsedRecord record;
        if (!volume.read_record(info.number, record, error)) continue;
        const Attribute* data = nullptr;
        for (const auto& attr : record.attributes) {
            if (attr.type == kAttrData && attr.name.empty()) {
                data = &attr;
                break;
            }
        }
        if (data == nullptr || (data->real_size == 0 && data->resident.empty()))
            continue;
        const std::wstring name = std::to_wstring(info.number) + L"_" +
            sanitize_component(info.name);
        const std::wstring path = join_path(extracted, name);
        std::uint64_t written = 0;
        std::wstring write_error;
        if (!write_attribute_to_file(volume, *data, path, 1024ull * 1024 * 1024,
                                     written, write_error)) {
            std::cerr << "image-prefetch: " << art::to_utf8(info.name) << ": "
                      << art::to_utf8(write_error) << "\n";
            continue;
        }
        ++files;
        std::cout << "image-prefetch: extracted=" << art::to_utf8(name)
                  << " bytes=" << written << "\n";
    }
    volume.close();
    if (files == 0) {
        std::cerr << "image-prefetch: no .pf files found in MFT\n";
        return 1;
    }
    ArtifactOptions options;
    options.out_dir = out_dir;
    options.db_path = join_path(out_dir, L"case.db");
    options.registry = options.browser = options.recycle = false;
    options.lnk = options.jumplist = options.evtx = false;
    options.shellbags = false;
    options.prefetch = true;
    options.prefetch_dir = extracted;
    return run_artifacts(options);
}

bool recover_record(NtfsVolume& volume, const ParsedRecord& record,
                    const NameInfo& info, const std::wstring& recovered_dir,
                    std::uint64_t max_bytes, ScanStats& stats, HANDLE jsonl,
                    const std::wstring& case_id) {
    bool any = false;
    for (const auto& attr : record.attributes) {
        if (attr.type != kAttrData) continue;
        if (attr.real_size == 0 && attr.resident.empty()) continue;
        std::wstring base = std::to_wstring(record.number) + L"_" +
            sanitize_component(info.has_name ? info.name : L"unnamed");
        if (!attr.name.empty()) base += L"__" + sanitize_component(attr.name);
        std::wstring path = join_path(recovered_dir, base);
        for (int attempt = 1; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
             ++attempt) {
            path = join_path(recovered_dir, base + L"_" + std::to_wstring(attempt));
        }
        uint64_t written = 0;
        std::wstring error;
        const bool ok = write_attribute_to_file(volume, attr, path, max_bytes,
                                                written, error);
        std::string digest;
        if (ok) sha256_file(path, digest);
        std::ostringstream line;
        line << "{\"case\": " << json_escape(to_utf8(case_id))
             << ", \"record\": " << record.number
             << ", \"stream\": " << json_escape_w(attr.name)
             << ", \"size\": " << attr.real_size
             << ", \"output\": " << json_escape_w(path)
             << ", \"sha256\": " << json_escape(digest)
             << ", \"status\": "
             << json_escape_w(ok ? L"recovered" : L"failed")
             << ", \"error\": " << json_escape_w(error) << "}";
        append_jsonl(jsonl, line.str());
        if (ok) {
            ++stats.recovered;
            any = true;
        } else {
            ++stats.recovery_failed;
        }
    }
    return any;
}

// ---------------------------------------------------------------------------
// USN journal.

struct ReasonName {
    uint32_t bit;
    const char* name;
};

const ReasonName kReasons[] = {
    {0x00000001, "DATA_OVERWRITE"}, {0x00000002, "DATA_EXTEND"},
    {0x00000004, "DATA_TRUNCATION"}, {0x00000010, "NAMED_DATA_OVERWRITE"},
    {0x00000020, "NAMED_DATA_EXTEND"}, {0x00000040, "NAMED_DATA_TRUNCATION"},
    {0x00000100, "FILE_CREATE"}, {0x00000200, "FILE_DELETE"},
    {0x00000400, "EA_CHANGE"}, {0x00000800, "SECURITY_CHANGE"},
    {0x00001000, "RENAME_OLD_NAME"}, {0x00002000, "RENAME_NEW_NAME"},
    {0x00004000, "INDEXABLE_CHANGE"}, {0x00008000, "BASIC_INFO_CHANGE"},
    {0x00010000, "HARD_LINK_CHANGE"}, {0x00020000, "COMPRESSION_CHANGE"},
    {0x00040000, "ENCRYPTION_CHANGE"}, {0x00080000, "OBJECT_ID_CHANGE"},
    {0x00100000, "REPARSE_POINT_CHANGE"}, {0x00200000, "STREAM_CHANGE"},
    {0x00400000, "TRANSACTED_CHANGE"}, {0x00800000, "INTEGRITY_CHANGE"},
    {0x80000000, "CLOSE"},
};

std::string reasons_text(uint32_t reasons) {
    std::string out;
    for (const auto& reason : kReasons) {
        if ((reasons & reason.bit) == 0) continue;
        if (!out.empty()) out += "|";
        out += reason.name;
    }
    return out;
}

bool read_usn_stream(NtfsVolume& volume, std::vector<uint8_t>& journal,
                     std::wstring& error) {
    const auto& names = volume.names();
    const NameInfo* target = nullptr;
    for (const auto& entry : names) {
        if (_wcsicmp(entry.second.name.c_str(), L"$UsnJrnl") == 0) {
            target = &entry.second;
            break;
        }
    }
    if (target == nullptr) {
        error = L"$UsnJrnl not present in this volume";
        return false;
    }
    ParsedRecord record;
    if (!volume.read_record(target->number, record, error)) return false;
    const Attribute* data = find_attribute(record, kAttrData, L"$J", true);
    if (data == nullptr) {
        data = find_attribute(record, kAttrData, L"$J", false);
    }
    if (data == nullptr) {
        error = L"$UsnJrnl:$J not found";
        return false;
    }
    const uint64_t size = data->real_size;
    if (size == 0 || size > 4ull * 1024ull * 1024ull * 1024ull) {
        error = L"USN journal empty or implausibly large";
        return false;
    }
    journal.resize(static_cast<std::size_t>(size));
    if (!volume.read_stream(*data, 0, journal.data(), journal.size())) {
        error = L"USN journal read failed";
        return false;
    }
    return true;
}

void parse_usn_records(const std::vector<uint8_t>& journal,
                       const std::unordered_map<uint64_t, NameInfo>& names,
                       HANDLE jsonl, ScanStats& stats) {
    std::size_t pos = 0;
    while (pos + 8 <= journal.size()) {
        const uint32_t length = read_u32(journal.data() + pos);
        if (length == 0 || length > 65536 || pos + length > journal.size()) {
            pos += 8;
            continue;
        }
        const uint16_t major = read_u16(journal.data() + pos + 4);
        const uint8_t* record = journal.data() + pos;
        uint64_t file_ref = 0;
        uint64_t parent_ref = 0;
        uint64_t usn = 0;
        uint64_t timestamp = 0;
        uint32_t reasons = 0;
        uint32_t source = 0;
        uint32_t security_id = 0;
        uint32_t attributes = 0;
        uint16_t name_len = 0;
        uint16_t name_off = 0;
        uint64_t file_id_low = 0;
        if (major == 2) {
            if (length < 60) { pos += 8; continue; }
            file_ref = read_u64(record + 8);
            parent_ref = read_u64(record + 16);
            usn = read_u64(record + 24);
            timestamp = read_u64(record + 32);
            reasons = read_u32(record + 40);
            source = read_u32(record + 44);
            security_id = read_u32(record + 48);
            attributes = read_u32(record + 52);
            name_len = read_u16(record + 56);
            name_off = read_u16(record + 58);
            file_id_low = file_ref & 0x0000FFFFFFFFFFFFull;
        } else if (major == 3) {
            if (length < 76) { pos += 8; continue; }
            file_id_low = read_u64(record + 8); // low 64 of the 128-bit id
            parent_ref = read_u64(record + 24);
            usn = read_u64(record + 40);
            timestamp = read_u64(record + 48);
            reasons = read_u32(record + 56);
            source = read_u32(record + 60);
            security_id = read_u32(record + 64);
            attributes = read_u32(record + 68);
            name_len = read_u16(record + 72);
            name_off = read_u16(record + 74);
        } else {
            pos += 8;
            continue;
        }
        if (name_off + name_len <= length && name_len % 2 == 0) {
            const std::wstring name = utf16_name(record + name_off, name_len / 2);
            std::wstring path = L"<unknown>";
            const auto it = names.find(file_id_low);
            if (it != names.end()) {
                bool resolved = false;
                path = resolve_path_map(names, file_id_low, resolved);
            }
            std::ostringstream line;
            line << "{\"timestamp_utc\": " << json_escape(format_filetime(timestamp))
                 << ", \"usn\": " << static_cast<int64_t>(usn)
                 << ", \"file_ref\": " << hex64(file_ref)
                 << ", \"parent_ref\": " << hex64(parent_ref)
                 << ", \"name\": " << json_escape_w(name)
                 << ", \"reasons\": " << json_escape(reasons_text(reasons))
                 << ", \"reason_flags\": " << hex64(reasons)
                 << ", \"source\": " << source
                 << ", \"security_id\": " << security_id
                 << ", \"attributes\": " << attributes
                 << ", \"path_hint\": " << json_escape_w(path) << "}";
            append_jsonl(jsonl, line.str());
            ++stats.usn_records;
        }
        pos += length;
    }
}

} // namespace

namespace {

enum class SigKind {
    Jpeg, Png, Pdf, Zip, Sqlite, Evtx, Mp3, Riff, Mp4, Gif, Prefetch
};

struct SigDef {
    SigKind kind;
    const char* name;
    const char* ext;
    const char* magic;
    std::size_t magic_len;
};

const SigDef kSignatures[] = {
    {SigKind::Jpeg, "jpeg", "jpg", "\xFF\xD8\xFF", 3},
    {SigKind::Png, "png", "png", "\x89PNG\r\n\x1A\n", 8},
    {SigKind::Pdf, "pdf", "pdf", "%PDF-", 5},
    {SigKind::Zip, "zip", "zip", "PK\x03\x04", 4},
    {SigKind::Sqlite, "sqlite", "sqlite", "SQLite format 3\0", 16},
    {SigKind::Evtx, "evtx", "evtx", "ElfFile\0", 8},
    {SigKind::Prefetch, "prefetch", "pf", "SCCA", 4},
    {SigKind::Mp3, "mp3", "mp3", "ID3", 3},
    {SigKind::Riff, "riff", "riff", "RIFF", 4},
    {SigKind::Mp4, "mp4", "mp4", "\x00\x00\x00\x18" "ftyp", 8},
    {SigKind::Mp4, "mp4", "mp4", "\x00\x00\x00\x20" "ftyp", 8},
    {SigKind::Mp4, "mp4", "mp4", "\x00\x00\x00\x1C" "ftyp", 8},
    {SigKind::Mp4, "mp4", "mp4", "\x00\x00\x00\x24" "ftyp", 8},
    {SigKind::Gif, "gif", "gif", "GIF8", 4},
};

const uint8_t* find_bytes(const uint8_t* begin, const uint8_t* end,
                          const char* needle, std::size_t needle_size) {
    if (needle_size == 0 || static_cast<std::size_t>(end - begin) < needle_size)
        return end;
    const uint8_t* found = std::search(begin, end, 
        reinterpret_cast<const uint8_t*>(needle),
        reinterpret_cast<const uint8_t*>(needle) + needle_size);
    return found;
}

uint64_t validate_signature(SigKind kind, const uint8_t* buf, std::size_t size,
                            std::size_t pos, uint64_t max) {
    if (pos >= size || pos + 8 > size) return 0;
    const std::size_t available = size - pos;
    uint64_t length = 0;
    switch (kind) {
    case SigKind::Jpeg: {
        if (pos + 3 > size || buf[pos + 2] != 0xFF) return 0;
        std::size_t cursor = pos + 2;
        bool sof = false;
        bool sos = false;
        while (cursor + 4 <= size) {
            if (buf[cursor] != 0xFF) return 0;
            const uint8_t marker = buf[cursor + 1];
            if (marker == 0xD8 || marker == 0x01 ||
                (marker >= 0xD0 && marker <= 0xD7)) {
                cursor += 2;
                continue;
            }
            if (marker == 0xD9) break;
            if (marker == 0xDA) sos = true;
            const uint16_t segment = read_u16(buf + cursor + 2);
            if (segment < 2 || cursor + 2 + segment > size) return 0;
            if ((marker >= 0xC0 && marker <= 0xC3) ||
                (marker >= 0xC5 && marker <= 0xC7) ||
                (marker >= 0xC9 && marker <= 0xCB) ||
                (marker >= 0xCD && marker <= 0xCF)) {
                sof = true;
            }
            cursor += 2 + segment;
            if (sos) break;
        }
        if (!sof || !sos) return 0;
        for (std::size_t i = cursor; i + 1 < size; ++i) {
            if (buf[i] == 0xFF && buf[i + 1] == 0xD9) {
                length = i + 2 - pos;
                break;
            }
        }
        break;
    }
    case SigKind::Png: {
        const uint8_t* iend = find_bytes(buf + pos + 8, buf + size, "IEND", 4);
        if (iend != buf + size && iend >= buf + pos + 12 &&
            read_u32(iend - 4) == 0) {
            length = static_cast<uint64_t>(iend - (buf + pos)) + 8;
        }
        break;
    }
    case SigKind::Pdf: {
        const uint8_t* eof = find_bytes(buf + pos + 5, buf + size, "%%EOF", 5);
        if (eof != buf + size) {
            const uint8_t* end = eof + 5;
            while (end < buf + size && (*end == '\r' || *end == '\n')) ++end;
            length = static_cast<uint64_t>(end - (buf + pos));
        }
        break;
    }
    case SigKind::Zip: {
        if (pos + 30 > size) return 0;
        const uint16_t version = read_u16(buf + pos + 4);
        const uint16_t method = read_u16(buf + pos + 8);
        const uint16_t name_len = read_u16(buf + pos + 26);
        if ((version & 0xFF) > 63 || name_len == 0 || name_len > 512 ||
            pos + 30 + name_len > size) return 0;
        const bool method_ok = method == 0 || method == 8 || method == 9 ||
            method == 12 || method == 14 || method == 93 || method == 95 ||
            method == 98;
        if (!method_ok) return 0;
        for (uint16_t i = 0; i < name_len; ++i) {
            const uint8_t c = buf[pos + 30 + i];
            if (c < 0x20 || c == 0x7F || c == '\\') return 0;
        }
        std::size_t eocd_pos = 0;
        bool found_eocd = false;
        for (std::size_t i = pos + 30 + name_len; i + 22 <= size; ++i) {
            if (std::memcmp(buf + i, "PK\x05\x06", 4) != 0) continue;
            eocd_pos = i;
            found_eocd = true;
        }
        if (!found_eocd) return 0;
        const uint16_t comment = read_u16(buf + eocd_pos + 20);
        if (static_cast<std::size_t>(eocd_pos) + 22 + comment > size) return 0;
        const uint32_t cd_offset = read_u32(buf + eocd_pos + 16);
        if (cd_offset < 30u + static_cast<uint32_t>(name_len) ||
            pos + cd_offset + 4 > eocd_pos ||
            std::memcmp(buf + pos + cd_offset, "PK\x01\x02", 4) != 0) return 0;
        length = static_cast<uint64_t>(eocd_pos + 22 + comment - pos);
        break;
    }
    case SigKind::Sqlite: {
        if (pos + 32 > size) return 0;
        uint32_t page_size = (static_cast<uint32_t>(buf[pos + 16]) << 8) |
            buf[pos + 17];
        if (page_size == 1) page_size = 65536;
        const uint32_t page_count = (static_cast<uint32_t>(buf[pos + 28]) << 24) |
            (static_cast<uint32_t>(buf[pos + 29]) << 16) |
            (static_cast<uint32_t>(buf[pos + 30]) << 8) | buf[pos + 31];
        if (page_size >= 512 && page_size <= 65536 && page_count > 0)
            length = static_cast<uint64_t>(page_size) * page_count;
        break;
    }
    case SigKind::Evtx: {
        if (pos + 0x2C > size) return 0;
        const uint16_t chunks = read_u16(buf + pos + 0x2A);
        if (chunks > 0 && chunks < 32768)
            length = 4096ull + static_cast<uint64_t>(chunks) * 65536ull;
        break;
    }
    case SigKind::Prefetch: {
        if (pos < 4 || pos + 16 > size) return 0;
        const uint32_t version = read_u32(buf + pos - 4);
        const uint32_t file_size = read_u32(buf + pos + 8);
        if (version < 17 || version > 31 || version == 18 || version == 19 ||
            version == 20 || version == 21 || version == 22 ||
            version == 24 || version == 25 || version == 27 ||
            version == 28 || version == 29)
            return 0;
        if (file_size < 256 || file_size > max ||
            file_size > size - (pos - 4))
            return 0;
        // The executable name is UTF-16LE at +12, 60 bytes, usually with a
        // printable ASCII subset. Reject all-zero and control-heavy names.
        std::size_t printable = 0;
        for (std::size_t i = 0; i < 30 && pos + 12 + i * 2 + 1 < size; ++i) {
            const uint16_t c = read_u16(buf + pos + 12 + i * 2);
            if (c == 0) break;
            if (c >= 0x20 && c < 0x7F) ++printable;
        }
        if (printable < 3) return 0;
        length = file_size - 4;
        break;
    }
    case SigKind::Mp3: {
        if (pos + 10 <= size && std::memcmp(buf + pos, "ID3", 3) == 0) {
            const uint32_t tag_size =
                (static_cast<uint32_t>(buf[pos + 6] & 0x7F) << 21) |
                (static_cast<uint32_t>(buf[pos + 7] & 0x7F) << 14) |
                (static_cast<uint32_t>(buf[pos + 8] & 0x7F) << 7) |
                (buf[pos + 9] & 0x7F);
            length = 10ull + tag_size;
        }
        break;
    }
    case SigKind::Riff: {
        if (pos + 12 > size) return 0;
        const uint32_t riff_size = read_u32(buf + pos + 4);
        const bool wave = std::memcmp(buf + pos + 8, "WAVE", 4) == 0;
        const bool avi = std::memcmp(buf + pos + 8, "AVI ", 4) == 0;
        if ((wave || avi) && riff_size < max && riff_size > 16)
            length = static_cast<uint64_t>(riff_size) + 8;
        break;
    }
    case SigKind::Mp4: {
        if (pos + 12 > size || std::memcmp(buf + pos + 4, "ftyp", 4) != 0)
            return 0;
        std::size_t cursor = pos;
        bool moov = false;
        bool mdat = false;
        for (int box = 0; box < 100000 && cursor + 8 <= size; ++box) {
            uint64_t box_size = (static_cast<uint64_t>(buf[cursor]) << 24) |
                (static_cast<uint64_t>(buf[cursor + 1]) << 16) |
                (static_cast<uint64_t>(buf[cursor + 2]) << 8) |
                buf[cursor + 3];
            const char* type = reinterpret_cast<const char*>(buf + cursor + 4);
            if (box_size == 0) { cursor = size; break; }
            if (box_size < 8 || cursor + box_size > size) break;
            if (std::memcmp(type, "moov", 4) == 0) moov = true;
            if (std::memcmp(type, "mdat", 4) == 0) mdat = true;
            cursor += static_cast<std::size_t>(box_size);
        }
        if (moov && mdat) length = cursor - pos;
        break;
    }
    case SigKind::Gif: {
        for (std::size_t i = pos + 6; i < size; ++i) {
            if (buf[i] == 0x3B) { length = i + 1 - pos; break; }
        }
        break;
    }
    }
    if (length < 64 || length > max || length > available) return 0;
    return length;
}

struct CarveContext {
    NtfsVolume* volume = nullptr;
    std::wstring directory;
    HANDLE log = INVALID_HANDLE_VALUE;
    uint64_t min_size = 4096;
    uint64_t max_size = 256ull * 1024ull * 1024ull;
    ScanStats* stats = nullptr;
    std::set<uint64_t> seen_offsets;
    std::string source_label = "unallocated";
};

void try_carve(CarveContext& ctx, const uint8_t* buffer, std::size_t size,
               uint64_t absolute_base, const std::wstring& name_prefix) {
    for (const auto& sig : kSignatures) {
        const uint8_t* cursor = buffer;
        const uint8_t* end = buffer + size;
        while (cursor < end) {
            const uint8_t* found = find_bytes(cursor, end, sig.magic, sig.magic_len);
            if (found == end) break;
            const std::size_t pos = static_cast<std::size_t>(found - buffer);
            const uint64_t length = validate_signature(sig.kind, buffer, size, pos,
                                                       ctx.max_size);
            if (length >= ctx.min_size) {
                std::size_t output_pos = pos;
                uint64_t output_length = length;
                if (sig.kind == SigKind::Prefetch && pos >= 4) {
                    output_pos = pos - 4;
                    output_length = length + 4;
                }
                const uint64_t absolute = absolute_base + output_pos;
                if (ctx.seen_offsets.insert(absolute).second) {
                    std::string ext = sig.ext;
                    if (sig.kind == SigKind::Riff && pos + 12 <= size) {
                        if (std::memcmp(buffer + pos + 8, "WAVE", 4) == 0) ext = "wav";
                        else if (std::memcmp(buffer + pos + 8, "AVI ", 4) == 0) ext = "avi";
                    }
                    std::wstring name = name_prefix + from_utf8(sig.name) + L"_" +
                        std::to_wstring(absolute) + L"." + from_utf8(ext);
                    const std::wstring path = join_path(ctx.directory, name);
                    if (write_new_file(path, buffer + output_pos, static_cast<std::size_t>(output_length),
                                       true)) {
                        std::string digest;
                        sha256_buffer(buffer + output_pos, static_cast<std::size_t>(output_length), digest);
                        std::ostringstream line;
                        line << "{\"offset\": " << absolute
                             << ", \"size\": " << output_length
                             << ", \"type\": " << json_escape(sig.name)
                             << ", \"sha256\": " << json_escape(digest)
                             << ", \"output\": " << json_escape_w(path)
                             << ", \"source\": " << json_escape(ctx.source_label)
                             << "}";
                        append_jsonl(ctx.log, line.str());
                        ++ctx.stats->carved;
                        ctx.stats->carved_bytes += output_length;
                    }
                }
            }
            cursor = found + 1;
        }
    }
}

bool load_bitmap(NtfsVolume& volume, std::vector<uint8_t>& bits,
                 uint64_t& clusters, std::wstring& error) {
    const auto it = volume.names().find(6);
    if (it == volume.names().end()) {
        error = L"$Bitmap record missing";
        return false;
    }
    ParsedRecord record;
    if (!volume.read_record(6, record, error)) return false;
    const Attribute* data = find_attribute(record, kAttrData, L"", false);
    if (data == nullptr) {
        error = L"$Bitmap data missing";
        return false;
    }
    const uint64_t bytes = data->nonresident ? data->real_size : data->resident.size();
    if (bytes == 0 || bytes > 512ull * 1024ull * 1024ull) {
        error = L"$Bitmap size implausible";
        return false;
    }
    bits.resize(static_cast<std::size_t>(bytes));
    if (!volume.read_stream(*data, 0, bits.data(), bits.size())) {
        error = L"$Bitmap read failed";
        return false;
    }
    clusters = (std::min)(volume.boot().cluster_count,
                          static_cast<uint64_t>(bits.size()) * 8);
    return true;
}

bool cluster_free(const std::vector<uint8_t>& bits, uint64_t cluster) {
    if (cluster / 8 >= bits.size()) return false;
    return ((bits[cluster / 8] >> (cluster % 8)) & 1) == 0;
}

void carve_unallocated(NtfsVolume& volume, const ScanOptions& options,
                       const std::wstring& directory, HANDLE log,
                       ScanStats& stats) {
    std::vector<uint8_t> bits;
    uint64_t clusters = 0;
    std::wstring error;
    if (!load_bitmap(volume, bits, clusters, error)) {
        std::cerr << "deep: carving skipped: " << to_utf8(error) << "\n";
        return;
    }
    ensure_directory(directory);
    CarveContext ctx;
    ctx.volume = &volume;
    ctx.directory = directory;
    ctx.log = log;
    ctx.min_size = options.min_carve;
    ctx.max_size = options.max_carve;
    ctx.stats = &stats;
    const uint64_t cluster_size = volume.boot().cluster_size;
    const uint64_t chunk_size = 8ull * 1024ull * 1024ull;
    const uint64_t overlap = 64ull * 1024ull;

    uint64_t cluster = 0;
    while (cluster < clusters) {
        if (!cluster_free(bits, cluster)) { ++cluster; continue; }
        const uint64_t start = cluster;
        while (cluster < clusters && cluster_free(bits, cluster)) ++cluster;
        const uint64_t run_clusters = cluster - start;
        const uint64_t run_bytes = run_clusters * cluster_size;
        const uint64_t absolute_base = volume.source().base() + start * cluster_size;
        for (uint64_t offset = 0; offset < run_bytes; offset += chunk_size - overlap) {
            const uint64_t remaining = run_bytes - offset;
            const std::size_t want = static_cast<std::size_t>(
                (std::min)(remaining, chunk_size));
            std::vector<uint8_t> buffer(want);
            if (!volume.source().read(start * cluster_size + offset,
                                      buffer.data(), buffer.size())) break;
            try_carve(ctx, buffer.data(), buffer.size(), absolute_base + offset,
                      L"carved_");
            if (want < chunk_size) break;
        }
    }
    std::cout << "  carving: " << stats.carved << " files, "
              << (stats.carved_bytes / (1024 * 1024)) << " MiB\n";
}

void carve_slack(NtfsVolume& volume, const ScanOptions& options,
                 const std::wstring& directory, HANDLE log, ScanStats& stats) {
    ensure_directory(directory);
    CarveContext ctx;
    ctx.volume = &volume;
    ctx.directory = directory;
    ctx.log = log;
    ctx.min_size = (std::max)(options.min_carve, 256ull);
    ctx.max_size = options.max_carve;
    ctx.stats = &stats;
    ctx.source_label = "slack";
    const uint64_t cluster_size = volume.boot().cluster_size;
    const uint64_t before = stats.carved;

    for (const auto& entry : volume.names()) {
        const NameInfo& info = entry.second;
        if (!info.has_name) continue;
        ParsedRecord record;
        std::wstring error;
        if (!volume.read_record(entry.first, record, error)) continue;
        const Attribute* data = find_attribute(record, kAttrData, L"", false);
        if (data != nullptr && data->nonresident != 0 && data->real_size > 0) {
            const uint64_t tail = data->real_size % cluster_size;
            if (tail != 0 && data->real_size / cluster_size <
                volume.boot().cluster_count) {
                const uint64_t cluster_offset =
                    (data->real_size / cluster_size) * cluster_size;
                std::vector<uint8_t> buffer(static_cast<std::size_t>(cluster_size));
                if (volume.read_stream(*data, cluster_offset, buffer.data(),
                                       buffer.size())) {
                    try_carve(ctx, buffer.data() + tail,
                              buffer.size() - static_cast<std::size_t>(tail),
                              volume.source().base() + cluster_offset + tail,
                              L"slack_" + std::to_wstring(entry.first) + L"_");
                }
            }
        }
        std::vector<uint8_t> raw;
        if (volume.read_record_raw(entry.first, raw) && record.used < raw.size()) {
            try_carve(ctx, raw.data() + record.used,
                      raw.size() - record.used,
                      volume.source().base() +
                          (volume.boot().mft_lcn * cluster_size) +
                          entry.first * volume.boot().record_size + record.used,
                      L"mftex_" + std::to_wstring(entry.first) + L"_");
        }
    }
    std::cout << "  slack: " << (stats.carved - before) << " new files\n";
}

} // namespace

namespace {

std::wstring absolute_path(const std::wstring& path) {
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return {};
    std::vector<wchar_t> buffer(needed + 1);
    const DWORD written = GetFullPathNameW(path.c_str(), needed + 1,
                                            buffer.data(), nullptr);
    if (written == 0 || written >= needed + 1) return {};
    std::wstring result(buffer.data());
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    return result;
}

std::wstring lower_path(std::wstring value) {
    for (wchar_t& c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}

bool protected_target(const std::wstring& path) {
    const std::wstring full = lower_path(absolute_path(path));
    static const wchar_t* kProtected[] = {
        L"c:\\", L"c:\\windows", L"c:\\windows\\system32",
        L"c:\\windows\\syswow64", L"c:\\program files",
        L"c:\\program files (x86)", L"c:\\programdata", L"c:\\users",
        L"c:\\windows\\winsxs"
    };
    for (const wchar_t* candidate : kProtected) {
        if (full == candidate) return true;
    }
    return full.size() <= 3;
}

std::wstring default_audit_dir(const wchar_t* prefix) {
    wchar_t cwd[32768] = {};
    if (GetCurrentDirectoryW(32768, cwd) == 0) return L".";
    return join_path(cwd, std::wstring(L"cases\\") + prefix + L"_" +
        from_utf8(current_utc_id()));
}

struct WipeAudit {
    HANDLE log = INVALID_HANDLE_VALUE;
    std::wstring dir;
};

bool open_audit(const char* action, std::wstring requested_dir,
                WipeAudit& audit, std::wstring& error) {
    audit.dir = requested_dir.empty() ? default_audit_dir(L"wipe") : requested_dir;
    if (!ensure_directory(audit.dir)) {
        error = L"cannot create audit directory";
        return false;
    }
    audit.log = create_jsonl(join_path(audit.dir, L"destruction_audit.jsonl"));
    if (audit.log == INVALID_HANDLE_VALUE) {
        error = L"cannot create destruction audit";
        return false;
    }
    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD computer_len = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer, &computer_len);
    wchar_t user[256] = {};
    DWORD user_len = 256;
    GetUserNameW(user, &user_len);
    std::ostringstream header;
    header << "{\"format\": \"EOG_WIPE_V1\", \"action\": " << json_escape(action)
           << ", \"started_utc\": " << json_escape_w(current_utc_iso())
           << ", \"computer\": " << json_escape_w(computer)
           << ", \"operator\": " << json_escape_w(user) << "}";
    append_jsonl(audit.log, header.str());
    return true;
}

void audit_line(WipeAudit& audit, const std::string& line) {
    if (audit.log != INVALID_HANDLE_VALUE) append_jsonl(audit.log, line);
}

bool write_at(HANDLE file, uint64_t offset, const void* data, std::size_t size) {
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, pos, nullptr, FILE_BEGIN)) return false;
    return write_all(file, data, size);
}

void fill_pattern(std::vector<uint8_t>& buffer, unsigned pass) {
    if (pass == 0) {
        std::memset(buffer.data(), 0x00, buffer.size());
    } else if (pass == 1) {
        std::memset(buffer.data(), 0xFF, buffer.size());
    } else if (BCryptGenRandom(nullptr, buffer.data(),
                               static_cast<ULONG>(buffer.size()),
                               BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        std::memset(buffer.data(), static_cast<int>(0xA5 ^ pass), buffer.size());
    }
}

bool wipe_streams(const std::wstring& path, unsigned passes, unsigned& streams_wiped) {
    WIN32_FIND_STREAM_DATA stream{};
    HANDLE search = FindFirstStreamW(path.c_str(), FindStreamInfoStandard,
                                     &stream, 0);
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_HANDLE_EOF;
    bool ok = true;
    do {
        if (_wcsicmp(stream.cStreamName, L"::$DATA") == 0) continue;
        const std::wstring stream_path = path + stream.cStreamName;
        HANDLE handle = CreateFileW(extend_path(stream_path).c_str(),
            GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size{};
        if (GetFileSizeEx(handle, &size) && size.QuadPart > 0) {
            std::vector<uint8_t> buffer(256 * 1024);
            for (unsigned pass = 0; pass < passes; ++pass) {
                fill_pattern(buffer, pass);
                SetFilePointerEx(handle, LARGE_INTEGER{}, nullptr, FILE_BEGIN);
                uint64_t remaining = static_cast<uint64_t>(size.QuadPart);
                while (remaining > 0) {
                    const std::size_t chunk = static_cast<std::size_t>(
                        (std::min)(remaining, static_cast<uint64_t>(buffer.size())));
                    if (!write_all(handle, buffer.data(), chunk)) { ok = false; break; }
                    remaining -= chunk;
                }
                FlushFileBuffers(handle);
            }
        }
        CloseHandle(handle);
        if (!DeleteFileW(extend_path(stream_path).c_str())) ok = false;
        ++streams_wiped;
    } while (FindNextStreamW(search, &stream));
    FindClose(search);
    return ok;
}

bool wipe_one_file(const std::wstring& path, unsigned passes, WipeAudit& audit,
                   unsigned& failed, unsigned& wiped, uint64_t& bytes_wiped) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        audit_line(audit, "{\"action\": \"wipe_file\", \"path\": " +
            json_escape_w(path) + ", \"status\": \"missing\"}");
        ++failed;
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        audit_line(audit, "{\"action\": \"wipe_file\", \"path\": " +
            json_escape_w(path) + ", \"status\": \"reparse_skipped\"}");
        ++failed;
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA info{};
    GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info);
    const uint64_t size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) |
        info.nFileSizeLow;
    std::string before_hash;
    sha256_file(path, before_hash);
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }
    unsigned streams = 0;
    wipe_streams(path, passes, streams);

    HANDLE handle = CreateFileW(extend_path(path).c_str(),
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    std::string status = "wiped";
    if (handle == INVALID_HANDLE_VALUE) {
        status = "open_failed_" + std::to_string(GetLastError());
    } else {
        std::vector<uint8_t> buffer(1024 * 1024);
    std::vector<uint8_t> final_pattern;
    bool ok = true;
    for (unsigned pass = 0; pass < passes && ok; ++pass) {
        fill_pattern(buffer, pass);
        if (pass + 1 == passes) final_pattern = buffer;
        SetFilePointerEx(handle, LARGE_INTEGER{}, nullptr, FILE_BEGIN);
        uint64_t remaining = size;
        while (remaining > 0) {
            const std::size_t chunk = static_cast<std::size_t>(
                (std::min)(remaining, static_cast<uint64_t>(buffer.size())));
            if (!write_all(handle, buffer.data(), chunk)) { ok = false; break; }
            remaining -= chunk;
        }
        if (!FlushFileBuffers(handle)) ok = false;
    }
    if (ok && size > 0 && !final_pattern.empty()) {
        const std::size_t probe = static_cast<std::size_t>(
            (std::min)(size, static_cast<uint64_t>(final_pattern.size())));
        std::vector<uint8_t> verify(probe);
        SetFilePointerEx(handle, LARGE_INTEGER{}, nullptr, FILE_BEGIN);
        DWORD read = 0;
        if (!ReadFile(handle, verify.data(), static_cast<DWORD>(verify.size()),
                      &read, nullptr) || read != verify.size() ||
            std::memcmp(verify.data(), final_pattern.data(), verify.size()) != 0) {
            ok = false;
            status = "verify_failed";
        }
    }
        FlushFileBuffers(handle);
        CloseHandle(handle);
        if (!ok && status == "wiped") status = "write_failed";
        bytes_wiped += size;
    }
    if (status == "wiped" || status == "verify_failed") {
        const std::size_t slash = path.find_last_of(L'\\');
        const std::wstring dir = slash == std::wstring::npos
            ? L"." : path.substr(0, slash);
        uint8_t random[8] = {};
        if (BCryptGenRandom(nullptr, random, sizeof(random),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            std::memset(random, 0x5A, sizeof(random));
        }
        const std::wstring new_name = join_path(dir,
            std::wstring(L"eog_") + from_utf8(hex_bytes(
                std::vector<uint8_t>(random, random + sizeof(random)))) + L".tmp");
        if (MoveFileExW(path.c_str(), new_name.c_str(), MOVEFILE_WRITE_THROUGH)) {
            SetFileAttributesW(new_name.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        const std::wstring& final_path = GetFileAttributesW(new_name.c_str()) !=
            INVALID_FILE_ATTRIBUTES ? new_name : path;
        SetFileAttributesW(final_path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(extend_path(final_path).c_str())) {
            status = "delete_failed_" + std::to_string(GetLastError());
        }
    }
    std::ostringstream line;
    line << "{\"action\": \"wipe_file\", \"path\": " << json_escape_w(path)
         << ", \"size\": " << size
         << ", \"sha256_before\": " << json_escape(before_hash)
         << ", \"passes\": " << passes
         << ", \"streams\": " << streams
         << ", \"status\": " << json_escape(status) << "}";
    audit_line(audit, line.str());
    if (status == "wiped") {
        ++wiped;
        return true;
    }
    ++failed;
    return false;
}

void wipe_directory(const std::wstring& directory, unsigned passes,
                    WipeAudit& audit, unsigned& failed, unsigned& wiped,
                    uint64_t& bytes_wiped) {
    const std::wstring pattern = join_path(directory, L"*");
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        ++failed;
        return;
    }
    std::vector<std::wstring> directories;
    do {
        if (wcscmp(entry.cFileName, L".") == 0 ||
            wcscmp(entry.cFileName, L"..") == 0) continue;
        const std::wstring item = join_path(directory, entry.cFileName);
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                directories.push_back(item);
            else
                ++failed;
        } else {
            wipe_one_file(item, passes, audit, failed, wiped, bytes_wiped);
        }
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    for (const auto& dir : directories) {
        wipe_directory(dir, passes, audit, failed, wiped, bytes_wiped);
        if (!RemoveDirectoryW(dir.c_str())) ++failed;
    }
}

std::vector<std::pair<uint64_t, uint64_t>> free_cluster_runs(
    const std::vector<uint8_t>& bits, uint64_t clusters) {
    std::vector<std::pair<uint64_t, uint64_t>> runs;
    uint64_t cluster = 0;
    while (cluster < clusters) {
        if (!cluster_free(bits, cluster)) { ++cluster; continue; }
        const uint64_t start = cluster;
        while (cluster < clusters && cluster_free(bits, cluster)) ++cluster;
        runs.emplace_back(start, cluster - start);
    }
    return runs;
}

void audit_summary(WipeAudit& audit, const char* action, const std::string& detail) {
    std::ostringstream line;
    line << "{\"action\": " << json_escape(action)
         << ", \"completed_utc\": " << json_escape_w(current_utc_iso())
         << ", " << detail << "}";
    audit_line(audit, line.str());
}

bool open_write_target(const std::wstring& target, bool volume, HANDLE& handle,
                       std::wstring& error) {
    const std::wstring path = volume ? volume_path(target) : extend_path(target);
    handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = L"write open failed, error " +
            std::to_wstring(GetLastError()) +
            (volume ? L" (admin and unlocked volume required)" : L"");
        return false;
    }
    return true;
}

} // namespace

int run_scan(const ScanOptions& options) {

    SetConsoleOutputCP(CP_UTF8);
    if (options.source.empty()) {
        std::cerr << "deep: no source image/volume\n";
        return 2;
    }

    NtfsVolume volume;
    std::wstring error;
    if (!volume.open(options.source, options.offset, error)) {
        std::cerr << "deep: open failed: " << to_utf8(error) << "\n";
        return 1;
    }

    uint64_t record_count = 0;
    if (!volume.load_index(error, record_count)) {
        std::cerr << "deep: MFT index failed: " << to_utf8(error) << "\n";
        return 1;
    }

    std::wstring out = options.out_dir;
    if (out.empty()) {
        wchar_t cwd[32768] = {};
        if (GetCurrentDirectoryW(32768, cwd) == 0) {
            std::cerr << "deep: cannot read current directory\n";
            return 1;
        }
        out = join_path(cwd, L"cases\\deep_" + from_utf8(current_utc_id()));
    }
    if (!ensure_directory(out)) {
        std::cerr << "deep: cannot create output directory\n";
        return 1;
    }
    const std::wstring recovered_dir = join_path(out, L"recovered");
    if (options.recover) ensure_directory(recovered_dir);

    ScanStats stats;
    stats.records = record_count;

    std::ostringstream volume_info;
    volume_info << "{\n"
        << "  \"source\": " << json_escape_w(options.source) << ",\n"
        << "  \"volume_offset\": " << volume.source().base() << ",\n"
        << "  \"bytes_per_sector\": " << volume.boot().bytes_per_sector << ",\n"
        << "  \"sectors_per_cluster\": " << volume.boot().sectors_per_cluster << ",\n"
        << "  \"cluster_size\": " << volume.boot().cluster_size << ",\n"
        << "  \"mft_record_size\": " << volume.boot().record_size << ",\n"
        << "  \"mft_lcn\": " << volume.boot().mft_lcn << ",\n"
        << "  \"serial\": " << hex64(volume.boot().serial) << ",\n"
        << "  \"volume_bytes\": " << volume.boot().volume_bytes << ",\n"
        << "  \"cluster_count\": " << volume.boot().cluster_count << ",\n"
        << "  \"mft_records\": " << record_count << "\n}\n";
    write_new_file(join_path(out, L"volume_info.json"),
        volume_info.str().data(), volume_info.str().size(), true);

    HANDLE inventory = INVALID_HANDLE_VALUE;
    if (options.list) {
        inventory = create_jsonl(join_path(out, L"mft_inventory.jsonl"));
        if (inventory == INVALID_HANDLE_VALUE) {
            std::cerr << "deep: cannot create inventory\n";
            return 1;
        }
    }
    HANDLE recovered_jsonl = INVALID_HANDLE_VALUE;
    if (options.recover) {
        recovered_jsonl = create_jsonl(join_path(out, L"recovered.jsonl"));
        if (recovered_jsonl == INVALID_HANDLE_VALUE) {
            std::cerr << "deep: cannot create recovery log\n";
            return 1;
        }
    }

    std::uint64_t processed = 0;
    for (uint64_t i = 0; i < record_count; ++i) {
        ParsedRecord record;
        if (!volume.read_record(i, record, error)) continue;
        if (!record.valid) continue;
        ++processed;
        const auto it = volume.names().find(record.number);
        if (it == volume.names().end()) continue;
        const NameInfo& info = it->second;
        if (info.in_use) ++stats.in_use;
        if (info.directory) ++stats.directories;
        if (!info.in_use) ++stats.deleted;
        bool resolved = false;
        std::wstring path = volume.resolve_path(record.number, resolved);
        if (options.list) {
            uint32_t si_flags = 0;
            const Attribute* si = find_attribute(record, kAttrStandardInformation, L"", false);
            if (si && si->resident.size() >= 36) si_flags = read_u32(si->resident.data() + 32);
            std::ostringstream line;
            line << "{\"record\": " << record.number
                 << ", \"sequence\": " << record.sequence
                 << ", \"in_use\": " << (info.in_use ? "true" : "false")
                 << ", \"directory\": " << (info.directory ? "true" : "false")
                 << ", \"name\": " << json_escape_w(info.name)
                 << ", \"namespace\": " << static_cast<unsigned>(info.name_space)
                 << ", \"parent_record\": " << info.parent
                 << ", \"parent_sequence\": " << info.parent_sequence
                 << ", \"path\": " << json_escape_w(path)
                 << ", \"path_confidence\": "
                 << (resolved ? "\"parent_sequence_match\"" : "\"orphan_or_stale_parent\"")
                 << ", \"size_bytes\": " << info.size
                 << ", \"allocated_bytes\": " << info.alloc_size
                 << ", \"si_flags\": " << si_flags
                 << ", \"created_utc\": " << json_escape(format_filetime(info.created))
                 << ", \"modified_utc\": " << json_escape(format_filetime(info.modified))
                 << ", \"mft_modified_utc\": " << json_escape(format_filetime(info.mft_modified))
                 << ", \"accessed_utc\": " << json_escape(format_filetime(info.accessed))
                 << ", \"streams\": " << streams_json(record, volume.boot()) << "}";
            append_jsonl(inventory, line.str());
        }
        if (options.recover && !info.in_use && !info.directory) {
            const std::wstring case_id = from_utf8(current_utc_id());
            recover_record(volume, record, info, recovered_dir, options.max_carve,
                           stats, recovered_jsonl, case_id);
        }
        if (processed % 2000 == 0) {
            std::cout << "  MFT: " << processed << "/" << record_count << "\r";
            std::cout.flush();
        }
    }
    std::cout << "  MFT: " << processed << "/" << record_count << "\n";
    if (inventory != INVALID_HANDLE_VALUE) CloseHandle(inventory);
    if (recovered_jsonl != INVALID_HANDLE_VALUE) CloseHandle(recovered_jsonl);

    if (options.usn) {
        std::vector<uint8_t> journal;
        std::wstring usn_error;
        if (!read_usn_stream(volume, journal, usn_error)) {
            std::cerr << "deep: USN: " << to_utf8(usn_error) << "\n";
        } else {
            HANDLE usn = create_jsonl(join_path(out, L"usn_journal.jsonl"));
            if (usn != INVALID_HANDLE_VALUE) {
                parse_usn_records(journal, volume.names(), usn, stats);
                CloseHandle(usn);
            }
            std::cout << "  USN records: " << stats.usn_records << "\n";
        }
    }

    if (options.carve) {
        HANDLE carve_log = create_jsonl(join_path(out, L"carved.jsonl"));
        if (carve_log == INVALID_HANDLE_VALUE) {
            std::cerr << "deep: cannot create carving log\n";
        } else {
            carve_unallocated(volume, options, join_path(out, L"carved"),
                              carve_log, stats);
            CloseHandle(carve_log);
        }
    }
    if (options.slack) {
        HANDLE slack_log = create_jsonl(join_path(out, L"slack.jsonl"));
        if (slack_log == INVALID_HANDLE_VALUE) {
            std::cerr << "deep: cannot create slack log\n";
        } else {
            carve_slack(volume, options, join_path(out, L"slack"),
                        slack_log, stats);
            CloseHandle(slack_log);
        }
    }

    std::ostringstream summary;
    summary << "{\n"
        << "  \"records\": " << stats.records << ",\n"
        << "  \"in_use\": " << stats.in_use << ",\n"
        << "  \"deleted\": " << stats.deleted << ",\n"
        << "  \"directories\": " << stats.directories << ",\n"
        << "  \"recovered\": " << stats.recovered << ",\n"
        << "  \"recovery_failed\": " << stats.recovery_failed << ",\n"
        << "  \"usn_records\": " << stats.usn_records << ",\n"
        << "  \"carved\": " << stats.carved << ",\n"
        << "  \"carved_bytes\": " << stats.carved_bytes << ",\n"
        << "  \"slack_carved\": " << stats.slack_carved << ",\n"
        << "  \"output\": " << json_escape_w(out) << "\n}\n";
    write_new_file(join_path(out, L"summary.json"),
        summary.str().data(), summary.str().size(), true);

    std::cout << "deep: records=" << stats.records
              << " in_use=" << stats.in_use
              << " deleted=" << stats.deleted
              << " recovered=" << stats.recovered
              << " failed=" << stats.recovery_failed << "\n";
    std::cout << "deep: output=" << to_utf8(out) << "\n";
    return 0;
}

int run_usn_file(const std::wstring& journal_file, const std::wstring& out_dir) {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE file = CreateFileW(extend_path(journal_file).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << "deep: cannot open journal file\n";
        return 1;
    }
    LARGE_INTEGER length{};
    GetFileSizeEx(file, &length);
    if (length.QuadPart <= 0 || length.QuadPart > 4ll * 1024 * 1024 * 1024) {
        CloseHandle(file);
        std::cerr << "deep: journal size implausible\n";
        return 1;
    }
    std::vector<uint8_t> journal(static_cast<std::size_t>(length.QuadPart));
    DWORD read = 0;
    const bool ok = ReadFile(file, journal.data(),
        static_cast<DWORD>(journal.size()), &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!ok || read != journal.size()) {
        std::cerr << "deep: journal read failed\n";
        return 1;
    }
    std::wstring out = out_dir;
    if (out.empty()) {
        wchar_t cwd[32768] = {};
        if (GetCurrentDirectoryW(32768, cwd) == 0) return 1;
        out = join_path(cwd, L"cases\\usn_" + from_utf8(current_utc_id()));
    }
    if (!ensure_directory(out)) {
        std::cerr << "deep: cannot create output\n";
        return 1;
    }
    HANDLE log = create_jsonl(join_path(out, L"usn_journal.jsonl"));
    if (log == INVALID_HANDLE_VALUE) return 1;
    ScanStats stats;
    std::unordered_map<uint64_t, NameInfo> empty;
    parse_usn_records(journal, empty, log, stats);
    CloseHandle(log);
    std::cout << "deep: USN records=" << stats.usn_records
              << " output=" << to_utf8(out) << "\n";
    return 0;
}

int run_wipe_file(const WipeOptions& options) {
    SetConsoleOutputCP(CP_UTF8);
    if (!options.confirm) {
        std::cerr << "wipe: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    if (options.target.empty()) {
        std::cerr << "wipe: no target\n";
        return 2;
    }
    const std::wstring target = absolute_path(options.target);
    if (target.empty()) {
        std::cerr << "wipe: cannot normalize target\n";
        return 2;
    }
    if (protected_target(target)) {
        std::cerr << "wipe: refusing a protected root (" << to_utf8(target) << ")\n";
        return 2;
    }
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "wipe: target not found\n";
        return 2;
    }
    WipeAudit audit;
    std::wstring error;
    if (!open_audit("wipe_file", options.out_dir, audit, error)) {
        std::cerr << "wipe: " << to_utf8(error) << "\n";
        return 1;
    }
    const unsigned passes = (std::max)(1u, (std::min)(options.passes, 7u));
    unsigned failed = 0, wiped = 0;
    uint64_t bytes = 0;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        wipe_directory(target, passes, audit, failed, wiped, bytes);
        if (!RemoveDirectoryW(target.c_str())) ++failed;
    } else {
        wipe_one_file(target, passes, audit, failed, wiped, bytes);
    }
    std::ostringstream detail;
    detail << "\"target\": " << json_escape_w(target)
           << ", \"files_wiped\": " << wiped
           << ", \"failed\": " << failed
           << ", \"bytes\": " << bytes
           << ", \"passes\": " << passes;
    audit_summary(audit, "wipe_file_session", detail.str());
    if (audit.log != INVALID_HANDLE_VALUE) CloseHandle(audit.log);
    std::cout << "wipe: files=" << wiped << " failed=" << failed
              << " bytes=" << bytes << " passes=" << passes << "\n";
    std::cout << "wipe: audit=" << to_utf8(audit.dir) << "\n";
    return failed == 0 ? 0 : 1;
}

int run_wipe_free(const WipeOptions& options) {
    SetConsoleOutputCP(CP_UTF8);
    if (!options.confirm) {
        std::cerr << "wipe-free: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    if (options.target.empty()) {
        std::cerr << "wipe-free: no image/volume\n";
        return 2;
    }
    NtfsVolume volume;
    std::wstring error;
    if (!volume.open(options.target, options.offset, error)) {
        std::cerr << "wipe-free: " << to_utf8(error) << "\n";
        return 1;
    }
    std::vector<uint8_t> bits;
    uint64_t clusters = 0;
    uint64_t indexed_records = 0;
    if (!volume.load_index(error, indexed_records)) {
        std::cerr << "wipe-free: " << to_utf8(error) << "\n";
        return 1;
    }
    if (!load_bitmap(volume, bits, clusters, error)) {
        std::cerr << "wipe-free: " << to_utf8(error) << "\n";
        return 1;
    }
    const auto runs = free_cluster_runs(bits, clusters);
    const uint64_t cluster_size = volume.boot().cluster_size;
    const uint64_t base = volume.source().base();
    const bool is_volume = volume.source().volume();
    struct MftWipe { uint64_t physical; uint32_t length; };
    std::vector<MftWipe> mft_wipes;
    uint64_t record_count = volume.record_count();
    for (uint64_t i = 0; i < record_count; ++i) {
        std::vector<uint8_t> raw;
        if (!volume.read_record_raw(i, raw)) continue;
        if (std::memcmp(raw.data(), "FILE", 4) != 0) continue;
        uint64_t physical = 0;
        if (!volume.mft_record_physical(i, physical)) continue;
        const uint16_t flags = read_u16(raw.data() + 22);
        const uint32_t used = read_u32(raw.data() + 24);
        (void)used;
        if ((flags & 1) == 0) {
            // Only unused records: their fixup bytes are not needed anymore.
            // Zeroing the slack of a live record destroys the update sequence
            // at sector ends and makes the record unreadable.
            mft_wipes.push_back({physical, volume.boot().record_size});
        }
    }
    volume.close();

    HANDLE writer = INVALID_HANDLE_VALUE;
    if (!open_write_target(options.target, is_volume, writer, error)) {
        std::cerr << "wipe-free: " << to_utf8(error) << "\n";
        return 1;
    }
    if (is_volume) {
        DWORD returned = 0;
        if (!DeviceIoControl(writer, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0,
                             &returned, nullptr)) {
            std::cerr << "wipe-free: volume is busy and cannot be locked; refusing\n";
            CloseHandle(writer);
            return 1;
        }
    }
    WipeAudit audit;
    if (!open_audit("wipe_free", options.out_dir, audit, error)) {
        CloseHandle(writer);
        std::cerr << "wipe-free: " << to_utf8(error) << "\n";
        return 1;
    }
    const unsigned passes = (std::max)(1u, (std::min)(options.passes, 7u));
    std::vector<uint8_t> buffer(1024 * 1024);
    std::vector<uint8_t> final_pattern;
    uint64_t bytes = 0;
    bool ok = true;
    for (unsigned pass = 0; pass < passes && ok; ++pass) {
        fill_pattern(buffer, pass);
        if (pass + 1 == passes) final_pattern = buffer;
        for (const auto& run : runs) {
            uint64_t written = 0;
            const uint64_t total = run.second * cluster_size;
            while (written < total) {
                const std::size_t chunk = static_cast<std::size_t>(
                    (std::min)(total - written,
                               static_cast<uint64_t>(buffer.size())));
                const uint64_t absolute = base + run.first * cluster_size + written;
                if (!write_at(writer, absolute, buffer.data(), chunk)) {
                    ok = false;
                    break;
                }
                written += chunk;
                bytes += chunk;
            }
            if (!ok) break;
        }
        FlushFileBuffers(writer);
    }
    std::vector<uint8_t> zero(4096, 0);
    uint64_t mft_bytes = 0;
    if (ok) {
        for (const auto& item : mft_wipes) {
            if (!write_at(writer, item.physical, zero.data(),
                          (std::min)(zero.size(),
                                     static_cast<std::size_t>(item.length)))) {
                ok = false;
                break;
            }
            mft_bytes += item.length;
        }
        FlushFileBuffers(writer);
    }

    bool verified = false;
    if (ok && !runs.empty() && !final_pattern.empty()) {
        std::vector<uint8_t> probe(
            (std::min)(final_pattern.size(), static_cast<std::size_t>(4096)));
        if (volume.open(options.target, options.offset, error)) {
            if (volume.source().read(runs.front().first * cluster_size,
                                     probe.data(), probe.size())) {
                verified = std::memcmp(probe.data(), final_pattern.data(),
                                       probe.size()) == 0;
            }
        }
    }
    CloseHandle(writer);
    std::ostringstream detail;
    detail << "\"target\": " << json_escape_w(options.target)
           << ", \"free_runs\": " << runs.size()
           << ", \"clusters\": " << clusters
           << ", \"cluster_bytes\": " << bytes
           << ", \"mft_bytes\": " << mft_bytes
           << ", \"passes\": " << passes
           << ", \"verified\": " << (verified ? "true" : "false")
           << ", \"status\": " << json_escape(ok ? "completed" : "failed");
    audit_summary(audit, "wipe_free_session", detail.str());
    if (audit.log != INVALID_HANDLE_VALUE) CloseHandle(audit.log);
    std::cout << "wipe-free: runs=" << runs.size() << " bytes=" << bytes
              << " mft_bytes=" << mft_bytes
              << " verified=" << (verified ? "yes" : "no") << "\n";
    std::cout << "wipe-free: audit=" << to_utf8(audit.dir) << "\n";
    return ok ? 0 : 1;
}

int run_wipe_journal(const std::wstring& image, std::uint64_t offset,
                     const std::wstring& out_dir, bool confirm) {
    if (!confirm) {
        std::cerr << "wipe-journal: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    NtfsVolume volume;
    std::wstring error;
    if (!volume.open(image, offset, error)) {
        std::cerr << "wipe-journal: " << to_utf8(error) << "\n";
        return 1;
    }
    std::uint64_t records = 0;
    if (!volume.load_index(error, records)) {
        std::cerr << "wipe-journal: " << to_utf8(error) << "\n";
        return 1;
    }
    struct Run {
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
    };
    std::vector<Run> runs;
    const std::uint64_t cluster = volume.boot().cluster_size;
    const std::uint64_t base = volume.source().base();
    for (const auto& entry : volume.names()) {
        const NameInfo& info = entry.second;
        if (!info.in_use || !info.has_name) continue;
        const bool logfile = _wcsicmp(info.name.c_str(), L"$LogFile") == 0;
        const bool usn = _wcsicmp(info.name.c_str(), L"$UsnJrnl") == 0;
        if (!logfile && !usn) continue;
        ParsedRecord record;
        if (!volume.read_record(info.number, record, error)) continue;
        for (const auto& attr : record.attributes) {
            if (attr.type != kAttrData || attr.nonresident == 0) continue;
            if (usn && _wcsicmp(attr.name.c_str(), L"$J") != 0) continue;
            for (const auto& run : attr.runs) {
                if (run.sparse || run.length == 0) continue;
                runs.push_back(Run{base + run.lcn * cluster,
                                   run.length * cluster});
            }
        }
    }
    volume.close();
    if (runs.empty()) {
        std::cerr << "wipe-journal: $LogFile/$J runs not found\n";
        return 1;
    }
    HANDLE file = CreateFileW(image.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << "wipe-journal: write open failed, error "
                  << GetLastError() << "\n";
        return 1;
    }
    std::vector<std::uint8_t> zeros(1024 * 1024, 0);
    std::uint64_t bytes = 0;
    bool ok = true;
    for (const auto& run : runs) {
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(run.offset);
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
            ok = false;
            break;
        }
        std::uint64_t remaining = run.length;
        while (remaining > 0) {
            const DWORD chunk = static_cast<DWORD>(
                (std::min)(remaining, static_cast<std::uint64_t>(zeros.size())));
            DWORD written = 0;
            if (!WriteFile(file, zeros.data(), chunk, &written, nullptr) ||
                written != chunk) {
                ok = false;
                break;
            }
            remaining -= chunk;
            bytes += chunk;
        }
        if (!ok) break;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) {
        std::cerr << "wipe-journal: write failed\n";
        return 1;
    }
    const std::wstring audit_dir = out_dir.empty()
        ? join_path(L"cases", L"wipe_journal_" + from_utf8(current_utc_id())) : out_dir;
    ensure_directory(audit_dir);
    HANDLE audit = CreateFileW(join_path(audit_dir,
        L"journal_sanitize.jsonl").c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (audit != INVALID_HANDLE_VALUE) {
        std::ostringstream line;
        line << "{\"action\":\"wipe_journal\",\"image\":"
             << json_escape_w(image) << ",\"runs\":" << runs.size()
             << ",\"bytes\":" << bytes << ",\"completed_utc\":"
             << json_escape(art::now_iso()) << "}";
        std::string text = line.str() + "\r\n";
        DWORD written = 0;
        WriteFile(audit, text.data(), static_cast<DWORD>(text.size()), &written,
                  nullptr);
        CloseHandle(audit);
    }
    std::cout << "wipe-journal: runs=" << runs.size() << " bytes=" << bytes
              << "\n";
    std::cout << "wipe-journal: audit=" << to_utf8(audit_dir) << "\n";
    return 0;
}

int run_vss_read(int number, const std::wstring& out_dir) {
    if (number <= 0) {
        std::cerr << "vss-read: shadow number must be positive\n";
        return 2;
    }
    const std::wstring device =
        L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopy" +
        std::to_wstring(number);
    ScanOptions options;
    options.source = device;
    options.out_dir = out_dir;
    options.list = true;
    options.recover = true;
    options.usn = true;
    options.carve = false;
    options.slack = false;
    return run_scan(options);
}

int run_purge_usn(const std::wstring& volume_name, bool confirm) {    SetConsoleOutputCP(CP_UTF8);
    if (!confirm) {
        std::cerr << "purge-usn: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    const std::wstring device = volume_path(volume_name);
    HANDLE volume = CreateFileW(device.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_RANDOM_ACCESS, nullptr);
    WipeAudit audit;
    std::wstring error;
    if (!open_audit("purge_usn", L"", audit, error)) return 1;
    if (volume == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        std::cerr << "purge-usn: open failed, error " << code
                  << " (admin required)\n";
        audit_summary(audit, "purge_usn", "\"status\": \"open_failed\", \"error\": " +
            std::to_string(code));
        CloseHandle(audit.log);
        return 1;
    }
    DELETE_USN_JOURNAL_DATA data{};
    data.UsnJournalID = 0;
    data.DeleteFlags = USN_DELETE_FLAG_DELETE;
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(volume, FSCTL_DELETE_USN_JOURNAL,
        &data, sizeof(data), nullptr, 0, &returned, nullptr);
    const DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(volume);
    audit_summary(audit, "purge_usn", "\"status\": " +
        std::string(ok ? "\"deleted\"" : "\"failed\"") +
        ", \"error\": " + std::to_string(code));
    CloseHandle(audit.log);
    std::cout << "purge-usn: " << (ok ? "deleted" : "failed")
              << " error=" << code << "\n";
    std::cout << "purge-usn: audit=" << to_utf8(audit.dir) << "\n";
    return ok ? 0 : 1;
}

int run_clear_logs(bool confirm) {
    SetConsoleOutputCP(CP_UTF8);
    if (!confirm) {
        std::cerr << "clear-logs: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    WipeAudit audit;
    std::wstring error;
    if (!open_audit("clear_logs", L"", audit, error)) return 1;
    EVT_HANDLE channels = EvtOpenChannelEnum(nullptr, 0);
    if (channels == nullptr) {
        std::cerr << "clear-logs: channel enumeration failed\n";
        CloseHandle(audit.log);
        return 1;
    }
    unsigned cleared = 0, failed = 0;
    std::vector<wchar_t> name(1024);
    DWORD used = 0;
    while (EvtNextChannelPath(channels, static_cast<DWORD>(name.size()),
                              name.data(), &used)) {
        const std::wstring channel(name.data());
        const BOOL ok = EvtClearLog(nullptr, channel.c_str(), nullptr, 0);
        const DWORD code = ok ? ERROR_SUCCESS : GetLastError();
        std::ostringstream line;
        line << "{\"action\": \"clear_log\", \"channel\": "
             << json_escape_w(channel)
             << ", \"status\": " << json_escape(ok ? "cleared" : "failed")
             << ", \"error\": " << code << "}";
        audit_line(audit, line.str());
        if (ok) ++cleared; else ++failed;
        std::fill(name.begin(), name.end(), L'\0');
    }
    EvtClose(channels);
    audit_summary(audit, "clear_logs_session",
        "\"cleared\": " + std::to_string(cleared) +
        ", \"failed\": " + std::to_string(failed));
    CloseHandle(audit.log);
    std::cout << "clear-logs: cleared=" << cleared << " failed=" << failed
              << "\n";
    std::cout << "clear-logs: audit=" << to_utf8(audit.dir) << "\n";
    return 0;
}

int run_wipe_artifacts(bool confirm) {
    SetConsoleOutputCP(CP_UTF8);
    if (!confirm) {
        std::cerr << "wipe-artifacts: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    WipeAudit audit;
    std::wstring error;
    if (!open_audit("wipe_artifacts", L"", audit, error)) return 1;
    wchar_t windows[MAX_PATH] = {};
    wchar_t appdata[MAX_PATH] = {};
    wchar_t localappdata[MAX_PATH] = {};
    GetWindowsDirectoryW(windows, MAX_PATH);
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata);
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT,
                     localappdata);
    std::vector<std::wstring> directories = {
        join_path(windows, L"Prefetch"),
        join_path(appdata, L"Microsoft\\Windows\\Recent"),
        join_path(localappdata, L"Microsoft\\Windows\\WER\\ReportArchive"),
        join_path(localappdata, L"Microsoft\\Windows\\WER\\ReportQueue"),
    };
    unsigned failed = 0, wiped = 0;
    uint64_t bytes = 0;
    for (const auto& dir : directories) {
        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        wipe_directory(dir, 1, audit, failed, wiped, bytes);
    }
    const std::wstring thumb_dir =
        join_path(localappdata, L"Microsoft\\Windows\\Explorer");
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW(join_path(thumb_dir, L"thumbcache_*.db").c_str(),
                                   &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            wipe_one_file(join_path(thumb_dir, entry.cFileName), 1, audit,
                          failed, wiped, bytes);
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    audit_summary(audit, "wipe_artifacts_session",
        "\"files_wiped\": " + std::to_string(wiped) +
        ", \"failed\": " + std::to_string(failed) +
        ", \"bytes\": " + std::to_string(bytes));
    CloseHandle(audit.log);
    std::cout << "wipe-artifacts: files=" << wiped << " failed=" << failed
              << " bytes=" << bytes << "\n";
    std::cout << "wipe-artifacts: audit=" << to_utf8(audit.dir) << "\n";
    return failed == 0 ? 0 : 1;
}

int run_delete_vss(bool confirm) {
    SetConsoleOutputCP(CP_UTF8);
    if (!confirm) {
        std::cerr << "delete-vss: confirmation missing; pass --confirm WIPE\n";
        return 2;
    }
    WipeAudit audit;
    std::wstring error;
    if (!open_audit("delete_vss", L"", audit, error)) return 1;
    wchar_t system_dir[MAX_PATH] = {};
    GetSystemDirectoryW(system_dir, MAX_PATH);
    const std::wstring exe = join_path(system_dir, L"vssadmin.exe");
    std::wstring command = L"\"" + exe + L"\" delete shadows /all /quiet";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(exe.c_str(), mutable_command.data(),
        nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
        &startup, &process);
    DWORD exit_code = static_cast<DWORD>(-1);
    if (started) {
        WaitForSingleObject(process.hProcess, 120000);
        GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    audit_summary(audit, "delete_vss",
        "\"started\": " + std::string(started ? "true" : "false") +
        ", \"exit_code\": " + std::to_string(exit_code));
    CloseHandle(audit.log);
    std::cout << "delete-vss: started=" << (started ? "yes" : "no")
              << " exit=" << exit_code << "\n";
    std::cout << "delete-vss: audit=" << to_utf8(audit.dir) << "\n";
    return started && exit_code == 0 ? 0 : 1;
}

namespace {

bool has_flag(const std::vector<std::wstring>& args, const wchar_t* name) {
    for (const auto& arg : args) if (arg == name) return true;
    return false;
}

bool option_value(const std::vector<std::wstring>& args, const wchar_t* name,
                  std::wstring& value) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name && args[i + 1].rfind(L"--", 0) != 0) {
            value = args[i + 1];
            return true;
        }
    }
    return false;
}

std::uint64_t parse_number(const std::wstring& text, bool& ok) {
    ok = false;
    if (text.empty()) return 0;
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != L'\0') return 0;
    ok = true;
    return static_cast<std::uint64_t>(value);
}

std::string deep_help() {
    return
        "Olho de Deus - deep engine\n"
        "  --deep-scan <imagem|volume> [--offset N] [--out DIR] [--list] [--recover]\n"
        "              [--usn] [--carve] [--slack] [--min-size N]\n"
        "  --deep-usn-file <$J bruto> [--out DIR]\n"
        "  --artifacts [--out DIR] [--db ARQUIVO] [--no-registry] [--no-browser]\n"
        "              [--no-recycle] [--no-lnk] [--no-jumplist] [--no-prefetch]\n"
        "              [--no-shellbags] [--prefetch-dir DIR]\n"
        "              [--hive-file HIVE] [--hive-dir DIR] [--no-hive-deleted]\n"
        "              [--amcache-file HIVE] [--shimcache-file BIN] [--shimcache-hive SYSTEM]\n"
        "              [--srum-db SRUDB.dat]\n"
        "              [--bam-hive SYSTEM]\n"
        "              [--browser-secrets] [--browser-profile DIR]\n"
        "              [--evtx-file ARQUIVO] [--evtx-dir DIR]\n"
        "  --case-query <db> <sql>     consulta o banco do caso em JSON\n"
        "  --case-summary <db>         contagens de artifacts/timeline\n"
        "  --image-create <origem> --out <arquivo> [--split-mb N] [--sign-key PRIV]\n"
        "  --image-verify <imagem> [--manifest ARQUIVO]\n"
        "  --keygen <prefixo>          gera ECDSA P-256 .priv/.pub\n"
        "  --sign-manifest <arq> --key <priv> [--sig ARQUIVO]\n"
        "  --verify-manifest <arq> --key <pub> [--sig ARQUIVO]\n"
        "  --wipe-file <arquivo|pasta> [--passes N] [--out DIR] --confirm WIPE\n"
        "  --wipe-free <imagem|volume> [--offset N] [--passes N] [--out DIR] --confirm WIPE\n"
        "  --wipe-hive <HIVE> [--out DIR] --confirm WIPE\n"
        "  --wipe-journal <imagem> [--offset N] [--out DIR] --confirm WIPE\n"
        "  --wipe-browser [--browser-profile DIR] [--out DIR] --confirm WIPE\n"
        "  --purge-usn <volume> --confirm WIPE\n"
        "  --clear-logs --confirm WIPE\n"
        "  --wipe-artifacts --confirm WIPE\n"
        "  --delete-vss --confirm WIPE\n"
        "  --vss-list                  enumera shadow copies (admin)\n"
        "  --vss-read <N> --out DIR    le a shadow copy N (admin)\n"
        "  --image-evtx <imagem> --out DIR [--offset N]\n"
        "  --image-prefetch <imagem> --out DIR [--offset N]\n"
        "  --aff4-create <imagem> --out DIR\n"
        "  --aff4-zip <imagem> --out ARQUIVO.aff4\n"
        "  --aff4-zip-verify <ARQUIVO.aff4>\n"
        "  --list-disks\n"
        "  --gui                       abre a interface grafica\n"
        "  --mode forensic|sanitize   (default forensic; sanitize unlocks destructive)\n"
        "  --sanitize-disk <N> --serial SERIAL [--passes N] [--verify] --confirm WIPE-DISK\n"
        "  --nvme-sanitize <N> --serial SERIAL --action block|crypto --confirm NVME-SANITIZE\n"
        "  --ata-erase <N> --serial SERIAL [--password SENHA] [--enhanced] --confirm ATA-ERASE\n"
        "  --exposure-audit --out DIR\n"
        "  --clean-local-residue --out DIR --confirm CLEAN-RESIDUE [--include-sync-cache]\n"
        "  --cloud-m365-auth --tenant ID --client-id ID [--no-browser] [--mock-http]\n"
        "  --cloud-m365-collect --case DIR --account UPN --tenant ID --vault-key KEY\n"
        "              --legal-basis REF --job ID [--since YYYY-MM-DD] [--until YYYY-MM-DD]\n"
        "              [--download-attachments] [--vault-key-stdin]\n"
        "  --secret-gc (--case DIR | --cases-root DIR) [--older-days N] [--force]\n"
        "  --cloud-report --case DIR --out ARQUIVO [--format html|json]\n"
        "  --cloud-db-import --case DIR [--db ARQUIVO]\n"
        "  --cloud-verify --case DIR [--account UPN]\n"
        "  --kernel-version\n"
        "  --kernel-read <N> --offset BYTES --length BYTES --out ARQUIVO\n"
        "  --e01-create <origem> --out BASE\n"
        "  --e01-export <arquivo.E01> --out RAW\n"
        "  --e01-scan <arquivo.E01> --out DIR\n"
        "  --aff4-verify <DIR>\n"
        "  --recycle-recover <pasta>   copia os $R da lixeira\n"
        "\n"
        "Read-only scan works on images without admin. Live volumes and all\n"
        "destructive operations require an elevated process. Every wipe is\n"
        "audited under .\\cases\\wipe_<utc>\\destruction_audit.jsonl.\n";
}

std::string ask_line(const char* prompt) {
    std::cout << prompt;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

bool ask_yes(const char* prompt) {
    const std::string value = ask_line(prompt);
    return value == "s" || value == "S" || value == "sim" || value == "SIM";
}

} // namespace

namespace {
bool g_sanitize_mode = false;
}

void set_sanitize_mode(bool enabled) { g_sanitize_mode = enabled; }
bool sanitize_mode_enabled() { return g_sanitize_mode; }
int run_cli(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int wide_argc = 0;
    LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv == nullptr) return -1;
    std::vector<std::wstring> args;
    for (int i = 1; i < wide_argc; ++i) args.emplace_back(wide_argv[i]);
    LocalFree(wide_argv);
    if (args.empty()) return -1;

    bool mode_sanitize = false;
    bool mode_seen = false;
    std::vector<std::wstring> filtered;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::wstring value;
        if (args[i] == L"--mode") {
            if (i + 1 >= args.size()) {
                std::cerr << "mode: --mode requires forensic|sanitize\n";
                return 2;
            }
            value = args[++i];
        } else if (args[i].rfind(L"--mode=", 0) == 0) {
            value = args[i].substr(7);
        } else {
            filtered.push_back(args[i]);
            continue;
        }
        if (value == L"forensic") mode_sanitize = false;
        else if (value == L"sanitize") mode_sanitize = true;
        else {
            std::cerr << "mode: expected forensic|sanitize\n";
            return 2;
        }
        mode_seen = true;
    }
    if (filtered.empty()) return 2;
    args = filtered;
    set_sanitize_mode(mode_sanitize);
    (void)mode_seen;

    const std::wstring command = args[0];
    const bool destructive = command == L"--sanitize-disk" ||
        command == L"--nvme-sanitize" || command == L"--ata-erase" ||
        command == L"--clean-local-residue" ||
        command == L"--purge-usn" || command == L"--clear-logs" ||
        command == L"--delete-vss" || command.rfind(L"--wipe", 0) == 0;
    if (destructive && !mode_sanitize) {
        std::cerr << "mode: destructive command requires --mode sanitize\n";
        return 2;
    }
    const bool is_deep = command == L"--deep-scan" ||
        command == L"--deep-usn-file" || command == L"--deep-help" ||
        command == L"--artifacts" || command == L"--case-query" ||
        command == L"--case-summary" || command == L"--image-create" ||
        command == L"--image-verify" ||
        command == L"--vss-list" ||
        command == L"--vss-read" ||
        command == L"--image-evtx" ||
        command == L"--image-prefetch" ||
        command == L"--recycle-recover" ||
        command == L"--aff4-create" || command == L"--aff4-verify" ||
        command == L"--aff4-zip" ||
        command == L"--aff4-zip-verify" ||
        command == L"--list-disks" || command == L"--sanitize-disk" ||
        command == L"--nvme-sanitize" || command == L"--ata-erase" ||
        command == L"--exposure-audit" || command == L"--clean-local-residue" ||
        command == L"--gui" ||
        command == L"--cloud-m365-auth" || command == L"--cloud-m365-collect" ||
        command == L"--cloud-verify" || command == L"--cloud-db-import" ||
        command == L"--cloud-report" || command == L"--secret-gc" ||
        command == L"--kernel-version" || command == L"--kernel-read" ||
        command == L"--e01-create" || command == L"--e01-export" ||
        command == L"--e01-scan" ||
        command == L"--wipe-browser" ||
        command == L"--keygen" || command == L"--sign-manifest" ||
        command == L"--verify-manifest" ||
        command.rfind(L"--wipe", 0) == 0 || command == L"--purge-usn" ||
        command == L"--clear-logs" || command == L"--delete-vss";
    if (!is_deep) return -1;

    bool confirm = false;
    bool confirm_disk = false;
    bool confirm_nvme = false;
    bool confirm_ata = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == L"--confirm" && i + 1 < args.size() &&
            args[i + 1] == L"WIPE") confirm = true;
        if (args[i] == L"--confirm=WIPE") confirm = true;
        if (args[i] == L"--confirm" && i + 1 < args.size() &&
            args[i + 1] == L"WIPE-DISK") confirm_disk = true;
        if (args[i] == L"--confirm=WIPE-DISK") confirm_disk = true;
        if (args[i] == L"--confirm" && i + 1 < args.size() &&
            args[i + 1] == L"NVME-SANITIZE") confirm_nvme = true;
        if (args[i] == L"--confirm=NVME-SANITIZE") confirm_nvme = true;
        if (args[i] == L"--confirm" && i + 1 < args.size() &&
            args[i + 1] == L"ATA-ERASE") confirm_ata = true;
        if (args[i] == L"--confirm=ATA-ERASE") confirm_ata = true;
    }

    if (command == L"--cloud-m365-auth") {
        cloud::AuthFlowOptions options;
        std::wstring value;
        if (option_value(args, L"--tenant", value)) options.tenant = to_utf8(value);
        if (option_value(args, L"--client-id", value)) options.client_id = to_utf8(value);
        if (option_value(args, L"--scope", value)) options.scope = to_utf8(value);
        if (option_value(args, L"--vault-key", value)) options.vault_key = to_utf8(value);
        if (option_value(args, L"--authorize-endpoint", value))
            options.authorize_endpoint = to_utf8(value);
        if (option_value(args, L"--token-endpoint", value))
            options.token_endpoint = to_utf8(value);
        if (option_value(args, L"--timeout", value)) {
            bool ok = false;
            const auto parsed = parse_number(value, ok);
            if (ok && parsed >= 30) options.timeout_seconds = static_cast<int>(parsed);
        }
        options.no_browser = has_flag(args, L"--no-browser");
        options.mock_http = has_flag(args, L"--mock-http");
        return cloud::run_auth_flow(options);
    }
    if (command == L"--secret-gc") {
        std::wstring value;
        std::wstring case_dir;
        std::wstring cases_root;
        if (option_value(args, L"--case", value)) case_dir = value;
        if (option_value(args, L"--cases-root", value)) cases_root = value;
        if (case_dir.empty() && cases_root.empty()) {
            std::cerr << "secret-gc: --case or --cases-root is required\n";
            return 2;
        }
        std::int64_t older_days = 7;
        if (option_value(args, L"--older-days", value)) {
            bool ok = false;
            const auto parsed = parse_number(value, ok);
            if (ok && parsed >= 0) older_days = static_cast<std::int64_t>(parsed);
        }
        if (has_flag(args, L"--force")) older_days = 0;
        std::set<std::string> keep;
        std::vector<std::filesystem::path> roots;
        if (!case_dir.empty()) roots.emplace_back(case_dir);
        std::error_code directory_error;
        if (!cases_root.empty() &&
            std::filesystem::exists(cases_root, directory_error)) {
            for (const auto& entry :
                 std::filesystem::directory_iterator(cases_root, directory_error)) {
                if (!directory_error && entry.is_directory()) roots.push_back(entry.path());
            }
        }
        for (const auto& root : roots) {
            const std::filesystem::path queue_dir = root / "cloud" / "queue";
            if (!std::filesystem::exists(queue_dir, directory_error)) continue;
            for (const auto& entry :
                 std::filesystem::directory_iterator(queue_dir, directory_error)) {
                if (directory_error || !entry.is_regular_file() ||
                    entry.path().extension() != ".json") continue;
                const auto job = nlohmann::json::parse(
                    [&] {
                        std::ifstream input(entry.path(), std::ios::binary);
                        return std::string((std::istreambuf_iterator<char>(input)),
                                           std::istreambuf_iterator<char>());
                    }(), nullptr, false);
                if (!job.is_object()) continue;
                if (!job.contains("vault_ref") || !job["vault_ref"].is_string()) continue;
                const std::string ref = job["vault_ref"].get<std::string>();
                if (deep::cloud::SecretStore::valid_ref(ref)) keep.insert(ref);
            }
        }
        deep::cloud::SecretStore store;
        std::string error;
        int removed = 0;
        if (!store.prune(std::vector<std::string>(keep.begin(), keep.end()),
                         older_days * 24 * 60 * 60, removed, error)) {
            std::cerr << "secret-gc: " << error << "\n";
            return 7;
        }
        std::cout << "secret-gc: ok\n"
                  << "kept: " << keep.size() << "\n"
                  << "removed: " << removed << "\n";
        return 0;
    }
    if (command == L"--cloud-report") {
        std::wstring value;
        std::wstring case_dir;
        std::wstring output;
        std::string format;
        if (option_value(args, L"--case", value)) case_dir = value;
        if (option_value(args, L"--out", value)) output = value;
        if (option_value(args, L"--format", value)) format = to_utf8(value);
        if (case_dir.empty() || output.empty()) {
            std::cerr << "cloud-report: --case and --out are required\n";
            return 2;
        }
        const cloud::CloudReportResult report = cloud::generate_cloud_report(
            to_utf8(case_dir), to_utf8(output), format);
        if (!report.ok) {
            std::cerr << "cloud-report: " << report.error << "\n";
            return 7;
        }
        std::cout << "cloud-report: ok\npath: " << report.path << "\n";
        return 0;
    }
    if (command == L"--cloud-db-import") {
        std::wstring value;
        std::wstring case_dir;
        std::wstring db_path;
        if (option_value(args, L"--case", value)) case_dir = value;
        if (option_value(args, L"--db", value)) db_path = value;
        if (case_dir.empty()) {
            std::cerr << "cloud-db-import: --case is required\n";
            return 2;
        }
        const cloud::CloudDbImportReport report = cloud::import_cloud_case(
            to_utf8(case_dir), to_utf8(db_path));
        std::cout << "cloud-db-import: " << (report.ok ? "ok" : "failed") << "\n"
                  << "database: " << report.database_path << "\n"
                  << "jobs: " << report.jobs << "\n"
                  << "accounts: " << report.accounts << "\n"
                  << "objects: " << report.objects
                  << " existing: " << report.objects_existing << "\n"
                  << "artifacts: " << report.artifacts
                  << " existing: " << report.artifacts_existing << "\n";
        if (!report.error.empty()) std::cerr << "error: " << report.error << "\n";
        return report.ok ? 0 : 7;
    }
    if (command == L"--cloud-verify") {
        std::wstring value;
        std::wstring case_dir;
        std::wstring account;
        if (option_value(args, L"--case", value)) case_dir = value;
        if (option_value(args, L"--account", value)) account = value;
        if (case_dir.empty()) {
            std::cerr << "cloud-verify: --case is required\n";
            return 2;
        }
        const cloud::ManifestVerifyReport report = cloud::verify_cloud_manifests(
            to_utf8(case_dir), to_utf8(account));
        std::cout << "cloud-verify: " << (report.ok ? "ok" : "failed") << "\n"
                  << "manifests: " << report.manifests << "\n"
                  << "files: " << report.verified << "/" << report.files << "\n"
                  << "objects: " << report.objects << "\n";
        for (const auto& error : report.errors)
            std::cerr << "error: " << error << "\n";
        return report.ok ? 0 : 7;
    }
    if (command == L"--cloud-m365-collect") {
        cloud::CollectFlowOptions options;
        std::wstring value;
        if (option_value(args, L"--case", value)) options.case_dir = to_utf8(value);
        if (option_value(args, L"--account", value)) options.account = to_utf8(value);
        if (option_value(args, L"--tenant", value)) options.tenant = to_utf8(value);
        if (option_value(args, L"--vault-key", value)) options.vault_key = to_utf8(value);
        if (option_value(args, L"--legal-basis", value)) options.legal_basis = to_utf8(value);
        if (option_value(args, L"--job", value)) options.job = to_utf8(value);
        if (option_value(args, L"--since", value)) options.since = to_utf8(value);
        if (option_value(args, L"--until", value)) options.until = to_utf8(value);
        if (option_value(args, L"--base-url", value)) options.base_url = to_utf8(value);
        if (option_value(args, L"--max-pages", value)) {
            bool ok = false;
            const auto parsed = parse_number(value, ok);
            if (ok && parsed > 0) options.max_pages = static_cast<int>(parsed);
        }
        options.mock_http = has_flag(args, L"--mock-http");
        options.download_attachments = has_flag(args, L"--download-attachments");
        if (has_flag(args, L"--vault-key-stdin")) {
            std::string vault_key;
            std::getline(std::cin, vault_key);
            if (!vault_key.empty()) options.vault_key = vault_key;
        }
        return cloud::run_collect_flow(options);
    }
    if (command == L"--deep-help") {
        std::cout << deep_help();
        return 0;
    }
    if (command == L"--deep-scan") {
        ScanOptions options;
        if (args.size() < 2 || args[1].rfind(L"--", 0) == 0) {
            std::cerr << "deep-scan: source missing\n";
            return 2;
        }
        options.source = args[1];
        std::wstring value;
        bool ok = false;
        if (option_value(args, L"--offset", value)) {
            options.offset = parse_number(value, ok);
            if (!ok) { std::cerr << "deep-scan: invalid offset\n"; return 2; }
        }
        if (option_value(args, L"--out", value)) options.out_dir = value;
        options.list = has_flag(args, L"--list");
        options.recover = has_flag(args, L"--recover");
        options.usn = has_flag(args, L"--usn");
        options.carve = has_flag(args, L"--carve");
        options.slack = has_flag(args, L"--slack");
        if (option_value(args, L"--min-size", value)) {
            options.min_carve = parse_number(value, ok);
            if (!ok) { std::cerr << "deep-scan: invalid min-size\n"; return 2; }
        }
        return run_scan(options);
    }
    if (command == L"--deep-usn-file") {
        if (args.size() < 2) return 2;
        std::wstring out;
        option_value(args, L"--out", out);
        return run_usn_file(args[1], out);
    }
    if (command == L"--artifacts") {
        ArtifactOptions options;
        std::wstring value;
        if (option_value(args, L"--out", value)) options.out_dir = value;
        if (option_value(args, L"--db", value)) options.db_path = value;
        if (option_value(args, L"--evtx-file", value)) options.evtx_file = value;
        if (option_value(args, L"--evtx-dir", value)) options.evtx_dir = value;
        options.registry = !has_flag(args, L"--no-registry");
        options.browser = !has_flag(args, L"--no-browser");
        options.recycle = !has_flag(args, L"--no-recycle");
        options.lnk = !has_flag(args, L"--no-lnk");
        options.jumplist = !has_flag(args, L"--no-jumplist");
        options.prefetch = !has_flag(args, L"--no-prefetch");
        options.shellbags = !has_flag(args, L"--no-shellbags");
        options.hive_deleted = !has_flag(args, L"--no-hive-deleted");
        if (option_value(args, L"--hive-file", value))
            options.hive_file = value;
        if (option_value(args, L"--hive-dir", value))
            options.hive_dir = value;
        if (option_value(args, L"--amcache-file", value))
            options.amcache_file = value;
        if (option_value(args, L"--shimcache-file", value))
            options.shimcache_file = value;
        if (option_value(args, L"--shimcache-hive", value))
            options.shimcache_hive = value;
        if (option_value(args, L"--srum-db", value))
            options.srum_db = value;
        if (option_value(args, L"--bam-hive", value))
            options.bam_hive = value;
        if (has_flag(args, L"--browser-secrets")) {
            options.browser_secrets = true;
            if (option_value(args, L"--browser-profile", value))
                options.browser_profile = value;
        }
        if (option_value(args, L"--prefetch-dir", value))
            options.prefetch_dir = value;
        options.evtx = !has_flag(args, L"--no-evtx");
        return run_artifacts(options);
    }
    if (command == L"--case-query") {
        if (args.size() < 3) {
            std::cerr << "case-query: usage --case-query <db> <sql>\n";
            return 2;
        }
        CaseDatabase database;
        std::wstring error;
        if (!database.open(args[1], error)) {
            std::cerr << "case-query: " << to_utf8(error) << "\n";
            return 1;
        }
        std::string output;
        if (!database.query_json(to_utf8(args[2]), output, error)) {
            std::cerr << "case-query: " << to_utf8(error) << "\n";
            return 1;
        }
        std::cout << output << "\n";
        return 0;
    }
    if (command == L"--case-summary") {
        if (args.size() < 2) return 2;
        CaseDatabase database;
        std::wstring error;
        if (!database.open(args[1], error)) {
            std::cerr << "case-summary: " << to_utf8(error) << "\n";
            return 1;
        }
        const int artifacts = database.count("artifacts", error);
        const int timeline = database.count("timeline", error);
        const int cloud_jobs = database.count("cloud_job", error);
        const int cloud_objects = database.count("cloud_object", error);
        const int cloud_artifacts = database.count("cloud_artifact", error);
        std::cout << "artifacts=" << artifacts << " timeline=" << timeline
                  << " cloud_jobs=" << cloud_jobs
                  << " cloud_objects=" << cloud_objects
                  << " cloud_artifacts=" << cloud_artifacts << "\n";
        return 0;
    }
    if (command == L"--image-create") {
        if (args.size() < 2) return 2;
        ImageOptions options;
        options.source = args[1];
        std::wstring value;
        if (!option_value(args, L"--out", value)) {
            std::cerr << "image-create: --out is required\n";
            return 2;
        }
        options.output = value;
        if (option_value(args, L"--split-mb", value)) {
            bool ok = false;
            const std::uint64_t megabytes = parse_number(value, ok);
            if (!ok || megabytes == 0) {
                std::cerr << "image-create: invalid --split-mb\n";
                return 2;
            }
            options.split_bytes = megabytes * 1024ull * 1024ull;
        }
        const int rc = create_image(options);
        if (rc == 0) {
            const bool split = options.split_bytes != 0;
            const int verified = verify_image(
                options.output + (split ? L".001" : L""), L"");
            std::wstring sign_key;
            if (verified == 0 &&
                option_value(args, L"--sign-key", sign_key)) {
                std::wstring sign_error;
                sign_manifest(options.output + L".manifest.json", sign_key,
                              L"", sign_error);
            }
            return verified;
        }
        return rc;
    }
    if (command == L"--image-verify") {
        if (args.size() < 2) return 2;
        std::wstring manifest;
        option_value(args, L"--manifest", manifest);
        return verify_image(args[1], manifest);
    }
    if (command == L"--keygen") {
        if (args.size() < 2) return 2;
        std::wstring error;
        return generate_signing_key(args[1], error) == 0 ? 0 : 1;
    }
    if (command == L"--sign-manifest") {
        if (args.size() < 2) return 2;
        std::wstring key;
        if (!option_value(args, L"--key", key)) {
            std::cerr << "sign-manifest: --key is required\n";
            return 2;
        }
        std::wstring signature;
        option_value(args, L"--sig", signature);
        std::wstring error;
        return sign_manifest(args[1], key, signature, error) == 0 ? 0 : 1;
    }
    if (command == L"--verify-manifest") {
        if (args.size() < 2) return 2;
        std::wstring key;
        if (!option_value(args, L"--key", key)) {
            std::cerr << "verify-manifest: --key is required\n";
            return 2;
        }
        std::wstring signature;
        option_value(args, L"--sig", signature);
        std::wstring error;
        return verify_manifest(args[1], key, signature, error) == 0 ? 0 : 1;
    }
    if (command == L"--gui") {
        return run_gui(GetModuleHandleW(nullptr));
    }
    if (command == L"--e01-create") {
        if (args.size() < 2) return 2;
        std::wstring out;
        if (!option_value(args, L"--out", out)) return 2;
        std::wstring error;
        const int result = e01_create(args[1], out, error);
        if (result != 0) std::cerr << "e01-create: " << art::to_utf8(error) << "\n";
        return result;
    }
    if (command == L"--e01-scan") {
        if (args.size() < 2) return 2;
        std::wstring out;
        if (!option_value(args, L"--out", out)) return 2;
        wchar_t temp_path[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp_path);
        const std::wstring temp = std::wstring(temp_path) +
            L"eog_e01_scan_" + std::to_wstring(GetTickCount64()) + L".raw";
        std::wstring error;
        if (e01_export(args[1], temp, error) != 0) {
            std::cerr << "e01-scan: " << art::to_utf8(error) << "\n";
            return 1;
        }
        ScanOptions options;
        options.source = temp;
        options.out_dir = out;
        options.list = true;
        options.recover = true;
        options.usn = true;
        const int result = run_scan(options);
        DeleteFileW(temp.c_str());
        return result;
    }
    if (command == L"--e01-export") {
        if (args.size() < 2) return 2;
        std::wstring out;
        if (!option_value(args, L"--out", out)) return 2;
        std::wstring error;
        const int result = e01_export(args[1], out, error);
        if (result != 0) std::cerr << "e01-export: " << art::to_utf8(error) << "\n";
        return result;
    }
    if (command == L"--kernel-version") {
        std::wstring error;
        const int result = kernel_version(error);
        if (result != 0) std::cerr << "kernel: " << art::to_utf8(error) << "\n";
        return result;
    }
    if (command == L"--kernel-read") {
        if (args.size() < 2) return 2;
        bool ok = false;
        const std::uint64_t disk = parse_number(args[1], ok);
        if (!ok || disk > 31) return 2;
        std::wstring offset_text, length_text, out;
        if (!option_value(args, L"--offset", offset_text) ||
            !option_value(args, L"--length", length_text) ||
            !option_value(args, L"--out", out)) {
            std::cerr << "kernel-read: --offset, --length and --out are required\n";
            return 2;
        }
        const std::uint64_t offset = parse_number(offset_text, ok);
        if (!ok) return 2;
        const std::uint64_t length = parse_number(length_text, ok);
        if (!ok || length > 16u * 1024u * 1024u) return 2;
        std::wstring error;
        const int result = kernel_read_disk(static_cast<int>(disk), offset,
            static_cast<std::uint32_t>(length), out, error);
        if (result != 0) std::cerr << "kernel: " << art::to_utf8(error) << "\n";
        return result;
    }
    if (command == L"--exposure-audit") {
        std::wstring out;
        if (!option_value(args, L"--out", out)) out = L"cases\\exposure";
        return exposure_audit(out);
    }
    if (command == L"--clean-local-residue") {
        std::wstring out;
        if (!option_value(args, L"--out", out)) out = L"cases\\residue_clean";
        bool confirm = false;
        bool include_sync = false;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == L"--confirm" && i + 1 < args.size() &&
                args[i + 1] == L"CLEAN-RESIDUE") confirm = true;
            if (args[i] == L"--confirm=CLEAN-RESIDUE") confirm = true;
            if (args[i] == L"--include-sync-cache") include_sync = true;
        }
        return clean_local_residue(out, confirm, include_sync);
    }
    if (command == L"--ata-erase") {
        if (args.size() < 2) return 2;
        bool ok = false;
        const std::uint64_t number = parse_number(args[1], ok);
        if (!ok || number > 31) return 2;
        std::wstring serial;
        if (!option_value(args, L"--serial", serial) || serial.empty()) {
            std::cerr << "ata-erase: --serial is required\n";
            return 2;
        }
        std::wstring password;
        option_value(args, L"--password", password);
        bool enhanced = false;
        for (const auto& arg : args) if (arg == L"--enhanced") enhanced = true;
        std::wstring out;
        option_value(args, L"--out", out);
        return ata_erase(static_cast<int>(number), serial, enhanced, password,
                         confirm_ata, out);
    }
    if (command == L"--nvme-sanitize") {
        if (args.size() < 2) return 2;
        bool ok = false;
        const std::uint64_t number = parse_number(args[1], ok);
        if (!ok || number > 31) return 2;
        std::wstring serial;
        if (!option_value(args, L"--serial", serial) || serial.empty()) {
            std::cerr << "nvme-sanitize: --serial is required\n";
            return 2;
        }
        std::wstring action_text;
        if (!option_value(args, L"--action", action_text)) {
            std::cerr << "nvme-sanitize: --action block|crypto is required\n";
            return 2;
        }
        int action = 0;
        if (action_text == L"block") action = 1;
        else if (action_text == L"overwrite") action = 2;
        else if (action_text == L"crypto") action = 3;
        else return 2;
        std::wstring out;
        option_value(args, L"--out", out);
        return nvme_sanitize(static_cast<int>(number), serial, action,
                             confirm_nvme, out);
    }
    if (command == L"--list-disks") return list_disks();
    if (command == L"--sanitize-disk") {
        if (args.size() < 2) return 2;
        bool ok = false;
        const std::uint64_t number = parse_number(args[1], ok);
        if (!ok || number > 31) return 2;
        std::wstring serial;
        if (!option_value(args, L"--serial", serial) || serial.empty()) {
            std::cerr << "sanitize-disk: --serial is required\n";
            return 2;
        }
        unsigned passes = 1;
        std::wstring value;
        if (option_value(args, L"--passes", value)) {
            const std::uint64_t parsed = parse_number(value, ok);
            if (!ok || parsed == 0 || parsed > 7) return 2;
            passes = static_cast<unsigned>(parsed);
        }
        std::wstring out;
        option_value(args, L"--out", out);
        const bool verify = has_flag(args, L"--verify");
        return sanitize_disk(static_cast<int>(number), serial, passes, verify,
                             confirm_disk, out);
    }
    if (command == L"--aff4-zip-verify") {
        if (args.size() < 2) return 2;
        std::wstring error;
        const int result = verify_aff4_zip(args[1], error);
        if (result != 0 && !error.empty())
            std::cerr << "aff4-zip-verify: " << art::to_utf8(error) << "\n";
        return result == 0 ? 0 : 1;
    }
    if (command == L"--aff4-zip") {
        if (args.size() < 2) return 2;
        std::wstring out;
        if (!option_value(args, L"--out", out)) {
            std::cerr << "aff4-zip: --out is required\n";
            return 2;
        }
        std::wstring error;
        const int result = create_aff4_zip(args[1], out, error);
        if (result != 0 && !error.empty())
            std::cerr << "aff4-zip: " << art::to_utf8(error) << "\n";
        return result == 0 ? 0 : 1;
    }
    if (command == L"--aff4-create") {
        if (args.size() < 2) return 2;
        std::wstring out;
        if (!option_value(args, L"--out", out)) {
            std::cerr << "aff4-create: --out is required\n";
            return 2;
        }
        std::wstring error;
        const int result = create_aff4(args[1], out, error);
        if (result != 0 && !error.empty())
            std::cerr << "aff4-create: " << art::to_utf8(error) << "\\n";
        return result == 0 ? 0 : 1;
    }
    if (command == L"--aff4-verify") {
        if (args.size() < 2) return 2;
        std::wstring error;
        const int result = verify_aff4(args[1], error);
        if (result != 0 && !error.empty())
            std::cerr << "aff4-verify: " << art::to_utf8(error) << "\\n";
        return result == 0 ? 0 : 1;
    }
    if (command == L"--recycle-recover") {
        if (args.size() < 2) return 2;
        int count = 0;
        ArtifactSink sink = [&](const ArtifactRow& row) {
            ++count;
            std::cout << "recycle: " << row.name << " -> " << row.path << "\n";
        };
        std::wstring error;
        const int result = recover_recycle(args[1], sink, error);
        if (result < 0) {
            std::cerr << "recycle-recover: " << art::to_utf8(error) << "\n";
            return 1;
        }
        std::cout << "recycle-recover: items=" << count << "\n";
        return 0;
    }
    if (command == L"--image-prefetch") {
        if (args.size() < 2) return 2;
        std::wstring out;
        if (!option_value(args, L"--out", out)) {
            std::cerr << "image-prefetch: --out is required\n";
            return 2;
        }
        std::uint64_t offset = 0;
        std::wstring value;
        if (option_value(args, L"--offset", value)) {
            bool ok = false;
            offset = parse_number(value, ok);
            if (!ok) return 2;
        }
        return extract_image_prefetch(args[1], out, offset);
    }
    if (command == L"--image-evtx") {
        if (args.size() < 2) return 2;
        std::wstring out;
        if (!option_value(args, L"--out", out)) {
            std::cerr << "image-evtx: --out is required\n";
            return 2;
        }
        std::uint64_t offset = 0;
        std::wstring value;
        if (option_value(args, L"--offset", value)) {
            bool ok = false;
            offset = parse_number(value, ok);
            if (!ok) return 2;
        }
        return extract_image_evtx(args[1], out, offset);
    }
    if (command == L"--vss-read") {
        if (args.size() < 2) return 2;
        bool ok = false;
        const std::uint64_t number = parse_number(args[1], ok);
        if (!ok || number > 1000) return 2;
        std::wstring out;
        option_value(args, L"--out", out);
        return run_vss_read(static_cast<int>(number), out);
    }
    if (command == L"--vss-list") {
        int count = 0;
        ArtifactSink sink = [&](const ArtifactRow& row) {
            ++count;
            std::cout << "vss: " << row.key << " | " << row.path << "\n";
        };
        std::wstring error;
        const int result = collect_vss(sink, error);
        if (result < 0) {
            std::cerr << "vss-list: " << art::to_utf8(error) << "\n";
            return 1;
        }
        std::cout << "vss: shadows=" << count << "\n";
        return 0;
    }
    if (command == L"--wipe-browser") {
        std::wstring profile;
        option_value(args, L"--browser-profile", profile);
        std::wstring out;
        option_value(args, L"--out", out);
        return wipe_browser(profile, out, confirm);
    }
    if (command == L"--wipe-journal") {
        if (args.size() < 2) return 2;
        std::wstring out;
        option_value(args, L"--out", out);
        std::uint64_t offset = 0;
        std::wstring value;
        if (option_value(args, L"--offset", value)) {
            bool ok = false;
            offset = parse_number(value, ok);
            if (!ok) return 2;
        }
        return run_wipe_journal(args[1], offset, out, confirm);
    }
    if (command == L"--wipe-hive") {
        if (args.size() < 2) return 2;
        std::wstring out;
        option_value(args, L"--out", out);
        return sanitize_hive(args[1], out, confirm);
    }
    if (command == L"--wipe-file" || command == L"--wipe-free") {
        WipeOptions options;
        if (args.size() < 2 || args[1].rfind(L"--", 0) == 0) {
            std::cerr << "wipe: target missing\n";
            return 2;
        }
        options.target = args[1];
        options.confirm = confirm;
        std::wstring value;
        bool ok = false;
        if (option_value(args, L"--out", value)) options.out_dir = value;
        if (option_value(args, L"--passes", value)) {
            const std::uint64_t passes = parse_number(value, ok);
            if (!ok || passes == 0 || passes > 7) {
                std::cerr << "wipe: passes must be 1..7\n";
                return 2;
            }
            options.passes = static_cast<unsigned>(passes);
        }
        if (command == L"--wipe-free") {
            if (option_value(args, L"--offset", value)) {
                options.offset = parse_number(value, ok);
                if (!ok) { std::cerr << "wipe-free: invalid offset\n"; return 2; }
            }
            return run_wipe_free(options);
        }
        return run_wipe_file(options);
    }
    if (command == L"--purge-usn") {
        if (args.size() < 2) return 2;
        return run_purge_usn(args[1], confirm);
    }
    if (command == L"--clear-logs") return run_clear_logs(confirm);
    if (command == L"--wipe-artifacts") return run_wipe_artifacts(confirm);
    if (command == L"--delete-vss") return run_delete_vss(confirm);
    return 2;
}

void interactive(int choice) {
    SetConsoleOutputCP(CP_UTF8);
    if ((choice == 38 || choice == 40 || choice == 44 || choice == 46) &&
        !g_sanitize_mode) {
        std::cout << "Modo pericia ativo. Use --mode sanitize ou o menu 47 "
                     "para liberar esta acao.\n";
        return;
    }
    if (choice == 13 || choice == 14) {
        ScanOptions options;
        options.source = from_utf8(ask_line("Imagem ou volume NTFS: "));
        if (options.source.empty()) {
            std::cout << "Nada a fazer.\n";
            return;
        }
        const std::string offset = ask_line("Offset do volume em bytes (Enter = auto): ");
        if (!offset.empty()) {
            bool ok = false;
            options.offset = parse_number(from_utf8(offset), ok);
            if (!ok) {
                std::cout << "Offset invalido.\n";
                return;
            }
        }
        options.out_dir = from_utf8(ask_line(
            "Pasta de saida (Enter = cases\\deep_<UTC>): "));
        if (choice == 13) {
            options.list = true;
            options.recover = ask_yes("Recuperar arquivos apagados? (s/N): ");
            options.usn = ask_yes("Ler $UsnJrnl:$J? (s/N): ");
            options.carve = ask_yes("Carving de espaco nao alocado? (s/N): ");
        } else {
            options.carve = true;
            options.slack = true;
            options.list = false;
            const std::string min_size = ask_line(
                "Tamanho minimo de carving (Enter = 4096): ");
            if (!min_size.empty()) {
                bool ok = false;
                options.min_carve = parse_number(from_utf8(min_size), ok);
                if (!ok) options.min_carve = 4096;
            }
        }
        run_scan(options);
        return;
    }
    if (choice == 15) {
        WipeOptions options;
        options.target = from_utf8(ask_line("Arquivo ou pasta a apagar: "));
        const std::string passes = ask_line("Passadas (1..7, Enter = 1): ");
        if (!passes.empty()) {
            bool ok = false;
            options.passes = static_cast<unsigned>(
                parse_number(from_utf8(passes), ok));
            if (!ok || options.passes == 0) options.passes = 1;
        }
        std::cout << "Isto sobrescreve o conteudo, remove streams e apaga os nomes. "
                  << "Nao ha desfazer.\n";
        if (ask_line("Digite APAGAR para confirmar: ") != "APAGAR") {
            std::cout << "Cancelado.\n";
            return;
        }
        options.confirm = true;
        run_wipe_file(options);
        return;
    }
    if (choice == 16) {
        std::cout << "  1. Espaco livre de imagem/volume\n"
                  << "  2. Journal USN ($J)\n"
                  << "  3. Logs de eventos do Windows\n"
                  << "  4. Artefatos de uso (Prefetch, Recent, Jump Lists, WER, thumbcache)\n";
        const std::string selected = ask_line("Escolha: ");
        std::cout << "Operacao destrutiva; nao ha desfazer.\n";
        if (ask_line("Digite APAGAR para confirmar: ") != "APAGAR") {
            std::cout << "Cancelado.\n";
            return;
        }
        if (selected == "1") {
            WipeOptions options;
            options.target = from_utf8(ask_line("Imagem ou volume: "));
            const std::string offset = ask_line("Offset em bytes (Enter = auto): ");
            if (!offset.empty()) {
                bool ok = false;
                options.offset = parse_number(from_utf8(offset), ok);
            }
            const std::string passes = ask_line("Passadas (1..7, Enter = 1): ");
            if (!passes.empty()) {
                bool ok = false;
                options.passes = static_cast<unsigned>(
                    parse_number(from_utf8(passes), ok));
                if (!ok) options.passes = 1;
            }
            options.confirm = true;
            run_wipe_free(options);
        } else if (selected == "2") {
            run_purge_usn(from_utf8(ask_line("Volume (ex.: C:): ")), true);
        } else if (selected == "3") {
            run_clear_logs(true);
        } else if (selected == "4") {
            run_wipe_artifacts(true);
        }
        return;
    }
    if (choice == 17) {
        std::cout << "Isto apaga todas as copias de sombra (VSS) do computador.\n";
        if (ask_line("Digite APAGAR para confirmar: ") != "APAGAR") {
            std::cout << "Cancelado.\n";
            return;
        }
        run_delete_vss(true);
        return;
    }
    if (choice == 18) {
        ArtifactOptions options;
        options.out_dir = from_utf8(ask_line(
            "Pasta de saida (Enter = cases\\artifacts_<UTC>): "));
        options.db_path = from_utf8(ask_line(
            "Banco SQLite (Enter = <saida>\\case.db): "));
        options.registry = ask_yes("Coletar registro (Run/UserAssist/USBSTOR/etc.)? (s/N): ");
        options.browser = ask_yes("Coletar historico de navegador? (s/N): ");
        options.recycle = ask_yes("Coletar Recycle Bin? (s/N): ");
        options.lnk = ask_yes("Coletar atalhos LNK recentes? (s/N): ");
        options.jumplist = ask_yes("Coletar Jump Lists? (s/N): ");
        options.prefetch = ask_yes("Coletar Prefetch? (s/N): ");
        if (options.prefetch) {
            options.prefetch_dir = from_utf8(ask_line(
                "Pasta do Prefetch (Enter = C:\\Windows\\Prefetch): "));
        }
        options.shellbags = ask_yes("Coletar Shellbags? (s/N): ");
        const std::string evtx_file = ask_line(
            "Arquivo EVTX para importar (Enter = nenhum): ");
        if (!evtx_file.empty()) options.evtx_file = from_utf8(evtx_file);
        const std::string evtx_dir = ask_line(
            "Pasta com EVTX para importar (Enter = nenhuma): ");
        if (!evtx_dir.empty()) options.evtx_dir = from_utf8(evtx_dir);
        run_artifacts(options);
        return;
    }
    if (choice == 19) {
        ImageOptions options;
        options.source = from_utf8(ask_line("Origem (imagem ou volume, ex.: C:): "));
        options.output = from_utf8(ask_line("Arquivo de saida da imagem raw: "));
        const std::string split = ask_line(
            "Dividir a cada N MiB (Enter = arquivo unico): ");
        if (!split.empty()) {
            bool ok = false;
            const std::uint64_t megabytes = parse_number(from_utf8(split), ok);
            if (ok) options.split_bytes = megabytes * 1024ull * 1024ull;
        }
        const int rc = create_image(options);
        if (rc == 0) {
            const bool is_split = options.split_bytes != 0;
            verify_image(options.output + (is_split ? L".001" : L""), L"");
        }
        return;
    }
    if (choice == 20) {
        ArtifactOptions options;
        options.out_dir = from_utf8(ask_line(
            "Pasta de saida (Enter = cases\\artifacts_<UTC>): "));
        options.db_path = from_utf8(ask_line(
            "Banco SQLite (Enter = <saida>\\case.db): "));
        const std::string hive_file = ask_line(
            "Arquivo hive (SYSTEM/SOFTWARE/SAM/SECURITY/NTUSER; Enter = varrer pasta): ");
        if (!hive_file.empty()) {
            options.hive_file = from_utf8(hive_file);
        } else {
            options.hive_dir = from_utf8(ask_line("Pasta com hives: "));
        }
        options.hive_deleted = ask_yes("Recuperar celulas apagadas? (s/N): ");
        options.registry = options.browser = options.evtx = false;
        options.recycle = options.lnk = options.jumplist = false;
        options.prefetch = options.shellbags = false;
        run_artifacts(options);
        return;
    }
    if (choice == 21) {
        ArtifactOptions options;
        options.out_dir = from_utf8(ask_line(
            "Pasta de saida (Enter = cases\\artifacts_<UTC>): "));
        options.db_path = from_utf8(ask_line(
            "Banco SQLite (Enter = <saida>\\case.db): "));
        const std::string amcache = ask_line(
            "Amcache.hve (Enter = nenhum): ");
        if (!amcache.empty()) options.amcache_file = from_utf8(amcache);
        const std::string shimcache = ask_line(
            "AppCompatCache bin exportado (Enter = nenhum): ");
        if (!shimcache.empty()) options.shimcache_file = from_utf8(shimcache);
        const std::string shimcache_hive = ask_line(
            "SYSTEM hive para extrair ShimCache (Enter = nenhum): ");
        if (!shimcache_hive.empty()) options.shimcache_hive = from_utf8(shimcache_hive);
        options.registry = options.browser = options.evtx = false;
        options.recycle = options.lnk = options.jumplist = false;
        options.prefetch = options.shellbags = false;
        run_artifacts(options);
        return;
    }
    if (choice == 22) {
        const std::string mode = ask_line("1 Assinar manifesto / 2 Verificar: ");
        const std::wstring manifest = from_utf8(ask_line("Manifesto: "));
        const std::wstring key = from_utf8(ask_line("Chave (.priv ou .pub): "));
        const std::wstring signature = from_utf8(ask_line(
            "Arquivo de assinatura (Enter = <manifesto>.sig): "));
        std::wstring error;
        if (mode == "1")
            sign_manifest(manifest, key, signature, error);
        else if (mode == "2")
            verify_manifest(manifest, key, signature, error);
        else
            std::cout << "Modo invalido.\n";
        if (!error.empty()) std::cout << "erro: " << art::to_utf8(error) << "\n";
        return;
    }
    if (choice == 23) {
        ArtifactOptions options;
        options.out_dir = from_utf8(ask_line(
            "Pasta de saida (Enter = cases\\artifacts_<UTC>): "));
        options.db_path = from_utf8(ask_line(
            "Banco SQLite (Enter = <saida>\\case.db): "));
        options.srum_db = from_utf8(ask_line(
            "SRUDB.dat (Enter = nenhum): "));
        options.registry = options.browser = options.evtx = false;
        options.recycle = options.lnk = options.jumplist = false;
        options.prefetch = options.shellbags = false;
        run_artifacts(options);
        return;
    }
    if (choice == 24) {
        const std::wstring hive = from_utf8(ask_line(
            "Hive offline para sanitizar (copie antes; nunca use o hive live): "));
        const std::wstring out_dir = from_utf8(ask_line(
            "Pasta de auditoria (Enter = cases\\wipe_<UTC>): "));
        const std::string confirmation = ask_line(
            "Digite APAGAR para zerar as celulas livres: ");
        if (confirmation != "APAGAR") {
            std::cout << "Cancelado.\n";
            return;
        }
        sanitize_hive(hive, out_dir, true);
        return;
    }
    if (choice == 25) {
        int count = 0;
        ArtifactSink sink = [&](const ArtifactRow& row) {
            ++count;
            std::cout << "vss: " << row.key << " | " << row.path << "\n";
        };
        std::wstring error;
        if (collect_vss(sink, error) < 0)
            std::cout << "vss: " << art::to_utf8(error) << "\n";
        else
            std::cout << "vss: shadows=" << count << "\n";
        return;
    }
    if (choice == 26) {
        const std::wstring image = from_utf8(ask_line("Imagem NTFS: "));
        const std::wstring out_dir = from_utf8(ask_line("Pasta de saida: "));
        const std::string offset_text = ask_line(
            "Offset do volume (Enter = auto): ");
        std::uint64_t offset = 0;
        if (!offset_text.empty()) {
            bool ok = false;
            offset = parse_number(from_utf8(offset_text), ok);
            if (!ok) {
                std::cout << "offset invalido.\n";
                return;
            }
        }
        extract_image_evtx(image, out_dir, offset);
        return;
    }
    if (choice == 27) {
        const std::wstring image = from_utf8(ask_line("Imagem NTFS: "));
        const std::wstring out_dir = from_utf8(ask_line("Pasta de saida: "));
        const std::string offset_text = ask_line(
            "Offset do volume (Enter = auto): ");
        std::uint64_t offset = 0;
        if (!offset_text.empty()) {
            bool ok = false;
            offset = parse_number(from_utf8(offset_text), ok);
            if (!ok) {
                std::cout << "offset invalido.\n";
                return;
            }
        }
        extract_image_prefetch(image, out_dir, offset);
        return;
    }
    if (choice == 28) {
        const std::wstring out_dir = from_utf8(ask_line(
            "Pasta para recuperar os $R: "));
        int count = 0;
        ArtifactSink sink = [&](const ArtifactRow& row) {
            ++count;
            std::cout << "recycle: " << row.name << " -> " << row.path << "\n";
        };
        std::wstring error;
        if (recover_recycle(out_dir, sink, error) < 0)
            std::cout << "recycle: " << art::to_utf8(error) << "\n";
        else
            std::cout << "recycle: items=" << count << "\n";
        return;
    }
    if (choice == 29) {
        ArtifactOptions options;
        options.out_dir = from_utf8(ask_line(
            "Pasta de saida (Enter = cases\\browser_secrets_<UTC>): "));
        options.db_path = from_utf8(ask_line(
            "Banco SQLite (Enter = <saida>\\case.db): "));
        const std::string profile = ask_line(
            "User Data do Chrome/Edge (Enter = detectar): ");
        if (!profile.empty()) options.browser_profile = from_utf8(profile);
        options.registry = options.browser = options.evtx = false;
        options.recycle = options.lnk = options.jumplist = false;
        options.prefetch = options.shellbags = false;
        options.browser_secrets = true;
        run_artifacts(options);
        return;
    }
    if (choice == 30) {
        const std::wstring image = from_utf8(ask_line(
            "Imagem NTFS para zerar $LogFile/$J: "));
        const std::wstring out_dir = from_utf8(ask_line(
            "Pasta de auditoria (Enter = cases\\wipe_journal_<UTC>): "));
        const std::string offset_text = ask_line(
            "Offset do volume (Enter = auto): ");
        std::uint64_t offset = 0;
        if (!offset_text.empty()) {
            bool ok = false;
            offset = parse_number(from_utf8(offset_text), ok);
            if (!ok) {
                std::cout << "offset invalido.\n";
                return;
            }
        }
        const std::string confirmation = ask_line(
            "Digite APAGAR para zerar os journals: ");
        if (confirmation != "APAGAR") {
            std::cout << "Cancelado.\n";
            return;
        }
        run_wipe_journal(image, offset, out_dir, true);
        return;
    }
    if (choice == 31) {
        ArtifactOptions options;
        options.out_dir = from_utf8(ask_line(
            "Pasta de saida (Enter = cases\\\\bam_<UTC>): "));
        options.db_path = from_utf8(ask_line(
            "Banco SQLite (Enter = <saida>\\\\case.db): "));
        options.bam_hive = from_utf8(ask_line("SYSTEM hive: "));
        options.registry = options.browser = options.evtx = false;
        options.recycle = options.lnk = options.jumplist = false;
        options.prefetch = options.shellbags = false;
        run_artifacts(options);
        return;
    }
    if (choice == 32) {
        const std::string number_text = ask_line(
            "Numero da shadow copy (HarddiskVolumeShadowCopyN): ");
        bool ok = false;
        const std::uint64_t number = parse_number(from_utf8(number_text), ok);
        if (!ok || number == 0 || number > 1000) {
            std::cout << "numero invalido.\n";
            return;
        }
        const std::wstring out_dir = from_utf8(ask_line("Pasta de saida: "));
        run_vss_read(static_cast<int>(number), out_dir);
        return;
    }
    if (choice == 33) {
        const std::wstring profile = from_utf8(ask_line(
            "User Data do Chrome/Edge (copie antes): "));
        const std::wstring out_dir = from_utf8(ask_line(
            "Pasta de auditoria (Enter = cases\\\\wipe_browser_<UTC>): "));
        const std::string confirmation = ask_line(
            "Digite APAGAR para sanitizar o perfil: ");
        if (confirmation != "APAGAR") {
            std::cout << "Cancelado.\\n";
            return;
        }
        wipe_browser(profile, out_dir, true);
        return;
    }
    if (choice == 34) {
        const std::wstring image = from_utf8(ask_line("Imagem NTFS: "));
        const std::wstring out_dir = from_utf8(ask_line("Pasta AFF4 de saida: "));
        std::wstring error;
        if (create_aff4(image, out_dir, error) == 0) verify_aff4(out_dir, error);
        if (!error.empty()) std::cout << "aff4: " << art::to_utf8(error) << "\\n";
        return;
    }
    if (choice == 35) {
        const std::wstring image = from_utf8(ask_line("Imagem NTFS: "));
        const std::wstring zip = from_utf8(ask_line("Arquivo AFF4 .aff4: "));
        std::wstring error;
        if (create_aff4_zip(image, zip, error) == 0) verify_aff4_zip(zip, error);
        if (!error.empty()) std::cout << "aff4: " << art::to_utf8(error) << "\\n";
        return;
    }
    if (choice == 36) {
        const std::wstring zip = from_utf8(ask_line("Arquivo AFF4 .aff4: "));
        std::wstring error;
        if (verify_aff4_zip(zip, error) == 0)
            std::cout << "AFF4 ZIP: OK\n";
        else
            std::cout << "AFF4 ZIP: " << art::to_utf8(error) << "\n";
        return;
    }
    if (choice == 37) {
        list_disks();
        return;
    }
    if (choice == 38) {
        list_disks();
        const std::string number_text = ask_line("Numero do disco: ");
        bool ok = false;
        const std::uint64_t number = parse_number(from_utf8(number_text), ok);
        if (!ok || number > 31) {
            std::cout << "disco invalido.\n";
            return;
        }
        const std::wstring serial = from_utf8(ask_line("Serial exato do disco: "));
        const unsigned passes = 1;
        const std::string confirmation = ask_line(
            "Digite WIPE-DISK para apagar o disco inteiro: ");
        if (confirmation != "WIPE-DISK") {
            std::cout << "Cancelado.\n";
            return;
        }
        sanitize_disk(static_cast<int>(number), serial, passes, true, true, L"");
        return;
    }
    if (choice == 39) {
        run_gui(GetModuleHandleW(nullptr));
        return;
    }
    if (choice == 40) {
        list_disks();
        const std::string number_text = ask_line("Numero do disco NVMe: ");
        bool ok = false;
        const std::uint64_t number = parse_number(from_utf8(number_text), ok);
        if (!ok || number > 31) {
            std::cout << "disco invalido.\n";
            return;
        }
        const std::wstring serial = from_utf8(ask_line("Serial exato: "));
        const std::string action_text = ask_line("Acao (block/crypto): ");
        int action = 0;
        if (action_text == "block") action = 1;
        else if (action_text == "crypto") action = 3;
        else {
            std::cout << "acao invalida.\n";
            return;
        }
        const std::string confirmation = ask_line(
            "Digite NVME-SANITIZE para iniciar: ");
        if (confirmation != "NVME-SANITIZE") {
            std::cout << "Cancelado.\n";
            return;
        }
        nvme_sanitize(static_cast<int>(number), serial, action, true, L"");
        return;
    }
    if (choice == 41) {
        std::wstring error;
        if (kernel_version(error) != 0) {
            std::cout << "kernel: " << art::to_utf8(error) << "\\n";
            return;
        }
        bool ok = false;
        const std::uint64_t disk = parse_number(from_utf8(ask_line("Disco: ")), ok);
        if (!ok || disk > 31) return;
        const std::uint64_t offset = parse_number(from_utf8(ask_line("Offset: ")), ok);
        if (!ok) return;
        const std::uint64_t length = parse_number(from_utf8(ask_line("Length: ")), ok);
        if (!ok || length > 16u * 1024u * 1024u) return;
        const std::wstring out = from_utf8(ask_line("Arquivo de saida: "));
        kernel_read_disk(static_cast<int>(disk), offset,
            static_cast<std::uint32_t>(length), out, error);
        return;
    }
    if (choice == 42) {
        const std::string mode = ask_line("1 Criar E01 / 2 Exportar E01: ");
        const std::wstring source = from_utf8(ask_line("Origem/E01: "));
        const std::wstring out = from_utf8(ask_line("Saida (BASE ou .raw): "));
        std::wstring error;
        if (mode == "1") e01_create(source, out, error);
        else if (mode == "2") e01_export(source, out, error);
        else std::cout << "modo invalido.\\n";
        if (!error.empty()) std::cout << "e01: " << art::to_utf8(error) << "\\n";
        return;
    }
    if (choice == 43) {
        const std::wstring e01 = from_utf8(ask_line("Arquivo .E01: "));
        const std::wstring out = from_utf8(ask_line("Pasta de saida: "));
        wchar_t temp_path[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp_path);
        const std::wstring temp = std::wstring(temp_path) +
            L"eog_e01_menu_" + std::to_wstring(GetTickCount64()) + L".raw";
        std::wstring error;
        if (e01_export(e01, temp, error) != 0) {
            std::cout << "e01: " << art::to_utf8(error) << "\\n";
            return;
        }
        ScanOptions options;
        options.source = temp;
        options.out_dir = out;
        options.list = true;
        options.recover = true;
        options.usn = true;
        run_scan(options);
        DeleteFileW(temp.c_str());
        return;
    }
    if (choice == 44) {
        std::cout << "ATA SECURITY ERASE: apaga o disco inteiro de forma "
                     "irreversivel; nao use no disco do sistema.\n";
        list_disks();
        const std::string number_text = ask_line("Numero do disco SATA: ");
        bool ok = false;
        const std::uint64_t number = parse_number(from_utf8(number_text), ok);
        if (!ok || number > 31) return;
        const std::wstring serial = from_utf8(ask_line("Serial exato: "));
        const std::wstring password = from_utf8(ask_line("Senha ATA (vazia se nao houver): "));
        const std::string enhanced = ask_line("Enhanced erase? (s/n): ");
        const std::string confirmation = ask_line(
            "Digite ATA-ERASE para apagar o disco: ");
        if (confirmation != "ATA-ERASE") {
            std::cout << "Cancelado.\\n";
            return;
        }
        ata_erase(static_cast<int>(number), serial, enhanced == "s" ||
                  enhanced == "S", password, true, L"");
        return;
    }
    if (choice == 45) {
        const std::wstring out = from_utf8(ask_line("Pasta de saida: "));
        exposure_audit(out.empty() ? L"cases\\exposure" : out);
        return;
    }
    if (choice == 46) {
        const std::wstring out = from_utf8(ask_line("Pasta de saida: "));
        const std::string confirmation = ask_line(
            "Digite CLEAN-RESIDUE para apagar residuos locais: ");
        if (confirmation != "CLEAN-RESIDUE") {
            std::cout << "Cancelado.\\n";
            return;
        }
        clean_local_residue(out.empty() ? L"cases\\residue_clean" : out,
                            true, false);
    }
}

} // namespace deep
