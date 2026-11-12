// language: C++, file: .oracle/dropbox_flow_test.cpp
// Prova o fluxo Dropbox com o transporte falso: validacao, mapeamento de
// configuracao e os codigos de saida que o produto devolve.
#include "cloud/cli/dropbox_flow.hpp"
#include "cloud/connectors/dropbox/dropbox_client.hpp"
#include "cloud/connectors/dropbox/dropbox_collector.hpp"
#include "cloud/connectors/dropbox/dropbox_http.hpp"
#include "cloud/core/cloud_core.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using deep::cloud::CollectFlowOptions;
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
    return response;
}

static const char* kPage = R"({"entries":[
 {".tag":"file","name":"contrato.pdf","path_lower":"/caso/contrato.pdf","id":"id:1",
  "size":9,"server_modified":"2026-10-01T12:00:00Z"},
 {".tag":"folder","name":"anexos","path_lower":"/caso/anexos","id":"id:2"}],
 "cursor":"C1","has_more":false})";

static CollectFlowOptions base_options(const fs::path& case_dir) {
    CollectFlowOptions options;
    options.provider = "dropbox";
    options.case_dir = case_dir.string();
    options.account = "kai@exemplo.com";
    options.vault_key = "vault:dropbox:kai";
    options.legal_basis = "OFICIO-77/2026";
    options.job = "job-flow";
    options.root_path = "/caso";
    return options;
}

