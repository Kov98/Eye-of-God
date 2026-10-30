#include "oidc.hpp"

#include "cloud_core.hpp"
#include "third_party/nlohmann/json.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <vector>

namespace deep::cloud {
namespace {

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

std::vector<unsigned char> base64url_decode(std::string_view input) {
    std::vector<unsigned char> output;
    int buffer = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=') break;
        const int value = b64_value(c);
        if (value < 0) return {};
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<unsigned char>((buffer >> bits) & 0xff));
        }
    }
    return output;
}

bool split_jwt(std::string_view token, std::string_view& header,
               std::string_view& payload, std::string_view& signature) {
    const std::size_t first = token.find('.');
    if (first == std::string_view::npos) return false;
    const std::size_t second = token.find('.', first + 1);
    if (second == std::string_view::npos) return false;
    header = token.substr(0, first);
    payload = token.substr(first + 1, second - first - 1);
    signature = token.substr(second + 1);
    return !header.empty() && !payload.empty() && !signature.empty();
}

std::string trim_trailing_slash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string url_origin(std::string_view url) {
    if (url.rfind("https://", 0) != 0) return {};
    const std::size_t begin = 8;
    const std::size_t end = url.find_first_of("/?#", begin);
    const std::string authority(url.substr(begin, end == std::string_view::npos
        ? std::string_view::npos : end - begin));
    if (authority.find('@') != std::string::npos) return {};
    std::string host = authority;
    std::string port;
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
    }
    std::transform(host.begin(), host.end(), host.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (host.empty()) return {};
    return host + (port.empty() ? ":443" : ":" + port);
}
bool audience_matches(const nlohmann::json& payload, std::string_view expected) {
    if (!payload.contains("aud")) return false;
    const auto& audience = payload["aud"];
    if (audience.is_string()) return audience.get<std::string>() == expected;
    if (audience.is_array()) {
        for (const auto& item : audience) {
            if (item.is_string() && item.get<std::string>() == expected) return true;
        }
    }
    return false;
}

JwtResult failure(std::string error) {
    JwtResult result;
    result.error = std::move(error);
    return result;
}

