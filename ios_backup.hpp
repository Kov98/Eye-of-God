#pragma once

// Aquisicao iOS: leitor de backup do Finder/iTunes.
//
// Estrutura coberta (iOS 10+):
//   Manifest.plist  Apple binary plist, guarda BackupKeyBag e IsEncrypted
//   Manifest.db     SQLite, tabela Files (fileID, domain, relativePath, flags, file)
//   Info.plist      metadados do dispositivo
//   <2 primeiros hex do fileID>/<fileID>   conteudo do arquivo
// O layout pre-iOS 10 (Manifest.mbdb binario) nao e suportado: o erro diz isso.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ios_keybag.hpp"

namespace olho::ios {

// ------------------------------------------------------------------ plist
enum class PlistKind { Null, Bool, Integer, Real, Date, Data, String, Uid, Array, Dict, Set };

struct PlistValue {
    PlistKind kind = PlistKind::Null;
    bool boolean = false;
    std::int64_t integer = 0;
    double real = 0.0;
    std::uint64_t uid = 0;
    std::string text;                                // UTF-8 quando String
    Bytes bytes;                                     // Data
    std::vector<PlistValue> items;                   // Array / Set
    std::vector<std::pair<std::string, PlistValue>> entries;   // Dict

    const PlistValue* find(const char* key) const;
};

std::string plist_kind_name(PlistKind kind);

// Aceita apenas bplist00. O Manifest.plist dos backups e binario; XML e recusado
// com mensagem explicita em vez de silenciosamente virar um dicionario vazio.
bool plist_parse(const Bytes& blob, PlistValue& out, std::string& error);
bool plist_load(const std::string& path, PlistValue& out, std::string& error);

// ------------------------------------------------------------------ NSKeyedArchiver
// O campo Files.file do Manifest.db e um NSKeyedArchiver: $objects + $top e UIDs.
const PlistValue* nskeyed_object(const PlistValue& root, std::uint64_t uid);
const PlistValue* nskeyed_top(const PlistValue& root, const char* key);

// Desembrulha o campo de um objeto arquivado, olhando atraves de "NS.data",
// "NS.string", "NS.number" e de referencias por UID.
bool archive_bytes(const PlistValue& root, const PlistValue& field, Bytes& out);
bool archive_string(const PlistValue& root, const PlistValue& field, std::string& out);
bool archive_integer(const PlistValue& root, const PlistValue& field, std::int64_t& out);

// ------------------------------------------------------------------ backup
struct BackupEntry {
    std::string file_id;      // 40 caracteres hexadecimais
    std::string domain;       // ex.: HomeDomain, CameraRollDomain
    std::string relative_path;
    std::uint32_t flags = 0;
    std::uint64_t size = 0;   // do metadado MBFile; 0 quando ausente
    std::uint32_t protection_class = 0;
    bool metadata_ok = false;
    bool is_file = false;
    bool is_directory = false;
    bool is_symlink = false;
    std::string target;       // destino do symlink
    Bytes encryption_key;     // blob cru do campo EncryptionKey
};

struct Backup {
    std::string root;
    bool is_encrypted = false;
    bool unlocked = false;
    std::uint32_t plist_version = 0;
    std::string device_name;
    std::string product_version;
    std::uint64_t entry_count = 0;
    Keybag keybag;
    Bytes passcode_key;
    std::vector<BackupEntry> entries;
    std::vector<std::string> notes;   // avisos honestos de layout nao coberto
};

bool backup_open(const std::string& directory, Backup& out, std::string& error);
bool backup_unlock(Backup& backup, const std::string& password, std::string& error);

// Le e decripta (quando necessario) o conteudo de uma entrada.
bool backup_read_entry(Backup& backup, const BackupEntry& entry, Bytes& out,
                       std::string& error);

struct ManifestRecord {
    std::string file_id;
    std::string domain;
    std::string relative_path;
    std::string output_path;      // caminho gravado, relativo ao diretorio de saida
    std::uint64_t size = 0;
    std::uint32_t protection_class = 0;
    std::string sha256;
    std::string sha256_ciphertext;
    std::string status;           // extracted, directory, symlink, skipped, failed
};

struct ExtractResult {
    std::uint64_t entries_seen = 0;
    std::uint64_t files_written = 0;
    std::uint64_t bytes_written = 0;
    std::uint64_t directories_created = 0;
    std::uint64_t symlinks_skipped = 0;
    std::uint64_t files_skipped = 0;
    std::uint64_t files_failed = 0;
    std::vector<std::string> failures;
    std::vector<ManifestRecord> records;
};

bool backup_extract(Backup& backup, const std::string& out_dir, ExtractResult& result,
                    std::string& error);
bool backup_write_manifest_json(const Backup& backup, const ExtractResult& result,
                                const std::string& path, std::string& error);

// ------------------------------------------------------------------ utilidades
std::string sha256_hex(const Bytes& data);
bool sha256_file(const std::string& path, std::string& hex, std::uint64_t& size,
                 std::string& error);
bool read_whole_file(const std::string& path, Bytes& out, std::string& error);
bool write_whole_file(const std::string& path, const Bytes& data, std::string& error);
bool ensure_directory(const std::string& path, std::string& error);

} // namespace olho::ios
