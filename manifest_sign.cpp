// language: C++, file: manifest_sign.cpp, target: Windows 10/11
// CNG ECDSA P-256 / SHA-256 signing for evidence manifests.

#include "manifest_sign.hpp"

#include "artifacts.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace deep {
namespace {

constexpr char kPrivateMagic[8] = {'E', 'O', 'G', 'P', 'R', 'I', 'V', '1'};
constexpr char kPublicMagic[8] = {'E', 'O', 'G', 'P', 'U', 'B', '1'};

bool read_all(const std::wstring& path, std::vector<std::uint8_t>& data,
              std::wstring& error) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"open failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > 256ll * 1024 * 1024) {
        error = L"size invalid";
        CloseHandle(file);
        return false;
    }
    data.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = data.empty() || (ReadFile(file, data.data(),
        static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size());
    CloseHandle(file);
    if (!ok) error = L"read failed";
    return ok;
}

bool write_all(const std::wstring& path, const void* data, std::size_t size,
               std::wstring& error) {
    HANDLE file = CreateFileW(art::extend_path(path).c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"create failed, error " + std::to_wstring(GetLastError());
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, data, static_cast<DWORD>(size), &written,
                              nullptr) && written == size;
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) error = L"write failed";
    return ok;
}

bool sha256(const void* data, std::size_t size, std::vector<std::uint8_t>& digest) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    do {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm,
            BCRYPT_SHA256_ALGORITHM, nullptr, 0))) break;
        DWORD object_length = 0, hash_length = 0, result = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
            &result, 0))) break;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length),
            &result, 0))) break;
        std::vector<std::uint8_t> object(object_length);
        digest.resize(hash_length);
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(),
            object_length, nullptr, 0, 0))) break;
        if (!BCRYPT_SUCCESS(BCryptHashData(hash,
            const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
            static_cast<ULONG>(size), 0))) break;
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), hash_length, 0)))
            break;
        ok = true;
    } while (false);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

