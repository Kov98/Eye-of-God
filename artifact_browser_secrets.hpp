#pragma once

#include "artifacts.hpp"

#include <string>

namespace deep {

// Explicit, opt-in browser secret collector. Reads the DPAPI-protected AES key
// from Local State and decrypts Chrome/Edge v10/v11 cookies and passwords.
// v20 app-bound values are reported as encrypted but not decrypted.
int collect_browser_secrets(const std::wstring& user_data_dir,
                            const ArtifactSink& sink, std::wstring& error);

} // namespace deep
