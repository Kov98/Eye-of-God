// language: C++, file: .oracle/dropbox_collect_test.cpp
// Ponta a ponta do conector Dropbox sobre o transporte falso: listagem paginada,
// checkpoint de cursor, retomada, cancelamento e o que fica gravado em disco.
#include "cloud/connectors/dropbox/dropbox_collector.hpp"
#include "cloud/connectors/dropbox/dropbox_http.hpp"
#include "cloud/core/cloud_core.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using deep::cloud::HttpClient;
using deep::cloud::HttpResponse;
using deep::cloud::MockDownloadSpec;
using deep::cloud::MockHttpTransport;

static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++failures;
}

static HttpResponse ok_body(std::string body) {
    HttpResponse response;
    response.status = 200;
    response.body = std::move(body);
    response.headers.emplace_back("Content-Type", "application/json");
    return response;
}

static std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static const char* kPage1 = R"({"entries":[
 {".tag":"file","name":"contrato.pdf","path_lower":"/caso/contrato.pdf","id":"id:1",
  "size":11,"server_modified":"2026-10-01T12:00:00Z","content_hash":"ch1"},
 {".tag":"folder","name":"anexos","path_lower":"/caso/anexos","id":"id:2"},
 {".tag":"file","name":"foto.jpg","path_lower":"/caso/foto.jpg","id":"id:3",
  "size":7,"server_modified":"2026-10-02T08:30:00Z"}],
 "cursor":"C1","has_more":true})";

static const char* kPage2 = R"({"entries":[
 {".tag":"file","name":"planilha.xlsx","path_lower":"/caso/planilha.xlsx","id":"id:4",
  "size":5,"server_modified":"2026-10-03T09:00:00Z"}],
 "cursor":"C2","has_more":false})";

