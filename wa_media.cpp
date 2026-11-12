// language: C++, file: wa_media.cpp, target: Windows 11 (MSVC / MinGW g++)
// WhatsApp media recovery. See wa_media.hpp for the design rationale.
#include "wa_media.hpp"
#include "wa_crypto.hpp"

#include <windows.h>
#include <sqlite3.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace olho::wa {
namespace {

// Container signatures. A wrong key produces noise, and noise carries no magic
// number, so this doubles as the verification oracle for decryption.
struct signature {
    const char* type;
    const char* extension;
    std::size_t offset;
    std::vector<std::uint8_t> magic;
};

const std::vector<signature>& signatures() {
    static const std::vector<signature> table = {
        {"jpeg", "jpg", 0, {0xFF, 0xD8, 0xFF}},
        {"png", "png", 0, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}},
        {"gif", "gif", 0, {'G', 'I', 'F', '8'}},
        {"webp", "webp", 0, {'R', 'I', 'F', 'F'}},
        {"mp4", "mp4", 4, {'f', 't', 'y', 'p'}},
        {"ogg", "ogg", 0, {'O', 'g', 'g', 'S'}},
        {"pdf", "pdf", 0, {'%', 'P', 'D', 'F'}},
        {"amr", "amr", 0, {'#', '!', 'A', 'M', 'R'}},
        {"zip", "zip", 0, {'P', 'K', 0x03, 0x04}},
        {"3gp", "3gp", 4, {'f', 't', 'y', 'p'}},
        {"m4a", "m4a", 4, {'f', 't', 'y', 'p'}},
    };
    return table;
}

bool file_exists(const std::wstring& p) {
    const DWORD attr = GetFileAttributesW(p.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool read_file(const std::wstring& path, std::vector<std::uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart <= 0 || size.QuadPart > (512ll << 20)) { CloseHandle(h); return false; }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD got = 0;
    const BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != out.size()) { out.clear(); return false; }
    return true;
}

bool write_file(const std::wstring& path, const std::vector<std::uint8_t>& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const BOOL ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &put, nullptr);
    CloseHandle(h);
    return ok && put == bytes.size();
}

std::string utf8(const unsigned char* p) {
    return p ? std::string(reinterpret_cast<const char*>(p)) : std::string();
}

std::vector<std::string> table_columns(sqlite3* db, const std::string& table) {
    std::vector<std::string> cols;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "PRAGMA table_info(\"" + table + "\")";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return cols;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name) cols.emplace_back(reinterpret_cast<const char*>(name));
    }
    sqlite3_finalize(stmt);
    return cols;
}

// Column names differ between releases and between the Business / GB forks.
// Match a family of spellings instead of one literal.
bool is_key_column(const std::string& c) {
    return c == "media_key" || c == "file_key" || c == "media_crypt_key" ||
           c == "encryption_key" || c == "file_hash" || c == "media_hash";
}

std::string pick_path_column(const std::vector<std::string>& cols) {
    for (const char* want : {"file_path", "media_name", "file_name", "media_url", "media_path"}) {
        if (std::find(cols.begin(), cols.end(), want) != cols.end()) return want;
    }
    return {};
}

std::string pick_view_once_column(const std::vector<std::string>& cols) {
    for (const char* want : {"view_once", "message_view_once", "is_view_once", "view_once_flag"}) {
        if (std::find(cols.begin(), cols.end(), want) != cols.end()) return want;
    }
    return {};
}

std::string pick_mime_column(const std::vector<std::string>& cols) {
    for (const char* want : {"media_mime_type", "mime_type", "file_mime_type", "media_wa_type_str"}) {
        if (std::find(cols.begin(), cols.end(), want) != cols.end()) return want;
    }
    return {};
}

}  // namespace

std::string sniff_container(const std::uint8_t* data, std::size_t len) {
    if (!data) return {};
    for (const auto& sig : signatures()) {
        if (len < sig.offset + sig.magic.size()) continue;
        if (std::memcmp(data + sig.offset, sig.magic.data(), sig.magic.size()) == 0) {
            // WebP and the ISO-BMFF family share a prefix; separate them.
            if (std::strcmp(sig.type, "webp") == 0) {
                if (len < 12 || std::memcmp(data + 8, "WEBP", 4) != 0) continue;
            }
            if (std::strcmp(sig.type, "mp4") == 0 && len >= 12) {
                if (std::memcmp(data + 8, "heic", 4) == 0 || std::memcmp(data + 8, "heix", 4) == 0) return "heic";
                if (std::memcmp(data + 8, "qt  ", 4) == 0) return "mov";
            }
            return sig.type;
        }
    }
    return {};
}

std::string extension_for(const std::string& type) {
    for (const auto& sig : signatures()) {
        if (type == sig.type) return sig.extension;
    }
    if (type == "heic") return "heic";
    if (type == "mov") return "mov";
    return "bin";
}

