void spawnRuntimeChronoWarpEffect(
    RuntimeProductionHostState& state, const SpecialMechanismEvent& event, bool playSound) {
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