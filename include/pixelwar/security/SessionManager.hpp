#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace pixelwar::security {

class SessionManager {
public:
    explicit SessionManager(std::chrono::seconds ttl);

    std::string createSession(std::uint64_t userId);
    std::optional<std::uint64_t> validate(const std::string& token);
    void revoke(const std::string& token);

private:
    struct Session {
        std::uint64_t userId = 0;
        std::chrono::system_clock::time_point expiresAt{};
    };

    void pruneExpiredLocked(std::chrono::system_clock::time_point now);

    std::chrono::seconds ttl_;
    std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;
};

} // namespace pixelwar::security