discovery discover_media(const std::wstring& plain_db) {
    discovery result;

    const std::wstring narrow_path = plain_db;
    const std::string path_utf8(narrow_path.begin(), narrow_path.end());

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path_utf8.c_str(), &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        result.detail = "nao abriu o banco recuperado";
        if (db) sqlite3_close(db);
        return result;
    }

    std::vector<std::string> tables;
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table'", -1,
                               &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(stmt, 0);
                if (name) tables.emplace_back(reinterpret_cast<const char*>(name));
            }
            sqlite3_finalize(stmt);
        }
    }

    for (const std::string& table : tables) {
        if (table.find("view_once") != std::string::npos) result.view_once_table = true;
        const auto cols = table_columns(db, table);
        std::string key_col;
        for (const auto& c : cols) {
            if (is_key_column(c)) { key_col = c; break; }
        }
        if (key_col.empty()) continue;

        const std::string path_col = pick_path_column(cols);
        const std::string mime_col = pick_mime_column(cols);
        const std::string once_col = pick_view_once_column(cols);

        std::string sql = "SELECT \"" + key_col + "\"";
        sql += path_col.empty() ? ", NULL" : ", \"" + path_col + "\"";
        sql += mime_col.empty() ? ", NULL" : ", \"" + mime_col + "\"";
        sql += once_col.empty() ? ", NULL" : ", \"" + once_col + "\"";
        sql += " FROM \"" + table + "\" WHERE \"" + key_col + "\" IS NOT NULL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) continue;

        int seen = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && seen < 200000) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            const int blob_len = sqlite3_column_bytes(stmt, 0);
            if (!blob || blob_len < 16) continue;

            media_row row;
            row.table = table;
            row.key_column = key_col;
            row.path_hint = utf8(sqlite3_column_text(stmt, 1));
            row.mime_hint = utf8(sqlite3_column_text(stmt, 2));
            row.view_once = sqlite3_column_type(stmt, 3) != SQLITE_NULL &&
                            sqlite3_column_int(stmt, 3) != 0;
            if (row.view_once) ++result.view_once_rows;
            const auto* p = static_cast<const std::uint8_t*>(blob);
            row.key.assign(p, p + blob_len);
            result.rows.push_back(std::move(row));
            ++seen;
        }
        sqlite3_finalize(stmt);
        if (seen > 0) result.scanned_tables.push_back(table + " (" + std::to_string(seen) + ")");
    }

    sqlite3_close(db);
    result.ok = true;
    result.detail = std::to_string(result.rows.size()) + " linhas de midia em " +
                    std::to_string(result.scanned_tables.size()) + " tabelas";
    return result;
}

media_result decrypt_media(const std::wstring& enc_path,
                           const std::wstring& out_path,
                           const std::vector<std::uint8_t>& media_key) {
    media_result result;

    if (media_key.size() < 16) { result.detail = "chave de midia curta demais"; return result; }
    if (!file_exists(enc_path)) { result.detail = "arquivo .enc ausente"; return result; }

    std::vector<std::uint8_t> raw;
    if (!read_file(enc_path, raw)) { result.detail = "nao leu o .enc"; return result; }
    if (raw.size() < 32) { result.detail = ".enc truncado"; return result; }

    // Layout candidates. WhatsApp has shipped all three across app generations.
    struct scheme {
        const char* name;
        bool iv_from_file;
        std::size_t key_len;
    };
    const scheme schemes[] = {
        {"iv-prefixo,key32", true, 32},
        {"iv-na-chave,key32", false, 32},
        {"iv-prefixo,key16", true, 16},
    };

    for (const auto& s : schemes) {
        if (media_key.size() < s.key_len) continue;

        std::uint8_t iv[16] = {0};
        std::size_t data_off = 0;
        if (s.iv_from_file) {
            std::memcpy(iv, raw.data(), 16);
            data_off = 16;
        } else {
            if (media_key.size() >= s.key_len + 16) {
                std::memcpy(iv, media_key.data() + s.key_len, 16);
            } else {
                continue;
            }
            data_off = 0;
        }

        const std::size_t ct_len = raw.size() - data_off;
        const std::size_t usable = ct_len - (ct_len % 16);
        if (usable < 32) continue;

        std::vector<std::uint8_t> plain(raw.begin() + data_off, raw.begin() + data_off + usable);
        crypto::aes_cbc aes(media_key.data(), s.key_len);
        if (!aes.ok()) { result.detail = "CNG recusou a chave"; return result; }
        if (!aes.decrypt_update(plain.data(), plain.size(), iv)) continue;

        const std::string type = sniff_container(plain.data(), plain.size());
        if (type.empty()) continue;   // wrong key: noise has no magic

        if (!write_file(out_path, plain)) { result.detail = "nao gravou a saida"; return result; }

        result.ok = true;
        result.scheme = s.name;
        result.media_type = type;
        result.extension = extension_for(type);
        result.bytes = plain.size();
        result.detail = "verificado por assinatura de container";
        return result;
    }

    result.detail = "nenhum esquema produziu um container reconhecivel";
    return result;
}

}  // namespace olho::wa
