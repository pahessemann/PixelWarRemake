#include "pixelwar/storage/PixelHistory.hpp"

#include "pixelwar/utils/Base64.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace pixelwar::storage {

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t end = line.find('\t', start);
        parts.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

std::string encodeText(const std::string& text) {
    return utils::base64Encode(std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::string decodeText(const std::string& encoded) {
    const auto bytes = utils::base64Decode(encoded);
    if (!bytes) {
        return {};
    }
    return std::string(bytes->begin(), bytes->end());
}

template <typename T>
bool parseInteger(const std::string& text, T& value) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoll(text, &consumed, 10);
        if (consumed != text.size() || parsed < 0) {
            return false;
        }
        if (static_cast<unsigned long long>(parsed) > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
            return false;
        }
        value = static_cast<T>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

PixelHistory::PixelHistory(std::filesystem::path path, std::size_t maxEntries)
    : path_(std::move(path)),
      maxEntries_(std::max<std::size_t>(1, maxEntries)) {}

bool PixelHistory::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(path_);
    if (!file) {
        return false;
    }

    entries_.clear();
    std::string line;
    while (std::getline(file, line)) {
        const auto parts = splitTabs(line);
        if (parts.size() != 6) {
            continue;
        }

        PixelHistoryEntry entry;
        std::uint32_t color = 0;
        if (!parseInteger(parts[0], entry.sequence) ||
            !parseInteger(parts[1], entry.x) ||
            !parseInteger(parts[2], entry.y) ||
            !parseInteger(parts[3], color) ||
            !parseInteger(parts[5], entry.timestamp)) {
            continue;
        }
        if (color > 255) {
            continue;
        }
        entry.color = static_cast<std::uint8_t>(color);
        entry.username = decodeText(parts[4]);
        entries_.push_back(std::move(entry));
    }

    if (entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - maxEntries_));
    }
    return true;
}

void PixelHistory::append(const PixelChange& change, const std::string& username, std::int64_t timestamp) {
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    PixelHistoryEntry entry{change.sequence, change.x, change.y, change.color, username, timestamp};
    entries_.push_back(entry);
    if (entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - maxEntries_));
    }

    std::ofstream file(path_, std::ios::app);
    if (file) {
        file << entry.sequence << '\t'
             << entry.x << '\t'
             << entry.y << '\t'
             << static_cast<int>(entry.color) << '\t'
             << encodeText(entry.username) << '\t'
             << entry.timestamp << '\n';
    }
}

std::vector<PixelHistoryEntry> PixelHistory::latest(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(limit, entries_.size());
    return std::vector<PixelHistoryEntry>(entries_.end() - static_cast<std::ptrdiff_t>(count), entries_.end());
}

} // namespace pixelwar::storage
