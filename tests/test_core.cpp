#if defined(PIXELWAR_HAS_CATCH2)
#include <catch2/catch_test_macros.hpp>
#else
#include "minicatch.hpp"
#endif

#include "pixelwar/security/PasswordHasher.hpp"
#include "pixelwar/security/RateLimiter.hpp"
#include "pixelwar/security/SessionManager.hpp"
#include "pixelwar/storage/MapBackup.hpp"
#include "pixelwar/models/User.hpp"
#include "pixelwar/config/ServerConfig.hpp"
#include "pixelwar/controllers/ApiController.hpp"
#include "pixelwar/controllers/StaticController.hpp"
#include "pixelwar/http/Router.hpp"
#include "pixelwar/storage/PixelMap.hpp"
#include "pixelwar/storage/UserStore.hpp"
#include "pixelwar/utils/Base64.hpp"
#include "pixelwar/utils/Json.hpp"
#include "pixelwar/utils/Sha256.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("base64 roundtrip") {
    const std::vector<std::uint8_t> input = {0, 1, 2, 3, 4, 250, 251, 252};
    const auto encoded = pixelwar::utils::base64Encode(input);
    const auto decoded = pixelwar::utils::base64Decode(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == input);
}

TEST_CASE("json flat object parser") {
    const auto parsed = pixelwar::utils::json::parseObject(R"({"username":"paul","x":12,"ready":true})");
    REQUIRE(parsed.has_value());
    REQUIRE(pixelwar::utils::json::getString(*parsed, "username").value() == "paul");
    REQUIRE(pixelwar::utils::json::getInt(*parsed, "x").value() == 12);
    REQUIRE(pixelwar::utils::json::getBool(*parsed, "ready").value());
}

TEST_CASE("user validation rejects unsafe names and weak passwords") {
    REQUIRE(pixelwar::models::isValidUsername("paul_42"));
    REQUIRE(!pixelwar::models::isValidUsername("pa"));
    REQUIRE(!pixelwar::models::isValidUsername("paul admin"));
    REQUIRE(pixelwar::models::isValidPassword("motdepasse-solide"));
    REQUIRE(!pixelwar::models::isValidPassword("court"));
}

TEST_CASE("sha256 known vector") {
    const std::string text = "abc";
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    const auto digest = pixelwar::utils::sha256(bytes);
    const std::vector<std::uint8_t> digestBytes(digest.begin(), digest.end());
    REQUIRE(pixelwar::utils::base64Encode(digestBytes) == "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");
}

TEST_CASE("password hash verifies") {
    const auto hash = pixelwar::security::PasswordHasher::hashPassword("motdepasse-solide");
    REQUIRE(pixelwar::security::PasswordHasher::verifyPassword("motdepasse-solide", hash));
    REQUIRE(!pixelwar::security::PasswordHasher::verifyPassword("autre", hash));
}

TEST_CASE("sessions validate until revoked") {
    pixelwar::security::SessionManager sessions(std::chrono::seconds(60));
    const auto token = sessions.createSession(42);
    REQUIRE(sessions.validate(token).value() == 42);
    sessions.revoke(token);
    REQUIRE(!sessions.validate(token).has_value());
}

TEST_CASE("rate limiter blocks after quota") {
    pixelwar::security::RateLimiter limiter;
    REQUIRE(limiter.allow("client", 2, std::chrono::seconds(60)));
    REQUIRE(limiter.allow("client", 2, std::chrono::seconds(60)));
    REQUIRE(!limiter.allow("client", 2, std::chrono::seconds(60)));
}

TEST_CASE("pixel map returns diffs") {
    pixelwar::storage::PixelMap map(4, 4, 16);
    pixelwar::storage::PixelChange change;
    REQUIRE(map.setPixel(1, 2, 3, change));
    REQUIRE(change.sequence == 1);
    const auto diff = map.toJson(0);
    REQUIRE(diff.find(R"("type":"full")") != std::string::npos);
    const auto diff2 = map.toJson(1);
    REQUIRE(diff2.find(R"("type":"diff")") != std::string::npos);
}

