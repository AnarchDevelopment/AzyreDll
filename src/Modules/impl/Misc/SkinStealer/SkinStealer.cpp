#include "SkinStealer.hpp"

#include "Config/Config.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Entity/Actor.hpp"
#include "SDK/Core/Game.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace mc {

namespace {

uint32_t crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

void appendBigEndian(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void appendChunk(std::vector<uint8_t>& png, const char type[4],
                 const uint8_t* data, size_t size)
{
    appendBigEndian(png, static_cast<uint32_t>(size));
    const size_t crcStart = png.size();
    png.insert(png.end(), type, type + 4);
    if (size)
        png.insert(png.end(), data, data + size);
    appendBigEndian(png, crc32(png.data() + crcStart, png.size() - crcStart));
}

bool writePngRgba(const std::filesystem::path& path,
                  const std::array<uint8_t, off::skin::PixelBytes>& pixels)
{
    constexpr uint32_t width = 64;
    constexpr uint32_t height = 64;
    constexpr size_t rowBytes = width * 4;

    // PNG scanlines use filter 0. A single stored DEFLATE block keeps the
    // writer self-contained and avoids doing codec work on the game thread.
    std::vector<uint8_t> scanlines;
    scanlines.reserve((rowBytes + 1) * height);
    for (uint32_t y = 0; y < height; ++y)
    {
        scanlines.push_back(0);
        const size_t begin = static_cast<size_t>(y) * rowBytes;
        scanlines.insert(scanlines.end(), pixels.begin() + begin,
                         pixels.begin() + begin + rowBytes);
    }

    if (scanlines.size() > 0xFFFFu)
        return false;
    std::vector<uint8_t> compressed{0x78, 0x01, 0x01};
    const uint16_t blockSize = static_cast<uint16_t>(scanlines.size());
    const uint16_t blockSizeInverse = static_cast<uint16_t>(~blockSize);
    compressed.push_back(static_cast<uint8_t>(blockSize));
    compressed.push_back(static_cast<uint8_t>(blockSize >> 8));
    compressed.push_back(static_cast<uint8_t>(blockSizeInverse));
    compressed.push_back(static_cast<uint8_t>(blockSizeInverse >> 8));
    compressed.insert(compressed.end(), scanlines.begin(), scanlines.end());

    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t byte : scanlines)
    {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    appendBigEndian(compressed, (b << 16) | a);

    std::vector<uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::array<uint8_t, 13> header{};
    header[0] = 0;
    header[1] = 0;
    header[2] = 0;
    header[3] = static_cast<uint8_t>(width);
    header[4] = 0;
    header[5] = 0;
    header[6] = 0;
    header[7] = static_cast<uint8_t>(height);
    header[8] = 8; // bit depth
    header[9] = 6; // RGBA
    appendChunk(png, "IHDR", header.data(), header.size());
    appendChunk(png, "IDAT", compressed.data(), compressed.size());
    appendChunk(png, "IEND", nullptr, 0);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char*>(png.data()),
                static_cast<std::streamsize>(png.size()));
    return file.good();
}

std::string safeFileName(const std::string& playerName)
{
    std::string name;
    name.reserve(playerName.size());
    for (unsigned char c : playerName)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')
            name.push_back(static_cast<char>(c));
        else if (c == ' ' && !name.empty() && name.back() != '_')
            name.push_back('_');
    }
    while (!name.empty() && name.back() == '_')
        name.pop_back();
    return name;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

SkinStealer::SkinStealer()
    : Module("SkinStealer", "Save loaded player skins as PNG files", Category::Misc, 0)
{
}

bool SkinStealer::saveActor(const Actor& actor, std::string& savedPath) const
{
    if (!actor.valid())
        return false;

    const std::string playerName = actor.name();
    const std::string fileName = safeFileName(playerName);
    if (fileName.empty())
        return false;

    std::array<uint8_t, off::skin::PixelBytes> pixels{};
    if (!actor.skinRgba(pixels))
        return false;

    const std::string base = config::baseDir();
    if (base.empty())
        return false;
    const std::filesystem::path directory = std::filesystem::path(base) / "Skins";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec)
        return false;

    const std::filesystem::path path = directory / (fileName + "_skin.png");
    if (!writePngRgba(path, pixels))
        return false;
    savedPath = path.string();
    MC_LOG("[SkinStealer] Saved %s to %s", playerName.c_str(), savedPath.c_str());
    return true;
}

int SkinStealer::savePlayer(const std::string& playerName, std::string& savedPath) const
{
    const std::string wanted = lower(playerName);
    bool matched = false;
    for (const Actor& actor : Game::get().remotePlayersSnapshot())
    {
        if (!actor.valid() || lower(actor.name()) != wanted)
            continue;
        matched = true;
        return saveActor(actor, savedPath) ? 1 : -1;
    }
    return matched ? -1 : 0;
}

std::pair<size_t, size_t> SkinStealer::saveAll() const
{
    size_t saved = 0;
    size_t failed = 0;
    for (const Actor& actor : Game::get().remotePlayersSnapshot())
    {
        if (!actor.valid())
            continue;
        std::string path;
        if (saveActor(actor, path))
            ++saved;
        else
            ++failed;
    }
    return {saved, failed};
}

MC_REGISTER_MODULE(SkinStealer);

} // namespace mc
