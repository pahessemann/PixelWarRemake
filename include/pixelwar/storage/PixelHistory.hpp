#pragma once

#include "pixelwar/storage/PixelMap.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace pixelwar::storage {

struct PixelHistoryEntry {
    std::uint64_t sequence = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint8_t color = 0;
    std::string username;
    std::int64_t timestamp = 0;
};

class PixelHistory {
public:
    explicit PixelHistory(std::filesystem::path path, std::size_t maxEntries = 1000);

    bool load();
    void append(const PixelChange& change, const std::string& username, std::int64_t timestamp);
    [[nodiscard]] std::vector<PixelHistoryEntry> latest(std::size_t limit) const;

private:
    std::filesystem::path path_;
    std::size_t maxEntries_;
    mutable std::mutex mutex_;
    std::vector<PixelHistoryEntry> entries_;
};

} // namespace pixelwar::storage
