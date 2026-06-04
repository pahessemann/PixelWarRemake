#include "pixelwar/storage/UserStore.hpp"

#include "pixelwar/security/PasswordHasher.hpp"
#include "pixelwar/utils/Base64.hpp"
#include "pixelwar/utils/Random.hpp"
#include "pixelwar/utils/Sha256.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pixelwar::storage {

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t end = line.find('\t', start);
        parts.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

std::optional<std::uint64_t> parseUint64(const std::string& text) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(text, &consumed, 10);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::int64_t> parseInt64(const std::string& text) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoll(text, &consumed, 10);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> parseUint32(const std::string& text) {
    const auto value = parseUint64(text);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

bool parseBoolFlag(const std::string& text, bool fallback) {
    if (text == "1" || text == "true") {
        return true;
    }
    if (text == "0" || text == "false") {
        return false;
    }
    return fallback;
}

std::string decodeBase64Text(const std::string& encoded) {
    const auto bytes = utils::base64Decode(encoded);
    if (!bytes) {
        return {};
    }
    return std::string(bytes->begin(), bytes->end());
}

std::string encodeText(const std::string& text) {
    return utils::base64Encode(std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::string hashToken(const std::string& token) {
    const std::vector<std::uint8_t> bytes(token.begin(), token.end());
    const auto digest = utils::sha256(bytes);
    return utils::base64Encode(std::vector<std::uint8_t>(digest.begin(), digest.end()));
}

std::string newVerificationToken() {
    return utils::base64UrlEncode(utils::randomBytes(32));
}

std::string normalizeEmail(std::string email) {
    std::transform(email.begin(), email.end(), email.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return email;
}

bool isValidEmail(const std::string& email) {
    if (email.size() < 5 || email.size() > 254) {
        return false;
    }
    if (std::any_of(email.begin(), email.end(), [](unsigned char c) {
            return std::isspace(c) || c < 33 || c > 126;
        })) {
        return false;
    }

    const auto at = email.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= email.size()) {
        return false;
    }
    if (email.find('@', at + 1) != std::string::npos) {
        return false;
    }

    const auto dot = email.find('.', at + 2);
    return dot != std::string::npos && dot + 1 < email.size();
}

} // namespace

UserStore::UserStore(std::filesystem::path path) : path_(std::move(path)) {}

bool UserStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(path_);
    if (!file) {
        return false;
    }

    usersById_.clear();
    userIdsByName_.clear();
    userIdsByEmail_.clear();
    nextId_ = 1;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        const auto parts = splitTabs(line);
        if (parts.size() != 9 && parts.size() != 12) {
            continue;
        }

        const auto id = parseUint64(parts[0]);
        const auto lastPixel = parseInt64(parts[3]);
        if (!id || !lastPixel) {
            continue;
        }

        models::User user;
        user.id = *id;
        user.username = decodeBase64Text(parts[1]);
        if (user.username.empty()) {
            continue;
        }
        user.passwordHash = parts[2];
        user.lastPixelTimestamp = *lastPixel;
        const auto windowStart = parseInt64(parts[4]);
        const auto placedInWindow = parseUint32(parts[5]);
        if (!windowStart || !placedInWindow) {
            continue;
        }
        user.pixelWindowStartTimestamp = *windowStart;
        user.pixelsPlacedInWindow = *placedInWindow;

        user.email = normalizeEmail(decodeBase64Text(parts[8]));
        if (user.passwordHash.empty() || user.email.empty()) {
            continue;
        }
        if (parts.size() == 12) {
            user.emailVerified = parseBoolFlag(parts[9], false);
            user.emailVerificationTokenHash = parts[10];
            const auto expiresAt = parseInt64(parts[11]);
            user.emailVerificationExpiresAt = expiresAt.value_or(0);
        } else {
            user.emailVerified = true;
        }

        userIdsByName_[user.username] = user.id;
        userIdsByEmail_[user.email] = user.id;
        usersById_[user.id] = std::move(user);
        nextId_ = std::max(nextId_, *id + 1);
    }

    return true;
}

