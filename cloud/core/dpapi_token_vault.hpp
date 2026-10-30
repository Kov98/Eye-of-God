#pragma once

#include "cloud_core.hpp"

#include <string>

namespace deep::cloud {

// Per-user Windows DPAPI vault. The file contains only a DPAPI blob; the
// plaintext token exists in memory while the process needs it. The default
// root is %LOCALAPPDATA%\OlhoDeDeus\cloud_tokens.
class DpapiTokenVault final : public TokenVault {
public:
    explicit DpapiTokenVault(std::wstring root = {});
    std::optional<OAuthToken> load(const std::string& key) override;
    bool save(const std::string& key, const OAuthToken& token) override;
    bool erase(const std::string& key) override;

private:
    std::wstring path_for(const std::string& key) const;
    std::wstring root_;
};

} // namespace deep::cloud
