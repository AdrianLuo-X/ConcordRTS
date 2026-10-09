#pragma once

#include "IniFile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

struct RuntimeSpatialAudioPosition {
    // Runtime world units: cells, including fractional muzzle/impact positions.
    double x = 0, y = 0, level = 0;
};

struct RuntimeSpatialAudioRules {
    int rangeCells = 10;
    int minimumVolumePercent = 50;
    bool local = false;
    bool global = false;
    bool shroud = false;
};

inline RuntimeSpatialAudioRules runtimeSpatialAudioRules(const IniFile& ini,
    const std::string& section) {
    RuntimeSpatialAudioRules result;
    const auto value = [&](const char* key, const char* fallback) {
        std::string text = ini.getValue(section, key, ini.getValue("Defaults", key, fallback));
        if (const auto comment = text.find(';'); comment != std::string::npos) text.resize(comment);
        return text;
    };
    const auto integer = [&](const char* key, int fallback, int maximum) {
        std::istringstream stream(value(key, std::to_string(fallback).c_str()));
        int parsed = fallback;
        if (!(stream >> parsed)) parsed = fallback;
        return std::clamp(parsed, 0, maximum);
    };
    result.rangeCells = integer("Range", 10, 4096);
    result.minimumVolumePercent = integer("MinVolume", 50, 100);
    auto type = value("Type", "NORMAL SCREEN UNSHROUD");
    for (auto& c : type) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == ',' || c == '|') c = ' ';
    }
    std::istringstream stream(type); std::string token;
    while (stream >> token) {
        result.local |= token == "local";
        result.global |= token == "global";
        result.shroud |= token == "shroud";
    }
    return result;
}

struct RuntimeSpatialAudioResult {
    int gain16384 = 0;
    int pan16384 = 8192;
};

// Retail 750AC0: rectangle/LOCAL-center distance, double vertical weight,
// GLOBAL floor, then a strict 5% audibility gate. SCREEN is not a separate
// branch. Coordinates here are relative to the map viewport's upper left.
inline RuntimeSpatialAudioResult runtimeSpatialAudioAtProjection(
    const RuntimeSpatialAudioRules& rules, double x, double y,
    double width, double height, bool mappedOrNoFog, double pixelScale = 1.0) {
    RuntimeSpatialAudioResult result;
    if (!(width > 0 && height > 0 && pixelScale > 0)
        || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width)
        || !std::isfinite(height) || !std::isfinite(pixelScale)
        || (rules.shroud && !mappedOrNoFog)) return result;
    const double signedX = x - width * 0.5;
    // Retail truncates the signed displacement before abs; use trunc in
    // floating point to avoid overflowing an integer for malformed positions.
    double dx = std::abs(std::trunc(signedX));
    double dy = std::abs(std::trunc(y - height * 0.5));
    if (!rules.local) {
        dx = std::max(0.0, dx - width * 0.5);
        dy = std::max(0.0, dy - height * 0.5);
    }
    dy *= 2.0;
    const double range = std::max(0, rules.rangeCells) * 60.0 * pixelScale;
    double gain = range > 0 && dx < range && dy < range
        ? (range - std::max(dx, dy)) / range : 0.0;
    if (rules.global) gain = std::max(gain,
        std::clamp(rules.minimumVolumePercent, 0, 100) / 100.0);
    if (gain < 0.05) gain = 0;
    result.gain16384 = static_cast<int>(gain * 16384.0);
    result.pan16384 = std::clamp(static_cast<int>(
        std::clamp(signedX, -width, width) * 8192.0 / width + 8192.0), 0, 16384);
    return result;
}

// Numeric attenuation table in retail gamemd.exe at 816380. Driver 40A6D0
// quantizes volume to 101 entries; pan uses the same table for the weaker ear.
inline constexpr std::array<int, 101> runtimeRetailAudioDecibels100 = {
    -10000,-6644,-5644,-5059,-4644,-4322,-4059,-3837,-3644,-3474,
    -3322,-3184,-3059,-2943,-2837,-2737,-2644,-2556,-2474,-2396,
    -2322,-2252,-2184,-2120,-2059,-2000,-1943,-1889,-1837,-1786,
    -1737,-1690,-1644,-1599,-1556,-1515,-1474,-1434,-1396,-1358,
    -1322,-1286,-1252,-1218,-1184,-1152,-1120,-1089,-1059,-1029,
    -1000,-971,-943,-916,-889,-862,-837,-811,-786,-761,
    -737,-713,-690,-667,-644,-621,-599,-578,-556,-535,
    -515,-494,-474,-454,-434,-415,-396,-377,-358,-340,
    -322,-304,-286,-269,-252,-234,-218,-201,-184,-168,
    -152,-136,-120,-105,-89,-74,-59,-44,-29,-14,0
};

struct RuntimeAudioMix {
    float volume = 1, left = 1, right = 1;
};

inline RuntimeAudioMix runtimeRetailAudioMix(int gain16384, int pan16384) {
    const auto amplitude = [](int index) {
        return index <= 0 ? 0.0f : static_cast<float>(std::pow(10.0,
            runtimeRetailAudioDecibels100[std::min(index, 100)] / 2000.0));
    };
    RuntimeAudioMix mix;
    mix.volume = amplitude(std::clamp(gain16384, 0, 16384) * 100 / 16384);
    const int pan = std::clamp(pan16384, 0, 16384) * 200 / 16384 - 100;
    if (pan < 0) mix.right = amplitude(100 + pan);
    if (pan > 0) mix.left = amplitude(100 - pan);
    return mix;
}
