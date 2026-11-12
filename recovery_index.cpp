#include "recovery_index.hpp"

#include "third_party/nlohmann/json.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace deep::forensic {
namespace {

std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    }
    return result;
}

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &now);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string filetime_utc(const FILETIME& file_time) {
    SYSTEMTIME system_time{};
    if (!FileTimeToSystemTime(&file_time, &system_time)) return {};
    char buffer[40]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ",
        system_time.wYear, system_time.wMonth, system_time.wDay,
        system_time.wHour, system_time.wMinute, system_time.wSecond);
    return buffer;
}

bool sha256_file(const std::filesystem::path& path, std::string& hex) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest;
    bool ok = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    do {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) break;
        DWORD object_size = 0;
        DWORD result_size = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                &result_size, 0))) break;
        DWORD digest_size = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&digest_size), sizeof(digest_size),
                &result_size, 0))) break;
        object.resize(object_size);
        digest.resize(digest_size);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(),
                object_size, nullptr, 0, 0))) break;
        std::array<unsigned char, 64 * 1024> buffer{};
        DWORD read = 0;
        while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                        &read, nullptr) && read > 0) {
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0))) break;
        }
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), digest_size, 0)))
            break;
        std::ostringstream stream;
        stream << std::hex << std::setfill('0');
        for (const unsigned char byte : digest) {
            stream << std::setw(2) << static_cast<unsigned int>(byte);
        }
        hex = stream.str();
        ok = true;
    } while (false);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return ok;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string detect_kind(const std::filesystem::path& path,
                        const std::vector<unsigned char>& head) {
    const std::string extension = lowercase(path.extension().string());
    auto starts = [&](const char* bytes, std::size_t size) {
        return head.size() >= size &&
            std::equal(bytes, bytes + size, head.begin());
    };
    if (starts("\xFF\xD8\xFF", 3)) return "image/jpeg";
    if (starts("\x89PNG\r\n\x1A\n", 8)) return "image/png";
    if (starts("GIF8", 4)) return "image/gif";
    if (starts("BM", 2)) return "image/bmp";
    if (starts("II*\0", 4) || starts("MM\0*", 4)) return "image/tiff";
    if (head.size() >= 12 && starts("RIFF", 4) &&
        std::equal(head.begin() + 8, head.begin() + 12,
                   reinterpret_cast<const unsigned char*>("WEBP")))
        return "image/webp";
    if (starts("%PDF-", 5)) return "application/pdf";
    if (starts("PK\x03\x04", 4)) {
        if (extension == ".docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        if (extension == ".xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        if (extension == ".pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        return "application/zip";
    }
    if (starts("SQLite format 3\0", 16)) return "application/sqlite";
    if (starts("ElfFile\0", 8)) return "application/evtx";
    if (starts("SCCA", 4)) return "application/prefetch";
    if (starts("7z\xBC\xAF\x27\x1C", 6)) return "application/x-7z-compressed";
    if (starts("Rar!\x1A\x07", 6)) return "application/x-rar-compressed";
    if (starts("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8)) return "application/ole";
    if (starts("ID3", 3)) return "audio/mpeg";
    if (starts("RIFF", 4)) return "audio/wav";
    if (starts("\x00\x00\x00", 3) && head.size() >= 12 &&
        std::equal(head.begin() + 4, head.begin() + 8,
                   reinterpret_cast<const unsigned char*>("ftyp")))
        return "video/mp4";
    if (extension == ".eml") return "message/rfc822";
    if (extension == ".msg") return "application/vnd.ms-outlook";
    if (extension == ".evtx") return "application/evtx";
    if (extension == ".log" || extension == ".txt" || extension == ".json" ||
        extension == ".csv" || extension == ".xml" || extension == ".html")
        return "text/plain";
    return "application/octet-stream";
}

bool looks_text(const std::vector<unsigned char>& head) {
    if (head.empty()) return false;
    std::size_t printable = 0;
    for (const unsigned char byte : head) {
        if ((byte >= 0x20 && byte < 0x7F) || byte == '\n' || byte == '\r' ||
            byte == '\t' || byte >= 0x80)
            ++printable;
    }
    return printable * 100 >= head.size() * 92;
}

} // namespace

RecoveryIndex scan_recovery_directory(const std::string& root,
                                      std::string& error) {
    error.clear();
    RecoveryIndex index;
    index.root = root;
    index.generated_at_utc = utc_now();
    std::error_code fs_error;
    const std::filesystem::path base(root);
    if (!std::filesystem::exists(base, fs_error)) {
        error = "recovery directory does not exist";
        return index;
    }
    std::map<std::string, std::vector<std::string>> by_hash;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             base, std::filesystem::directory_options::skip_permission_denied,
             fs_error)) {
        if (!entry.is_regular_file(fs_error)) continue;
        const std::filesystem::path path = entry.path();
        if (lowercase(path.filename().string()) == "index.json") continue;
        RecoveredFile file;
        file.path = path.string();
        file.relative = std::filesystem::relative(path, base, fs_error).string();
        file.size = static_cast<std::uint64_t>(entry.file_size(fs_error));
        std::ifstream input(path, std::ios::binary);
        std::vector<unsigned char> head(16, 0);
        input.read(reinterpret_cast<char*>(head.data()),
                   static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(input.gcount()));
        file.kind = detect_kind(path, head);
        file.extension = lowercase(path.extension().string());
        file.text_like = looks_text(head) || file.kind == "text/plain";
        if (!sha256_file(path, file.sha256)) {
            error = "could not hash " + file.relative;
            return index;
        }
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
            file.modified_utc = filetime_utc(attributes.ftLastWriteTime);
        by_hash[file.sha256].push_back(file.path);
        index.files.push_back(std::move(file));
    }
    for (const auto& [hash, paths] : by_hash) {
        if (paths.size() > 1) index.duplicate_groups.push_back({hash, paths});
    }
    return index;
}

bool write_recovery_index_json(const RecoveryIndex& index,
                               const std::string& path, std::string& error) {
    error.clear();
    try {
        nlohmann::json document;
        document["schema"] = index.schema;
        document["root"] = index.root;
        document["generated_at_utc"] = index.generated_at_utc;
        document["files"] = nlohmann::json::array();
        for (const auto& file : index.files) {
            document["files"].push_back({
                {"path", file.path},
                {"relative", file.relative},
                {"kind", file.kind},
                {"extension", file.extension},
                {"size", file.size},
                {"sha256", file.sha256},
                {"modified_utc", file.modified_utc},
                {"text_like", file.text_like}
            });
        }
        document["duplicate_groups"] = nlohmann::json::array();
        for (const auto& group : index.duplicate_groups) {
            document["duplicate_groups"].push_back({
                {"sha256", group.sha256},
                {"paths", group.paths}
            });
        }
        const std::filesystem::path target(path);
        if (target.has_parent_path()) {
            std::error_code directory_error;
            std::filesystem::create_directories(target.parent_path(),
                                               directory_error);
            if (directory_error) {
                error = directory_error.message();
                return false;
            }
        }
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "could not open recovery index";
            return false;
        }
        output << document.dump(2) << '\n';
        return static_cast<bool>(output);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace deep::forensic
