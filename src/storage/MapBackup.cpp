#include "pixelwar/storage/MapBackup.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace pixelwar::storage {

namespace {

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string timestampIdPart() {
    const auto now = std::chrono::system_clock::now();
    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const auto millis = static_cast<int>(epochMs % 1000);
    const auto timeValue = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &timeValue);
#else
    localtime_r(&timeValue, &local);
#endif

    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S")
        << '-' << std::setw(3) << std::setfill('0') << millis;
    return out.str();
}

std::string sanitizeReason(const std::string& reason) {
    std::string out;
    out.reserve(reason.size());
    for (const unsigned char c : reason) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == '-' || c == '_' || c == ' ') {
            out.push_back('-');
        }
    }

    while (!out.empty() && out.front() == '-') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "manual";
    }
    if (out.size() > 32) {
        out.resize(32);
    }
    return out;
}

bool isBackupIdSafe(const std::string& id) {
    if (id.empty() || id.size() > 96) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

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

std::optional<std::uint64_t> parseUint64(const std::string& text) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(text, &consumed, 10);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::int64_t> parseInt64(const std::string& text) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoll(text, &consumed, 10);
        if (consumed != text.size()) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::filesystem::path backupMetaPath(const std::filesystem::path& dataDir, const std::string& id) {
    return mapBackupsDir(dataDir) / (id + ".meta");
}

std::filesystem::path backupPwmPath(const std::filesystem::path& dataDir, const std::string& id) {
    return mapBackupsDir(dataDir) / (id + ".pwm");
}

std::filesystem::path backupBmpPath(const std::filesystem::path& dataDir, const std::string& id) {
    return mapBackupsDir(dataDir) / (id + ".bmp");
}

void writeMetadata(const std::filesystem::path& dataDir, const MapBackupEntry& entry) {
    std::ofstream file(backupMetaPath(dataDir, entry.id), std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot write map backup metadata");
    }
    file << entry.id << '\t'
         << entry.createdAt << '\t'
         << entry.reason << '\t'
         << entry.sequence << '\t'
         << entry.bytes << '\t'
         << (entry.screenshot ? 1 : 0) << '\n';
}

std::optional<MapBackupEntry> readMetadata(const std::filesystem::path& metaPath) {
    std::ifstream file(metaPath);
    std::string line;
    if (!file || !std::getline(file, line)) {
        return std::nullopt;
    }

    const auto parts = splitTabs(line);
    if (parts.size() != 6 || !isBackupIdSafe(parts[0])) {
        return std::nullopt;
    }

    const auto createdAt = parseInt64(parts[1]);
    const auto sequence = parseUint64(parts[3]);
    const auto bytes = parseUint64(parts[4]);
    if (!createdAt || !sequence || !bytes) {
        return std::nullopt;
    }

    MapBackupEntry entry;
    entry.id = parts[0];
    entry.createdAt = *createdAt;
    entry.reason = parts[2];
    entry.sequence = *sequence;
    entry.bytes = *bytes;
    entry.screenshot = parts[5] == "1";
    return entry;
}

} // namespace

std::filesystem::path mapBackupsDir(const std::filesystem::path& dataDir) {
    return dataDir / "backups";
}

std::vector<MapBackupEntry> listMapBackups(const std::filesystem::path& dataDir) {
    std::vector<MapBackupEntry> backups;
    const auto dir = mapBackupsDir(dataDir);
    if (!std::filesystem::exists(dir)) {
        return backups;
    }

    for (const auto& item : std::filesystem::directory_iterator(dir)) {
        if (!item.is_regular_file() || item.path().extension() != ".meta") {
            continue;
        }
        if (auto entry = readMetadata(item.path())) {
            if (std::filesystem::exists(backupPwmPath(dataDir, entry->id))) {
                entry->screenshot = std::filesystem::exists(backupBmpPath(dataDir, entry->id));
                backups.push_back(*entry);
            }
        }
    }

    std::sort(backups.begin(), backups.end(), [](const auto& left, const auto& right) {
        if (left.createdAt != right.createdAt) {
            return left.createdAt > right.createdAt;
        }
        return left.id > right.id;
    });
    return backups;
}

