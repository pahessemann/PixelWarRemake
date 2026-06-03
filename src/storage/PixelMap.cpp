#include "pixelwar/storage/PixelMap.hpp"

#include "pixelwar/utils/Base64.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace pixelwar::storage {

namespace {

constexpr char kMagic[] = {'P', 'W', 'R', 'M', 'A', 'P', '1', '\0'};

void appendLe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void writeU32(std::ofstream& file, std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 24) & 0xFF)
    };
    file.write(bytes, sizeof(bytes));
}

void writeU16(std::ofstream& file, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF)
    };
    file.write(bytes, sizeof(bytes));
}

void writeU64(std::ofstream& file, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        const char byte = static_cast<char>((value >> shift) & 0xFF);
        file.write(&byte, 1);
    }
}

bool readU32(std::ifstream& file, std::uint32_t& value) {
    char bytes[4]{};
    if (!file.read(bytes, sizeof(bytes))) {
        return false;
    }
    value = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24);
    return true;
}

bool readU64(std::ifstream& file, std::uint64_t& value) {
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        char byte = 0;
        if (!file.read(&byte, 1)) {
            return false;
        }
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << shift;
    }
    return true;
}

struct RgbColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

RgbColor paletteColor(std::uint8_t color) {
    static constexpr RgbColor kPalette[] = {
        {0x00, 0x00, 0x00}, {0xff, 0xff, 0xff}, {0xff, 0x45, 0x00}, {0xff, 0xa8, 0x00},
        {0xff, 0xd6, 0x35}, {0x00, 0xa3, 0x68}, {0x7e, 0xed, 0x56}, {0x24, 0x50, 0xa4},
        {0x36, 0x90, 0xea}, {0x51, 0xe9, 0xf4}, {0x81, 0x1e, 0x9f}, {0xb4, 0x4a, 0xc0},
        {0xff, 0x99, 0xaa}, {0x9c, 0x69, 0x26}, {0x89, 0x8d, 0x90}, {0xd4, 0xd7, 0xd9}
    };

    if (color < std::size(kPalette)) {
        return kPalette[color];
    }
    return kPalette[0];
}

} // namespace

PixelMap::PixelMap(std::size_t width, std::size_t height, std::uint8_t paletteSize)
    : width_(width),
      height_(height),
      paletteSize_(paletteSize),
      pixels_(width * height, 0) {
    if (width_ == 0 || height_ == 0) {
        throw std::invalid_argument("pixel map dimensions must be positive");
    }
    if (paletteSize_ == 0) {
        throw std::invalid_argument("palette size must be positive");
    }
}

std::size_t PixelMap::width() const {
    return width_;
}

std::size_t PixelMap::height() const {
    return height_;
}

std::uint8_t PixelMap::paletteSize() const {
    return paletteSize_;
}

std::uint64_t PixelMap::sequence() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return sequence_;
}

bool PixelMap::loadBinary(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    char magic[sizeof(kMagic)]{};
    if (!file.read(magic, sizeof(magic)) || !std::equal(kMagic, kMagic + sizeof(kMagic), magic)) {
        return false;
    }

    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::uint64_t sequence = 0;
    std::uint32_t palette = 0;
    std::uint32_t runCount = 0;
    if (!readU64(file, width) || !readU64(file, height) || !readU64(file, sequence) || !readU32(file, palette) || !readU32(file, runCount)) {
        return false;
    }

    if (width != width_ || height != height_ || palette != paletteSize_) {
        return false;
    }

    std::vector<std::uint8_t> loaded;
    loaded.reserve(width_ * height_);
    for (std::uint32_t i = 0; i < runCount; ++i) {
        char colorByte = 0;
        std::uint32_t count = 0;
        if (!file.read(&colorByte, 1) || !readU32(file, count)) {
            return false;
        }
        const auto color = static_cast<std::uint8_t>(colorByte);
        if (color >= paletteSize_) {
            return false;
        }
        if (loaded.size() + count > width_ * height_) {
            return false;
        }
        loaded.insert(loaded.end(), count, color);
    }

    if (loaded.size() != width_ * height_) {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    pixels_ = std::move(loaded);
    sequence_ = sequence;
    changes_.clear();
    cacheDirty_ = true;
    return true;
}

void PixelMap::saveBinary(const std::filesystem::path& path) const {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot open map file for writing");
    }

    file.write(kMagic, sizeof(kMagic));
    writeU64(file, static_cast<std::uint64_t>(width_));
    writeU64(file, static_cast<std::uint64_t>(height_));
    writeU64(file, sequence_);
    writeU32(file, paletteSize_);

    std::vector<std::pair<std::uint8_t, std::uint32_t>> runs;
    runs.reserve(1024);
    std::size_t i = 0;
    while (i < pixels_.size()) {
        const std::uint8_t color = pixels_[i];
        std::uint32_t count = 0;
        while (i < pixels_.size() && pixels_[i] == color && count < std::numeric_limits<std::uint32_t>::max()) {
            ++i;
            ++count;
        }
        runs.emplace_back(color, count);
    }

    writeU32(file, static_cast<std::uint32_t>(runs.size()));
    for (const auto& [color, count] : runs) {
        const char colorByte = static_cast<char>(color);
        file.write(&colorByte, 1);
        writeU32(file, count);
    }
}

