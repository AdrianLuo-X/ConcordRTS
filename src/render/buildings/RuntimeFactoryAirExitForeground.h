#pragma once

#include "data/art/ArtDatabase.h"
#include "render/geometry/RuntimeSpriteHitSpan.h"
#include "ui/input/RuntimeInputPixelValues.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace concord::render {

bool runtimeFactoryAirExitForegroundActive(bool alive, bool airborneExit, int startedTick,
    int endedTick, int animationTick) noexcept;

template<class Lowercase>
const ArtType::ActiveAnimation* selectRuntimeFactoryAirExitForegroundLayer(
    std::span<const ArtType::ActiveAnimation> layers, Lowercase&& lowercase) {
    for (const auto& layer : layers) {
        if (lowercase(layer.key) == "roofdeployinganim") return &layer;
    }
    return nullptr;
}

using RuntimeFactoryAirExitOcclusionPixels = std::vector<std::pair<std::size_t, std::uint32_t>>;

RuntimeFactoryAirExitOcclusionPixels collectRuntimeFactoryAirExitForegroundPixels(
    RuntimeInputPixelRect clip, int surfaceWidth, int surfaceHeight,
    std::span<const std::uint32_t> surfacePixels, std::span<const RuntimeSpriteHitSpan> spans);

} // namespace concord::render
