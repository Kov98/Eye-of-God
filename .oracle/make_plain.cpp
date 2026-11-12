// language: C++, file: .oracle/make_plain.cpp
// Builds a plaintext fixture with the reserved-bytes-per-page that SQLCipher
// implies, so the decrypted image is a database SQLite actually accepts.
#include <sqlite3.h>
#include <cstdio>

int main() {
    sqlite3* db = nullptr;
    const char* path = "C:/Users/pedri/Downloads/Eye-of-God-main/.oracle/plain2.db";
    std::remove(path);
    if (sqlite3_open(path, &db) != SQLITE_OK) { std::printf("open failed\n"); return 1; }

    int reserve = 48;
    const int fc = sqlite3_file_control(db, "main", SQLITE_FCNTL_RESERVE_BYTES, &reserve);
    std::printf("file_control reserve=%d rc=%d (%s)\n", reserve, fc, sqlite3_errmsg(db));

    char* err = nullptr;
    if (sqlite3_exec(db, "PRAGMA page_size=4096;"
                         "CREATE TABLE message(_id INTEGER PRIMARY KEY, key_remote_jid TEXT,"
                         " data TEXT, timestamp INTEGER);", nullptr, nullptr, &err) != SQLITE_OK) {
        std::printf("exec failed: %s\n", err ? err : "?");
        return 1;
    }
    sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
    for (int i = 1; i <= 399; ++i) {
        char sql[512];
        std::snprintf(sql, sizeof(sql),
            "INSERT INTO message(key_remote_jid,data,timestamp)"
            " VALUES('5511999%04d@s.whatsapp.net','mensagem de teste numero %d',%d);",
            i, i, 1700000000 + i);
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::printf("insert failed: %s\n", err ? err : "?");
            return 1;
        }
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    // The header records the reserve at offset 20. If the control took effect it is 48.
    sqlite3_close(db);
    FILE* f = std::fopen(path, "rb");
    unsigned char header[24] = {0};
    std::fread(header, 1, sizeof(header), f);
    std::fclose(f);
    std::printf("header page_size=%d reserved=%d\n", (header[16] << 8) | header[17], header[20]);
    return 0;
}
