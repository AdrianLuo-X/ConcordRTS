#include "RuntimeChronoState.h"
#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

void RuntimeChronoState::reset(){byObject_.clear();}
void RuntimeChronoState::swap(RuntimeChronoState& other)noexcept{byObject_.swap(other.byObject_);}
void RuntimeChronoState::erase(int id){byObject_.erase(id);}
bool RuntimeChronoState::empty()const{return byObject_.empty();}
const std::map<int,ChronoMoveState>& RuntimeChronoState::records()const{return byObject_;}

void RuntimeChronoState::beginChronosphereRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz,
    const SpecialMechanismRules& chronoRules
) {
    beginChronoArrivalRecovery(object, tick, fixedTickHz, chronoRules.chronoDelay);
}

void RuntimeChronoState::beginChronoReinforcementRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz,
    const SpecialMechanismRules& chronoRules
) {
    beginChronoArrivalRecovery(object, tick, fixedTickHz, chronoRules.chronoReinforcementDelay);
}

void RuntimeChronoState::beginChronoArrivalRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz, int delayFrames
) {
    if (!object.alive) return;
    const int delay = static_cast<int>(std::clamp(
        std::round(static_cast<double>(std::max(0, delayFrames))
            * std::max(1, fixedTickHz) / 15.0),
        0.0, static_cast<double>(std::numeric_limits<int>::max() / 2)));
    if (delay == 0) {
        byObject_.erase(object.instanceId);
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
    byObject_[object.instanceId] = recovery;
    object.mission = "ChronoFrozen";
}

bool RuntimeChronoState::requestChronoMove(
    const RuntimeChronoStateRequestQueries& queries,
    const SpecialMechanismRules& chronoRules,
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
    WorldObjectInstance* object = queries.findObject(objectId);
    const UnitType* unit = object == nullptr ? nullptr : queries.unitType(*object);
    if (object == nullptr || unit == nullptr || !object->alive
        || !unit->teleporter) {
        reason = "not a teleporter";
        return false;
    }
    if (object->dockedToObjectId >= 0 || !object->transportPassengerIds.empty()) {
        reason = "transport state blocks chrono";
        return false;
    }
    const auto current = byObject_.find(objectId);
    if (current != byObject_.end()
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
    const double variableFrames = chronoRules.chronoTrigger
            && distanceLeptons > std::max(0, chronoRules.chronoRangeMinimum)
        ? std::floor(distanceLeptons / std::max(1, chronoRules.chronoDistanceFactor)) : 0.0;
    const double recoveryFrames = std::max(
        static_cast<double>(chronoRules.chronoMinimumDelay), variableFrames);
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
    byObject_[objectId] = state;
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

bool RuntimeChronoState::tickChronoMoves(
    const RuntimeChronoStateTickQueries& queries,
    int tick,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    const SpecialEventSink& emit
) {
    bool changed = false;
    std::vector<int> completed;
    for (auto& [objectId, state] : byObject_) {
        WorldObjectInstance* object = queries.findObject(objectId);
        // A real passenger retains the absolute recovery deadline without
        // replaying a landing or replacing its InsideTransport mission.
        if (object && !object->alive
            && (state.phase == ChronoPhase::WarpIn || state.phase == ChronoPhase::Cooldown)
            && queries.combatSourceAvailable(object)) {
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
        byObject_.erase(objectId);
    }
    return changed;
}

bool RuntimeChronoState::isChronoInTransit(int objectId) const {
    const auto found = byObject_.find(objectId);
    return found != byObject_.end()
        && (found->second.phase == ChronoPhase::WarpOut
            || found->second.phase == ChronoPhase::InTransit);
}

bool RuntimeChronoState::isChronoMovementLocked(int objectId, int tick) const {
    const auto found = byObject_.find(objectId);
    return found != byObject_.end()
        && (isChronoInTransit(objectId) || tick < found->second.cooldownUntilTick);
}

bool RuntimeChronoState::cancelChronoMoveInTransit(int objectId) {
        const auto found = byObject_.find(objectId);
        if (found == byObject_.end()
            || (found->second.phase != ChronoPhase::WarpOut
                && found->second.phase != ChronoPhase::InTransit)) return false;
        byObject_.erase(found);
        return true;
    }

const ChronoMoveState* RuntimeChronoState::chronoMoveState(int objectId) const {
        const auto found = byObject_.find(objectId);
        return found == byObject_.end() ? nullptr : &found->second;
    }

