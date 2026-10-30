#pragma once

#include "cloud_core.hpp"

#include <string>

namespace deep::cloud {

// Windows-native HTTP transport. TLS certificate validation is left at the
// WinHTTP default (never disabled), redirects are disabled so bearer tokens
// cannot be replayed to another host, and no request data is logged.
class WinHttpTransport final : public HttpTransport {
public:
    explicit WinHttpTransport(std::wstring user_agent = L"OlhoDeDeus-Cloud/0.1",
                              bool allow_insecure_http = false);
    HttpResponse send(const HttpRequest& request) override;
    HttpDownloadResult download(const HttpDownloadRequest& request) override;

private:
    std::wstring user_agent_;
    bool allow_insecure_http_ = false;
};

} // namespace deep::cloud
