#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "render/units/RuntimeVehicleBodyBlastPresentation.h"
#include "render/units/RuntimeVehicleBodyDynamics.h"
#include "world/model/GameWorld.h"

namespace concord::render {

static inline RuntimeVehicleBodyPose runtimeVehicleGrappleHullPose(int progress, int fixedTickHz, int facing) {
    RuntimeVehicleBodyPose pose;
    pose.roll = runtimeSquidGrapplePose(progress, fixedTickHz).roll;
    pose.hullLocalRoll = true;
    pose.hullYaw = (worldObjectArtFacingBucket(facing, 32) - 8)
        * 6.283185307179586 / 32;
    return pose;
}

static inline void accumulateRuntimeVehicleBlastPose(
    RuntimeVehicleBodyPose& pose, const RuntimeVehicleBlastPresentation& blast,
    const std::array<double, 3>& bodyPosition, const std::array<double, 3>& impactPosition,
    int damage, double weight
) {
    const auto& [x, y, z] = bodyPosition;
    const auto& [impactX, impactY, impactLevel] = impactPosition;
    const double dx = x - impactX, dy = y - impactY;
    const double distance = std::sqrt(dx * dx + dy * dy + (z - impactLevel) * (z - impactLevel));
    const auto impulse = runtimeVehicleBlastMotion(
        blast.age, blast.duration, dx, dy, distance, blast.radius,
        damage, weight, blast.coefficient);
    pose.pitch += impulse.pitch; pose.roll += impulse.roll; pose.height += impulse.height;
}

static inline RuntimeVehicleBodyPose composeRuntimeGroundBodyPose(
    RuntimeVehicleBodyPose pose, const RuntimeVehicleBodyPose& terrainPose
) {
    pose.pitch = std::clamp(pose.pitch, -0.31, 0.31);
    pose.roll = std::clamp(pose.roll, -0.31, 0.31);
    pose.height = std::clamp(pose.height, 0.0, 0.22);
    // Bound the impact motion before adding the independent resting plane.
    // Otherwise a real ramp would be cropped by the old impulse-only limit.
    pose.pitch += terrainPose.pitch;
    pose.roll += terrainPose.roll;
    pose.terrainPitch = terrainPose.terrainPitch;
    pose.terrainRoll = terrainPose.terrainRoll;
    pose.terrainHeight = terrainPose.terrainHeight;
    pose.terrainSupported = terrainPose.terrainSupported;
    return pose;
}

static inline double runtimeVehicleGroundPlanePixelOffset(const RuntimeVehicleBodyPose& pose, int tileHeight) {
    return pose.terrainHeight * tileHeight * 0.5;
}

static inline double projectRuntimeVehicleGroundBodyLift(
    const RuntimeVoxelModelBounds& modelBounds, const RuntimeVoxelBodyRotation& rotation,
    const RuntimeVehicleBodyPose& bodyPose, double yaw, bool originPivot,
    double rasterFitScale, double heightYScale, int tileHeight
) {
    const double slopeX = -std::tan(bodyPose.terrainPitch) / std::cos(bodyPose.terrainRoll);
    const double slopeY = std::tan(bodyPose.terrainRoll);
    const double support = runtimeVehicleGroundSupportLift(modelBounds, rotation, yaw,
        originPivot, slopeX, slopeY);
    return support * rasterFitScale
        * heightYScale + bodyPose.height * tileHeight
        + runtimeVehicleGroundPlanePixelOffset(bodyPose, tileHeight);
}

} // namespace concord::render
