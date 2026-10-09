int runtimeHarvesterDepartureRefineryId(
    const RuntimeWindowState& state, const WorldObjectInstance& harvester
) {
    if (const auto worker = state.slaveMinerTasks.find(harvester.instanceId);
        worker != state.slaveMinerTasks.end()) {
        const auto* parent = findObjectById(state, worker->second.refineryId);
        if (parent && parent->owner == harvester.owner
            && runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(harvester), *parent) == 0.0)
            return parent->instanceId;
    }
    const auto task = state.harvesterTasks.find(harvester.instanceId);
    if (task == state.harvesterTasks.end()) return -1;
    const auto* refinery = findObjectById(state, task->second.targetRefineryId);
    // The dock can replace impassable ground with the refinery's drive row.
    // Retain that same passage for the first journey out after unloading.
    return refinery != nullptr
        && runtimeHarvesterCanUnloadAtRefinery(state, harvester, *refinery)
        && runtimeStructureCellIsVehicleDriveRow(
            state, *refinery, runtimeCellPointForObject(harvester), harvester)
        ? refinery->instanceId : -1;
}

// Harvesting must have ground contact with the resource. A nearby empty cell
// on the other side of a cliff/wall is not a valid mining approach.
struct RuntimeHarvesterResourceAccess {
    const RuntimeWindowState& state;
    const WorldObjectInstance& harvester;
    int departureRefineryId;
    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const std::unordered_map<int, const TerrainCell*>* terrain;
    std::unordered_map<int, int> staticOccupancy;
    std::optional<std::unordered_map<int, int>> pathOccupancy;
    std::optional<std::unordered_set<int>> reachableAfterFailure;

    RuntimeHarvesterResourceAccess(const RuntimeWindowState& worldState, const WorldObjectInstance& miner)
        : state(worldState), harvester(miner),
          departureRefineryId(runtimeHarvesterDepartureRefineryId(state, miner)),
          terrain(&state.terrainByCell),
          staticOccupancy(buildRuntimeOccupancyLookup(state, miner.instanceId)) {
        if (terrain->empty()) {
            generatedTerrain = buildRuntimeTerrainLookup(state.world);
            terrain = &generatedTerrain;
        }
        // Another miner can share the same ore deposit. Mobile traffic must not
        // make a stationary harvester change its intake direction every tick.
        for (auto it = staticOccupancy.begin(); it != staticOccupancy.end();) {
            const auto* occupant = runtimeOccupantIdIsTerrain(it->second)
                ? nullptr : findObjectById(state, it->second);
            if (occupant != nullptr && occupant->kind != WorldObjectKind::Structure)
                it = staticOccupancy.erase(it);
            else ++it;
        }
    }

    bool canGatherFrom(const RuntimeCellPoint& from, const RuntimeCellPoint& resource) const {
        if (!runtimeCellHasHarvestableResource(state, resource)
            || std::abs(from.x - resource.x) > 1 || std::abs(from.y - resource.y) > 1)
            return false;
        const auto source = terrain->find(runtimeCellKey(from.x, from.y));
        if (source == terrain->end() || source->second == nullptr
            || runtimeCachedTerrainInfoForCell(state, *source->second).bridge)
            return false;
        // A delivery point inside a refinery is valid for unloading and
        // departure, but cannot become a stationary mining approach.
        if (!runtimeCellIsPassable(state, *terrain, staticOccupancy, from, harvester))
            return false;
        if (from == resource) return runtimeCellIsPassable(
            state, *terrain, staticOccupancy, resource, harvester);
        return runtimeCellTransitionIsPassable(
            state, *terrain, staticOccupancy, from, resource, harvester, -1, nullptr, false);
    }

    const std::unordered_map<int, int>& occupiedForPath() {
        if (!pathOccupancy) {
            pathOccupancy = buildRuntimeOccupancyLookup(state, harvester.instanceId);
        }
        return *pathOccupancy;
    }

    void rememberFailedSearch() {
        if (!reachableAfterFailure) {
            // One blocked worker can see hundreds of ore cells outside its
            // pocket. Discover its reachable ground once for this search,
            // rather than rebuilding a path request for every ore approach.
            reachableAfterFailure = buildRuntimeReachableMoveCells(
                state, harvester, {runtimeCellPointForObject(harvester)},
                *terrain, occupiedForPath(), departureRefineryId);
        }
    }
};

std::optional<RuntimeCellPoint> findRuntimeHarvesterResourceApproach(
    RuntimeHarvesterResourceAccess& access,
    const RuntimeCellPoint& resource
) {
    const auto& state = access.state;
    const auto& harvester = access.harvester;
    const RuntimeCellPoint from = runtimeCellPointForObject(harvester);
    if (access.canGatherFrom(from, resource)) return from;
    // Invalid resource terrain cannot become reachable by choosing a distant
    // fallback destination. Check it before doing any path searches.
    if (!access.canGatherFrom(resource, resource)) return std::nullopt;
    const auto& occupied = access.occupiedForPath();
    std::vector<RuntimeCellPoint> approaches{resource};
    const auto neighbors = runtimeNeighborsOf(resource);
    approaches.insert(approaches.end(), neighbors.begin(), neighbors.end());
    for (const auto& approach : approaches) {
        if (!access.canGatherFrom(approach, resource)
            || !runtimeCellIsPassable(state, *access.terrain, occupied, approach, harvester))
            continue;
        if (access.reachableAfterFailure
            && !access.reachableAfterFailure->contains(runtimeCellKey(approach.x, approach.y)))
            continue;
        const auto path = findRuntimePath(
            state, harvester, from, approach, access.departureRefineryId);
        if (!path.empty() && path.back() == approach) return approach;
        access.rememberFailedSearch();
    }
    return std::nullopt;
}

std::optional<RuntimeCellPoint> findRuntimeHarvesterResourceApproach(
    const RuntimeWindowState& state, const WorldObjectInstance& harvester,
    const RuntimeCellPoint& resource
) {
    RuntimeHarvesterResourceAccess access(state, harvester);
    return findRuntimeHarvesterResourceApproach(access, resource);
}
