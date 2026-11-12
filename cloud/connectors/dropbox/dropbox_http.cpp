// language: C++, file: dropbox_http.cpp
#include "cloud/connectors/dropbox/dropbox_http.hpp"

#include "cloud/connectors/dropbox/dropbox_client.hpp"

#include "third_party/nlohmann/json.hpp"

namespace olho::cloud::dropbox {
namespace {

using deep::cloud::HttpRequest;
using deep::cloud::HttpResponse;

std::string form_escape(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

// Maps an HTTP failure onto something a forensic operator can act on. The body
// of a Dropbox error is a JSON document; when it does not parse the status is
// still reported, so a failure is never silent.
std::string describe_failure(int status, const std::string& body) {
    // Two different things matter to an operator: the machine code used for
    // decisions (invalid_grant, path/not_found) and the human description. Keep
    // both instead of letting one shadow the other.
    std::string code;
    std::string detail;
    try {
        const auto doc = nlohmann::json::parse(body);
        if (doc.is_object()) {
            if (doc.contains("error_summary") && doc["error_summary"].is_string()) {
                code = doc["error_summary"].get<std::string>();
            }
            if (doc.contains("error") && doc["error"].is_string()) {
                if (code.empty()) code = doc["error"].get<std::string>();
            }
            if (doc.contains("error_description") && doc["error_description"].is_string()) {
                detail = doc["error_description"].get<std::string>();
            }
        }
    } catch (const std::exception&) {
        code.clear();
        detail.clear();
    }
    std::string text = "HTTP " + std::to_string(status);
    if (status == 401) {
        text += " (token recusado)";
    } else if (status == 409) {
        text += " (conflito de caminho)";
    } else if (status == 429) {
        text += " (limite de requisicoes)";
    }
    if (!code.empty()) text += ": " + code;
    if (!detail.empty() && detail != code) text += " (" + detail + ")";
    return text;
}

}  // namespace

client::client(deep::cloud::HttpClient& http, std::string access_token,
               std::string api_base, std::string content_base)
    : http_(http),
      access_token_(std::move(access_token)),
      api_base_(std::move(api_base)),
      content_base_(std::move(content_base)) {}

HttpResponse client::post_json(const std::string& url, const std::string& body) {
    HttpRequest request;
    request.method = "POST";
    request.url = url;
    request.headers.emplace_back("Authorization", "Bearer " + access_token_);
    request.headers.emplace_back("Content-Type", "application/json");
    request.body = body;
    return http_.execute(request);
}

bool client::list_page(const std::string& path, const std::string& cursor, listing& out,
                       std::string& error) {
    const bool cont = !cursor.empty();
    const std::string url = cont ? api_base_ + "/files/list_folder/continue"
                                 : api_base_ + "/files/list_folder";
    const HttpResponse response =
        post_json(url, list_folder_body(cont ? std::string() : path, cursor));
    if (response.transport_error) {
        error = "falha de transporte: " + response.error;
        return false;
    }
    if (!response.ok()) {
        last_body_.clear();
        error = describe_failure(response.status, response.body);
        return false;
    }
    listing parsed;
    if (!parse_listing(response.body, parsed, error)) {
        last_body_.clear();
        return false;
    }
    last_body_ = response.body;
    out = std::move(parsed);
    return true;
}

deep::cloud::HttpDownloadResult client::download(const std::string& path,
                                                 const std::string& destination_path,
                                                 std::uint64_t max_bytes) {
    deep::cloud::HttpDownloadRequest request;
    request.method = "POST";
    request.url = content_base_ + "/files/download";
    request.headers.emplace_back("Authorization", "Bearer " + access_token_);
    request.headers.emplace_back("Dropbox-API-Arg", nlohmann::json{{"path", path}}.dump());
    request.destination_path = destination_path;
    request.max_bytes = max_bytes;
    return http_.download(request);
}

bool client::token_request(const std::string& form, token_set& out, std::string& error) {
    HttpRequest request;
    request.method = "POST";
    request.url = "https://api.dropboxapi.com/oauth2/token";
    request.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    request.body = form;
    const HttpResponse response = http_.execute(request);
    if (response.transport_error) {
        error = "falha de transporte: " + response.error;
        return false;
    }
    if (!response.ok()) {
        error = describe_failure(response.status, response.body);
        return false;
    }
    return parse_token(response.body, out, error);
}

bool client::refresh_access_token(const std::string& app_key, const std::string& refresh_token,
                                  token_set& out, std::string& error) {
    const std::string form = "grant_type=refresh_token&refresh_token=" +
                             form_escape(refresh_token) + "&client_id=" + form_escape(app_key);
    return token_request(form, out, error);
}

bool client::exchange_code(const std::string& app_key, const std::string& code,
                           const std::string& code_verifier, const std::string& redirect_uri,
                           token_set& out, std::string& error) {
    const std::string form = "grant_type=authorization_code&code=" + form_escape(code) +
                             "&client_id=" + form_escape(app_key) +
                             "&code_verifier=" + form_escape(code_verifier) +
                             "&redirect_uri=" + form_escape(redirect_uri);
    return token_request(form, out, error);
}

}  // namespace olho::cloud::dropbox
