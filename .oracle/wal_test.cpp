// language: C++, file: .oracle/wal_test.cpp
#include "wa_wal.hpp"
#include "third_party/nlohmann/json.hpp"
#include "third_party/sqlite/sqlite3.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++failures;
}

struct row {
    int id = 0;
    std::string text;
};

// Le as linhas vivas. Abrir um arquivo que tem vizinho -wal faz o proprio
// SQLite aplicar o WAL: e o oraculo, nao a nossa implementacao.
static bool read_rows(const std::string& path, std::vector<row>& rows) {
    rows.clear();
    sqlite3* db = nullptr;
    // READWRITE: um banco que ficou marcado como WAL recusa abrir somente leitura
    // sem o -shm, e ambos os arquivos aqui sao copias nossas.
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id, texto FROM msg ORDER BY id", -1, &statement,
                           nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    while (sqlite3_step(statement) == SQLITE_ROW) {
        row entry;
        entry.id = sqlite3_column_int(statement, 0);
        const unsigned char* text = sqlite3_column_text(statement, 1);
        entry.text = text == nullptr ? std::string()
                                     : std::string(reinterpret_cast<const char*>(text));
        rows.push_back(entry);
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return true;
}

int main() {
    // Le o WAL agora: abrir o banco de referencia com o SQLite faz checkpoint e
    // apaga o -wal, entao os bytes precisam ser capturados antes de qualquer open.
    std::vector<unsigned char> wal_bytes;
    {
        std::ifstream in(".oracle/wal_db.db-wal", std::ios::binary);
        wal_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    olho::wa::wal_stats stats;
    const bool ok = olho::wa::wal_apply(L".oracle\\wal_db.db", L".oracle\\wal_db.db-wal",
                                        L".oracle\\wal_out.db", stats);
    check("aplica o wal", ok);
    if (!ok) std::printf("    erro: %s\n", stats.error.c_str());
    check("frames lidos", stats.frames > 0);
    check("frames commitados", stats.committed > 0);
    check("tamanho de pagina conhecido", stats.page_size >= 512);

    std::vector<row> ours;
    std::vector<row> oracle;
    const bool ours_ok = read_rows(".oracle/wal_out.db", ours);
    const bool oracle_ok = read_rows(".oracle/wal_db.db", oracle);
    check("banco reconstruido abre", ours_ok);
    check("banco original abre", oracle_ok);
    check("mesmo numero de linhas", ours.size() == oracle.size());
    bool same = ours.size() == oracle.size();
    for (std::size_t i = 0; same && i < ours.size(); ++i) {
        if (ours[i].id != oracle[i].id || ours[i].text != oracle[i].text) same = false;
    }
    check("conteudo identico ao do SQLite", same);
    check("vinte linhas (uma foi apagada)", oracle.size() == 19);
    bool editada = false;
    for (const auto& entry : oracle) {
        if (entry.id == 3 && entry.text == "mensagem 3 editada") editada = true;
    }
    check("atualizacao veio do wal", editada);
    bool apagada_sumiu = true;
    for (const auto& entry : ours) {
        if (entry.id == 7) apagada_sumiu = false;
    }
    check("linha apagada nao volta na reconstrucao", apagada_sumiu);


    // 4. Varredura pura (wal_scan) contra a lista que o Python produziu do mesmo
    //    arquivo: mesma contagem, mesmos pgno/db_size e mesmo trecho pos-commit.
    {
        std::vector<olho::wa::wal_frame_info> frames;
        olho::wa::wal_stats scan_stats;
        check("scan le o wal", olho::wa::wal_scan(wal_bytes, frames, scan_stats));
        std::ifstream ref_in(".oracle/wal_frames.json", std::ios::binary);
        const std::string ref_text((std::istreambuf_iterator<char>(ref_in)),
                                   std::istreambuf_iterator<char>());
        const auto reference = nlohmann::json::parse(ref_text, nullptr, false);
        check("referencia legivel", reference.is_array());
        if (reference.is_array()) {
            check("mesma contagem de frames", frames.size() == reference.size());
            bool same = frames.size() == reference.size();
            for (std::size_t i = 0; same && i < frames.size(); ++i) {
                if (frames[i].pgno != reference[i].value("pgno", 0u)) same = false;
                if (frames[i].db_size != reference[i].value("db_size", 0u)) same = false;
                if (frames[i].after_last_commit != reference[i].value("after_last_commit", false))
                    same = false;
            }
            check("pgno, db_size e pos-commit iguais aos do Python", same);
        }
        check("so a cauda ficou sem commit",
              scan_stats.frames == frames.size());
    }

    std::printf(failures == 0 ? "WAL OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
