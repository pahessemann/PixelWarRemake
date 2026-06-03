#include "pixelwar/config/ServerConfig.hpp"

#include "pixelwar/utils/Json.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

namespace pixelwar::config {

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::size_t positiveSizeOr(std::optional<std::int64_t> value, std::size_t fallback) {
    if (!value || *value <= 0) {
        return fallback;
    }
    return static_cast<std::size_t>(*value);
}

std::int64_t positiveIntOr(std::optional<std::int64_t> value, std::int64_t fallback) {
    if (!value || *value <= 0) {
        return fallback;
    }
    return *value;
}

} // namespace

ServerConfig loadServerConfig(const std::filesystem::path& path) {
    ServerConfig cfg;
    const std::string content = readFile(path);
    if (content.empty()) {
        return cfg;
    }

    const auto parsed = utils::json::parseObject(content);
    if (!parsed) {
        return cfg;
    }

    if (auto host = utils::json::getString(*parsed, "host")) {
        cfg.host = *host;
    }
    if (auto port = utils::json::getInt(*parsed, "port")) {
        if (*port > 0 && *port <= 65535) {
            cfg.port = static_cast<std::uint16_t>(*port);
        }
    }
    cfg.mapWidth = positiveSizeOr(utils::json::getInt(*parsed, "map_width"), cfg.mapWidth);
    cfg.mapHeight = positiveSizeOr(utils::json::getInt(*parsed, "map_height"), cfg.mapHeight);
    cfg.cooldownSeconds = positiveIntOr(utils::json::getInt(*parsed, "cooldown_seconds"), cfg.cooldownSeconds);
    if (auto quota = utils::json::getInt(*parsed, "pixel_quota_per_cooldown")) {
        if (*quota > 0 && *quota <= 1000) {
            cfg.pixelQuotaPerCooldown = static_cast<std::uint32_t>(*quota);
        }
    }
    cfg.sessionTtlSeconds = positiveIntOr(utils::json::getInt(*parsed, "session_ttl_seconds"), cfg.sessionTtlSeconds);
    cfg.threadPoolSize = positiveSizeOr(utils::json::getInt(*parsed, "thread_pool_size"), cfg.threadPoolSize);
    cfg.maxBodyBytes = positiveSizeOr(utils::json::getInt(*parsed, "max_body_bytes"), cfg.maxBodyBytes);
    if (auto adminUsername = utils::json::getString(*parsed, "admin_username")) {
        cfg.adminUsername = *adminUsername;
    }
    if (auto paletteSize = utils::json::getInt(*parsed, "palette_size")) {
        if (*paletteSize > 0 && *paletteSize <= 256) {
            cfg.paletteSize = static_cast<std::uint8_t>(*paletteSize);
        }
    }
    if (auto dataDir = utils::json::getString(*parsed, "data_dir")) {
        cfg.dataDir = *dataDir;
    }

    return cfg;
}

} // namespace pixelwar::config
