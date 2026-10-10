#pragma once

#include <algorithm>
#include "simulation/superweapons/SuperWeaponNuclearFlight.h"

struct RuntimeNuclearFlightPose {
    bool visible = false;
    bool rising = false;
    double x = 0.0;
    double y = 0.0;
    double level = 0.0;
};

inline RuntimeNuclearFlightPose runtimeNuclearFlightPose(
    int elapsed, int duration, bool hasSource,
    double sourceX, double sourceY, double sourceLevel,
    double targetX, double targetY, double targetLevel,
    const SuperWeaponRule* rule = nullptr) {
    RuntimeNuclearFlightPose pose;
    if (duration <= 0 || elapsed < 0 || elapsed >= duration) return pose;
    if (rule && rule->nuclearAltitudeLeptons > 0) {
        const double frames = double(elapsed) / duration * superWeaponNuclearFlightFrames(*rule,hasSource);
        const int ascent = superWeaponNuclearAscentFrames(*rule,hasSource);
        const bool rising = frames < ascent;
        const double height = rising
            ? superWeaponNuclearDistance(frames,10,255,rule->nuclearUpAcceleration)
            : std::max(0.0, rule->nuclearAltitudeLeptons-superWeaponNuclearDistance(
                frames-ascent,hasSource?1:100,rule->nuclearDownSpeed,rule->nuclearDownAcceleration));
        const double levels=height*2.449489742783178/256.0;
        return {levels <= 64.0, rising, rising?sourceX:targetX, rising?sourceY:targetY,
            (rising?sourceLevel:targetLevel)+levels};
    }
    const double progress = double(elapsed) / duration;
    // The ascent and descent are separate vertical columns. Their transfer
    // happens above the battlefield; no horizontal projectile crosses it.
    constexpr double skyHeight = 64.0;
    if (progress < 0.4 && hasSource) {
        const double phase = progress / 0.4;
        pose = {true, true, sourceX, sourceY,
            sourceLevel + skyHeight * phase * phase};
    } else if (progress >= 0.6) {
        const double phase = std::clamp((progress - 0.6) / 0.4, 0.0, 1.0);
        pose = {true, false, targetX, targetY,
            targetLevel + skyHeight * (1.0 - phase * phase)};
    }
    return pose;
}
