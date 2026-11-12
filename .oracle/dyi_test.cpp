// language: C++, file: .oracle/dyi_test.cpp
#include "social/dyi_import.hpp"

#include "third_party/nlohmann/json.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++failures;
}
static std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main() {
    // 1. Correcao de mojibake.
    {
        check("mojibake vira UTF-8", olho::social::repair_mojibake("JoÃ£o") == "João");
        check("ascii intocado", olho::social::repair_mojibake("texto normal") == "texto normal");
        check("utf-8 correto intocado",
              olho::social::repair_mojibake("João") == "João");
    }

    // 2. Export do Facebook vindo de ZIP.
    const fs::path case_dir = ".oracle/dyi_case/caso-1";
    std::error_code ignore;
    fs::remove_all(".oracle/dyi_case", ignore);
    {
        olho::social::dyi_config config;
        config.source = L".oracle\\dyi_fb.zip";
        config.case_dir = case_dir.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OFICIO-31/2026";
        config.job_id = "job-fb";
        const auto report = olho::social::import_export(config);
        check("importa facebook", report.ok);
        if (!report.ok) std::printf("    erro: %s\n", report.error.c_str());
        check("provedor detectado", report.provider == "facebook");
        check("duas conversas", report.threads == 2);
        check("sete mensagens", report.messages == 7);
        check("uma midia", report.media == 1);
        check("uma apagada", report.unsent == 1);
        check("tres arquivos de conversa", report.files == 3);

        const auto manifest = nlohmann::json::parse(
            read_all(fs::path(report.manifest_path)), nullptr, false);
        check("manifesto legivel", manifest.is_object());
        check("status complete", manifest.value("status", "") == "complete");
        check("provedor no manifesto", manifest.value("provider", "") == "facebook");
        check("tres arquivos com hash", manifest["files"].size() == 3);
        bool hashes_ok = true;
        for (const auto& file : manifest["files"]) {
            const fs::path raw = fs::path(report.output_dir) / file.value("name", "");
            const std::string content = read_all(raw);
            if (content.empty()) { hashes_ok = false; break; }
            if (static_cast<std::uint64_t>(content.size()) != file.value("size_bytes", 0ull)) {
                hashes_ok = false;
                break;
            }
        }
        check("copias brutas presentes e do tamanho declarado", hashes_ok);

        // Cada copia bruta tem de ser identica a um dos arquivos do export.
        const std::string originals[3] = {
            read_all(".oracle/dyi_fb/messages/inbox/ana_123/message_1.json"),
            read_all(".oracle/dyi_fb/messages/inbox/ana_123/message_2.json"),
            read_all(".oracle/dyi_fb/messages/archived_threads/beto_456/message_1.json")};
        bool byte_equal = true;
        bool kept_mojibake = true;
        for (const auto& file : manifest["files"]) {
            const std::string content = read_all(fs::path(report.output_dir) /
                                                 file.value("name", ""));
            bool matched = false;
            for (const auto& original : originals) {
                if (content == original) { matched = true; break; }
            }
            if (!matched) byte_equal = false;
            if (content.find("JoÃ£o") != std::string::npos) kept_mojibake = true;
        }
        check("cada copia bruta igual a um arquivo do export", byte_equal);
        check("bruto mantem o mojibake (nao foi reescrito)", kept_mojibake);

        // Timeline: uma linha por mensagem, canal, carimbo e texto corrigido.
        const std::string timeline = read_all(fs::path(report.timeline_path));
        int lines = 0;
        bool channel_ok = true;
        bool utf8_ok = false;
        bool stamp_ok = false;
        std::size_t start = 0;
        while (start < timeline.size()) {
            const std::size_t end = timeline.find('\n', start);
            if (end == std::string::npos) break;
            const auto event = nlohmann::json::parse(timeline.substr(start, end - start), nullptr, false);
            if (!event.is_object()) { channel_ok = false; break; }
            if (event.value("channel", "") != "Facebook/Messages") channel_ok = false;
            const std::string text = event["event_data"].value("text", "");
            if (text == "João, tudo bem?") utf8_ok = true;
            if (event.value("timestamp_utc", "") == "2023-09-29T15:06:40Z") stamp_ok = true;
            ++lines;
            start = end + 1;
        }
        check("sete linhas na timeline", lines == 7);
        check("canal do facebook", channel_ok);
        check("texto do evento corrigido para UTF-8", utf8_ok);
        check("carimbo convertido de ms para UTC", stamp_ok);
    }

    // 3. Export do Instagram vindo de PASTA (modo diretorio).
    {
        olho::social::dyi_config config;
        config.source = L".oracle\\dyi_ig_pasta";
        config.case_dir = case_dir.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OFICIO-31/2026";
        config.job_id = "job-ig";
        config.provider = "instagram";
        const auto report = olho::social::import_export(config);
        check("importa instagram de pasta", report.ok);
        if (!report.ok) std::printf("    erro: %s\n", report.error.c_str());
        check("uma conversa", report.threads == 1);
        check("duas mensagens", report.messages == 2);
        check("saida separada por provedor",
              fs::exists(fs::path(report.output_dir) / "timeline.jsonl"));
    }

    // 4. Export sem conversas e recusado.
    {
        std::error_code e2;
        fs::create_directories(".oracle/dyi_vazio/vazio", e2);
        std::ofstream out(".oracle/dyi_vazio/vazio/leiame.txt");
        out << "sem mensagens";
        out.close();
        olho::social::dyi_config config;
        config.source = L".oracle\\dyi_vazio";
        config.case_dir = case_dir.string();
        config.account = "vazio@exemplo.com";
        config.legal_basis = "OFICIO-31/2026";
        config.job_id = "job-vazio";
        const auto report = olho::social::import_export(config);
        check("export vazio recusado", !report.ok);
        check("motivo claro", report.error.find("nenhuma conversa") != std::string::npos);
        // O manifesto do job anterior continua intacto.
        const auto kept = nlohmann::json::parse(
            read_all(case_dir / "social" / "facebook" / "kai_exemplo.com" / "manifest.json"),
            nullptr, false);
        check("manifesto anterior preservado", kept.is_object() &&
              kept.value("job_id", "") == "job-fb");
    }

    // 5. Outro job na mesma pasta e recusado (nao se apaga evidencia).
    {
        olho::social::dyi_config config;
        config.source = L".oracle\\dyi_ig_pasta";
        config.case_dir = case_dir.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OUTRA";
        config.job_id = "job-intruso";
        config.provider = "instagram";
        const auto report = olho::social::import_export(config);
        check("job diferente recusado", !report.ok);
        check("motivo cita o dono da pasta",
              report.error.find("job-ig") != std::string::npos);
    }

    std::printf(failures == 0 ? "DYI OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
