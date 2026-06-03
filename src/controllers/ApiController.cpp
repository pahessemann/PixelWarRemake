#include "pixelwar/controllers/ApiController.hpp"

#include "pixelwar/utils/Base64.hpp"
#include "pixelwar/utils/HttpsClient.hpp"
#include "pixelwar/utils/Json.hpp"
#include "pixelwar/utils/Random.hpp"

#include <charconv>
#include <chrono>
#include <optional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace pixelwar::controllers {

namespace {

using pixelwar::http::HttpRequest;
using pixelwar::http::HttpResponse;

HttpResponse jsonError(int status, const std::string& code) {
    return HttpResponse::json(status, R"({"error":")" + pixelwar::utils::json::escape(code) + R"("})");
}

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

class OAuthStateStore {
public:
    std::string create() {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string state = utils::base64UrlEncode(utils::randomBytes(32));
        states_[state] = nowSeconds() + 600;
        return state;
    }

    bool consume(const std::string& state) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = nowSeconds();
        for (auto it = states_.begin(); it != states_.end();) {
            if (it->second <= now) {
                it = states_.erase(it);
            } else {
                ++it;
            }
        }

        const auto it = states_.find(state);
        if (it == states_.end()) {
            return false;
        }
        states_.erase(it);
        return true;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::int64_t> states_;
};

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

bool isDiscordOAuthConfigured(const config::ServerConfig& cfg) {
    return !cfg.discordClientId.empty() && !cfg.discordClientSecret.empty() && !cfg.publicBaseUrl.empty();
}

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string discordRedirectUri(const config::ServerConfig& cfg) {
    return trimTrailingSlash(cfg.publicBaseUrl) + cfg.discordRedirectPath;
}

std::string urlEncode(const std::string& value) {
    std::ostringstream out;
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out << static_cast<char>(c);
        } else {
            out << '%' << hex[(c >> 4) & 0xF] << hex[c & 0xF];
        }
    }
    return out.str();
}

std::string formBody(const std::unordered_map<std::string, std::string>& values) {
    std::ostringstream out;
    bool first = true;
    for (const auto& [key, value] : values) {
        if (!first) {
            out << '&';
        }
        first = false;
        out << urlEncode(key) << '=' << urlEncode(value);
    }
    return out.str();
}

HttpResponse redirectTo(const std::string& location) {
    auto response = HttpResponse::text(302, "");
    response.headers["Location"] = location;
    return response;
}

HttpResponse htmlResponse(std::string body) {
    auto response = HttpResponse::text(200, std::move(body));
    response.headers["Content-Type"] = "text/html; charset=utf-8";
    return response;
}

HttpResponse authResultHtml(const std::string& token, const std::string& username) {
    std::ostringstream out;
    out << "<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
        << "<title>Connexion Discord</title></head><body>"
        << "<script>"
        << "localStorage.setItem('pixelwar.token',\"" << utils::json::escape(token) << "\");"
        << "localStorage.setItem('pixelwar.username',\"" << utils::json::escape(username) << "\");"
        << "location.replace('/');"
        << "</script>"
        << "Connexion Discord terminee."
        << "</body></html>";
    return htmlResponse(out.str());
}

HttpResponse authErrorHtml(const std::string& code) {
    std::ostringstream out;
    out << "<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
        << "<title>Connexion Discord</title></head><body>"
        << "<script>"
        << "localStorage.removeItem('pixelwar.token');"
        << "localStorage.removeItem('pixelwar.username');"
        << "location.replace('/?auth_error=" << urlEncode(code) << "');"
        << "</script>"
        << "Connexion Discord impossible."
        << "</body></html>";
    return htmlResponse(out.str());
}

struct DiscordToken {
    std::string accessToken;
};

struct DiscordUser {
    std::string id;
    std::string username;
    std::string email;
};

