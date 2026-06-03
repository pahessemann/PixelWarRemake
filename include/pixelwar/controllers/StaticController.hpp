#pragma once

#include "pixelwar/http/Router.hpp"

#include <filesystem>

namespace pixelwar::controllers {

void registerStaticRoutes(http::Router& router, std::filesystem::path publicDir);

} // namespace pixelwar::controllers

