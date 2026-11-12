// language: C++, file: evidence_main.cpp, target: Windows 11
// Checks acquired evidence for files whose content contradicts their extension.
//   olho_evidence <dir>            relatorio legivel
//   olho_evidence <dir> --quiet    so o codigo de saida (1 = houve divergencia)
#include "evidence_verify.hpp"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("uso: %s <dir> [--quiet]\n", argv[0]); return 2; }
    const bool quiet = argc > 2 && std::strcmp(argv[2], "--quiet") == 0;

    const std::wstring root = std::wstring(argv[1], argv[1] + std::strlen(argv[1]));
    const auto result = olho::evidence::verify_tree(root);

    if (!quiet) {
        std::printf("verificados: %llu | conferem: %llu | divergem: %llu | sem verificacao: %llu\n",
                    (unsigned long long)result.checked, (unsigned long long)result.matched,
                    (unsigned long long)result.mismatched, (unsigned long long)result.unknown);
        for (const auto& f : result.findings) {
            std::printf("  DIVERGE  %s\n           %s\n", f.relative_path.c_str(), f.detail.c_str());
        }
        if (result.findings.empty()) std::printf("  nenhuma divergencia de tipo\n");
    }
    return result.mismatched > 0 ? 1 : 0;
}
