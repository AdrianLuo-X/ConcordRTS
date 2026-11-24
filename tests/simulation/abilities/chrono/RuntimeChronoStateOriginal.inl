// Frozen original H02303 algorithms; only the containing class name changes.
struct OriginalChrono {
 SpecialMechanismRules rules_;
 std::map<int,ChronoMoveState> chronoByObject_;
 static WorldObjectInstance* findObject(std::vector<WorldObjectInstance>& objects,int id);
void beginChronosphereRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz
);
void beginChronoReinforcementRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz
);
void beginChronoArrivalRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz, int delayFrames
);
bool requestChronoMove(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int objectId,
    SpecialCell destination,
    int destinationLevel,
    int tick,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    const SpecialEventSink& emit,
    std::string& reason,
    int fixedTickHz
);
bool tickChronoMoves(
    std::vector<WorldObjectInstance>& objects,
    int tick,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    const SpecialEventSink& emit
);
bool isChronoInTransit(int objectId) const;
bool isChronoMovementLocked(int objectId, int tick) const;
bool cancelChronoMoveInTransit(int objectId) {
        const auto found = chronoByObject_.find(objectId);
        if (found == chronoByObject_.end()
            || (found->second.phase != ChronoPhase::WarpOut
                && found->second.phase != ChronoPhase::InTransit)) return false;
        chronoByObject_.erase(found);
        return true;
    }
const ChronoMoveState* chronoMoveState(int objectId) const {
        const auto found = chronoByObject_.find(objectId);
        return found == chronoByObject_.end() ? nullptr : &found->second;
    }
};
void OriginalChrono::beginChronosphereRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz
) {
    beginChronoArrivalRecovery(object, tick, fixedTickHz, rules_.chronoDelay);
}

void OriginalChrono::beginChronoReinforcementRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz
) {
    beginChronoArrivalRecovery(object, tick, fixedTickHz, rules_.chronoReinforcementDelay);
}

void OriginalChrono::beginChronoArrivalRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz, int delayFrames
) {
    if (!object.alive) return;
    const int delay = static_cast<int>(std::clamp(
        std::round(static_cast<double>(std::max(0, delayFrames))
            * std::max(1, fixedTickHz) / 15.0),
        0.0, static_cast<double>(std::numeric_limits<int>::max() / 2)));
    if (delay == 0) {
        chronoByObject_.erase(object.instanceId);
        object.mission = "Guard";
        return;
    }
    ChronoMoveState recovery;
    recovery.objectId = object.instanceId;
    recovery.phase = ChronoPhase::WarpIn;
    recovery.source = recovery.destination = {object.x, object.y};
    recovery.sourceLevel = recovery.destinationLevel = object.level;
    recovery.startTick = recovery.completeTick = tick;
    recovery.cooldownUntilTick = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), static_cast<std::int64_t>(tick) + delay));
    chronoByObject_[object.instanceId] = recovery;
    object.mission = "ChronoFrozen";
}

bool OriginalChrono::requestChronoMove(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int objectId,
    SpecialCell destination,
    int destinationLevel,
    int tick,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    const SpecialEventSink& emit,
    std::string& reason,
    int fixedTickHz
) {
    WorldObjectInstance* object = findObject(objects, objectId);
    const UnitType* unit = object == nullptr ? nullptr : unitTypeFor(rules, *object);
    if (object == nullptr || unit == nullptr || !object->alive
        || !unit->teleporter) {
        reason = "not a teleporter";
        return false;
    }
    if (object->dockedToObjectId >= 0 || !object->transportPassengerIds.empty()) {
        reason = "transport state blocks chrono";
        return false;
    }
    const auto current = chronoByObject_.find(objectId);
    if (current != chronoByObject_.end()
        && (isChronoInTransit(objectId) || tick < current->second.cooldownUntilTick)) {
        reason = "chrono cooldown or transit";
        return false;
    }
    if (cellIsLegal != nullptr
        && !cellIsLegal(destination.x, destination.y, objectId)) {
        reason = "illegal destination";
        return false;
    }
    if (cellIsOccupied != nullptr
        && cellIsOccupied(destination.x, destination.y, objectId)) {
        reason = "destination occupied";
        return false;
    }

    const SpecialCell source{object->x, object->y};
    // This is recovery AFTER arrival, not travel time. RangeMinimum is a
    // threshold for the minimum delay, not a distance to subtract from a trip.
    const double distanceLeptons = std::hypot(
        static_cast<double>(destination.x) - source.x,
        static_cast<double>(destination.y) - source.y) * 256.0;
    const double variableFrames = rules_.chronoTrigger
            && distanceLeptons > std::max(0, rules_.chronoRangeMinimum)
        ? std::floor(distanceLeptons / std::max(1, rules_.chronoDistanceFactor)) : 0.0;
    const double recoveryFrames = std::max(
        static_cast<double>(rules_.chronoMinimumDelay), variableFrames);
    const int delay = static_cast<int>(std::clamp(
        std::round(recoveryFrames * std::max(1, fixedTickHz) / 15.0),
        0.0, static_cast<double>(std::numeric_limits<int>::max() / 2)));

    ChronoMoveState state;
    state.objectId = objectId;
    state.phase = ChronoPhase::WarpOut;
    state.source = source;
    state.destination = destination;
    state.sourceLevel = object->level;
    state.destinationLevel = destinationLevel;
    state.startTick = tick;
    state.completeTick = tick + 1; // A blink takes one simulation tick at any distance.
    state.cooldownUntilTick = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), static_cast<std::int64_t>(state.completeTick) + delay));
    chronoByObject_[objectId] = state;
    object->mission = "ChronoOut";
    reason = "chrono warp queued";
    if (emit != nullptr) {
        emit({
            "ChronoWarpOut",
            tick,
            objectId,
            -1,
            delay,
            source,
            reason
        });
    }
    return true;
}

