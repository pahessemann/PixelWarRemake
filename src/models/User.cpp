#include "pixelwar/models/User.hpp"

#include <algorithm>
#include <cctype>

namespace pixelwar::models {

bool isValidUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 32) {
        return false;
    }

    return std::all_of(username.begin(), username.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

bool isValidPassword(const std::string& password) {
    return password.size() >= 8 && password.size() <= 256;
}

} // namespace pixelwar::models