void UserStore::save() const {
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(path_, std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot open users file for writing");
    }

    for (const auto& [id, user] : usersById_) {
        if (user.passwordHash.empty() || user.email.empty()) {
            continue;
        }
        file << id << '\t'
             << encodeText(user.username) << '\t'
             << user.passwordHash << '\t'
             << user.lastPixelTimestamp << '\t'
             << user.pixelWindowStartTimestamp << '\t'
             << user.pixelsPlacedInWindow << '\t'
             << '\t'
             << '\t'
             << encodeText(user.email) << '\t'
             << (user.emailVerified ? 1 : 0) << '\t'
             << user.emailVerificationTokenHash << '\t'
             << user.emailVerificationExpiresAt << '\n';
    }
}

RegistrationResult UserStore::registerUser(
    const std::string& username,
    const std::string& email,
    const std::string& password,
    bool requireEmailVerification,
    std::int64_t verificationTtlSeconds
) {
    RegistrationResult result;
    if (!models::isValidUsername(username)) {
        result.error = "invalid_username";
        return result;
    }
    const std::string normalizedEmail = normalizeEmail(email);
    if (!isValidEmail(normalizedEmail)) {
        result.error = "invalid_email";
        return result;
    }
    if (!models::isValidPassword(password)) {
        result.error = "invalid_password";
        return result;
    }

    const std::string token = requireEmailVerification ? newVerificationToken() : "";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (userIdsByName_.find(username) != userIdsByName_.end()) {
            result.error = "username_taken";
            return result;
        }
        if (userIdsByEmail_.find(normalizedEmail) != userIdsByEmail_.end()) {
            result.error = "email_taken";
            return result;
        }

        models::User user;
        user.id = nextId_++;
        user.username = username;
        user.email = normalizedEmail;
        user.passwordHash = security::PasswordHasher::hashPassword(password);
        user.emailVerified = !requireEmailVerification;
        user.emailVerificationTokenHash = requireEmailVerification ? hashToken(token) : "";
        user.emailVerificationExpiresAt = requireEmailVerification
            ? nowSeconds() + std::max<std::int64_t>(60, verificationTtlSeconds)
            : 0;
        user.lastPixelTimestamp = 0;
        user.pixelWindowStartTimestamp = 0;
        user.pixelsPlacedInWindow = 0;

        userIdsByName_[user.username] = user.id;
        userIdsByEmail_[user.email] = user.id;
        result.userId = user.id;
        result.username = user.username;
        result.email = user.email;
        usersById_[user.id] = std::move(user);
    }

    save();
    result.created = true;
    result.verificationToken = token;
    return result;
}

std::optional<std::uint64_t> UserStore::verifyCredentials(const std::string& login, const std::string& password) {
    models::User user;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::optional<std::uint64_t> userId;
        if (const auto byName = userIdsByName_.find(login); byName != userIdsByName_.end()) {
            userId = byName->second;
        } else if (const auto byEmail = userIdsByEmail_.find(normalizeEmail(login)); byEmail != userIdsByEmail_.end()) {
            userId = byEmail->second;
        }

        if (!userId) {
            return std::nullopt;
        }

        const auto it = usersById_.find(*userId);
        if (it == usersById_.end() || it->second.passwordHash.empty()) {
            return std::nullopt;
        }
        user = it->second;
    }

    if (!security::PasswordHasher::verifyPassword(password, user.passwordHash)) {
        return std::nullopt;
    }
    return user.id;
}

bool UserStore::verifyEmailToken(const std::string& token, std::uint64_t& userId) {
    if (token.empty()) {
        return false;
    }

    const std::string tokenHash = hashToken(token);
    bool verified = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::int64_t now = nowSeconds();
        for (auto& [id, user] : usersById_) {
            if (user.emailVerificationTokenHash != tokenHash) {
                continue;
            }
            if (user.emailVerificationExpiresAt > 0 && user.emailVerificationExpiresAt < now) {
                return false;
            }

            user.emailVerified = true;
            user.emailVerificationTokenHash.clear();
            user.emailVerificationExpiresAt = 0;
            userId = id;
            verified = true;
            break;
        }
    }

    if (verified) {
        save();
    }
    return verified;
}

