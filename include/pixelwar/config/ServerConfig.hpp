#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pixelwar::config {

struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::size_t mapWidth = 1000;
    std::size_t mapHeight = 1000;
    std::uint8_t paletteSize = 16;
    std::int64_t cooldownSeconds = 600;
    std::uint32_t pixelQuotaPerCooldown = 3;
    std::int64_t sessionTtlSeconds = 86400;
    std::size_t threadPoolSize = 8;
    std::size_t maxBodyBytes = 8192;
    std::string adminUsername = "pahessemann";
    std::string adminOidcSubject;
    std::string publicBaseUrl = "http://127.0.0.1:8080";
    std::string oidcProviderName = "Google";
    std::string oidcAuthorizationEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
    std::string oidcTokenEndpoint = "https://oauth2.googleapis.com/token";
    std::string oidcUserinfoEndpoint = "https://openidconnect.googleapis.com/v1/userinfo";
    std::string oidcClientId;
    std::string oidcClientSecret;
    std::string oidcRedirectPath = "/auth/callback";
    std::filesystem::path dataDir = "data";
};

ServerConfig loadServerConfig(const std::filesystem::path& path);

} // namespace pixelwar::config
