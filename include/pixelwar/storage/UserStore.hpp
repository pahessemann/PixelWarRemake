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
    std::int64_t lastPixelTimestamp = 0;
    std::int64_t pixelWindowStartTimestamp = 0;
    std::uint32_t pixelsPlacedInWindow = 0;
};

class UserStore {
public:
    explicit UserStore(std::filesystem::path path);

    bool load();
    void save() const;

    bool registerUser(const std::string& username, const std::string& password, std::string& error);
    std::optional<std::uint64_t> verifyCredentials(const std::string& username, const std::string& password);
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
    std::uint64_t nextId_ = 1;
};

} // namespace pixelwar::storage
