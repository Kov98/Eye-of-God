// language: C++, file: .oracle/timeline_export_test.cpp
#include "case_timeline_export.hpp"
#include "cloud/core/cloud_core.hpp"
#include "cloud/db/cloud_db_import.hpp"
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
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
static void write_all(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

int main() {
    std::error_code ignore;
    fs::remove_all(".oracle/tlx_case", ignore);

    const fs::path case_dir = ".oracle/tlx_case/caso-1";
    const fs::path account_dir = case_dir / "social" / "telegram" / "acct";
    fs::create_directories(account_dir / "raw", ignore);
    const std::string db_path = (case_dir / "case.db").string();

    // Titulo com virgula e aspas: e o que quebra CSV mal escrito.
    const std::string payload = "{\"messages\":[]}";
    write_all(account_dir / "raw" / "1-result.json", payload);

    nlohmann::json manifest = {
        {"provider", "telegram"},
        {"account", "kai@exemplo.com"},
        {"legal_basis", "OFICIO-1"},
        {"job_id", "job-tlx"},
        {"status", "complete"},
        {"files", nlohmann::json::array()}};
    manifest["files"].push_back({{"name", "raw/1-result.json"},
                                 {"size_bytes", payload.size()},
                                 {"sha256", deep::cloud::sha256_hex(payload)},
                                 {"source", "result.json"}});
    write_all(account_dir / "manifest.json", manifest.dump(2));
    write_all(account_dir / "timeline.jsonl",
              R"({"channel":"Telegram/Messages","event_id":"chat#1",)"
              R"("timestamp_utc":"2023-11-14T19:13:00","event_data":{"sender":"Ana",)"
              R"("text":"oi, tudo \"bem\"?"}})" "\n");

    const auto imported = deep::cloud::import_cloud_case(case_dir.string(), db_path);
    check("banco do caso montado", imported.ok);
    if (!imported.ok) std::printf("    erro: %s\n", imported.error.c_str());

    // CSV
    const auto csv = deep::forensic::export_case_timeline(db_path, ".oracle/tlx.csv", "csv");
    check("exporta csv", csv.ok);
    if (!csv.ok) std::printf("    erro: %s\n", csv.error.c_str());
        // Duas linhas: a mensagem da timeline e o proprio arquivo de evidencia
    // (raw_page). O export mostra tudo o que esta gravado, nao so o que a tela
    // pinta na aba Timeline.
    check("duas linhas exportadas", csv.rows == 2);
    const std::string text = read_all(".oracle/tlx.csv");
    check("cabecalho presente",
          text.rfind("timestamp_utc,object_type,channel,provider,account,title,external_id,"
                     "size_bytes,sha256,storage_path", 0) == 0);
    check("carimbo do evento, nao o da importacao",
          text.find("2023-11-14T19:13:00") != std::string::npos);
    check("canal do telegram", text.find("Telegram/Messages") != std::string::npos);
    check("sha256 do artefato na linha",
          text.find(deep::cloud::sha256_hex(payload)) != std::string::npos);
    check("caminho do artefato na linha",
          text.find("social/telegram/acct/raw/1-result.json") != std::string::npos);
    check("virgula nao quebrou a coluna",
          text.find("oi, tudo") == std::string::npos ||
              text.find("\"") != std::string::npos);
    // Conta as colunas da linha de dados respeitando aspas.
    // A linha do evento e a que comeca com o carimbo de 2023 (a outra e o arquivo).
    std::string data_line;
    std::size_t line_start = text.find('\n') + 1;
    while (line_start < text.size()) {
        std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string::npos) line_end = text.size();
        const std::string candidate = text.substr(line_start, line_end - line_start);
        if (candidate.rfind("2023-11-14T19:13:00", 0) == 0) {
            data_line = candidate;
            break;
        }
        line_start = line_end + 1;
    }
    int columns = 1;
    bool in_quotes = false;
    for (std::size_t i = 0; i < data_line.size(); ++i) {
        const char c = data_line[i];
        if (c == '"') {
            if (in_quotes && i + 1 < data_line.size() && data_line[i + 1] == '"') { ++i; continue; }
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            ++columns;
        }
    }
    check("dez colunas na linha de dados", columns == 10);

    // JSON
    const auto json = deep::forensic::export_case_timeline(db_path, ".oracle/tlx.json", "json");
    check("exporta json", json.ok);
    const auto parsed = nlohmann::json::parse(read_all(".oracle/tlx.json"), nullptr, false);
    check("json legivel", parsed.is_array() && parsed.size() == 2);
    if (parsed.is_array() && parsed.size() == 2) {
        bool found_event = false;
        bool artifact_hash = false;
        for (const auto& entry : parsed) {
            if (entry.value("sha256", "").size() == 64) artifact_hash = true;
            if (entry.value("timestamp_utc", "") != "2023-11-14T19:13:00") continue;
            found_event = true;
            check("json tem canal", entry.value("channel", "") == "Telegram/Messages");
            check("json tem provedor", entry.value("provider", "") == "telegram");
        }
        check("evento presente no json", found_event);
        check("artefato com sha256 no json", artifact_hash);
    }

    // Banco vazio e recusado.
    fs::create_directories(".oracle/tlx_empty", ignore);
    const auto empty = deep::forensic::export_case_timeline(".oracle/tlx_empty/vazio.db",
                                                            ".oracle/tlx_vazio.csv", "csv");
    check("banco inexistente recusado", !empty.ok);

    std::printf(failures == 0 ? "TIMELINE EXPORT OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
