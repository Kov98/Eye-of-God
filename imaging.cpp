// language: C++, file: imaging.cpp, target: Windows 10/11
// Raw image acquisition: source image or live volume, 4 MiB reads, incremental
// SHA-256, optional split, JSON manifest. Read-only on the source.

#include "imaging.hpp"

#include "artifacts.hpp"

#include <windows.h>
#include <winioctl.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace deep {
namespace {

class Sha256 {
public:
    ~Sha256() { reset(); }

    bool begin() {
        reset();
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_,
            BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
        DWORD object_length = 0;
        DWORD result = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
            &result, 0))) return false;
        DWORD hash_length = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm_, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length),
            &result, 0))) return false;
        object_.resize(object_length);
        value_.resize(hash_length);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm_, &hash_, object_.data(),
            object_length, nullptr, 0, 0))) return false;
        ready_ = true;
        return true;
    }

    bool update(const void* data, std::size_t size) {
        if (!ready_) return false;
        return BCRYPT_SUCCESS(BCryptHashData(hash_,
            const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
            static_cast<ULONG>(size), 0)) != 0;
    }

    bool finish(std::string& hex) {
        if (!ready_) return false;
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash_, value_.data(),
            static_cast<ULONG>(value_.size()), 0))) return false;
        static const char* digits = "0123456789abcdef";
        hex.resize(value_.size() * 2);
        for (std::size_t i = 0; i < value_.size(); ++i) {
            hex[i * 2] = digits[value_[i] >> 4];
            hex[i * 2 + 1] = digits[value_[i] & 0x0F];
        }
        reset();
        return true;
    }

private:
    void reset() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
        hash_ = nullptr;
        algorithm_ = nullptr;
        ready_ = false;
    }

    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<std::uint8_t> object_;
    std::vector<std::uint8_t> value_;
    bool ready_ = false;
};

bool is_volume_path(const std::wstring& path) {
    return path.rfind(L"\\\\.\\", 0) == 0 ||
        (path.size() == 2 && path[1] == L':') ||
        (path.size() == 3 && path[1] == L':' &&
         (path[2] == L'\\' || path[2] == L'/'));
}

std::wstring device_path(const std::wstring& path) {
    if (path.rfind(L"\\\\.\\", 0) == 0) return path;
    if (path.size() >= 2 && path[1] == L':') return L"\\\\.\\" + path.substr(0, 2);
    return path;
}

bool open_source(const std::wstring& path, HANDLE& handle, std::uint64_t& size,
                 bool& volume, std::wstring& error) {
    volume = is_volume_path(path);
    const std::wstring open_path = volume ? device_path(path) : art::extend_path(path);
    handle = CreateFileW(open_path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, volume ? FILE_FLAG_RANDOM_ACCESS : FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = L"open failed, error " + std::to_wstring(GetLastError()) +
            (volume ? L" (admin required)" : L"");
        return false;
    }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(handle, &length) || length.QuadPart <= 0) {
        GET_LENGTH_INFORMATION info{};
        DWORD returned = 0;
        if (!DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                             &info, sizeof(info), &returned, nullptr)) {
            error = L"size unavailable, error " + std::to_wstring(GetLastError());
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            return false;
        }
        length.QuadPart = info.Length.QuadPart;
    }
    size = static_cast<std::uint64_t>(length.QuadPart);
    return size > 0;
}

std::wstring part_path(const std::wstring& output, unsigned index) {
    if (index == 0) return output;
    wchar_t suffix[16];
    std::swprintf(suffix, 16, L".%03u", index);
    return output + suffix;
}

bool write_manifest(const std::wstring& output, const std::wstring& source,
                    std::uint64_t size, const std::string& digest,
                    std::uint64_t split_bytes, unsigned parts,
                    std::wstring& error) {
    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD computer_len = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer, &computer_len);
    wchar_t user[256] = {};
    DWORD user_len = 256;
    GetUserNameW(user, &user_len);
    std::ostringstream json;
    json << "{\n"
         << "  \"format\": \"EOG_RAW_IMAGE_V1\",\n"
         << "  \"source\": " << art::json_escape_w(source) << ",\n"
         << "  \"output\": " << art::json_escape_w(output) << ",\n"
         << "  \"size\": " << size << ",\n"
         << "  \"sha256\": " << art::json_escape(digest) << ",\n"
         << "  \"split_bytes\": " << split_bytes << ",\n"
         << "  \"parts\": " << parts << ",\n"
         << "  \"created_utc\": " << art::json_escape(art::now_iso()) << ",\n"
         << "  \"computer\": " << art::json_escape_w(computer) << ",\n"
         << "  \"operator\": " << art::json_escape_w(user) << "\n}\n";
    const std::wstring manifest = output + L".manifest.json";
    HANDLE file = CreateFileW(art::extend_path(manifest).c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"cannot create manifest, error " +
            std::to_wstring(GetLastError());
        return false;
    }
    const std::string text = json.str();
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
                              &written, nullptr) && written == text.size();
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) error = L"manifest write failed";
    return ok;
}

} // namespace

