// language: C++, file: cloud/cli/dropbox_auth.hpp
// Interactive Dropbox authorization: PKCE in the browser, code exchange on the
// loopback callback, token sealed in DPAPI.
//
// WHY IT IS A SEPARATE COMMAND
//   The Microsoft flow depends on tenant endpoints and a scope string; Dropbox
//   depends on an app key and always asks for a refresh token (offline access).
//   Sharing one command would mean a pile of provider switches in the same
//   function, so the two live side by side instead.
#pragma once

#include <string>

namespace deep::cloud {

struct DropboxAuthOptions {
    std::string app_key;
    // Where the token is filed. Empty means "dropbox/default".
    std::string vault_key;
    int timeout_seconds = 300;
    bool no_browser = false;
    bool mock_http = false;
};

int run_dropbox_auth(const DropboxAuthOptions& options);

}  // namespace deep::cloud
