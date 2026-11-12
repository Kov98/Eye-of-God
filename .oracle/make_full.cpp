// language: C++, file: .oracle/make_full.cpp
// Full fixture: WhatsApp-shaped database with the SQLCipher page reserve, media
// keys taken from media_keys.txt, and a known set of deleted messages.
#include <sqlite3.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main() {
    const char* dir = "C:/Users/pedri/Downloads/Eye-of-God-main/.oracle/";
    const std::string path = std::string(dir) + "full.db";
    std::remove(path.c_str());

    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) { std::printf("open failed\n"); return 1; }

    int reserve = 48;
    sqlite3_file_control(db, "main", SQLITE_FCNTL_RESERVE_BYTES, &reserve);

    char* err = nullptr;
    // view_once faz parte do fixture desde que a galeria passou a marcar a
    // midia efemera: sem uma linha efemera nao ha como provar, ponta a ponta,
    // que a marca sai do banco e chega na tela.
    const char* ddl =
        "PRAGMA page_size=4096;"
        "CREATE TABLE message_media (message_row_id INTEGER PRIMARY KEY, chat_row_id INTEGER,"
        " file_path TEXT, media_key BLOB, media_name TEXT, mime_type TEXT, file_size INTEGER,"
        " view_once INTEGER DEFAULT 0);"
        "CREATE TABLE message_view_once (message_row_id INTEGER PRIMARY KEY,"
        " message_media_row_id INTEGER, played INTEGER);"
        "CREATE TABLE message(_id INTEGER PRIMARY KEY, key_remote_jid TEXT, data TEXT, timestamp INTEGER);";
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
        std::printf("ddl: %s\n", err ? err : "?"); return 1;
    }

    // Media rows: keys come from the PowerShell-generated key file.
    {
        FILE* f = std::fopen((std::string(dir) + "media_keys.txt").c_str(), "rb");
        if (!f) { std::printf("media_keys.txt missing\n"); return 1; }
        char line[512];
        int media_index = 0;
        while (std::fgets(line, sizeof(line), f)) {
            char name[256] = {0}, hex[256] = {0};
            if (std::sscanf(line, "%255s %255s", name, hex) != 2) continue;
            unsigned char key[32];
            for (int i = 0; i < 32; ++i) {
                unsigned v = 0;
                std::sscanf(hex + i * 2, "%2x", &v);
                key[i] = static_cast<unsigned char>(v);
            }
            ++media_index;
            // A segunda linha do fixture e efemera: o aplicativo apagaria o
            // arquivo depois da primeira visualizacao, entao a linha do banco
            // e a unica prova de que ela existiu.
            const int view_once = media_index == 2 ? 1 : 0;
            sqlite3_stmt* st = nullptr;
            sqlite3_prepare_v2(db,
                "INSERT INTO message_media(chat_row_id,file_path,media_key,media_name,mime_type,file_size,"
                "view_once) VALUES(101,?,?,?,'image/jpeg',2032,?)", -1, &st, nullptr);
            const std::string rel = std::string("Media/WhatsApp Images/") + name;
            sqlite3_bind_text(st, 1, rel.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_blob(st, 2, key, 32, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 4, view_once);
            sqlite3_step(st);
            sqlite3_finalize(st);
            if (view_once != 0) {
                sqlite3_exec(db,
                             "INSERT INTO message_view_once(message_row_id,message_media_row_id,played)"
                             " VALUES(1,1,0)", nullptr, nullptr, nullptr);
            }
        }
        std::fclose(f);
    }

    sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO message(_id,key_remote_jid,data,timestamp) VALUES(?,?,?,?)",
                       -1, &ins, nullptr);
    for (int i = 1; i <= 400; ++i) {
        char jid[64], data[256];
        std::snprintf(jid, sizeof(jid), "5511999%04d@s.whatsapp.net", i);
        std::snprintf(data, sizeof(data), "APAGADA-MARCADOR-%d conversa removida do aparelho", i);
        sqlite3_bind_int(ins, 1, i);
        sqlite3_bind_text(ins, 2, jid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, data, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 4, 1700000000 + i);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    // Contiguous block retires whole pages; scattered ids exercise freeblocks.
    std::vector<int> victims;
    for (int i = 50; i <= 200; ++i) victims.push_back(i);
    for (int v : {250, 260, 270, 280, 290, 300}) victims.push_back(v);
    sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
    sqlite3_stmt* del = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM message WHERE _id=?", -1, &del, nullptr);
    for (int v : victims) {
        sqlite3_bind_int(del, 1, v);
        sqlite3_step(del);
        sqlite3_reset(del);
    }
    sqlite3_finalize(del);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    FILE* check = std::fopen(path.c_str(), "rb");
    unsigned char header[24] = {0};
    std::fread(header, 1, sizeof(header), check);
    std::fclose(check);
    std::printf("full.db reserved_byte=%d\n", header[20]);

    FILE* vf = std::fopen((std::string(dir) + "victims_full.txt").c_str(), "wb");
    for (std::size_t i = 0; i < victims.size(); ++i)
        std::fprintf(vf, "%d\n", victims[i]);
    std::fclose(vf);

    sqlite3_close(db);
    std::printf("apagadas=%zu\n", victims.size());
    return 0;
}
