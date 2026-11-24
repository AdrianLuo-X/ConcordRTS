#include "RuntimeAirImpactBodyCenterValues.h"
#include <cstdint>

namespace concord::render::geometry {
bool resolveAirImpactCameraOffset(const RuntimeInputPixelRect& bodyBounds,
    const RuntimeInputPixelPoint& logicalHit, RuntimeInputPixelPoint& offset) noexcept {
    const auto centerX = (static_cast<std::int64_t>(bodyBounds.left) + bodyBounds.right) / 2;
    const auto centerY = (static_cast<std::int64_t>(bodyBounds.top) + bodyBounds.bottom) / 2;
    const auto x = centerX - logicalHit.x;
    const auto y = centerY - logicalHit.y;
    if (x < -4096 || x > 4096 || y < -4096 || y > 4096) return false;
    offset = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
    return true;
}

AirImpactBodyPixelOffset airImpactBodyPixelOffset(
    const RuntimeInputPixelPoint& reference, const RuntimeInputPixelPoint& doubled) noexcept {
    return {reference, {2 * reference.x - doubled.x, 2 * reference.y - doubled.y}};
}
}
