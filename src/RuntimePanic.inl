// Damage only records the reaction. Orders and paths are replaced here at a
// safe simulation boundary, never while a projectile iterates its victims.
bool runtimePanicEligible(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    return object.alive && object.health > 0 && !object.airborne
        && (object.kind == WorldObjectKind::Infantry || object.kind == WorldObjectKind::Unit)
        && type != nullptr && type->speed > 0 && !runtimeObjectHasDeployedState(state, object)
        && !runtimeMissionIsDeploying(object) && !runtimeMissionIsUndeploying(object);
}

bool beginRuntimePanic(RuntimeWindowState& state, WorldObjectInstance& object,
    bool permanent, int threatX = -1, int threatY = -1) {
    if (!runtimePanicEligible(state, object)) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!permanent && (!type->fraidycat || type->fearless
        || runtimeVeterancyHasAbility(state, object, VeteranAbility::Fearless))) return false;
    auto [it, inserted] = state.combatDetails.panics.try_emplace(object.instanceId);
    auto& panic = it->second;
    if (inserted) {
        panic.owner = object.owner; panic.sourceType = object.type;
        panic.nextChooseTick = state.tick;
    }
    panic.permanent = panic.permanent || permanent;
    // Five simulation seconds is this runtime's finite damage reaction. A new
    // hit refreshes it; it does not consume wall time while the game is paused.
    panic.ticksRemaining = 5 * std::clamp(state.config.fixedTickHz, 1, 1000);
    panic.lastTick = state.tick;
    panic.threatX = threatX; panic.threatY = threatY;
    object.prone = false; object.proneTicksRemaining = 0;
    return true;
}

bool advanceRuntimePanics(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimePanics", state.tick);
    bool changed = false;
    std::vector<int> ids;
    for (const auto& [id, panic] : state.combatDetails.panics) { (void)panic; ids.push_back(id); }
    for (int id : ids) {
        auto found = state.combatDetails.panics.find(id);
        if (found == state.combatDetails.panics.end()) continue;
        RuntimePanicState panic = found->second;
        auto* object = findMutableObjectById(state, id);
        if (object == nullptr || !runtimePanicEligible(state, *object)
            || object->owner != panic.owner || object->type != panic.sourceType) {
            eraseRuntimeBehavior(state, id);
            changed = true;
            continue;
        }
        const int elapsed = std::max(0, state.tick - panic.lastTick);
        panic.lastTick = state.tick;
        if (runtimeComputerAiPausedForOwner(state, object->owner)
            || !runtimeObjectHasOperationalPower(state, *object)
            || runtimeObjectDisabledByParasite(state, id)
            || (state.specialMechanismsInitialized
                && (state.specialMechanisms.isTemporalFrozen(id)
                    || state.specialMechanisms.isChronoMovementLocked(id, state.tick)))) {
            state.combatDetails.panics[id] = std::move(panic);
            continue;
        }
        if (!panic.permanent) panic.ticksRemaining = std::max(0, panic.ticksRemaining - elapsed);
        if (!panic.permanent && panic.ticksRemaining == 0) {
            eraseRuntimeBehavior(state, id);
            object->mission = "Guard";
            changed = true;
            continue;
        }
        if (panic.pendingStart) {
            eraseRuntimeBehavior(state, id);
            cancelRuntimeCombatFire(state, id);
            panic.pendingStart = false;
            changed = true;
        }
        state.combatDetails.panics[id] = panic;
        object->prone = false; object->proneTicksRemaining = 0;
        object->mission = "Panic";
        if (state.behaviors.contains(id) || state.tick < panic.nextChooseTick) continue;

        // A bounded local walk uses the very same terrain, corner, bridge,
        // occupancy and infantry subcell rules as an ordinary Move. Failure
        // retries twice per second, without queuing unbounded path searches.
        const auto occupied = buildRuntimeOccupancyLookup(state, id);
        const RuntimeCellPoint start = runtimeCellPointForObject(*object);
        const std::uint64_t seed = runtimeCombatRandomMix(
            (static_cast<std::uint64_t>(id) << 32U) ^ panic.choiceSerial++);
        std::vector<RuntimeCellPoint> bestPath;
        double bestScore = -std::numeric_limits<double>::infinity();
        int direction = 0;
        for (const auto& neighbor : runtimeNeighborsOf(start)) {
            const int radius = 2 + static_cast<int>((seed >> (direction * 3)) & 3U);
            const RuntimeCellPoint goal{start.x + (neighbor.x - start.x) * radius,
                start.y + (neighbor.y - start.y) * radius};
            auto path = findRuntimeGreedyPathUntimed(state, *object, start, goal, -1, nullptr, 12, &occupied);
            if (!path.empty()) {
                const auto end = path.back();
                const double away = panic.threatX >= 0 && panic.threatY >= 0
                    ? std::hypot(static_cast<double>(end.x - panic.threatX),
                        static_cast<double>(end.y - panic.threatY)) : 0.0;
                const double score = away + static_cast<double>((seed >> (direction * 7)) & 127U) / 32.0;
                if (score > bestScore) { bestScore = score; bestPath = std::move(path); }
            }
            ++direction;
        }
        panic.nextChooseTick = state.tick + std::max(1, state.config.fixedTickHz / 2);
        state.combatDetails.panics[id] = panic;
        if (!bestPath.empty()) {
            RuntimeObjectBehavior move;
            move.kind = RuntimeBehaviorKind::Move;
            move.state = RuntimeBehaviorState::Queued;
            move.moveTarget = bestPath.back();
            replaceRuntimeBehaviorPath(move, std::move(bestPath));
            state.behaviors[id] = std::move(move);
            changed = true;
        }
    }
    return changed;
}

void finishRuntimePanicMovementLeg(RuntimeWindowState& state, int id) {
    const auto found = state.combatDetails.panics.find(id);
    if (found == state.combatDetails.panics.end()) { eraseRuntimeBehavior(state, id); return; }
    const auto panic = found->second;
    eraseRuntimeBehavior(state, id);
    if (auto* object = findMutableObjectById(state, id); object && runtimePanicEligible(state, *object)) {
        state.combatDetails.panics[id] = panic;
        object->mission = "Panic";
    }
}
