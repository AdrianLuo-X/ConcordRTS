#pragma once

#include "core/io/IniFile.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

struct RuntimeAudioVariationSpec {
    int minimumFrequencyPercent = 0;
    int maximumFrequencyPercent = 0;
    int maximumVolumeDropPercent = 0;
};

inline RuntimeAudioVariationSpec runtimeAudioVariationRules(
    const IniFile& ini, const std::string& section) {
    RuntimeAudioVariationSpec result;
    const auto text = [&](const std::string& key) {
        std::string value = ini.getValue(section, key, ini.getValue("Defaults", key, ""));
        value.resize(value.find(';') == std::string::npos ? value.size() : value.find(';'));
        std::replace(value.begin(), value.end(), ',', ' ');
        return value;
    };
    std::istringstream frequency(text("FShift"));
    int minimum = 0, maximum = 0;
    if (frequency >> minimum) {
        if (!(frequency >> maximum)) maximum = minimum;
        result.minimumFrequencyPercent = std::clamp(std::min(minimum, maximum), -75, 100);
        result.maximumFrequencyPercent = std::clamp(std::max(minimum, maximum), -75, 100);
    }
    std::istringstream volume(text("VShift"));
    int drop = 0;
    if (volume >> drop) result.maximumVolumeDropPercent = std::clamp(drop, 0, 100);
    return result;
}

inline std::uint64_t runtimeAudioVariationSeed(const std::string& event,
    std::uint64_t source, int ordinal) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char character : event) {
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        hash = (hash ^ character) * 1099511628211ULL;
    }
    hash ^= source + 0x9e3779b97f4a7c15ULL + (static_cast<std::uint64_t>(ordinal) << 32);
    hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
    hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebULL;
    return hash ^ (hash >> 31);
}

struct RuntimeAudioVariation {
    int frequencyPermille = 1000;
    int volumeDeltaPermille = 0;
};

inline RuntimeAudioVariation runtimeAudioVariationFor(
    const RuntimeAudioVariationSpec& spec, std::uint64_t seed) {
    RuntimeAudioVariation result;
    const int low = std::clamp(spec.minimumFrequencyPercent, -75, 100);
    const int high = std::clamp(std::max(low, spec.maximumFrequencyPercent), -75, 100);
    // Whole-percent steps keep the number of reusable PCM variants bounded.
    const int percent = low + static_cast<int>(seed % static_cast<std::uint64_t>(high - low + 1));
    result.frequencyPermille = 1000 + percent * 10;
    const int drop = std::clamp(spec.maximumVolumeDropPercent, 0, 100) * 10;
    result.volumeDeltaPermille = -static_cast<int>((seed >> 32) % static_cast<std::uint64_t>(drop + 1));
    return result;
}

inline std::uint32_t runtimeAudioReadLe32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return bytes[at] | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[at + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

// Change playback pitch/speed without resampling or touching PCM payload.
// The decoder already supplies PCM WAVs; unsupported formats keep their source.
inline bool runtimeAudioPitchWave(std::vector<std::uint8_t>& bytes, int frequencyPermille) {
    const auto tag = [&](std::size_t at, const char* value) {
        return at + 4 <= bytes.size()
            && std::equal(bytes.begin() + at, bytes.begin() + at + 4, value);
    };
    if (bytes.size() < 12 || !tag(0, "RIFF") || !tag(8, "WAVE")) return false;
    std::size_t format = 0;
    bool data = false;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const auto count = runtimeAudioReadLe32(bytes, at + 4);
        if (count > bytes.size() - at - 8) return false;
        if (tag(at, "fmt ") && count >= 16) format = at + 8;
        if (tag(at, "data")) data = true;
        at += 8 + static_cast<std::size_t>(count) + (count & 1U);
    }
    if (!format || !data || (bytes[format] != 1 && bytes[format] != 3)
        || bytes[format + 1] != 0) return false;
    const auto oldRate = runtimeAudioReadLe32(bytes, format + 4);
    const auto oldBytesPerSecond = runtimeAudioReadLe32(bytes, format + 8);
    if (oldRate == 0 || oldBytesPerSecond == 0) return false;
    const auto scaled = [&](std::uint32_t value) {
        return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
            (static_cast<std::uint64_t>(value) * std::clamp(frequencyPermille, 250, 2000) + 500) / 1000,
            1, std::numeric_limits<std::uint32_t>::max()));
    };
    const auto write32 = [&](std::size_t at, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
    };
    const auto newRate = scaled(oldRate);
    const auto blockAlign = static_cast<std::uint32_t>(bytes[format + 12])
        | (static_cast<std::uint32_t>(bytes[format + 13]) << 8);
    if (blockAlign == 0 || static_cast<std::uint64_t>(newRate) * blockAlign
        > std::numeric_limits<std::uint32_t>::max()) return false;
    write32(format + 4, newRate);
    write32(format + 8, newRate * blockAlign);
    return true;
}

inline std::string runtimeAudioPitchVariantPath(const std::string& sourcePath,
    const std::filesystem::path& cacheRoot, int frequencyPermille) {
    if (frequencyPermille == 1000 || sourcePath.empty()) return sourcePath;
    std::error_code error;
    const std::filesystem::path source(sourcePath);
    const auto size = std::filesystem::file_size(source, error);
    if (error || size < 44 || size > 64U * 1024U * 1024U) return sourcePath;
    const auto modified = std::filesystem::last_write_time(source, error);
    if (error) return sourcePath;
    const auto key = runtimeAudioVariationSeed(source.generic_string(),
        size ^ static_cast<std::uint64_t>(modified.time_since_epoch().count()), frequencyPermille);
    const auto directory = cacheRoot / "pitch";
    const auto target = directory / (std::to_string(key) + ".wav");
    if (std::filesystem::file_size(target, error) == size && !error) return target.string();
    std::ifstream input(source, std::ios::binary);
    if (!input) return sourcePath;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))
        || !runtimeAudioPitchWave(bytes, frequencyPermille)) return sourcePath;
    std::filesystem::create_directories(directory, error);
    if (error) return sourcePath;
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return sourcePath;
    output.close();
    return output ? target.string() : sourcePath;
}
