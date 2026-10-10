#pragma once

#include "simulation/movement/ground/RuntimeVehicleArrivalMotion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace RuntimeVehicleStartupMotion {

inline constexpr std::int64_t distanceOneQ16 = 65536;
inline constexpr std::int64_t maximumDistanceQ16 = std::numeric_limits<std::int64_t>::max() / 4;

inline constexpr std::int64_t startupMaximumDistanceQ16 = maximumDistanceQ16;

inline std::int64_t distanceQ16(double distance) {
    if (!(distance > 0.0)) return 0;
    const long double scaled = static_cast<long double>(distance) * distanceOneQ16;
    if (!std::isfinite(distance) || scaled >= maximumDistanceQ16) return maximumDistanceQ16;
    return static_cast<std::int64_t>(std::llround(scaled));
}

inline std::int64_t rampDistanceQ16(double distance) {
    if (!(distance > 0.0)) return 0;
    const long double scaled = static_cast<long double>(distance) * distanceOneQ16;
    if (!std::isfinite(distance) || scaled >= maximumDistanceQ16) return maximumDistanceQ16;
    return static_cast<std::int64_t>(std::ceil(scaled));
}

// The factor adds this fraction of cruise velocity per original rule frame.
// Zero is deliberately immobile; callers must not accept a movement segment.
inline double accelerationDistance(double cruiseCellsPerRuleFrame, double factor) {
    if (!(factor > 0.0)) return std::numeric_limits<double>::infinity();
    return std::max(0.0, cruiseCellsPerRuleFrame) / (2.0 * factor);
}

enum class Phase { Cruise, Accelerating, Braking };
struct Piece { double from = 0.0, to = 0.0, duration = 0.0; Phase phase = Phase::Cruise; };
struct Profile {
    double length = 0.0, before = 0.0, ramp = 0.0, remainingAtStart = 0.0;
    double duration = 0.0;
    std::array<Piece, 4> pieces{};
    std::size_t count = 0;
};

// Time is cruise-speed cell intervals. Integrate the slower of startup,
// unchanged cruise, and the existing arrival brake at every point. This also
// handles a short route that must brake before reaching cruise velocity.
inline Profile profile(double length, double before, double ramp, double after) {
    Profile result;
    result.length = std::max(0.0, length); result.before = std::max(0.0, before);
    result.ramp = ramp; result.remainingAtStart = result.length + std::max(0.0, after);
    if (result.length <= 0.0) return result;
    const bool braking = after >= 0.0 && after < RuntimeVehicleArrivalMotion::brakingDistance;
    const double brake = RuntimeVehicleArrivalMotion::brakingDistance;
    std::array<double, 5> cuts{{0.0, result.length,
        std::clamp(ramp - result.before, 0.0, result.length),
        braking ? std::clamp(result.remainingAtStart - brake, 0.0, result.length) : result.length,
        braking ? std::clamp((ramp * result.remainingAtStart - brake * result.before)
            / (ramp + brake), 0.0, result.length) : result.length}};
    std::sort(cuts.begin(), cuts.end());
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        const double low = cuts[i-1], high = cuts[i];
        if (high <= low) continue;
        const double middle = low + (high - low) * 0.5;
        const double startup = ramp > 0.0 ? std::sqrt((result.before + middle) / ramp) : 1.0;
        const double arrival = braking ? std::sqrt(std::max(0.0, result.remainingAtStart - middle) / brake) : 1.0;
        auto& piece = result.pieces[result.count++]; piece.from = low; piece.to = high;
        if (startup < 1.0 && startup <= arrival) {
            piece.phase = Phase::Accelerating;
            piece.duration = 2.0 * std::sqrt(ramp) * (high-low)
                / (std::sqrt(result.before+high) + std::sqrt(result.before+low));
        } else if (arrival < 1.0) {
            piece.phase = Phase::Braking;
            piece.duration = 2.0 * std::sqrt(brake) * (high-low)
                / (std::sqrt(std::max(0.0, result.remainingAtStart-low))
                    + std::sqrt(std::max(0.0, result.remainingAtStart-high)));
        } else piece.duration = high-low;
        result.duration += piece.duration;
    }
    return result;
}

inline bool startupActive(double length, double before, double ramp) {
    return length > 0.0 && ramp > 0.0 && std::isfinite(ramp) && before < ramp;
}

inline double durationScale(double length, double before, double ramp, double after = -1.0) {
    if (std::isinf(ramp)) return std::numeric_limits<double>::infinity();
    if (!startupActive(length, before, ramp))
        return RuntimeVehicleArrivalMotion::durationScale(length, after);
    return profile(length, before, ramp, after).duration / length;
}

inline double progress(double elapsedFraction, double length, double before, double ramp,
    double after = -1.0) {
    const double elapsed = std::clamp(elapsedFraction, 0.0, 1.0);
    if (std::isinf(ramp)) return 0.0;
    if (!startupActive(length, before, ramp))
        return RuntimeVehicleArrivalMotion::progress(elapsed, length, after);
    if (elapsed == 0.0 || elapsed == 1.0) return elapsed;
    const auto motion = profile(length, before, ramp, after);
    double time = elapsed * motion.duration;
    for (std::size_t i = 0; i < motion.count; ++i) {
        const auto& piece = motion.pieces[i];
        if (time > piece.duration && i+1 < motion.count) { time -= piece.duration; continue; }
        double distance = piece.from + time;
        if (piece.phase == Phase::Accelerating) {
            const double root = std::sqrt(motion.before + piece.from) + time / (2.0 * std::sqrt(ramp));
            distance = root*root - motion.before;
        } else if (piece.phase == Phase::Braking) {
            const double root = std::max(0.0, std::sqrt(std::max(0.0, motion.remainingAtStart-piece.from))
                - time / (2.0 * std::sqrt(RuntimeVehicleArrivalMotion::brakingDistance)));
            distance = motion.remainingAtStart - root*root;
        }
        return std::clamp(distance / length, 0.0, 1.0);
    }
    return 1.0;
}

} // namespace RuntimeVehicleStartupMotion
