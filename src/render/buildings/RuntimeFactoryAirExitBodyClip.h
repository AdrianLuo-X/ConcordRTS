#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

#include <span>

namespace concord::render {

struct RuntimeFactoryAirExitTransition {
    int startedTick;
    int openingTicks;
};

template<class StructureRecord, class DescribeTransition>
RuntimeInputPixelRect clipRuntimeFactoryAirExitBody(RuntimeInputPixelRect viewport, int hostId, int animationTick,
    std::span<const StructureRecord> structures, DescribeTransition&& describeTransition) {
    if (hostId < 0) return viewport;
    for (const auto& record : structures) {
        const auto transition = describeTransition(record, hostId);
        if (!transition) continue;
        // The native foreground wall supplies occlusion after the hatch opens.
        if (animationTick - transition->startedTick < transition->openingTicks)
            viewport.bottom = viewport.top;
        break;
    }
    return viewport;
}

} // namespace concord::render
