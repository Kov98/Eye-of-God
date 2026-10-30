#include "cloud/service/api.hpp"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "third_party/httplib.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::string default_local_path(const char* file) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) return std::string(".\\") + file;
    const std::wstring root = std::wstring(buffer) + L"\\OlhoDeDeus\\";
    const std::wstring path = root + std::wstring(file, file + std::strlen(file));
    const int needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0,
                                           nullptr, nullptr);
    if (needed <= 0) return std::string(".\\") + file;
    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

void usage() {
    std::cout
        << "OlhoDeDeus cloud service (loopback only)\n"
        << "  --case DIR       case directory (required)\n"
        << "  --port N         loopback port (default 8766)\n"
        << "  --identity FILE  local identity store\n"
        << "  --audit FILE     audit JSONL path\n"
        << "  --help\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string case_dir;
    std::string identity = default_local_path("identity.json");
    std::string audit = default_local_path("audit.jsonl");
    std::string tls_cert;
    std::string tls_key;
    int port = 8766;
    int rate_limit = 120;
    std::string oidc_issuer;
    std::string oidc_client_id;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--case" && i + 1 < argc) case_dir = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--identity" && i + 1 < argc) identity = argv[++i];
        else if (arg == "--audit" && i + 1 < argc) audit = argv[++i];
        else if (arg == "--tls-cert" && i + 1 < argc) tls_cert = argv[++i];
        else if (arg == "--tls-key" && i + 1 < argc) tls_key = argv[++i];
        else if (arg == "--rate-limit" && i + 1 < argc) rate_limit = std::atoi(argv[++i]);
        else if (arg == "--oidc-issuer" && i + 1 < argc) oidc_issuer = argv[++i];
        else if (arg == "--oidc-client-id" && i + 1 < argc) oidc_client_id = argv[++i];
        else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }
    if (case_dir.empty()) {
        usage();
        return 2;
    }
    if (port < 1 || port > 65535) return 2;
    if (oidc_issuer.empty() != oidc_client_id.empty()) {
        std::cerr << "--oidc-issuer and --oidc-client-id must be provided together\\n";
        return 2;
    }
    if (rate_limit < 1) rate_limit = 1;
    if (rate_limit > 10000) rate_limit = 10000;
    if (tls_cert.empty() != tls_key.empty()) {
        std::cerr << "--tls-cert and --tls-key must be provided together\\n";
        return 2;
    }

    deep::cloud::CloudApiService service(case_dir, identity, audit, rate_limit);
    if (!oidc_issuer.empty())
        service.set_oidc_configuration(oidc_issuer, oidc_client_id);
    std::unique_ptr<httplib::Server> server;
    if (!tls_cert.empty()) {
        auto tls = std::make_unique<httplib::SSLServer>(tls_cert.c_str(), tls_key.c_str());
        if (!tls->is_valid()) {
            std::cerr << "invalid TLS certificate/key\\n";
            return 3;
        }
        server = std::move(tls);
    } else {
        server = std::make_unique<httplib::Server>();
    }
    server->set_payload_max_length(1024 * 1024);

    auto dispatch = [&service](const httplib::Request& request, httplib::Response& response) {
        deep::cloud::ApiRequest api;
        api.method = request.method;
        api.path = request.path;
        api.authorization = request.get_header_value("Authorization");
        api.body = request.body;
        const auto result = service.handle(api);
        response.status = result.status;
        response.set_content(result.body, result.content_type.c_str());
    };

    server->Get("/health", dispatch);
    server->Get("/v1/config", dispatch);
    server->Get("/v1/jobs", dispatch);
    server->Post("/v1/session", dispatch);
    server->Post("/v1/session/oidc", dispatch);
    server->Delete("/v1/session", dispatch);
    server->Get("/v1/audit", dispatch);
    server->Post("/v1/verify", dispatch);
    server->Post("/v1/jobs", dispatch);
    server->Post("/v1/queue", dispatch);
    server->Get("/v1/queue", dispatch);
    server->Post(R"(/v1/queue/.*)", dispatch);
    server->Get("/v1/metrics", dispatch);
    server->Get("/v1/metrics/prometheus", dispatch);
    server->Delete(R"(/v1/queue/.*)", dispatch);
    if (!server->bind_to_port("127.0.0.1", port)) {
        std::cerr << "could not bind 127.0.0.1:" << port << "\n";
        return 3;
    }
    std::cout << "cloud service listening on http://127.0.0.1:" << port << "\n";
    std::cout.flush();
    server->listen_after_bind();
    return 0;
}
