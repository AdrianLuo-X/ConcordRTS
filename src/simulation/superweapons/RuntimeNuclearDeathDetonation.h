#pragma once
#include "simulation/projectiles/RuntimeProjectileValues.h"
#include <cstddef>
#include <string>
struct WorldObjectInstance;
namespace concord::simulation::superweapons {
using RuntimeProjectileEffect=concord::runtime::session_detail::RuntimeProjectileEffect;
using RuntimeProjectileTargetPosition=concord::runtime::session_detail::RuntimeProjectileTargetPosition;
using RuntimeProjectileLifecycleState=concord::runtime::session_detail::RuntimeProjectileLifecycleState;
// Synchronous borrows from the existing predicate, position and lifecycle owners.
class RuntimeNuclearDeathQueries {
public:
    virtual ~RuntimeNuclearDeathQueries()=default;
    virtual bool isNuclearDeath(const RuntimeProjectileEffect&) const=0;
    virtual RuntimeProjectileTargetPosition targetPosition(const WorldObjectInstance&) const=0;
    virtual std::string lifecycleName(RuntimeProjectileLifecycleState) const=0;
    virtual std::size_t projectileCount() const=0;
};
class RuntimeNuclearDeathEffects {
public:
    virtual ~RuntimeNuclearDeathEffects()=default;
    virtual void recordReplay(const RuntimeProjectileEffect&,const std::string&)=0;
    virtual bool applyImpact(const RuntimeProjectileEffect&)=0;
};
// Tick and summary are live references to their original fields; no extra state.
bool detonateRuntimeNuclearDeathWeapon(const int& tick,int& projectilesInFlight,
    const RuntimeNuclearDeathQueries&,RuntimeNuclearDeathEffects&,
    RuntimeProjectileEffect&,const WorldObjectInstance&);
}
