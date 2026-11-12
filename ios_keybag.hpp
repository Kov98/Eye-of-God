#pragma once

// Aquisicao iOS: primitivas criptograficas (CNG/bcrypt) e parser do BackupKeyBag.
//
// O keybag de um backup cifrado vive no Manifest.plist e e um blob de blocos TLV:
// tag ASCII de 4 bytes, tamanho big-endian u32, dados. A cadeia de chaves e
//   senha -> PBKDF2-HMAC-SHA256(SALT, ITER) -> passcode key
//   passcode key --AES-KW--> WPKY da classe de protecao -> class key
//   class key    --AES-KW--> WRAP por arquivo -> file key

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace olho::ios {

using Bytes = std::vector<std::uint8_t>;

// ------------------------------------------------------------------ bytes/hex
std::string bytes_to_hex(const Bytes& value);
bool bytes_from_hex(const std::string& text, Bytes& out);
std::uint32_t read_be32(const std::uint8_t* data);
std::uint64_t read_be64(const std::uint8_t* data);

// ------------------------------------------------------------------ digests
bool sha1(const Bytes& data, Bytes& digest, std::string& error);
bool sha256(const Bytes& data, Bytes& digest, std::string& error);
bool hmac_sha1(const Bytes& key, const Bytes& data, Bytes& mac, std::string& error);
bool hmac_sha256(const Bytes& key, const Bytes& data, Bytes& mac, std::string& error);

bool pbkdf2_hmac_sha256(const Bytes& password, const Bytes& salt,
                        std::uint32_t iterations, std::size_t key_len,
                        Bytes& out, std::string& error);

// Hash incremental: backup real tem arquivos grandes demais para caber em memoria.
class Sha256Stream {
public:
    Sha256Stream();
    ~Sha256Stream();
    Sha256Stream(const Sha256Stream&) = delete;
    Sha256Stream& operator=(const Sha256Stream&) = delete;

    bool init(std::string& error);
    bool update(const std::uint8_t* data, std::size_t size, std::string& error);
    bool finish(Bytes& digest, std::string& error);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// ------------------------------------------------------------------ AES
bool aes_ecb_encrypt_block(const Bytes& key, const std::uint8_t in[16],
                           std::uint8_t out[16], std::string& error);
bool aes_ecb_decrypt_block(const Bytes& key, const std::uint8_t in[16],
                           std::uint8_t out[16], std::string& error);

// Sem padding: o chamador decide (backup iOS usa PKCS#7, o keybag usa blocos puros).
bool aes_cbc_encrypt(const Bytes& key, const std::uint8_t iv[16],
                     const Bytes& in, Bytes& out, std::string& error);
bool aes_cbc_decrypt(const Bytes& key, const std::uint8_t iv[16],
                     const Bytes& in, Bytes& out, std::string& error);

// ------------------------------------------------------------------ RFC 3394
// O CNG nao expoe key wrap; o algoritmo e montado aqui sobre BCrypt em modo ECB,
// com o IV padrao A6A6A6A6A6A6A6A6 e o XOR do contador t = n*j+i.
bool aes_key_wrap(const Bytes& kek, const Bytes& plaintext, Bytes& wrapped,
                  std::string& error);
bool aes_key_unwrap(const Bytes& kek, const Bytes& wrapped, Bytes& plaintext,
                    std::string& error);

bool pkcs7_pad(Bytes& data, std::size_t block);
bool pkcs7_unpad(Bytes& data, std::size_t block);

// ------------------------------------------------------------------ keybag
struct KeybagBlock {
    std::string tag;
    Bytes data;
    std::size_t tag_offset = 0;   // posicao no blob original, usada pela verificacao HMCK
    std::size_t data_offset = 0;
};

struct KeybagClass {
    std::uint32_t class_id = 0;
    std::uint32_t wrap_type = 0;
    std::uint32_t key_type = 0;
    Bytes wrapped_key;            // WPKY
    Bytes public_key;             // PBKY
    Bytes class_key;              // preenchida por keybag_unlock
};

struct Keybag {
    std::uint32_t version = 0;
    std::uint32_t type = 0;
    Bytes uuid;
    Bytes hmac;                   // HMCK
    std::uint32_t wrap_type = 0;
    Bytes salt;
    std::uint32_t iterations = 0;
    std::vector<KeybagBlock> blocks;
    std::vector<KeybagClass> classes;
    Bytes raw;                    // blob original, necessario para conferir o HMCK

    const KeybagBlock* block(const char* tag) const;
    const KeybagClass* protection_class(std::uint32_t class_id) const;
    KeybagClass* protection_class(std::uint32_t class_id);
};

bool keybag_parse(const Bytes& blob, Keybag& out, std::string& error);

// Deriva a passcode key e tenta desembrulhar o WPKY de cada classe. A deteccao de
// senha errada vem do integrity check do AES-KW (IV A6A6...), nao de heuristica.
bool keybag_unlock(Keybag& keybag, const std::string& passcode, Bytes& passcode_key,
                   std::string& error);

// O intervalo coberto pelo HMCK nao e identico em todas as implementacoes de
// referencia que existem por ai. Testamos as convencoes conhecidas e reportamos
// qual (se alguma) fechou; a verificacao e informativa e nunca bloqueia o unlock.
bool keybag_verify_hmac(const Keybag& keybag, const Bytes& passcode_key,
                        int& convention_index, std::string& error);

} // namespace olho::ios