std::optional<models::User> UserStore::findById(std::uint64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = usersById_.find(id);
    if (it == usersById_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::size_t UserStore::userCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return usersById_.size();
}

std::vector<AdminUserView> UserStore::adminUsers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AdminUserView> users;
    users.reserve(usersById_.size());

    for (const auto& [id, user] : usersById_) {
        users.push_back(AdminUserView{
            id,
            user.username,
            user.email,
            user.emailVerified,
            user.lastPixelTimestamp,
            user.pixelWindowStartTimestamp,
            user.pixelsPlacedInWindow
        });
    }

    std::sort(users.begin(), users.end(), [](const AdminUserView& lhs, const AdminUserView& rhs) {
        return lhs.id < rhs.id;
    });
    return users;
}

bool UserStore::resetPixelQuota(std::uint64_t userId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = usersById_.find(userId);
        if (it == usersById_.end()) {
            return false;
        }

        it->second.lastPixelTimestamp = 0;
        it->second.pixelWindowStartTimestamp = 0;
        it->second.pixelsPlacedInWindow = 0;
    }

    save();
    return true;
}

PixelQuotaStatus UserStore::pixelQuotaStatus(std::uint64_t userId, std::int64_t cooldownSeconds, std::uint32_t quota) const {
    std::lock_guard<std::mutex> lock(mutex_);
    PixelQuotaStatus status;
    status.quota = quota;

    const auto it = usersById_.find(userId);
    if (it == usersById_.end()) {
        status.remainingSeconds = cooldownSeconds;
        status.remainingPlacements = 0;
        return status;
    }

    const std::int64_t now = nowSeconds();
    const std::int64_t elapsed = now - it->second.pixelWindowStartTimestamp;
    if (it->second.pixelWindowStartTimestamp <= 0 || elapsed >= cooldownSeconds) {
        status.remainingSeconds = 0;
        status.remainingPlacements = quota;
        return status;
    }

    status.remainingPlacements = it->second.pixelsPlacedInWindow >= quota
        ? 0
        : quota - it->second.pixelsPlacedInWindow;
    status.remainingSeconds = status.remainingPlacements > 0
        ? 0
        : std::max<std::int64_t>(0, cooldownSeconds - elapsed);
    return status;
}

bool UserStore::consumePixelSlot(
    std::uint64_t userId,
    std::int64_t cooldownSeconds,
    std::uint32_t quota,
    PixelQuotaStatus& status
) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status.quota = quota;

        const auto it = usersById_.find(userId);
        if (it == usersById_.end()) {
            status.remainingSeconds = cooldownSeconds;
            status.remainingPlacements = 0;
            return false;
        }

        const std::int64_t now = nowSeconds();
        const std::int64_t elapsed = now - it->second.pixelWindowStartTimestamp;
        if (it->second.pixelWindowStartTimestamp <= 0 || elapsed >= cooldownSeconds) {
            it->second.pixelWindowStartTimestamp = now;
            it->second.pixelsPlacedInWindow = 0;
        }

        if (it->second.pixelsPlacedInWindow >= quota) {
            const std::int64_t activeElapsed = now - it->second.pixelWindowStartTimestamp;
            status.remainingSeconds = std::max<std::int64_t>(0, cooldownSeconds - activeElapsed);
            status.remainingPlacements = 0;
            return false;
        }

        ++it->second.pixelsPlacedInWindow;
        it->second.lastPixelTimestamp = now;
        status.remainingPlacements = quota - it->second.pixelsPlacedInWindow;
        status.remainingSeconds = status.remainingPlacements > 0
            ? 0
            : std::max<std::int64_t>(0, cooldownSeconds - (now - it->second.pixelWindowStartTimestamp));
    }

    save();
    return true;
}

std::int64_t UserStore::nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace pixelwar::storage
