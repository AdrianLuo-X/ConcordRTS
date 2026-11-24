#pragma once

#include <optional>

struct TerrainCell;
struct RuntimeVehicleBodyPose;

namespace concord::render {

inline constexpr double runtimeVehicleTerrainSupportRadius = 0.35;

struct RuntimeVehicleTerrainSlopeSample {
    const TerrainCell* cell = nullptr;
    unsigned char ramp = 0;
    double height = 0.0;
};

struct RuntimeVehicleTerrainSlopeSamples {
    RuntimeVehicleTerrainSlopeSample center, left, right, back, front;
};

struct RuntimeVehicleTerrainSupport {
    double leftHeight, rightHeight, backHeight, frontHeight, centerHeight;
};

double runtimeVehicleRampHeight(unsigned char ramp, double localX, double localY);

// Sampling remains in its original frame owner. The center is available for
// this calculation, and each contact borrows the corresponding terrain cell.
std::optional<RuntimeVehicleTerrainSupport> runtimeVehicleTerrainSupport(
    double x, double y, RuntimeVehicleTerrainSlopeSamples samples);

RuntimeVehicleBodyPose runtimeVehicleTerrainPose(
    const RuntimeVehicleTerrainSupport& support, double scale, double objectLevel);

} // namespace concord::render
