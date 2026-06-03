#include "pixelwar/controllers/ApiController.hpp"

#include "pixelwar/utils/Json.hpp"

#include <charconv>
#include <chrono>
#include <optional>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace pixelwar::controllers {

namespace {

using pixelwar::http::HttpRequest;
using pixelwar::http::HttpResponse;

HttpResponse jsonError(int status, const std::string& code) {
    return HttpResponse::json(status, R"({"error":")" + pixelwar::utils::json::escape(code) + R"("})");
}

std::optional<std::uint64_t> parseUint64(const std::string& text) {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> tokenFromRequest(const HttpRequest& request, const std::optional<pixelwar::utils::json::Object>& body = std::nullopt) {
    if (const auto authorization = request.header("authorization")) {
        constexpr const char* prefix = "Bearer ";
        if (authorization->rfind(prefix, 0) == 0) {
            return authorization->substr(7);
        }
    }

    if (const auto token = request.queryValue("token")) {
        if (!token->empty()) {
            return token;
        }
    }

    if (body) {
        return pixelwar::utils::json::getString(*body, "token");
    }

    return std::nullopt;
}

std::optional<pixelwar::utils::json::Object> parseJsonBody(const HttpRequest& request) {
    if (request.body.empty()) {
        return std::nullopt;
    }
    return pixelwar::utils::json::parseObject(request.body);
}

std::string paletteJson(std::uint8_t paletteSize) {
    static constexpr const char* kPalette[] = {
        "#000000", "#ffffff", "#ff4500", "#ffa800",
        "#ffd635", "#00a368", "#7eed56", "#2450a4",
        "#3690ea", "#51e9f4", "#811e9f", "#b44ac0",
        "#ff99aa", "#9c6926", "#898d90", "#d4d7d9"
    };

    std::ostringstream out;
    out << R"({"palette_size":)" << static_cast<int>(paletteSize) << R"(,"colors":[)";
    for (std::uint16_t i = 0; i < paletteSize; ++i) {
        if (i != 0) {
            out << ',';
        }
        const std::string color = i < 16 ? kPalette[i] : "#000000";
        out << R"({"id":)" << i << R"(,"hex":")" << color << R"("})";
    }
    out << "]}";
    return out.str();
}

std::string quotaJson(const storage::PixelQuotaStatus& status, std::int64_t cooldownSeconds) {
    std::ostringstream out;
    out << R"({"remaining_seconds":)" << status.remainingSeconds
        << R"(,"ready":)" << (status.remainingPlacements > 0 ? "true" : "false")
        << R"(,"placements_remaining":)" << status.remainingPlacements
        << R"(,"quota":)" << status.quota
        << R"(,"cooldown_seconds":)" << cooldownSeconds
        << '}';
    return out.str();
}

} // namespace

void registerApiRoutes(
    http::Router& router,
    storage::PixelMap& pixelMap,
    storage::UserStore& userStore,
    security::SessionManager& sessions,
    security::RateLimiter& rateLimiter,
    const config::ServerConfig& cfg
) {
    const auto mapPath = cfg.dataDir / "map.pwm";

    router.add("GET", "/health", [&pixelMap](const HttpRequest&) {
        std::ostringstream out;
        out << R"({"status":"ok","sequence":)" << pixelMap.sequence() << '}';
        return HttpResponse::json(200, out.str());
    });

    router.add("GET", "/palette", [&cfg](const HttpRequest&) {
        return HttpResponse::json(200, paletteJson(cfg.paletteSize));
    });

    router.add("POST", "/register", [&userStore, &rateLimiter](const HttpRequest& request) {
        if (!rateLimiter.allow("register:" + request.remoteAddress, 20, std::chrono::minutes(1))) {
            return jsonError(429, "rate_limited");
        }

        const auto body = parseJsonBody(request);
        if (!body) {
            return jsonError(400, "invalid_json");
        }

        const auto username = utils::json::getString(*body, "username");
        const auto password = utils::json::getString(*body, "password");
        if (!username || !password) {
            return jsonError(400, "missing_credentials");
        }

        std::string error;
        if (!userStore.registerUser(*username, *password, error)) {
            return jsonError(error == "username_exists" ? 409 : 400, error);
        }

        return HttpResponse::json(201, R"({"status":"created"})");
    });

    router.add("POST", "/login", [&userStore, &sessions, &rateLimiter, &cfg](const HttpRequest& request) {
        if (!rateLimiter.allow("login:" + request.remoteAddress, 10, std::chrono::minutes(1))) {
            return jsonError(429, "rate_limited");
        }

        const auto body = parseJsonBody(request);
        if (!body) {
            return jsonError(400, "invalid_json");
        }

        const auto username = utils::json::getString(*body, "username");
        const auto password = utils::json::getString(*body, "password");
        if (!username || !password) {
            return jsonError(400, "missing_credentials");
        }

        const auto userId = userStore.verifyCredentials(*username, *password);
        if (!userId) {
            return jsonError(401, "invalid_credentials");
        }

        const std::string token = sessions.createSession(*userId);
        return HttpResponse::json(
            200,
            R"({"token":")" + utils::json::escape(token) + R"(","expires_in_seconds":)" + std::to_string(cfg.sessionTtlSeconds) + "}"
        );
    });

    router.add("GET", "/map", [&pixelMap](const HttpRequest& request) {
        std::optional<std::uint64_t> since;
        if (const auto value = request.queryValue("since")) {
            since = parseUint64(*value);
            if (!since) {
                return jsonError(400, "invalid_since");
            }
        }
        return HttpResponse::json(200, pixelMap.toJson(since));
    });

    router.add("POST", "/pixel", [&pixelMap, &userStore, &sessions, &rateLimiter, &cfg, mapPath](const HttpRequest& request) {
        const auto body = parseJsonBody(request);
        if (!body) {
            return jsonError(400, "invalid_json");
        }

        const auto token = tokenFromRequest(request, body);
        if (!token) {
            return jsonError(401, "missing_token");
        }

        const auto userId = sessions.validate(*token);
        if (!userId) {
            return jsonError(401, "invalid_token");
        }

        if (!rateLimiter.allow("pixel:" + *token, 120, std::chrono::minutes(1))) {
            return jsonError(429, "rate_limited");
        }

        const auto x = utils::json::getInt(*body, "x");
        const auto y = utils::json::getInt(*body, "y");
        const auto color = utils::json::getInt(*body, "color");
        if (!x || !y || !color || *x < 0 || *y < 0 || *color < 0 || *color >= cfg.paletteSize) {
            return jsonError(400, "invalid_pixel");
        }
        if (!pixelMap.isInBounds(static_cast<std::size_t>(*x), static_cast<std::size_t>(*y))) {
            return jsonError(400, "out_of_bounds");
        }

        storage::PixelQuotaStatus quotaStatus;
        if (!userStore.consumePixelSlot(*userId, cfg.cooldownSeconds, cfg.pixelQuotaPerCooldown, quotaStatus)) {
            return HttpResponse::json(
                429,
                R"({"error":"cooldown_active","remaining_seconds":)" + std::to_string(quotaStatus.remainingSeconds) +
                    R"(,"placements_remaining":)" + std::to_string(quotaStatus.remainingPlacements) +
                    R"(,"quota":)" + std::to_string(quotaStatus.quota) + "}"
            );
        }

        storage::PixelChange change;
        if (!pixelMap.setPixel(
                static_cast<std::size_t>(*x),
                static_cast<std::size_t>(*y),
                static_cast<std::uint8_t>(*color),
                change
            )) {
            return jsonError(400, "invalid_pixel");
        }

        pixelMap.saveBinary(mapPath);

        std::ostringstream out;
        out << R"({"status":"placed","sequence":)" << change.sequence
            << R"(,"cooldown_seconds":)" << cfg.cooldownSeconds
            << R"(,"placements_remaining":)" << quotaStatus.remainingPlacements
            << R"(,"quota":)" << quotaStatus.quota
            << R"(,"remaining_seconds":)" << quotaStatus.remainingSeconds
            << '}';
        return HttpResponse::json(200, out.str());
    });

    router.add("GET", "/cooldown", [&userStore, &sessions, &cfg](const HttpRequest& request) {
        const auto token = tokenFromRequest(request);
        if (!token) {
            return jsonError(401, "missing_token");
        }

        const auto userId = sessions.validate(*token);
        if (!userId) {
            return jsonError(401, "invalid_token");
        }

        const auto status = userStore.pixelQuotaStatus(*userId, cfg.cooldownSeconds, cfg.pixelQuotaPerCooldown);
        return HttpResponse::json(200, quotaJson(status, cfg.cooldownSeconds));
    });
}

} // namespace pixelwar::controllers
