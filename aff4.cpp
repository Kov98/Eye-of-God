// language: C++, file: aff4.cpp, target: Windows 10/11
// AFF4-L directory container writer/verifier. The image is an uncompressed
// ImageStream of 32 KiB chunks, with big-endian index entries and SHA-256 in
// the information.turtle metadata.

#include "aff4.hpp"

#include "artifacts.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>

namespace deep {
namespace {

constexpr std::size_t kChunk = 32768;

std::wstring guid_string() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return L"00000000-0000-0000-0000-000000000000";
    wchar_t text[64] = {};
    StringFromGUID2(guid, text, 64);
    std::wstring value(text);
    if (!value.empty() && value.front() == L'{') value.erase(value.begin());
    if (!value.empty() && value.back() == L'}') value.pop_back();
    return value;
}

bool read_source(const std::wstring& path, HANDLE& file, std::uint64_t& size,
                 std::wstring& error) {
    file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"source open failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 0) {
        error = L"source size failed";
        CloseHandle(file);
        return false;
    }
    size = static_cast<std::uint64_t>(length.QuadPart);
    return true;
}

bool write_file(const std::wstring& path, const std::string& text,
                std::wstring& error) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"create failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
                              &written, nullptr) && written == text.size();
    CloseHandle(file);
    if (!ok) error = L"write failed";
    return ok;
}

bool read_text(const std::wstring& path, std::string& text) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > 16ll * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    text.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = text.empty() || (ReadFile(file, text.data(),
        static_cast<DWORD>(text.size()), &read, nullptr) && read == text.size());
    CloseHandle(file);
    return ok;
}

class Sha256 {
public:
    Sha256() {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_,
            BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return;
        DWORD object_length = 0, hash_length = 0, result = 0;
        BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
            &result, 0);
        BCryptGetProperty(algorithm_, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length),
            &result, 0);
        object_.resize(object_length);
        digest_.resize(hash_length);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm_, &hash_, object_.data(),
            object_length, nullptr, 0, 0)))
            hash_ = nullptr;
    }
    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    bool update(const void* data, std::size_t size) {
        return hash_ && BCRYPT_SUCCESS(BCryptHashData(hash_,
            const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
            static_cast<ULONG>(size), 0));
    }
    bool finish(std::vector<std::uint8_t>& digest) {
        if (!hash_) return false;
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash_, digest_.data(),
            static_cast<ULONG>(digest_.size()), 0)))
            return false;
        digest = digest_;
        return true;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<std::uint8_t> object_;
    std::vector<std::uint8_t> digest_;
};

std::string hex(const std::vector<std::uint8_t>& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::uint8_t byte : data) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

void append_be64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 7; i >= 0; --i)
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

std::uint64_t read_be64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
}

std::uint32_t read_be32(const std::uint8_t* p) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | p[i];
    return value;
}

bool write_block(HANDLE file, const void* data, std::size_t size) {
    DWORD written = 0;
    return WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) &&
        written == size;
}

bool read_exact(HANDLE file, void* data, std::size_t size) {
    DWORD read = 0;
    return ReadFile(file, data, static_cast<DWORD>(size), &read, nullptr) &&
        read == size;
}

} // namespace

