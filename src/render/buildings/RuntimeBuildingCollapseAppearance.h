#pragma once

#include "assets/codecs/RuntimeTmpTypes.h"
#include "simulation/buildings/RuntimeBuildingCollapse.h"

#include <filesystem>
#include <string>
#include <vector>

namespace concord::render {

enum class RuntimeBuildingCollapseLayer { Body, Shadow, Hidden };

struct RuntimeBuildingCollapseAppearance {
    int frame;
    bool splitShadow;
    RuntimeBuildingCollapseLayer layer;
};

inline RuntimeBuildingCollapseAppearance runtimeBuildingCollapseAppearance(
    const RuntimeBuildingCollapseState& collapse, bool splitShadow, int tick, int hz, bool shadowPass) {
    return {runtimeBuildingCollapseFrame(collapse, tick, hz), splitShadow,
        shadowPass ? (splitShadow ? RuntimeBuildingCollapseLayer::Shadow : RuntimeBuildingCollapseLayer::Hidden)
                   : RuntimeBuildingCollapseLayer::Body};
}

template<class PaletteCandidates, class LoadNamedPalette>
const std::vector<RuntimePaletteColor>* resolveRuntimeBuildingCustomPalette(const std::string& name,
    PaletteCandidates&& unitPaletteCandidates, LoadNamedPalette&& loadNamedPalette) {
    if (name.empty()) return nullptr;
    // An extensionless retail palette key first uses the current theater suffix.
    if (std::filesystem::path(name).extension().empty()) {
        const auto candidates = unitPaletteCandidates();
        const auto& ordinary = candidates.back();
        if (const auto* palette = loadNamedPalette(name + ordinary.substr(4))) return palette;
    }
    return loadNamedPalette(name);
}

} // namespace concord::render
