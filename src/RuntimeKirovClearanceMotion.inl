bool runtimeJumpjetUsesKirovClearance(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    return object.alive && object.airborne
        && toUpperAsciiLocal(object.type) == "ZEP" && object.factoryExitHostId < 0
        && runtimeJumpjetProfile(state, object);
}

std::int64_t runtimeKirovClearanceTargetQ16(RuntimeWindowState& state,
    const WorldObjectInstance& object, double directionX, double directionY) {
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = state.artLoaded ? &state.art : nullptr;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    const auto* unit = runtimeJumpjetProfile(state, object);
    const double approachSpeed = unit ? unit->jumpjetSpeed * std::clamp(
        runtimeVeterancyAbilityMultiplier(state, object, VeteranAbility::Faster), 0.01, 10.0) : 0.0;
    const double target = runtimeKirovClearanceTargetLevel(state.world, object,
        context, state.renderCache, directionX, directionY, approachSpeed);
    return std::isfinite(target)
        ? std::llround(std::clamp(target, 0.0, 64.0) * runtimeJumpjetCellQ16) : 0;
}

void initializeRuntimeKirovClearanceMotion(RuntimeWindowState& state,
    const WorldObjectInstance& object, RuntimeJumpjetFlight& flight) {
    if (!runtimeJumpjetUsesKirovClearance(state, object)) {
        flight.clearanceLift = 0;
        flight.clearanceTick = -1;
        return;
    }
    // Legacy and newly created flights begin at their old current-position
    // roof pose. Forecasting belongs only to later simulation ticks.
    flight.clearanceLift = runtimeKirovClearanceTargetQ16(state, object, 0.0, 0.0);
    flight.clearanceTick = state.tick;
}

void advanceRuntimeKirovClearanceMotion(RuntimeWindowState& state,
    const WorldObjectInstance& object, RuntimeJumpjetFlight& flight, const UnitType& unit) {
    if (!runtimeJumpjetUsesKirovClearance(state, object)) {
        flight.clearanceLift = 0;
        flight.clearanceTick = -1;
        return;
    }
    if (flight.clearanceTick < 0) {
        initializeRuntimeKirovClearanceMotion(state, object, flight);
        return;
    }
    if (flight.clearanceTick == state.tick) return;
    double directionX = 0.0, directionY = 0.0;
    if (flight.active && !flight.stopped) {
        directionX = flight.toX - flight.fromX;
        directionY = flight.toY - flight.fromY;
    } else if (!flight.stopped) {
        const auto behavior = state.behaviors.find(object.instanceId);
        if (behavior != state.behaviors.end()
            && behavior->second.state != RuntimeBehaviorState::Idle
            && behavior->second.state != RuntimeBehaviorState::Complete) {
            RuntimeCellPoint destination{-1, -1};
            const auto& intent = behavior->second;
            // Ordinary saves rebuild future path cells only after the saved
            // segment completes. Forecast from persisted intent in every
            // branch, including the original with its warm path cache.
            if (intent.kind == RuntimeBehaviorKind::Move) {
                destination = flight.targetX >= 0 && flight.targetY >= 0
                    ? RuntimeCellPoint{flight.targetX, flight.targetY} : intent.moveTarget;
            } else if (intent.kind == RuntimeBehaviorKind::Attack) {
                const auto* targetObject = findObjectById(state, intent.targetObjectId);
                if (targetObject && targetObject->alive)
                    destination = runtimeCellPointForObject(*targetObject);
                else if (intent.attackGround || intent.attackBridge)
                    destination = intent.attackBridgeCell;
            }
            if (destination.x >= 0 && destination.y >= 0
                && destination.x < 512 && destination.y < 512) {
                directionX = destination.x - object.renderX;
                directionY = destination.y - object.renderY;
            }
        }
    }
    const double length = std::hypot(directionX, directionY);
    if (length > 0.0) { directionX /= length; directionY /= length; }
    const auto target = runtimeKirovClearanceTargetQ16(state, object, directionX, directionY);
    const double rate = std::clamp(target > flight.clearanceLift
        ? unit.jumpjetClimb : unit.jumpjetCrash, 0.0, 10000.0);
    const auto delta = std::max<std::int64_t>(0, std::llround(rate * runtimeJumpjetQ16 * 15.0
        / std::max(1, state.config.fixedTickHz)));
    flight.clearanceLift += std::clamp(target - flight.clearanceLift, -delta, delta);
    flight.clearanceTick = state.tick;
}
