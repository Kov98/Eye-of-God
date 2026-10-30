#pragma once

#include "cloud/core/cloud_core.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace deep::cloud {

// Absolute HTTP(S) URL split into the components the collector needs before it
// hands a URL to the transport. `host` is lower-cased; `port` is empty when the
// URL omits it.
struct HttpUrlParts {
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
};

// True when `value` starts with a URI scheme followed by "://". Relative paths
// (for example "/v1.0/me/messages") return false.
bool url_has_scheme(std::string_view value);

// Parses an absolute http/https URL. Userinfo ("user@host"), control
// characters, backslashes, empty/invalid hosts and malformed ports are
// rejected. The query and fragment remain part of `path`.
bool parse_http_url(std::string_view url, HttpUrlParts& parts);

// Small read-only Microsoft Graph client. Pagination is deliberately exposed
// as a page callback so the caller can checkpoint after each page instead of
// loading an entire mailbox into memory.
class GraphClient {
public:
    using PageHandler = std::function<bool(std::string_view body, std::string& error)>;

    GraphClient(HttpClient& client, std::string access_token,
                std::string base_url = "https://graph.microsoft.com/v1.0");

    HttpResponse get(std::string_view path);
    HttpResponse get_absolute(std::string_view url, bool authenticated);

    // Streams a Graph `$value` payload to `destination_path` without buffering
    // it in memory. Relative paths resolve against the configured Graph base;
    // absolute URLs are accepted only on the configured Graph host. A 3xx
    // response is reported through HttpDownloadResult::redirected/location and
    // is never followed here.
    HttpDownloadResult download(std::string_view path,
                                const std::string& destination_path,
                                std::uint64_t max_bytes = 0);

    // Streams an absolute URL. `authenticated` must stay false for redirect
    // targets (pre-signed download URLs) so no bearer token leaves Graph.
    HttpDownloadResult download_absolute(std::string_view url, bool authenticated,
                                         const std::string& destination_path,
                                         std::uint64_t max_bytes = 0);

    // `has_more` is set when the page budget was exhausted while Graph still
    // advertised another page, so callers must checkpoint instead of treating
    // the source as complete. `collect` still returns true in that case.
    bool collect(std::string_view path, int max_pages, const PageHandler& on_page,
                 std::string& error, int& pages_fetched, bool& has_more);

    // True when `url` is a relative path or an absolute https URL on the
    // configured Graph host, i.e. safe to request with the bearer token.
    bool is_trusted_url(std::string_view url) const;

private:
    // Resolves `path` (relative or same-host absolute) into the URL that may be
    // requested with the bearer token. Shared by get() and download().
    bool resolve_url(std::string_view path, std::string& url, bool& authenticated,
                     std::string& error) const;

    HttpClient& client_;
    std::string access_token_;
    std::string base_url_;
};

} // namespace deep::cloud
