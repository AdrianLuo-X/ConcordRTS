// Teleporting infantry retain the ordinary three-subcell capacity. The
// caller separately validates terrain and whole-cell blockers at both ends.
bool runtimeChronoCellIsOccupied(
    const RuntimeWindowState& state, const std::unordered_map<int,int>& occupied,
    int x, int y, int objectId
) {
    const auto* object=findObjectById(state,objectId);
    if(object && runtimeInfantryUsesGroundSubcells(*object))
        return runtimeAvailableInfantrySubcells(state,*object,{x,y}).empty();
    const auto found=occupied.find(runtimeCellKey(x,y));
    return found!=occupied.end() && found->second!=objectId;
}

// Warp effects outlive the one-tick displacement. Recovery belongs to the
// special mechanism state, so loading or issuing another order cannot skip it.
void spawnRuntimeChronoWarpEffect(
    RuntimeWindowState& state, const SpecialMechanismEvent& event, bool playSound
) {
    WorldObjectInstance* object = findMutableObjectById(state, event.objectId);
    if (object == nullptr || !object->alive) return;
    const bool reinforcement = event.kind == "ChronoReinforcementIn";
    const bool arriving = reinforcement || event.kind == "ChronoWarpIn";
    const auto boarding = arriving && !reinforcement
        ? runtimeChronoTransportDestinationAdmission(state, *object, runtimeCellPointForObject(*object))
        : std::optional<bool>{};
    const auto pendingHost = state.pendingTransportTargets.find(object->instanceId);
    const auto* boardingHost = boarding && *boarding && pendingHost != state.pendingTransportTargets.end()
        ? findObjectById(state, pendingHost->second) : nullptr;
    if (arriving) {
        // The standalone mechanism uses a packed 512-wide cell identifier;
        // runtime occupancy and saves use the canonical runtime cell key.
        object->cell = runtimeCellKey(object->x, object->y);
        object->onBridge = runtimeCellIsBridge(state, object->x, object->y);
        if(runtimeInfantryUsesGroundSubcells(*object)) {
            releaseRuntimeInfantrySubcellReservation(state,object->instanceId);
            if (boardingHost) object->subCell = -1;
            else assignRuntimeInfantrySubcellAtCurrentLocation(state,*object);
        }
        clearRuntimeRenderPosition(*object);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        invalidateRuntimeObjectProjectionCache(state);
    }
    const auto& rules = state.rules.getSpecialMechanismRules();
    RuntimeImpactEffect effect;
    effect.attackerObjectId = object->instanceId;
    effect.targetObjectId = object->instanceId;
    effect.owner = object->owner;
    effect.cell = {object->x, object->y};
    effect.level = object->level;
    effect.hasWorldPosition = true;
    const auto offset = worldObjectSubCellOffset(*object);
    effect.worldX = (object->hasRenderPosition ? object->renderX : object->x) + offset.x;
    effect.worldY = (object->hasRenderPosition ? object->renderY : object->y) + offset.y;
    effect.worldLevel = object->hasRenderPosition ? object->renderLevel : object->level;
    if (boardingHost) {
        const auto hull = runtimeTransportMotionBody(*boardingHost);
        effect.worldX = hull.x; effect.worldY = hull.y; effect.worldLevel = hull.level;
    }
    effect.facing = object->facing;
    effect.lifecycleState = reinforcement ? "ChronoReinforcementIn"
        : arriving ? "ChronoWarpIn" : "ChronoWarpOut";
    effect.renderIntent = "chronoWarp";
    // Retail TeleportLocomotionClass uses Rules::WarpOut at BOTH ends.
    // WarpIn is a separate, red animation used by chrono reinforcements.
    effect.animId = reinforcement ? rules.warpInAnim : rules.warpOutAnim;
    if (!effect.animId.empty() && toLowerAsciiLocal(effect.animId) != "none") {
        initializeRuntimeImpactAnimationTimeline(state, effect, true);
        state.impactEffects.push_back(std::move(effect));
    }
    if (playSound) {
        const UnitType* unit = findRuntimeUnitType(state.rules, *object);
        std::string sound = unit == nullptr ? std::string()
            : (arriving ? unit->chronoInSound : unit->chronoOutSound);
        if (trimAsciiLocal(sound).empty()) sound = arriving ? rules.chronoInSound : rules.chronoOutSound;
        if (!trimAsciiLocal(sound).empty()) emitRuntimeSound(state, "sound:" + trimAsciiLocal(sound), runtimeSpatialAudioPositionForObject(*object));
    }
}

bool holdRuntimeChronoRecovery(
    RuntimeWindowState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior
) {
    if (!state.specialMechanismsInitialized
        || !state.specialMechanisms.isChronoMovementLocked(object.instanceId, state.tick)) return false;
    clearRuntimeRenderPosition(object);
    behavior.state = RuntimeBehaviorState::Cooldown;
    if (!state.specialMechanisms.isChronoInTransit(object.instanceId)) object.mission = "ChronoFrozen";
    return true;
}

// Shared by queued movement and an attack's approach path. A teleporter must
// never fall back to walking when the destination or locomotor is unavailable.
std::optional<bool> advanceRuntimeTeleporterMove(
    RuntimeWindowState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, RuntimeCellPoint destination
) {
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    // Harvesters manage their drive-to-ore / blink-to-refinery cycle separately.
    if (unit == nullptr || !unit->teleporter || unit->harvester) return std::nullopt;
    const auto transportAdmission = runtimeChronoTransportDestinationAdmission(state, object, destination);
    if (transportAdmission && !*transportAdmission) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    if (runtimeCellPointForObject(object) == destination && !transportAdmission) return std::nullopt;
    const auto generatedTerrain = state.terrainByCell.empty()
        ? buildRuntimeTerrainLookup(state.world) : std::unordered_map<int, const TerrainCell*>{};
    const auto& terrain = state.terrainByCell.empty() ? generatedTerrain : state.terrainByCell;
    const auto occupied = buildRuntimeOccupancyLookup(state);
    const SpecialCellPredicate legal = [&](int x, int y, int ignoredId) {
        if (const auto entry = runtimeChronoTransportDestinationAdmission(state, object, {x, y})) return *entry;
        return runtimeCellIsPassable(state, terrain, occupied, {x, y}, object, ignoredId);
    };
    const SpecialCellPredicate blocked = [&](int x, int y, int ignoredId) {
        if (const auto entry = runtimeChronoTransportDestinationAdmission(state, object, {x, y})) return !*entry;
        return runtimeChronoCellIsOccupied(state,occupied,x,y,ignoredId);
    };
    std::string reason;
    const auto pendingHost = state.pendingTransportTargets.find(object.instanceId);
    const auto* host = transportAdmission && pendingHost != state.pendingTransportTargets.end()
        ? findObjectById(state, pendingHost->second) : nullptr;
    const int destinationLevel = host ? host->level : terrainLevelAtRuntimeCell(state, destination.x, destination.y);
    if (!requestRuntimeChronoMoveWithParasite(state, object.instanceId,
            {destination.x, destination.y}, destinationLevel,
            legal, blocked, reason)) {
        behavior.state = RuntimeBehaviorState::Blocked;
        return false;
    }
    behavior.path.clear();
    behavior.pathRequestPending = false;
    behavior.moveCooldownTicks = 0;
    behavior.renderMoveTicksRemaining = 0;
    behavior.blockedTicks = 0;
    behavior.state = RuntimeBehaviorState::Cooldown;
    releaseRuntimeInfantrySubcellReservation(state, object.instanceId);
    clearRuntimeRenderPosition(object);
    return true;
}