JwtResult verify_with_key(std::string_view header_part, std::string_view payload_part,
                          std::string_view signature_part, const nlohmann::json& jwk,
                          std::string_view expected_issuer,
                          std::string_view expected_audience, int skew) {
    if (!jwk.is_object() || jwk.value("kty", "") != "RSA")
        return failure("JWKS key is not RSA");
    const std::string n = jwk.value("n", "");
    const std::string e = jwk.value("e", "");
    if (n.empty() || e.empty()) return failure("JWKS key has no n/e");
    std::vector<unsigned char> modulus = base64url_decode(n);
    std::vector<unsigned char> exponent = base64url_decode(e);
    if (modulus.empty() || exponent.empty()) return failure("invalid JWK material");
    while (!modulus.empty() && modulus.front() == 0) modulus.erase(modulus.begin());
    while (!exponent.empty() && exponent.front() == 0) exponent.erase(exponent.begin());
    if (modulus.size() < 128) return failure("RSA modulus too small");
    const std::vector<unsigned char> signature = base64url_decode(signature_part);
    if (signature.empty()) return failure("invalid JWT signature encoding");

    BCRYPT_ALG_HANDLE rsa_algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    unsigned char digest[32] = {};
    bool verified = false;
    if (BCryptOpenAlgorithmProvider(&rsa_algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0)
        return failure("could not open RSA provider");

    const std::size_t blob_size = sizeof(BCRYPT_RSAKEY_BLOB) + exponent.size() + modulus.size();
    std::vector<unsigned char> blob(blob_size);
    auto* header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(blob.data());
    header->Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header->BitLength = static_cast<ULONG>(modulus.size() * 8);
    header->cbPublicExp = static_cast<ULONG>(exponent.size());
    header->cbModulus = static_cast<ULONG>(modulus.size());
    header->cbPrime1 = 0;
    header->cbPrime2 = 0;
    std::memcpy(blob.data() + sizeof(BCRYPT_RSAKEY_BLOB), exponent.data(), exponent.size());
    std::memcpy(blob.data() + sizeof(BCRYPT_RSAKEY_BLOB) + exponent.size(),
                modulus.data(), modulus.size());

    const std::string signed_data = std::string(header_part) + "." + std::string(payload_part);
    const std::string digest_hex = sha256_hex(signed_data);
    if (digest_hex.size() != 64) {
        BCryptCloseAlgorithmProvider(rsa_algorithm, 0);
        return failure("could not hash JWT signing input");
    }
    for (int i = 0; i < 32; ++i) {
        digest[i] = static_cast<unsigned char>(std::stoi(digest_hex.substr(i * 2, 2), nullptr, 16));
    }
    const NTSTATUS import_status = BCryptImportKeyPair(rsa_algorithm, nullptr,
        BCRYPT_RSAPUBLIC_BLOB, &key, blob.data(), static_cast<ULONG>(blob.size()), 0);
    if (import_status != 0) {
        BCryptCloseAlgorithmProvider(rsa_algorithm, 0);
        char code[32];
        std::snprintf(code, sizeof(code), "%08lx", static_cast<unsigned long>(import_status));
        return failure(std::string("BCryptImportKeyPair 0x") + code);
    }
    BCRYPT_PKCS1_PADDING_INFO padding{};
    padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;
    const NTSTATUS verify_status = BCryptVerifySignature(key, &padding, digest,
        sizeof(digest), const_cast<PUCHAR>(signature.data()),
        static_cast<ULONG>(signature.size()), BCRYPT_PAD_PKCS1);
    const bool signature_ok = verify_status == 0;
    verified = signature_ok;
    if (key) BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(rsa_algorithm, 0);
    if (!verified) {
        char code[32];
        std::snprintf(code, sizeof(code), "%08lx", static_cast<unsigned long>(verify_status));
        return failure(std::string("JWT signature verification failed 0x") + code);
    }

    nlohmann::json payload;
    try {
        const std::vector<unsigned char> payload_bytes = base64url_decode(payload_part);
        payload = nlohmann::json::parse(std::string(payload_bytes.begin(), payload_bytes.end()));
    } catch (const std::exception&) {
        return failure("invalid JWT payload");
    }
    if (!payload.is_object()) return failure("JWT payload is not an object");
    if (payload.value("iss", "") != expected_issuer)
        return failure("JWT issuer mismatch");
    if (!audience_matches(payload, expected_audience))
        return failure("JWT audience mismatch");
    if (!payload.contains("exp") || !payload["exp"].is_number())
        return failure("JWT exp missing");
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::int64_t exp = payload["exp"].get<std::int64_t>();
    if (now > exp + skew) return failure("JWT expired");
    if (payload.contains("nbf") && payload["nbf"].is_number()) {
        const std::int64_t nbf = payload["nbf"].get<std::int64_t>();
        if (now + skew < nbf) return failure("JWT not yet valid");
    }
    JwtResult result;
    result.ok = true;
    result.claims.subject = payload.value("sub", "");
    result.claims.email = payload.value("email", "");
    result.claims.issuer = payload.value("iss", "");
    result.claims.nonce = payload.value("nonce", "");
    if (payload.contains("roles") && payload["roles"].is_array()) {
        for (const auto& role : payload["roles"])
            if (role.is_string()) result.claims.roles.push_back(role.get<std::string>());
    }
    if (payload.contains("groups") && payload["groups"].is_array()) {
        for (const auto& group : payload["groups"])
            if (group.is_string()) result.claims.roles.push_back(group.get<std::string>());
    }
    result.claims.expires_at = exp;
    if (payload["aud"].is_string()) result.claims.audience = payload["aud"].get<std::string>();
    else if (payload["aud"].is_array() && !payload["aud"].empty())
        result.claims.audience = payload["aud"].front().get<std::string>();
    if (result.claims.subject.empty()) return failure("JWT sub missing");
    return result;
}

} // namespace

