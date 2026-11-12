// language: C++, file: wa_sqlcipher.hpp, target: Windows 11 (MSVC / MinGW g++)
// SQLCipher acquisition core: parameter detection, full-database decryption and
// WAL replay. Links: bcrypt only (no OpenSSL, no libsodium).
//
// Why a probe and not a constant: WhatsApp ships different SQLCipher builds per
// channel (stable/beta/business) and upgrades them in place. Page size, KDF
// iterations and HMAC algorithm all move. Hardcoding one triple means the tool
// silently fails on the next release. The HMAC tag on page 1 is a deterministic
// oracle: exactly one configuration verifies it. Detect, then prove by use.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::wa {

// HMAC / KDF digest selection. Values index a table, not a CNG handle.
enum class digest : int { sha1 = 0, sha256 = 1, sha512 = 2 };

struct cipher_params {
    std::uint32_t page_size = 4096;
    std::uint32_t reserve = 48;   // bytes at page end holding IV + HMAC
    std::uint32_t kdf_iter = 64000;
    digest hmac_alg = digest::sha1;
    digest kdf_alg = digest::sha1;
    // SQLCipher 1.x/2.x did not store an IV for page 1; it reused the salt.
    // 3.x+ always stores one. The oracle decides which.
    bool salt_as_iv_page1 = false;

    std::uint32_t iv_size() const { return 16; }
    std::uint32_t mac_size() const;
    std::string describe() const;
};

// Either a 32-byte raw key (WhatsApp's files/key on a rooted or FFS acquisition)
// or a passphrase. Raw key is the fast path: it skips the 64000-round KDF.
struct key_material {
    std::vector<std::uint8_t> raw_key;
    std::string passphrase;
    bool empty() const { return raw_key.empty() && passphrase.empty(); }
};

struct open_result {
    bool ok = false;
    cipher_params params;
    std::string detail;
};

// Scan candidate configurations against the real file until the page-1 HMAC
// verifies. Cheap: page 1 plus one data page, no full read.
open_result detect(const std::wstring& db_path, const key_material& key);

struct decrypt_stats {
    std::uint64_t pages = 0;
    std::uint64_t wal_frames = 0;
    std::uint64_t wal_discarded = 0;   // frames de transacao incompleta: nao aplicados
    bool wal_present = false;          // existia -wal ao lado do banco
    std::uint64_t bytes_in = 0;
    std::uint64_t bytes_out = 0;
};

// Decrypt the database into a plaintext SQLite image that vanilla sqlite3 can
// open read-only. WAL frames are replayed in commit order when present: that is
// where a large share of recently deleted messages live, because a checkpoint
// has not overwritten them yet.
bool decrypt_database(const std::wstring& db_path,
                      const std::wstring& out_path,
                      const cipher_params& params,
                      const key_material& key,
                      decrypt_stats& stats,
                      std::string& error);

}  // namespace olho::wa
