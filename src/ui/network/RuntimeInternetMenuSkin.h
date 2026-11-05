#pragma once

#include "runtime/config/RuntimeConfig.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/input/RuntimeInputPixelValues.h"

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

// Synchronous drawing capability only. Skin/strings/rectangle are borrowed
// for this call; resource lookup, raster, DC effects and failure policy remain
// in the selected backend. This is not the final frame Presenter.
enum class RuntimeInternetSkinDrawingCapability : std::uint32_t { Unavailable, Available };
class RuntimeInternetSkinDrawTarget {
public:
    virtual ~RuntimeInternetSkinDrawTarget() = default;
    RuntimeInternetSkinDrawTarget(const RuntimeInternetSkinDrawTarget&) = delete;
    RuntimeInternetSkinDrawTarget& operator=(const RuntimeInternetSkinDrawTarget&) = delete;
    virtual RuntimeInternetSkinDrawingCapability capability() const noexcept = 0;
    virtual bool drawSurface(const RuntimeInputPixelRect& bounds,
        const RuntimeInternetMenuSkin& skin, RuntimeInternetMenuSkinPage page,
        RuntimeInternetMenuSkinFaction faction, std::uint32_t ladderRank) = 0;
    virtual bool drawButton(const RuntimeInputPixelRect& bounds,
        const RuntimeInternetMenuSkin& skin, std::string_view releasedAsset,
        std::string_view pressedAsset, std::wstring_view text, bool pressed,
        bool disabled) = 0;
protected:
    RuntimeInternetSkinDrawTarget() = default;
};
inline bool drawRuntimeInternetMenuSkinSurface(RuntimeInternetSkinDrawTarget& target,
    const RuntimeInputPixelRect& bounds, const RuntimeInternetMenuSkin& skin,
    RuntimeInternetMenuSkinPage page, RuntimeInternetMenuSkinFaction faction,
    std::uint32_t ladderRank) {
    return target.drawSurface(bounds,skin,page,faction,ladderRank);
}
inline bool drawRuntimeInternetMenuSkinButton(RuntimeInternetSkinDrawTarget& target,
    const RuntimeInputPixelRect& bounds, const RuntimeInternetMenuSkin& skin,
    std::string_view releasedAsset, std::string_view pressedAsset,
    std::wstring_view text, bool pressed, bool disabled) {
    return target.drawButton(bounds,skin,releasedAsset,pressedAsset,text,pressed,disabled);
}

int runRuntimeInternetMenuSkinSmokeTest(
    const RuntimeConfig& config,
    const std::string& outputPath
);