int create_image(const ImageOptions& options) {
    SetConsoleOutputCP(CP_UTF8);
    if (options.source.empty() || options.output.empty()) {
        std::cerr << "image-create: source and output are required\n";
        return 2;
    }
    HANDLE source = INVALID_HANDLE_VALUE;
    std::uint64_t size = 0;
    bool volume = false;
    std::wstring error;
    if (!open_source(options.source, source, size, volume, error)) {
        std::cerr << "image-create: " << art::to_utf8(error) << "\n";
        return 1;
    }
    Sha256 hasher;
    if (!hasher.begin()) {
        std::cerr << "image-create: SHA-256 initialization failed\n";
        CloseHandle(source);
        return 1;
    }

    const std::uint64_t split = options.split_bytes;
    unsigned part_index = split == 0 ? 0 : 1;
    std::wstring current = part_path(options.output, part_index);
    HANDLE output = CreateFileW(art::extend_path(current).c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        std::cerr << "image-create: cannot create output, error "
                  << GetLastError() << "\n";
        CloseHandle(source);
        return 1;
    }

    std::vector<std::uint8_t> buffer(4 * 1024 * 1024);
    std::uint64_t offset = 0;
    std::uint64_t part_written = 0;
    bool ok = true;
    while (offset < size && ok) {
        const DWORD want = static_cast<DWORD>(
            (std::min)(static_cast<std::uint64_t>(buffer.size()), size - offset));
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(source, position, nullptr, FILE_BEGIN)) {
            ok = false;
            break;
        }
        DWORD read = 0;
        if (!ReadFile(source, buffer.data(), want, &read, nullptr) || read != want) {
            std::cerr << "image-create: read failed at offset " << offset
                      << " error " << GetLastError() << "\n";
            ok = false;
            break;
        }
        if (!hasher.update(buffer.data(), read)) { ok = false; break; }
        std::size_t cursor = 0;
        while (cursor < read && ok) {
            if (split != 0 && part_written == split) {
                FlushFileBuffers(output);
                CloseHandle(output);
                ++part_index;
                current = part_path(options.output, part_index);
                output = CreateFileW(art::extend_path(current).c_str(),
                    GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (output == INVALID_HANDLE_VALUE) { ok = false; break; }
                part_written = 0;
            }
            std::size_t chunk = static_cast<std::size_t>(read - cursor);
            if (split != 0) chunk = static_cast<std::size_t>(
                (std::min)(static_cast<std::uint64_t>(chunk), split - part_written));
            DWORD written = 0;
            if (!WriteFile(output, buffer.data() + cursor,
                           static_cast<DWORD>(chunk), &written, nullptr) ||
                written != chunk) {
                ok = false;
                break;
            }
            cursor += written;
            part_written += written;
        }
        offset += read;
        if ((offset % (64ull * 1024 * 1024)) < read) {
            std::cout << "  image: " << (offset / (1024 * 1024)) << " MiB\r";
            std::cout.flush();
        }
    }
    FlushFileBuffers(output);
    CloseHandle(output);
    CloseHandle(source);
    std::string digest;
    if (!ok || !hasher.finish(digest)) {
        std::cerr << "image-create: acquisition failed\n";
        return 1;
    }
    const unsigned parts = split == 0 ? 1 : part_index;
    if (!write_manifest(options.output, options.source, size, digest, split, parts,
                        error)) {
        std::cerr << "image-create: " << art::to_utf8(error) << "\n";
        return 1;
    }
    std::cout << "image-create: bytes=" << size << " sha256=" << digest
              << " parts=" << parts << "\n";
    std::cout << "image-create: output=" << art::to_utf8(options.output) << "\n";
    return 0;
}

int verify_image(const std::wstring& image, const std::wstring& manifest_path) {
    SetConsoleOutputCP(CP_UTF8);
    const std::wstring manifest = manifest_path.empty()
        ? image + L".manifest.json" : manifest_path;
    HANDLE file = CreateFileW(art::extend_path(manifest).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::cerr << "image-verify: manifest not found\n";
        return 1;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(file, &text[0], static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    text.resize(read);
    const std::size_t hash_pos = text.find("\"sha256\": \"");
    if (hash_pos == std::string::npos) {
        std::cerr << "image-verify: manifest has no sha256\n";
        return 1;
    }
    const std::string expected = text.substr(hash_pos + 11, 64);

    std::vector<std::wstring> parts;
    std::wstring base = image;
    if (base.size() >= 4 && base.substr(base.size() - 4) == L".001")
        base.resize(base.size() - 4);
    if (GetFileAttributesW((base + L".001").c_str()) != INVALID_FILE_ATTRIBUTES) {
        for (unsigned index = 1; index < 100000; ++index) {
            const std::wstring path = part_path(base, index);
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) break;
            parts.push_back(path);
        }
    } else {
        parts.push_back(image);
    }
    Sha256 hasher;
    if (!hasher.begin()) return 1;
    std::uint64_t total = 0;
    for (const auto& part : parts) {
        HANDLE input = CreateFileW(art::extend_path(part).c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (input == INVALID_HANDLE_VALUE) {
            std::cerr << "image-verify: cannot open " << art::to_utf8(part) << "\n";
            return 1;
        }
        std::vector<std::uint8_t> buffer(4 * 1024 * 1024);
        while (true) {
            DWORD got = 0;
            if (!ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()),
                          &got, nullptr)) break;
            if (got == 0) break;
            hasher.update(buffer.data(), got);
            total += got;
        }
        CloseHandle(input);
    }
    std::string actual;
    hasher.finish(actual);
    std::cout << "image-verify: expected=" << expected << "\n";
    std::cout << "image-verify: actual  =" << actual << "\n";
    std::cout << "image-verify: bytes=" << total << " parts=" << parts.size() << "\n";
    if (actual == expected) {
        std::cout << "image-verify: OK\n";
        return 0;
    }
    std::cerr << "image-verify: MISMATCH\n";
    return 1;
}

} // namespace deep
