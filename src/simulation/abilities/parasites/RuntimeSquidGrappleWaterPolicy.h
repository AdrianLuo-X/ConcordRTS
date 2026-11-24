#pragma once
#include "simulation/effects/RuntimeImpactEffectValues.h"
#include <map>
#include <string>
#include <vector>
class RulesDatabase;
class ArtDatabase;
struct WorldObjectInstance;
struct UnitType;
struct ParasiteLinkState;
namespace concord::runtime::session_detail {
struct RuntimeProjectileEffect;
struct RuntimeProjectileTargetPosition;
}
namespace concord::simulation::abilities {
using concord::runtime::session_detail::RuntimeImpactEffect;
using concord::runtime::session_detail::RuntimeProjectileEffect;
using concord::runtime::session_detail::RuntimeProjectileTargetPosition;
// Live canonical queries and the existing complete impact effects only.
class RuntimeSquidWaterEffects {
public:
    virtual ~RuntimeSquidWaterEffects() = default;
    virtual const WorldObjectInstance* activeObject(int objectId) const = 0;
    virtual const UnitType* unitType(const RulesDatabase&, const WorldObjectInstance&) const = 0;
    virtual void spawnImpact(const RuntimeCellPoint&, int level, bool death,
        const std::string& animId, const std::string& warheadId, int damage,
        double cellSpread, bool bright, const std::string& particleId,
        const RuntimeProjectileEffect*, const RuntimeProjectileTargetPosition*) = 0;
    virtual std::vector<RuntimeImpactEffect>& impacts() noexcept = 0;
    virtual void initializeTimeline(RuntimeImpactEffect&, bool fullSequence) = 0;
};
bool advanceRuntimeSquidGrappleWater(const std::map<int, ParasiteLinkState>& links,
    const int& tick, const int& fixedTickHz, const RulesDatabase& rules,
    const ArtDatabase& art, RuntimeSquidWaterEffects& effects);
}
