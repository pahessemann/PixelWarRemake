#if defined(PIXELWAR_HAS_CATCH2)
#include <catch2/catch_test_macros.hpp>
#else
#include "minicatch.hpp"
#endif

#include "pixelwar/security/PasswordHasher.hpp"
#include "pixelwar/security/RateLimiter.hpp"
#include "pixelwar/security/SessionManager.hpp"
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

TEST_CASE("user store allows three pixels per cooldown window") {
    const auto path = std::filesystem::current_path() / "quota_users_test.db";
    std::filesystem::remove(path);

    pixelwar::storage::UserStore store(path);
    std::string error;
    REQUIRE(store.registerUser("quota_user", "motdepasse-solide", error));
    const auto userId = store.verifyCredentials("quota_user", "motdepasse-solide");
    REQUIRE(userId.has_value());

    auto status = store.pixelQuotaStatus(*userId, 600, 3);
    REQUIRE(status.remainingPlacements == 3);

    REQUIRE(store.consumePixelSlot(*userId, 600, 3, status));
    REQUIRE(status.remainingPlacements == 2);
    REQUIRE(store.consumePixelSlot(*userId, 600, 3, status));
    REQUIRE(status.remainingPlacements == 1);
    REQUIRE(store.consumePixelSlot(*userId, 600, 3, status));
    REQUIRE(status.remainingPlacements == 0);
    REQUIRE(!store.consumePixelSlot(*userId, 600, 3, status));
    REQUIRE(status.remainingPlacements == 0);
    REQUIRE(status.remainingSeconds > 0);

    REQUIRE(store.resetPixelQuota(*userId));
    status = store.pixelQuotaStatus(*userId, 600, 3);
    REQUIRE(status.remainingPlacements == 3);
    REQUIRE(status.remainingSeconds == 0);

    std::filesystem::remove(path);
}

TEST_CASE("oauth users are created without local password registration") {
    const auto path = std::filesystem::current_path() / "oauth_users_test.db";
    std::filesystem::remove(path);

    pixelwar::storage::UserStore store(path);
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

    std::filesystem::remove(path);
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
