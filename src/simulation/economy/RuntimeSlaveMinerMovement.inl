// Both forms retain the same worker manager and home relationships.
bool runtimeObjectIsMobileSlaveMiner(
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
    const auto* unit = object.kind == WorldObjectKind::Unit
        ? findRuntimeUnitType(state.rules, object) : nullptr;
    return unit != nullptr && runtimeUnitIsMobileSlaveMiner(*unit);
}

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerDeployCellNearOre(
    const RuntimeWindowState& state, const WorldObjectInstance& miner,
    const RuntimeCellPoint& ore,
    const RuntimeManualFormationMappedFilter* manualMappedFilter = nullptr, const std::string* owner = nullptr
) {
    const auto* unit = findRuntimeUnitType(state.rules, miner);
    if (unit == nullptr || !runtimeUnitIsMobileSlaveMiner(*unit)
        || !runtimeCellHasHarvestableResource(state, ore)) return std::nullopt;
    const auto* building = findRuntimeBuildingTypeById(state.rules, unit->deploysInto);
    if (building == nullptr) return std::nullopt;
    RuntimeBuildCatalogItem item;
    item.kind = RuntimeBuildKind::Building;
    item.typeId = building->id;
    item.waterBound = building->waterBound;
    const auto footprint = runtimeStructureFootprintForBuildItem(state, item);
    const int width = std::max(1, footprint.width), height = std::max(1, footprint.height);
    const auto occupied = buildRuntimeOccupancyLookup(state, miner.instanceId);
    const auto origin = runtimeCellPointForObject(miner);
    std::vector<RuntimeCellPoint> candidates;
    // Keep the resource outside the foundation, immediately beside its edge.
    for (int y = ore.y - height; y <= ore.y + 1; ++y) {
        for (int x = ore.x - width; x <= ore.x + 1; ++x) {
            if (x <= ore.x && ore.x < x + width && y <= ore.y && ore.y < y + height)
                continue;
            const RuntimeCellPoint cell{x, y};
            if (terrainLevelAtRuntimeCell(state, x, y)
                    != terrainLevelAtRuntimeCell(state, ore.x, ore.y)
                || !runtimeCellCanPlaceBuilding(state, item, cell, miner.instanceId, &occupied, owner ? *owner : std::string{}))
                continue;
            // Vehicle routes, manual mapping and deployment requests use
            // center anchors; the preceding foundation check alone uses a corner.
            candidates.push_back(runtimePlacementAnchorCellForFootprint(cell, footprint));
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& left, const auto& right) {
        const auto distance = [&](const auto& cell) {
            const long long dx = cell.x - origin.x, dy = cell.y - origin.y;
            return dx * dx + dy * dy;
        };
        if (distance(left) != distance(right)) return distance(left) < distance(right);
        return RuntimeCellPointLess{}(left, right);
    });
    for (const auto& cell : candidates) {
        if (cell != origin) {
            const auto path = findRuntimePath(state, miner, origin, cell);
            if (path.empty() || path.back() != cell) continue;
        }
        // Check the workers' ground route as well as the vehicle route.
        WorldObjectInstance worker = miner;
        worker.instanceId = -1;
        worker.kind = WorldObjectKind::Infantry;
        worker.type = building->enslaves.empty() ? "SLAV" : building->enslaves;
        worker.x = cell.x;
        worker.y = cell.y;
        worker.cell = runtimeCellKey(cell.x, cell.y);
        worker.onBridge = false;
        if (findRuntimeHarvesterResourceApproach(state, worker, ore, owner).has_value()
            && (manualMappedFilter == nullptr || manualMappedFilter->allows(cell))) return cell;
    }
    return std::nullopt;
}

void synchronizeRuntimeStoredSlaveMinerWorker(
    RuntimeWindowState&, WorldObjectInstance&, const WorldObjectInstance&
);

void transferRuntimeSlaveMinerWorkers(
    RuntimeWindowState& state, int sourceId, const WorldObjectInstance& target
) {
    if (const auto regen = state.slaveMinerRegenTicks.find(sourceId);
        regen != state.slaveMinerRegenTicks.end()) {
        const int ticks = regen->second;
        state.slaveMinerRegenTicks.erase(regen);
        state.slaveMinerRegenTicks[target.instanceId] = ticks;
    }
    for (auto& [workerId, task] : state.slaveMinerTasks) {
        if (task.refineryId != sourceId) continue;
        task.refineryId = target.instanceId;
        task.status = "miner relocated";
        if (auto behavior = state.behaviors.find(workerId); behavior != state.behaviors.end()
            && behavior->second.pendingAllowedOccupiedObjectId == sourceId)
            behavior->second.pendingAllowedOccupiedObjectId = target.instanceId;
        // Do not cancel a live gather/return segment or replace its ore goal,
        // cargo, shovel clock, pause state or phase during the transformation.
        if (task.phase == RuntimeSlaveMinerPhase::Stored) {
            if (auto* worker = findMutableRuntimeObjectByIdIncludingInactive(state, workerId);
                worker && worker->health > 0)
                synchronizeRuntimeStoredSlaveMinerWorker(state, *worker, target);
        }
    }
}
