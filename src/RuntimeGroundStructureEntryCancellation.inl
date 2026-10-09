// Include before eraseRuntimeBehavior. A slotless entry commits its destination
// before the visible body reaches the host. Revoking that intent must also
// revoke the temporary foundation cell; ordinary service/exit leases stay owned.
bool runtimeCancelledStructureEntryNeedsRecovery(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& previous
) {
    if (!object.alive || object.health <= 0 || object.airborne
        || (object.kind != WorldObjectKind::Infantry && object.kind != WorldObjectKind::Unit)
        || runtimeObjectMovementMode(state, object) == PathMovementMode::Air
        || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
        || previous.kind != RuntimeBehaviorKind::Move || previous.pendingAllowedOccupiedObjectId < 0
        || object.garrisonHostObjectId >= 0 || object.tankBunkerHostId >= 0
        || object.dockedToObjectId >= 0 || object.factoryExitHostId >= 0) return false;
    const int hostId = previous.pendingAllowedOccupiedObjectId;
    const auto* host = findObjectById(state, hostId);
    if (!host || !host->alive || host->kind != WorldObjectKind::Structure) return false;
    const auto footprint = runtimeStructureFootprintForObject(state, *host);
    const auto offset = worldObjectSubCellOffset(object);
    const double footX = runtimeObjectRenderX(object) + offset.x;
    const double footY = runtimeObjectRenderY(object) + offset.y;
    const bool footInside = std::isfinite(footX) && std::isfinite(footY)
        && footX > host->x - .5 + 1e-9 && footX < host->x + footprint.width - .5 - 1e-9
        && footY > host->y - .5 + 1e-9 && footY < host->y + footprint.height - .5 - 1e-9;
    if (!footInside && !runtimeStructureCellInsideFoundation(state, *host, runtimeCellPointForObject(object))) return false;
    if (std::any_of(state.factoryDeliveries.leases().begin(), state.factoryDeliveries.leases().end(),
            [&](const auto& lease) { return lease.producedObjectId == object.instanceId; })) return false;
    if (std::any_of(state.repairDepots.relations().begin(), state.repairDepots.relations().end(),
            [&](const auto& relation) { return relation.unitId == object.instanceId && relation.hostId == hostId; })) return false;
    // Player commands release a repair relation before erasing its approach.
    // The authored pad/drive-row departure remains the repair system's contract.
    const auto* hostRules = state.rules.findBuildingType(host->type);
    if (object.kind == WorldObjectKind::Unit && hostRules && hostRules->unitRepair) return false;
    const auto harvester = state.harvesterTasks.find(object.instanceId);
    if (harvester != state.harvesterTasks.end() && harvester->second.targetRefineryId == hostId
        && harvester->second.phase != RuntimeHarvesterPhase::Idle) return false;
    const auto slave = state.slaveMinerTasks.find(object.instanceId);
    if (slave != state.slaveMinerTasks.end() && slave->second.refineryId == hostId
        && (slave->second.phase == RuntimeSlaveMinerPhase::Returning
            || slave->second.phase == RuntimeSlaveMinerPhase::Stored)) return false;
    if (std::any_of(state.grinding.relations().begin(), state.grinding.relations().end(),
            [&](const auto& relation) { return relation.sourceId == object.instanceId && relation.hostId == hostId; })) return false;

    // Revoking an already accepted foundation permission must not depend on
    // a pending map or current entry eligibility: rejection may clear that map,
    // and capture may change the host's owner while the last step is visible.
    return true;
}

bool runtimeCancelledStructureEntryNeedsRecovery(
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
    const auto previous = state.behaviors.find(object.instanceId);
    return previous != state.behaviors.end()
        && runtimeCancelledStructureEntryNeedsRecovery(state, object, previous->second);
}

