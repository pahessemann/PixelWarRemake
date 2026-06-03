#include "pixelwar/utils/Sha256.hpp"

#include <algorithm>
#include <cstring>

namespace pixelwar::utils {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

constexpr std::uint32_t rotateRight(std::uint32_t value, std::uint32_t bits) {
    return (value >> bits) | (value << (32U - bits));
}

constexpr std::uint32_t choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (~x & z);
}

constexpr std::uint32_t majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr std::uint32_t bigSigma0(std::uint32_t x) {
    return rotateRight(x, 2) ^ rotateRight(x, 13) ^ rotateRight(x, 22);
}

constexpr std::uint32_t bigSigma1(std::uint32_t x) {
    return rotateRight(x, 6) ^ rotateRight(x, 11) ^ rotateRight(x, 25);
}

constexpr std::uint32_t smallSigma0(std::uint32_t x) {
    return rotateRight(x, 7) ^ rotateRight(x, 18) ^ (x >> 3);
}

constexpr std::uint32_t smallSigma1(std::uint32_t x) {
    return rotateRight(x, 17) ^ rotateRight(x, 19) ^ (x >> 10);
}

std::uint32_t readBigEndian32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

void writeBigEndian32(std::uint32_t value, std::uint8_t* out) {
    out[0] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    out[1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[3] = static_cast<std::uint8_t>(value & 0xFF);
}

} // namespace

Sha256Digest sha256(const std::vector<std::uint8_t>& bytes) {
    std::array<std::uint32_t, 8> state = {
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U
    };

    std::vector<std::uint8_t> padded = bytes;
    padded.push_back(0x80U);
    while ((padded.size() % 64) != 56) {
        padded.push_back(0);
    }

    const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8U;
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xFFU));
    }

    for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            words[i] = readBigEndian32(&padded[chunk + i * 4]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            words[i] = smallSigma1(words[i - 2]) + words[i - 7] + smallSigma0(words[i - 15]) + words[i - 16];
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t t1 = h + bigSigma1(e) + choose(e, f, g) + kRoundConstants[i] + words[i];
            const std::uint32_t t2 = bigSigma0(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    Sha256Digest digest{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        writeBigEndian32(state[i], digest.data() + i * 4);
    }
    return digest;
}

Sha256Digest hmacSha256(const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& message) {
    constexpr std::size_t blockSize = 64;
    std::vector<std::uint8_t> normalizedKey = key;
    if (normalizedKey.size() > blockSize) {
        const auto digest = sha256(normalizedKey);
        normalizedKey.assign(digest.begin(), digest.end());
    }
    normalizedKey.resize(blockSize, 0);

    std::vector<std::uint8_t> outerPad(blockSize);
    std::vector<std::uint8_t> innerPad(blockSize);
    for (std::size_t i = 0; i < blockSize; ++i) {
        outerPad[i] = static_cast<std::uint8_t>(normalizedKey[i] ^ 0x5cU);
        innerPad[i] = static_cast<std::uint8_t>(normalizedKey[i] ^ 0x36U);
    }

    std::vector<std::uint8_t> inner = innerPad;
    inner.insert(inner.end(), message.begin(), message.end());
    const auto innerDigest = sha256(inner);

    std::vector<std::uint8_t> outer = outerPad;
    outer.insert(outer.end(), innerDigest.begin(), innerDigest.end());
    return sha256(outer);
}

std::vector<std::uint8_t> pbkdf2HmacSha256(
    const std::string& password,
    const std::vector<std::uint8_t>& salt,
    std::uint32_t iterations,
    std::size_t outputBytes
) {
    if (iterations == 0 || outputBytes == 0) {
        return {};
    }

    const std::vector<std::uint8_t> key(password.begin(), password.end());
    std::vector<std::uint8_t> output;
    output.reserve(outputBytes);

    std::uint32_t blockIndex = 1;
    while (output.size() < outputBytes) {
        std::vector<std::uint8_t> saltBlock = salt;
        saltBlock.push_back(static_cast<std::uint8_t>((blockIndex >> 24) & 0xFF));
        saltBlock.push_back(static_cast<std::uint8_t>((blockIndex >> 16) & 0xFF));
        saltBlock.push_back(static_cast<std::uint8_t>((blockIndex >> 8) & 0xFF));
        saltBlock.push_back(static_cast<std::uint8_t>(blockIndex & 0xFF));

        auto u = hmacSha256(key, saltBlock);
        std::array<std::uint8_t, 32> block = u;
        for (std::uint32_t i = 1; i < iterations; ++i) {
            std::vector<std::uint8_t> previous(u.begin(), u.end());
            u = hmacSha256(key, previous);
            for (std::size_t j = 0; j < block.size(); ++j) {
                block[j] ^= u[j];
            }
        }

        const std::size_t take = std::min(block.size(), outputBytes - output.size());
        output.insert(output.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(take));
        ++blockIndex;
    }

    return output;
}

bool constantTimeEqual(const std::vector<std::uint8_t>& lhs, const std::vector<std::uint8_t>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        diff |= static_cast<std::uint8_t>(lhs[i] ^ rhs[i]);
    }
    return diff == 0;
}

} // namespace pixelwar::utils

