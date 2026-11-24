#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

namespace concord::render::geometry {
bool resolveAirImpactCameraOffset(const RuntimeInputPixelRect& bodyBounds,
    const RuntimeInputPixelPoint& logicalHit, RuntimeInputPixelPoint& offset) noexcept;

struct AirImpactBodyPixelOffset {
    RuntimeInputPixelPoint reference;
    RuntimeInputPixelPoint fixed;
};

AirImpactBodyPixelOffset airImpactBodyPixelOffset(
    const RuntimeInputPixelPoint& reference, const RuntimeInputPixelPoint& doubled) noexcept;
}
