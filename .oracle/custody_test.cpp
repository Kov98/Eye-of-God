#include "custody_chain.hpp"
#include <cstdio>
#include <string>

static void report(const char* label, bool verified, std::size_t idx, const std::string& err) {
    std::printf("  %-26s %s", label, verified ? "INTACTA" : "DETECTADA");
    if (!verified) std::printf("  [indice %zu] %s", idx, err.c_str());
    std::printf("\n");
}

int main() {
    olho::custody::chain original;
    std::string error;
    if (!olho::custody::build("CASE-2026-0042|2026-11-05T17:00:00", ".oracle/case_test/recovered",
                              original, error)) {
        std::printf("build falhou: %s\n", error.c_str());
        return 1;
    }
    std::printf("cadeia: %zu entradas, selo %s\n", original.entries.size(),
                original.head.substr(0, 16).c_str());

    std::size_t idx = 0;
    std::string path;
    report("original", olho::custody::verify(original, idx, path, error), idx, error);
    int failures = 0;

    // 1. remover uma entrada do meio
    {
        auto c = original;
        if (c.entries.size() > 1) c.entries.erase(c.entries.begin() + 1);
        bool ok = olho::custody::verify(c, idx, path, error);
        report("entrada removida", ok, idx, error);
        if (ok) ++failures;
    }
    // 2. alterar o hash de um arquivo
    {
        auto c = original;
        if (!c.entries.empty()) c.entries.back().sha256[0] = (c.entries.back().sha256[0] == 'a') ? 'b' : 'a';
        bool ok = olho::custody::verify(c, idx, path, error);
        report("hash alterado", ok, idx, error);
        if (ok) ++failures;
    }
    // 3. reordenar
    {
        auto c = original;
        if (c.entries.size() > 1) std::swap(c.entries[0], c.entries[1]);
        bool ok = olho::custody::verify(c, idx, path, error);
        report("entradas reordenadas", ok, idx, error);
        if (ok) ++failures;
    }
    // 4. remover a ultima e re-selar (o ataque realista)
    {
        auto c = original;
        if (!c.entries.empty()) {
            c.entries.pop_back();
            c.head = c.entries.empty() ? c.head : c.entries.back().link;
        }
        bool ok = olho::custody::verify(c, idx, path, error);
        report("cauda removida e re-selada", ok, idx, error);
        if (!ok) ++failures;   // aqui o selo bate; so o JSON externo denunciaria
    }
    // 5. ida e volta por JSON
    {
        auto c = original;
        olho::custody::chain back;
        std::string e2;
        bool parsed = olho::custody::from_json(olho::custody::to_json(c), back, e2);
        bool ok = parsed && olho::custody::verify(back, idx, path, error);
        report("ida e volta JSON", ok, idx, error);
        if (!ok) ++failures;
    }
    std::printf(failures == 0 ? "TODOS OS CASOS CORRETOS\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}