// language: C++, file: .oracle/dyi_test.cpp
#include "social/dyi_import.hpp"
#include "social/takeout_import.hpp"
#include "social/telegram_import.hpp"

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
        check("uma foto copiada", report.media_files == 1);
        check("uma midia ausente contada", report.media_missing == 1);
        check("hash do zip de origem calculado", report.source_sha256.size() == 64);
        check("tamanho do zip registrado", report.source_bytes > 0);

        const auto manifest = nlohmann::json::parse(
            read_all(fs::path(report.manifest_path)), nullptr, false);
        check("manifesto legivel", manifest.is_object());
        check("status complete", manifest.value("status", "") == "complete");
        check("provedor no manifesto", manifest.value("provider", "") == "facebook");
        check("quatro arquivos com hash (3 conversas + 1 foto)",
              manifest["files"].size() == 4);
        check("hash da origem no manifesto", manifest.value("source_sha256", "").size() == 64);
        bool media_entry = false;
        for (const auto& file : manifest["files"]) {
            if (file.value("source", "") == "media") {
                media_entry = true;
                const std::string stored = read_all(fs::path(report.output_dir) /
                                                    file.value("name", ""));
                if (stored != std::string("JPEGDATA\1\2\3")) media_entry = false;
            }
        }
        check("foto copiada byte a byte e listada como midia", media_entry);
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
            // A foto tambem entra no manifesto; esta checagem e das conversas.
            if (file.value("source", "") != "messages") continue;
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

    // 4b. Takeout do Google (Gmail em .mbox), detectado automaticamente.
    {
        olho::social::dyi_config config;
        config.source = L".oracle\\takeout_fixture";
        config.case_dir = case_dir.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OFICIO-31/2026";
        config.job_id = "job-takeout";
        check("detecta takeout", olho::social::looks_like_takeout(config.source));
        const auto report = olho::social::import_export(config);
        check("importa takeout", report.ok);
        if (!report.ok) std::printf("    erro: %s\n", report.error.c_str());
        check("provedor google", report.provider == "google");
        check("uma caixa de correio", report.threads == 1);
        check("tres mensagens do gmail", report.messages == 3);
        const std::string timeline = read_all(fs::path(report.timeline_path));
        int lines = 0;
        bool channel_ok = true;
        bool subject_ok = false;
        bool body_ok = false;
        std::size_t start = 0;
        while (start < timeline.size()) {
            const std::size_t end = timeline.find('\n', start);
            if (end == std::string::npos) break;
            const auto event = nlohmann::json::parse(timeline.substr(start, end - start), nullptr, false);
            if (!event.is_object() || event.value("channel", "") != "Google/Mail") channel_ok = false;
            const std::string subject = event["event_data"].value("subject", "");
            if (subject == "M\u00e3e com acento") subject_ok = true;
            const std::string body = event["event_data"].value("body", "");
            if (body == "Ol\u00e1, tudo bem?\nSegunda linha.") body_ok = true;
            ++lines;
            start = end + 1;
        }
        check("tres linhas na timeline", lines == 3);
        check("canal do gmail", channel_ok);
        check("assunto decodificado no evento", subject_ok);
        check("corpo decodificado no evento", body_ok);
    }

    // 4c. Export do Telegram Desktop (result.json por conversa).
    {
        olho::social::dyi_config config;
        config.source = L".oracle\\telegram_fixture";
        config.case_dir = case_dir.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OFICIO-31/2026";
        config.job_id = "job-telegram";
        check("detecta telegram", olho::social::looks_like_telegram(config.source));
        const auto report = olho::social::import_export(config);
        check("importa telegram", report.ok);
        if (!report.ok) std::printf("    erro: %s\n", report.error.c_str());
        check("provedor telegram", report.provider == "telegram");
        check("duas conversas", report.threads == 2);
        check("seis mensagens", report.messages == 6);
        check("uma foto", report.media_files == 1);
        check("uma midia referenciada", report.media == 1);

        const std::string timeline = read_all(fs::path(report.timeline_path));
        int lines = 0;
        bool channel_ok = true;
        bool joined_text = false;
        bool date_as_is = false;
        bool service_kept = false;
        bool reply_kept = false;
        std::size_t start = 0;
        while (start < timeline.size()) {
            const std::size_t end = timeline.find('\n', start);
            if (end == std::string::npos) break;
            const auto event = nlohmann::json::parse(timeline.substr(start, end - start), nullptr, false);
            if (!event.is_object() || event.value("channel", "") != "Telegram/Messages") {
                channel_ok = false;
            }
            const auto data = event["event_data"];
            if (data.value("text", "") == "olha isso e me diz") joined_text = true;
            if (event.value("timestamp_utc", "") == "2023-11-14T19:13:00") date_as_is = true;
            if (data.value("message_type", "") == "service") service_kept = true;
            if (data.value("reply_to", 0) == 1) reply_kept = true;
            ++lines;
            start = end + 1;
        }
        check("seis linhas na timeline", lines == 6);
        check("canal do telegram", channel_ok);
        check("texto em pedacos concatenado", joined_text);
        check("data entra como veio (sem inventar UTC)", date_as_is);
        check("mensagem de servico preservada", service_kept);
        check("resposta a mensagem anterior preservada", reply_kept);
    }

    // 4d. Takeout completo: Fotos (com sidecar), Drive e Contatos.
    {
        olho::social::dyi_config config;
        config.source = L".oracle\\takeout_full";
        config.case_dir = case_dir.string();
        // Conta diferente do teste de mbox: a pasta do provedor pertence a um job,
        // e reutilizar a mesma conta faria o importador recusar (por desenho).
        config.account = "takeout2@exemplo.com";
        config.legal_basis = "OFICIO-31/2026";
        config.job_id = "job-takeout-full";
        const auto report = olho::social::import_export(config);
        check("importa takeout completo", report.ok);
        if (!report.ok) std::printf("    erro: %s\n", report.error.c_str());
        check("uma foto do google fotos", report.media_files == 1);
        const auto manifest = nlohmann::json::parse(read_all(fs::path(report.manifest_path)),
                                                    nullptr, false);
        check("um arquivo do drive", manifest.value("drive_files", -1) == 1);
        check("dois contatos", manifest.value("contacts", -1) == 2);
        const std::string timeline = read_all(fs::path(report.timeline_path));
        bool photo_ok = false;
        bool contacts_ok = 0;
        int contact_events = 0;
        std::size_t start = 0;
        while (start < timeline.size()) {
            const std::size_t end = timeline.find(char(10), start);
            if (end == std::string::npos) break;
            const auto event = nlohmann::json::parse(timeline.substr(start, end - start), nullptr, false);
            if (event.is_object()) {
                const std::string channel = event.value("channel", "");
                if (channel == "Google/Photos" &&
                    event.value("timestamp_utc", "") == "2023-09-29T15:06:40Z") {
                    photo_ok = true;
                }
                if (channel == "Google/Contacts") {
                    ++contact_events;
                    if (event["event_data"].value("email", "") == "ana@example.com") contacts_ok = true;
                }
            }
            start = end + 1;
        }
        check("foto com carimbo do sidecar", photo_ok);
        check("dois eventos de contato", contact_events == 2);
        check("email do contato preservado", contacts_ok);
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
