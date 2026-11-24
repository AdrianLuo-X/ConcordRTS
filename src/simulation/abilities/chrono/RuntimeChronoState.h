#pragma once
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"
#include <map>
struct WorldObjectInstance;
struct UnitType;
struct SpecialMechanismRules;
using SpecialCellPredicate = std::function<bool(int, int, int)>;
enum class ChronoPhase {
    None,
    WarpOut,
    InTransit,
    WarpIn,
    Cooldown
};

struct ChronoMoveState {
    int objectId = -1;
    ChronoPhase phase = ChronoPhase::None;
    SpecialCell source;
    SpecialCell destination;
    int sourceLevel = 0;
    int destinationLevel = 0;
    int startTick = 0;
    int completeTick = 0;
    int cooldownUntilTick = 0;
};


// These borrowed lookups keep the original world and rules helpers with their owner.
// The component owns only Chrono records; callers retain world/rules lifetimes.
class RuntimeChronoStateObjectQueries {
public:
    virtual ~RuntimeChronoStateObjectQueries() = default;
    virtual WorldObjectInstance* findObject(int objectId) const = 0;
};
class RuntimeChronoStateRequestQueries : public RuntimeChronoStateObjectQueries {
public:
    virtual const UnitType* unitType(const WorldObjectInstance& object) const = 0;
};
class RuntimeChronoStateTickQueries : public RuntimeChronoStateObjectQueries {
public:
    virtual bool combatSourceAvailable(const WorldObjectInstance* source) const = 0;
};
class RuntimeChronoState {
public:
    void reset();
    void swap(RuntimeChronoState& other) noexcept;
    void erase(int objectId);
    bool empty() const;
    const std::map<int, ChronoMoveState>& records() const;
    void beginChronosphereRecovery(WorldObjectInstance& object, int tick, int fixedTickHz,
        const SpecialMechanismRules& chronoRules);
    void beginChronoReinforcementRecovery(WorldObjectInstance& object, int tick, int fixedTickHz,
        const SpecialMechanismRules& chronoRules);
    bool requestChronoMove(const RuntimeChronoStateRequestQueries& queries,
        const SpecialMechanismRules& chronoRules, int objectId, SpecialCell destination,
        int destinationLevel, int tick, const SpecialCellPredicate& cellIsLegal,
        const SpecialCellPredicate& cellIsOccupied, const SpecialEventSink& emit,
        std::string& reason, int fixedTickHz);
    bool tickChronoMoves(const RuntimeChronoStateTickQueries& queries, int tick,
        const SpecialCellPredicate& cellIsLegal, const SpecialCellPredicate& cellIsOccupied,
        const SpecialEventSink& emit);
    bool isChronoInTransit(int objectId) const;
    bool isChronoMovementLocked(int objectId, int tick) const;
    bool cancelChronoMoveInTransit(int objectId);
    const ChronoMoveState* chronoMoveState(int objectId) const;
private:
    // The existing aggregate may populate records only inside its uncommitted
    // whole-SpecialMechanisms codec transaction, as in RuntimeTimedBombState.
    friend class RuntimeSpecialMechanismState;
    void beginChronoArrivalRecovery(WorldObjectInstance& object, int tick,
        int fixedTickHz, int delayFrames);
    std::map<int, ChronoMoveState> byObject_;
};
