// Phase 1 interactive auth CLI. Shared implementation lives in auth_flow.cpp
// so the main product binary can expose --cloud-m365-auth without duplication.

#include "cloud/cli/auth_flow.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace {

std::string argument(int argc, char** argv, const std::string& name,
                     const std::string& fallback = {}) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (name == argv[i]) return argv[i + 1];
    }
    return fallback;
}

bool present(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) if (name == argv[i]) return true;
    return false;
}

void usage() {
    std::cout
        << "cloud_auth --tenant <id> --client-id <id> [options]\n"
        << "  --scope <scopes>          default: User.Read Mail.Read Files.Read.All\n"
        << "  --vault-key <key>         default: <tenant>/default\n"
        << "  --timeout <seconds>       default: 300\n"
        << "  --no-browser              print URL and wait\n"
        << "  --mock-http               allow http (mock endpoints only)\n"
        << "  --authorize-endpoint URL  override for mock/testing\n"
        << "  --token-endpoint URL      override for mock/testing\n";
}

} // namespace

int main(int argc, char** argv) {
    if (present(argc, argv, "--help") || present(argc, argv, "-h")) {
        usage();
        return 0;
    }
    deep::cloud::AuthFlowOptions options;
    options.tenant = argument(argc, argv, "--tenant");
    options.client_id = argument(argc, argv, "--client-id");
    options.scope = argument(argc, argv, "--scope",
        "User.Read Mail.Read Files.Read.All");
    options.vault_key = argument(argc, argv, "--vault-key");
    options.authorize_endpoint = argument(argc, argv, "--authorize-endpoint");
    options.token_endpoint = argument(argc, argv, "--token-endpoint");
    options.no_browser = present(argc, argv, "--no-browser");
    options.mock_http = present(argc, argv, "--mock-http");
    if (options.tenant.empty() || options.client_id.empty()) {
        usage();
        std::cerr << "error: --tenant and --client-id are required\n";
        return 2;
    }
    try {
        options.timeout_seconds = std::max(30,
            std::stoi(argument(argc, argv, "--timeout", "300")));
    } catch (...) {
        options.timeout_seconds = 300;
    }
    return deep::cloud::run_auth_flow(options);
}
