#include "pixelwar/security/SessionManager.hpp"

#include "pixelwar/utils/Base64.hpp"
#include "pixelwar/utils/Random.hpp"

namespace pixelwar::security {

SessionManager::SessionManager(std::chrono::seconds ttl) : ttl_(ttl) {}

std::string SessionManager::createSession(std::uint64_t userId) {
    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    pruneExpiredLocked(now);

    std::string token;
    do {
        token = utils::base64UrlEncode(utils::randomBytes(32));
    } while (sessions_.find(token) != sessions_.end());

    sessions_[token] = Session{userId, now + ttl_};
    return token;
}

std::optional<std::uint64_t> SessionManager::validate(const std::string& token) {
    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    pruneExpiredLocked(now);

    const auto it = sessions_.find(token);
    if (it == sessions_.end() || it->second.expiresAt <= now) {
        return std::nullopt;
    }
    return it->second.userId;
}

void SessionManager::revoke(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
}

void SessionManager::pruneExpiredLocked(std::chrono::system_clock::time_point now) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.expiresAt <= now) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace pixelwar::security

