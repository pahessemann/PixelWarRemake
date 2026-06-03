#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pixelwar::security {

class RateLimiter {
public:
    bool allow(const std::string& key, std::uint32_t maxRequests, std::chrono::seconds window);

private:
    struct Bucket {
        std::uint32_t count = 0;
        std::chrono::steady_clock::time_point windowStart{};
    };

    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

} // namespace pixelwar::security

