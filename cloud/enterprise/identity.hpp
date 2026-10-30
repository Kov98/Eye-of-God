#pragma once

#include "cloud/core/rbac.hpp"

#include <optional>
#include <string>
#include <vector>

namespace deep::cloud {

struct LocalUser {
    std::string id;
    std::string display_name;
    Role role = Role::Auditor;
    bool active = false;
    std::string salt_hex;
    std::string hash_hex;
    int iterations = 120000;
    std::string created_at_utc;
};

// File-backed local identity store. Passwords use PBKDF2-HMAC-SHA256 with a
// per-user random salt and 120k iterations. The file is not a full identity
// provider: it is the local/offline fallback until OIDC/SSO is wired into the
// session layer.
class LocalIdentityStore {
public:
    explicit LocalIdentityStore(std::string path);

    bool load(std::string& error);
    bool save(std::string& error) const;

    bool create_user(const std::string& id, const std::string& display_name,
                     Role role, const std::string& password, std::string& error,
                     bool overwrite = false);
    std::optional<Principal> authenticate(const std::string& id,
                                          const std::string& password) const;
    std::vector<LocalUser> users() const;
    bool set_active(const std::string& id, bool active, std::string& error);
    bool set_role(const std::string& id, Role role, std::string& error);

    const std::string& path() const { return path_; }
    const std::string& last_error() const { return last_error_; }

private:
    const LocalUser* find(const std::string& id) const;
    LocalUser* find(const std::string& id);

    std::string path_;
    std::vector<LocalUser> users_;
    std::string last_error_;
};

} // namespace deep::cloud