int create_aff4(const std::wstring& source, const std::wstring& out_dir,
                std::wstring& error) {
    HANDLE input = INVALID_HANDLE_VALUE;
    std::uint64_t size = 0;
    if (!read_source(source, input, size, error)) return 1;
    if (!art::ensure_directory(out_dir)) {
        CloseHandle(input);
        error = L"cannot create AFF4 directory";
        return 1;
    }
    const std::wstring image_guid = guid_string();
    const std::wstring stream_guid = guid_string();
    const std::wstring volume_guid = guid_string();
    const std::wstring volume_uri = L"aff4://" + volume_guid;
    const std::wstring stream_uri = L"aff4://" + stream_guid;
    const std::wstring stream_dir = art::join_path(out_dir,
        L"aff4%3A%2F%2F" + stream_guid);
    if (!art::ensure_directory(stream_dir)) {
        CloseHandle(input);
        error = L"cannot create AFF4 stream directory";
        return 1;
    }
    const std::string version = "major=1\r\nminor=0\r\ntool=OlhoDeDeus deep\r\n";
    if (!write_file(art::join_path(out_dir, L"version.txt"), version, error) ||
        !write_file(art::join_path(out_dir, L"container.description"),
                    art::to_utf8(volume_uri) + "\r\n", error)) {
        CloseHandle(input);
        return 1;
    }
    const std::wstring bevy_path = art::join_path(stream_dir, L"00000000");
    const std::wstring index_path = art::join_path(stream_dir,
        L"00000000.index");
    HANDLE bevy = CreateFileW(bevy_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (bevy == INVALID_HANDLE_VALUE) {
        CloseHandle(input);
        error = L"cannot create bevy";
        return 1;
    }
    Sha256 sha;
    std::vector<std::uint8_t> chunk(kChunk, 0);
    std::vector<std::uint8_t> index;
    std::uint64_t remaining = size;
    std::uint64_t offset = 0;
    bool ok = true;
    while (remaining > 0) {
        const std::size_t want = static_cast<std::size_t>(
            (std::min)(remaining, static_cast<std::uint64_t>(kChunk)));
        if (!read_exact(input, chunk.data(), want)) {
            error = L"source read failed";
            ok = false;
            break;
        }
        if (want < kChunk) std::memset(chunk.data() + want, 0, kChunk - want);
        if (!sha.update(chunk.data(), want) ||
            !write_block(bevy, chunk.data(), kChunk)) {
            error = L"bevy write failed";
            ok = false;
            break;
        }
        append_be64(index, offset);
        append_be32(index, static_cast<std::uint32_t>(kChunk));
        offset += kChunk;
        remaining -= want;
    }
    CloseHandle(input);
    CloseHandle(bevy);
    if (!ok) {
        DeleteFileW(bevy_path.c_str());
        return 1;
    }
    if (!write_file(index_path,
        std::string(index.begin(), index.end()), error))
        return 1;
    std::vector<std::uint8_t> digest;
    if (!sha.finish(digest)) {
        error = L"SHA-256 finalization failed";
        return 1;
    }
    const std::string hash_hex = hex(digest);
    const std::string turtle =
        "@prefix aff4: <http://aff4.org/Schema#> .\r\n"
        "@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\r\n\r\n"
        "<aff4://" + art::to_utf8(image_guid) + ">\r\n"
        "    a aff4:DiskImage, aff4:ContiguousImage, aff4:Image ;\r\n"
        "    aff4:dataStream <" + art::to_utf8(stream_uri) + "> ;\r\n"
        "    aff4:size \"" + std::to_string(size) + "\"^^xsd:long ;\r\n"
        "    aff4:hash \"" + hash_hex + "\"^^aff4:SHA256 .\r\n\r\n"
        "<" + art::to_utf8(stream_uri) + ">\r\n"
        "    a aff4:ImageStream ;\r\n"
        "    aff4:chunkSize \"32768\"^^xsd:int ;\r\n"
        "    aff4:size \"" + std::to_string(size) + "\"^^xsd:long ;\r\n"
        "    aff4:stored <" + art::to_utf8(volume_uri) + "> .\r\n";
    if (!write_file(art::join_path(out_dir, L"information.turtle"), turtle,
                    error))
        return 1;
    std::cout << "aff4: chunks=" << (size + kChunk - 1) / kChunk
              << " bytes=" << size << " sha256=" << hash_hex << "\n";
    std::cout << "aff4: out=" << art::to_utf8(out_dir) << "\n";
    return 0;
}


namespace {

std::uint32_t crc32_file(const std::wstring& path, std::uint64_t& size) {
    static std::uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        initialized = true;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER length{};
    GetFileSizeEx(file, &length);
    size = static_cast<std::uint64_t>(length.QuadPart);
    std::uint32_t crc = 0xFFFFFFFFu;
    std::vector<std::uint8_t> buffer(1024 * 1024);
    DWORD read = 0;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &read, nullptr) && read > 0) {
        for (DWORD i = 0; i < read; ++i)
            crc = table[(crc ^ buffer[i]) & 0xFF] ^ (crc >> 8);
    }
    CloseHandle(file);
    return crc ^ 0xFFFFFFFFu;
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

bool append_file(HANDLE zip, const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::vector<std::uint8_t> buffer(1024 * 1024);
    DWORD read = 0;
    bool ok = true;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &read, nullptr) && read > 0) {
        DWORD written = 0;
        if (!WriteFile(zip, buffer.data(), read, &written, nullptr) ||
            written != read) {
            ok = false;
            break;
        }
    }
    CloseHandle(file);
    return ok;
}

} // namespace

