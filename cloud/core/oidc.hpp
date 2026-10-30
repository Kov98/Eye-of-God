#pragma once

#include "cloud_core.hpp"
#include "rbac.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deep::cloud {

struct OidcConfiguration {
    std::string issuer;
    std::string authorization_endpoint;
    std::string token_endpoint;
    std::string jwks_uri;
};

struct OidcDiscoveryResult {
    bool ok = false;
    OidcConfiguration configuration;
    std::string error;
};

OidcDiscoveryResult discover_oidc(HttpClient& client, std::string_view issuer);

struct JwtClaims {
    std::string subject;
    std::string email;
    std::string issuer;
    std::string audience;
    std::string nonce;
    std::vector<std::string> roles;
    std::int64_t expires_at = 0;
};

struct JwtResult {
    bool ok = false;
    JwtClaims claims;
    std::string error;
};

// Verifies an RS256 JWT against a JWKS document. Validation covers signature,
// alg, kid, issuer, audience, exp and nbf. No caller-supplied algorithm is
// accepted.
JwtResult verify_rs256_token(std::string_view token, std::string_view jwks_json,
                             std::string_view expected_issuer,
                             std::string_view expected_audience,
                             int clock_skew_seconds = 60);

JwtResult verify_oidc_token(HttpClient& client, const OidcConfiguration& configuration,
                            std::string_view token, std::string_view expected_audience);

Principal principal_from_oidc(const JwtClaims& claims, Role role);
Role oidc_role(const JwtClaims& claims, Role fallback = Role::Reviewer);

} // namespace deep::cloud