// false leaves the object and entry authority untouched. The caller must retain
// the original behavior and reject cancellation when recovery is required but
// every ordinary outside destination is blocked.
bool recoverRuntimeCancelledStructureEntry(
    RuntimeWindowState& state, WorldObjectInstance& object,
    const RuntimeObjectBehavior& previous
) {
    if (!runtimeCancelledStructureEntryNeedsRecovery(state, object, previous)) return false;
    const auto oldSlot = worldObjectSubCellOffset(object);
    const double bodyX = runtimeObjectRenderX(object) + oldSlot.x;
    const double bodyY = runtimeObjectRenderY(object) + oldSlot.y;
    const double bodyLevel = worldObjectHasGroundRenderPose(object) ? object.renderLevel : double(object.level);
    const bool bodyOnBridge = worldObjectHasGroundRenderPose(object) ? object.renderOnBridge : object.onBridge;
    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const auto* terrain = &state.terrainByCell;
    if (terrain->empty()) { generatedTerrain = buildRuntimeTerrainLookup(state.world); terrain = &generatedTerrain; }
    const auto occupied = buildRuntimeOccupancyLookup(state, object.instanceId);
    const auto ordinary = [&](const RuntimeCellPoint& cell) {
        if (runtimeCellKey(cell.x, cell.y) < 0
            || !runtimeCellIsPassable(state, *terrain, occupied, cell, object, -1, nullptr, false)) return false;
        return !runtimeInfantryUsesGroundSubcells(object)
            || !state.subcells.availableSubCells(state.world.getObjects(), state.world.getTerrainObjects(),
                runtimeSubcellLocationForCell(state, cell), {object.instanceId},
                runtimeGateSubcellContext(state, &object, RuntimeGatePassageMode::Commit)).empty();
    };
    const auto cellForBody = [](double x, double y) -> std::optional<RuntimeCellPoint> {
        if (!std::isfinite(x) || !std::isfinite(y) || x < -.5 || y < -.5 || x >= 511.5 || y >= 511.5)
            return std::nullopt;
        return RuntimeCellPoint{static_cast<int>(std::floor(x + .5)), static_cast<int>(std::floor(y + .5))};
    };
    const bool infantry = runtimeInfantryUsesGroundSubcells(object);
    // Ordinary occupancy grants a slot, not arbitrary continuous body space.
    // Inspect every live ground body, including stationary units without a
    // render record. Recovery cannot use the movement-only separation exception.
    const auto bodyClear = [&](double x, double y, bool onBridge) {
        for (const auto& other : state.world.getObjects()) {
            if (other.instanceId == object.instanceId
                || !runtimeAllGroundBodyIsSpatialCandidate(state, other)
                || onBridge != (worldObjectHasGroundRenderPose(other) ? other.renderOnBridge : other.onBridge)) continue;
            const auto offset = worldObjectSubCellOffset(other);
            const double dx = x - runtimeObjectRenderX(other) - offset.x;
            const double dy = y - runtimeObjectRenderY(other) - offset.y;
            const bool otherInfantry = other.kind == WorldObjectKind::Infantry;
            const double minimum = infantry && otherInfantry ? .5 : !infantry && !otherInfantry ? 1.0 : .75;
            if (dx * dx + dy * dy + 1e-9 < minimum * minimum) return false;
        }
        return true;
    };
    const double sourceBodyX = previous.renderFrom.x + previous.renderFromOffsetX + oldSlot.x;
    const double sourceBodyY = previous.renderFrom.y + previous.renderFromOffsetY + oldSlot.y;
    std::optional<RuntimeCellPoint> destination;
    int recoveredSlot = -1;
    double recoveredBodyX = 0, recoveredBodyY = 0, recoveredLevel = 0;
    bool recoveredOnBridge = false;
    enum class RecoveryPose { Current, Source, Canonical };
    const auto tryDestination = [&](const RuntimeCellPoint& cell, RecoveryPose pose) {
        if (!ordinary(cell)) return false;
        const bool onBridge = runtimeCellIsBridge(state, cell.x, cell.y);
        const double level = terrainLevelAtRuntimeCell(state, cell.x, cell.y);
        const std::vector<int> slots = infantry
            ? state.subcells.availableSubCells(state.world.getObjects(), state.world.getTerrainObjects(),
                runtimeSubcellLocationForCell(state, cell), {object.instanceId},
                runtimeGateSubcellContext(state, &object, RuntimeGatePassageMode::Commit))
            : std::vector<int>{-1};
        if (slots.empty()) return false;
        const auto accept = [&](int slot, double x, double y) {
            if (!bodyClear(x, y, onBridge)) return false;
            destination = cell; recoveredSlot = slot;
            recoveredBodyX = x; recoveredBodyY = y;
            recoveredLevel = level; recoveredOnBridge = onBridge;
            return true;
        };
        if (pose != RecoveryPose::Canonical) {
            const double x = pose == RecoveryPose::Current ? bodyX : sourceBodyX;
            const double y = pose == RecoveryPose::Current ? bodyY : sourceBodyY;
            const auto heldCell = cellForBody(x, y);
            const double heldLevel = pose == RecoveryPose::Current ? bodyLevel : previous.renderFromLevel;
            const bool heldBridge = pose == RecoveryPose::Current ? bodyOnBridge : previous.renderFromOnBridge;
            if (heldCell && *heldCell == cell && heldBridge == onBridge && std::abs(heldLevel - level) < 1e-6) {
                int preciseSlot = slots.front();
                bool canKeep = !infantry || pose == RecoveryPose::Current;
                if (infantry) {
                    double nearestDistance = std::numeric_limits<double>::infinity();
                    for (int slot : slots) {
                        const auto offset = worldInfantrySubCellOffset(slot);
                        const double dx = cell.x + offset.x - x, dy = cell.y + offset.y - y;
                        const double distance = dx * dx + dy * dy;
                        if (distance < nearestDistance) { nearestDistance = distance; preciseSlot = slot; }
                        if (std::abs(dx) < 1e-6 && std::abs(dy) < 1e-6) canKeep = true;
                    }
                }
                if (canKeep && accept(preciseSlot, x, y)) return true;
            }
            // A body still safely outside stays exact. Otherwise prefer its
            // entry source before moving it to any other ordinary free slot.
            if (pose == RecoveryPose::Current) return false;
        }
        for (int slot : slots) {
            const auto offset = infantry ? worldInfantrySubCellOffset(slot) : WorldObjectCellOffset{};
            if (accept(slot, cell.x + offset.x, cell.y + offset.y)) return true;
        }
        return false;
    };
    const auto bodyCell = cellForBody(bodyX, bodyY);
    if (bodyCell) tryDestination(*bodyCell, RecoveryPose::Current);
    if (!destination) tryDestination(previous.renderFrom, RecoveryPose::Source);
    // Match the bounded deterministic perimeter order of the ordinary outside
    // search, but skip cells whose free slots are too close to a visible body.
    if (!destination) {
        const RuntimeCellPoint desired = runtimeCellKey(previous.renderFrom.x, previous.renderFrom.y) >= 0
            ? previous.renderFrom : runtimeCellPointForObject(object);
        tryDestination(desired, RecoveryPose::Canonical);
        for (int radius = 1; radius <= 6 && !destination; ++radius)
            for (int dy = -radius; dy <= radius && !destination; ++dy)
                for (int dx = -radius; dx <= radius && !destination; ++dx)
                    if (std::abs(dx) == radius || std::abs(dy) == radius)
                        tryDestination({desired.x + dx, desired.y + dy}, RecoveryPose::Canonical);
    }
    if (!destination) return false;

    releaseRuntimeInfantrySubcellReservation(state, object.instanceId);
    setRuntimeObjectCell(state, object, *destination, false);
    if (runtimeInfantryUsesGroundSubcells(object)) object.subCell = recoveredSlot;
    const auto newSlot = worldObjectSubCellOffset(object);
    object.hasRenderPosition = true;
    object.renderMoveActive = false;
    object.renderX = recoveredBodyX - newSlot.x;
    object.renderY = recoveredBodyY - newSlot.y;
    object.renderLevel = recoveredLevel;
    object.renderOnBridge = recoveredOnBridge;
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    invalidateRuntimeObjectProjectionCache(state);
    state.repaintRequested = true;
    return true;
}
