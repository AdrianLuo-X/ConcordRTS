#include "RuntimeNearestSubcellSelection.h"
#include <cmath>
#include <limits>
int concord::simulation::navigation::selectNearestAvailableSubCell(
    const std::vector<int>& available, WorldObjectInstance& grounded,
    int x, int y, double desiredX, double desiredY) {
    int selected = -1;
    double nearest = std::numeric_limits<double>::infinity();
    for (int slot : available) {
        grounded.subCell = slot;
        const auto offset = worldObjectSubCellOffset(grounded);
        const double distance = std::pow(x + offset.x - desiredX, 2)
            + std::pow(y + offset.y - desiredY, 2);
        if (distance < nearest) { nearest = distance; selected = slot; }
    }
    return selected;
}
