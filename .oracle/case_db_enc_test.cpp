// language: C++, file: .oracle/case_db_enc_test.cpp
// O teste que fecha o buraco: o proprio CaseDatabase, nao o VFS cru.
#include "case_db.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
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
    const std::string db_path = ".oracle/case_db_enc.db";
    const std::wstring wdb = L".oracle/case_db_enc.db";
    const std::wstring wkey = L".oracle/case_db_enc.key";
    std::remove(db_path.c_str());
    std::remove((db_path + "-wal").c_str());
    std::remove((db_path + "-journal").c_str());
    std::remove(".oracle/case_db_enc.key");

    const std::string marker = "EVIDENCIA-SIGILOSA-7B21";

    {
        deep::CaseDatabase db;
        std::wstring error;
        check("abre cifrado (cria chave)", db.open_encrypted(wdb, wkey, error));
        if (!error.empty()) std::printf("    erro: %ls\n", error.c_str());

        check("grava meta", db.set_meta("caso", marker, error));
        deep::ArtifactRow row;
        row.type = "message";
        row.source = "whatsapp";
        row.key = marker;
        row.timestamp_utc = "2026-11-05T00:00:00Z";
        row.data = marker;
        check("insere artefato", db.insert(row, error));
        check("conta artefatos", db.count("artifacts", error) == 1);
    }

    // arquivo em disco
    const auto raw = slurp(db_path);
    check("arquivo existe e tem conteudo", raw.size() > 1000);
    check("SEM texto claro no banco", !contains(raw, marker));
    check("cabecalho SQLite cifrado", !contains(raw, "SQLite format 3"));

    // WAL, se existir, tambem nao pode vazar
    const auto wal = slurp(db_path + "-wal");
    if (!wal.empty()) check("SEM texto claro no WAL", !contains(wal, marker));

    // reabre e le
    {
        deep::CaseDatabase db;
        std::wstring error;
        check("reabre cifrado", db.open_encrypted(wdb, wkey, error));
        check("meta preservada", db.count("artifacts", error) == 1);
        std::string json;
        check("consulta responde", db.query_json("SELECT key FROM artifacts", json, error));
        check("valor volta igual", json.find(marker) != std::string::npos);
    }

    // chave de outra conta/maquina nao abre: simulamos corrompendo o arquivo de chave
    {
        std::ofstream bad(".oracle/case_db_enc.key", std::ios::binary | std::ios::trunc);
        bad << "chave-invalida";
        bad.close();
        deep::CaseDatabase db;
        std::wstring error;
        const bool opened = db.open_encrypted(wdb, wkey, error);
        check("chave invalida recusada", !opened);
    }

    std::printf(failures == 0 ? "CASE DB ENC OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}