#pragma once

#include "simulation/navigation/pathfinding/PathGoalTypes.h"

namespace concord::simulation::pathfinding::detail {

inline long long cellKey(int x, int y) {
    return (static_cast<long long>(y) << 32)
        ^ static_cast<unsigned int>(x);
}

inline long long cellKey(PathCell cell) {
    return cellKey(cell.x, cell.y);
}

inline bool sameCell(PathCell left, PathCell right) {
    return left.x == right.x && left.y == right.y;
}

inline bool pathCellsLess(PathCell left, PathCell right) {
    if (left.y != right.y) {
        return left.y < right.y;
    }
    return left.x < right.x;
}

} // namespace concord::simulation::pathfinding::detail
