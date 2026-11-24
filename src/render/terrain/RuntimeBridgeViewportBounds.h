#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

#include <span>
#include <string>

struct RuntimeTmpSubTileImage;

namespace concord::render {

RuntimeInputPixelPoint runtimeBridgeViewportCenter(
    RuntimeInputPixelPoint center, RuntimeInputPixelSize tileSize, bool highBridge) noexcept;
RuntimeInputPixelRect runtimeBridgeViewportTmpBounds(
    RuntimeInputPixelPoint center, const RuntimeTmpSubTileImage& image) noexcept;

template<class ProbeTmp, class ProbeShp>
bool runtimeBridgeViewportResourceIntersects(
    const std::string& preferred, const std::string& fallbackImage,
    std::span<const std::string> candidates, RuntimeInputPixelRect paintArea,
    ProbeTmp probeTmp, ProbeShp probeShp) {
    const auto intersects = [paintArea](RuntimeInputPixelRect bounds) {
        return bounds.right > bounds.left && bounds.bottom > bounds.top
            && runtimeInputPixelRectsIntersect(bounds, paintArea);
    };
    // A resolved TMP is authoritative even if the selected slot is intentionally
    // empty or its frame lies outside the paint area.
    if (const auto bounds = probeTmp(preferred, false)) {
        return intersects(*bounds);
    }
    for (const auto& name : candidates) {
        if (const auto bounds = probeTmp(name, true)) {
            return intersects(*bounds);
        }
    }
    for (const auto& name : candidates) {
        if (const auto bounds = probeShp(name, true); bounds && intersects(*bounds)) {
            return true;
        }
    }
    if (fallbackImage.empty()) {
        return false;
    }
    const auto bounds = probeShp(fallbackImage, false);
    return bounds && intersects(*bounds);
}

} // namespace concord::render
