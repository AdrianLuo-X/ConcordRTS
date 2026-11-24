#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteValues.h"
#include "world/values/RuntimeCellPoint.h"
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
struct WorldObjectInstance;
namespace concord::simulation::abilities {
// Synchronous borrows keep the original lookup, placement and owner queries.
class RuntimeParasiteActionQueries {
public:
    virtual ~RuntimeParasiteActionQueries() = default;
    virtual const WorldObjectInstance* objectIncludingInactive(int id) const = 0;
    virtual std::optional<RuntimeCellPoint> placementCell(
        const WorldObjectInstance& source, const SpecialCell& requested,
        const std::unordered_set<int>* reserved, int terminatingHostId) const = 0;
    virtual int cellKey(int x, int y) const = 0;
    virtual std::string canonicalOwner(const std::string& owner) const = 0;
    virtual bool exclusiveAuthority(const WorldObjectInstance& object,
        bool asHost = false) const = 0;
};
std::vector<SpecialMechanismAction> prepareRuntimeParasiteActions(
    const std::vector<SpecialMechanismAction>& actions,
    const RuntimeParasiteActionQueries& queries);
bool validateRuntimeParasiteActionBatch(
    const std::vector<SpecialMechanismAction>& actions,
    const std::unordered_set<int>& pendingObjectExitIds,
    const RuntimeParasiteActionQueries& queries);
}
