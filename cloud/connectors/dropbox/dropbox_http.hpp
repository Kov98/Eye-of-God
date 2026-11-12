// language: C++, file: dropbox_http.hpp, target: Windows 11 (MSVC / MinGW g++)
// Dropbox API client over the existing HttpClient/transport stack.
//
// SCOPE
//   Only the request shapes and error mapping live here. Pagination, resume and
//   evidence layout live in dropbox_collector.cpp; parsing lives in
//   dropbox_client.cpp, which is transport-free and proven offline.
//
// WHY THE TWO HOSTS
//   Dropbox splits metadata (api.dropboxapi.com) from content
//   (content.dropboxapi.com). Both need the bearer token, so both are pinned to
//   an https base and never followed from a redirect: a redirect would hand the
//   token to another host.
#pragma once

#include "cloud/connectors/dropbox/dropbox_client.hpp"
#include "cloud/core/cloud_core.hpp"

#include <cstdint>
#include <string>

namespace olho::cloud::dropbox {

class client {
public:
    client(deep::cloud::HttpClient& http, std::string access_token,
           std::string api_base = "https://api.dropboxapi.com/2",
           std::string content_base = "https://content.dropboxapi.com/2");

    // One listing page. With an empty cursor this starts at path; with a cursor
    // it continues the previous page and path is ignored, exactly like the API.
    bool list_page(const std::string& path, const std::string& cursor, listing& out,
                   std::string& error);

    // Streams files/download into destination_path. The argument travels in the
    // Dropbox-API-Arg header because that endpoint takes no body.
    deep::cloud::HttpDownloadResult download(const std::string& path,
                                             const std::string& destination_path,
                                             std::uint64_t max_bytes = 0);

    // Exchanges a refresh token for a short-lived access token.
    bool refresh_access_token(const std::string& app_key, const std::string& refresh_token,
                              token_set& out, std::string& error);

    // Exchanges an authorization code for the first token pair.
    bool exchange_code(const std::string& app_key, const std::string& code,
                       const std::string& code_verifier, const std::string& redirect_uri,
                       token_set& out, std::string& error);

    void set_access_token(std::string token) { access_token_ = std::move(token); }

    // Body of the last successful list_page, byte for byte. The collector keeps
    // it as evidence: the parsed view is an index, the bytes are the record.
    const std::string& last_listing_body() const { return last_body_; }

private:
    deep::cloud::HttpResponse post_json(const std::string& url, const std::string& body);
    bool token_request(const std::string& form, token_set& out, std::string& error);

    deep::cloud::HttpClient& http_;
    std::string access_token_;
    std::string api_base_;
    std::string content_base_;
    std::string last_body_;
};

}  // namespace olho::cloud::dropbox