std::string to_hex(const std::vector<std::uint8_t>& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.resize(data.size() * 2);
    for (std::size_t i = 0; i < data.size(); ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    return out;
}

bool from_hex(const std::string& text, std::vector<std::uint8_t>& data) {
    if (text.size() % 2 != 0) return false;
    data.clear();
    data.reserve(text.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = nibble(text[i]);
        const int low = nibble(text[i + 1]);
        if (high < 0 || low < 0) return false;
        data.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

struct KeyBlob {
    std::vector<std::uint8_t> bytes;
    bool is_private = false;
};

bool read_key(const std::wstring& path, KeyBlob& key, std::wstring& error) {
    std::vector<std::uint8_t> raw;
    if (!read_all(path, raw, error)) return false;
    if (raw.size() < 12) {
        error = L"key file too small";
        return false;
    }
    if (std::memcmp(raw.data(), kPrivateMagic, 8) == 0) key.is_private = true;
    else if (std::memcmp(raw.data(), kPublicMagic, 8) == 0) key.is_private = false;
    else {
        error = L"unknown key file format";
        return false;
    }
    key.bytes.assign(raw.begin() + 8, raw.end());
    return !key.bytes.empty();
}

bool write_key(const std::wstring& path, const char magic[8],
               const std::vector<std::uint8_t>& blob, std::wstring& error) {
    std::vector<std::uint8_t> file(magic, magic + 8);
    file.insert(file.end(), blob.begin(), blob.end());
    return write_all(path, file.data(), file.size(), error);
}

struct EcProvider {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    ~EcProvider() {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    bool open() {
        return BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm,
            BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)) != 0;
    }
};

bool import_key(BCRYPT_ALG_HANDLE algorithm, const KeyBlob& key,
                BCRYPT_KEY_HANDLE& handle) {
    return BCRYPT_SUCCESS(BCryptImportKeyPair(algorithm, nullptr,
        key.is_private ? BCRYPT_ECCPRIVATE_BLOB : BCRYPT_ECCPUBLIC_BLOB,
        &handle, const_cast<PUCHAR>(key.bytes.data()),
        static_cast<ULONG>(key.bytes.size()), 0)) != 0;
}

std::string signature_json(const std::string& manifest_hash,
                           const std::string& key_fingerprint,
                           const std::vector<std::uint8_t>& signature) {
    std::ostringstream out;
    out << "{\n"
        << "  \"format\": \"EOG_ECDSA_P256_SIG_V1\",\n"
        << "  \"algorithm\": \"ECDSA_P256_SHA256\",\n"
        << "  \"manifest_sha256\": " << art::json_escape(manifest_hash) << ",\n"
        << "  \"public_key_sha256\": " << art::json_escape(key_fingerprint) << ",\n"
        << "  \"signature_hex\": " << art::json_escape(to_hex(signature)) << "\n"
        << "}\n";
    return out.str();
}

bool extract_json_string(const std::string& text, const std::string& field,
                         std::string& value) {
    const std::string prefix = "\"" + field + "\": \"";
    const std::size_t start = text.find(prefix);
    if (start == std::string::npos) return false;
    const std::size_t value_start = start + prefix.size();
    const std::size_t end = text.find('"', value_start);
    if (end == std::string::npos) return false;
    value = text.substr(value_start, end - value_start);
    return true;
}

} // namespace

int generate_signing_key(const std::wstring& prefix, std::wstring& error) {
    EcProvider provider;
    if (!provider.open()) {
        error = L"ECDSA provider unavailable";
        return 1;
    }
    BCRYPT_KEY_HANDLE key = nullptr;
    if (!BCRYPT_SUCCESS(BCryptGenerateKeyPair(provider.algorithm, &key, 256, 0)) ||
        !BCRYPT_SUCCESS(BCryptFinalizeKeyPair(key, 0))) {
        if (key) BCryptDestroyKey(key);
        error = L"key generation failed";
        return 1;
    }
    std::vector<std::uint8_t> private_blob, public_blob;
    ULONG length = 0;
    if (!BCRYPT_SUCCESS(BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB,
        nullptr, 0, &length, 0))) {
        BCryptDestroyKey(key);
        error = L"private export size failed";
        return 1;
    }
    private_blob.resize(length);
    if (!BCRYPT_SUCCESS(BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB,
        private_blob.data(), length, &length, 0))) {
        BCryptDestroyKey(key);
        error = L"private export failed";
        return 1;
    }
    private_blob.resize(length);
    length = 0;
    if (!BCRYPT_SUCCESS(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB,
        nullptr, 0, &length, 0))) {
        BCryptDestroyKey(key);
        error = L"public export size failed";
        return 1;
    }
    public_blob.resize(length);
    if (!BCRYPT_SUCCESS(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB,
        public_blob.data(), length, &length, 0))) {
        BCryptDestroyKey(key);
        error = L"public export failed";
        return 1;
    }
    public_blob.resize(length);
    BCryptDestroyKey(key);
    if (!write_key(prefix + L".priv", kPrivateMagic, private_blob, error) ||
        !write_key(prefix + L".pub", kPublicMagic, public_blob, error)) {
        return 1;
    }
    std::vector<std::uint8_t> fingerprint;
    sha256(public_blob.data(), public_blob.size(), fingerprint);
    std::cout << "keygen: private=" << art::to_utf8(prefix + L".priv")
              << " public=" << art::to_utf8(prefix + L".pub") << "\n";
    std::cout << "keygen: public_sha256=" << to_hex(fingerprint) << "\n";
    return 0;
}

