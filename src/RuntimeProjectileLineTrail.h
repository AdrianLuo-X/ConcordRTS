#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <string>

struct RuntimeLineTrailPoint {
    double x = 0, y = 0, z = 0;
    double frame = 0;
    double offsetX = 0, offsetY = 0;
    double fixedX = 0, fixedY = 0;
};

// This is a renderer cache, not simulation state. Positions are sampled at
// authored 15 Hz; extra redraws never consume slots or advance the fade.
struct RuntimeProjectileLineTrail {
    std::string owner, image;
    int spawnTick = 0;
    int lastSeenTick = 0;
    int decrement = 16;
    std::array<int, 3> color{0,0,0};
    std::deque<RuntimeLineTrailPoint> points;
    RuntimeLineTrailPoint head;
};

inline RuntimeLineTrailPoint runtimeLineTrailInterpolate(
    const RuntimeLineTrailPoint& a, const RuntimeLineTrailPoint& b, double t) {
    return {a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t,
        a.frame+(b.frame-a.frame)*t, a.offsetX+(b.offsetX-a.offsetX)*t,
        a.offsetY+(b.offsetY-a.offsetY)*t,
        a.fixedX+(b.fixedX-a.fixedX)*t, a.fixedY+(b.fixedY-a.fixedY)*t};
}

inline void runtimeLineTrailSample(RuntimeProjectileLineTrail& trail, const RuntimeLineTrailPoint& next) {
    if (trail.points.empty() || next.frame < trail.head.frame) {
        trail.points.clear(); trail.points.push_back(next); trail.head = next;
        return;
    }
    const auto previous = trail.head;
    if (next.frame > previous.frame) {
        // Bound work after an off-screen gap; at most the most recent 32 nodes survive.
        const auto first = std::max(std::floor(previous.frame) + 1.0, std::floor(next.frame) - 31.0);
        for (double frame = first; frame <= std::floor(next.frame); frame += 1.0) {
            auto point = runtimeLineTrailInterpolate(previous, next,
                (frame - previous.frame) / (next.frame - previous.frame));
            point.frame = frame;
            trail.points.push_back(point);
            if (trail.points.size() > 32U) trail.points.pop_front();
        }
    }
    trail.head = next;
}

inline std::array<int,3> runtimeLineTrailFadedColor(const std::array<int,3>& color,
    int decrement, double ageFrames) {
    const double loss = std::clamp(decrement,0,255) * std::max(0.0,ageFrames);
    std::array<int,3> result{};
    for (int n=0;n<3;++n)
        result[n] = static_cast<int>(std::max(0.0, std::clamp(color[n],0,255) - loss));
    return result;
}

inline int runtimeDirectionalProjectileFrame(double screenDx, double screenDy, int directions) {
    if (directions <= 1 || std::hypot(screenDx, screenDy) < 1e-8) return 0;
    constexpr double tau = 6.28318530717958647692;
    double turns = std::atan2(screenDx, -screenDy) / tau;
    if (turns < 0) turns += 1;
    return static_cast<int>(std::lround(turns * directions)) % directions;
}
