#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace pixelwar::storage {

struct PixelChange {
    std::uint64_t sequence = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint8_t color = 0;
};

class PixelMap {
public:
    PixelMap(std::size_t width, std::size_t height, std::uint8_t paletteSize);

    [[nodiscard]] std::size_t width() const;
    [[nodiscard]] std::size_t height() const;
    [[nodiscard]] std::uint8_t paletteSize() const;
    [[nodiscard]] std::uint64_t sequence() const;

    bool loadBinary(const std::filesystem::path& path);
    void saveBinary(const std::filesystem::path& path) const;

    bool setPixel(std::size_t x, std::size_t y, std::uint8_t color, PixelChange& change);
    [[nodiscard]] std::string toJson(std::optional<std::uint64_t> since) const;
    [[nodiscard]] bool isInBounds(std::size_t x, std::size_t y) const;

private:
    [[nodiscard]] std::string fullJsonLocked() const;
    [[nodiscard]] std::string diffJsonLocked(std::uint64_t since) const;
    [[nodiscard]] bool canServeDiffLocked(std::uint64_t since) const;
    void rebuildCacheLocked() const;

    std::size_t width_;
    std::size_t height_;
    std::uint8_t paletteSize_;
    std::vector<std::uint8_t> pixels_;
    std::uint64_t sequence_ = 0;
    std::vector<PixelChange> changes_;
    std::size_t maxChanges_ = 100000;

    mutable std::shared_mutex mutex_;
    mutable bool cacheDirty_ = true;
    mutable std::string cachedRleBase64_;
};

} // namespace pixelwar::storage

