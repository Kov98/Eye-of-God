#define NOMINMAX

#include "ios_keybag.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace olho::ios {
namespace {

void put_be32(std::uint8_t* out, std::uint32_t value) {
    out[0] = static_cast<std::uint8_t>(value >> 24);
    out[1] = static_cast<std::uint8_t>(value >> 16);
    out[2] = static_cast<std::uint8_t>(value >> 8);
    out[3] = static_cast<std::uint8_t>(value);
}

std::string nt_failure(const char* what, NTSTATUS status) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s falhou (NTSTATUS 0x%08lX)", what,
                  static_cast<unsigned long>(status));
    return buffer;
}

bool fits_ulong(std::size_t size) { return size <= 0xFFFFFFFFull; }

// Buffer vazio com ponteiro nulo e rejeitado pelo CNG em algumas rotinas.
const std::uint8_t* safe_ptr(const Bytes& data) {
    static const std::uint8_t kZero = 0;
    return data.empty() ? &kZero : data.data();
}

struct AlgHandle {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~AlgHandle() {
        if (handle) BCryptCloseAlgorithmProvider(handle, 0);
    }
};

struct HashHandle {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~HashHandle() {
        if (handle) BCryptDestroyHash(handle);
    }
};

struct KeyHandle {
    BCRYPT_KEY_HANDLE handle = nullptr;
    ~KeyHandle() {
        if (handle) BCryptDestroyKey(handle);
    }
};

bool open_alg(AlgHandle& alg, LPCWSTR id, ULONG flags, std::string& error) {
    const NTSTATUS status = BCryptOpenAlgorithmProvider(&alg.handle, id, nullptr, flags);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptOpenAlgorithmProvider", status);
        return false;
    }
    return true;
}

bool digest(LPCWSTR alg_id, const Bytes& data, Bytes& out, std::string& error) {
    AlgHandle alg;
    if (!open_alg(alg, alg_id, 0, error)) return false;

    DWORD object_len = 0;
    DWORD hash_len = 0;
    DWORD written = 0;
    NTSTATUS status = BCryptGetProperty(alg.handle, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&object_len),
                                        sizeof(object_len), &written, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptGetProperty(OBJECT_LENGTH)", status);
        return false;
    }
    status = BCryptGetProperty(alg.handle, BCRYPT_HASH_LENGTH,
                               reinterpret_cast<PUCHAR>(&hash_len), sizeof(hash_len),
                               &written, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptGetProperty(HASH_LENGTH)", status);
        return false;
    }
    if (!fits_ulong(data.size())) {
        error = "entrada grande demais para o CNG";
        return false;
    }

    std::vector<UCHAR> object(object_len);
    HashHandle hash;
    status = BCryptCreateHash(alg.handle, &hash.handle, object.empty() ? nullptr : object.data(),
                              object_len, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptCreateHash", status);
        return false;
    }
    status = BCryptHashData(hash.handle, const_cast<PUCHAR>(safe_ptr(data)),
                            static_cast<ULONG>(data.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptHashData", status);
        return false;
    }
    out.assign(hash_len, 0);
    status = BCryptFinishHash(hash.handle, out.data(), hash_len, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptFinishHash", status);
        return false;
    }
    return true;
}

