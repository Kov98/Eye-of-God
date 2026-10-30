#include "session.hpp"

#include "cloud/core/cloud_core.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <vector>

namespace deep::cloud {
namespace {

std::string random_token(std::string& error) {
    unsigned char bytes[32] = {};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        error = "could not generate session token";
        return {};
    }
    static const char digits[] = "0123456789abcdef";
    std::string token;
    token.reserve(sizeof(bytes) * 2);
    for (unsigned char byte : bytes) {
        token.push_back(digits[byte >> 4]);
        token.push_back(digits[byte & 0x0f]);
    }
    return token;
}

} // namespace

SessionManager::SessionManager(int ttl_seconds)
    : ttl_seconds_(std::max(0, ttl_seconds)) {}

std::optional<std::string> SessionManager::create(const Principal& principal,
                                                  std::string& error) {
    error.clear();
    if (principal.id.empty()) {
        error = "principal has no id";
        return std::nullopt;
    }
    if (!principal.active) {
        error = "principal is inactive";
        return std::nullopt;
    }
    const std::string token = random_token(error);
    if (token.empty()) return std::nullopt;
    Entry entry;
    entry.principal = principal;
    entry.expires_at = std::chrono::system_clock::now() +
        std::chrono::seconds(ttl_seconds_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[sha256_hex(token)] = std::move(entry);
    }
    return token;
}

std::optional<Principal> SessionManager::validate(const std::string& token) const {
    if (token.empty()) return std::nullopt;
    const std::string key = sha256_hex(token);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(key);
    if (found == sessions_.end()) return std::nullopt;
    if (std::chrono::system_clock::now() >= found->second.expires_at) {
        sessions_.erase(found);
        return std::nullopt;
    }
    return found->second.principal;
}

bool SessionManager::revoke(const std::string& token) {
    if (token.empty()) return false;
    const std::string key = sha256_hex(token);
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.erase(key) > 0;
}

void SessionManager::revoke_user(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.principal.id == user_id)
            it = sessions_.erase(it);
        else
            ++it;
    }
}

std::size_t SessionManager::active_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::system_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now >= it->second.expires_at)
            it = sessions_.erase(it);
        else
            ++it;
    }
    return sessions_.size();
}


bool SessionManager::restore(const std::string& token, const Principal& principal,
                             std::chrono::system_clock::time_point expires_at,
                             std::string& error) {
    error.clear();
    if (token.empty() || principal.id.empty() || !principal.active) {
        error = "invalid persisted session";
        return false;
    }
    if (expires_at <= std::chrono::system_clock::now()) {
        error = "persisted session expired";
        return false;
    }
    Entry entry;
    entry.principal = principal;
    entry.expires_at = expires_at;
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[sha256_hex(token)] = std::move(entry);
    return true;
}
} // namespace deep::cloud
