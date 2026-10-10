#include "simulation/movement/ground/RuntimeVehicleArrivalMotion.h"

#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    using namespace RuntimeVehicleArrivalMotion;
    const auto require = [](bool ok, const char* reason) {
        if (!ok) { std::cerr << reason << '\n'; std::exit(1); }
    };
    for (double length : {1.0, std::sqrt(2.0)}) {
        for (double after : {-1.0, 0.0, 1.0, std::sqrt(2.0), 2.0, 8.0}) {
            require(progress(0, length, after) == 0 && progress(1, length, after) == 1,
                "endpoint must be exact");
            double previous = 0.0;
            double previousStep = 100.0;
            for (int tick = 1; tick <= 100; ++tick) {
                const double next = progress(tick / 100.0, length, after);
                require(next >= previous && next <= 1.0, "motion overshoots or reverses");
                require(next - previous <= previousStep + 1e-12, "braking accelerates");
                previousStep = next - previous;
                previous = next;
            }
            if (after < 0 || after >= brakingDistance) {
                require(durationScale(length, after) == 1.0
                    && progress(0.3, length, after) == 0.3, "cruise changed");
            } else {
                require(durationScale(length, after) > 1.0, "arrival did not extend travel time");
            }
        }
    }
    // Sample actual time across cell and braking-zone boundaries, including a
    // mixed orthogonal/diagonal route. Speed must be continuous before stopping.
    for (const auto& route : {std::vector<double>{1, 1, 1, 1, 1},
            std::vector<double>{1, std::sqrt(2.0), 1, std::sqrt(2.0)}}) {
        double remaining = 0.0;
        for (double length : route) remaining += length;
        double previousEndSpeed = 1.0;
        for (double length : route) {
            remaining -= length;
            const double after = std::max(0.0, remaining);
            const double duration = length * durationScale(length, after);
            constexpr double dt = 0.00001;
            const double startSpeed = length * progress(dt / duration, length, after) / dt;
            const double endSpeed = length * (1.0 - progress(1.0 - dt / duration, length, after)) / dt;
            require(std::abs(startSpeed - previousEndSpeed) < 0.00001, "speed jumps at cell boundary");
            require(endSpeed <= startSpeed + 0.00001, "arrival speed increases");
            previousEndSpeed = endSpeed;
        }
        require(previousEndSpeed < 0.00001, "arrival does not approach zero speed");
    }
    std::cout << "Vehicle arrival curve: cruise, short/diagonal routes, continuous braking, exact stop passed\n";
}
