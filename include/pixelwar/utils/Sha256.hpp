#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pixelwar::utils {

using Sha256Digest = std::array<std::uint8_t, 32>;

Sha256Digest sha256(const std::vector<std::uint8_t>& bytes);
Sha256Digest hmacSha256(const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& message);
std::vector<std::uint8_t> pbkdf2HmacSha256(
    const std::string& password,
    const std::vector<std::uint8_t>& salt,
    std::uint32_t iterations,
    std::size_t outputBytes
);
bool constantTimeEqual(const std::vector<std::uint8_t>& lhs, const std::vector<std::uint8_t>& rhs);

} // namespace pixelwar::utils