std::optional<MapBackupEntry> findMapBackup(const std::filesystem::path& dataDir, const std::string& id) {
    if (!isBackupIdSafe(id)) {
        return std::nullopt;
    }
    const auto metaPath = backupMetaPath(dataDir, id);
    if (!std::filesystem::exists(metaPath) || !std::filesystem::exists(backupPwmPath(dataDir, id))) {
        return std::nullopt;
    }
    auto entry = readMetadata(metaPath);
    if (entry) {
        entry->screenshot = std::filesystem::exists(backupBmpPath(dataDir, id));
    }
    return entry;
}

MapBackupEntry createMapBackup(
    PixelMap& pixelMap,
    const std::filesystem::path& dataDir,
    const std::string& reason,
    bool includeScreenshot
) {
    std::filesystem::create_directories(mapBackupsDir(dataDir));

    const auto sequence = pixelMap.sequence();
    const auto safeReason = sanitizeReason(reason);
    std::string id = timestampIdPart() + "-" + safeReason + "-seq" + std::to_string(sequence);
    int suffix = 2;
    while (std::filesystem::exists(backupPwmPath(dataDir, id))) {
        id = timestampIdPart() + "-" + safeReason + "-seq" + std::to_string(sequence) + "-" + std::to_string(suffix++);
    }

    const auto mapPath = backupPwmPath(dataDir, id);
    pixelMap.saveBinary(mapPath);

    bool screenshotWritten = false;
    if (includeScreenshot) {
        pixelMap.saveBmp(backupBmpPath(dataDir, id));
        screenshotWritten = true;
    }

    MapBackupEntry entry;
    entry.id = id;
    entry.createdAt = nowSeconds();
    entry.reason = safeReason;
    entry.sequence = sequence;
    entry.bytes = std::filesystem::file_size(mapPath);
    entry.screenshot = screenshotWritten;
    writeMetadata(dataDir, entry);
    return entry;
}

bool restoreMapBackup(
    PixelMap& pixelMap,
    const std::filesystem::path& dataDir,
    const std::string& id,
    const std::filesystem::path& liveMapPath
) {
    const auto backupPath = mapBackupFilePath(dataDir, id);
    if (!backupPath) {
        return false;
    }
    if (!pixelMap.loadBinary(*backupPath)) {
        return false;
    }
    pixelMap.saveBinary(liveMapPath);
    return true;
}

std::optional<std::filesystem::path> mapBackupScreenshotPath(
    const std::filesystem::path& dataDir,
    const std::string& id
) {
    if (!isBackupIdSafe(id)) {
        return std::nullopt;
    }
    const auto path = backupBmpPath(dataDir, id);
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }
    return path;
}

std::optional<std::filesystem::path> mapBackupFilePath(
    const std::filesystem::path& dataDir,
    const std::string& id
) {
    if (!isBackupIdSafe(id)) {
        return std::nullopt;
    }
    const auto path = backupPwmPath(dataDir, id);
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }
    return path;
}

MapBackupScheduler::MapBackupScheduler(
    PixelMap& pixelMap,
    std::filesystem::path dataDir,
    std::chrono::seconds interval
) : pixelMap_(pixelMap),
    dataDir_(std::move(dataDir)),
    interval_(interval) {}

MapBackupScheduler::~MapBackupScheduler() {
    stop();
}

void MapBackupScheduler::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] {
        run();
    });
}

void MapBackupScheduler::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void MapBackupScheduler::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_) {
        if (wake_.wait_for(lock, interval_, [this] { return !running_.load(); })) {
            break;
        }

        lock.unlock();
        try {
            const auto backup = createMapBackup(pixelMap_, dataDir_, "hourly", false);
            (void)backup;
        } catch (const std::exception& ex) {
            std::cerr << "Hourly map backup failed: " << ex.what() << '\n';
        }
        lock.lock();
    }
}

} // namespace pixelwar::storage
