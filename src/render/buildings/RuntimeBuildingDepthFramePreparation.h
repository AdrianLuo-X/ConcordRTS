#pragma once

#include "render/buildings/RuntimeBuildingDepthMaterials.h"
#include "render/core/RuntimeSpriteFrameImage.h"
#include "ui/input/RuntimeInputPixelValues.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <unordered_set>

namespace concord::render {

class RuntimeBuildingDepthViewportBounds {
public:
    explicit RuntimeBuildingDepthViewportBounds(RuntimeInputPixelRect requested) noexcept
        : requested_(requested), bounds_{requested.left, requested.top, requested.left, requested.top} {}

    void addDraw(RuntimeInputPixelRect draw) noexcept {
        const RuntimeInputPixelRect clipped{
            std::max(draw.left, requested_.left), std::max(draw.top, requested_.top),
            std::min(draw.right, requested_.right), std::min(draw.bottom, requested_.bottom)
        };
        if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) return;
        if (!hasDraw_) {
            bounds_ = clipped;
            hasDraw_ = true;
        } else {
            bounds_.left = std::min(bounds_.left, clipped.left);
            bounds_.top = std::min(bounds_.top, clipped.top);
            bounds_.right = std::max(bounds_.right, clipped.right);
            bounds_.bottom = std::max(bounds_.bottom, clipped.bottom);
        }
    }

    RuntimeInputPixelRect bounds() const noexcept { return bounds_; }

private:
    RuntimeInputPixelRect requested_;
    RuntimeInputPixelRect bounds_;
    bool hasDraw_ = false;
};

struct RuntimeBuildingDepthDrawOrder {
    bool wall;
    double ySort;
    std::size_t sequence;
};

inline bool runtimeBuildingDepthDrawBefore(
    RuntimeBuildingDepthDrawOrder left, RuntimeBuildingDepthDrawOrder right) noexcept {
    if (left.wall != right.wall) return left.wall;
    if (left.ySort != right.ySort) return left.ySort < right.ySort;
    return left.sequence < right.sequence;
}

struct RuntimeBuildingDepthTemplateView {
    std::string& key;
    bool& attempted;
    RuntimeShpFrameImage& image;
};

template<typename ReadImage>
bool ensureRuntimeBuildingDepthTemplate(RuntimeBuildingDepthTemplateView cache, const std::string& key,
    ReadImage&& readImage) {
    if (cache.key != key) {
        cache.key = key;
        cache.attempted = false;
        cache.image = {};
    }
    if (!cache.attempted) {
        cache.attempted = true;
        readImage(cache.image);
    }
    return cache.image.width > 0 && cache.image.height > 0
        && cache.image.pixels.size() >= static_cast<std::size_t>(cache.image.width) * cache.image.height;
}

struct RuntimeBuildingDepthProjection {
    int nativeY;
    int heightPixel;
    double animationYSort;
};

inline RuntimeBuildingDepthProjection projectRuntimeBuildingDepthOrigin(
    double renderX, double subX, double renderY, double subY, double level) {
    const double row = renderX + subX + renderY + subY;
    return {static_cast<int>(std::llround((row - level) * 15.0)),
        static_cast<int>(std::llround(level * 15.0)), row * 256.0};
}

inline double runtimeBuildingDepthDrawSort(double originSort, bool mobile, bool voxelTurret, bool gate) noexcept {
    // Mobile centers differ by half a cell along each axis. Structures keep
    // the original foundation origin and authored turret and gate offsets.
    if (mobile) return originSort + 256.0;
    if (voxelTurret) originSort += 32;
    if (gate) originSort -= 16;
    return originSort;
}

inline RuntimeInputPixelSize runtimeBuildingDepthBufferExtent(RuntimeInputPixelRect bounds) noexcept {
    return {std::max(0,bounds.right-bounds.left), std::max(0,bounds.bottom-bounds.top)};
}

inline RuntimeInputPixelRect compactRuntimeBuildingDepthViewport(RuntimeInputPixelRect requested,
    std::span<const RuntimeBuildingDepthDraw> draws, bool fullViewport) noexcept {
    if (fullViewport)
        return requested;
    RuntimeBuildingDepthViewportBounds compact(requested);
    for (const auto& draw : draws)
        compact.addDraw(draw.rect);
    return compact.bounds();
}

struct RuntimeBuildingDepthSceneFlags {
    bool opaqueMarkerCovers;
    bool hasAlpha;
};

inline RuntimeBuildingDepthSceneFlags runtimeBuildingDepthSceneFlags(
    std::span<const RuntimeBuildingDepthDraw> draws, const std::unordered_set<int>& frozenOwners,
    const std::unordered_set<int>& occludingStructureIds, bool composeColors) {
    const bool opaqueMarkerCovers = frozenOwners.empty()
        && std::none_of(draws.begin(), draws.end(), [&](const auto& draw) {
            return draw.opacity < 255 && occludingStructureIds.contains(draw.instanceId);
        });
    const bool hasAlpha = composeColors && std::any_of(draws.begin(),draws.end(),
        [](const auto& draw) { return draw.opacity > 0 && draw.opacity < 255; });
    return {opaqueMarkerCovers,hasAlpha};
}

} // namespace concord::render
