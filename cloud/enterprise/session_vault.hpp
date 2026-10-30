#pragma once

#include "cloud/core/rbac.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace deep::cloud {

struct PersistedSession {
    std::string token;
    Principal principal;
    std::int64_t expires_at_epoch = 0;
};

// DPAPI-protected, per-user session snapshot. The raw token is only stored
// inside the DPAPI blob; no plaintext token touches disk.
class SessionVault {
public:
    explicit SessionVault(std::string path);

    bool save(const PersistedSession& session, std::string& error);
    std::optional<PersistedSession> load(bool& found, std::string& error);
    bool erase(std::string& error);

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

} // namespace deep::cloud
