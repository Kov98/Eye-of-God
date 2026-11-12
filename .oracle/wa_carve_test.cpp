// language: C++, file: .oracle/wa_carve_test.cpp
#include "wa_carve.hpp"

#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>

int main() {
    auto r = olho::wa::carve_sqlite(L"C:/Users/pedri/Downloads/Eye-of-God-main/.oracle/carvedb.db");
    std::printf("ok=%d pages=%llu freelist=%llu candidatas=%llu confirmadas=%llu\n",
                r.ok ? 1 : 0, (unsigned long long)r.pages_scanned,
                (unsigned long long)r.freelist_pages,
                (unsigned long long)r.candidates, (unsigned long long)r.confirmed);
    for (const auto& t : r.tables)
        std::printf("  tabela %s: %llu linhas vivas, %zu colunas\n",
                    t.name.c_str(), (unsigned long long)t.live_rows, t.columns.size());

    // Which deleted ids came back.
    std::ifstream vf("C:/Users/pedri/Downloads/Eye-of-God-main/.oracle/victims.txt");
    std::set<long long> victims;
    long long v;
    while (vf >> v) victims.insert(v);

    std::set<long long> recovered;
    int ghosts = 0;
    for (const auto& row : r.rows) {
        if (!row.confirmed_deleted) continue;
        if (victims.count(row.rowid)) recovered.insert(row.rowid);
        else ++ghosts;
        if (recovered.size() <= 3 && victims.count(row.rowid)) {
            std::printf("  amostra rowid=%lld origem=%s tabela=%s texto=\"%s\"\n",
                        (long long)row.rowid, row.origin.c_str(), row.table_guess.c_str(),
                        row.texts.empty() ? "" : row.texts.front().c_str());
        }
    }
    std::printf("recuperadas=%zu de %zu apagadas (%.1f%%) | nao-vitimas=%d\n",
                recovered.size(), victims.size(),
                victims.empty() ? 0.0 : 100.0 * recovered.size() / victims.size(), ghosts);
    return recovered.empty() ? 1 : 0;
}
