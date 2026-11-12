// language: C++, file: android_backup.hpp, target: Windows 11 (MSVC / MinGW g++)
// Reader for the 'adb backup' archive (.ab) plus the WhatsApp case builder.
// Links: bcrypt (PBKDF2, AES, SHA-256), kernel32.
//
// The format below is not a guess: it is the layout AOSP writes, taken from
// PerformAdbBackupTask.emitAesBackupHeader (Android 10) and cross-checked against
// android-backup-extractor. Header lines, all UTF-8, each terminated by '\n':
//
//   1  "ANDROID BACKUP"
//   2  version, decimal, 1..5
//   3  compressed flag, "0" or "1"
//   4  encryption algorithm, "none" or "AES-256"
//   when 4 is not "none":
//   5  user password salt, hex (64 bytes)
//   6  master key checksum salt, hex (64 bytes)
//   7  PBKDF2 rounds, decimal (same count for user key and checksum)
//   8  IV of the user key, hex (16 bytes)
//   9  master key blob, hex: AES-CBC/PKCS7(user key, IV=line 8) over
//      [1 byte Niv][Niv bytes IV][1 byte Nmk][Nmk bytes master key]
//      [1 byte Nck][Nck bytes checksum]
//
// Payload starts immediately after line 9 (or line 4 when unencrypted):
// optionally AES-256-CBC with PKCS7 padding under the master key and the IV that
// came inside the blob, then optionally gzip, then tar.
//
// Two consequences the caller must not paper over:
//   * the password is only proven by the master key checksum, which is
//     PBKDF2(master key bytes, checksum salt, rounds). A wrong password must
//     abort before a single byte of payload is decrypted.
//   * the payload is PKCS7 padded, so the last block is padding, not data.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "wa_sqlcipher.hpp"   // olho::wa::key_material, the 32-byte raw key contract

namespace olho::android {

struct AbHeader {
    int version = 0;
    bool compressed = false;
    std::string algorithm = "none";
    bool encrypted = false;
    std::vector<std::uint8_t> user_salt;
    std::vector<std::uint8_t> checksum_salt;
    int rounds = 0;
    std::vector<std::uint8_t> user_iv;
    std::vector<std::uint8_t> master_blob;
    std::uint64_t header_bytes = 0;   // payload begins at this offset

    std::string describe() const;
};

struct MasterKey {
    std::vector<std::uint8_t> key;    // 32 bytes, AES-256
    std::vector<std::uint8_t> iv;     // 16 bytes, for the payload
    bool verified = false;            // checksum matched: the password is right
    // Which PBKDF2 byte encoding the archive was built with: "utf8" (modern JDK)
    // or "8bit" (the PBKDF2WithHmacSHA1And8bit fallback AOSP still carries).
    std::string encoding;
};

// Parses the nine header lines. Touches no key material.
bool read_ab_header(const std::wstring& ab_path, AbHeader& header, std::string& error);

// Derives the user key, unwraps the master key blob and verifies its checksum.
// A mismatch is reported as a wrong password, not as a corrupt archive.
bool unwrap_master_key(const AbHeader& header, const std::string& password, MasterKey& out,
                       std::string& error);

struct TarStats {
    std::uint64_t entries = 0;
    std::uint64_t files = 0;
    std::uint64_t dirs = 0;
    std::uint64_t links = 0;
    std::uint64_t skipped = 0;
    std::uint64_t bytes_written = 0;
    std::uint64_t bad_headers = 0;     // tar header checksum mismatch
    std::vector<std::string> rejected; // unsafe paths, reported and never written
};

struct AbStats {
    AbHeader header;
    MasterKey master;
    bool ok = false;
    std::string container;           // "zlib" (what adb actually writes) or "gzip"
    bool integrity_ok = false;       // Adler-32 (zlib) or CRC-32 (gzip) matched
    std::uint32_t checksum_expected = 0;
    std::uint32_t checksum_computed = 0;
    std::uint64_t uncompressed_bytes = 0;
    bool trailing_data = false;      // bytes after the container trailer, undecoded
    std::uint64_t payload_bytes = 0; // bytes after the header, before decryption
    TarStats tar;
    std::string error;
};

// Full pipeline: header -> (decrypt) -> (inflate) -> tar extraction.
// Writes only under out_dir, and never a path that escapes it.
bool extract_ab(const std::wstring& ab_path, const std::wstring& out_dir, const std::string& password,
                AbStats& stats);

std::string sha256_hex(const void* data, std::size_t len);
bool sha256_file(const std::wstring& path, std::string& hex, std::string& error);

// Inflate de um fluxo DEFLATE cru (RFC 1951), o que uma entrada de ZIP usa.
// Existe aqui porque o projeto nao linka zlib e este e o unico inflate do
// repositorio: o leitor de exportacao (social/) reaproveita esta funcao em vez
// de carregar uma segunda implementacao. O buffer de saida recebe o conteudo
// completo; um fluxo truncado e reportado como erro, nunca como sucesso parcial.
bool inflate_deflate(const std::uint8_t* data, std::size_t length, std::string& out,
                     std::string& error);

// ---------------------------------------------------------------------------
// WhatsApp case assembly
// ---------------------------------------------------------------------------

struct WaEntry {
    std::wstring path;
    std::uint64_t size = 0;
    std::uint64_t mtime = 0;   // last write, unix seconds; only used to rank copies
};

struct WaCase {
    bool ok = false;
    std::wstring root;                    // directory that was scanned
    WaEntry msgstore;                     // msgstore.db
    WaEntry wal;                          // msgstore.db-wal, may be empty
    WaEntry shm;                          // msgstore.db-shm, may be empty
    WaEntry key_file;                     // WhatsApp's 32-byte files/key
    std::wstring media_dir;               // WhatsApp/Media, may be empty
    std::vector<WaEntry> candidates;      // every msgstore copy seen, best first
    std::uint64_t media_files = 0;
    std::uint64_t media_bytes = 0;
    std::string detail;                   // what was found and what was missing
};

// Discovers the pieces by name, not by hardcoded path: an adb backup stores
// data/ as apps/<pkg>/db and apps/<pkg>/f, while a pulled tree keeps
// databases/ and files/, and a rooted image can be anywhere in between.
bool locate_whatsapp(const std::wstring& root, WaCase& out, std::string& error);

struct CaseStats {
    bool ok = false;
    std::wstring whatsapp_dir;
    std::wstring manifest_path;
    std::wstring command_path;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t media_files = 0;
    std::uint64_t media_bytes = 0;
    std::uint64_t hash_mismatches = 0;
    std::vector<std::string> hashes;       // "<sha256>  <relative path>"
    std::string command;                   // ready-to-run wa_recover command line
    std::string error;
};

// Copies the WhatsApp-relevant subset into <out_dir>/whatsapp, hashing every
// copy twice (source and destination) so "recovered" means byte-identical, and
// writes case_manifest.json plus wa_recover.txt. Input trees are never modified.
bool build_whatsapp_case(const std::wstring& source_root, const std::wstring& out_dir,
                         const std::string& label, CaseStats& stats);

// Fills the key_material the existing wa_recover engine expects. The file is
// WhatsApp's raw 32-byte 'key'; a longer file uses its first 32 bytes, exactly
// as wa_recover.cpp does.
bool load_wa_key(const std::wstring& key_file, olho::wa::key_material& out, std::string& error);

}  // namespace olho::android
