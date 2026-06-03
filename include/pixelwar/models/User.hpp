#pragma once

#include <cstdint>
#include <string>

namespace pixelwar::models {

struct User {
    std::uint64_t id = 0;
    std::string username;
    std::string passwordHash;
    std::int64_t lastPixelTimestamp = 0;
    std::int64_t pixelWindowStartTimestamp = 0;
    std::uint32_t pixelsPlacedInWindow = 0;
};

bool isValidUsername(const std::string& username);
bool isValidPassword(const std::string& password);

} // namespace pixelwar::models
