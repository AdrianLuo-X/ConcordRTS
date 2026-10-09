#pragma once

#include "RuntimeSubcellSystem.h"

// Only the caller's independently proven empty object/terrain cell may use
// this predicate. Reservations remain CURRENT inputs, including self, layers,
// invalid locations and legacy cell-only records. No snapshot is retained.
// It is the boolean projection of availableSubCells(noObjects,noTerrain,
// destination,{ignoredMover}) with default gate context, not a general query.
inline bool runtimeEmptySubcellHasAvailability(
    const RuntimeSubcellLocation& destination,
    int ignoredMover,
    const std::map<int, RuntimeSubcellReservation>& reservations) noexcept {
    const bool coordinates = destination.x >= 0 && destination.y >= 0;
    if (!coordinates && destination.cell < 0) return false;
    unsigned blocked = 0;
    for (const auto& [moverId, reservation] : reservations) {
        if (moverId == ignoredMover) continue;
        unsigned slot = 0;
        switch (reservation.subCell) {
        case 2: slot = 1; break;
        case 4: slot = 2; break;
        case 3: slot = 4; break;
        default: continue;
        }
        const auto& reserved = reservation.destination;
        const bool reservedCoordinates = reserved.x >= 0 && reserved.y >= 0;
        if ((!reservedCoordinates && reserved.cell < 0)
            || reserved.onBridge != destination.onBridge
            || reservedCoordinates != coordinates) continue;
        if (coordinates
            ? (reserved.x != destination.x || reserved.y != destination.y)
            : reserved.cell != destination.cell) continue;
        blocked |= slot;
    }
    return blocked != 7;
}
