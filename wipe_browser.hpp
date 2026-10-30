#pragma once

#include <string>

namespace deep {

// Destructive browser-profile sanitizer. Removes history, cookies, logins,
// web data, local/session storage, caches and the Local State key after
// overwriting each file. Refuses to run while chrome.exe/msedge.exe is alive.
int wipe_browser(const std::wstring& user_data_dir, const std::wstring& out_dir,
                 bool confirm);

} // namespace deep
