#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace deep::cloud {

// Fixed-window limiter for the loopback API. It is intentionally simple:
// one window per bearer token (or "anonymous" for login/health-adjacent calls).
// A distributed deployment should move this to Redis or the edge proxy.
class RateLimiter {
public:
    explicit RateLimiter(int max_per_minute = 120);

    bool allow(const std::string& key, int& retry_after_seconds);
    int limit() const { return max_per_minute_; }

private:
    struct Window {
        std::int64_t started_epoch = 0;
        int count = 0;
    };

    int max_per_minute_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Window> windows_;
};

} // namespace deep::cloud
