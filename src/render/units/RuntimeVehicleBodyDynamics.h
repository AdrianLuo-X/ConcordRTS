#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "simulation/abilities/parasites/RuntimeSquidGrappleMotion.h"

// Presentation coordinates: angles are world-axis radians; height is in cells.
// Evaluate from simulation time, never from paint count or wall-clock time.
struct RuntimeVehicleBodyPose {
    double pitch = 0.0;
    double roll = 0.0;
    double height = 0.0;
    bool hullLocalRoll = false;
    double hullYaw = 0.0;
    // Derived from the current physical ground position and original TMP.
    // These are a resting slope, not another saved impulse or movement state.
    double terrainPitch = 0.0;
    double terrainRoll = 0.0;
    double terrainHeight = 0.0;
    bool terrainSupported = false;

    bool active() const {
        return std::abs(pitch) > 0.0001 || std::abs(roll) > 0.0001 || height > 0.0001
            || std::abs(terrainHeight) > 0.0001;
    }
};

inline int runtimeVehicleParasiteBiteAgeTicks(
    int attachmentAgeTicks, int intervalTicks, int nextDamageTick, int tick
) {
    if (attachmentAgeTicks < 0 || intervalTicks <= 0 || nextDamageTick <= tick) return -1;
    // The damage scheduler advances this deadline only when it issues a bite.
    // Derive its last bite from saved authority, including delayed updates.
    const auto lastBite = static_cast<long long>(nextDamageTick) - intervalTicks;
    const auto attached = static_cast<long long>(tick) - attachmentAgeTicks;
    if (lastBite < attached || lastBite > tick) return -1;
    return static_cast<int>(tick - lastBite);
}

inline RuntimeVehicleBodyPose runtimeVehicleParasiteMotion(double age, double weight) {
    constexpr double duration = 0.72;
    if (!std::isfinite(age) || age < 0.0 || age >= duration) return {};
    constexpr double tau = 6.283185307179586;
    const double mass = std::clamp(std::sqrt(3.5 / std::max(0.5, weight)), 0.5, 1.35);
    const double remaining = 1.0 - age / duration;
    const double envelope = remaining * remaining;
    const double phase = age * tau / duration;
    // One immediate jolt and a smaller rebound, then rest until the next bite.
    const double hop = std::max(0.0, 1.05 * age - 5.0 * age * age);
    return {0.075 * std::sin(phase) * mass * envelope,
        0.225 * std::cos(phase) * mass * envelope, hop * mass};
}

inline RuntimeVehicleBodyPose runtimeVehicleBlastMotion(
    double age, double duration, double dx, double dy, double distance,
    double radius, double damage, double weight, double rockingCoefficient
) {
    if (!std::isfinite(age) || age < 0.0 || duration <= 0.0 || age >= duration
        || radius <= 0.0 || distance >= radius || damage <= 0.0) return {};
    const double falloff = 1.0 - distance / radius;
    const double strength = std::clamp(damage / 240.0 * falloff * falloff
        * 3.5 / std::max(0.5, weight) * rockingCoefficient, 0.0, 1.4);
    const double remaining = 1.0 - age / duration;
    const double angle = 0.15 * strength * std::sin(age * 22.0)
        * std::exp(-age * 3.0) * remaining * remaining;
    const double length = std::hypot(dx, dy);
    if (length > 0.0001) { dx /= length; dy /= length; }
    else { dx = 0.7071067811865476; dy = -dx; }
    const double height = std::max(0.0, 1.8 * std::sqrt(strength) * age - 5.0 * age * age)
        * remaining * remaining;
    return {dx * angle, -dy * angle, height};
}

struct RuntimeVoxelModelBounds {
    bool valid = false;
    std::array<double, 3> minimum{};
    std::array<double, 3> maximum{};
};

// Two-degree bins keep original VXL raster caching finite. Lift is applied to
// the common draw origin, so it does not create another raster-cache dimension.
struct RuntimeVoxelBodyRotation {
    int pitch = 0;
    int roll = 0;
    double floorZ = 0.0;
    double waterClipZ = -1.0e30;
    bool useQuaternion = false;
    std::array<double, 4> quaternion{0, 0, 0, 1};

    bool clipsWater() const { return waterClipZ > -1.0e20; }
    bool active() const { return useQuaternion || pitch != 0 || roll != 0 || clipsWater(); }

    std::array<double, 3> rotate(double x, double y, double z) const {
        if (useQuaternion) {
            const auto& q = quaternion;
            const double tx = 2 * (q[1] * z - q[2] * y);
            const double ty = 2 * (q[2] * x - q[0] * z);
            const double tz = 2 * (q[0] * y - q[1] * x);
            return {x + q[3]*tx + q[1]*tz - q[2]*ty,
                y + q[3]*ty + q[2]*tx - q[0]*tz,
                z + q[3]*tz + q[0]*ty - q[1]*tx};
        }
        constexpr double step = 3.141592653589793 / 90.0;
        const double cp = std::cos(pitch * step), sp = std::sin(pitch * step);
        const double cr = std::cos(roll * step), sr = std::sin(roll * step);
        const double px = cp * x + sp * z;
        const double pz = -sp * x + cp * z;
        return {px, cr * y - sr * pz, sr * y + cr * pz};
    }
};

inline RuntimeVoxelBodyRotation runtimeVehicleBodyRotation(
    const RuntimeVehicleBodyPose& pose, double floorZ = 0.0
) {
    if (pose.hullLocalRoll) {
        RuntimeVoxelBodyRotation rotation;
        // Ships pivot at the original model's water origin, without the
        // ground-vehicle support lift or eighteen-degree impulse clamp.
        rotation.useQuaternion = std::abs(pose.roll) > 0.0001;
        const double sine = std::sin(pose.roll * 0.5);
        rotation.quaternion = {std::cos(pose.hullYaw) * sine,
            std::sin(pose.hullYaw) * sine, 0.0, std::cos(pose.roll * 0.5)};
        return rotation;
    }
    constexpr double degrees = 90.0 / 3.141592653589793;
    // Preserve the eighteen-degree impulse bound on flat ground. A resting
    // slope can be steeper and must not be clipped by that old rocking bound.
    const int limit = pose.terrainSupported ? 45 : 9;
    return {std::clamp(static_cast<int>(std::lround(pose.pitch * degrees)), -limit, limit),
        std::clamp(static_cast<int>(std::lround(pose.roll * degrees)), -limit, limit), floorZ};
}

inline double runtimeVehicleGroundSupportLift(
    const RuntimeVoxelModelBounds& bounds, const RuntimeVoxelBodyRotation& rotation,
    double yaw, bool originPivot, double slopeX = 0.0, double slopeY = 0.0
) {
    if (!bounds.valid || !rotation.active()) return 0.0;
    const double cx = originPivot ? 0.0 : (bounds.minimum[0] + bounds.maximum[0]) * 0.5;
    const double cy = originPivot ? 0.0 : (bounds.minimum[1] + bounds.maximum[1]) * 0.5;
    const double c = std::cos(yaw), s = std::sin(yaw);
    double lowest = 0.0;
    for (double x : {bounds.minimum[0] - cx, bounds.maximum[0] - cx})
        for (double y : {bounds.minimum[1] - cy, bounds.maximum[1] - cy}) {
            const auto point = rotation.rotate(x * c - y * s, x * s + y * c, 0.0);
            lowest = std::min(lowest, point[2] - slopeX * point[0] - slopeY * point[1]);
        }
    return -lowest;
}
