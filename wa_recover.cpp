// language: C++, file: wa_recover.cpp, target: Windows 11 (MSVC / MinGW g++)
// End-to-end WhatsApp acquisition from an msgstore.db and its key.
//
//   wa_recover <msgstore.db> --key <hex|path-to-key-file> --out <dir> [--media <dir>]
//
// Steps, in order, each one gated on the previous succeeding:
//   1. detect SQLCipher parameters by HMAC oracle
//   2. decrypt the database and replay its WAL into a plaintext image
//   3. decrypt every referenced .enc media file
//   4. carve rows that SQLite no longer serves
//   5. write report.json
//
// Nothing is written outside --out. Evidence directories are treated as
// read-only inputs, never modified.
#include "wa_sqlcipher.hpp"
#include "wa_media.hpp"
#include "wa_carve.hpp"

#include <windows.h>
#include <sqlite3.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

bool make_dir(const std::wstring& w) {
    if (CreateDirectoryW(w.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

// Length of a valid UTF-8 sequence starting here, or 0 when the byte is not a
// legal lead byte. Carved SQLite TEXT routinely carries raw bytes that would
// make report.json unparseable, so validity is enforced at the boundary.
std::size_t utf8_sequence(const std::string& s, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t need = 0;
    if (c < 0x80) return 1;
    else if ((c & 0xE0) == 0xC0) need = 1;
    else if ((c & 0xF0) == 0xE0) need = 2;
    else if ((c & 0xF8) == 0xF0) need = 3;
    else return 0;
    if (i + need >= s.size()) return 0;
    for (std::size_t k = 1; k <= need; ++k) {
        if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return 0;
    }
    return need + 1;
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (std::size_t i = 0; i < in.size();) {
        const std::size_t len = utf8_sequence(in, i);
        if (len == 0) { out += "\\ufffd"; ++i; continue; }   // drop the bad byte
        if (len > 1) { out.append(in, i, len); i += len; continue; }
        const unsigned char c = static_cast<unsigned char>(in[i]);
        ++i;
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::vector<std::uint8_t> load_key(const std::string& spec) {
    // Accept a raw hex key or a path to WhatsApp's 32-byte files/key blob.
    if (spec.size() >= 64 && spec.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
        std::vector<std::uint8_t> key;
        for (std::size_t i = 0; i + 1 < spec.size(); i += 2)
            key.push_back(static_cast<std::uint8_t>(std::stoi(spec.substr(i, 2), nullptr, 16)));
        return key;
    }
    std::vector<std::uint8_t> key;
    HANDLE h = CreateFileW(widen(spec).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return key;
    std::uint8_t buf[64] = {0};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf), &got, nullptr);
    CloseHandle(h);
    if (got >= 32) key.assign(buf, buf + 32);
    return key;
}

// Reads the decrypted database and writes conversations.json: live messages
// grouped by chat, plus the carved rows marked as deleted. Keeping this in the
// engine means the UI never has to know SQL, and the same JSON can feed a web
// front end later.
// Unix segundos -> ISO-8601 UTC ("2026-11-07T18:04:00Z"). O mesmo formato que os
// importadores de nuvem e de rede social escrevem, para a aba Timeline da GUI e
// o importador do caso lerem sem caso especial.
std::string iso_from_unix(const long long seconds) {
    if (seconds <= 0) return std::string();
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
    if (gmtime_s(&tm, &t) != 0) return std::string();
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) return std::string();
    return std::string(buffer);
}

std::uint64_t timeline_count = 0;   // eventos de timeline escritos (para o resumo)

void write_conversations(const std::wstring& plain_db, const std::string& out_path,
                         const std::string& timeline_path,
                         const olho::wa::carve_result& carved) {
    const std::string path(plain_db.begin(), plain_db.end());
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return;
    }

    // Locate the message table and its columns. WhatsApp renames these across
    // releases, so discovery beats a hardcoded SELECT.
    std::string table, jid_col, text_col, ts_col;
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table'", -1, &st, nullptr) == SQLITE_OK) {
            std::vector<std::string> names;
            while (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* n = sqlite3_column_text(st, 0);
                if (n) names.emplace_back(reinterpret_cast<const char*>(n));
            }
            sqlite3_finalize(st);
            for (const auto& candidate : {"message", "messages"}) {
                for (const auto& n : names) {
                    if (n == candidate) { table = n; break; }
                }
                if (!table.empty()) break;
            }
        }
        if (!table.empty()) {
            sqlite3_stmt* pc = nullptr;
            const std::string pragma = "PRAGMA table_info(\"" + table + "\")";
            if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &pc, nullptr) == SQLITE_OK) {
                std::vector<std::string> cols;
                while (sqlite3_step(pc) == SQLITE_ROW) {
                    const unsigned char* cn = sqlite3_column_text(pc, 1);
                    if (cn) cols.emplace_back(reinterpret_cast<const char*>(cn));
                }
                sqlite3_finalize(pc);
                auto pick = [&cols](std::initializer_list<const char*> wants) {
                    for (const char* w : wants)
                        for (const auto& c : cols)
                            if (c == w) return c;
                    return std::string();
                };
                jid_col = pick({"key_remote_jid", "chat_row_id", "jid", "remote_jid"});
                text_col = pick({"data", "text_data", "body", "message"});
                ts_col = pick({"timestamp", "sort_id", "time", "date"});
            }
        }
    }

    FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f) { sqlite3_close(db); return; }
    // A aba Timeline da GUI le timeline.jsonl; sem este arquivo a recuperacao de
    // WhatsApp ficava fora da linha do tempo mesmo com 243 mensagens no caso.
    // Mesmas chaves que os importadores de nuvem/social escrevem.
    FILE* tl = timeline_path.empty() ? nullptr : std::fopen(timeline_path.c_str(), "wb");
    std::uint64_t timeline_events = 0;
    std::fprintf(f, "{\n  \"conversas\": [\n");

    if (!table.empty() && !jid_col.empty() && !text_col.empty()) {
        std::string sql = "SELECT \"" + jid_col + "\", \"" + text_col + "\"";
        sql += ts_col.empty() ? ", 0" : ", \"" + ts_col + "\"";
        sql += " FROM \"" + table + "\" ORDER BY 3";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK) {
            std::string current;
            bool first_chat = true;
            while (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* jid = sqlite3_column_text(st, 0);
                const unsigned char* text = sqlite3_column_text(st, 1);
                const long long ts = sqlite3_column_int64(st, 2);
                const std::string chat = jid ? reinterpret_cast<const char*>(jid) : "";
                if (chat != current) {
                    if (!current.empty()) std::fprintf(f, "\n    ]},");
                    std::fprintf(f, "%s\n    {\"jid\": \"%s\", \"mensagens\": [", first_chat ? "" : "",
                                 json_escape(chat).c_str());
                    first_chat = false;
                    current = chat;
                } else {
                    std::fprintf(f, ",");
                }
                std::fprintf(f, "\n      {\"texto\": \"%s\", \"ts\": %lld, \"apagada\": false, \"origem\": \"vivo\"}",
                             json_escape(text ? reinterpret_cast<const char*>(text) : "").c_str(), ts);
                if (tl != nullptr) {
                    const std::string stamp = iso_from_unix(ts);
                    // Sem data nao ha evento: uma linha de timeline sem tempo nao
                    // entra no grafico e so polui a tabela.
                    if (!stamp.empty()) {
                        std::fprintf(tl,
                                     "{\"timestamp_utc\": \"%s\", \"provider\": \"WhatsApp\", "
                                     "\"channel\": \"WhatsApp/Messages\", \"event_id\": \"%s#%llu\", "
                                     "\"record_id\": \"%s\", \"event_user_sid\": \"%s\", "
                                     "\"event_data\": {\"text\": \"%s\", \"apagada\": false, "
                                     "\"origem\": \"vivo\"}}\n",
                                     stamp.c_str(), json_escape(chat).c_str(),
                                     (unsigned long long)(timeline_events + 1),
                                     json_escape(chat).c_str(), json_escape(chat).c_str(),
                                     json_escape(text ? reinterpret_cast<const char*>(text) : "").c_str());
                        ++timeline_events;
                    }
                }
            }
            if (!current.empty()) std::fprintf(f, "\n    ]}");
            sqlite3_finalize(st);
        }
    }
    std::fprintf(f, "\n  ],\n");

    // Carved rows travel in the same document, flagged, so the UI can mark them.
    std::fprintf(f, "  \"apagadas\": [\n");
    bool first = true;
    for (const auto& row : carved.rows) {
        if (!row.confirmed_deleted) continue;
        if (!first) std::fprintf(f, ",\n");
        first = false;
        std::fprintf(f, "    {\"rowid\": %lld, \"pagina\": %u, \"origem\": \"%s\", \"tabela\": \"%s\", \"textos\": [",
                     (long long)row.rowid, row.page, row.origin.c_str(),
                     json_escape(row.table_guess).c_str());
        for (std::size_t i = 0; i < row.texts.size() && i < 8; ++i) {
            std::fprintf(f, "%s\"%s\"", i ? ", " : "", json_escape(row.texts[i]).c_str());
        }
        std::fprintf(f, "]}");
    }
    std::fprintf(f, "\n  ]\n}\n");
    std::fclose(f);
    if (tl != nullptr) std::fclose(tl);
    sqlite3_close(db);
    timeline_count = timeline_events;
}

}  // namespace


