#include "pixelwar/storage/UserStore.hpp"

#include "pixelwar/security/PasswordHasher.hpp"
#include "pixelwar/utils/Base64.hpp"

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

std::string oauthKey(const std::string& provider, const std::string& subject) {
    return provider + ":" + subject;
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

std::string sanitizeUsername(const std::string& preferred, const std::string& subject) {
    std::string out;
    out.reserve(preferred.size());

    for (const unsigned char c : preferred) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == ' ' || c == '.') {
            out.push_back('_');
        }
    }

    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }

    if (out.size() < 3) {
        out = "discord_" + subject.substr(subject.size() > 8 ? subject.size() - 8 : 0);
    }
    if (out.size() > 24) {
        out.resize(24);
    }
    return out;
}

std::string uniqueUsername(
    const std::string& preferred,
    const std::string& subject,
    const std::unordered_map<std::string, std::uint64_t>& existing
) {
    std::string username = sanitizeUsername(preferred, subject);
    if (existing.find(username) == existing.end()) {
        return username;
    }

    const std::string suffix = "_" + subject.substr(subject.size() > 6 ? subject.size() - 6 : 0);
    const std::size_t maxBase = 32 > suffix.size() ? 32 - suffix.size() : 16;
    if (username.size() > maxBase) {
        username.resize(maxBase);
    }
    username += suffix;

    if (existing.find(username) == existing.end()) {
        return username;
    }

    for (int i = 2; i < 1000; ++i) {
        std::string candidate = username;
        const std::string number = "_" + std::to_string(i);
        if (candidate.size() + number.size() > 32) {
            candidate.resize(32 - number.size());
        }
        candidate += number;
        if (existing.find(candidate) == existing.end()) {
            return candidate;
        }
    }

    return "discord_" + std::to_string(existing.size() + 1);
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
    userIdsByOAuth_.clear();
    nextId_ = 1;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        const auto parts = splitTabs(line);
        if (parts.size() != 4 && parts.size() != 6 && parts.size() != 9) {
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
        user.pixelWindowStartTimestamp = *lastPixel;
        user.pixelsPlacedInWindow = *lastPixel > 0 ? 1 : 0;

        if (parts.size() == 6) {
            const auto windowStart = parseInt64(parts[4]);
            const auto placedInWindow = parseUint32(parts[5]);
            if (!windowStart || !placedInWindow) {
                continue;
            }
            user.pixelWindowStartTimestamp = *windowStart;
            user.pixelsPlacedInWindow = *placedInWindow;
        }

        if (parts.size() == 9) {
            user.oauthProvider = decodeBase64Text(parts[6]);
            user.oauthSubject = decodeBase64Text(parts[7]);
            user.email = decodeBase64Text(parts[8]);
        }

        userIdsByName_[user.username] = user.id;
        if (!user.oauthProvider.empty() && !user.oauthSubject.empty()) {
            userIdsByOAuth_[oauthKey(user.oauthProvider, user.oauthSubject)] = user.id;
        }
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
        file << id << '\t'
             << encodeText(user.username) << '\t'
             << user.passwordHash << '\t'
             << user.lastPixelTimestamp << '\t'
             << user.pixelWindowStartTimestamp << '\t'
             << user.pixelsPlacedInWindow << '\t'
             << encodeText(user.oauthProvider) << '\t'
             << encodeText(user.oauthSubject) << '\t'
             << encodeText(user.email) << '\n';
    }
}

bool UserStore::registerUser(const std::string& username, const std::string& password, std::string& error) {
    if (!models::isValidUsername(username)) {
        error = "invalid_username";
        return false;
    }
    if (!models::isValidPassword(password)) {
        error = "invalid_password";
        return false;
    }

    const std::string passwordHash = security::PasswordHasher::hashPassword(password);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (userIdsByName_.find(username) != userIdsByName_.end()) {
            error = "username_exists";
            return false;
        }

        models::User user;
        user.id = nextId_++;
        user.username = username;
        user.passwordHash = passwordHash;
        user.lastPixelTimestamp = 0;
        user.pixelWindowStartTimestamp = 0;
        user.pixelsPlacedInWindow = 0;

        userIdsByName_[user.username] = user.id;
        usersById_[user.id] = std::move(user);
    }

    save();
    return true;
}

std::optional<std::uint64_t> UserStore::verifyCredentials(const std::string& username, const std::string& password) {
    std::string hash;
    std::uint64_t userId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto idIt = userIdsByName_.find(username);
        if (idIt == userIdsByName_.end()) {
            return std::nullopt;
        }
        const auto userIt = usersById_.find(idIt->second);
        if (userIt == usersById_.end()) {
            return std::nullopt;
        }
        userId = userIt->second.id;
        hash = userIt->second.passwordHash;
    }

    if (!security::PasswordHasher::verifyPassword(password, hash)) {
        return std::nullopt;
    }
    return userId;
}

std::uint64_t UserStore::upsertOAuthUser(
    const std::string& provider,
    const std::string& subject,
    const std::string& preferredUsername,
    const std::string& email
) {
    std::uint64_t userId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto existing = userIdsByOAuth_.find(oauthKey(provider, subject));
        if (existing != userIdsByOAuth_.end()) {
            const auto userIt = usersById_.find(existing->second);
            if (userIt != usersById_.end()) {
                userIt->second.email = email;
                if (!preferredUsername.empty()) {
                    userIt->second.username = userIt->second.username.empty()
                        ? uniqueUsername(preferredUsername, subject, userIdsByName_)
                        : userIt->second.username;
                }
                userId = userIt->second.id;
            }
        } else {
            models::User user;
            user.id = nextId_++;
            user.username = uniqueUsername(preferredUsername, subject, userIdsByName_);
            user.passwordHash = "";
            user.oauthProvider = provider;
            user.oauthSubject = subject;
            user.email = email;
            user.lastPixelTimestamp = 0;
            user.pixelWindowStartTimestamp = 0;
            user.pixelsPlacedInWindow = 0;

            userIdsByName_[user.username] = user.id;
            userIdsByOAuth_[oauthKey(provider, subject)] = user.id;
            userId = user.id;
            usersById_[user.id] = std::move(user);
        }
    }

    save();
    return userId;
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
