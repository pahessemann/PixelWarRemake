#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace pixelwar::storage {

struct AuditEntry {
    std::int64_t timestamp = 0;
    std::string actor;
    std::string action;
    std::string detail;
};

class AuditLog {
public:
    explicit AuditLog(std::filesystem::path path, std::size_t maxEntries = 1000);

    bool load();
    void append(const std::string& actor, const std::string& action, const std::string& detail, std::int64_t timestamp);
    [[nodiscard]] std::vector<AuditEntry> latest(std::size_t limit) const;

private:
    std::filesystem::path path_;
    std::size_t maxEntries_;
    mutable std::mutex mutex_;
    std::vector<AuditEntry> entries_;
};

} // namespace pixelwar::storage
