#pragma once

#include "render/core/RuntimeSpriteFrameImage.h"
#include "runtime/timing/RuntimeRuleTime.h"
#include "ui/input/RuntimeInputPixelValues.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace concord::render {

inline int runtimeBuildingRepairIconFrame(int tick, int fixedTickHz, int gameSpeed) {
    const int retailSpeed = 6 - std::clamp(gameSpeed, 0, 6);
    const int period = std::max(2, runtimeRuleNormalizedAnimationDelay(14, retailSpeed) / 4);
    const auto phase = runtimeRuleFrameAtTick(tick, fixedTickHz) % period;
    return static_cast<int>(phase * 6 / (period - 1));
}

inline int runtimeRepairCursorPhase(int tick, int fixedTickHz) {
    return static_cast<int>((runtimeRuleFrameAtTick(tick, fixedTickHz) / 4) % 20);
}

template<class Point>
RuntimeInputPixelPoint runtimeBuildingRepairIconAnchor(std::span<const Point> corners, int verticalHeight) {
    RuntimeInputPixelPoint anchor{};
    for (const auto& corner : corners) {
        anchor.x += corner.x;
        anchor.y += corner.y;
    }
    anchor.x /= static_cast<std::int32_t>(corners.size());
    anchor.y /= static_cast<std::int32_t>(corners.size());
    anchor.y -= verticalHeight / 2;
    return anchor;
}

struct RuntimeBuildingRepairIconPlacement {
    RuntimeInputPixelRect image;
    RuntimeInputPixelRect visible;
    bool needsCrop;
};

inline std::optional<RuntimeBuildingRepairIconPlacement> runtimePlaceBuildingRepairIcon(
    RuntimeInputPixelPoint anchor, RuntimeInputPixelRect viewport, const RuntimeShpFrameImage& image) {
    const int left = anchor.x - image.globalWidth / 2 + image.offsetX;
    const int top = anchor.y - image.globalHeight / 2 + image.offsetY;
    const RuntimeInputPixelRect bounds{left, top, left + image.width, top + image.height};
    const RuntimeInputPixelRect visible{
        std::max(bounds.left, viewport.left), std::max(bounds.top, viewport.top),
        std::min(bounds.right, viewport.right), std::min(bounds.bottom, viewport.bottom)};
    if (visible.left >= visible.right || visible.top >= visible.bottom) return std::nullopt;
    const bool needsCrop = visible.left != bounds.left || visible.top != bounds.top
        || visible.right != bounds.right || visible.bottom != bounds.bottom;
    return RuntimeBuildingRepairIconPlacement{bounds, visible, needsCrop};
}

inline void cropRuntimeBuildingRepairIcon(const RuntimeShpFrameImage& image,
    const RuntimeBuildingRepairIconPlacement& placement, RuntimeShpFrameImage& clipped) {
    const auto& visible = placement.visible;
    const auto& bounds = placement.image;
    clipped.width = visible.right - visible.left;
    clipped.height = visible.bottom - visible.top;
    clipped.pixels.reserve(static_cast<std::size_t>(clipped.width) * clipped.height);
    const int startX = visible.left - bounds.left;
    for (int y = visible.top - bounds.top; y < visible.bottom - bounds.top; ++y) {
        const auto first = image.pixels.begin() + (y * image.width + startX);
        clipped.pixels.insert(clipped.pixels.end(), first, first + clipped.width);
    }
}

} // namespace concord::render
