// language: C++, file: case_crypto.hpp, target: Windows 11 (MSVC / MinGW g++)
// Encryption at rest for the case database.
//
// WHY CTR AND NOT CBC
//   The container is a live SQLite file. SQLite reads and writes at arbitrary
//   offsets, and it also spills pages into a journal and a WAL. A cipher that
//   needs block alignment or per-page bookkeeping would have to special-case all
//   three. AES-CTR is seekable: the keystream position IS the file offset, so the
//   same code encrypts the database, the journal and the WAL with no idea what
//   they contain. That single property is why this works at all.
//
// WHAT THIS DOES NOT GIVE
//   CTR provides confidentiality, not authentication. A flipped byte in the file
//   corrupts that byte on read and is NOT detected by this layer; SQLite's own
//   page checks and the report's SHA-256 are what catch corruption. Saying this
//   plainly matters more than adding a MAC that nobody would verify.
//
// KEY HANDLING
//   A random 256-bit key per case, wrapped with DPAPI (CryptProtectData) and
//   stored beside the database. DPAPI binds it to the Windows account, so a
//   copied case directory does not open on another machine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace deep {

using crypto_bytes = std::vector<std::uint8_t>;

class case_cipher {
public:
    case_cipher() = default;
    ~case_cipher();
    case_cipher(const case_cipher&) = delete;
    case_cipher& operator=(const case_cipher&) = delete;

    // Creates a fresh key file (DPAPI-wrapped) when none exists, or loads the
    // existing one. Returns false with a reason if the key cannot be unsealed.
    bool open_or_create(const std::wstring& key_path, std::string& error);

    // Loads only; used when opening an existing case for reading.
    bool load(const std::wstring& key_path, std::string& error);

    bool ready() const { return ready_; }

    // XORs the buffer with the keystream for file offset. Symmetric: the same call
    // encrypts and decrypts. Safe at any offset, including unaligned ones.
    bool apply(std::uint64_t offset, std::uint8_t* data, std::size_t len) const;

    // For tests and diagnostics: hex of the key is never exposed; this returns the
    // first bytes of the keystream at offset 0 instead.
    std::string keystream_probe() const;

private:
    void* algorithm_ = nullptr;   // BCRYPT_ALG_HANDLE
    void* raw_key_ = nullptr;     // BCRYPT_KEY_HANDLE
    bool ready_ = false;
    crypto_bytes key_;
};

}  // namespace deep
