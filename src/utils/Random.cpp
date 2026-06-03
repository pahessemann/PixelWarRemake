#include "pixelwar/utils/Random.hpp"

#include <random>

namespace pixelwar::utils {

std::vector<std::uint8_t> randomBytes(std::size_t count) {
    std::vector<std::uint8_t> bytes(count);
    std::random_device rd;

    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(rd() & 0xFF);
    }

    return bytes;
}

} // namespace pixelwar::utils

