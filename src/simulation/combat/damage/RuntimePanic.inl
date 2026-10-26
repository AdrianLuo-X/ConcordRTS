// Damage records a pending reaction; order replacement remains at the
// simulation boundary where advanceRuntimePanics consumes that reaction.
bool runtimePanicEligible(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    if (!object.alive || object.health <= 0 || object.airborne) return false;
    switch (object.kind) {
    case WorldObjectKind::Infantry:
    case WorldObjectKind::Unit:
        break;
    default:
        return false;
    }
    if (runtimeObjectHasDeployedState(state, object)
        || runtimeMissionIsDeploying(object) || runtimeMissionIsUndeploying(object)) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    return type != nullptr && type->speed > 0;
}

bool beginRuntimePanic(RuntimeWindowState& state, WorldObjectInstance& object,
    bool permanent, int threatX = -1, int threatY = -1) {
    if (!runtimePanicEligible(state, object)) return false;
    if (!permanent) {
        const auto& type = *findRuntimeUnitType(state.rules, object);
        const bool ignoresDamageReaction = type.fearless
            || runtimeVeterancyHasAbility(state, object, VeteranAbility::Fearless);
        if (!type.fraidycat || ignoresDamageReaction) return false;
    }

    RuntimePanicState reaction;
    const auto existing = state.combatDetails.panics.find(object.instanceId);
    if (existing != state.combatDetails.panics.end()) {
        reaction = existing->second;
    } else {
        reaction.owner = object.owner;
        reaction.sourceType = object.type;
        reaction.nextChooseTick = state.tick;
    }
    // Refresh the finite duration without downgrading a script's permanent
    // reaction or resetting its pending-start flag and deterministic walk cursor.
    reaction.permanent |= permanent;
    reaction.ticksRemaining = 5 * std::clamp(state.config.fixedTickHz, 1, 1000);
    reaction.lastTick = state.tick;
    reaction.threatX = threatX;
    reaction.threatY = threatY;
    state.combatDetails.panics.insert_or_assign(object.instanceId, std::move(reaction));
    object.prone = false;
    object.proneTicksRemaining = 0;
    return true;
}

enum class RuntimePanicClockStep { Suspended, Active, Expired };

RuntimePanicClockStep advanceRuntimePanicClock(
    RuntimePanicState& panic, int tick, bool suspended
) {
    const auto elapsed = std::max<std::int64_t>(0,
        static_cast<std::int64_t>(tick) - panic.lastTick);
    panic.lastTick = tick;
    if (suspended) return RuntimePanicClockStep::Suspended;
    if (panic.permanent) return RuntimePanicClockStep::Active;
    panic.ticksRemaining = static_cast<int>(std::max<std::int64_t>(0,
        static_cast<std::int64_t>(panic.ticksRemaining) - elapsed));
    return panic.ticksRemaining == 0
        ? RuntimePanicClockStep::Expired : RuntimePanicClockStep::Active;
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
        const bool suspended = runtimeComputerAiPausedForOwner(state, object->owner)
            || !runtimeObjectHasOperationalPower(state, *object)
            || runtimeObjectDisabledByParasite(state, id)
            || (state.specialMechanismsInitialized
                && (state.specialMechanisms.isTemporalFrozen(id)
                    || state.specialMechanisms.isChronoMovementLocked(id, state.tick)));
        const auto clockStep = advanceRuntimePanicClock(panic, state.tick, suspended);
        if (clockStep == RuntimePanicClockStep::Suspended) {
            state.combatDetails.panics[id] = std::move(panic);
            continue;
        }
        if (clockStep == RuntimePanicClockStep::Expired) {
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
