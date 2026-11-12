// language: C++, file: dropbox_client.cpp, target: Windows 11 (MSVC / MinGW g++)
#include "dropbox_client.hpp"

#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>

namespace olho::cloud::dropbox {
namespace {

std::string url_encode(const std::string& raw) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size() * 3);
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

}  // namespace

std::string authorize_url(const std::string& app_key, const std::string& redirect_uri,
                          const std::string& code_challenge, const std::string& state) {
    // token_access_type=offline is what returns a refresh token; without it a
    // long acquisition dies when the four-hour access token expires.
    return "https://www.dropbox.com/oauth2/authorize"
           "?client_id=" + url_encode(app_key) +
           "&response_type=code"
           "&token_access_type=offline"
           "&redirect_uri=" + url_encode(redirect_uri) +
           "&code_challenge=" + url_encode(code_challenge) +
           "&code_challenge_method=S256"
           "&state=" + url_encode(state);
}

bool parse_token(const std::string& json, token_set& out, std::string& error) {
    out = token_set{};
    try {
        const auto doc = nlohmann::json::parse(json);
        if (doc.contains("error")) {
            error = "Dropbox recusou: " + doc.value("error", std::string("desconhecido"));
            if (doc.contains("error_description")) {
                error += " - " + doc.value("error_description", std::string());
            }
            return false;
        }
        out.access_token = doc.value("access_token", "");
        out.refresh_token = doc.value("refresh_token", "");
        out.token_type = doc.value("token_type", "");
        out.expires_in = doc.value("expires_in", 0ull);
        out.scope = doc.value("scope", "");
        out.account_id = doc.value("account_id", "");
        if (out.access_token.empty()) { error = "resposta sem access_token"; return false; }
        return true;
    } catch (const std::exception& e) {
        error = std::string("JSON invalido: ") + e.what();
        return false;
    }
}

bool parse_listing(const std::string& json, listing& out, std::string& error) {
    out = listing{};
    try {
        const auto doc = nlohmann::json::parse(json);
        if (doc.contains("error") || doc.contains("error_summary")) {
            error = "listagem recusada: " + doc.dump();
            return false;
        }
        // Uma listagem bem-sucedida sempre traz "entries" (mesmo vazia). Exigir o
        // campo evita que uma resposta inesperada vire "pasta vazia" silenciosa -
        // num laco de aquisicao isso significa perder a evidencia sem avisar.
        if (!doc.is_object() || !doc.contains("entries") || !doc["entries"].is_array()) {
            error = "resposta sem lista de entradas: " + doc.dump();
            return false;
        }
        for (const auto& raw : doc.value("entries", nlohmann::json::array())) {
            entry e;
            e.tag = raw.value(".tag", "");
            e.name = raw.value("name", "");
            e.path_lower = raw.value("path_lower", "");
            e.id = raw.value("id", "");
            e.size = raw.value("size", 0ull);
            e.server_modified = raw.value("server_modified", "");
            e.content_hash = raw.value("content_hash", "");
            // Folders carry no size; that is data, not a parse failure.
            out.entries.push_back(std::move(e));
        }
        out.has_more = doc.value("has_more", false);
        out.cursor = doc.value("cursor", "");
        return true;
    } catch (const std::exception& e) {
        error = std::string("JSON invalido: ") + e.what();
        return false;
    }
}

std::string list_folder_body(const std::string& path, const std::string& cursor) {
    nlohmann::json body;
    if (!cursor.empty()) {
        body["cursor"] = cursor;
    } else {
        body["path"] = path;
        body["recursive"] = true;
        body["include_media_info"] = false;
        body["include_deleted"] = false;
        body["limit"] = 2000;
    }
    return body.dump();
}

std::string create_folder_body(const std::string& path) {
    nlohmann::json body;
    body["path"] = path;
    body["autorename"] = false;
    return body.dump();
}

bool sanitize_path(const std::string& raw, std::string& out, std::string& error) {
    out.clear();
    if (raw.empty()) { error = "caminho vazio"; return false; }
    for (unsigned char c : raw) {
        if (c < 0x20 || c == 0x7F) { error = "caminho com caractere de controle"; return false; }
    }
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized == "/") { out = ""; return true; }   // root is the empty path in the API

    if (normalized.front() != '/') normalized.insert(normalized.begin(), '/');
    if (normalized.size() > 1 && normalized.back() == '/') normalized.pop_back();

    // A parent reference must never survive into a request path.
    std::string segment;
    std::vector<std::string> parts;
    for (std::size_t i = 1; i <= normalized.size(); ++i) {
        if (i == normalized.size() || normalized[i] == '/') {
            if (segment == "..") { error = "caminho com referencia ao pai"; return false; }
            if (!segment.empty() && segment != ".") parts.push_back(segment);
            segment.clear();
        } else {
            segment.push_back(normalized[i]);
        }
    }
    if (parts.empty()) { out = ""; return true; }

    out = "/";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back('/');
        out += parts[i];
    }
    return true;
}

}  // namespace olho::cloud::dropbox
