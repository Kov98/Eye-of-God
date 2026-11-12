// language: C++, file: .oracle/mem_test.cpp
// Prova sem depender de nenhum alvo externo: planta um JPEG na propria memoria,
// adquire o proprio processo, e exige o arquivo de volta identico.
#include "mem_acq.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++failures;
}

static std::vector<std::uint8_t> make_jpeg(std::size_t payload) {
    std::vector<std::uint8_t> j = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00};
    for (std::size_t i = 0; i < payload; ++i) {
        j.push_back(static_cast<std::uint8_t>((i * 37 + 11) & 0xFF));
    }
    j.push_back(0xFF);
    j.push_back(0xD9);
    return j;
}

int main() {
    const std::vector<std::uint8_t> planted = make_jpeg(4000);
    // Heap, so it survives and is definitely in a readable committed region.
    auto* buffer = new std::uint8_t[planted.size()];
    std::memcpy(buffer, planted.data(), planted.size());

    const std::uint32_t pid = GetCurrentProcessId();
    std::printf("pid = %u, jpeg plantado = %zu bytes\n", pid, planted.size());

    // 1. enumeracao
    const auto processes = olho::mem::list_processes();
    check("enumerou processos", processes.size() > 5);
    bool self_found = false;
    for (const auto& p : processes) if (p.pid == pid) self_found = true;
    check("o proprio processo aparece", self_found);

    // 2. aquisicao + carving
    olho::mem::region_stats stats;
    std::vector<olho::mem::carve_result> found;
    std::string error;
    const bool ok = olho::mem::acquire(pid, 512ull * 1024 * 1024, stats, found, error);
    std::printf("regioes vistas=%llu lidas=%llu bytes=%llu negadas=%llu\n",
                (unsigned long long)stats.regions_seen,
                (unsigned long long)stats.regions_read,
                (unsigned long long)stats.bytes_read,
                (unsigned long long)stats.regions_denied);
    check("aquisicao rodou", ok && error.empty());
    check("leu memoria de verdade", stats.bytes_read > 1024 * 1024);

    int exact = 0;
    for (const auto& c : found) {
        if (c.kind == "jpeg" && c.data.size() == planted.size() &&
            std::memcmp(c.data.data(), planted.data(), planted.size()) == 0) {
            ++exact;
        }
    }
    std::printf("containers encontrados = %zu, jpeg identico = %d\n", found.size(), exact);
    check("achou o jpeg plantado", exact >= 1);
    check("nao inventou containers demais", found.size() < 500);

    // 3. busca de material de chave
    const std::string needle_text = "CHAVE-EFEMERA-4C9E";
    auto* secret = new char[needle_text.size() + 1];
    std::memcpy(secret, needle_text.data(), needle_text.size() + 1);

    std::uint64_t address = 0;
    olho::mem::bytes hit;
    const bool foundNeedle = olho::mem::find_bytes(
        pid, olho::mem::bytes(needle_text.begin(), needle_text.end()), 32, address, hit, error);
    check("achou o material de chave", foundNeedle);
    if (foundNeedle) {
        const std::string window(hit.begin(), hit.end());
        check("janela contem o segredo", window.find(needle_text) != std::string::npos);
        std::printf("  endereco = 0x%llx\n", (unsigned long long)address);
    }

    // 4. alvo invalido falha limpo
    {
        olho::mem::region_stats s2;
        std::vector<olho::mem::carve_result> f2;
        std::string e2;
        check("pid inexistente recusado", !olho::mem::acquire(0xFFFFFFF0u, 0, s2, f2, e2));
    }

    delete[] buffer;
    delete[] secret;
    std::printf(failures == 0 ? "MEM OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}