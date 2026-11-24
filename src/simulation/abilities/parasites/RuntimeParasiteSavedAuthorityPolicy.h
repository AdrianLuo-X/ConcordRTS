#pragma once
#include <string>
#include <map>
#include "simulation/abilities/parasites/RuntimeParasiteValues.h"
#include <unordered_map>
#include <vector>
class IniFile;
class RulesDatabase;
class RuntimeRepairDepotSystemState;
class RuntimeFactoryDeliveryState;
struct WorldObjectInstance;
struct RuntimeObjectBehavior;
namespace concord::runtime::session_detail { struct RuntimeProjectileEffect; }
struct BuildingType;
struct UnitType;
struct WeaponType;
struct WarheadType;
struct ProjectileType;
struct SpawnedMissileProfile;
namespace concord::simulation::abilities {
using concord::runtime::session_detail::RuntimeProjectileEffect;
// Synchronous codecs, primitive rule lookups and six read-only saved-state queries.
// The saved state stays with its caller; no aggregate mechanism, live session or
// private save schema crosses this boundary. Getters are evaluated at the original sites.
class RuntimeParasiteSavedAuthorityQueries {
public:
    virtual ~RuntimeParasiteSavedAuthorityQueries() = default;
    virtual bool savedIsCarrierChild(int objectId) const = 0;
    virtual bool savedIsCarrierParent(int objectId) const = 0;
    virtual const double& savedConditionRed() const = 0;
    virtual const double& savedConditionYellow() const = 0;
    virtual const std::map<int, ParasitePendingLaunchState>& savedPendingParasiteLaunches() const = 0;
    virtual const std::map<int, ParasiteLinkState>& savedParasiteLinks() const = 0;
    virtual bool parseRuntimeStrictBool(const std::string&, bool&) const = 0;
    virtual bool parseRuntimeSavedBehaviorRecord(const std::vector<std::string>&,
        const WorldObjectInstance&, int saveVersion, int& objectId,
        RuntimeObjectBehavior&) const = 0;
    virtual const BuildingType* findRuntimeBuildingTypeById(
        const RulesDatabase&, const std::string&) const = 0;
    virtual const UnitType* findRuntimeUnitType(
        const RulesDatabase&, const WorldObjectInstance&) const = 0;
    virtual const WeaponType* findRuntimeWeaponType(
        const RulesDatabase&, const std::string&) const = 0;
    virtual const WarheadType* findRuntimeWarheadType(
        const RulesDatabase&, const std::string&) const = 0;
    virtual const ProjectileType* findRuntimeProjectileType(
        const RulesDatabase&, const std::string&) const = 0;
    virtual const SpawnedMissileProfile* runtimeSpawnedMissileProfileForWeapon(
        const RulesDatabase&, const WorldObjectInstance&, const WeaponType&) const = 0;
};
bool validateRuntimeSavedParasiteAuthority(
    const RuntimeParasiteSavedAuthorityQueries& queries,
    const RulesDatabase& rules,
    const int& fixedTickHz,
    const IniFile& ini,
    int saveVersion,
    const std::vector<WorldObjectInstance>& savedObjects,
    const std::vector<RuntimeProjectileEffect>& savedProjectiles,
    const int& savedNextProjectileActorId,
    int savedTick,
    const RuntimeRepairDepotSystemState& savedRepairDepots,
    const RuntimeFactoryDeliveryState& savedFactoryDeliveries,
    std::unordered_map<int, int>& savedStuns,
    std::string& errorMessage);
}
