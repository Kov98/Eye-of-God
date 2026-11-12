// language: C++, file: dropbox_client.hpp, target: Windows 11 (MSVC / MinGW g++)
// Dropbox connector core: authorization, listing and download arguments.
//
// SCOPE OF THIS FILE
//   The parts that carry judgement — URLs, request shapes, response parsing and
//   path sanitisation. Transport is the existing WinHttpTransport; wiring into the
//   job queue is a separate step. Everything here is pure, so it is proven against
//   captured response shapes instead of against the network.
//
// WHY DROPBOX BEFORE GOOGLE
//   Same OAuth2 and the same REST shape as Microsoft Graph, so the connector
//   pattern transfers. Google Workspace would add Drive-specific permission
//   models; iCloud has no public API at all and is out of scope by fact, not by
//   choice.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olho::cloud::dropbox {

struct token_set {
    std::string access_token;
    std::string refresh_token;
    std::string token_type;
    std::uint64_t expires_in = 0;
    std::string scope;
    std::string account_id;
};

// Authorization URL for the PKCE flow. The verifier never leaves the machine; only
// its S256 challenge travels.
std::string authorize_url(const std::string& app_key, const std::string& redirect_uri,
                          const std::string& code_challenge, const std::string& state);

// Parses a token endpoint reply. Returns false with a reason on an error document,
// so a refusal is never mistaken for an empty success.
bool parse_token(const std::string& json, token_set& out, std::string& error);

struct entry {
    std::string tag;            // file or folder
    std::string name;
    std::string path_lower;     // Dropbox's canonical path
    std::string id;
    std::uint64_t size = 0;
    std::string server_modified; // ISO-8601 as sent
    std::string content_hash;
};

struct listing {
    std::vector<entry> entries;
    bool has_more = false;
    std::string cursor;
};

bool parse_listing(const std::string& json, listing& out, std::string& error);

// Body for create_folder_v2 and for list_folder. Kept here so escaping has one home.
std::string list_folder_body(const std::string& path, const std::string& cursor);
std::string create_folder_body(const std::string& path);

// A Dropbox path must start with a slash, must not contain a parent reference, and
// must not carry control characters. Rejecting here means a hostile path in a case
// file cannot escape the intended namespace.
bool sanitize_path(const std::string& raw, std::string& out, std::string& error);

}  // namespace olho::cloud::dropbox
