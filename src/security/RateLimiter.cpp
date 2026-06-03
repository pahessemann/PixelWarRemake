#include "pixelwar/security/RateLimiter.hpp"

namespace pixelwar::security {

bool RateLimiter::allow(const std::string& key, std::uint32_t maxRequests, std::chrono::seconds window) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);

    auto& bucket = buckets_[key];
    if (bucket.windowStart.time_since_epoch().count() == 0 || now - bucket.windowStart >= window) {
        bucket.windowStart = now;
        bucket.count = 0;
    }

    if (bucket.count >= maxRequests) {
        return false;
    }

    ++bucket.count;
    return true;
}

} // namespace pixelwar::security

