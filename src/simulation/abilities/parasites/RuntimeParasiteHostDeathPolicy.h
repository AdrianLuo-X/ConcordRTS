#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteValues.h"
#include "world/values/RuntimeCellPoint.h"
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
struct WorldObjectInstance;
namespace concord::simulation::abilities {
class RuntimeParasiteHostDeathQueries {
public:
    virtual ~RuntimeParasiteHostDeathQueries() = default;
    virtual const WorldObjectInstance* objectIncludingInactive(int id) const = 0;
    virtual bool damageIsTypeImmune(const WorldObjectInstance& target, int sourceId, int damage) const = 0;
    virtual int authoredDamage(const WorldObjectInstance& target, int damage,
        const std::string& warhead, double distance, double modifier) const = 0;
    virtual RuntimeCellPoint objectCell(const WorldObjectInstance& object) const = 0;
    virtual std::optional<RuntimeCellPoint> placementCell(const WorldObjectInstance& source,
        const SpecialCell& requested, const std::unordered_set<int>* reserved, int terminatingHostId) const = 0;
};
class RuntimeParasiteHostDeathCandidate {
public:
    virtual ~RuntimeParasiteHostDeathCandidate() = default;
    virtual const std::map<int, ParasiteLinkState>& links() const = 0;
    virtual ParasiteOperationResult handle(const ParasiteHostEventRequest& request) = 0;
};
class RuntimeParasiteHostDeathRejection {
public:
    virtual ~RuntimeParasiteHostDeathRejection() = default;
    virtual void rejectAtomically() = 0;
};
bool collectRuntimeParasiteHostDeathActions(
    const std::vector<SpecialMechanismAction>& prepared, const int& tick,
    const RuntimeParasiteHostDeathQueries& queries, RuntimeParasiteHostDeathCandidate& candidate,
    std::vector<SpecialMechanismAction>& deathActions, bool& hasHostDeath,
    RuntimeParasiteHostDeathRejection& rejection);
}
