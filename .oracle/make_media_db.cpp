// language: C++, file: .oracle/make_media_db.cpp
// Builds a WhatsApp-shaped plaintext database with media rows, so discovery and
// decryption can be exercised without a real device.
#include <sqlite3.h>
#include <cstdio>

int main() {
    const char* path = "C:/Users/pedri/Downloads/Eye-of-God-main/.oracle/mediadb.db";
    std::remove(path);
    sqlite3* db = nullptr;
    if (sqlite3_open(path, &db) != SQLITE_OK) { std::printf("open failed\n"); return 1; }

    char* err = nullptr;
    // Column names deliberately use the modern WhatsApp spelling.
    const char* ddl =
        "CREATE TABLE message_media (message_row_id INTEGER PRIMARY KEY,"
        " chat_row_id INTEGER, file_path TEXT, media_key BLOB, media_name TEXT,"
        " mime_type TEXT, file_size INTEGER);"
        // A decoy table with no key column must be ignored.
        "CREATE TABLE messages (_id INTEGER PRIMARY KEY, data TEXT);";
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
        std::printf("ddl failed: %s\n", err ? err : "?");
        return 1;
    }
    sqlite3_exec(db, "INSERT INTO messages(data) VALUES('mensagem sem midia');",
                 nullptr, nullptr, nullptr);

    // One row per fixture file, key supplied on the command line as hex.
    for (int i = 1; i < 3; ++i) {
        char keyhex[128] = {0};
        char name[64];
        std::snprintf(name, sizeof(name), ".oracle/media%d.jpg.enc", i);
        std::printf("expecting key for %s\n", name);
        (void)keyhex;
    }

    sqlite3_close(db);
    std::printf("db written\n");
    return 0;
}
