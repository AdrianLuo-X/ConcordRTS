#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

// Retail BulletClass's ROT branch adds BulletType::Acceleration to the
// velocity magnitude each rule frame and clamps it to BulletClass::Speed
// (gamemd.exe 0x466985 / 0x4669CD). MoveTo seeds ROT velocity at one
// lepton/frame (0x468A98 / 0x468B2C). Integrate those native frame velocities
// across fractional fixed ticks. No mutable velocity is needed: the saved
// launch age, acceleration and cruise step completely determine this curve.
inline double runtimeMissileAccelerationDistanceLeptons(
    double frames, double cruiseLeptons, int acceleration) {
    frames = std::max(0.0, frames);
    cruiseLeptons = std::max(0.0, cruiseLeptons);
    // Explicit zero/negative mod values retain the engine's existing constant
    // speed compatibility. Reverse/stationary launches are outside this change.
    if (acceleration <= 0 || cruiseLeptons <= 0.0) return frames * cruiseLeptons;
    const double whole = std::floor(frames);
    const double initial = std::min(1.0, cruiseLeptons);
    const double ramp = std::max(1.0, std::ceil((cruiseLeptons - initial) / acceleration));
    const double accelerating = std::min(whole, ramp - 1.0);
    const double complete = initial * accelerating
        + acceleration * accelerating * (accelerating + 1.0) * 0.5
        + (whole - accelerating) * cruiseLeptons;
    const double next = std::min(cruiseLeptons, initial + (whole + 1.0) * acceleration);
    return complete + (frames - whole) * next;
}

inline double runtimeMissileAccelerationStepCells(
    double cruiseCellsPerTick, int acceleration, int elapsedTicks, int fixedTickHz) {
    const double cruise = std::max(0.00001, cruiseCellsPerTick);
    if (acceleration <= 0) return cruise;
    const double hz = std::max(1, fixedTickHz);
    const double dt = 15.0 / hz;
    const double end = std::max(1, elapsedTicks) * dt;
    const double top = cruise * hz / 15.0 * 256.0;
    const double distance = runtimeMissileAccelerationDistanceLeptons(end, top, acceleration)
        - runtimeMissileAccelerationDistanceLeptons(end - dt, top, acceleration);
    return std::clamp(distance / 256.0, 0.0, cruise);
}

// Keep the old authored cruising/fuel distance after adding the launch ramp.
// The missing distance is constant after the speed cap; its equivalent cruise
// time extends the existing lifetime once, when the projectile is created.
inline int runtimeMissileAccelerationStartupDelayTicks(
    double cruiseCellsPerTick, int acceleration, int fixedTickHz) {
    if (acceleration <= 0 || cruiseCellsPerTick <= 0.0) return 0;
    const double hz = std::max(1, fixedTickHz);
    const double top = cruiseCellsPerTick * hz / 15.0 * 256.0;
    const double ramp = std::max(1.0, std::ceil((top - std::min(1.0, top)) / acceleration));
    const double loss = std::max(0.0, ramp * top
        - runtimeMissileAccelerationDistanceLeptons(ramp, top, acceleration));
    return static_cast<int>(std::clamp(std::ceil(loss / top * hz / 15.0),
        0.0, double(std::numeric_limits<int>::max())));
}
