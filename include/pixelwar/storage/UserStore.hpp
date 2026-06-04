#pragma once

#include "pixelwar/models/User.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pixelwar::storage {

struct PixelQuotaStatus {
    std::int64_t remainingSeconds = 0;
    std::uint32_t remainingPlacements = 0;
    std::uint32_t quota = 0;
};

struct AdminUserView {
    std::uint64_t id = 0;
    std::string username;
    std::string email;
    bool emailVerified = false;
    std::int64_t lastPixelTimestamp = 0;
    std::int64_t pixelWindowStartTimestamp = 0;
    std::uint32_t pixelsPlacedInWindow = 0;
};

struct RegistrationResult {
    bool created = false;
    std::uint64_t userId = 0;
    std::string username;
    std::string email;
    std::string verificationToken;
    std::string error;
};

class UserStore {
public:
    explicit UserStore(std::filesystem::path path);

    bool load();
    void save() const;

    RegistrationResult registerUser(
        const std::string& username,
        const std::string& email,
        const std::string& password,
        bool requireEmailVerification,
        std::int64_t verificationTtlSeconds
    );
    std::optional<std::uint64_t> verifyCredentials(const std::string& login, const std::string& password);
    bool verifyEmailToken(const std::string& token, std::uint64_t& userId);
    std::optional<models::User> findById(std::uint64_t id) const;
    std::size_t userCount() const;
    std::vector<AdminUserView> adminUsers() const;
    bool resetPixelQuota(std::uint64_t userId);

    PixelQuotaStatus pixelQuotaStatus(std::uint64_t userId, std::int64_t cooldownSeconds, std::uint32_t quota) const;
    bool consumePixelSlot(
        std::uint64_t userId,
        std::int64_t cooldownSeconds,
        std::uint32_t quota,
        PixelQuotaStatus& status
    );

private:
    static std::int64_t nowSeconds();

    std::filesystem::path path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, models::User> usersById_;
    std::unordered_map<std::string, std::uint64_t> userIdsByName_;
    std::unordered_map<std::string, std::uint64_t> userIdsByEmail_;
    std::uint64_t nextId_ = 1;
};

} // namespace pixelwar::storage