void PixelMap::saveBmp(const std::filesystem::path& path) const {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::shared_lock<std::shared_mutex> lock(mutex_);
    const std::uint32_t width = static_cast<std::uint32_t>(width_);
    const std::uint32_t height = static_cast<std::uint32_t>(height_);
    const std::uint32_t rowStride = ((width * 3U + 3U) / 4U) * 4U;
    const std::uint32_t imageSize = rowStride * height;
    const std::uint32_t fileSize = 14U + 40U + imageSize;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot open screenshot file for writing");
    }

    file.write("BM", 2);
    writeU32(file, fileSize);
    writeU16(file, 0);
    writeU16(file, 0);
    writeU32(file, 54);

    writeU32(file, 40);
    writeU32(file, width);
    writeU32(file, height);
    writeU16(file, 1);
    writeU16(file, 24);
    writeU32(file, 0);
    writeU32(file, imageSize);
    writeU32(file, 2835);
    writeU32(file, 2835);
    writeU32(file, 0);
    writeU32(file, 0);

    std::vector<char> padding(rowStride - width * 3U, 0);
    for (std::size_t y = height_; y > 0; --y) {
        const std::size_t sourceY = y - 1;
        for (std::size_t x = 0; x < width_; ++x) {
            const auto color = paletteColor(pixels_[sourceY * width_ + x]);
            const char bgr[] = {
                static_cast<char>(color.b),
                static_cast<char>(color.g),
                static_cast<char>(color.r)
            };
            file.write(bgr, sizeof(bgr));
        }
        if (!padding.empty()) {
            file.write(padding.data(), static_cast<std::streamsize>(padding.size()));
        }
    }
}

bool PixelMap::setPixel(std::size_t x, std::size_t y, std::uint8_t color, PixelChange& change) {
    if (!isInBounds(x, y) || color >= paletteSize_) {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    const std::size_t index = y * width_ + x;
    const bool changed = pixels_[index] != color;
    pixels_[index] = color;
    ++sequence_;
    change = PixelChange{
        sequence_,
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        color
    };
    changes_.push_back(change);
    if (changes_.size() > maxChanges_) {
        changes_.erase(changes_.begin(), changes_.begin() + static_cast<std::ptrdiff_t>(changes_.size() - maxChanges_));
    }
    cacheDirty_ = cacheDirty_ || changed;
    return true;
}

void PixelMap::reset(std::uint8_t color) {
    if (color >= paletteSize_) {
        color = 0;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    std::fill(pixels_.begin(), pixels_.end(), color);
    ++sequence_;
    changes_.clear();
    cacheDirty_ = true;
}

std::string PixelMap::toJson(std::optional<std::uint64_t> since) const {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (since && canServeDiffLocked(*since)) {
        return diffJsonLocked(*since);
    }
    rebuildCacheLocked();
    return fullJsonLocked();
}

bool PixelMap::isInBounds(std::size_t x, std::size_t y) const {
    return x < width_ && y < height_;
}

std::string PixelMap::fullJsonLocked() const {
    std::ostringstream out;
    out << R"({"type":"full","width":)" << width_
        << R"(,"height":)" << height_
        << R"(,"sequence":)" << sequence_
        << R"(,"encoding":"rle-base64","palette_size":)" << static_cast<int>(paletteSize_)
        << R"(,"data":")" << cachedRleBase64_ << R"("})";
    return out.str();
}

std::string PixelMap::diffJsonLocked(std::uint64_t since) const {
    std::ostringstream out;
    out << R"({"type":"diff","width":)" << width_
        << R"(,"height":)" << height_
        << R"(,"sequence":)" << sequence_
        << R"(,"changes":[)";

    bool first = true;
    for (const auto& change : changes_) {
        if (change.sequence <= since) {
            continue;
        }
        if (!first) {
            out << ',';
        }
        first = false;
        out << R"({"seq":)" << change.sequence
            << R"(,"x":)" << change.x
            << R"(,"y":)" << change.y
            << R"(,"color":)" << static_cast<int>(change.color)
            << '}';
    }

    out << "]}";
    return out.str();
}

bool PixelMap::canServeDiffLocked(std::uint64_t since) const {
    if (since >= sequence_) {
        return true;
    }
    if (since == 0 || changes_.empty()) {
        return false;
    }
    return since >= changes_.front().sequence - 1;
}

void PixelMap::rebuildCacheLocked() const {
    if (!cacheDirty_) {
        return;
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(pixels_.size() / 4);

    std::size_t i = 0;
    while (i < pixels_.size()) {
        const std::uint8_t color = pixels_[i];
        std::uint32_t count = 0;
        while (i < pixels_.size() && pixels_[i] == color && count < std::numeric_limits<std::uint32_t>::max()) {
            ++i;
            ++count;
        }
        encoded.push_back(color);
        appendLe32(encoded, count);
    }

    cachedRleBase64_ = utils::base64Encode(encoded);
    cacheDirty_ = false;
}

} // namespace pixelwar::storage
