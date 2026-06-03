#pragma once

#include "pixelwar/storage/PixelMap.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace pixelwar::storage {

struct MapBackupEntry {
    std::string id;
    std::int64_t createdAt = 0;
    std::string reason;
    std::uint64_t sequence = 0;
    std::uintmax_t bytes = 0;
    bool screenshot = false;
};

[[nodiscard]] std::filesystem::path mapBackupsDir(const std::filesystem::path& dataDir);
[[nodiscard]] std::vector<MapBackupEntry> listMapBackups(const std::filesystem::path& dataDir);
[[nodiscard]] std::optional<MapBackupEntry> findMapBackup(const std::filesystem::path& dataDir, const std::string& id);
[[nodiscard]] MapBackupEntry createMapBackup(
    PixelMap& pixelMap,
    const std::filesystem::path& dataDir,
    const std::string& reason,
    bool includeScreenshot
);
bool restoreMapBackup(
    PixelMap& pixelMap,
    const std::filesystem::path& dataDir,
    const std::string& id,
    const std::filesystem::path& liveMapPath
);
[[nodiscard]] std::optional<std::filesystem::path> mapBackupScreenshotPath(
    const std::filesystem::path& dataDir,
    const std::string& id
);
[[nodiscard]] std::optional<std::filesystem::path> mapBackupFilePath(
    const std::filesystem::path& dataDir,
    const std::string& id
);

class MapBackupScheduler {
public:
    MapBackupScheduler(
        PixelMap& pixelMap,
        std::filesystem::path dataDir,
        std::chrono::seconds interval
    );
    ~MapBackupScheduler();

    MapBackupScheduler(const MapBackupScheduler&) = delete;
    MapBackupScheduler& operator=(const MapBackupScheduler&) = delete;

    void start();
    void stop();

private:
    void run();

    PixelMap& pixelMap_;
    std::filesystem::path dataDir_;
    std::chrono::seconds interval_;
    std::atomic_bool running_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
};

} // namespace pixelwar::storage
