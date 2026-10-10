#pragma once

#include "runtime/host/RuntimeShell.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

enum class RuntimeInternetMenuSkinFaction : std::uint8_t {
    Allied,
    Soviet,
    Yuri,
    Observer
};

enum class RuntimeInternetMenuSkinPage : std::uint8_t {
    Account,
    Community,
    Browser,
    Ladder,
    News,
    Lobby,
    Kicked,
    MatchHistory
};

struct RuntimeInternetMenuSkinPalette {
    std::string id;
    std::string source;
    std::vector<std::uint8_t> rgb;
    std::uint64_t pixelFingerprint = 0U;

    bool valid() const { return rgb.size() == 256U * 3U; }
};

struct RuntimeInternetMenuSkinAsset {
    std::string id;
    std::string source;
    std::string paletteSource;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;
    std::uint64_t pixelFingerprint = 0U;

    bool valid() const {
        return width > 0 && height > 0
            && bgra.size() == static_cast<std::size_t>(width * height * 4);
    }
};

struct RuntimeInternetMenuSkin {
    bool productionReady = false;
    std::unordered_map<std::string, RuntimeInternetMenuSkinPalette> palettes;
    std::unordered_map<std::string, RuntimeInternetMenuSkinAsset> assets;
    std::vector<std::string> diagnostics;

    const RuntimeInternetMenuSkinPalette* palette(
        std::string_view id
    ) const;
    const RuntimeInternetMenuSkinAsset* asset(std::string_view id) const;
};

bool loadRuntimeInternetMenuSkin(
    const RuntimeConfig& config,
    RuntimeInternetMenuSkin& skin
);

RuntimeInternetMenuSkinFaction runtimeInternetMenuSkinFactionForCountry(
    std::string_view countryId,
    bool observer
);

std::string runtimeInternetMenuSkinFactionName(
    RuntimeInternetMenuSkinFaction faction
);

std::string runtimeInternetMenuSkinScrollAsset(
    RuntimeInternetMenuSkinPage page,
    RuntimeInternetMenuSkinFaction faction
);

std::string runtimeInternetMenuSkinRankAsset(std::uint32_t ladderRank);

#if defined(_WIN32)
bool drawRuntimeInternetMenuSkinSurface(
    HDC dc,
    const RECT& bounds,
    const RuntimeInternetMenuSkin& skin,
    RuntimeInternetMenuSkinPage page,
    RuntimeInternetMenuSkinFaction faction,
    std::uint32_t ladderRank
);

bool drawRuntimeInternetMenuSkinButton(
    HDC dc,
    const RECT& bounds,
    const RuntimeInternetMenuSkin& skin,
    std::string_view releasedAsset,
    std::string_view pressedAsset,
    std::wstring_view text,
    bool pressed,
    bool disabled
);
#endif

int runRuntimeInternetMenuSkinSmokeTest(
    const RuntimeConfig& config,
    const std::string& outputPath
);
