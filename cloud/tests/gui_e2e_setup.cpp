// E2E fixture: local admin identity + DPAPI test token.
#include "cloud/core/dpapi_token_vault.hpp"
#include "cloud/enterprise/identity.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: gui_e2e_setup <identity> <vault-key>\n";
        return 2;
    }
    deep::cloud::LocalIdentityStore store(argv[1]);
    std::string error;
    if (!store.load(error)) {
        std::cerr << "identity load: " << error << "\n";
        return 3;
    }
    if (store.users().empty()) {
        if (!store.create_user("admin", "Admin E2E", deep::cloud::Role::Admin,
                               "TestPass12345!", error)) {
            std::cerr << "identity create: " << error << "\n";
            return 4;
        }
        if (!store.save(error)) {
            std::cerr << "identity save: " << error << "\n";
            return 5;
        }
    }
    deep::cloud::DpapiTokenVault vault;
    deep::cloud::OAuthToken token;
    token.access_token = "gui-e2e-token";
    token.refresh_token = "gui-e2e-refresh";
    token.expires_in = 3600;
    token.reset_expiry();
    if (!vault.save(argv[2], token)) {
        std::cerr << "token save failed\n";
        return 6;
    }
    std::cout << "identity=" << argv[1] << "\n";
    return 0;
}
