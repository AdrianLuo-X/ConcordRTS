const BuildingType* runtimeTankBunkerType(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    const auto* type = object.kind == WorldObjectKind::Structure
        ? state.rules.findBuildingType(object.type) : nullptr;
    return type != nullptr && type->bunker ? type : nullptr;
}

bool runtimeObjectIsTurningInTankBunker(const RuntimeWindowState& state,
    const WorldObjectInstance& tank) {
    const auto* host = tank.tankBunkerHostId >= 0
        ? findObjectById(state, tank.tankBunkerHostId) : nullptr;
    return host != nullptr && host->tankBunkerOccupantId == tank.instanceId
        && runtimeTankBunkerWaitingForFacing(*host);
}

bool runtimeObjectCanEnterTankBunker(const RuntimeWindowState& state,
    const WorldObjectInstance& tank, const WorldObjectInstance& host, bool reserve) {
    const auto* type = findRuntimeUnitType(state.rules, tank);
    if (runtimeTankBunkerType(state, host) == nullptr || !host.alive || host.health <= 0
        || host.rubble || host.buildAnimTicksRemaining > 0 || host.tankBunkerOccupantId >= 0
        || host.tankBunkerLowering || !tank.alive || tank.health <= 0
        || tank.kind != WorldObjectKind::Unit || type == nullptr || !type->bunkerable
        || tank.airborne || type->naval || tank.tankBunkerHostId >= 0
        || tank.dockedToObjectId >= 0 || tank.deployStateTicksRemaining >= 0
        || runtimeObjectHasDeployedState(state, tank)
        || runtimeObjectSuspendedByMagBeam(state, tank.instanceId)
        || runtimeObjectDisabledByParasite(state, tank.instanceId)
        || !runtimeOwnersAreAllied(state, tank.owner, host.owner)) return false;
    if (reserve) for (const auto& [id, target] : state.pendingTransportTargets) {
        if (id != tank.instanceId && target == host.instanceId) return false;
    }
    return true;
}

std::optional<RuntimeCellPoint> runtimeTankBunkerEntryCell(RuntimeWindowState& state,
    const WorldObjectInstance& tank, const WorldObjectInstance& host) {
    const auto cells = runtimeStructureFootprintCellsForObject(state, host);
    if (cells.empty()) return std::nullopt;
    // Drive into the foundation; never complete entry merely by proximity.
    const auto destination = cells.back();
    const auto origin = runtimeCellPointForObject(tank);
    if (origin == destination || !findRuntimePath(state, tank, origin, destination,
            host.instanceId).empty()) return destination;
    return std::nullopt;
}

void positionRuntimeBunkeredTank(RuntimeWindowState& state, WorldObjectInstance& tank,
    const WorldObjectInstance& host) {
    const auto cells = runtimeStructureFootprintCellsForObject(state, host);
    if (cells.empty()) return;
    double x = 0.0, y = 0.0;
    for (const auto& cell : cells) { x += cell.x; y += cell.y; }
    tank.x = cells.back().x; tank.y = cells.back().y;
    tank.cell = runtimeCellKey(tank.x, tank.y);
    tank.level = host.level; tank.onBridge = host.onBridge;
    tank.hasRenderPosition = true; tank.renderMoveActive = false;
    // The building art's interior is centered on the full foundation extent.
    // Cell indices alone put the hull half a tile above the floor, where it
    // covers the rear wall and misses the authored foreground wall mask.
    tank.renderX = x / cells.size() + 0.5;
    tank.renderY = y / cells.size() + 0.5;
    tank.renderLevel = host.level; tank.renderOnBridge = host.onBridge;
}

bool completeRuntimeTankBunkerEntry(RuntimeWindowState& state, int tankId, int hostId) {
    auto* tank = findMutableObjectById(state, tankId);
    auto* host = findMutableObjectById(state, hostId);
    if (tank == nullptr || host == nullptr
        || !runtimeObjectCanEnterTankBunker(state, *tank, *host, false)) {
        state.pendingTransportTargets.erase(tankId);
        return false;
    }
    const auto cells = runtimeStructureFootprintCellsForObject(state, *host);
    if (cells.empty() || runtimeCellPointForObject(*tank) != cells.back()
        || tank->renderMoveActive) return false;
    host->tankBunkerOccupantId = tankId;
    // Keep the walls lowered until the live tank has finished parking. The
    // existing negative timestamp is the saved/hashed turning stage.
    host->tankBunkerAnimationStartTick = -1;
    host->tankBunkerLowering = false;
    tank->tankBunkerHostId = hostId;
    tank->mission = "Guard";
    positionRuntimeBunkeredTank(state, *tank, *host);
    state.pendingTransportTargets.erase(tankId);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    return true;
}

void releaseRuntimeTankBunker(RuntimeWindowState& state, WorldObjectInstance& host) {
    const bool wallsStarted = host.tankBunkerAnimationStartTick >= 0;
    if (auto* tank = findMutableRuntimeObjectByIdIncludingInactive(state, host.tankBunkerOccupantId)) {
        tank->tankBunkerHostId = -1;
        tank->bodyFacingClock = {};
        tank->turretFacingClock = {};
        if (tank->alive) tank->mission = "Guard";
    }
    host.tankBunkerOccupantId = -1;
    host.tankBunkerAnimationStartTick = wallsStarted ? state.tick : -1;
    host.tankBunkerLowering = wallsStarted;
    if (wallsStarted) emitRuntimeSound(state, "sound:" + runtimeRulesSoundSection(
        state, "BunkerWallsDownSound", "TankBunkerDown"), runtimeSpatialAudioPositionForObject(host));
}

