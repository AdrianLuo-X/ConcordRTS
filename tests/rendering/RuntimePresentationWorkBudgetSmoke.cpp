#include "runtime/timing/RuntimeGameSpeedTiming.h"
#include "runtime/timing/RuntimePresentationWorkBudget.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void scenario(int speed, double tickMs, double paintMs) {
    const auto timing = runtimeGameSpeedTiming(speed, 30);
    RuntimePresentationWorkBudget budget(1.0 / 60.0, paintMs, tickMs,
        speed == 6 ? 1.0 / runtimeGameSpeedTiming(5, 30).secondsPerTick : 1.0 / timing.secondsPerTick);
    double wall = 0.0, priorWall = 0.0, credit = 0.0, deadline = 1.0 / 60.0;
    int ticks = 0, paints = 0, maximumTicks = 0;
    for (int batch = 0; batch < 2000; ++batch) {
        credit = timing.accumulate(credit, wall - priorWall, 16);
        priorWall = wall;
        const double start = wall;
        int advanced = 0;
        while (timing.ready(credit, advanced, 16)) {
            ++ticks;
            ++advanced;
            wall += tickMs / 1000.0;
            const double previousCredit = credit;
            credit = timing.consume(credit);
            require(timing.unlimited() || std::abs(credit
                - std::max(0.0, previousCredit - timing.secondsPerTick)) < 1e-12,
                "yield changed tick credit consumption");
            if (budget.shouldYield(advanced, wall - start, wall >= deadline)) break;
        }
        maximumTicks = std::max(maximumTicks, advanced);
        if (wall >= deadline) {
            // The next service batch starts after the paint, including when
            // a slow paint itself leaves its deadline in the past.
            deadline += 1.0 / 60.0;
            if (deadline + 1.0 / 60.0 <= wall) deadline = wall + 1.0 / 60.0;
            wall += paintMs / 1000.0;
            ++paints;
        } else if (!timing.unlimited()) wall += 0.001;
    }
    require(ticks > 0 && paints > 0, "simulation or presentation was starved");
    require(maximumTicks <= 16, "catch-up cap changed");
    if (speed == 6 && tickMs <= 4.0)
        require(maximumTicks > 1, "stale deadline caused one tick per paint");
    if (speed != 6 && tickMs == 0.1 && paintMs == 1.0) {
        const double expected = wall / timing.secondsPerTick;
        require(std::abs(ticks - expected) <= 2.0, "finite authored speed changed");
    }
    if (speed != 6 && tickMs == 4.0 && paintMs == 10.0) {
        const double expected = wall / timing.secondsPerTick;
        require(std::abs(ticks - expected) <= 18.0,
            "presentation stole the CPU share of a feasible finite speed");
    }
}
}
int main() {
    try {
        RuntimePresentationWorkBudget budget(1.0 / 60.0, 11.0);
        require(!budget.shouldYield(0, 100.0, true), "yield split a tick");
        require(!budget.shouldYield(5, 100.0, false), "yield without due paint");
        require(budget.shouldYield(5, 0.04, true), "due paint was not serviced");
        require(!budget.shouldYield(5, std::numeric_limits<double>::quiet_NaN(), true),
            "invalid elapsed time interrupted service");
        for (double ms = 9.8; ms <= 10.2; ms += 0.01) {
            const RuntimePresentationWorkBudget before(1.0 / 60.0, ms);
            const RuntimePresentationWorkBudget after(1.0 / 60.0, ms + 0.01);
            require(std::abs(after.seconds - before.seconds) <= 0.00004,
                "drawing-time noise caused a discontinuous service budget");
        }
        const RuntimePresentationWorkBudget light(1.0 / 60.0, 11.0, 1.0, 187.5);
        const RuntimePresentationWorkBudget loaded(1.0 / 60.0, 11.0, 4.0, 187.5);
        require(loaded.seconds >= 0.033 && loaded.seconds > light.seconds,
            "high speed did not retain its measured CPU share");
        for (int speed = 0; speed <= 6; ++speed)
            for (double tickMs : {0.1, 4.0, 20.0})
                for (double paintMs : {1.0, 10.0, 30.0, 100.0})
                    scenario(speed, tickMs, paintMs);
        std::cout << "Presentation service budget passed: 84 clock scenarios, "
            "finite cadence, retained tick credit, unlimited service, slow paints.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
