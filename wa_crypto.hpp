// language: C++, file: wa_crypto.hpp, target: Windows 11
// Small header-only CNG AES-CBC helper shared by the acquisition modules.
// Header-only on purpose: the SQLCipher core is already proven byte-exact
// against an independent implementation, and refactoring it to share this
// would risk that proof for no gain.
#pragma once

#include <windows.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace olho::wa::crypto {

class aes_cbc {
public:
    explicit aes_cbc(const std::uint8_t* key, std::size_t key_len) {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg_, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
            alg_ = nullptr;
            return;
        }
        const PUCHAR mode = const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(BCRYPT_CHAIN_MODE_CBC));
        if (!BCRYPT_SUCCESS(BCryptSetProperty(alg_, BCRYPT_CHAINING_MODE, mode,
                                              sizeof(BCRYPT_CHAIN_MODE_CBC), 0))) return;
        ULONG obj_len = 0, written = 0;
        BCryptGetProperty(alg_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len),
                          sizeof(obj_len), &written, 0);
        object_.resize(obj_len);
        if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
                alg_, &key_, object_.empty() ? nullptr : object_.data(), obj_len,
                const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key)),
                static_cast<ULONG>(key_len), 0))) {
            key_ = nullptr;
        }
    }

    ~aes_cbc() {
        if (key_) BCryptDestroyKey(key_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    aes_cbc(const aes_cbc&) = delete;
    aes_cbc& operator=(const aes_cbc&) = delete;

    bool ok() const { return key_ != nullptr; }

    // Streams: CNG rewrites the IV buffer with the final ciphertext block, so
    // passing the same buffer on the next call chains the CBC correctly. That
    // lets a 400 MB video decrypt without ever being resident.
    bool decrypt_update(std::uint8_t* buf, std::size_t len, std::uint8_t* iv) {
        if (!key_ || len == 0 || (len % 16) != 0) return false;
        ULONG written = 0;
        return BCRYPT_SUCCESS(BCryptDecrypt(key_, buf, static_cast<ULONG>(len), nullptr,
                                            iv, 16, buf, static_cast<ULONG>(len),
                                            &written, 0));
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
    std::vector<std::uint8_t> object_;
};

}  // namespace olho::wa::crypto
