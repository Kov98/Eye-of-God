#include "auth_flow.hpp"

#include "cloud/core/dpapi_token_vault.hpp"
#include "cloud/core/oauth_callback.hpp"
#include "cloud/core/oauth_pkce.hpp"
#include "cloud/core/winhttp_transport.hpp"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace deep::cloud {
namespace {

std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}

} // namespace

int run_auth_flow(const AuthFlowOptions& options) {
    if (options.client_id.empty() || options.tenant.empty()) {
        std::cerr << "auth: --tenant and --client-id are required\n";
        return 2;
    }
    OAuthCallbackServer callback;
    std::string callback_error;
    if (!callback.start(callback_error)) {
        std::cerr << "auth: callback error: " << callback_error << "\n";
        return 1;
    }

    OAuthConfig config;
    config.client_id = options.client_id;
    config.redirect_uri = callback.redirect_uri();
    config.scope = options.scope;
    config.tenant = options.tenant;
    config.authorization_endpoint = options.authorize_endpoint.empty()
        ? "https://login.microsoftonline.com/" + options.tenant +
            "/oauth2/v2.0/authorize"
        : options.authorize_endpoint;
    config.token_endpoint = options.token_endpoint.empty()
        ? "https://login.microsoftonline.com/" + options.tenant +
            "/oauth2/v2.0/token"
        : options.token_endpoint;

    const PkcePair pkce = generate_pkce();
    const std::string state = random_urlsafe(24);
    const std::string authorize_url = build_authorize_url(config, state, pkce.challenge);
    if (pkce.verifier.empty() || pkce.challenge.empty() || state.empty() ||
        authorize_url.empty()) {
        std::cerr << "auth: could not build PKCE authorization request\n";
        return 1;
    }

    std::cout << "authorize_url: " << authorize_url << "\n" << std::flush;
    if (!options.no_browser) {
        const HINSTANCE opened = ShellExecuteW(nullptr, L"open",
            widen(authorize_url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<std::intptr_t>(opened) <= 32) {
            std::cout << "browser_open_failed: open the URL above manually\n";
        }
    }

    const int timeout = options.timeout_seconds > 0 ? options.timeout_seconds : 300;
    const CallbackResult result = callback.wait(std::chrono::seconds(timeout));
    callback.stop();
    if (result.timed_out) {
        std::cerr << "auth: timed out waiting for callback\n";
        return 3;
    }
    if (!result.error.empty()) {
        std::cerr << "auth: oauth error: " << result.error << "\n";
        return 3;
    }
    if (result.state != state) {
        std::cerr << "auth: state mismatch; code discarded\n";
        return 3;
    }

    WinHttpTransport transport(L"OlhoDeDeus-Cloud-Auth/0.1", options.mock_http);
    const OAuthTokenResult token = exchange_authorization_code(
        transport, config, result.code, pkce.verifier);
    if (!token.token) {
        std::cerr << "auth: token error: " << token.error << "\n";
        return 4;
    }

    DpapiTokenVault vault;
    const std::string key = options.vault_key.empty()
        ? options.tenant + "/default" : options.vault_key;
    if (!vault.save(key, *token.token)) {
        std::cerr << "auth: could not save token in DPAPI vault\n";
        return 5;
    }
    std::cout << "auth: success\n"
              << "vault_key: " << key << "\n"
              << "token_type: " << token.token->token_type << "\n"
              << "expires_in: " << token.token->expires_in << "\n";
    return 0;
}

} // namespace deep::cloud