int main() {
    const fs::path base = ".oracle/case_dropbox_flow/caso-1";
    std::error_code ignored;
    fs::remove_all(".oracle/case_dropbox_flow", ignored);

    // 1. Argumento obrigatorio faltando: recusa antes de qualquer pedido.
    {
        MockHttpTransport transport;
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token");
        CollectFlowOptions options = base_options(base);
        options.legal_basis.clear();
        const auto flow = deep::cloud::run_dropbox_collect(api, options, {});
        check("sem base legal -> codigo 2", flow.exit_code == 2);
        check("sem base legal -> nenhum pedido", transport.requests().empty());
        check("motivo nomeia o campo", flow.outcome.error.find("legal-basis") != std::string::npos);
    }

    // 2. Coleta completa: codigo 0 e campos do resultado preenchidos.
    {
        MockHttpTransport transport;
        transport.push(ok_body(kPage));
        MockDownloadSpec spec;
        spec.status = 200;
        spec.chunks.push_back("%PDF-1.4\n");
        transport.push_download(spec);

        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token");
        CollectFlowOptions options = base_options(base);
        options.download_files = true;
        options.max_file_bytes = 100ull * 1024 * 1024 * 1024;  // 100 GiB: acima do teto
        const auto flow = deep::cloud::run_dropbox_collect(api, options, {});
        check("coleta completa -> codigo 0", flow.exit_code == 0);
        check("ok e nao parcial", flow.outcome.ok && !flow.outcome.partial);
        check("duas entradas", flow.outcome.objects == 2);
        check("uma pagina", flow.outcome.pages == 1);
        check("um arquivo baixado", flow.outcome.files_downloaded == 1);
        check("nove bytes", flow.outcome.bytes_downloaded == 9);
        check("manifesto no resultado",
              flow.outcome.manifest_path.find("manifest.json") != std::string::npos);
        check("nada de erro", flow.outcome.error.empty());

        // Limite gigante na opcao precisa ser aparado no maximo duro.
        check("limite aparado",
              !transport.download_requests().empty() &&
                  transport.download_requests()[0].max_bytes ==
                      olho::cloud::dropbox::kHardMaxFileBytes);
    }

    // 3. Limite zero usa o padrao, nao "ilimitado".
    {
        MockHttpTransport transport;
        transport.push(ok_body(kPage));
        MockDownloadSpec spec;
        spec.status = 200;
        spec.chunks.push_back("x");
        transport.push_download(spec);
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token");
        CollectFlowOptions options = base_options(base);
        options.job = "job-flow-default";
        options.download_files = true;
        options.max_file_bytes = 0;
        deep::cloud::run_dropbox_collect(api, options, {});
        check("limite zero vira o padrao",
              !transport.download_requests().empty() &&
                  transport.download_requests()[0].max_bytes ==
                      olho::cloud::dropbox::kDefaultMaxFileBytes);
    }

    // 4. Cancelamento: codigo 6, marcado como cancelado.
    {
        MockHttpTransport transport;
        transport.push(ok_body(kPage));
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token");
        CollectFlowOptions options = base_options(base);
        options.job = "job-flow-cancel";
        int calls = 0;
        const auto flow = deep::cloud::run_dropbox_collect(
            api, options, [&calls]() { return ++calls > 2; });
        check("cancelado -> codigo 6", flow.exit_code == 6);
        check("cancelado marcado", flow.outcome.cancelled);
        check("cancelado nao e completa", !flow.outcome.ok);
    }

    // 5. Orcamento de paginas: parcial, codigo 6, cursor guardado.
    {
        MockHttpTransport transport;
        transport.push(ok_body(R"({"entries":[{".tag":"folder","name":"a","path_lower":"/caso/a","id":"id:9"}],
                                "cursor":"C9","has_more":true})"));
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token");
        CollectFlowOptions options = base_options(base);
        options.job = "job-flow-budget";
        options.max_pages = 1;
        const auto flow = deep::cloud::run_dropbox_collect(api, options, {});
        check("orcamento -> codigo 6", flow.exit_code == 6);
        check("orcamento parcial", flow.outcome.partial && !flow.outcome.cancelled);
        check("orcamento deixa motivo",
              flow.outcome.error.find("budget") != std::string::npos);
    }

    // 6. Falha da API: codigo 5, com o status no motivo.
    {
        MockHttpTransport transport;
        HttpResponse denied;
        denied.status = 401;
        denied.body = R"({"error_summary":"expired_access_token/"})";
        transport.push(denied);
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "token");
        CollectFlowOptions options = base_options(base);
        options.job = "job-flow-401";
        const auto flow = deep::cloud::run_dropbox_collect(api, options, {});
        check("401 -> codigo 5", flow.exit_code == 5);
        check("401 nao e parcial", !flow.outcome.partial);
        check("motivo cita o status", flow.outcome.error.find("401") != std::string::npos);
        check("motivo cita o token",
              flow.outcome.error.find("expired_access_token") != std::string::npos);
        const std::string journal = ".oracle/case_dropbox_flow/caso-1/cloud/jobs/job-flow-401.json";
        check("diario diz failed, nao complete", [&] {
            std::ifstream in(journal, std::ios::binary);
            if (!in) return false;
            const auto doc = nlohmann::json::parse(
                std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()),
                nullptr, false);
            return doc.is_object() && doc.value("status", "") == "failed";
        }());
    }

    // 7. Troca de codigo e renovacao: o corpo do formulario e o alvo do pedido.
    {
        MockHttpTransport transport;
        transport.push(ok_body(R"({"access_token":"sl.NOVO","refresh_token":"rt.NOVO",)"
                               R"("token_type":"bearer","expires_in":14400,"account_id":"dbid:1"})"));
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "");
        olho::cloud::dropbox::token_set tokens;
        std::string error;
        const bool ok = api.exchange_code("app-key", "CODE-1", "VERIFIER-1",
                                          "http://127.0.0.1:8080/cb", tokens, error);
        check("troca de codigo ok", ok);
        check("access token recebido", tokens.access_token == "sl.NOVO");
        check("refresh token recebido", tokens.refresh_token == "rt.NOVO");
        check("validade recebida", tokens.expires_in == 14400);
        const auto& request = transport.requests().at(0);
        check("endpoint do token",
              request.url == "https://api.dropboxapi.com/oauth2/token");
        check("corpo urlencoded",
              deep::cloud::find_header_value(request.headers, "Content-Type").find(
                  "x-www-form-urlencoded") != std::string::npos);
        for (const char* needle : {"grant_type=authorization_code", "code=CODE-1",
                                   "code_verifier=VERIFIER-1", "client_id=app-key",
                                   "redirect_uri=http%3A%2F%2F127.0.0.1%3A8080%2Fcb"}) {
            check(needle, request.body.find(needle) != std::string::npos);
        }
    }

    {
        MockHttpTransport transport;
        HttpResponse denied;
        denied.status = 400;
        denied.body = R"({"error":"invalid_grant","error_description":"code expired"})";
        transport.push(denied);
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "");
        olho::cloud::dropbox::token_set tokens;
        std::string error;
        check("codigo recusado", !api.exchange_code("app", "C", "V", "http://127.0.0.1/cb",
                                                   tokens, error));
        check("motivo legivel de recusa", error.find("invalid_grant") != std::string::npos);
    }

    {
        MockHttpTransport transport;
        transport.push(ok_body(R"({"access_token":"sl.RENOVADO","expires_in":14400,"token_type":"bearer"})"));
        HttpClient http(transport);
        olho::cloud::dropbox::client api(http, "");
        olho::cloud::dropbox::token_set tokens;
        std::string error;
        check("renovacao ok", api.refresh_access_token("app-key", "rt.ANTIGO", tokens, error));
        check("token renovado", tokens.access_token == "sl.RENOVADO");
        const auto& body = transport.requests().at(0).body;
        check("grant de renovacao", body.find("grant_type=refresh_token") != std::string::npos);
        check("refresh token enviado", body.find("refresh_token=rt.ANTIGO") != std::string::npos);
    }

    std::printf(failures == 0 ? "DROPBOX FLOW OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
