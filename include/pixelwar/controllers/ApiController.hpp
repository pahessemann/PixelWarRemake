#pragma once

#include "pixelwar/config/ServerConfig.hpp"
#include "pixelwar/http/Router.hpp"
#include "pixelwar/security/RateLimiter.hpp"
#include "pixelwar/security/SessionManager.hpp"
#include "pixelwar/storage/PixelMap.hpp"
#include "pixelwar/storage/UserStore.hpp"

namespace pixelwar::controllers {

void registerApiRoutes(
    http::Router& router,
    storage::PixelMap& pixelMap,
    storage::UserStore& userStore,
    security::SessionManager& sessions,
    security::RateLimiter& rateLimiter,
    const config::ServerConfig& cfg
);

} // namespace pixelwar::controllers

