// language: C++, file: wa_media.hpp, target: Windows 11 (MSVC / MinGW g++)
// WhatsApp media recovery: discovers media rows in a decrypted msgstore, then
// decrypts the .enc payloads they point at. Links: bcrypt, sqlite3.
//
// The .enc container is AES-CBC with a per-file key stored inside the message
// database. The IV placement has changed across app versions, so this never
// assumes one layout: it tries the known schemes and accepts only the one whose
// plaintext carries a real container signature. A wrong key produces noise, and
// noise has no magic number. That check is the oracle.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::wa {

struct media_row {
    std::string table;            // where it was found
    std::string key_column;       // media_key / file_key / ...
    std::string path_hint;        // file_path / media_name / file_name, may be empty
    std::string mime_hint;        // media_mime_type / mime_type, may be empty
    std::vector<std::uint8_t> key;
    // Midia efemera ("ver uma vez"): o aplicativo apaga o arquivo depois da
    // visualizacao, entao a linha pode ser a unica prova de que ela existiu.
    bool view_once = false;
};

// Walks every table in the decrypted database and collects rows that carry a
// media key. Schema is discovered, not assumed: WhatsApp renames these columns
// between releases and across the Business/GB variants.
struct discovery {
    bool ok = false;
    std::vector<media_row> rows;
    std::vector<std::string> scanned_tables;
    int view_once_rows = 0;        // linhas de midia marcadas como "ver uma vez"
    bool view_once_table = false;  // existe tabela dedicada a midia efemera
    std::string detail;
};

discovery discover_media(const std::wstring& plain_db);

struct media_result {
    bool ok = false;
    std::string scheme;        // which layout verified
    std::string media_type;    // jpeg / png / mp4 / ...
    std::string extension;     // suggested output extension
    std::uint64_t bytes = 0;
    std::string detail;
};

// Decrypt one .enc file. Output path is written only when a container signature
// is recognised, so a failed key never leaves a plausible-looking fragment in
// the evidence set.
media_result decrypt_media(const std::wstring& enc_path,
                           const std::wstring& out_path,
                           const std::vector<std::uint8_t>& media_key);

// Best-effort type probe on a plaintext buffer. Exposed for the gallery UI and
// for carving unlabelled media out of unallocated space.
std::string sniff_container(const std::uint8_t* data, std::size_t len);

}  // namespace olho::wa
