#pragma once

#include "cloud/core/rbac.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace deep::cloud {

// Short-lived in-memory session tokens. Tokens are never persisted; only their
// SHA-256 hashes are kept in the map. For a multi-process/service deployment
// this is replaced by the central session store.
class SessionManager {
public:
    explicit SessionManager(int ttl_seconds = 8 * 60 * 60);

    std::optional<std::string> create(const Principal& principal, std::string& error);
    bool restore(const std::string& token, const Principal& principal,
                 std::chrono::system_clock::time_point expires_at,
                 std::string& error);
    std::optional<Principal> validate(const std::string& token) const;
    bool revoke(const std::string& token);
    void revoke_user(const std::string& user_id);
    std::size_t active_sessions() const;

private:
    struct Entry {
        Principal principal;
        std::chrono::system_clock::time_point expires_at;
    };

    int ttl_seconds_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, Entry> sessions_;
};

} // namespace deep::cloud