bool OriginalChrono::tickChronoMoves(
    std::vector<WorldObjectInstance>& objects,
    int tick,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    const SpecialEventSink& emit
) {
    bool changed = false;
    std::vector<int> completed;
    for (auto& [objectId, state] : chronoByObject_) {
        WorldObjectInstance* object = findObject(objects, objectId);
        // A real passenger retains the absolute recovery deadline without
        // replaying a landing or replacing its InsideTransport mission.
        if (object && !object->alive
            && (state.phase == ChronoPhase::WarpIn || state.phase == ChronoPhase::Cooldown)
            && specialCombatSourceAvailable(objects, object)) {
            if (tick >= state.cooldownUntilTick) {
                completed.push_back(objectId);
                changed = true;
            } else if (state.phase != ChronoPhase::Cooldown) {
                state.phase = ChronoPhase::Cooldown;
                changed = true;
            }
            continue;
        }
        if (object == nullptr || !object->alive) {
            completed.push_back(objectId);
            continue;
        }
        if ((state.phase == ChronoPhase::WarpOut || state.phase == ChronoPhase::InTransit)
            && tick >= state.completeTick) {
            state.phase = ChronoPhase::InTransit;
            object->mission = "ChronoInTransit";
            if (cellIsLegal != nullptr
                && !cellIsLegal(
                    state.destination.x,
                    state.destination.y,
                    objectId
                )) {
                object->mission = "Guard";
                if (emit != nullptr) {
                    emit({
                        "ChronoRejectedAtArrival",
                        tick,
                        objectId,
                        -1,
                        0,
                        state.source,
                        "destination became illegal"
                    });
                }
                completed.push_back(objectId);
                changed = true;
                continue;
            }
            if (cellIsOccupied != nullptr
                && cellIsOccupied(
                    state.destination.x,
                    state.destination.y,
                    objectId
                )) {
                object->mission = "Guard";
                if (emit != nullptr) {
                    emit({
                        "ChronoRejectedAtArrival",
                        tick,
                        objectId,
                        -1,
                        0,
                        state.source,
                        "destination became occupied"
                    });
                }
                completed.push_back(objectId);
                changed = true;
                continue;
            }
            object->x = state.destination.x;
            object->y = state.destination.y;
            object->cell = state.destination.x + state.destination.y * 512;
            object->level = state.destinationLevel;
            object->hasRenderPosition = false;
            object->renderMoveActive = false;
            object->renderX = object->x;
            object->renderY = object->y;
            object->renderLevel = object->level;
            object->mission = "ChronoFrozen";
            const int recoveryTicks = std::max(0, state.cooldownUntilTick - state.completeTick);
            state.phase = ChronoPhase::WarpIn;
            state.completeTick = tick; // Retain actual arrival time through recovery/save/load.
            state.cooldownUntilTick = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), static_cast<std::int64_t>(tick) + recoveryTicks));
            if (emit != nullptr) {
                emit({
                    "ChronoWarpIn",
                    tick,
                    objectId,
                    -1,
                    0,
                    state.destination,
                    "destination reached"
                });
            }
            changed = true;
        } else if (state.phase == ChronoPhase::WarpIn || state.phase == ChronoPhase::Cooldown) {
            if (tick >= state.cooldownUntilTick) {
                object->mission = "Guard";
                completed.push_back(objectId);
                if (emit != nullptr) {
                    emit({"ChronoReady", tick, objectId, -1, 0,
                        state.destination, "chrono recovery complete"});
                }
                changed = true;
            } else if (state.phase == ChronoPhase::WarpIn) {
                state.phase = ChronoPhase::Cooldown;
                object->mission = "ChronoFrozen";
                changed = true;
            }
        }
    }
    for (int objectId : completed) {
        chronoByObject_.erase(objectId);
    }
    return changed;
}

bool OriginalChrono::isChronoInTransit(int objectId) const {
    const auto found = chronoByObject_.find(objectId);
    return found != chronoByObject_.end()
        && (found->second.phase == ChronoPhase::WarpOut
            || found->second.phase == ChronoPhase::InTransit);
}

bool OriginalChrono::isChronoMovementLocked(int objectId, int tick) const {
    const auto found = chronoByObject_.find(objectId);
    return found != chronoByObject_.end()
        && (isChronoInTransit(objectId) || tick < found->second.cooldownUntilTick);
}