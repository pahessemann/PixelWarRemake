#include "pixelwar/storage/AuditLog.hpp"

#include "pixelwar/utils/Base64.hpp"

#include <algorithm>
#include <fstream>
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

bool parseTimestamp(const std::string& text, std::int64_t& value) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoll(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

AuditLog::AuditLog(std::filesystem::path path, std::size_t maxEntries)
    : path_(std::move(path)),
      maxEntries_(std::max<std::size_t>(1, maxEntries)) {}

bool AuditLog::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(path_);
    if (!file) {
        return false;
    }

    entries_.clear();
    std::string line;
    while (std::getline(file, line)) {
        const auto parts = splitTabs(line);
        if (parts.size() != 4) {
            continue;
        }

        AuditEntry entry;
        if (!parseTimestamp(parts[0], entry.timestamp)) {
            continue;
        }
        entry.actor = decodeText(parts[1]);
        entry.action = decodeText(parts[2]);
        entry.detail = decodeText(parts[3]);
        entries_.push_back(std::move(entry));
    }

    if (entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - maxEntries_));
    }
    return true;
}

void AuditLog::append(const std::string& actor, const std::string& action, const std::string& detail, std::int64_t timestamp) {
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    AuditEntry entry{timestamp, actor, action, detail};
    entries_.push_back(entry);
    if (entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - maxEntries_));
    }

    std::ofstream file(path_, std::ios::app);
    if (file) {
        file << timestamp << '\t'
             << encodeText(actor) << '\t'
             << encodeText(action) << '\t'
             << encodeText(detail) << '\n';
    }
}

std::vector<AuditEntry> AuditLog::latest(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(limit, entries_.size());
    return std::vector<AuditEntry>(entries_.end() - static_cast<std::ptrdiff_t>(count), entries_.end());
}

} // namespace pixelwar::storage