TEST_CASE("map backups create screenshots and restore map state") {
    const auto dir = std::filesystem::current_path() / "map_backups_test";
    std::filesystem::remove_all(dir);

    pixelwar::storage::PixelMap map(4, 4, 16);
    pixelwar::storage::PixelChange change;
    REQUIRE(map.setPixel(1, 1, 3, change));
    REQUIRE(map.sequence() == 1);

    const auto backup = pixelwar::storage::createMapBackup(map, dir, "manual", true);
    REQUIRE(!backup.id.empty());
    REQUIRE(backup.sequence == 1);
    REQUIRE(backup.screenshot);
    REQUIRE(pixelwar::storage::mapBackupFilePath(dir, backup.id).has_value());
    REQUIRE(pixelwar::storage::mapBackupScreenshotPath(dir, backup.id).has_value());

    const auto backups = pixelwar::storage::listMapBackups(dir);
    REQUIRE(backups.size() == 1);
    REQUIRE(backups.front().id == backup.id);

    const auto liveMapPath = dir / "map.pwm";
    map.reset(0);
    map.saveBinary(liveMapPath);
    REQUIRE(map.sequence() == 2);

    REQUIRE(pixelwar::storage::restoreMapBackup(map, dir, backup.id, liveMapPath));
    REQUIRE(map.sequence() == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("user store allows three pixels per cooldown window") {
    const auto path = std::filesystem::current_path() / "quota_users_test.db";
    std::filesystem::remove(path);

    pixelwar::storage::UserStore store(path);
    const auto userId = store.upsertOAuthUser("discord", "quota-subject", "quota_user", "quota@example.test");
    REQUIRE(userId > 0);

    auto status = store.pixelQuotaStatus(userId, 600, 3);
    REQUIRE(status.remainingPlacements == 3);

    REQUIRE(store.consumePixelSlot(userId, 600, 3, status));
    REQUIRE(status.remainingPlacements == 2);
    REQUIRE(store.consumePixelSlot(userId, 600, 3, status));
    REQUIRE(status.remainingPlacements == 1);
    REQUIRE(store.consumePixelSlot(userId, 600, 3, status));
    REQUIRE(status.remainingPlacements == 0);
    REQUIRE(!store.consumePixelSlot(userId, 600, 3, status));
    REQUIRE(status.remainingPlacements == 0);
    REQUIRE(status.remainingSeconds > 0);

    REQUIRE(store.resetPixelQuota(userId));
    status = store.pixelQuotaStatus(userId, 600, 3);
    REQUIRE(status.remainingPlacements == 3);
    REQUIRE(status.remainingSeconds == 0);

    std::filesystem::remove(path);
}

TEST_CASE("oauth users are created without local password registration") {
    const auto path = std::filesystem::current_path() / "oauth_users_test.db";
    std::filesystem::remove(path);

    pixelwar::storage::UserStore store(path);
    REQUIRE(store.upsertOAuthUser("google", "google-123", "Google User", "google@example.test") == 0);
    REQUIRE(store.userCount() == 0);

    const auto userId = store.upsertOAuthUser("discord", "123456789", "Discord User", "user@example.test");
    REQUIRE(userId > 0);

    const auto user = store.findById(userId);
    REQUIRE(user.has_value());
    REQUIRE(user->oauthProvider == "discord");
    REQUIRE(user->oauthSubject == "123456789");
    REQUIRE(user->email == "user@example.test");
    REQUIRE(user->passwordHash.empty());

    const auto sameUserId = store.upsertOAuthUser("discord", "123456789", "Another Name", "next@example.test");
    REQUIRE(sameUserId == userId);

    std::filesystem::remove(path);
}

TEST_CASE("user store ignores legacy password accounts on load") {
    const auto path = std::filesystem::current_path() / "legacy_users_test.db";
    std::filesystem::remove(path);

    {
        std::ofstream file(path, std::ios::trunc);
        file << "1\tbGVnYWN5\toldhash\t0\t0\t0\n";
        file << "2\tZGlzY29yZF91c2Vy\t\t0\t0\t0\tZGlzY29yZA==\tMTIz\tZGlzY29yZEBleGFtcGxlLnRlc3Q=\n";
    }

    pixelwar::storage::UserStore store(path);
    REQUIRE(store.load());
    REQUIRE(store.userCount() == 1);

    const auto discordUser = store.findById(2);
    REQUIRE(discordUser.has_value());
    REQUIRE(discordUser->username == "discord_user");
    REQUIRE(discordUser->oauthProvider == "discord");
    REQUIRE(discordUser->oauthSubject == "123");

    std::string error;
    REQUIRE(!store.registerUser("legacy", "motdepasse-solide", error));
    REQUIRE(error == "discord_auth_required");
    REQUIRE(!store.verifyCredentials("discord_user", "motdepasse-solide").has_value());

    std::filesystem::remove(path);
}

TEST_CASE("api disables password register and login routes") {
    const auto path = std::filesystem::current_path() / "api_users_test.db";
    std::filesystem::remove(path);

    pixelwar::http::Router router;
    pixelwar::storage::PixelMap map(4, 4, 16);
    pixelwar::storage::UserStore store(path);
    pixelwar::security::SessionManager sessions(std::chrono::seconds(60));
    pixelwar::security::RateLimiter limiter;
    pixelwar::config::ServerConfig cfg;
    cfg.dataDir = std::filesystem::current_path();

    pixelwar::controllers::registerApiRoutes(router, map, store, sessions, limiter, cfg);

    pixelwar::http::HttpRequest request;
    request.method = "POST";
    request.path = "/register";
    request.body = R"({"username":"paul","password":"motdepasse-solide"})";
    REQUIRE(router.dispatch(request).status == 410);

    request.path = "/login";
    REQUIRE(router.dispatch(request).status == 410);

    request.method = "GET";
    request.path = "/auth/discord/status";
    request.body.clear();
    const auto status = router.dispatch(request);
    REQUIRE(status.status == 200);
    REQUIRE(status.body.find(R"("enabled":false)") != std::string::npos);

    const auto legacyToken = sessions.createSession(999);
    request.method = "GET";
    request.path = "/cooldown";
    request.headers["authorization"] = "Bearer " + legacyToken;
    const auto legacyCooldown = router.dispatch(request);
    REQUIRE(legacyCooldown.status == 403);
    REQUIRE(legacyCooldown.body.find("discord_auth_required") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("admin api manages map backups and reset") {
    const auto dir = std::filesystem::current_path() / "admin_backup_api_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    pixelwar::http::Router router;
    pixelwar::storage::PixelMap map(4, 4, 16);
    pixelwar::storage::UserStore store(dir / "users.db");
    const auto adminUserId = store.upsertOAuthUser("discord", "admin-subject", "Admin User", "admin@example.test");
    pixelwar::security::SessionManager sessions(std::chrono::seconds(60));
    const auto token = sessions.createSession(adminUserId);
    pixelwar::security::RateLimiter limiter;
    pixelwar::config::ServerConfig cfg;
    cfg.dataDir = dir;
    cfg.adminUsername = "admin_user";

    pixelwar::controllers::registerApiRoutes(router, map, store, sessions, limiter, cfg);

    pixelwar::http::HttpRequest request;
    request.method = "POST";
    request.path = "/admin/backups/create";
    request.headers["authorization"] = "Bearer " + token;
    request.body = R"({"reason":"manual","screenshot":true})";
    const auto createResponse = router.dispatch(request);
    REQUIRE(createResponse.status == 200);
    REQUIRE(createResponse.body.find(R"("screenshot":true)") != std::string::npos);

    request.method = "GET";
    request.path = "/admin/backups";
    request.body.clear();
    const auto listResponse = router.dispatch(request);
    REQUIRE(listResponse.status == 200);
    REQUIRE(listResponse.body.find(R"("backups":[)") != std::string::npos);
    REQUIRE(listResponse.body.find("manual") != std::string::npos);

    request.method = "POST";
    request.path = "/admin/map/reset";
    request.body = R"({"color":0})";
    const auto resetResponse = router.dispatch(request);
    REQUIRE(resetResponse.status == 200);
    REQUIRE(resetResponse.body.find(R"("status":"reset")") != std::string::npos);
    REQUIRE(resetResponse.body.find("before-reset") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("static controller serves frontend") {
    pixelwar::http::Router router;
    pixelwar::controllers::registerStaticRoutes(router, std::filesystem::path(PIXELWAR_SOURCE_DIR) / "public");

    pixelwar::http::HttpRequest request;
    request.method = "GET";
    request.path = "/";

    const auto response = router.dispatch(request);
    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("PixelWarRemake") != std::string::npos);

    request.path = "/gestion";
    const auto adminResponse = router.dispatch(request);
    REQUIRE(adminResponse.status == 200);
    REQUIRE(adminResponse.body.find("Gestion") != std::string::npos);
}
