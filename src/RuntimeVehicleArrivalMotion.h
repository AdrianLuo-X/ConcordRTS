#pragma once

#include <algorithm>
#include <cmath>

namespace RuntimeVehicleArrivalMotion {

// Distances are measured along the route in map cells. The cruise speed is
// unchanged until the last two cells; constant deceleration then reaches zero
// exactly at the destination. Time is expressed in cruise-speed cell intervals.
inline constexpr double brakingDistance = 2.0;

inline double timeToStop(double distance) {
    distance = std::max(0.0, distance);
    return distance >= brakingDistance ? distance + brakingDistance
        : 2.0 * std::sqrt(brakingDistance * distance);
}

inline double distanceFromTimeToStop(double time) {
    time = std::max(0.0, time);
    return time >= 2.0 * brakingDistance ? time - brakingDistance
        : time * time / (4.0 * brakingDistance);
}

inline double durationScale(double length, double distanceAfter) {
    if (length <= 0.0 || distanceAfter < 0.0 || distanceAfter >= brakingDistance)
        return 1.0;
    return (timeToStop(distanceAfter + length) - timeToStop(distanceAfter)) / length;
}

inline double progress(double elapsedFraction, double length, double distanceAfter) {
    const double elapsed = std::clamp(elapsedFraction, 0.0, 1.0);
    if (length <= 0.0 || distanceAfter < 0.0 || distanceAfter >= brakingDistance
        || elapsed == 0.0 || elapsed == 1.0) return elapsed;
    const double start = timeToStop(distanceAfter + length);
    const double end = timeToStop(distanceAfter);
    const double remaining = distanceFromTimeToStop(start + (end - start) * elapsed);
    return std::clamp((distanceAfter + length - remaining) / length, 0.0, 1.0);
}

} // namespace RuntimeVehicleArrivalMotion
