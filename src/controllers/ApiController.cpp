#include "pixelwar/controllers/ApiController.hpp"

#include "pixelwar/utils/Base64.hpp"
#include "pixelwar/utils/HttpsClient.hpp"
#include "pixelwar/utils/Json.hpp"
#include "pixelwar/utils/Random.hpp"
#include "pixelwar/storage/MapBackup.hpp"

#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
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

constexpr const char* kVerifiedAuthProvider = "oidc";

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

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

struct HttpsEndpoint {
    std::string host;
    std::string path;
};

std::optional<HttpsEndpoint> parseHttpsEndpoint(const std::string& endpoint) {
    constexpr const char* prefix = "https://";
    if (endpoint.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    const std::string rest = endpoint.substr(8);
    const std::size_t slash = rest.find('/');
    if (slash == std::string::npos || slash == 0) {
        return std::nullopt;
    }

    HttpsEndpoint parsed;
    parsed.host = rest.substr(0, slash);
    parsed.path = rest.substr(slash);
    if (parsed.host.empty() || parsed.path.empty()) {
        return std::nullopt;
    }
    return parsed;
}

bool isOidcConfigured(const config::ServerConfig& cfg) {
    return !cfg.oidcClientId.empty() &&
           !cfg.oidcClientSecret.empty() &&
           !cfg.publicBaseUrl.empty() &&
           parseHttpsEndpoint(cfg.oidcAuthorizationEndpoint).has_value() &&
           parseHttpsEndpoint(cfg.oidcTokenEndpoint).has_value() &&
           parseHttpsEndpoint(cfg.oidcUserinfoEndpoint).has_value();
}

std::string oidcRedirectUri(const config::ServerConfig& cfg) {
    return trimTrailingSlash(cfg.publicBaseUrl) + cfg.oidcRedirectPath;
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
        << "<title>Connexion</title></head><body>"
        << "<script>"
        << "localStorage.setItem('pixelwar.token',\"" << utils::json::escape(token) << "\");"
        << "localStorage.setItem('pixelwar.username',\"" << utils::json::escape(username) << "\");"
        << "location.replace('/');"
        << "</script>"
        << "Connexion terminee."
        << "</body></html>";
    return htmlResponse(out.str());
}

HttpResponse authErrorHtml(const std::string& code) {
    std::ostringstream out;
    out << "<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
        << "<title>Connexion</title></head><body>"
        << "<script>"
        << "localStorage.removeItem('pixelwar.token');"
        << "localStorage.removeItem('pixelwar.username');"
        << "location.replace('/?auth_error=" << urlEncode(code) << "');"
        << "</script>"
        << "Connexion impossible."
        << "</body></html>";
    return htmlResponse(out.str());
}

struct OidcToken {
    std::string accessToken;
};

struct VerifiedIdentity {
    std::string subject;
    std::string username;
    std::string email;
};

struct IdentityResult {
    std::optional<VerifiedIdentity> identity;
    std::string error;
};

std::optional<OidcToken> exchangeOidcCode(const config::ServerConfig& cfg, const std::string& code) {
    const auto endpoint = parseHttpsEndpoint(cfg.oidcTokenEndpoint);
    if (!endpoint) {
        return std::nullopt;
    }

    const std::string body = formBody({
        {"client_id", cfg.oidcClientId},
        {"client_secret", cfg.oidcClientSecret},
        {"grant_type", "authorization_code"},
        {"code", code},
        {"redirect_uri", oidcRedirectUri(cfg)}
    });

    const auto response = utils::httpsRequest(
        "POST",
        endpoint->host,
        endpoint->path,
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
    return OidcToken{*accessToken};
}

IdentityResult fetchVerifiedIdentity(const config::ServerConfig& cfg, const std::string& accessToken) {
    const auto endpoint = parseHttpsEndpoint(cfg.oidcUserinfoEndpoint);
    if (!endpoint) {
        return {{}, "invalid_oidc_userinfo_endpoint"};
    }

    const auto response = utils::httpsRequest(
        "GET",
        endpoint->host,
        endpoint->path,
        {
            {"Authorization", "Bearer " + accessToken},
            {"Accept", "application/json"}
        },
        ""
    );
    if (response.status < 200 || response.status >= 300) {
        return {{}, "oidc_userinfo_failed"};
    }

    const auto parsed = utils::json::parseObject(response.body);
    if (!parsed) {
        return {{}, "oidc_userinfo_invalid"};
    }

    const auto subject = utils::json::getString(*parsed, "sub");
    const auto email = utils::json::getString(*parsed, "email");
    const auto emailVerified = utils::json::getBool(*parsed, "email_verified");
    if (!subject || subject->empty()) {
        return {{}, "missing_subject"};
    }
    if (!email || email->empty()) {
        return {{}, "missing_email"};
    }
    if (!emailVerified || !*emailVerified) {
        return {{}, "email_not_verified"};
    }

    std::string username = utils::json::getString(*parsed, "preferred_username")
        .value_or(utils::json::getString(*parsed, "name").value_or(*email));
    return {VerifiedIdentity{*subject, username, *email}, {}};
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

std::optional<HttpResponse> verifiedSessionGuard(
    const HttpRequest& request,
    const storage::UserStore& userStore,
    pixelwar::security::SessionManager& sessions,
    std::uint64_t& userId,
    const std::optional<pixelwar::utils::json::Object>& body = std::nullopt
) {
    const auto token = tokenFromRequest(request, body);
    if (!token) {
        return jsonError(401, "missing_token");
    }

    const auto sessionUserId = sessions.validate(*token);
    if (!sessionUserId) {
        return jsonError(401, "invalid_token");
    }

    const auto user = userStore.findById(*sessionUserId);
    if (!user || user->oauthProvider != kVerifiedAuthProvider || user->oauthSubject.empty() || user->email.empty()) {
        return jsonError(403, "verified_email_auth_required");
    }

    userId = *sessionUserId;
    return std::nullopt;
}

std::optional<HttpResponse> adminGuard(
    const HttpRequest& request,
    const storage::UserStore& userStore,
    pixelwar::security::SessionManager& sessions,
    const config::ServerConfig& cfg
) {
    std::uint64_t userId = 0;
    if (auto error = verifiedSessionGuard(request, userStore, sessions, userId)) {
        return *error;
    }

    const auto user = userStore.findById(userId);
    if (!user) {
        return jsonError(403, "forbidden");
    }
    if (!cfg.adminOidcSubject.empty()) {
        if (user->oauthSubject != cfg.adminOidcSubject) {
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

std::string backupEntryJson(const storage::MapBackupEntry& backup) {
    std::ostringstream out;
    out << R"({"id":")" << utils::json::escape(backup.id) << '"'
        << R"(,"created_at":)" << backup.createdAt
        << R"(,"reason":")" << utils::json::escape(backup.reason) << '"'
        << R"(,"sequence":)" << backup.sequence
        << R"(,"bytes":)" << backup.bytes
        << R"(,"screenshot":)" << (backup.screenshot ? "true" : "false")
        << '}';
    return out.str();
}

std::string backupsJson(const std::vector<storage::MapBackupEntry>& backups) {
    std::ostringstream out;
    out << R"({"backups":[)";
    for (std::size_t i = 0; i < backups.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << backupEntryJson(backups[i]);
    }
    out << "]}";
    return out.str();
}

HttpResponse binaryFileResponse(const std::filesystem::path& path, const std::string& contentType) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return jsonError(404, "not_found");
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    HttpResponse response;
    response.status = 200;
    response.body = buffer.str();
    response.headers["Content-Type"] = contentType;
    response.headers["Cache-Control"] = "no-store";
    response.headers["Server"] = "PixelWarRemake";
    response.headers["Connection"] = "close";
    response.headers["Access-Control-Allow-Origin"] = "*";
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
    response.headers["Content-Disposition"] = "inline; filename=\"" + path.filename().string() + "\"";
    return response;
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

    router.add("GET", "/auth/status", [&cfg](const HttpRequest&) {
        std::ostringstream out;
        out << R"({"enabled":)" << (isOidcConfigured(cfg) ? "true" : "false")
            << R"(,"provider":")" << utils::json::escape(cfg.oidcProviderName) << '"'
            << R"(,"login_url":"/auth/login"})";
        return HttpResponse::json(200, out.str());
    });

    router.add("GET", "/auth/login", [&cfg, oauthStates](const HttpRequest&) {
        if (!isOidcConfigured(cfg)) {
            return authErrorHtml("oidc_not_configured");
        }

        const auto endpoint = parseHttpsEndpoint(cfg.oidcAuthorizationEndpoint);
        if (!endpoint) {
            return authErrorHtml("invalid_oidc_authorization_endpoint");
        }

        const std::string state = oauthStates->create();
        const std::string url = cfg.oidcAuthorizationEndpoint +
            "?response_type=code"
            "&client_id=" + urlEncode(cfg.oidcClientId) +
            "&scope=" + urlEncode("openid profile email") +
            "&redirect_uri=" + urlEncode(oidcRedirectUri(cfg)) +
            "&state=" + urlEncode(state);
        return redirectTo(url);
    });

    router.add("GET", cfg.oidcRedirectPath, [&userStore, &sessions, &cfg, oauthStates](const HttpRequest& request) {
        if (const auto error = request.queryValue("error")) {
            return authErrorHtml(*error);
        }

        const auto code = request.queryValue("code");
        const auto state = request.queryValue("state");
        if (!code || !state || !oauthStates->consume(*state)) {
            return authErrorHtml("invalid_oauth_state");
        }

        const auto oidcToken = exchangeOidcCode(cfg, *code);
        if (!oidcToken) {
            return authErrorHtml("oidc_token_exchange_failed");
        }

        const auto identity = fetchVerifiedIdentity(cfg, oidcToken->accessToken);
        if (!identity.identity) {
            return authErrorHtml(identity.error.empty() ? "oidc_user_fetch_failed" : identity.error);
        }

        const auto userId = userStore.upsertOAuthUser(
            kVerifiedAuthProvider,
            identity.identity->subject,
            identity.identity->username,
            identity.identity->email
        );
        const auto localUser = userStore.findById(userId);
        if (!localUser) {
            return authErrorHtml("local_user_failed");
        }

        const std::string token = sessions.createSession(userId);
        return authResultHtml(token, localUser->username);
    });

    router.add("POST", "/register", [](const HttpRequest&) {
        return jsonError(410, "verified_email_auth_required");
    });

    router.add("POST", "/login", [](const HttpRequest&) {
        return jsonError(410, "verified_email_auth_required");
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

        std::uint64_t userId = 0;
        if (auto error = verifiedSessionGuard(request, userStore, sessions, userId, body)) {
            return *error;
        }

        if (!rateLimiter.allow("pixel:" + std::to_string(userId), 120, std::chrono::minutes(1))) {
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
        if (!userStore.consumePixelSlot(userId, cfg.cooldownSeconds, cfg.pixelQuotaPerCooldown, quotaStatus)) {
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
        std::uint64_t userId = 0;
        if (auto error = verifiedSessionGuard(request, userStore, sessions, userId)) {
            return *error;
        }

        const auto status = userStore.pixelQuotaStatus(userId, cfg.cooldownSeconds, cfg.pixelQuotaPerCooldown);
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

    router.add("GET", "/admin/backups", [&userStore, &sessions, &cfg](const HttpRequest& request) {
        if (auto error = adminGuard(request, userStore, sessions, cfg)) {
            return *error;
        }

        return HttpResponse::json(200, backupsJson(storage::listMapBackups(cfg.dataDir)));
    });

    router.add("POST", "/admin/backups/create", [&pixelMap, &userStore, &sessions, &cfg](const HttpRequest& request) {
        if (auto error = adminGuard(request, userStore, sessions, cfg)) {
            return *error;
        }

        const auto body = parseJsonBody(request);
        const std::string reason = body ? utils::json::getString(*body, "reason").value_or("manual") : "manual";
        const bool includeScreenshot = body ? utils::json::getBool(*body, "screenshot").value_or(false) : false;

        try {
            const auto backup = storage::createMapBackup(pixelMap, cfg.dataDir, reason, includeScreenshot);
            return HttpResponse::json(200, R"({"backup":)" + backupEntryJson(backup) + "}");
        } catch (const std::exception&) {
            return jsonError(500, "backup_failed");
        }
    });

    router.add("POST", "/admin/backups/rollback", [&pixelMap, &userStore, &sessions, &cfg, mapPath](const HttpRequest& request) {
        if (auto error = adminGuard(request, userStore, sessions, cfg)) {
            return *error;
        }

        const auto body = parseJsonBody(request);
        if (!body) {
            return jsonError(400, "invalid_json");
        }

        const auto id = utils::json::getString(*body, "id");
        if (!id || !storage::findMapBackup(cfg.dataDir, *id)) {
            return jsonError(404, "backup_not_found");
        }

        try {
            const auto beforeRollback = storage::createMapBackup(pixelMap, cfg.dataDir, "before-rollback", true);
            if (!storage::restoreMapBackup(pixelMap, cfg.dataDir, *id, mapPath)) {
                return jsonError(500, "rollback_failed");
            }
            return HttpResponse::json(
                200,
                R"({"status":"rolled_back","restored_id":")" + utils::json::escape(*id) +
                    R"(","safety_backup":)" + backupEntryJson(beforeRollback) + "}"
            );
        } catch (const std::exception&) {
            return jsonError(500, "rollback_failed");
        }
    });

    router.add("POST", "/admin/map/reset", [&pixelMap, &userStore, &sessions, &cfg, mapPath](const HttpRequest& request) {
        if (auto error = adminGuard(request, userStore, sessions, cfg)) {
            return *error;
        }

        const auto body = parseJsonBody(request);
        const auto color = body ? utils::json::getInt(*body, "color").value_or(0) : 0;
        if (color < 0 || color >= cfg.paletteSize) {
            return jsonError(400, "invalid_color");
        }

        try {
            const auto finalBackup = storage::createMapBackup(pixelMap, cfg.dataDir, "before-reset", true);
            pixelMap.reset(static_cast<std::uint8_t>(color));
            pixelMap.saveBinary(mapPath);
            return HttpResponse::json(
                200,
                R"({"status":"reset","final_backup":)" + backupEntryJson(finalBackup) + "}"
            );
        } catch (const std::exception&) {
            return jsonError(500, "reset_failed");
        }
    });

    router.add("GET", "/admin/backups/screenshot", [&userStore, &sessions, &cfg](const HttpRequest& request) {
        if (auto error = adminGuard(request, userStore, sessions, cfg)) {
            return *error;
        }

        const auto id = request.queryValue("id");
        if (!id) {
            return jsonError(400, "missing_backup_id");
        }

        const auto path = storage::mapBackupScreenshotPath(cfg.dataDir, *id);
        if (!path) {
            return jsonError(404, "screenshot_not_found");
        }
        return binaryFileResponse(*path, "image/bmp");
    });
}

} // namespace pixelwar::controllers