int create_aff4_zip(const std::wstring& source, const std::wstring& out_file,
                    std::wstring& error) {
    wchar_t temp_path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp_path);
    const std::wstring temp = art::join_path(temp_path,
        L"eog_aff4_zip_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount()));
    if (create_aff4(source, temp, error) != 0) return 1;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(temp, ec)) {
        if (entry.is_regular_file(ec)) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        const std::string an = a.filename().string();
        const std::string bn = b.filename().string();
        if (an == "container.description") return true;
        if (bn == "container.description") return false;
        return a.generic_string() < b.generic_string();
    });
    struct Entry {
        std::string name;
        std::wstring path;
        std::uint64_t size = 0;
        std::uint32_t crc = 0;
        std::uint64_t offset = 0;
    };
    std::vector<Entry> entries;
    for (const auto& path : files) {
        Entry entry;
        entry.path = path.wstring();
        entry.name = std::filesystem::relative(path, temp, ec).generic_string();
        std::uint64_t size = 0;
        const std::uint32_t crc = crc32_file(entry.path, size);
        entry.size = size;
        entry.crc = crc;
        entries.push_back(std::move(entry));
    }
    HANDLE zip = CreateFileW(out_file.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (zip == INVALID_HANDLE_VALUE) {
        error = L"cannot create AFF4 ZIP";
        std::filesystem::remove_all(temp, ec);
        return 1;
    }
    std::vector<std::uint8_t> central;
    for (auto& entry : entries) {
        LARGE_INTEGER position{};
        GetFileSizeEx(zip, &position);
        entry.offset = static_cast<std::uint64_t>(position.QuadPart);
        std::vector<std::uint8_t> local;
        put32(local, 0x04034b50);
        put16(local, 45);
        put16(local, 0);
        put16(local, 0);
        put16(local, 0);
        put16(local, 0);
        put32(local, entry.crc);
        put32(local, 0xFFFFFFFFu);
        put32(local, 0xFFFFFFFFu);
        put16(local, static_cast<std::uint16_t>(entry.name.size()));
        put16(local, 20);
        local.insert(local.end(), entry.name.begin(), entry.name.end());
        put16(local, 0x0001);
        put16(local, 16);
        put64(local, entry.size);
        put64(local, entry.size);
        DWORD written = 0;
        WriteFile(zip, local.data(), static_cast<DWORD>(local.size()), &written,
                  nullptr);
        append_file(zip, entry.path);
        std::vector<std::uint8_t> central_entry;
        put32(central_entry, 0x02014b50);
        put16(central_entry, 45);
        put16(central_entry, 45);
        put16(central_entry, 0);
        put16(central_entry, 0);
        put16(central_entry, 0);
        put16(central_entry, 0);
        put32(central_entry, entry.crc);
        put32(central_entry, 0xFFFFFFFFu);
        put32(central_entry, 0xFFFFFFFFu);
        put16(central_entry, static_cast<std::uint16_t>(entry.name.size()));
        put16(central_entry, 28);
        put16(central_entry, 0);
        put16(central_entry, 0);
        put16(central_entry, 0);
        put32(central_entry, 0);
        put32(central_entry, 0xFFFFFFFFu);
        central_entry.insert(central_entry.end(), entry.name.begin(), entry.name.end());
        put16(central_entry, 0x0001);
        put16(central_entry, 24);
        put64(central_entry, entry.size);
        put64(central_entry, entry.size);
        put64(central_entry, entry.offset);
        central.insert(central.end(), central_entry.begin(), central_entry.end());
    }
    LARGE_INTEGER central_pos{};
    GetFileSizeEx(zip, &central_pos);
    DWORD written = 0;
    WriteFile(zip, central.data(), static_cast<DWORD>(central.size()), &written,
              nullptr);
    LARGE_INTEGER zip64_pos{};
    GetFileSizeEx(zip, &zip64_pos);
    std::vector<std::uint8_t> zip64;
    put32(zip64, 0x06064b50);
    put64(zip64, 44);
    put16(zip64, 45);
    put16(zip64, 45);
    put32(zip64, 0);
    put32(zip64, 0);
    put64(zip64, entries.size());
    put64(zip64, entries.size());
    put64(zip64, central.size());
    put64(zip64, static_cast<std::uint64_t>(central_pos.QuadPart));
    WriteFile(zip, zip64.data(), static_cast<DWORD>(zip64.size()), &written, nullptr);
    std::vector<std::uint8_t> locator;
    put32(locator, 0x07064b50);
    put32(locator, 0);
    put64(locator, static_cast<std::uint64_t>(zip64_pos.QuadPart));
    put32(locator, 1);
    WriteFile(zip, locator.data(), static_cast<DWORD>(locator.size()), &written, nullptr);
    std::string comment;
    std::string description;
    if (read_text(art::join_path(temp, L"container.description"), description)) {
        while (!description.empty() &&
               (description.back() == '\r' || description.back() == '\n' ||
                description.back() == ' '))
            description.pop_back();
        comment = description;
    }
    if (comment.size() > 60000) comment.resize(60000);
    std::vector<std::uint8_t> eocd;
    put32(eocd, 0x06054b50);
    put16(eocd, 0);
    put16(eocd, 0);
    put16(eocd, 0xFFFF);
    put16(eocd, 0xFFFF);
    put32(eocd, 0xFFFFFFFFu);
    put32(eocd, 0xFFFFFFFFu);
    put16(eocd, static_cast<std::uint16_t>(comment.size()));
    eocd.insert(eocd.end(), comment.begin(), comment.end());
    WriteFile(zip, eocd.data(), static_cast<DWORD>(eocd.size()), &written, nullptr);
    FlushFileBuffers(zip);
    CloseHandle(zip);
    std::filesystem::remove_all(temp, ec);
    std::cout << "aff4-zip: entries=" << entries.size() << " out="
              << art::to_utf8(out_file) << "\n";
    return 0;
}
namespace {

std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t le64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

bool read_binary_all(const std::wstring& path, std::vector<std::uint8_t>& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > 4ll * 1024 * 1024 * 1024) {
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

std::uint32_t crc32_bytes(const void* data, std::size_t size) {
    static std::uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        initialized = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void remove_tree(const std::wstring& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int verify_aff4_zip(const std::wstring& zip_file, std::wstring& error) {
    std::vector<std::uint8_t> data;
    if (!read_binary_all(zip_file, data) || data.size() < 22) {
        error = L"cannot read AFF4 ZIP";
        return 1;
    }
    std::size_t eocd = data.size();
    for (std::size_t i = data.size() - 22 + 1; i-- > 0;) {
        if (le32(data.data() + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd == data.size()) {
        error = L"EOCD not found";
        return 1;
    }
    std::uint64_t count = le16(data.data() + eocd + 10);
    std::uint64_t central_offset = le32(data.data() + eocd + 16);
    std::uint64_t central_size = le32(data.data() + eocd + 12);
    if (count == 0xFFFF || central_offset == 0xFFFFFFFFu) {
        if (eocd < 20 || le32(data.data() + eocd - 20) != 0x07064b50) {
            error = L"ZIP64 locator missing";
            return 1;
        }
        const std::uint64_t z64 = le64(data.data() + eocd - 20 + 8);
        if (z64 + 56 > data.size() || le32(data.data() + z64) != 0x06064b50) {
            error = L"ZIP64 EOCD missing";
            return 1;
        }
        count = le64(data.data() + z64 + 32);
        central_size = le64(data.data() + z64 + 40);
        central_offset = le64(data.data() + z64 + 48);
    }
    if (central_offset + central_size > data.size()) {
        error = L"central directory outside file";
        return 1;
    }
    wchar_t temp_path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp_path);
    const std::wstring temp = art::join_path(temp_path,
        L"eog_aff4_verify_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount()));
    std::error_code ec;
    std::filesystem::create_directories(temp, ec);
    std::size_t cursor = static_cast<std::size_t>(central_offset);
    for (std::uint64_t index = 0; index < count; ++index) {
        if (cursor + 46 > data.size() || le32(data.data() + cursor) != 0x02014b50) {
            remove_tree(temp);
            error = L"central entry malformed";
            return 1;
        }
        const std::uint16_t flags = le16(data.data() + cursor + 8);
        const std::uint16_t method = le16(data.data() + cursor + 10);
        const std::uint32_t crc = le32(data.data() + cursor + 16);
        std::uint64_t compressed = le32(data.data() + cursor + 20);
        std::uint64_t uncompressed = le32(data.data() + cursor + 24);
        const std::uint16_t name_len = le16(data.data() + cursor + 28);
        const std::uint16_t extra_len = le16(data.data() + cursor + 30);
        const std::uint16_t comment_len = le16(data.data() + cursor + 32);
        std::uint64_t local_offset = le32(data.data() + cursor + 42);
        if (cursor + 46 + name_len + extra_len + comment_len > data.size()) {
            remove_tree(temp);
            error = L"central entry truncated";
            return 1;
        }
        const std::string name(reinterpret_cast<const char*>(data.data() + cursor + 46),
                               name_len);
        const std::uint8_t* extra = data.data() + cursor + 46 + name_len;
        std::size_t extra_pos = 0;
        while (extra_pos + 4 <= extra_len) {
            const std::uint16_t tag = le16(extra + extra_pos);
            const std::uint16_t size = le16(extra + extra_pos + 2);
            if (extra_pos + 4 + size > extra_len) break;
            if (tag == 0x0001) {
                std::size_t value_pos = extra_pos + 4;
                if (uncompressed == 0xFFFFFFFFu && value_pos + 8 <= extra_pos + 4 + size) {
                    uncompressed = le64(extra + value_pos);
                    value_pos += 8;
                }
                if (compressed == 0xFFFFFFFFu && value_pos + 8 <= extra_pos + 4 + size) {
                    compressed = le64(extra + value_pos);
                    value_pos += 8;
                }
                if (local_offset == 0xFFFFFFFFu && value_pos + 8 <= extra_pos + 4 + size) {
                    local_offset = le64(extra + value_pos);
                    value_pos += 8;
                }
            }
            extra_pos += 4 + size;
        }
        if (method != 0 || (flags & 1) != 0) {
            remove_tree(temp);
            error = L"unsupported compression/encryption";
            return 1;
        }
        if (local_offset + 30 > data.size() ||
            le32(data.data() + local_offset) != 0x04034b50) {
            remove_tree(temp);
            error = L"local header malformed";
            return 1;
        }
        const std::uint16_t local_name = le16(data.data() + local_offset + 26);
        const std::uint16_t local_extra = le16(data.data() + local_offset + 28);
        const std::uint64_t start = local_offset + 30 + local_name + local_extra;
        if (start + compressed > data.size() ||
            crc32_bytes(data.data() + start, static_cast<std::size_t>(compressed)) != crc) {
            remove_tree(temp);
            error = L"entry CRC mismatch";
            return 1;
        }
        std::wstring relative(name.begin(), name.end());
        for (wchar_t& c : relative) if (c == L'/') c = L'\\';
        if (relative.find(L"..") != std::wstring::npos) {
            remove_tree(temp);
            error = L"unsafe entry path";
            return 1;
        }
        const std::wstring destination = art::join_path(temp, relative);
        art::ensure_directory(art::join_path(temp, L"."));
        const std::size_t slash = destination.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            art::ensure_directory(destination.substr(0, slash));
        const std::string content(reinterpret_cast<const char*>(data.data() + start),
                                  static_cast<std::size_t>(uncompressed));
        if (!write_file(destination, content, error)) {
            remove_tree(temp);
            return 1;
        }
        cursor += 46 + name_len + extra_len + comment_len;
    }
    const int result = verify_aff4(temp, error);
    remove_tree(temp);
    return result;
}
int verify_aff4(const std::wstring& out_dir, std::wstring& error) {
    std::string turtle;
    if (!read_text(art::join_path(out_dir, L"information.turtle"), turtle)) {
        error = L"information.turtle missing";
        return 1;
    }
    const std::string stream_marker = "aff4:dataStream <aff4://";
    std::size_t position = turtle.find(stream_marker);
    if (position == std::string::npos) {
        error = L"dataStream missing";
        return 1;
    }
    position += stream_marker.size();
    const std::size_t stream_end = turtle.find('>', position);
    if (stream_end == std::string::npos) {
        error = L"dataStream URI malformed";
        return 1;
    }
    const std::string stream_guid = turtle.substr(position, stream_end - position);
    std::size_t size_position = turtle.find("aff4:size \"");
    if (size_position == std::string::npos) {
        error = L"size missing";
        return 1;
    }
    size_position += 11;
    const std::size_t size_end = turtle.find('"', size_position);
    const std::uint64_t size = std::stoull(turtle.substr(size_position,
        size_end - size_position));
    std::size_t hash_position = turtle.find("aff4:hash \"");
    if (hash_position == std::string::npos) {
        error = L"hash missing";
        return 1;
    }
    hash_position += 11;
    const std::size_t hash_end = turtle.find('"', hash_position);
    const std::string expected = turtle.substr(hash_position,
        hash_end - hash_position);
    const std::wstring stream_dir = art::join_path(out_dir,
        L"aff4%3A%2F%2F" + std::wstring(stream_guid.begin(), stream_guid.end()));
    std::string index_text;
    if (!read_text(art::join_path(stream_dir, L"00000000.index"), index_text)) {
        error = L"bevy index missing";
        return 1;
    }
    HANDLE bevy = CreateFileW(art::join_path(stream_dir, L"00000000").c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (bevy == INVALID_HANDLE_VALUE) {
        error = L"bevy missing";
        return 1;
    }
    Sha256 sha;
    std::vector<std::uint8_t> chunk(kChunk, 0);
    std::uint64_t remaining = size;
    const std::uint8_t* index = reinterpret_cast<const std::uint8_t*>(
        index_text.data());
    const std::size_t entries = index_text.size() / 12;
    bool ok = true;
    for (std::size_t i = 0; i < entries && remaining > 0; ++i) {
        const std::uint64_t offset = read_be64(index + i * 12);
        const std::uint32_t chunk_size = read_be32(index + i * 12 + 8);
        if (chunk_size > kChunk) { ok = false; break; }
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(bevy, pos, nullptr, FILE_BEGIN) ||
            !read_exact(bevy, chunk.data(), chunk_size)) {
            ok = false;
            break;
        }
        const std::size_t take = static_cast<std::size_t>(
            (std::min)(remaining, static_cast<std::uint64_t>(chunk_size)));
        if (!sha.update(chunk.data(), take)) { ok = false; break; }
        remaining -= take;
    }
    CloseHandle(bevy);
    if (!ok || remaining != 0) {
        error = L"bevy read failed";
        return 1;
    }
    std::vector<std::uint8_t> digest;
    if (!sha.finish(digest)) {
        error = L"SHA-256 finalization failed";
        return 1;
    }
    const std::string actual = hex(digest);
    if (actual != expected) {
        error = L"AFF4 hash mismatch expected=" + std::wstring(expected.begin(), expected.end()) + L" actual=" + std::wstring(actual.begin(), actual.end());
        return 1;
    }
    std::cout << "aff4-verify: bytes=" << size << " sha256=" << actual << "\n";
    std::cout << "aff4-verify: OK\n";
    return 0;
}

} // namespace deep
