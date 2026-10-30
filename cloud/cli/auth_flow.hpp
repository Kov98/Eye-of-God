#pragma once

#include <string>

namespace deep::cloud {

struct AuthFlowOptions {
    std::string tenant;
    std::string client_id;
    std::string scope = "User.Read Mail.Read Files.Read.All";
    std::string vault_key;
    std::string authorize_endpoint;
    std::string token_endpoint;
    int timeout_seconds = 300;
    bool no_browser = false;
    bool mock_http = false;
};

// Runs the interactive PKCE flow, validates state, exchanges the code and
// stores the token in DPAPI. Returns a process-style exit code.
int run_auth_flow(const AuthFlowOptions& options);

} // namespace deep::cloud
