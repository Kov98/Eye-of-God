#pragma once

#include "cloud_core.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace deep::cloud {

// RFC 7636 PKCE. The verifier is 43 URL-safe characters derived from 32
// random bytes; the challenge is BASE64URL(SHA256(verifier)).
struct PkcePair {
    std::string verifier;
    std::string challenge;
};

std::string random_urlsafe(std::size_t bytes = 32);
PkcePair generate_pkce();
std::string pkce_challenge(std::string_view verifier);
std::string url_encode(std::string_view value);

struct OAuthConfig {
    std::string authorization_endpoint;
    std::string token_endpoint;
    std::string client_id;
    std::string redirect_uri;
    std::string scope;
    std::string tenant;
};

std::string build_authorize_url(const OAuthConfig& config,
                                std::string_view state,
                                std::string_view challenge);

struct OAuthTokenResult {
    std::optional<OAuthToken> token;
    std::string error;
    int status = 0;
};

// Authorization-code exchange and refresh are deliberately single-shot
// against the transport: an authorization code is single-use, so blind retry
// can turn a transient failure into a confusing "invalid_grant". Job-level
// retry is handled by the caller with a checkpoint.
OAuthTokenResult exchange_authorization_code(HttpTransport& transport,
                                             const OAuthConfig& config,
                                             std::string_view code,
                                             std::string_view verifier);
OAuthTokenResult refresh_access_token(HttpTransport& transport,
                                      const OAuthConfig& config,
                                      std::string_view refresh_token);

} // namespace deep::cloud
