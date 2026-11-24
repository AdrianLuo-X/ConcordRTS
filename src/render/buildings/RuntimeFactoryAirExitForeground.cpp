#include "render/buildings/RuntimeFactoryAirExitForeground.h"

#include <algorithm>

namespace concord::render {

bool runtimeFactoryAirExitForegroundActive(bool alive, bool airborneExit, int startedTick,
    int endedTick, int animationTick) noexcept {
    return alive && airborneExit && startedTick >= 0 && animationTick < endedTick;
}

RuntimeFactoryAirExitOcclusionPixels collectRuntimeFactoryAirExitForegroundPixels(
    RuntimeInputPixelRect clip, int surfaceWidth, int surfaceHeight,
    std::span<const std::uint32_t> surfacePixels, std::span<const RuntimeSpriteHitSpan> spans) {
    RuntimeFactoryAirExitOcclusionPixels pixels;
    const auto top = std::max<std::int32_t>(0, clip.top);
    const auto bottom = std::min<std::int32_t>(surfaceHeight, clip.bottom);
    const auto left = std::max<std::int32_t>(0, clip.left);
    const auto right = std::min<std::int32_t>(surfaceWidth, clip.right);
    for (const auto& span : spans) {
        if (span.y < top || span.y >= bottom) continue;
        for (auto x = std::max(span.left, left); x < std::min(span.right, right); ++x) {
            const auto index = static_cast<std::size_t>(span.y) * surfaceWidth + x;
            pixels.emplace_back(index, surfacePixels[index]);
        }
    }
    return pixels;
}

} // namespace concord::render
