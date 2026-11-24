#pragma once
#include "simulation/abilities/chrono/RuntimeTemporalState.h"
struct WorldObjectInstance;
struct RuntimeObjectBehavior;
struct RuntimeCombatFireCursor;
struct UnitType;
struct WeaponType;
struct WarheadType;
namespace concord::simulation::abilities {
// Live borrows preserve the original tick/owner reads around callbacks.
struct RuntimeTemporalAttackInputs {
    const std::map<int, TemporalTargetState>& targets;
    const int& tick;
    const int& fixedTickHz;
    const std::string& economyOwner;
    int& lastUnitLostSoundTick;
};
struct RuntimeTemporalAttackWeaponView {
    const WeaponType* weapon = nullptr;
    const WarheadType* warhead = nullptr;
};
class RuntimeTemporalAttackQueries {
public:
    virtual ~RuntimeTemporalAttackQueries() = default;
    virtual const WorldObjectInstance* target(int objectId) const = 0;
    virtual const WorldObjectInstance* sourceIncludingInactive(int objectId) const = 0;
    virtual const RuntimeObjectBehavior* order(int objectId) const = 0;
    virtual const RuntimeCombatFireCursor* cursor(int objectId) const = 0;
    virtual bool canAct(const WorldObjectInstance& object) const = 0;
    virtual bool deckProtected(const WorldObjectInstance& object) const = 0;
    virtual bool allied(const std::string& a, const std::string& b) const = 0;
    virtual bool validPassenger(const WorldObjectInstance& source, const WorldObjectInstance& host) const = 0;
    virtual const UnitType* unitType(const WorldObjectInstance& source) const = 0;
    // The adapter retains the original complete owned profile until finish.
    virtual RuntimeTemporalAttackWeaponView selectWeapon(bool valid,
        const WorldObjectInstance* source, const WorldObjectInstance* target,
        const WorldObjectInstance* host, bool forceFire) = 0;
    virtual void finishWeaponScope() noexcept = 0;
    virtual WorldObjectInstance firingPose(const WorldObjectInstance& source, const WorldObjectInstance* host) const = 0;
    virtual double openToppedWarpDistance() const = 0;
    virtual double effectiveWeaponRange(const WorldObjectInstance& pose, const WorldObjectInstance& target, const WeaponType& weapon) const = 0;
    virtual double cellDistance(const WorldObjectInstance& host, const WorldObjectInstance& target) const = 0;
    virtual double weaponDistance(const WorldObjectInstance& pose, const WorldObjectInstance& target, const WeaponType& weapon) const = 0;
    virtual bool visible(const std::string& owner, const WorldObjectInstance& target) const = 0;
    virtual bool lineOfSight(const WorldObjectInstance& pose, const WorldObjectInstance& target, const WeaponType& weapon) const = 0;
};
class RuntimeTemporalAttackEffects {
public:
    virtual ~RuntimeTemporalAttackEffects() = default;
    virtual SpecialEventSink eventSink() = 0;
    virtual bool beginTick(int targetId, int tick) = 0;
    virtual void clearAttacker(int attackerId, int targetId, int tick,
        const SpecialEventSink& sink, const std::string& reason) = 0;
    virtual bool applyHit(int attackerId, int targetId, int progress,
        int requiredProgress, int tick, const SpecialEventSink& sink,
        bool& completed, std::string& reason, bool forceFire) = 0;
    virtual void queueTemporalExit(int targetId, int attackerId,
        const WorldObjectInstance& source) = 0;
    virtual bool drainExitQueue() = 0;
    virtual void emitUnitLostSound() = 0;
};
bool advanceRuntimeTemporalAttacks(const RuntimeTemporalAttackInputs& inputs,
    RuntimeTemporalAttackQueries& queries, RuntimeTemporalAttackEffects& effects);
void clearRuntimeTemporalAttack(const bool& initialized, int attackerObjectId,
    const RuntimeObjectBehavior& behavior, const int& tick,
    RuntimeTemporalAttackEffects& effects, const std::string& reason);
}
