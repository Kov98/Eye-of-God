#include "oauth_pkce.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <vector>

namespace deep::cloud {
namespace {

const char* kBase64Url =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url(const unsigned char* data, std::size_t size) {
    std::string out;
    out.reserve((size * 4 + 2) / 3);
    std::size_t i = 0;
    while (i + 3 <= size) {
        const std::uint32_t value = (static_cast<std::uint32_t>(data[i]) << 16) |
            (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out.push_back(kBase64Url[(value >> 18) & 0x3f]);
        out.push_back(kBase64Url[(value >> 12) & 0x3f]);
        out.push_back(kBase64Url[(value >> 6) & 0x3f]);
        out.push_back(kBase64Url[value & 0x3f]);
        i += 3;
    }
    const std::size_t remaining = size - i;
    if (remaining == 1) {
        const std::uint32_t value = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kBase64Url[(value >> 18) & 0x3f]);
        out.push_back(kBase64Url[(value >> 12) & 0x3f]);
    } else if (remaining == 2) {
        const std::uint32_t value = (static_cast<std::uint32_t>(data[i]) << 16) |
            (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kBase64Url[(value >> 18) & 0x3f]);
        out.push_back(kBase64Url[(value >> 12) & 0x3f]);
        out.push_back(kBase64Url[(value >> 6) & 0x3f]);
    }
    return out;
}

std::string sha256_base64url(std::string_view input) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> digest(32);
    DWORD object_size = 0;
    DWORD result_size = 0;
    DWORD hash_size = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &result_size, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size), &result_size, 0) != 0 ||
        hash_size != 32) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::vector<unsigned char> object(object_size);
    const bool created = BCryptCreateHash(algorithm, &hash, object.data(), object_size,
        nullptr, 0, 0) == 0;
    const bool hashed = created && BCryptHashData(hash,
        reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
        static_cast<ULONG>(input.size()), 0) == 0;
    const bool finished = hashed && BCryptFinishHash(hash, digest.data(), 32, 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!finished) return {};
    return base64url(digest.data(), digest.size());
}

std::string json_string_field(std::string_view body, std::string_view key) {
    const std::string pattern = "\"" + std::string(key) + "\"";
    const std::size_t found = body.find(pattern);
    if (found == std::string_view::npos) return {};
    std::size_t cursor = found + pattern.size();
    while (cursor < body.size() && std::isspace(static_cast<unsigned char>(body[cursor])))
        ++cursor;
    if (cursor >= body.size() || body[cursor] != ':') return {};
    ++cursor;
    while (cursor < body.size() && std::isspace(static_cast<unsigned char>(body[cursor])))
        ++cursor;
    if (cursor >= body.size() || body[cursor] != '"') return {};
    ++cursor;
    std::string result;
    while (cursor < body.size()) {
        const char c = body[cursor++];
        if (c == '"') return result;
        if (c == '\\' && cursor < body.size()) {
            const char escaped = body[cursor++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u':
                if (cursor + 4 <= body.size()) cursor += 4;
                result.push_back('?');
                break;
            default: result.push_back(escaped); break;
            }
        } else {
            result.push_back(c);
        }
    }
    return {};
}

int json_int_field(std::string_view body, std::string_view key, int fallback) {
    const std::string pattern = "\"" + std::string(key) + "\"";
    const std::size_t found = body.find(pattern);
    if (found == std::string_view::npos) return fallback;
    std::size_t cursor = found + pattern.size();
    while (cursor < body.size() && body[cursor] != ':') ++cursor;
    if (cursor >= body.size()) return fallback;
    ++cursor;
    while (cursor < body.size() && std::isspace(static_cast<unsigned char>(body[cursor])))
        ++cursor;
    int value = 0;
    bool any = false;
    while (cursor < body.size() && std::isdigit(static_cast<unsigned char>(body[cursor]))) {
        value = value * 10 + (body[cursor] - '0');
        any = true;
        ++cursor;
    }
    return any ? value : fallback;
}

