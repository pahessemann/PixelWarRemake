#include "pixelwar/security/PasswordHasher.hpp"

#include "pixelwar/utils/Base64.hpp"
#include "pixelwar/utils/Random.hpp"
#include "pixelwar/utils/Sha256.hpp"

#include <charconv>
#include <cstdint>
#include <sstream>
#include <system_error>
#include <vector>

namespace pixelwar::security {

namespace {

constexpr std::uint32_t kIterations = 120000;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(delimiter, start);
        parts.push_back(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

bool parseUint32(const std::string& text, std::uint32_t& out) {
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

} // namespace

std::string PasswordHasher::hashPassword(const std::string& password) {
    const auto salt = utils::randomBytes(kSaltBytes);
    const auto hash = utils::pbkdf2HmacSha256(password, salt, kIterations, kHashBytes);

    std::ostringstream out;
    out << "pbkdf2_sha256$" << kIterations << '$' << utils::base64Encode(salt) << '$' << utils::base64Encode(hash);
    return out.str();
}

bool PasswordHasher::verifyPassword(const std::string& password, const std::string& encodedHash) {
    const auto parts = split(encodedHash, '$');
    if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") {
        return false;
    }

    std::uint32_t iterations = 0;
    if (!parseUint32(parts[1], iterations) || iterations == 0) {
        return false;
    }

    const auto salt = utils::base64Decode(parts[2]);
    const auto expected = utils::base64Decode(parts[3]);
    if (!salt || !expected || expected->empty()) {
        return false;
    }

    const auto actual = utils::pbkdf2HmacSha256(password, *salt, iterations, expected->size());
    return utils::constantTimeEqual(actual, *expected);
}

} // namespace pixelwar::security