int main() {
    const fs::path base = ".oracle/case_dropbox/caso-1";
    std::error_code ignored;
    fs::remove_all(".oracle/case_dropbox", ignored);

    const std::string bytes_a = "%PDF-1.4\n";
    const std::string bytes_b = "JPEGDATA";
    const std::string bytes_c = "XLSX55";

    {
        MockHttpTransport transport;
        transport.push(ok_body(kPage1));
        transport.push(ok_body(kPage2));
        for (const std::string* payload : {&bytes_a, &bytes_b, &bytes_c}) {
            MockDownloadSpec spec;
            spec.status = 200;
            spec.headers.emplace_back("Content-Length", std::to_string(payload->size()));
            spec.chunks.push_back(*payload);
            transport.push_download(spec);
        }

        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token-abc");
        olho::cloud::dropbox::collect_config config;
        config.case_dir = base.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OFICIO-123/2026";
        config.job_id = "job-full";
        config.root_path = "/caso";
        config.download_files = true;
        // A GUI depende deste callback para nao ficar muda durante a coleta.
        int progress_calls = 0;
        int last_pages = -1;
        bool monotonic = true;
        int last_objects = 0;
        config.progress = [&](int pages, int objects, int files) {
            ++progress_calls;
            if (pages < last_pages || objects < last_objects) monotonic = false;
            last_pages = pages;
            last_objects = objects;
            (void)files;
        };

        const auto report = olho::cloud::dropbox::collect(api, config);
        check("coleta completa ok", report.ok && !report.partial && !report.cancelled);
        check("duas paginas", report.pages == 2);
        check("quatro eventos", report.objects == 4);
        check("tres arquivos baixados", report.files_downloaded == 3);
        check("bytes somados",
              report.bytes_downloaded == bytes_a.size() + bytes_b.size() + bytes_c.size());
        check("progresso reportado a cada pagina", progress_calls == 2);
        check("progresso so avanca", monotonic);
        check("progresso termina nas duas paginas", last_pages == 2);
        check("progresso conta os objetos", last_objects == 4);

        // Pedido de listagem: a segunda pagina precisa ter ido para /continue.
        // Os downloads tambem entram em requests(), entao a lista e filtrada:
        // o que interessa aqui e a ordem dos pedidos de listagem.
        std::vector<deep::cloud::HttpRequest> lists;
        for (const auto& request : transport.requests()) {
            if (request.url.find("list_folder") != std::string::npos) lists.push_back(request);
        }
        check("dois pedidos de listagem", lists.size() == 2);
        if (lists.size() == 2) {
            check("primeira pagina em list_folder",
                  lists[0].url == "https://api.dropboxapi.com/2/files/list_folder");
            check("segunda pagina em continue",
                  lists[1].url == "https://api.dropboxapi.com/2/files/list_folder/continue");
            check("continue carrega o cursor", lists[1].body.find("C1") != std::string::npos);
            check("token no cabecalho",
                  deep::cloud::find_header_value(lists[0].headers, "Authorization") ==
                      "Bearer token-abc");
        }

        const fs::path manifest_path = base / "cloud" / "dropbox" / "kai_exemplo.com" /
                                       "manifest.json";
        const std::string manifest_text = read_all(manifest_path);
        nlohmann::json manifest = nlohmann::json::parse(manifest_text, nullptr, false);
        check("manifesto legivel", manifest.is_object());
        check("provedor dropbox", manifest.value("provider", "") == "dropbox");
        check("status complete", manifest.value("status", "") == "complete");
        check("arquivos de pagina gravados", manifest["files"].size() == 2);

        // O corpo bruto precisa ser identico ao que o servidor mandou.
        bool raw_exact = true;
        for (const auto& file : manifest["files"]) {
            const fs::path raw = base / "cloud" / "dropbox" / "kai_exemplo.com" /
                                 file.value("name", "");
            const std::string content = read_all(raw);
            if (content.empty()) { raw_exact = false; break; }
            if (deep::cloud::sha256_hex(content) != file.value("sha256", "")) { raw_exact = false; break; }
            if (static_cast<std::uint64_t>(content.size()) != file.value("size_bytes", 0ull)) {
                raw_exact = false;
                break;
            }
        }
        check("sha256 de cada pagina confere", raw_exact);
        const std::string page1_on_disk = read_all(base / "cloud" / "dropbox" / "kai_exemplo.com" /
                                                   "raw" / "list-1.json");
        check("pagina 1 byte a byte", page1_on_disk == kPage1);

        check("tres downloads no manifesto", manifest["downloads"].size() == 3);
        if (manifest["downloads"].size() == 3) {
            const auto& first = manifest["downloads"][0];
            check("caminho de origem", first.value("path_lower", "") == "/caso/contrato.pdf");
            check("sha256 do conteudo",
                  first.value("sha256", "") == deep::cloud::sha256_hex(bytes_a));
            check("tamanho do conteudo", first.value("size_bytes", 0ull) == bytes_a.size());
        }

        // Linha do tempo: um objeto JSON por entrada, canal e carimbo corretos.
        const std::string timeline =
            read_all(base / "cloud" / "dropbox" / "kai_exemplo.com" / "timeline.jsonl");
        int lines = 0;
        bool timeline_ok = true;
        std::string first_timestamp;
        std::size_t start = 0;
        while (start < timeline.size()) {
            const std::size_t end = timeline.find('\n', start);
            if (end == std::string::npos) break;
            const auto event = nlohmann::json::parse(timeline.substr(start, end - start), nullptr, false);
            if (!event.is_object() || event.value("channel", "") != "Dropbox/Files" ||
                event.value("provider", "") != "Dropbox") {
                timeline_ok = false;
                break;
            }
            if (first_timestamp.empty()) first_timestamp = event.value("timestamp_utc", "");
            ++lines;
            start = end + 1;
        }
        check("quatro linhas na timeline", lines == 4);
        check("eventos bem formados", timeline_ok);
        check("carimbo do servidor preservado", first_timestamp == "2026-10-01T12:00:00Z");

        // Diario do trabalho: completo e com o cursor final.
        const std::string job = read_all(base / "cloud" / "jobs" / "job-full.json");
        const auto journal = nlohmann::json::parse(job, nullptr, false);
        check("diario legivel", journal.is_object());
        check("diario complete", journal.is_object() && journal.value("status", "") == "complete");
        check("cursor final gravado",
              journal.is_object() && journal.value("cursor", "") == "C2");

        // Repetir o mesmo trabalho nao pode recoletar nem sobrescrever.
        HttpClient http2(transport);
        olho::cloud::dropbox::client api2(http2, "token-abc");
        const auto again = olho::cloud::dropbox::collect(api2, config);
        check("trabalho concluido recusado", !again.ok);
        check("motivo claro",
              again.error.find("already complete") != std::string::npos);
    }

    // Retomada por orcamento de paginas.
    {
        MockHttpTransport transport;
        transport.push(ok_body(kPage1));
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token-abc");
        olho::cloud::dropbox::collect_config config;
        config.case_dir = base.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OFICIO-123/2026";
        config.job_id = "job-budget";
        config.root_path = "/caso";
        config.max_pages = 1;

        const auto partial = olho::cloud::dropbox::collect(api, config);
        check("orcamento interrompe", partial.pages == 1 && !partial.cancelled);
        check("marca parcial", partial.partial);
        const auto journal = nlohmann::json::parse(
            read_all(base / "cloud" / "jobs" / "job-budget.json"), nullptr, false);
        check("diario guarda o cursor", journal.is_object() && journal.value("cursor", "") == "C1");
        check("diario nao diz complete",
              journal.is_object() && journal.value("status", "") != "complete");

        MockHttpTransport second;
        second.push(ok_body(kPage2));
        HttpClient http2(second);
        olho::cloud::dropbox::client api2(http2, "token-abc");
        config.max_pages = 10;
        const auto resumed = olho::cloud::dropbox::collect(api2, config);
        check("retomada conclui", resumed.ok && !resumed.partial);
        check("retomada contada", resumed.resumed);
        check("paginas acumuladas", resumed.pages == 2);
        check("eventos acumulados", resumed.objects == 4);
        check("segundo pedido foi continue",
              !second.requests().empty() &&
                  second.requests()[0].url ==
                      "https://api.dropboxapi.com/2/files/list_folder/continue");
        check("retomada usou o cursor gravado",
              !second.requests().empty() &&
                  second.requests()[0].body.find("C1") != std::string::npos);
        const auto final_journal = nlohmann::json::parse(
            read_all(base / "cloud" / "jobs" / "job-budget.json"), nullptr, false);
        check("diario final complete",
              final_journal.is_object() && final_journal.value("status", "") == "complete");
    }

    // Cancelamento: para depois da primeira pagina e guarda o ponto.
    {
        MockHttpTransport transport;
        transport.push(ok_body(kPage1));
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token-abc");
        olho::cloud::dropbox::collect_config config;
        config.case_dir = base.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OFICIO-123/2026";
        config.job_id = "job-cancel";
        config.root_path = "/caso";
        config.max_pages = 10;
        int calls = 0;
        // Chamadas: 1 = checagem inicial, 2 = topo da primeira pagina, 3 = topo
        // da segunda. Cancelar na terceira para depois de uma pagina coletada.
        config.cancel_check = [&calls]() { return ++calls > 2; };

        const auto report = olho::cloud::dropbox::collect(api, config);
        check("cancelamento sinalizado", report.cancelled && report.partial);
        check("parou apos uma pagina", report.pages == 1);
        const auto journal = nlohmann::json::parse(
            read_all(base / "cloud" / "jobs" / "job-cancel.json"), nullptr, false);
        check("diario cancelled",
              journal.is_object() && journal.value("status", "") == "cancelled");
        check("cursor preservado para retomar",
              journal.is_object() && journal.value("cursor", "") == "C1");
    }

    // Caminho hostil nao escapa da area do caso.
    {
        MockHttpTransport transport;
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token-abc");
        olho::cloud::dropbox::collect_config config;
        config.case_dir = base.string();
        config.account = "kai@exemplo.com";
        config.legal_basis = "OFICIO-123/2026";
        config.job_id = "job-evil";
        config.root_path = "/caso/../../etc";
        const auto report = olho::cloud::dropbox::collect(api, config);
        check("raiz com .. recusada", !report.ok);
        check("nada listado", report.pages == 0);
        check("pedido nenhum", transport.requests().empty());
    }

    std::printf(failures == 0 ? "DROPBOX E2E OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
