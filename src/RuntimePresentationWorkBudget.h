#pragma once

#include <algorithm>
#include <cmath>

// A complete simulation tick is indivisible. Presentation may interrupt only
// between ticks; remaining time credit stays with RuntimeGameSpeedTiming.
// This budget is measured from the current service batch, never from an old
// paint deadline, so unlimited speed cannot collapse to one tick per paint.
struct RuntimePresentationWorkBudget {
    double seconds = 1.0 / 60.0;

    RuntimePresentationWorkBudget(double frameSeconds, double previousPaintMs,
        double simulationMs = 0.0, double requestedTickHz = 0.0) {
        if (!std::isfinite(frameSeconds) || frameSeconds <= 0.0)
            frameSeconds = 1.0 / 60.0;
        if (!std::isfinite(previousPaintMs) || previousPaintMs < 0.0)
            previousPaintMs = 0.0;
        const double paintSeconds = previousPaintMs / 1000.0;
        if (paintSeconds <= frameSeconds * 0.6) {
            // When drawing fits comfortably in a frame, leave its measured
            // share of the slot free. The deadline is still tested separately.
            seconds = std::max(frameSeconds * 0.5, frameSeconds - paintSeconds);
        } else {
            // Increase amortization continuously as painting fills the slot.
            // A small drawing-time fluctuation must not jump the service
            // budget from half a frame to two frames.
            seconds = std::min(frameSeconds * 2.0, frameSeconds * 0.5
                + (paintSeconds - frameSeconds * 0.6) * 3.75);
        }
        if (std::isfinite(simulationMs) && simulationMs > 0.0
            && std::isfinite(requestedTickHz) && requestedTickHz > 0.0) {
            // Painting more often must not consume the CPU share needed by
            // the authored speed. Amortize drawing when that share grows;
            // retain the same two-frame bound and indivisible tick limit.
            const double simulationShare = std::clamp(
                simulationMs * requestedTickHz / 1000.0, 0.0, 0.95);
            const double cadenceBudget = paintSeconds * simulationShare
                / (1.0 - simulationShare);
            seconds = std::min(frameSeconds * 2.0, std::max(seconds, cadenceBudget));
        }
    }

    bool shouldYield(int completedTicks, double elapsedSeconds, bool paintDue) const {
        return completedTicks > 0 && paintDue
            && std::isfinite(elapsedSeconds) && elapsedSeconds >= seconds;
    }
};