bool hmac(LPCWSTR alg_id, const Bytes& key, const Bytes& data, Bytes& mac,
          std::string& error) {
    AlgHandle alg;
    if (!open_alg(alg, alg_id, BCRYPT_ALG_HANDLE_HMAC_FLAG, error)) return false;

    DWORD object_len = 0;
    DWORD hash_len = 0;
    DWORD written = 0;
    NTSTATUS status = BCryptGetProperty(alg.handle, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&object_len),
                                        sizeof(object_len), &written, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptGetProperty(OBJECT_LENGTH)", status);
        return false;
    }
    status = BCryptGetProperty(alg.handle, BCRYPT_HASH_LENGTH,
                               reinterpret_cast<PUCHAR>(&hash_len), sizeof(hash_len),
                               &written, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptGetProperty(HASH_LENGTH)", status);
        return false;
    }
    if (!fits_ulong(key.size()) || !fits_ulong(data.size())) {
        error = "entrada grande demais para o CNG";
        return false;
    }

    std::vector<UCHAR> object(object_len);
    HashHandle hash;
    status = BCryptCreateHash(alg.handle, &hash.handle, object.empty() ? nullptr : object.data(),
                              object_len, const_cast<PUCHAR>(safe_ptr(key)),
                              static_cast<ULONG>(key.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptCreateHash(HMAC)", status);
        return false;
    }
    status = BCryptHashData(hash.handle, const_cast<PUCHAR>(safe_ptr(data)),
                            static_cast<ULONG>(data.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptHashData", status);
        return false;
    }
    mac.assign(hash_len, 0);
    status = BCryptFinishHash(hash.handle, mac.data(), hash_len, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptFinishHash", status);
        return false;
    }
    return true;
}

// Handle AES pronto para um modo de encadeamento fixo. O IV vai por chamada.
class AesKey {
public:
    ~AesKey() = default;

    bool init(const Bytes& key_bytes, LPCWSTR chain_mode, std::string& error) {
        if (key_bytes.size() != 16 && key_bytes.size() != 24 && key_bytes.size() != 32) {
            error = "tamanho de chave AES invalido";
            return false;
        }
        if (!open_alg(alg_, BCRYPT_AES_ALGORITHM, 0, error)) return false;
        const NTSTATUS set = BCryptSetProperty(
            alg_.handle, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<LPWSTR>(chain_mode)),
            static_cast<ULONG>(sizeof(BCRYPT_CHAINING_MODE)), 0);
        if (!BCRYPT_SUCCESS(set)) {
            error = nt_failure("BCryptSetProperty(CHAINING_MODE)", set);
            return false;
        }
        DWORD object_len = 0;
        DWORD written = 0;
        NTSTATUS status = BCryptGetProperty(alg_.handle, BCRYPT_OBJECT_LENGTH,
                                            reinterpret_cast<PUCHAR>(&object_len),
                                            sizeof(object_len), &written, 0);
        if (!BCRYPT_SUCCESS(status)) {
            error = nt_failure("BCryptGetProperty(OBJECT_LENGTH)", status);
            return false;
        }
        object_.assign(object_len, 0);
        status = BCryptGenerateSymmetricKey(alg_.handle, &key_.handle,
                                            object_.empty() ? nullptr : object_.data(),
                                            object_len,
                                            const_cast<PUCHAR>(safe_ptr(key_bytes)),
                                            static_cast<ULONG>(key_bytes.size()), 0);
        if (!BCRYPT_SUCCESS(status)) {
            error = nt_failure("BCryptGenerateSymmetricKey", status);
            return false;
        }
        return true;
    }

    // O CNG escreve o IV atualizado de volta em pbIV; a copia local impede que a
    // API publica mute o buffer const do chamador.
    bool encrypt(const std::uint8_t* in, std::uint32_t len, const std::uint8_t* iv,
                 std::uint8_t* out, std::string& error) {
        std::uint8_t chain_iv[16] = {0};
        const std::size_t iv_size = iv ? 16 : 0;
        if (iv) std::memcpy(chain_iv, iv, 16);
        DWORD produced = 0;
        const NTSTATUS status = BCryptEncrypt(
            key_.handle, const_cast<PUCHAR>(in), len, nullptr,
            iv ? chain_iv : nullptr, static_cast<ULONG>(iv_size), out, len, &produced, 0);
        if (!BCRYPT_SUCCESS(status) || produced != len) {
            error = nt_failure("BCryptEncrypt", status);
            return false;
        }
        return true;
    }

    bool decrypt(const std::uint8_t* in, std::uint32_t len, const std::uint8_t* iv,
                 std::uint8_t* out, std::string& error) {
        std::uint8_t chain_iv[16] = {0};
        const std::size_t iv_size = iv ? 16 : 0;
        if (iv) std::memcpy(chain_iv, iv, 16);
        DWORD produced = 0;
        const NTSTATUS status = BCryptDecrypt(
            key_.handle, const_cast<PUCHAR>(in), len, nullptr,
            iv ? chain_iv : nullptr, static_cast<ULONG>(iv_size), out, len, &produced, 0);
        if (!BCRYPT_SUCCESS(status) || produced != len) {
            error = nt_failure("BCryptDecrypt", status);
            return false;
        }
        return true;
    }

private:
    AlgHandle alg_;
    KeyHandle key_;
    std::vector<UCHAR> object_;
};

