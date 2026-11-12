// language: C++, file: social_main.cpp
// Importador de exportacao de rede social ("Baixar suas informacoes").
//
// Le o .zip (ou a pasta ja extraida) do Facebook/Instagram, copia cada conversa
// como evidencia com hash e gera manifesto + timeline que a importacao do banco
// do caso consome.
#include "social/dyi_import.hpp"

#include <iostream>
#include <string>

namespace {

std::wstring widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

void usage() {
    std::cout
        << "olho_social --export <arquivo.zip|pasta> --case <dir> --account <rotulo>\n"
        << "  --legal-basis <ref> --job <id> [--provider facebook|instagram]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string source, case_dir, account, legal, job, provider;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string name = argv[i];
        if (name == "--export") source = argv[++i];
        else if (name == "--case") case_dir = argv[++i];
        else if (name == "--account") account = argv[++i];
        else if (name == "--legal-basis") legal = argv[++i];
        else if (name == "--job") job = argv[++i];
        else if (name == "--provider") provider = argv[++i];
        else if (name == "--help" || name == "-h") { usage(); return 0; }
    }
    if (source.empty() || case_dir.empty() || account.empty() || legal.empty() || job.empty()) {
        usage();
        std::cerr << "erro: faltam argumentos obrigatorios\n";
        return 2;
    }

    olho::social::dyi_config config;
    config.source = widen(source);
    config.case_dir = case_dir;
    config.account = account;
    config.legal_basis = legal;
    config.job_id = job;
    config.provider = provider;

    const olho::social::dyi_report report = olho::social::import_export(config);
    std::cout << "provider: " << report.provider << "\n"
              << "conversas: " << report.threads << "\n"
              << "mensagens: " << report.messages << "\n"
              << "com midia: " << report.media << "\n"
              << "apagadas: " << report.unsent << "\n"
              << "arquivos: " << report.files << "\n"
              << "midias: " << report.media_files << " (ausentes: " << report.media_missing << ")\n"
              << "origem_sha256: " << report.source_sha256 << "\n"
              << "bytes: " << report.bytes << "\n"
              << "saida: " << report.output_dir << "\n"
              << "manifesto: " << report.manifest_path << "\n";
    if (!report.error.empty()) std::cerr << "erro: " << report.error << "\n";
    if (report.ok) return 0;
    return report.partial ? 6 : 5;
}
