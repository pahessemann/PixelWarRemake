#include "pixelwar/config/ServerConfig.hpp"
#include "pixelwar/controllers/ApiController.hpp"
#include "pixelwar/controllers/StaticController.hpp"
#include "pixelwar/http/HttpServer.hpp"
#include "pixelwar/http/Router.hpp"
#include "pixelwar/security/RateLimiter.hpp"
#include "pixelwar/security/SessionManager.hpp"
#include "pixelwar/storage/PixelMap.hpp"
#include "pixelwar/storage/UserStore.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    const std::filesystem::path configPath = argc > 1 ? argv[1] : "config/server.json";
    auto cfg = pixelwar::config::loadServerConfig(configPath);

    try {
        std::filesystem::create_directories(cfg.dataDir);

        pixelwar::storage::PixelMap pixelMap(cfg.mapWidth, cfg.mapHeight, cfg.paletteSize);
        const auto mapPath = cfg.dataDir / "map.pwm";
        if (!pixelMap.loadBinary(mapPath)) {
            pixelMap.saveBinary(mapPath);
        }

        pixelwar::storage::UserStore userStore(cfg.dataDir / "users.db");
        userStore.load();

        pixelwar::security::SessionManager sessions(std::chrono::seconds(cfg.sessionTtlSeconds));
        pixelwar::security::RateLimiter rateLimiter;
        pixelwar::http::Router router;

        pixelwar::controllers::registerStaticRoutes(router, "public");
        pixelwar::controllers::registerApiRoutes(router, pixelMap, userStore, sessions, rateLimiter, cfg);

        pixelwar::http::HttpServer server(cfg.host, cfg.port, cfg.threadPoolSize, cfg.maxBodyBytes, router);
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
