#pragma once
#include <vector>
struct WorldObjectInstance;
struct WeaponType;
struct WarheadType;
struct RuntimeRepairDepotRelation;
struct RuntimeFactoryDeliveryLease;
class RuntimeParasiteStunState;
namespace concord::simulation::abilities {
// Synchronous, side-effect-free observations of the existing owners.
// Returned containers remain borrowed; no snapshot or key-set materialization.
class RuntimeParasiteAdmissionQueries {
public:
    virtual ~RuntimeParasiteAdmissionQueries() = default;
    virtual const std::vector<WorldObjectInstance>& objects() const noexcept = 0;
    virtual const RuntimeParasiteStunState& stunState() const noexcept = 0;
    virtual const int& tick() const noexcept = 0;
    virtual bool hasPendingEngineerCapture(int id) const noexcept = 0;
    virtual bool hasPendingOccupyTarget(int id) const noexcept = 0;
    virtual bool hasPendingTransportTarget(int id) const noexcept = 0;
    virtual bool hasPendingTimedBombIntent(int id) const noexcept = 0;
    virtual bool repairDepotsInitialized() const noexcept = 0;
    virtual const std::vector<RuntimeRepairDepotRelation>& repairRelations() const noexcept = 0;
    virtual const std::vector<RuntimeFactoryDeliveryLease>& factoryLeases() const noexcept = 0;
    virtual bool specialMechanismsInitialized() const noexcept = 0;
    virtual bool isCarrierChild(int id) const noexcept = 0;
    virtual bool isCarrierParent(int id) const noexcept = 0;
};
bool runtimeParasiteObjectIsContained(
    const RuntimeParasiteAdmissionQueries&, const WorldObjectInstance&);
bool runtimeParasiteObjectHasExclusiveAuthority(
    const RuntimeParasiteAdmissionQueries&, const WorldObjectInstance&,
    bool asHost = false);
bool runtimeParasiteWarheadIsExclusive(const WeaponType&, const WarheadType&);
}