OidcDiscoveryResult discover_oidc(HttpClient& client, std::string_view issuer) {
    OidcDiscoveryResult result;
    const std::string base = trim_trailing_slash(std::string(issuer));
    if (base.empty()) {
        result.error = "issuer is empty";
        return result;
    }
    HttpRequest request;
    request.method = "GET";
    request.url = base + "/.well-known/openid-configuration";
    request.headers.push_back({"Accept", "application/json"});
    const HttpResponse response = client.execute(request);
    if (response.transport_error) {
        result.error = response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        result.error = "discovery HTTP " + std::to_string(response.status);
        return result;
    }
    try {
        const nlohmann::json document = nlohmann::json::parse(response.body);
        result.configuration.issuer = document.value("issuer", "");
        result.configuration.authorization_endpoint = document.value("authorization_endpoint", "");
        result.configuration.token_endpoint = document.value("token_endpoint", "");
        result.configuration.jwks_uri = document.value("jwks_uri", "");
        result.ok = !result.configuration.jwks_uri.empty() &&
                    !result.configuration.token_endpoint.empty();
        if (result.configuration.issuer != base) {
            result.ok = false;
            result.error = "discovery issuer mismatch";
        } else if (result.ok &&
                   url_origin(base) != url_origin(result.configuration.jwks_uri)) {
            result.ok = false;
            result.error = "discovery jwks_uri is not same-origin";
        } else if (!result.ok) {
            result.error = "discovery missing endpoints";
        }
    } catch (const std::exception&) {
        result.error = "discovery returned invalid JSON";
    }
    return result;
}

JwtResult verify_rs256_token(std::string_view token, std::string_view jwks_json,
                             std::string_view expected_issuer,
                             std::string_view expected_audience,
                             int clock_skew_seconds) {
    std::string_view header_part;
    std::string_view payload_part;
    std::string_view signature_part;
    if (!split_jwt(token, header_part, payload_part, signature_part))
        return failure("JWT has invalid structure");
    nlohmann::json header_json;
    try {
        const auto bytes = base64url_decode(header_part);
        header_json = nlohmann::json::parse(std::string(bytes.begin(), bytes.end()));
    } catch (const std::exception&) {
        return failure("invalid JWT header");
    }
    if (header_json.value("alg", "") != "RS256")
        return failure("JWT alg must be RS256");
    const std::string kid = header_json.value("kid", "");

    nlohmann::json jwks;
    try {
        jwks = nlohmann::json::parse(jwks_json);
    } catch (const std::exception&) {
        return failure("invalid JWKS");
    }
    if (!jwks.is_object() || !jwks.contains("keys") || !jwks["keys"].is_array())
        return failure("JWKS keys[] missing");
    bool matched = false;
    for (const auto& key : jwks["keys"]) {
        if (!key.is_object()) continue;
        if (!kid.empty() && key.value("kid", "") != kid) continue;
        if (key.value("alg", "RS256") != "RS256") continue;
        matched = true;
        JwtResult result = verify_with_key(header_part, payload_part, signature_part,
            key, expected_issuer, expected_audience, clock_skew_seconds);
        if (result.ok) return result;
        if (!kid.empty()) return result;
    }
    return failure(matched ? "JWKS key rejected JWT" : "no matching JWKS key");
}

JwtResult verify_oidc_token(HttpClient& client, const OidcConfiguration& configuration,
                            std::string_view token, std::string_view expected_audience) {
    if (configuration.jwks_uri.empty())
        return failure("OIDC configuration has no jwks_uri");
    HttpRequest request;
    request.method = "GET";
    request.url = configuration.jwks_uri;
    request.headers.push_back({"Accept", "application/json"});
    const HttpResponse response = client.execute(request);
    if (response.transport_error) return failure(response.error);
    if (response.status < 200 || response.status >= 300)
        return failure("JWKS HTTP " + std::to_string(response.status));
    return verify_rs256_token(token, response.body, configuration.issuer, expected_audience);
}


Principal principal_from_oidc(const JwtClaims& claims, Role role) {
    Principal principal;
    principal.id = claims.subject;
    principal.display_name = claims.email.empty() ? claims.subject : claims.email;
    principal.role = role;
    principal.active = !claims.subject.empty();
    return principal;
}

Role oidc_role(const JwtClaims& claims, Role fallback) {
    for (const auto& value : claims.roles) {
        std::string role = value;
        std::transform(role.begin(), role.end(), role.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (role == "admin" || role == "olho-admin" || role == "olho.admin" ||
            role == "olhe-admin" || role == "olhe.admin")
            return Role::Admin;
        if (role == "examiner" || role == "olho-examiner" || role == "olho.examiner")
            return Role::Examiner;
        if (role == "reviewer" || role == "olho-reviewer" || role == "olho.reviewer")
            return Role::Reviewer;
        if (role == "auditor" || role == "olho-auditor" || role == "olho.auditor")
            return Role::Auditor;
    }
    return fallback;
}
} // namespace deep::cloud

