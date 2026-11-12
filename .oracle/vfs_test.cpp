// language: C++, file: .oracle/vfs_test.cpp
// O teste que importa: depois de gravar, o dado sensivel NAO pode existir em
// texto claro no arquivo — nem no banco, nem no journal, nem no WAL.
#include "case_crypto.hpp"
#include "sqlite_enc_vfs.hpp"

#include "sqlite3.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);   // sem isso, um crash depois apaga tudo que ja passou
    if (!ok) ++failures;
}

static std::vector<char> slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static bool contains(const std::vector<char>& hay, const std::string& needle) {
    if (hay.size() < needle.size() || needle.empty()) return false;
    return std::search(hay.begin(), hay.end(), needle.data(), needle.data() + needle.size()) != hay.end();
}

int main() {
    const std::string db_path = ".oracle/case_enc.db";
    std::remove(db_path.c_str());
    std::remove((db_path + "-journal").c_str());
    std::remove((db_path + "-wal").c_str());

    const std::string marker = "SEGREDO-DE-CASO-9F3A2B";

    std::puts("[dbg] inicio"); std::fflush(stdout);
    deep::case_cipher cipher;
    std::string error;
    if (!cipher.open_or_create(L".oracle/case_enc.key", error)) {
        std::printf("chave falhou: %s\n", error.c_str());
        return 1;
    }
    std::puts("[dbg] chave ok"); std::fflush(stdout);
    deep::install_encryption(&cipher);
    const char* vfs = deep::encryption_vfs_name();
    std::printf("[dbg] vfs = %s", vfs ? vfs : "(null)"); std::puts(""); std::fflush(stdout);
    check("VFS registrado", vfs && std::strlen(vfs) > 0);
    std::puts("[dbg] antes do open"); std::fflush(stdout);

    // 1. grava pelo VFS cifrado
    sqlite3* db = nullptr;
    check("abre banco pelo VFS cifrado",
          sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs) == SQLITE_OK);
    {
        char* err = nullptr;
        check("cria tabela", sqlite3_exec(db, "CREATE TABLE evidence(body TEXT, n INT);", nullptr, nullptr, &err) == SQLITE_OK);
        std::string sql = "INSERT INTO evidence VALUES('" + marker + "', 42);";
        check("insere o marcador", sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) == SQLITE_OK);
        // volume extra para garantir mais de uma pagina
        check("insere volume", sqlite3_exec(db,
            "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<500)"
            " INSERT INTO evidence SELECT 'enchimento '||x, x FROM c;", nullptr, nullptr, &err) == SQLITE_OK);
    }
    sqlite3_close(db);

    // 2. o arquivo em disco nao pode conter o marcador
    const auto raw = slurp(db_path);
    check("arquivo tem conteudo", raw.size() > 4000);
    check("NAO ha texto claro no banco", !contains(raw, marker));
    check("NAO ha texto claro no banco (enchimento)", !contains(raw, "enchimento"));

    // se o arquivo contivesse o cabecalho SQLite em claro, estaria errado
    check("cabecalho SQLite tambem cifrado", !contains(raw, "SQLite format 3"));

    // 3. reabre e o dado volta inteiro
    db = nullptr;
    check("reabre pelo VFS", sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, vfs) == SQLITE_OK);
    {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db, "SELECT body, n FROM evidence WHERE n = 42;", -1, &st, nullptr);
        const bool row = sqlite3_step(st) == SQLITE_ROW;
        const std::string body = row ? reinterpret_cast<const char*>(sqlite3_column_text(st, 0)) : "";
        const int n = row ? sqlite3_column_int(st, 1) : 0;
        sqlite3_finalize(st);
        check("linha lida de volta", row);
        check("conteudo intacto", body == marker);
        check("inteiro intacto", n == 42);

        sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM evidence;", -1, &st, nullptr);
        const int total = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int(st, 0) : 0;
        sqlite3_finalize(st);
        check("501 linhas preservadas", total == 501);
    }

    // 4. o journal: apagar uma linha escreve a pagina ANTIGA no journal. Se o
    //    journal vazasse texto claro, a protecao seria decorativa.
    {
        char* err = nullptr;
        sqlite3_exec(db, "PRAGMA journal_mode=DELETE;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "BEGIN;", nullptr, nullptr, &err);
        sqlite3_exec(db, "DELETE FROM evidence WHERE n = 42;", nullptr, nullptr, &err);
        const auto journal = slurp(db_path + "-journal");
        const bool journal_exists = !journal.empty();
        check("journal existe durante a transacao", journal_exists);
        if (journal_exists) check("NAO ha texto claro no journal", !contains(journal, marker));
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, &err);

        // A linha que existia aqui passava nullptr como sqlite3_stmt** e o SQLite
        // escrevia em ponteiro nulo — era o crash, nao o VFS.
        sqlite3_stmt* verify = nullptr;
        check("prepara consulta apos rollback",
              sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM evidence WHERE body = ?1;", -1, &verify,
                                 nullptr) == SQLITE_OK);
        sqlite3_finalize(verify);
    }
    sqlite3_close(db);

    std::printf(failures == 0 ? "VFS OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}