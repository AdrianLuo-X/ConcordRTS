#pragma once

#include <cstddef>
#include <cstdlib>

inline bool runtimePathQueueWarmAdmissionReference() {
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_PATH_QUEUE_WARM_ADMISSION_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

struct RuntimePathQueueWarmAdmission {
    bool reuseLayout = false;
    bool borrowTerrain = false;
};

// The caller has checked both vector shapes before permitting either read.
// Only pure values are examined. A failed borrow check must NOT stop the
// complete coordinate check: layout reuse has its own independent gate.
template<class Cells, class Coordinates, class Indices>
RuntimePathQueueWarmAdmission runtimeCheckPathQueueWarmAdmission(
    const Cells& cells, const Coordinates& coordinates, const Indices& indices,
    bool reuseEligible, bool borrowEligible,
    bool reference = runtimePathQueueWarmAdmissionReference()) {
    RuntimePathQueueWarmAdmission result{reuseEligible, reuseEligible && borrowEligible};
    if (!reuseEligible) return result;
    const auto sameCoordinate = [&](std::size_t index) {
        return coordinates[index].x == cells[index].cell.x
            && coordinates[index].y == cells[index].cell.y;
    };
    const auto canBorrow = [&](std::size_t index) {
        return indices[index] == static_cast<int>(index)
            && cells[index].cost >= 1 && cells[index].hazardCost >= 0;
    };
    if (reference) {
        // C37: two separate, independently short-circuited passes.
        for (std::size_t index = 0; index < cells.size(); ++index) {
            if (!sameCoordinate(index)) {
                result.reuseLayout = result.borrowTerrain = false;
                break;
            }
        }
        if (result.borrowTerrain) {
            for (std::size_t index = 0; index < cells.size(); ++index) {
                if (!canBorrow(index)) { result.borrowTerrain = false; break; }
            }
        }
    } else {
        for (std::size_t index = 0; index < cells.size(); ++index) {
            if (!sameCoordinate(index)) {
                result.reuseLayout = result.borrowTerrain = false;
                break;
            }
            if (result.borrowTerrain && !canBorrow(index)) result.borrowTerrain = false;
        }
    }
    return result;
}
