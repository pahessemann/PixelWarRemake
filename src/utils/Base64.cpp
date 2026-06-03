#include "pixelwar/utils/Base64.hpp"

#include <array>

namespace pixelwar::utils {

namespace {

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kBase64Url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string encodeWithAlphabet(const std::vector<std::uint8_t>& bytes, const char* alphabet, bool padding) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t b0 = bytes[i];
        const std::uint32_t b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const std::uint32_t b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        const std::uint32_t value = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(alphabet[(value >> 18) & 0x3F]);
        out.push_back(alphabet[(value >> 12) & 0x3F]);
        if (i + 1 < bytes.size()) {
            out.push_back(alphabet[(value >> 6) & 0x3F]);
        } else if (padding) {
            out.push_back('=');
        }

        if (i + 2 < bytes.size()) {
            out.push_back(alphabet[value & 0x3F]);
        } else if (padding) {
            out.push_back('=');
        }
    }

    return out;
}

int decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+' || c == '-') {
        return 62;
    }
    if (c == '/' || c == '_') {
        return 63;
    }
    return -1;
}

} // namespace

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
    return encodeWithAlphabet(bytes, kBase64, true);
}

std::string base64UrlEncode(const std::vector<std::uint8_t>& bytes) {
    return encodeWithAlphabet(bytes, kBase64Url, false);
}

std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& encoded) {
    std::vector<std::uint8_t> out;
    int value = 0;
    int bits = -8;

    for (char c : encoded) {
        if (c == '=') {
            break;
        }
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            continue;
        }

        const int decoded = decodeChar(c);
        if (decoded < 0) {
            return std::nullopt;
        }

        value = (value << 6) | decoded;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<std::uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return out;
}

} // namespace pixelwar::utils

