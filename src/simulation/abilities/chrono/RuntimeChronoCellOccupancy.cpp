#include "RuntimeChronoCellOccupancy.h"
namespace concord::simulation::abilities {
bool runtimeChronoCellIsOccupied(const std::unordered_map<int, int>& occupied,
    int x, int y, int objectId, const RuntimeChronoOccupancyQueries& queries) {
    const auto* object = queries.findObject(objectId);
    if (object && queries.usesGroundSubcells(*object))
        return queries.availableSubcells(*object, {x, y}).empty();
    const auto found = occupied.find(queries.cellKey(x, y));
    return found != occupied.end() && found->second != objectId;
}
}
