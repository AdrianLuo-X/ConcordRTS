// Called only after the tick occupancy proof has established that the cell
// contains no object/terrain subcell blockers. Reservations are deliberately
// queried through the original API, retaining layer, ignored-self and slot order.
bool runtimeInfantryCanUseEmptyCellAvailability(
    const RuntimeWindowState& state, const RuntimeCellPoint& destination
) {
    if (!state.movementEmptyCellProof.subcellCompatible
        || !runtimeMovementTickProvesEmptyCell(state, destination)) return false;
    const auto& objects = state.world.getObjects();
    // Air movement does not update ground navigation occupancy. Read the
    // current pose, including same-tick landing, docking and dock release.
    for (std::size_t index : state.movementEmptyCellProof.subcellAirObjectIndices) {
        if (index >= objects.size()) return false;
        const auto& object = objects[index];
        if (object.alive && !object.airborne && object.x == destination.x
            && object.y == destination.y) return false;
    }
    return true;
}

std::vector<int> runtimeInfantrySubcellsForProvenEmptyCell(
    const RuntimeWindowState& state, const WorldObjectInstance& mover,
    const RuntimeCellPoint& destination
) {
    static const std::vector<WorldObjectInstance> noObjects;
    static const std::vector<WorldTerrainObject> noTerrainObjects;
    return state.subcells.availableSubCells(noObjects, noTerrainObjects,
        runtimeSubcellLocationForCell(state, destination), {mover.instanceId});
}

struct RuntimeInfantryAvoidanceSubcellBatch;
static thread_local RuntimeInfantryAvoidanceSubcellBatch* runtimeActiveInfantryAvoidanceBatch = nullptr;

// This scope must end with the read-only candidate loop, before facing, crush,
// reservations or movement can mutate the world. Nested scopes own their slots.
struct RuntimeInfantryAvoidanceSubcellBatch {
    const RuntimeWindowState* state;
    const WorldObjectInstance* mover;
    std::array<RuntimeCellPoint, 8> cells;
    std::vector<std::vector<int>> available;
    RuntimeInfantryAvoidanceSubcellBatch* previous;
    int materializations = 0;
    bool unsupported = false;
    RuntimeInfantryAvoidanceSubcellBatch(const RuntimeWindowState& value,
        const WorldObjectInstance& object, RuntimeCellPoint current)
        : state(&value), mover(&object), cells(runtimeNeighborsOf(current)),
          previous(runtimeActiveInfantryAvoidanceBatch) {
        runtimeActiveInfantryAvoidanceBatch = this;
    }
    ~RuntimeInfantryAvoidanceSubcellBatch() { runtimeActiveInfantryAvoidanceBatch = previous; }
    RuntimeInfantryAvoidanceSubcellBatch(const RuntimeInfantryAvoidanceSubcellBatch&) = delete;
    RuntimeInfantryAvoidanceSubcellBatch& operator=(const RuntimeInfantryAvoidanceSubcellBatch&) = delete;

    const std::vector<int>* find(const RuntimeWindowState& value,
        const WorldObjectInstance& object, RuntimeCellPoint cell, RuntimeGatePassageMode mode) {
        if (unsupported || state != &value || mover != &object || object.kind != WorldObjectKind::Infantry
            || mode != RuntimeGatePassageMode::Commit) return nullptr;
        const auto found = std::find(cells.begin(), cells.end(), cell);
        if (found == cells.end()) return nullptr;
        if (available.empty()) {
            // The batch API expands terrain foundations. Keep unusual maps on
            // the targeted scalar query instead of allocating an unbounded map.
            std::int64_t terrainFootprintCells = 0;
            for (const auto& terrain : value.world.getTerrainObjects()) {
                if (!terrain.alive || terrain.health <= 0) continue;
                const std::int64_t width = std::max(1, terrain.footprintWidth);
                const std::int64_t height = std::max(1, terrain.footprintHeight);
                if (width > 4096 || height > 4096
                    || width * height > 4096 - terrainFootprintCells
                    || static_cast<std::int64_t>(terrain.x) + width - 1 > std::numeric_limits<int>::max()
                    || static_cast<std::int64_t>(terrain.y) + height - 1 > std::numeric_limits<int>::max()) {
                    unsupported = true;
                    return nullptr;
                }
                terrainFootprintCells += width * height;
            }
            std::vector<RuntimeSubcellLocation> destinations;
            destinations.reserve(cells.size());
            for (const auto& destination : cells)
                destinations.push_back(runtimeSubcellLocationForCell(value, destination));
            available = value.subcells.availableSubCellsBatch(value.world.getObjects(),
                value.world.getTerrainObjects(), destinations, {object.instanceId},
                runtimeGateSubcellContext(value, &object, mode));
            ++materializations;
        }
        return &available[static_cast<std::size_t>(found - cells.begin())];
    }
};