OAuthTokenResult parse_token_response(const HttpResponse& response) {
    OAuthTokenResult result;
    result.status = response.status;
    if (response.transport_error) {
        result.error = response.error.empty() ? "transport error" : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        const std::string code = json_string_field(response.body, "error");
        const std::string description = json_string_field(response.body, "error_description");
        result.error = code.empty() ? ("token endpoint HTTP " + std::to_string(response.status))
                                    : code + (description.empty() ? "" : ": " + description);
        return result;
    }
    OAuthToken token;
    token.access_token = json_string_field(response.body, "access_token");
    token.refresh_token = json_string_field(response.body, "refresh_token");
    token.id_token = json_string_field(response.body, "id_token");
    token.token_type = json_string_field(response.body, "token_type");
    token.expires_in = json_int_field(response.body, "expires_in", 3600);
    if (token.token_type.empty()) token.token_type = "Bearer";
    token.reset_expiry();
    if (token.access_token.empty()) {
        result.error = "token response has no access_token";
        return result;
    }
    result.token = token;
    return result;
}

HttpRequest token_request(const OAuthConfig& config, const std::string& body) {
    HttpRequest request;
    request.method = "POST";
    request.url = config.token_endpoint;
    request.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    request.headers.push_back({"Accept", "application/json"});
    request.body = body;
    return request;
}

} // namespace

std::string random_urlsafe(std::size_t bytes) {
    if (bytes == 0 || bytes > 128) return {};
    std::vector<unsigned char> random(bytes);
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return {};
    }
    return base64url(random.data(), random.size());
}

PkcePair generate_pkce() {
    PkcePair pair;
    pair.verifier = random_urlsafe(32);
    if (pair.verifier.empty()) return pair;
    pair.challenge = sha256_base64url(pair.verifier);
    if (pair.challenge.empty()) pair.verifier.clear();
    return pair;
}

std::string pkce_challenge(std::string_view verifier) {
    if (verifier.size() < 43 || verifier.size() > 128) return {};
    return sha256_base64url(verifier);
}

std::string url_encode(std::string_view value) {
    static const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[(c >> 4) & 0xf]);
            result.push_back(hex[c & 0xf]);
        }
    }
    return result;
}

std::string build_authorize_url(const OAuthConfig& config,
                                std::string_view state,
                                std::string_view challenge) {
    if (config.authorization_endpoint.empty() || config.client_id.empty() ||
        config.redirect_uri.empty() || config.scope.empty() || state.empty() ||
        challenge.empty()) {
        return {};
    }
    std::string url = config.authorization_endpoint;
    url += url.find('?') == std::string::npos ? "?" : "&";
    url += "response_type=code";
    url += "&client_id=" + url_encode(config.client_id);
    url += "&redirect_uri=" + url_encode(config.redirect_uri);
    url += "&scope=" + url_encode(config.scope);
    url += "&state=" + url_encode(state);
    url += "&code_challenge=" + url_encode(challenge);
    url += "&code_challenge_method=S256";
    return url;
}

OAuthTokenResult exchange_authorization_code(HttpTransport& transport,
                                             const OAuthConfig& config,
                                             std::string_view code,
                                             std::string_view verifier) {
    OAuthTokenResult result;
    if (config.token_endpoint.empty() || config.client_id.empty() ||
        config.redirect_uri.empty() || code.empty() || verifier.empty()) {
        result.error = "missing OAuth token endpoint, client, redirect, code or verifier";
        return result;
    }
    const std::string body =
        "grant_type=authorization_code" +
        std::string("&code=") + url_encode(code) +
        "&client_id=" + url_encode(config.client_id) +
        "&redirect_uri=" + url_encode(config.redirect_uri) +
        "&code_verifier=" + url_encode(verifier);
    return parse_token_response(transport.send(token_request(config, body)));
}

OAuthTokenResult refresh_access_token(HttpTransport& transport,
                                      const OAuthConfig& config,
                                      std::string_view refresh_token) {
    OAuthTokenResult result;
    if (config.token_endpoint.empty() || config.client_id.empty() || refresh_token.empty()) {
        result.error = "missing OAuth token endpoint, client or refresh token";
        return result;
    }
    std::string body =
        "grant_type=refresh_token" +
        std::string("&refresh_token=") + url_encode(refresh_token) +
        "&client_id=" + url_encode(config.client_id);
    if (!config.scope.empty()) body += "&scope=" + url_encode(config.scope);
    return parse_token_response(transport.send(token_request(config, body)));
}

} // namespace deep::cloud
