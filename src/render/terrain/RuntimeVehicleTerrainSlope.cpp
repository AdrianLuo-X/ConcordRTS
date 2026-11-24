#include "render/terrain/RuntimeVehicleTerrainSlope.h"

#include "render/units/RuntimeVehicleBodyDynamics.h"
#include "world/model/GameWorld.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace concord::render {

// Normalized original CellClass::GetFloorHeight (0x47B3A0) coefficients:
// gx, gy, base, cap. TMP's packed screen-depth bytes are not slope heights.
double runtimeVehicleRampHeight(unsigned char ramp, double localX, double localY) {
    // Retail special ramps 17--20 have a constant half-level collision floor.
    // Their image corners must not be turned into a spurious saddle tilt.
    if (ramp >= 17 && ramp <= 20) return 0.5;
    static constexpr std::array<double, 4> planes[] = {
        {0,0,0,0}, {1,0,0,1}, {0,1,0,1}, {-1,0,1,1}, {0,-1,1,1},
        {1,1,-1,1}, {-1,1,0,1}, {-1,-1,1,1}, {1,-1,0,1},
        {1,1,0,1}, {-1,1,1,1}, {-1,-1,2,1}, {1,-1,1,1},
        {1,1,0,2}, {-1,1,1,2}, {-1,-1,2,2}, {1,-1,1,2}
    };
    const auto& plane = planes[ramp < std::size(planes) ? ramp : 0];
    const double u = localX + 0.5, v = localY + 0.5;
    return std::clamp(plane[0] * u + plane[1] * v + plane[2], 0.0, plane[3]);
}

std::optional<RuntimeVehicleTerrainSupport> runtimeVehicleTerrainSupport(
    double x, double y, RuntimeVehicleTerrainSlopeSamples samples) {
    using Sample = RuntimeVehicleTerrainSlopeSample;
    const auto& center = samples.center;
    auto& left = samples.left;
    auto& right = samples.right;
    auto& back = samples.back;
    auto& front = samples.front;
    constexpr double supportRadius = runtimeVehicleTerrainSupportRadius;
    const auto supportedHeight = [&](Sample& point, double px, double py) {
        const double fallback = center.cell->level + runtimeVehicleRampHeight(center.ramp,
            px - center.cell->x, py - center.cell->y);
        if (!point.cell) { point.ramp = center.ramp; return fallback; }
        if (point.cell != center.cell) {
            // A cliff or missing bank is not a continuous road slope. Check
            // both tiles at their common edge before blending a support point.
            const double bx = point.cell->x == center.cell->x ? x
                : (point.cell->x + center.cell->x) * 0.5;
            const double by = point.cell->y == center.cell->y ? y
                : (point.cell->y + center.cell->y) * 0.5;
            const double a = center.cell->level + runtimeVehicleRampHeight(center.ramp,
                bx - center.cell->x, by - center.cell->y);
            const double b = point.cell->level + runtimeVehicleRampHeight(point.ramp,
                bx - point.cell->x, by - point.cell->y);
            if (std::abs(a - b) > 0.0001) { point.ramp = center.ramp; return fallback; }
        }
        return point.height;
    };
    const double hx0 = supportedHeight(left, x - supportRadius, y);
    const double hx1 = supportedHeight(right, x + supportRadius, y);
    const double hy0 = supportedHeight(back, x, y - supportRadius);
    const double hy1 = supportedHeight(front, x, y + supportRadius);
    if (center.ramp == 0 && left.ramp == 0 && right.ramp == 0 && back.ramp == 0 && front.ramp == 0)
        return std::nullopt;
    return RuntimeVehicleTerrainSupport{hx0, hx1, hy0, hy1, center.height};
}

RuntimeVehicleBodyPose runtimeVehicleTerrainPose(
    const RuntimeVehicleTerrainSupport& support, double scale, double objectLevel) {
    RuntimeVehicleBodyPose pose;
    constexpr double supportRadius = runtimeVehicleTerrainSupportRadius;
    const double gx = (support.rightHeight - support.leftHeight) / (2.0 * supportRadius) * scale;
    const double gy = (support.frontHeight - support.backHeight) / (2.0 * supportRadius) * scale;
    pose.roll = pose.terrainRoll = std::atan(gy);
    pose.pitch = pose.terrainPitch = -std::atan(gx * std::cos(pose.roll));
    pose.terrainHeight = support.centerHeight - objectLevel;
    pose.terrainSupported = true;
    return pose;
}

} // namespace concord::render
