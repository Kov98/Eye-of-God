// language: C++, file: case_crypto.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "case_crypto.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>

#include <cstdio>
#include <cstring>
#include <fstream>

namespace deep {
namespace {

constexpr std::size_t kKeyBytes = 32;
constexpr std::size_t kBlock = 16;

// Win32, not fstream: libstdc++ has no wide-character path overload, which is the
// same wall the evidence reader hit.
bool read_file(const std::wstring& path, crypto_bytes& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    out.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD got = 0;
    const BOOL ok = out.empty() ? TRUE
                                : ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != out.size()) { out.clear(); return false; }
    return true;
}

bool write_file(const std::wstring& path, const crypto_bytes& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &put, nullptr);
    CloseHandle(h);
    return ok && put == data.size();
}

bool random_bytes(crypto_bytes& out, std::size_t len) {
    out.assign(len, 0);
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(len),
                                         BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

}  // namespace

case_cipher::~case_cipher() {
    if (raw_key_) BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(raw_key_));
    if (algorithm_) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(algorithm_), 0);
}

bool case_cipher::open_or_create(const std::wstring& key_path, std::string& error) {
    crypto_bytes wrapped;
    if (read_file(key_path, wrapped) && !wrapped.empty()) {
        return load(key_path, error);
    }

    // No key yet: mint one and seal it to this Windows account.
    if (!random_bytes(key_, kKeyBytes)) { error = "BCryptGenRandom falhou"; return false; }

    DATA_BLOB plain{static_cast<DWORD>(key_.size()), key_.data()};
    DATA_BLOB sealed{};
    if (!CryptProtectData(&plain, L"Olho de Deus case key", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &sealed)) {
        error = "CryptProtectData falhou (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    crypto_bytes out(sealed.pbData, sealed.pbData + sealed.cbData);
    LocalFree(sealed.pbData);
    if (!write_file(key_path, out)) { error = "nao consegui gravar o arquivo de chave"; return false; }

    // Build the cipher context from the freshly made key.
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        error = "BCryptOpenAlgorithmProvider falhou";
        return false;
    }
    algorithm_ = algorithm;
    BCRYPT_KEY_HANDLE key = nullptr;
    const PUCHAR mode = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(BCRYPT_CHAIN_MODE_ECB));
    BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, mode, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
            algorithm, &key, nullptr, 0, key_.data(), static_cast<ULONG>(key_.size()), 0))) {
        error = "BCryptGenerateSymmetricKey falhou";
        return false;
    }
    raw_key_ = key;
    ready_ = true;
    return true;
}

bool case_cipher::load(const std::wstring& key_path, std::string& error) {
    crypto_bytes wrapped;
    if (!read_file(key_path, wrapped) || wrapped.empty()) {
        error = "arquivo de chave ausente: " + std::string(key_path.begin(), key_path.end());
        return false;
    }
    DATA_BLOB sealed{static_cast<DWORD>(wrapped.size()), wrapped.data()};
    DATA_BLOB plain{};
    if (!CryptUnprotectData(&sealed, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
        error = "CryptUnprotectData falhou (" + std::to_string(GetLastError()) +
                "); a chave nao pertence a esta conta ou a este computador";
        return false;
    }
    key_.assign(plain.pbData, plain.pbData + plain.cbData);
    LocalFree(plain.pbData);
    if (key_.size() != kKeyBytes) { error = "chave com tamanho inesperado"; return false; }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        error = "BCryptOpenAlgorithmProvider falhou";
        return false;
    }
    algorithm_ = algorithm;
    const PUCHAR mode = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(BCRYPT_CHAIN_MODE_ECB));
    BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, mode, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    BCRYPT_KEY_HANDLE key = nullptr;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
            algorithm, &key, nullptr, 0, key_.data(), static_cast<ULONG>(key_.size()), 0))) {
        error = "BCryptGenerateSymmetricKey falhou";
        return false;
    }
    raw_key_ = key;
    ready_ = true;
    return true;
}

bool case_cipher::apply(std::uint64_t offset, std::uint8_t* data, std::size_t len) const {
    if (!ready_ || len == 0) return ready_;
    BCRYPT_KEY_HANDLE key = static_cast<BCRYPT_KEY_HANDLE>(raw_key_);

    std::size_t done = 0;
    std::uint64_t block_index = offset / kBlock;
    const std::size_t in_block = static_cast<std::size_t>(offset % kBlock);

    std::uint8_t counter[kBlock];
    std::uint8_t stream[kBlock];

    while (done < len) {
        // Counter block: the file's block index, big-endian. Encrypting it with
        // ECB gives 16 keystream bytes for that position and nothing else, which
        // is what makes an arbitrary offset work.
        std::memset(counter, 0, sizeof(counter));
        std::uint64_t value = block_index;
        for (int i = 0; i < 8; ++i) {
            counter[kBlock - 1 - i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
        }
        ULONG written = 0;
        if (!BCRYPT_SUCCESS(BCryptEncrypt(key, counter, kBlock, nullptr, nullptr, 0,
                                          stream, kBlock, &written, 0))) {
            return false;
        }
        const std::size_t start = (done == 0) ? in_block : 0;
        for (std::size_t i = start; i < kBlock && done < len; ++i) {
            data[done] ^= stream[i];
            ++done;
        }
        ++block_index;
    }
    return true;
}

std::string case_cipher::keystream_probe() const {
    std::uint8_t zeros[kBlock] = {0};
    if (!apply(0, zeros, kBlock)) return {};
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : zeros) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 15]); }
    return out;
}

}  // namespace deep
