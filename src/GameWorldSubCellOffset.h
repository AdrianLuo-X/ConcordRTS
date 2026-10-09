#pragma once

#include "GameWorld.h"

// Canonical RA2 infantry slots, shared by drawing and ground occupancy.
// Keeping this pure table inline lets component-only queries use the same
// geometry without linking the world/map loader.
inline WorldObjectCellOffset worldInfantrySubCellOffset(int subCell) {
    switch (subCell) {
    case 2: return {0.25, -0.25};
    case 4: return {0.25, 0.25};
    case 3: return {-0.25, 0.25};
    default: return {};
    }
}