int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("uso: %s <msgstore.db> --key <hex|arquivo> --out <dir> [--media <dir>]\n", argv[0]);
        return 2;
    }

    std::string db_path = argv[1];
    std::string key_spec, out_dir, media_dir;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--key" && i + 1 < argc) key_spec = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--media" && i + 1 < argc) media_dir = argv[++i];
    }
    if (key_spec.empty() || out_dir.empty()) {
        std::printf("faltando --key ou --out\n");
        return 2;
    }

    olho::wa::key_material key;
    key.raw_key = load_key(key_spec);
    if (key.raw_key.size() != 32) {
        std::printf("chave invalida: esperava 32 bytes, veio %zu\n", key.raw_key.size());
        return 1;
    }

    make_dir(widen(out_dir));

    // 1. Detect.
    auto found = olho::wa::detect(widen(db_path), key);
    std::printf("[1/4] parametros: %s\n", found.detail.c_str());
    if (!found.ok) return 1;

    // 2. Decrypt.
    const std::string plain_path = out_dir + "/msgstore.plain.db";
    olho::wa::decrypt_stats stats{};
    std::string error;
    if (!olho::wa::decrypt_database(widen(db_path), widen(plain_path), found.params, key, stats, error)) {
        std::printf("[2/4] FALHOU: %s\n", error.c_str());
        return 1;
    }
    std::printf("[2/4] decriptado: %llu paginas, %llu frames de WAL -> %s\n",
                (unsigned long long)stats.pages, (unsigned long long)stats.wal_frames, plain_path.c_str());
    if (stats.wal_present) {
        std::printf("      wal: %llu frames aplicados, %llu descartados (transacao incompleta)\n",
                    (unsigned long long)stats.wal_frames, (unsigned long long)stats.wal_discarded);
    } else {
        std::printf("      wal: ausente (o .db e o estado mais recente disponivel)\n");
    }

    // 3. Media.
    std::uint64_t media_ok = 0, media_fail = 0, media_bytes = 0;
    // Midia efemera: fica fora do bloco para o relatorio poder citar mesmo
    // quando nao ha pasta de midia para varrer.
    int view_once_rows = 0;
    bool view_once_table = false;
    // Um registro por linha de midia, com o nome do arquivo que realmente
    // saiu. O relatorio e a unica ponte entre o banco e a pasta de midia: sem
    // ele a galeria nao tem como saber que aquele JPEG veio de uma linha
    // efemera ("ver uma vez"), e a marca fica so no banco decifrado.
    struct media_entry {
        std::string file;      // basename do arquivo gravado em media/
        std::string tipo;      // container identificado pela assinatura
        std::uint64_t bytes = 0;
        bool view_once = false;
        std::string tabela;
        std::string chave;
        std::string mime;
        std::string origem;    // caminho de onde o .enc foi lido
        bool ok = false;       // decifrou e foi gravado
    };
    std::vector<media_entry> media_log;
    std::uint64_t media_view_once_ok = 0;
    if (!media_dir.empty()) {
        make_dir(widen(out_dir + "/media"));
        auto disc = olho::wa::discover_media(widen(plain_path));
        std::printf("[3/4] midia: %s\n", disc.detail.c_str());
        view_once_rows = disc.view_once_rows;
        view_once_table = disc.view_once_table;
        if (disc.view_once_rows > 0 || disc.view_once_table) {
            std::printf("      ver-uma-vez: %d linha(s) marcada(s)%s\n", disc.view_once_rows,
                        disc.view_once_table ? ", tabela dedicada presente" : "");
        }
        int index = 0;
        for (const auto& row : disc.rows) {
            ++index;
            std::string name = row.path_hint;
            const std::size_t slash = name.find_last_of("/\\");
            if (slash != std::string::npos) name = name.substr(slash + 1);
            if (name.empty()) name = "midia_" + std::to_string(index) + ".enc";

            // Os caminhos ficam em UTF-8 e em UTF-16 lado a lado: o relatorio
            // cita o que foi lido de verdade, e o Windows precisa do wide.
            const std::vector<std::string> hint_paths{
                media_dir + "/" + name,
                media_dir + "/Media/WhatsApp Images/" + name,
                media_dir + "/Media/WhatsApp Video/" + name};
            std::vector<std::wstring> candidates;
            candidates.reserve(hint_paths.size());
            for (const std::string& hint : hint_paths) candidates.push_back(widen(hint));

            media_entry entry;
            entry.file = name;
            entry.view_once = row.view_once;
            entry.tabela = row.table;
            entry.chave = row.key_column;
            entry.mime = row.mime_hint;
            entry.origem = hint_paths.front();

            bool placed = false;
            for (std::size_t k = 0; k < candidates.size() && !placed; ++k) {
                const std::wstring& c = candidates[k];
                if (GetFileAttributesW(c.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                // Decrypt to .part first: the real extension is only known once
                // the container signature has been verified, and an export must
                // not be labelled with an extension it has not earned.
                const std::string stem = out_dir + "/media/" + std::to_string(index) + "_" + name;
                const std::string part = stem + ".part";
                auto r = olho::wa::decrypt_media(c, widen(part), row.key);
                if (r.ok) {
                    std::string final_name = name;
                    const std::size_t enc_dot = final_name.rfind(".enc");
                    if (enc_dot != std::string::npos) final_name.erase(enc_dot);
                    // Only add the extension when the name does not already carry it.
                    const std::size_t dot = final_name.rfind('.');
                    const std::string tail = (dot == std::string::npos) ? std::string() : final_name.substr(dot + 1);
                    if (tail != r.extension) final_name += "." + r.extension;
                    final_name = out_dir + "/media/" + std::to_string(index) + "_" + final_name;
                    MoveFileExW(widen(part).c_str(), widen(final_name).c_str(),
                                MOVEFILE_REPLACE_EXISTING);
                    ++media_ok;
                    media_bytes += r.bytes;
                    entry.ok = true;
                    entry.tipo = r.extension;
                    entry.bytes = r.bytes;
                    entry.origem = hint_paths[k];
                    const std::size_t cut = final_name.find_last_of("/\\");
                    if (cut != std::string::npos) final_name = final_name.substr(cut + 1);
                    entry.file = final_name;
                    placed = true;
                    break;
                }
                DeleteFileW(widen(part).c_str());
            }
            if (!placed) ++media_fail;
            if (entry.ok && entry.view_once) ++media_view_once_ok;
            media_log.push_back(std::move(entry));
        }
    }

    // 4. Carve.
    auto carved = olho::wa::carve_sqlite(widen(plain_path));
    std::printf("[4/4] apagadas: %s\n", carved.detail.c_str());

    write_conversations(widen(plain_path), out_dir + "/conversations.json",
                        out_dir + "/timeline.jsonl", carved);

    // Report.
    const std::string report_path = out_dir + "/report.json";
    if (FILE* f = std::fopen(report_path.c_str(), "wb")) {
        std::fprintf(f, "{\n");
        std::fprintf(f, "  \"origem\": \"%s\",\n", json_escape(db_path).c_str());
        std::fprintf(f, "  \"cipher\": \"%s\",\n", json_escape(found.params.describe()).c_str());
        std::fprintf(f, "  \"paginas\": %llu,\n", (unsigned long long)stats.pages);
        std::fprintf(f, "  \"wal_presente\": %s,\n", stats.wal_present ? "true" : "false");
        std::fprintf(f, "  \"wal_frames\": %llu,\n", (unsigned long long)stats.wal_frames);
        std::fprintf(f, "  \"wal_descartados\": %llu,\n", (unsigned long long)stats.wal_discarded);
        std::fprintf(f, "  \"midia_ok\": %llu,\n", (unsigned long long)media_ok);
        std::fprintf(f, "  \"midia_falha\": %llu,\n", (unsigned long long)media_fail);
        std::fprintf(f, "  \"midia_bytes\": %llu,\n", (unsigned long long)media_bytes);
        std::fprintf(f, "  \"midia_view_once\": %d,\n", view_once_rows);
        std::fprintf(f, "  \"midia_view_once_tabela\": %s,\n", view_once_table ? "true" : "false");
        std::fprintf(f, "  \"midia_view_once_arquivos\": %llu,\n", (unsigned long long)media_view_once_ok);
        std::fprintf(f, "  \"timeline_eventos\": %llu,\n", (unsigned long long)timeline_count);
        std::fprintf(f, "  \"apagadas_confirmadas\": %llu,\n", (unsigned long long)carved.confirmed);
        std::fprintf(f, "  \"paginas_freelist\": %llu,\n", (unsigned long long)carved.freelist_pages);
        // Uma entrada por linha de midia. "view_once" so aparece quando e
        // verdade: a galeria le a chave, nao o valor, e um false em todo
        // arquivo so encheria o painel de ruido.
        std::fprintf(f, "  \"midia\": [\n");
        for (std::size_t i = 0; i < media_log.size(); ++i) {
            const media_entry& e = media_log[i];
            std::fprintf(f,
                         "%s{\"file\": \"%s\", \"status\": \"%s\", \"tipo\": \"%s\", \"bytes\": %llu,"
                         " \"tabela\": \"%s\", \"chave\": \"%s\", \"mime\": \"%s\", \"origem\": \"%s\"",
                         i ? ",\n    " : "    ", json_escape(e.file).c_str(), e.ok ? "ok" : "falha",
                         json_escape(e.tipo).c_str(), (unsigned long long)e.bytes,
                         json_escape(e.tabela).c_str(), json_escape(e.chave).c_str(),
                         json_escape(e.mime).c_str(), json_escape(e.origem).c_str());
            if (e.view_once) std::fprintf(f, ", \"view_once\": true");
            std::fprintf(f, "}");
        }
        if (!media_log.empty()) std::fprintf(f, "\n  ");
        std::fprintf(f, "],\n");
        std::fprintf(f, "  \"registros\": [\n");
        bool first = true;
        for (const auto& row : carved.rows) {
            if (!row.confirmed_deleted) continue;
            if (!first) std::fprintf(f, ",\n");
            first = false;
            std::fprintf(f, "    {\"rowid\": %lld, \"pagina\": %u, \"origem\": \"%s\", \"tabela\": \"%s\", \"textos\": [",
                         (long long)row.rowid, row.page, row.origin.c_str(), json_escape(row.table_guess).c_str());
            for (std::size_t i = 0; i < row.texts.size() && i < 8; ++i) {
                std::fprintf(f, "%s\"%s\"", i ? ", " : "", json_escape(row.texts[i]).c_str());
            }
            std::fprintf(f, "]}");
        }
        std::fprintf(f, "\n  ]\n}\n");
        std::fclose(f);
    }

    if (media_view_once_ok > 0) {
        std::printf("ver-uma-vez: %llu arquivo(s) marcado(s) como efemero no relatorio\n",
                    (unsigned long long)media_view_once_ok);
    }
    if (media_fail > 0) {
        std::printf("aviso: %llu linha(s) de midia sem arquivo decifravel na pasta apontada\n",
                    (unsigned long long)media_fail);
    }

    if (timeline_count > 0) {
        std::printf("linha do tempo: %llu evento(s) em %s/timeline.jsonl\n",
                    (unsigned long long)timeline_count, out_dir.c_str());
    }

    std::printf("\nresumo: %llu apagadas recuperadas, %llu arquivos de midia, relatorio em %s\n",
                (unsigned long long)carved.confirmed, (unsigned long long)media_ok, report_path.c_str());
    return 0;
}
