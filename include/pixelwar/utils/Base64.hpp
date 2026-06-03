#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pixelwar::utils {

std::string base64Encode(const std::vector<std::uint8_t>& bytes);
std::string base64UrlEncode(const std::vector<std::uint8_t>& bytes);
std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& encoded);

} // namespace pixelwar::utils

