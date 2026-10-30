#include "graph_client.hpp"

#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>

namespace deep::cloud {
namespace {

bool is_scheme_character(unsigned char c) {
    return std::isalnum(c) != 0 || c == '+' || c == '-' || c == '.';
}

bool has_unsafe_characters(std::string_view value) {
    for (unsigned char c : value) {
        if (c <= 0x20 || c == 0x7f || c == '\\') return true;
    }
    return false;
}

std::string lower_ascii(std::string_view value) {
    std::string result(value);
    for (char& c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

bool is_valid_host(std::string_view host) {
    if (host.empty() || host.front() == '.' || host.back() == '.') return false;
    bool previous_dot = false;
    for (unsigned char c : host) {
        if (c == '.') {
            if (previous_dot) return false;
            previous_dot = true;
            continue;
        }
        previous_dot = false;
        if (std::isalnum(c) == 0 && c != '-') return false;
    }
    return true;
}

HttpResponse rejected_url(std::string message) {
    HttpResponse response;
    response.transport_error = true;
    response.error = std::move(message);
    return response;
}

HttpDownloadResult rejected_download(std::string message) {
    HttpDownloadResult result;
    result.transport_error = true;
    result.error = std::move(message);
    return result;
}

} // namespace

bool url_has_scheme(std::string_view value) {
    if (value.empty() || std::isalpha(static_cast<unsigned char>(value.front())) == 0)
        return false;
    std::size_t index = 1;
    while (index < value.size() &&
           is_scheme_character(static_cast<unsigned char>(value[index]))) {
        ++index;
    }
    return index + 3 <= value.size() && value.substr(index, 3) == "://";
}

bool parse_http_url(std::string_view url, HttpUrlParts& parts) {
    if (url.empty() || has_unsafe_characters(url)) return false;
    if (!url_has_scheme(url)) return false;

    const std::size_t scheme_end = url.find("://");
    const std::string scheme = lower_ascii(url.substr(0, scheme_end));
    if (scheme != "http" && scheme != "https") return false;

    const std::size_t authority_begin = scheme_end + 3;
    std::size_t authority_end = url.find_first_of("/?#", authority_begin);
    if (authority_end == std::string_view::npos) authority_end = url.size();
    const std::string_view authority =
        url.substr(authority_begin, authority_end - authority_begin);
    if (authority.empty()) return false;
    // Reject userinfo outright. Without this "https://graph.microsoft.com:443@evil"
    // would otherwise be read as host "graph.microsoft.com".
    if (authority.find('@') != std::string_view::npos) return false;

    std::string_view host = authority;
    std::string_view port;
    const std::size_t colon = authority.find(':');
    if (colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
        if (port.empty() || port.find(':') != std::string_view::npos) return false;
    }
    if (!is_valid_host(host)) return false;

    int port_number = 0;
    if (!port.empty()) {
        if (port.size() > 5) return false;
        for (char c : port) {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
            port_number = port_number * 10 + (c - '0');
        }
        if (port_number < 1 || port_number > 65535) return false;
    }

    parts.scheme = scheme;
    parts.host = lower_ascii(host);
    parts.port.assign(port);
    parts.path.assign(url.substr(authority_end));
    return true;
}

GraphClient::GraphClient(HttpClient& client, std::string access_token, std::string base_url)
    : client_(client), access_token_(std::move(access_token)), base_url_(std::move(base_url)) {}

HttpResponse GraphClient::get_absolute(std::string_view url, bool authenticated) {
    HttpRequest request;
    request.method = "GET";
    request.url = std::string(url);
    if (authenticated) request.headers.push_back({"Authorization", "Bearer " + access_token_});
    request.headers.push_back({"Accept", "*/*"});
    return client_.execute(request);
}

bool GraphClient::is_trusted_url(std::string_view url) const {
    if (url.empty() || has_unsafe_characters(url)) return false;
    if (!url_has_scheme(url)) return true; // Relative to the configured base URL.

    HttpUrlParts absolute;
    HttpUrlParts base;
    if (!parse_http_url(url, absolute) || !parse_http_url(base_url_, base)) return false;
    return absolute.scheme == "https" && absolute.host == base.host;
}

bool GraphClient::resolve_url(std::string_view path, std::string& url,
                              bool& authenticated, std::string& error) const {
    HttpUrlParts base;
    const bool base_ok = parse_http_url(base_url_, base);
    if (url_has_scheme(path)) {
        HttpUrlParts absolute;
        if (!parse_http_url(path, absolute)) {
            error = "Graph absolute URL is not a valid URL";
            return false;
        }
        if (absolute.scheme != "https") {
            error = "Graph absolute URL must use https";
            return false;
        }
        if (!base_ok || absolute.host != base.host) {
            error = "Graph absolute URL host is not allowed";
            return false;
        }
        url.assign(path);
        authenticated = true;
        return true;
    }
    if (has_unsafe_characters(path)) {
        error = "Graph request path contains invalid characters";
        return false;
    }
    if (!base_ok) {
        error = "Graph base URL is invalid";
        return false;
    }
    url = base_url_;
    if (!url.empty() && url.back() == '/') url.pop_back();
    if (path.empty() || path.front() != '/') url.push_back('/');
    url.append(path);
    authenticated = base.scheme == "https";
    return true;
}

HttpResponse GraphClient::get(std::string_view path) {
    std::string url;
    bool authenticated = false;
    std::string error;
    if (!resolve_url(path, url, authenticated, error))
        return rejected_url(std::move(error));

    HttpRequest request;
    request.method = "GET";
    request.url = std::move(url);
    if (authenticated) request.headers.push_back({"Authorization", "Bearer " + access_token_});
    request.headers.push_back({"Accept", "application/json"});
    return client_.execute(request);
}

HttpDownloadResult GraphClient::download_absolute(std::string_view url,
                                                  bool authenticated,
                                                  const std::string& destination_path,
                                                  std::uint64_t max_bytes) {
    HttpDownloadRequest request;
    request.method = "GET";
    request.url = std::string(url);
    request.destination_path = destination_path;
    request.max_bytes = max_bytes;
    // Redirect targets are pre-signed and must never see the Graph bearer.
    if (authenticated) request.headers.push_back({"Authorization", "Bearer " + access_token_});
    request.headers.push_back({"Accept", "*/*"});
    return client_.download(request);
}

HttpDownloadResult GraphClient::download(std::string_view path,
                                         const std::string& destination_path,
                                         std::uint64_t max_bytes) {
    std::string url;
    bool authenticated = false;
    std::string error;
    if (!resolve_url(path, url, authenticated, error))
        return rejected_download(std::move(error));

    HttpDownloadRequest request;
    request.method = "GET";
    request.url = std::move(url);
    request.destination_path = destination_path;
    request.max_bytes = max_bytes;
    if (authenticated) request.headers.push_back({"Authorization", "Bearer " + access_token_});
    request.headers.push_back({"Accept", "*/*"});
    return client_.download(request);
}

bool GraphClient::collect(std::string_view path, int max_pages,
                          const PageHandler& on_page, std::string& error,
                          int& pages_fetched, bool& has_more) {
    pages_fetched = 0;
    has_more = false;
    error.clear();
    std::string next(path);
    const int limit = std::max(1, max_pages);
    while (!next.empty() && pages_fetched < limit) {
        // A persisted checkpoint or @odata.nextLink is untrusted input: never
        // send the bearer token to a host other than the configured Graph host.
        if (!is_trusted_url(next)) {
            error = "Graph pagination URL is not allowed";
            return false;
        }
        const HttpResponse response = get(next);
        if (response.transport_error) {
            error = response.error.empty() ? "Graph transport error" : response.error;
            return false;
        }
        if (response.status < 200 || response.status >= 300) {
            error = "Graph HTTP " + std::to_string(response.status);
            return false;
        }
        ++pages_fetched;
        if (on_page && !on_page(response.body, error)) return false;

        nlohmann::json document;
        try {
            document = nlohmann::json::parse(response.body);
        } catch (const std::exception&) {
            error = "Graph returned invalid JSON";
            return false;
        }
        if (!document.is_object()) {
            error = "Graph page is not an object";
            return false;
        }
        const auto link = document.find("@odata.nextLink");
        if (link == document.end() || link->is_null()) {
            next.clear();
            break;
        }
        if (!link->is_string()) {
            error = "Graph nextLink is not a string";
            return false;
        }
        next = link->get<std::string>();
        if (!next.empty() && !is_trusted_url(next)) {
            error = "Graph nextLink is not an allowed URL";
            return false;
        }
    }
    // Reaching the page limit is not an error, but the caller must not mark the
    // source complete while another page is still advertised.
    has_more = !next.empty();
    return true;
}

} // namespace deep::cloud
