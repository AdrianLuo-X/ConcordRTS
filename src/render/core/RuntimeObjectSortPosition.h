#pragma once

#include <algorithm>

#include "world/model/GameWorld.h"

namespace concord::render {

struct RuntimeObjectSortPosition {
    double x;
    double y;
};

static inline RuntimeObjectSortPosition runtimeObjectSortPosition(
    const WorldObjectInstance& object,
    double sortX,
    double sortY,
    int footprintWidth,
    int footprintHeight
) {
    const WorldObjectCellOffset subCellOffset =
        worldObjectSubCellOffset(object);
    sortX += subCellOffset.x;
    sortY += subCellOffset.y;
    if (object.kind == WorldObjectKind::Structure) {
        sortX += static_cast<double>(std::max(1, footprintWidth) - 1);
        sortY += static_cast<double>(std::max(1, footprintHeight) - 1);
    }
    return {sortX, sortY};
}

static inline double runtimeObjectYSortLeptons(RuntimeObjectSortPosition position) {
    // Keep ObjectClass::GetYSort in leptons, before screen projection and rounding.
    return (position.x + position.y) * 256.0;
}

} // namespace concord::render