const std::uint8_t kKeyWrapIv[8] = {0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6};

} // namespace

// ------------------------------------------------------------------ bytes/hex
std::string bytes_to_hex(const Bytes& value) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.resize(value.size() * 2);
    for (std::size_t i = 0; i < value.size(); ++i) {
        out[i * 2] = kDigits[value[i] >> 4];
        out[i * 2 + 1] = kDigits[value[i] & 0x0F];
    }
    return out;
}

bool bytes_from_hex(const std::string& text, Bytes& out) {
    out.clear();
    if (text.size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = nibble(text[i]);
        const int low = nibble(text[i + 1]);
        if (high < 0 || low < 0) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

std::uint32_t read_be32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t read_be64(const std::uint8_t* data) {
    return (static_cast<std::uint64_t>(read_be32(data)) << 32) |
           static_cast<std::uint64_t>(read_be32(data + 4));
}

// ------------------------------------------------------------------ digests
bool sha1(const Bytes& data, Bytes& digest_out, std::string& error) {
    return digest(BCRYPT_SHA1_ALGORITHM, data, digest_out, error);
}

bool sha256(const Bytes& data, Bytes& digest_out, std::string& error) {
    return digest(BCRYPT_SHA256_ALGORITHM, data, digest_out, error);
}

struct Sha256Stream::Impl {
    AlgHandle alg;
    HashHandle hash;
    std::vector<UCHAR> object;
};

Sha256Stream::Sha256Stream() = default;
Sha256Stream::~Sha256Stream() { delete impl_; }

bool Sha256Stream::init(std::string& error) {
    delete impl_;
    impl_ = nullptr;
    auto impl = std::make_unique<Impl>();
    if (!open_alg(impl->alg, BCRYPT_SHA256_ALGORITHM, 0, error)) return false;
    DWORD object_len = 0;
    DWORD written = 0;
    const NTSTATUS status = BCryptGetProperty(impl->alg.handle, BCRYPT_OBJECT_LENGTH,
                                              reinterpret_cast<PUCHAR>(&object_len),
                                              sizeof(object_len), &written, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptGetProperty(OBJECT_LENGTH)", status);
        return false;
    }
    impl->object.assign(object_len, 0);
    const NTSTATUS created = BCryptCreateHash(impl->alg.handle, &impl->hash.handle,
                                              impl->object.empty() ? nullptr : impl->object.data(),
                                              object_len, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(created)) {
        error = nt_failure("BCryptCreateHash", created);
        return false;
    }
    impl_ = impl.release();
    return true;
}

bool Sha256Stream::update(const std::uint8_t* data, std::size_t size, std::string& error) {
    if (impl_ == nullptr) {
        error = "Sha256Stream::update sem init";
        return false;
    }
    if (size == 0) return true;
    if (!fits_ulong(size)) {
        error = "bloco grande demais para o CNG";
        return false;
    }
    const NTSTATUS status = BCryptHashData(impl_->hash.handle,
                                           const_cast<PUCHAR>(data),
                                           static_cast<ULONG>(size), 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptHashData", status);
        return false;
    }
    return true;
}

bool Sha256Stream::finish(Bytes& digest, std::string& error) {
    if (impl_ == nullptr) {
        error = "Sha256Stream::finish sem init";
        return false;
    }
    digest.assign(32, 0);
    const NTSTATUS status = BCryptFinishHash(impl_->hash.handle, digest.data(), 32, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptFinishHash", status);
        return false;
    }
    return true;
}

bool hmac_sha1(const Bytes& key, const Bytes& data, Bytes& mac, std::string& error) {
    return hmac(BCRYPT_SHA1_ALGORITHM, key, data, mac, error);
}

bool hmac_sha256(const Bytes& key, const Bytes& data, Bytes& mac, std::string& error) {
    return hmac(BCRYPT_SHA256_ALGORITHM, key, data, mac, error);
}

bool pbkdf2_hmac_sha256(const Bytes& password, const Bytes& salt,
                        std::uint32_t iterations, std::size_t key_len, Bytes& out,
                        std::string& error) {
    out.clear();
    if (iterations == 0) {
        error = "PBKDF2 com zero iteracoes";
        return false;
    }
    if (key_len == 0) {
        error = "PBKDF2 com tamanho de saida zero";
        return false;
    }

    AlgHandle alg;
    if (!open_alg(alg, BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG, error)) {
        return false;
    }
    DWORD object_len = 0;
    DWORD hash_len = 0;
    DWORD written = 0;
    NTSTATUS status = BCryptGetProperty(alg.handle, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&object_len),
                                        sizeof(object_len), &written, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptGetProperty(OBJECT_LENGTH)", status);
        return false;
    }
    status = BCryptGetProperty(alg.handle, BCRYPT_HASH_LENGTH,
                               reinterpret_cast<PUCHAR>(&hash_len), sizeof(hash_len),
                               &written, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = nt_failure("BCryptGetProperty(HASH_LENGTH)", status);
        return false;
    }
    if (!fits_ulong(password.size()) || !fits_ulong(salt.size())) {
        error = "senha ou salt grande demais para o CNG";
        return false;
    }

    std::vector<UCHAR> object(object_len);
    Bytes block_input(salt);
    block_input.resize(salt.size() + 4);
    Bytes u;
    Bytes acc;
    const std::uint32_t blocks = static_cast<std::uint32_t>((key_len + hash_len - 1) / hash_len);
    out.reserve(static_cast<std::size_t>(blocks) * hash_len);

    for (std::uint32_t index = 1; index <= blocks; ++index) {
        put_be32(block_input.data() + salt.size(), index);
        HashHandle hash;
        status = BCryptCreateHash(alg.handle, &hash.handle,
                                  object.empty() ? nullptr : object.data(), object_len,
                                  const_cast<PUCHAR>(safe_ptr(password)),
                                  static_cast<ULONG>(password.size()), 0);
        if (!BCRYPT_SUCCESS(status)) {
            error = nt_failure("BCryptCreateHash(PBKDF2)", status);
            return false;
        }
        status = BCryptHashData(hash.handle, block_input.data(),
                                static_cast<ULONG>(block_input.size()), 0);
        if (!BCRYPT_SUCCESS(status)) {
            error = nt_failure("BCryptHashData(PBKDF2 primeira rodada)", status);
            return false;
        }
        u.assign(hash_len, 0);
        status = BCryptFinishHash(hash.handle, u.data(), hash_len, 0);
        if (!BCRYPT_SUCCESS(status)) {
            error = nt_failure("BCryptFinishHash(PBKDF2 primeira rodada)", status);
            return false;
        }
        acc = u;

        for (std::uint32_t round = 1; round < iterations; ++round) {
            HashHandle inner;
            status = BCryptCreateHash(alg.handle, &inner.handle,
                                      object.empty() ? nullptr : object.data(), object_len,
                                      const_cast<PUCHAR>(safe_ptr(password)),
                                      static_cast<ULONG>(password.size()), 0);
            if (!BCRYPT_SUCCESS(status)) {
                error = nt_failure("BCryptCreateHash(PBKDF2)", status);
                return false;
            }
            status = BCryptHashData(inner.handle, u.data(), static_cast<ULONG>(u.size()), 0);
            if (!BCRYPT_SUCCESS(status)) {
                error = nt_failure("BCryptHashData(PBKDF2)", status);
                return false;
            }
            status = BCryptFinishHash(inner.handle, u.data(), hash_len, 0);
            if (!BCRYPT_SUCCESS(status)) {
                error = nt_failure("BCryptFinishHash(PBKDF2)", status);
                return false;
            }
            for (DWORD i = 0; i < hash_len; ++i) acc[i] ^= u[i];
        }
        out.insert(out.end(), acc.begin(), acc.end());
    }
    out.resize(key_len);
    return true;
}

// ------------------------------------------------------------------ AES
bool aes_ecb_encrypt_block(const Bytes& key, const std::uint8_t in[16],
                           std::uint8_t out[16], std::string& error) {
    AesKey aes;
    if (!aes.init(key, BCRYPT_CHAIN_MODE_ECB, error)) return false;
    return aes.encrypt(in, 16, nullptr, out, error);
}

bool aes_ecb_decrypt_block(const Bytes& key, const std::uint8_t in[16],
                           std::uint8_t out[16], std::string& error) {
    AesKey aes;
    if (!aes.init(key, BCRYPT_CHAIN_MODE_ECB, error)) return false;
    return aes.decrypt(in, 16, nullptr, out, error);
}

bool aes_cbc_encrypt(const Bytes& key, const std::uint8_t iv[16], const Bytes& in,
                     Bytes& out, std::string& error) {
    out.clear();
    if (in.size() % 16 != 0) {
        error = "AES-CBC: entrada nao e multipla de 16 bytes";
        return false;
    }
    if (in.empty()) return true;
    if (!fits_ulong(in.size())) {
        error = "AES-CBC: entrada grande demais";
        return false;
    }
    AesKey aes;
    if (!aes.init(key, BCRYPT_CHAIN_MODE_CBC, error)) return false;
    out.assign(in.size(), 0);
    return aes.encrypt(in.data(), static_cast<std::uint32_t>(in.size()), iv, out.data(), error);
}

bool aes_cbc_decrypt(const Bytes& key, const std::uint8_t iv[16], const Bytes& in,
                     Bytes& out, std::string& error) {
    out.clear();
    if (in.size() % 16 != 0) {
        error = "AES-CBC: entrada nao e multipla de 16 bytes";
        return false;
    }
    if (in.empty()) return true;
    if (!fits_ulong(in.size())) {
        error = "AES-CBC: entrada grande demais";
        return false;
    }
    AesKey aes;
    if (!aes.init(key, BCRYPT_CHAIN_MODE_CBC, error)) return false;
    out.assign(in.size(), 0);
    return aes.decrypt(in.data(), static_cast<std::uint32_t>(in.size()), iv, out.data(), error);
}

// ------------------------------------------------------------------ RFC 3394
bool aes_key_wrap(const Bytes& kek, const Bytes& plaintext, Bytes& wrapped,
                  std::string& error) {
    wrapped.clear();
    if (plaintext.size() < 16 || plaintext.size() % 8 != 0) {
        error = "AES-KW: dado precisa de ao menos 2 blocos de 8 bytes";
        return false;
    }
    AesKey aes;
    if (!aes.init(kek, BCRYPT_CHAIN_MODE_ECB, error)) return false;

    const std::size_t blocks = plaintext.size() / 8;
    Bytes r(plaintext);
    std::uint8_t a[8];
    std::memcpy(a, kKeyWrapIv, 8);
    std::uint8_t in[16];
    std::uint8_t out[16];

    for (int j = 0; j < 6; ++j) {
        for (std::size_t i = 1; i <= blocks; ++i) {
            std::memcpy(in, a, 8);
            std::memcpy(in + 8, r.data() + (i - 1) * 8, 8);
            if (!aes.encrypt(in, 16, nullptr, out, error)) return false;
            const std::uint64_t t = static_cast<std::uint64_t>(blocks) * static_cast<std::uint64_t>(j) + i;
            std::memcpy(a, out, 8);
            for (int k = 0; k < 8; ++k) {
                a[7 - k] ^= static_cast<std::uint8_t>((t >> (8 * k)) & 0xFF);
            }
            std::memcpy(r.data() + (i - 1) * 8, out + 8, 8);
        }
    }

    wrapped.assign(a, a + 8);
    wrapped.insert(wrapped.end(), r.begin(), r.end());
    return true;
}

bool aes_key_unwrap(const Bytes& kek, const Bytes& wrapped, Bytes& plaintext,
                    std::string& error) {
    plaintext.clear();
    if (wrapped.size() < 24 || wrapped.size() % 8 != 0) {
        error = "AES-KW: dado embrulhado com tamanho invalido";
        return false;
    }
    AesKey aes;
    if (!aes.init(kek, BCRYPT_CHAIN_MODE_ECB, error)) return false;

    const std::size_t blocks = wrapped.size() / 8 - 1;
    std::uint8_t a[8];
    std::memcpy(a, wrapped.data(), 8);
    Bytes r(wrapped.begin() + 8, wrapped.end());
    std::uint8_t in[16];
    std::uint8_t out[16];

    for (int j = 5; j >= 0; --j) {
        for (std::size_t i = blocks; i >= 1; --i) {
            const std::uint64_t t = static_cast<std::uint64_t>(blocks) * static_cast<std::uint64_t>(j) + i;
            for (int k = 0; k < 8; ++k) {
                in[7 - k] = static_cast<std::uint8_t>(a[7 - k] ^ ((t >> (8 * k)) & 0xFF));
            }
            std::memcpy(in + 8, r.data() + (i - 1) * 8, 8);
            if (!aes.decrypt(in, 16, nullptr, out, error)) return false;
            std::memcpy(a, out, 8);
            std::memcpy(r.data() + (i - 1) * 8, out + 8, 8);
        }
    }

    if (std::memcmp(a, kKeyWrapIv, 8) != 0) {
        error = "AES-KW: integrity check falhou (chave errada ou dado corrompido)";
        return false;
    }
    plaintext = std::move(r);
    return true;
}

bool pkcs7_pad(Bytes& data, std::size_t block) {
    if (block == 0 || block > 255) return false;
    const std::size_t padding = block - (data.size() % block);
    data.insert(data.end(), padding, static_cast<std::uint8_t>(padding));
    return true;
}

bool pkcs7_unpad(Bytes& data, std::size_t block) {
    if (block == 0 || data.empty() || data.size() % block != 0) return false;
    const std::size_t padding = data.back();
    if (padding == 0 || padding > block || padding > data.size()) return false;
    for (std::size_t i = 0; i < padding; ++i) {
        if (data[data.size() - 1 - i] != static_cast<std::uint8_t>(padding)) return false;
    }
    data.resize(data.size() - padding);
    return true;
}

// ------------------------------------------------------------------ keybag
const KeybagBlock* Keybag::block(const char* tag) const {
    for (const KeybagBlock& item : blocks) {
        if (item.tag == tag) return &item;
    }
    return nullptr;
}

const KeybagClass* Keybag::protection_class(std::uint32_t class_id) const {
    for (const KeybagClass& item : classes) {
        if (item.class_id == class_id) return &item;
    }
    return nullptr;
}

KeybagClass* Keybag::protection_class(std::uint32_t class_id) {
    for (KeybagClass& item : classes) {
        if (item.class_id == class_id) return &item;
    }
    return nullptr;
}

bool keybag_parse(const Bytes& blob, Keybag& out, std::string& error) {
    out = Keybag{};
    out.raw = blob;

    std::size_t pos = 0;
    bool inside_class = false;
    KeybagClass current;

    while (pos + 8 <= blob.size()) {
        KeybagBlock item;
        item.tag.assign(reinterpret_cast<const char*>(blob.data() + pos), 4);
        const std::uint32_t length = read_be32(blob.data() + pos + 4);
        if (length > blob.size() - pos - 8) {
            error = "bloco " + item.tag + " declara " + std::to_string(length) +
                    " bytes mas so restam " + std::to_string(blob.size() - pos - 8);
            return false;
        }
        item.tag_offset = pos;
        item.data_offset = pos + 8;
        item.data.assign(blob.begin() + static_cast<std::ptrdiff_t>(item.data_offset),
                         blob.begin() + static_cast<std::ptrdiff_t>(item.data_offset + length));
        pos = item.data_offset + length;

        const std::string& tag = item.tag;
        if (tag == "CLAS") {
            if (inside_class && !current.wrapped_key.empty()) out.classes.push_back(current);
            current = KeybagClass{};
            inside_class = true;
            if (item.data.size() >= 4) current.class_id = read_be32(item.data.data());
            else if (item.data.size() == 1) current.class_id = item.data[0];
            out.blocks.push_back(item);
            continue;
        }
        if (tag == "END") {
            out.blocks.push_back(item);
            break;
        }
        if (inside_class) {
            if (tag == "WRAP") {
                current.wrap_type = item.data.size() >= 4 ? read_be32(item.data.data()) : 0;
            } else if (tag == "KTYP") {
                current.key_type = item.data.size() >= 4 ? read_be32(item.data.data()) : 0;
            } else if (tag == "WPKY") {
                current.wrapped_key = item.data;
            } else if (tag == "PBKY") {
                current.public_key = item.data;
            }
            out.blocks.push_back(item);
            continue;
        }

        if (tag == "VERS") {
            out.version = item.data.size() >= 4 ? read_be32(item.data.data()) : 0;
        } else if (tag == "TYPE") {
            out.type = item.data.size() >= 4 ? read_be32(item.data.data()) : 0;
        } else if (tag == "UUID") {
            out.uuid = item.data;
        } else if (tag == "HMCK") {
            out.hmac = item.data;
        } else if (tag == "WRAP") {
            out.wrap_type = item.data.size() >= 4 ? read_be32(item.data.data()) : 0;
        } else if (tag == "SALT") {
            out.salt = item.data;
        } else if (tag == "ITER") {
            out.iterations = item.data.size() >= 4 ? read_be32(item.data.data()) : 0;
        }
        out.blocks.push_back(item);
    }

    if (inside_class && !current.wrapped_key.empty()) out.classes.push_back(current);

    if (out.blocks.empty()) {
        error = "blob de keybag vazio ou sem blocos TLV reconheciveis";
        return false;
    }
    if (out.salt.empty()) {
        error = "keybag sem bloco SALT";
        return false;
    }
    if (out.iterations == 0) {
        error = "keybag sem ITER valido";
        return false;
    }
    if (out.classes.empty()) {
        error = "keybag sem blocos de classe de protecao (CLAS)";
        return false;
    }
    return true;
}

bool keybag_unlock(Keybag& keybag, const std::string& passcode, Bytes& passcode_key,
                   std::string& error) {
    passcode_key.clear();
    if (keybag.salt.empty()) {
        error = "keybag sem SALT: impossivel derivar a passcode key";
        return false;
    }
    if (keybag.iterations == 0) {
        error = "keybag com ITER=0: impossivel derivar a passcode key";
        return false;
    }

    const Bytes password(passcode.begin(), passcode.end());
    if (!pbkdf2_hmac_sha256(password, keybag.salt, keybag.iterations, 32, passcode_key,
                            error)) {
        return false;
    }

    std::size_t unlocked = 0;
    for (KeybagClass& item : keybag.classes) {
        item.class_key.clear();
        if (item.wrapped_key.empty()) continue;
        Bytes candidate;
        std::string ignored;
        if (aes_key_unwrap(passcode_key, item.wrapped_key, candidate, ignored) &&
            candidate.size() >= 16) {
            item.class_key = std::move(candidate);
            ++unlocked;
        }
    }
    if (unlocked == 0) {
        error = "nenhuma classe de protecao foi desembrulhada: senha incorreta";
        return false;
    }
    return true;
}

bool keybag_verify_hmac(const Keybag& keybag, const Bytes& passcode_key,
                        int& convention_index, std::string& error) {
    convention_index = -1;
    const KeybagBlock* hmac_block = keybag.block("HMCK");
    if (hmac_block == nullptr || hmac_block->data.empty()) {
        error = "keybag sem bloco HMCK";
        return false;
    }
    if (keybag.raw.size() < hmac_block->data_offset + hmac_block->data.size()) {
        error = "blob do keybag nao contem o HMCK completo";
        return false;
    }

    const std::size_t range_before_tag = hmac_block->tag_offset;
    const std::size_t range_through_data = hmac_block->data_offset + hmac_block->data.size();
    const std::size_t ranges[2] = {range_before_tag, range_through_data};

    for (int variant = 0; variant < 4; ++variant) {
        const std::size_t range = ranges[variant % 2];
        Bytes slice(keybag.raw.begin(), keybag.raw.begin() + static_cast<std::ptrdiff_t>(range));
        Bytes mac;
        std::string ignored;
        const bool ok = (variant < 2) ? hmac_sha1(passcode_key, slice, mac, ignored)
                                      : hmac_sha256(passcode_key, slice, mac, ignored);
        if (!ok) continue;
        if (mac.size() == hmac_block->data.size() &&
            std::memcmp(mac.data(), hmac_block->data.data(), mac.size()) == 0) {
            convention_index = variant;
            return true;
        }
    }
    error = "HMCK nao confere em nenhuma das convencoes conhecidas";
    return false;
}

} // namespace olho::ios