std::optional<DiscordToken> exchangeDiscordCode(const config::ServerConfig& cfg, const std::string& code) {
    const std::string body = formBody({
        {"client_id", cfg.discordClientId},
        {"client_secret", cfg.discordClientSecret},
        {"grant_type", "authorization_code"},
        {"code", code},
        {"redirect_uri", discordRedirectUri(cfg)}
    });

    const auto response = utils::httpsRequest(
        "POST",
        "discord.com",
        "/api/oauth2/token",
        {
            {"Content-Type", "application/x-www-form-urlencoded"},
            {"Accept", "application/json"}
        },
        body
    );
    if (response.status < 200 || response.status >= 300) {
        return std::nullopt;
    }

    const auto parsed = utils::json::parseObject(response.body);
    if (!parsed) {
        return std::nullopt;
    }

    const auto accessToken = utils::json::getString(*parsed, "access_token");
    if (!accessToken || accessToken->empty()) {
        return std::nullopt;
    }
    return DiscordToken{*accessToken};
}

std::optional<DiscordUser> fetchDiscordUser(const std::string& accessToken) {
    const auto response = utils::httpsRequest(
        "GET",
        "discord.com",
        "/api/users/@me",
        {
            {"Authorization", "Bearer " + accessToken},
            {"Accept", "application/json"}
        },
        ""
    );
    if (response.status < 200 || response.status >= 300) {
        return std::nullopt;
    }

    const auto parsed = utils::json::parseObject(response.body);
    if (!parsed) {
        return std::nullopt;
    }

    const auto id = utils::json::getString(*parsed, "id");
    const auto username = utils::json::getString(*parsed, "username");
    if (!id || id->empty() || !username || username->empty()) {
        return std::nullopt;
    }

    const auto email = utils::json::getString(*parsed, "email").value_or("");
    return DiscordUser{*id, *username, email};
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

std::optional<HttpResponse> adminGuard(
    const HttpRequest& request,
    const storage::UserStore& userStore,
    pixelwar::security::SessionManager& sessions,
    const config::ServerConfig& cfg
) {
    const auto token = tokenFromRequest(request);
    if (!token) {
        return jsonError(401, "missing_token");
    }

    const auto userId = sessions.validate(*token);
    if (!userId) {
        return jsonError(401, "invalid_token");
    }

    const auto user = userStore.findById(*userId);
    if (!user) {
        return jsonError(403, "forbidden");
    }
    if (!cfg.adminDiscordId.empty()) {
        if (user->oauthProvider != "discord" || user->oauthSubject != cfg.adminDiscordId) {
            return jsonError(403, "forbidden");
        }
        return std::nullopt;
    }
    if (user->username != cfg.adminUsername) {
        return jsonError(403, "forbidden");
    }

    return std::nullopt;
}

std::string adminUsersJson(const std::vector<storage::AdminUserView>& users) {
    std::ostringstream out;
    out << R"({"users":[)";
    for (std::size_t i = 0; i < users.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        const auto& user = users[i];
        out << R"({"id":)" << user.id
            << R"(,"username":")" << utils::json::escape(user.username) << '"'
            << R"(,"last_pixel_timestamp":)" << user.lastPixelTimestamp
            << R"(,"pixel_window_start_timestamp":)" << user.pixelWindowStartTimestamp
            << R"(,"pixels_placed_in_window":)" << user.pixelsPlacedInWindow
            << '}';
    }
    out << "]}";
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
    const auto oauthStates = std::make_shared<OAuthStateStore>();

    router.add("GET", "/health", [&pixelMap](const HttpRequest&) {
        std::ostringstream out;
        out << R"({"status":"ok","sequence":)" << pixelMap.sequence() << '}';
        return HttpResponse::json(200, out.str());
    });

    router.add("GET", "/palette", [&cfg](const HttpRequest&) {
        return HttpResponse::json(200, paletteJson(cfg.paletteSize));
    });

    router.add("GET", "/auth/discord/status", [&cfg](const HttpRequest&) {
        std::ostringstream out;
        out << R"({"enabled":)" << (isDiscordOAuthConfigured(cfg) ? "true" : "false")
            << R"(,"login_url":"/auth/discord"})";
        return HttpResponse::json(200, out.str());
    });

    router.add("GET", "/auth/discord", [&cfg, oauthStates](const HttpRequest&) {
        if (!isDiscordOAuthConfigured(cfg)) {
            return authErrorHtml("discord_not_configured");
        }

        const std::string state = oauthStates->create();
        const std::string url = "https://discord.com/oauth2/authorize"
            "?response_type=code"
            "&client_id=" + urlEncode(cfg.discordClientId) +
            "&scope=" + urlEncode("identify email") +
            "&redirect_uri=" + urlEncode(discordRedirectUri(cfg)) +
            "&state=" + urlEncode(state);
        return redirectTo(url);
    });

    router.add("GET", cfg.discordRedirectPath, [&userStore, &sessions, &cfg, oauthStates](const HttpRequest& request) {
        if (const auto error = request.queryValue("error")) {
            return authErrorHtml(*error);
        }

        const auto code = request.queryValue("code");
        const auto state = request.queryValue("state");
        if (!code || !state || !oauthStates->consume(*state)) {
            return authErrorHtml("invalid_oauth_state");
        }

        const auto discordToken = exchangeDiscordCode(cfg, *code);
        if (!discordToken) {
            return authErrorHtml("discord_token_exchange_failed");
        }

        const auto discordUser = fetchDiscordUser(discordToken->accessToken);
        if (!discordUser) {
            return authErrorHtml("discord_user_fetch_failed");
        }

        const auto userId = userStore.upsertOAuthUser("discord", discordUser->id, discordUser->username, discordUser->email);
        const auto localUser = userStore.findById(userId);
        if (!localUser) {
            return authErrorHtml("local_user_failed");
        }

        const std::string token = sessions.createSession(userId);
        return authResultHtml(token, localUser->username);
    });

    router.add("POST", "/register", [](const HttpRequest&) {
        return jsonError(410, "discord_auth_required");
    });

    router.add("POST", "/login", [](const HttpRequest&) {
        return jsonError(410, "discord_auth_required");
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

    router.add("GET", "/admin/summary", [&pixelMap, &userStore, &sessions, &cfg](const HttpRequest& request) {
        if (auto error = adminGuard(request, userStore, sessions, cfg)) {
            return *error;
        }

        std::ostringstream out;
        out << R"({"admin_username":")" << utils::json::escape(cfg.adminUsername) << '"'
            << R"(,"users_count":)" << userStore.userCount()
            << R"(,"map":{"width":)" << pixelMap.width()
            << R"(,"height":)" << pixelMap.height()
            << R"(,"sequence":)" << pixelMap.sequence()
            << '}'
            << R"(,"rules":{"cooldown_seconds":)" << cfg.cooldownSeconds
            << R"(,"pixel_quota_per_cooldown":)" << cfg.pixelQuotaPerCooldown
            << R"(,"palette_size":)" << static_cast<int>(cfg.paletteSize)
            << "}}";
        return HttpResponse::json(200, out.str());
    });

    router.add("GET", "/admin/users", [&userStore, &sessions, &cfg](const HttpRequest& request) {
        if (auto error = adminGuard(request, userStore, sessions, cfg)) {
            return *error;
        }

        return HttpResponse::json(200, adminUsersJson(userStore.adminUsers()));
    });

    router.add("POST", "/admin/users/reset-cooldown", [&userStore, &sessions, &cfg](const HttpRequest& request) {
        if (auto error = adminGuard(request, userStore, sessions, cfg)) {
            return *error;
        }

        const auto body = parseJsonBody(request);
        if (!body) {
            return jsonError(400, "invalid_json");
        }

        const auto userId = utils::json::getInt(*body, "user_id");
        if (!userId || *userId <= 0) {
            return jsonError(400, "invalid_user_id");
        }

        if (!userStore.resetPixelQuota(static_cast<std::uint64_t>(*userId))) {
            return jsonError(404, "user_not_found");
        }

        return HttpResponse::json(200, R"({"status":"reset"})");
    });
}

} // namespace pixelwar::controllers
