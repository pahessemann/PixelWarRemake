#pragma once

#include <string>

namespace pixelwar::security {

class PasswordHasher {
public:
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& encodedHash);
};

} // namespace pixelwar::security

