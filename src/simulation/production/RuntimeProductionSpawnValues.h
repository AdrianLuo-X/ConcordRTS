#pragma once
#include "world/values/RuntimeCellPoint.h"
#include "simulation/ai/RuntimeAircraftProductionMatcher.h"
#include <optional>
struct WorldObjectInstance;
// Existing value identities retained for the original C++ ABI.
namespace concord::runtime::session_detail {
struct RuntimeProductionSpawn {
    const WorldObjectInstance* anchor = nullptr;
    RuntimeCellPoint cell;
    int dockIndex = -1;
    bool aircraftDock = false;
    double renderX = 0.0;
    double renderY = 0.0;
    int level = 0;
};

struct RuntimeAircraftProductionMatchInputBuild {
    RuntimeAircraftProductionMatchInput input;
    std::optional<RuntimeAircraftProductionMatchResult> error;
};
}
