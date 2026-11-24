#pragma once
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"
#include <map>
struct WorldObjectInstance;

struct TemporalTargetState {
    int targetObjectId = -1;
    int requiredProgress = 0;
    bool frozen = false;
    std::map<int, int> progressByAttacker;
    int lastAdvancedTick = -1;
};


// Borrowed queries retain the original world/rules helpers and their lifetimes.
class RuntimeTemporalStateObjectQueries {
public:
    virtual ~RuntimeTemporalStateObjectQueries() = default;
    virtual WorldObjectInstance* findObject(int objectId) const = 0;
};
class RuntimeTemporalStateHitQueries : public RuntimeTemporalStateObjectQueries {
public:
    virtual bool combatSourceAvailable(const WorldObjectInstance* source) const = 0;
    virtual bool targetImmune(const WorldObjectInstance& target) const = 0;
};
class RuntimeTemporalState {
public:
    void reset();
    void swap(RuntimeTemporalState& other) noexcept;
    bool empty() const;
    const std::map<int, TemporalTargetState>& records() const;
    bool applyTemporalHit(const RuntimeTemporalStateHitQueries& queries,
        int attackerObjectId, int targetObjectId, int progress, int requiredProgress,
        int tick, const std::function<bool(const std::string&, const std::string&)>& areAllied, const SpecialEventSink& emit,
        bool& completed, std::string& reason, bool allowAlliedTarget = false);
    bool isTemporalFrozen(int objectId) const;
    bool hasTemporalAttacker(int attackerObjectId, int targetObjectId) const;
    bool beginTemporalTick(int targetObjectId, int tick);
    bool hasTemporalAuthorityForObject(int objectId) const;
    void clearTemporalAttacker(int attackerObjectId, int targetObjectId, int tick,
        const SpecialEventSink& emit, const std::string& reason);
    void clearTemporalTarget(int targetObjectId, int tick,
        const SpecialEventSink& emit, const std::string& reason);
    void onObjectRemoved(int objectId, int tick, const SpecialEventSink& emit,
        const std::string& reason);
    void reconcile(const RuntimeTemporalStateObjectQueries& queries);
private:
    // Only the original aggregate's uncommitted whole-codec load transaction
    // may populate this map; normal callers use the original typed operations.
    friend class RuntimeSpecialMechanismState;
    std::map<int, TemporalTargetState> byTarget_;
};