int sign_manifest(const std::wstring& manifest, const std::wstring& private_key,
                  const std::wstring& signature_path, std::wstring& error) {
    KeyBlob key;
    if (!read_key(private_key, key, error) || !key.is_private) {
        if (error.empty()) error = L"not a private key";
        return 1;
    }
    std::vector<std::uint8_t> manifest_bytes;
    if (!read_all(manifest, manifest_bytes, error)) return 1;
    std::vector<std::uint8_t> manifest_hash;
    if (!sha256(manifest_bytes.data(), manifest_bytes.size(), manifest_hash)) {
        error = L"manifest SHA-256 failed";
        return 1;
    }
    EcProvider provider;
    if (!provider.open()) {
        error = L"ECDSA provider unavailable";
        return 1;
    }
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    if (!import_key(provider.algorithm, key, key_handle)) {
        error = L"private key import failed";
        return 1;
    }
    ULONG signature_length = 0;
    if (!BCRYPT_SUCCESS(BCryptSignHash(key_handle, nullptr, manifest_hash.data(),
        static_cast<ULONG>(manifest_hash.size()), nullptr, 0, &signature_length,
        0))) {
        BCryptDestroyKey(key_handle);
        error = L"signature size failed";
        return 1;
    }
    std::vector<std::uint8_t> signature(signature_length);
    if (!BCRYPT_SUCCESS(BCryptSignHash(key_handle, nullptr, manifest_hash.data(),
        static_cast<ULONG>(manifest_hash.size()), signature.data(),
        signature_length, &signature_length, 0))) {
        BCryptDestroyKey(key_handle);
        error = L"signature failed";
        return 1;
    }
    signature.resize(signature_length);
    BCryptDestroyKey(key_handle);

    std::vector<std::uint8_t> public_blob;
    ULONG public_length = 0;
    BCRYPT_KEY_HANDLE public_key_handle = nullptr;
    if (!import_key(provider.algorithm, key, public_key_handle) ||
        !BCRYPT_SUCCESS(BCryptExportKey(public_key_handle, nullptr,
            BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &public_length, 0))) {
        if (public_key_handle) BCryptDestroyKey(public_key_handle);
        error = L"public export failed";
        return 1;
    }
    public_blob.resize(public_length);
    if (!BCRYPT_SUCCESS(BCryptExportKey(public_key_handle, nullptr,
        BCRYPT_ECCPUBLIC_BLOB, public_blob.data(), public_length, &public_length,
        0))) {
        BCryptDestroyKey(public_key_handle);
        error = L"public export failed";
        return 1;
    }
    public_blob.resize(public_length);
    BCryptDestroyKey(public_key_handle);
    std::vector<std::uint8_t> fingerprint;
    sha256(public_blob.data(), public_blob.size(), fingerprint);
    const std::string signature_text = signature_json(to_hex(manifest_hash),
        to_hex(fingerprint), signature);
    const std::wstring output = signature_path.empty()
        ? manifest + L".sig" : signature_path;
    if (!write_all(output, signature_text.data(), signature_text.size(), error))
        return 1;
    std::cout << "sign: manifest=" << art::to_utf8(manifest)
              << " sha256=" << to_hex(manifest_hash) << "\n";
    std::cout << "sign: signature=" << art::to_utf8(output) << "\n";
    return 0;
}

int verify_manifest(const std::wstring& manifest, const std::wstring& public_key,
                    const std::wstring& signature_path, std::wstring& error) {
    KeyBlob key;
    if (!read_key(public_key, key, error) || key.is_private) {
        if (error.empty()) error = L"not a public key";
        return 1;
    }
    const std::wstring sig_path = signature_path.empty()
        ? manifest + L".sig" : signature_path;
    std::vector<std::uint8_t> signature_file;
    if (!read_all(sig_path, signature_file, error)) return 1;
    const std::string signature_text(signature_file.begin(), signature_file.end());
    std::string manifest_hash_hex, key_hash_hex, signature_hex;
    if (!extract_json_string(signature_text, "manifest_sha256", manifest_hash_hex) ||
        !extract_json_string(signature_text, "public_key_sha256", key_hash_hex) ||
        !extract_json_string(signature_text, "signature_hex", signature_hex)) {
        error = L"signature JSON is missing fields";
        return 1;
    }
    std::vector<std::uint8_t> manifest_bytes;
    if (!read_all(manifest, manifest_bytes, error)) return 1;
    std::vector<std::uint8_t> manifest_hash;
    sha256(manifest_bytes.data(), manifest_bytes.size(), manifest_hash);
    if (to_hex(manifest_hash) != manifest_hash_hex) {
        std::cerr << "verify: manifest hash mismatch\n";
        return 1;
    }
    std::vector<std::uint8_t> fingerprint;
    sha256(key.bytes.data(), key.bytes.size(), fingerprint);
    if (to_hex(fingerprint) != key_hash_hex) {
        std::cerr << "verify: public key fingerprint mismatch\n";
        return 1;
    }
    std::vector<std::uint8_t> signature;
    if (!from_hex(signature_hex, signature)) {
        error = L"signature hex invalid";
        return 1;
    }
    EcProvider provider;
    if (!provider.open()) {
        error = L"ECDSA provider unavailable";
        return 1;
    }
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    if (!import_key(provider.algorithm, key, key_handle)) {
        error = L"public key import failed";
        return 1;
    }
    const bool ok = BCRYPT_SUCCESS(BCryptVerifySignature(key_handle, nullptr,
        manifest_hash.data(), static_cast<ULONG>(manifest_hash.size()),
        signature.data(), static_cast<ULONG>(signature.size()), 0)) != 0;
    BCryptDestroyKey(key_handle);
    if (!ok) {
        std::cerr << "verify: signature invalid\n";
        return 1;
    }
    std::cout << "verify: OK manifest=" << art::to_utf8(manifest)
              << " sha256=" << manifest_hash_hex << "\n";
    return 0;
}

} // namespace deep
