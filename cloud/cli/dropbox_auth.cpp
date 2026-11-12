// language: C++, file: cloud/cli/dropbox_auth.cpp
#include "cloud/cli/dropbox_auth.hpp"

#include "cloud/connectors/dropbox/dropbox_http.hpp"
#include "cloud/core/cloud_core.hpp"
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

}  // namespace

int run_dropbox_auth(const DropboxAuthOptions& options) {
    if (options.app_key.empty()) {
        std::cerr << "dropbox-auth: --app-key is required\n";
        return 2;
    }
    OAuthCallbackServer callback;
    std::string callback_error;
    if (!callback.start(callback_error)) {
        std::cerr << "dropbox-auth: callback error: " << callback_error << "\n";
        return 1;
    }
    const std::string redirect_uri = callback.redirect_uri();

    const PkcePair pkce = generate_pkce();
    const std::string state = random_urlsafe(24);
    const std::string url = olho::cloud::dropbox::authorize_url(
        options.app_key, redirect_uri, pkce.challenge, state);
    if (pkce.verifier.empty() || pkce.challenge.empty() || state.empty() || url.empty()) {
        std::cerr << "dropbox-auth: could not build the authorization request\n";
        callback.stop();
        return 1;
    }

    std::cout << "authorize_url: " << url << "\n" << std::flush;
    if (!options.no_browser) {
        const HINSTANCE opened = ShellExecuteW(nullptr, L"open", widen(url).c_str(),
                                               nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<std::intptr_t>(opened) <= 32) {
            std::cout << "browser_open_failed: open the URL above manually\n";
        }
    }

    const int timeout = options.timeout_seconds > 0 ? options.timeout_seconds : 300;
    const CallbackResult result = callback.wait(std::chrono::seconds(timeout));
    callback.stop();
    if (result.timed_out) {
        std::cerr << "dropbox-auth: timed out waiting for the callback\n";
        return 3;
    }
    if (!result.error.empty()) {
        std::cerr << "dropbox-auth: oauth error: " << result.error << "\n";
        return 3;
    }
    if (result.state != state) {
        // A code that arrives with the wrong state is discarded, never redeemed.
        std::cerr << "dropbox-auth: state mismatch; code discarded\n";
        return 3;
    }

    WinHttpTransport transport(L"OlhoDeDeus-Cloud-Auth/0.1", options.mock_http);
    RetryPolicy policy;
    policy.max_attempts = 3;
    HttpClient http(transport, policy);
    olho::cloud::dropbox::client api(http, std::string());
    olho::cloud::dropbox::token_set tokens;
    std::string exchange_error;
    if (!api.exchange_code(options.app_key, result.code, pkce.verifier, redirect_uri, tokens,
                           exchange_error)) {
        std::cerr << "dropbox-auth: token error: " << exchange_error << "\n";
        return 4;
    }

    OAuthToken token;
    token.access_token = tokens.access_token;
    token.refresh_token = tokens.refresh_token;
    token.token_type = tokens.token_type.empty() ? "Bearer" : tokens.token_type;
    token.expires_in = static_cast<int>(tokens.expires_in == 0 ? 14400 : tokens.expires_in);
    token.obtained_at = Clock::now();

    DpapiTokenVault vault;
    const std::string key = options.vault_key.empty() ? "dropbox/default" : options.vault_key;
    if (!vault.save(key, token)) {
        std::cerr << "dropbox-auth: could not save the token in the DPAPI vault\n";
        return 5;
    }
    std::cout << "dropbox-auth: success\n"
              << "vault_key: " << key << "\n"
              << "token_type: " << token.token_type << "\n"
              << "expires_in: " << token.expires_in << "\n"
              << "refresh_token: "
              << (token.refresh_token.empty() ? "absent" : "present") << "\n";
    return 0;
}

}  // namespace deep::cloud
