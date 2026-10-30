#include "rate_limiter.hpp"

#include <algorithm>

namespace deep::cloud {
namespace {

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

RateLimiter::RateLimiter(int max_per_minute)
    : max_per_minute_(std::max(1, max_per_minute)) {}

bool RateLimiter::allow(const std::string& key, int& retry_after_seconds) {
    const std::int64_t now = now_seconds();
    std::lock_guard<std::mutex> lock(mutex_);
    if (windows_.find(key) == windows_.end() && windows_.size() >= 4096)
        windows_.clear();
    Window& window = windows_[key];
    if (window.started_epoch == 0 || now - window.started_epoch >= 60) {
        window.started_epoch = now;
        window.count = 0;
    }
    if (window.count >= max_per_minute_) {
        retry_after_seconds = static_cast<int>(
            std::max<std::int64_t>(1, 60 - (now - window.started_epoch)));
        return false;
    }
    ++window.count;
    retry_after_seconds = 0;
    return true;
}

} // namespace deep::cloud