bool unloadRuntimeTankBunker(RuntimeWindowState& state, WorldObjectInstance& object) {
    auto* host = object.tankBunkerHostId >= 0
        ? findMutableObjectById(state, object.tankBunkerHostId) : &object;
    if (host == nullptr || host->tankBunkerOccupantId < 0) return false;
    auto* tank = findMutableObjectById(state, host->tankBunkerOccupantId);
    if (tank == nullptr) { releaseRuntimeTankBunker(state, *host); return false; }
    std::optional<RuntimeCellPoint> exit;
    std::vector<RuntimeCellPoint> path;
    const auto foundation = runtimeStructureFootprintCellsForObject(state, *host);
    std::vector<RuntimeCellPoint> candidates;
    for (const auto& cell : foundation) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        const RuntimeCellPoint candidate{cell.x + dx, cell.y + dy};
        if (runtimeCellDistanceToObjectFootprint(state, candidate, *host) > 0.0
            && std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) candidates.push_back(candidate);
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        const double da = runtimeCellDistance(runtimeCellPointForObject(*tank), a);
        const double db = runtimeCellDistance(runtimeCellPointForObject(*tank), b);
        if (da != db) return da < db;
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    for (const auto& candidate : candidates) {
        if (!findRuntimePassableCellNear(state, *tank, candidate, 0, -1)) continue;
        path = findRuntimePath(state, *tank, runtimeCellPointForObject(*tank), candidate, host->instanceId);
        if (!path.empty()) { exit = candidate; break; }
    }
    if (!exit) return false;
    const int hostId = host->instanceId, tankId = tank->instanceId;
    releaseRuntimeTankBunker(state, *host);
    eraseRuntimeBehavior(state, tankId);
    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Move;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.moveTarget = *exit;
    behavior.pendingAllowedOccupiedObjectId = hostId;
    replaceRuntimeBehaviorPath(behavior, path);
    tank->mission = "Move";
    state.behaviors[tankId] = std::move(behavior);
    return true;
}

bool advanceRuntimeTankBunkers(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeTankBunkers", state.tick);
    bool changed = false;
    for (auto& host : state.world.accessObjects()) {
        if (host.tankBunkerOccupantId >= 0) {
            auto* tank = findMutableRuntimeObjectByIdIncludingInactive(state, host.tankBunkerOccupantId);
            if (!host.alive || host.health <= 0 || tank == nullptr || !tank->alive || tank->health <= 0
                || tank->tankBunkerHostId != host.instanceId) {
                releaseRuntimeTankBunker(state, host); changed = true;
            } else if (!runtimeOwnersAreAllied(state, tank->owner, host.owner)) {
                // Ownership changes must never retain the former side's bonuses.
                if (!unloadRuntimeTankBunker(state, host)) releaseRuntimeTankBunker(state, host);
                changed = true;
            } else {
                positionRuntimeBunkeredTank(state, *tank, host);
                if (runtimeTankBunkerWaitingForFacing(host)) {
                    const int desiredFacing = runtimeFacingFromDelta(0, 1);
                    const int previousBody = tank->facing;
                    const int previousTurret = tank->turretFacing;
                    // Use the same authored ROT and saved angular clocks as
                    // normal movement/aiming; rendering never advances them.
                    tank->facing = runtimeAdvanceBodyFacing(state, *tank,
                        tank->facing >= 0 ? tank->facing : desiredFacing, desiredFacing);
                    bool turretReady = true;
                    if (runtimeObjectHasTurret(state, *tank)) {
                        tank->turretFacing = runtimeAdvanceFacingClock(tank->turretFacingClock,
                            previousTurret >= 0 ? previousTurret
                                : (previousBody >= 0 ? previousBody : desiredFacing),
                            desiredFacing, runtimeTurretTurnStep(state, *tank),
                            state.tick, state.config.fixedTickHz);
                        turretReady = tank->turretFacing == desiredFacing;
                        if (tank->turretFacing != previousTurret)
                            ++state.actionSummary.turretTurnsThisTick;
                        if (tank->turretFacingClock.active())
                            markRuntimeTurretSoundRotation(state, *tank);
                    }
                    changed = changed || previousBody != tank->facing
                        || previousTurret != tank->turretFacing;
                    if (tank->facing == desiredFacing && turretReady) {
                        host.tankBunkerAnimationStartTick = state.tick;
                        emitRuntimeSound(state, "sound:" + runtimeRulesSoundSection(
                            state, "BunkerWallsUpSound", "TankBunkerUp"),
                            runtimeSpatialAudioPositionForObject(host));
                        changed = true;
                    }
                }
            }
        }
        if (host.tankBunkerLowering) {
            const auto* art = runtimeArtTypeForObject(state, host);
            int duration = 0;
            if (art != nullptr) for (const auto& layer : art->specialAnimations) {
                if (!runtimeTankBunkerLayerVisible(host, layer.key)) continue;
                if (const auto* animation = state.art.findArtType(layer.image)) {
                    duration = std::max(duration, runtimeTankBunkerFrameCount(*animation)
                        * runtimeTankBunkerFrameDelay(*animation, state.config.fixedTickHz));
                }
            }
            if (state.tick - host.tankBunkerAnimationStartTick >= duration) {
                host.tankBunkerLowering = false;
                host.tankBunkerAnimationStartTick = -1;
            }
            changed = true;
        }
    }
    return changed;
}
