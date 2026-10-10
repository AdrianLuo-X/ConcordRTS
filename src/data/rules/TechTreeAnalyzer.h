#pragma once

#include "data/rules/RulesDatabase.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

struct TechTreeNode {
    std::string kind;
    std::string id;
    std::string owner;
    std::string requiredHouses;
    std::string forbiddenHouses;
    std::string image;
    std::string primaryWeapon;
    std::string secondaryWeapon;
    std::string elitePrimaryWeapon;
    std::string eliteSecondaryWeapon;
    std::string deploysInto;
    std::string undeploysInto;
    std::string deployWeapon;
    std::string eliteDeployWeapon;
    std::string deployWeaponSource;
    std::string eliteDeployWeaponSource;
    std::string deployedSequence;
    std::string deployedFireSequence;
    std::string deployedIdleSequence;
    std::string undeploySequence;
    std::string deploySound;
    std::string undeploySound;
    std::string deployingAnim;
    std::string voiceDeploy;
    std::string occupyWeapon;
    std::string eliteOccupyWeapon;
    std::string pip;
    std::string pipScale;
    std::string occupyPip;
    std::string side;
    int aiBasePlanningSide = -1;
    std::string prerequisite;
    std::string prerequisiteOverride;
    std::string requiredFactory;
    std::string producesFactory;
    int techLevel = 0;
    int cost = 0;
    int strength = 0;
    int power = 0;
    double deployTime = 0.0;
    int undeployDelay = -1;
    int openTransportWeapon = -1;
    bool deployer = false;
    bool deployFire = false;
    bool deployToFire = false;
    bool isSimpleDeployer = false;
    bool deployToLand = false;
    bool deployedCrushable = false;
    bool noMovingFire = false;
    bool engineer = false;
    bool bridgeRepair = false;
    bool repairable = true;
    bool clickRepairable = true;
    bool capturable = true;
    bool needsEngineer = false;
    bool bridgeRepairHut = false;
    bool occupier = false;
    bool canBeOccupied = false;
    bool canOccupyFire = false;
    bool distributedFire = false;
    bool powered = false;
    bool pipsDrawForAll = false;
    bool ownerAllowed = false;
    bool requiredHousesAllowed = true;
    bool forbiddenHousesAllowed = true;
    bool planningSideAllowed = true;
    bool techAllowed = false;
    bool hasPrerequisite = false;
    bool buildableByRules = false;
    std::string blockedReason;
    ObjectCapability capability;
    WeaponSet weaponSet;
    DeployProfile deployProfile;
    OccupyProfile occupyProfile;
    EngineerProfile engineerProfile;
    TransportProfile transportProfile;
    BuildingCapability buildingCapability;
    UiProfile uiProfile;
};

struct CapabilityReportStats {
    std::size_t reportNodeCount = 0;
    std::size_t objectProfileCount = 0;
    std::size_t objectCapabilityCount = 0;
    std::size_t weaponSetCount = 0;
    std::size_t deployProfileCount = 0;
    std::size_t uiProfileCount = 0;
    std::size_t weaponProfileCount = 0;
    std::size_t projectileProfileCount = 0;
    std::size_t warheadProfileCount = 0;
    std::size_t indexedObjectCount = 0;
    std::size_t indexedWeaponCount = 0;
    std::size_t indexedProjectileCount = 0;
    std::size_t indexedWarheadCount = 0;
    std::size_t weaponLookupHits = 0;
    std::size_t weaponLookupMisses = 0;
    std::size_t projectileLookupHits = 0;
    std::size_t projectileLookupMisses = 0;
    std::size_t warheadLookupHits = 0;
    std::size_t warheadLookupMisses = 0;
    std::size_t duplicateReferencesSkipped = 0;
    std::size_t dependencyPasses = 0;
};

// Country visibility is intentionally separate from prerequisite/tech checks.
// The sidebar uses this decision to hide content belonging to another country
// instead of presenting it as a disabled cameo.
struct TechTreeCountryFilterDecision {
    bool visible = true;
    bool ownerAllowed = true;
    bool requiredHousesAllowed = true;
    bool forbiddenHousesAllowed = true;
    std::string blockedReason;
};

struct TechTreeReport {
    std::string house;
    int totalNodes = 0;
    int buildableNodes = 0;
    int blockedNodes = 0;
    int buildingNodes = 0;
    int defenseNodes = 0;
    int infantryNodes = 0;
    int vehicleNodes = 0;
    int aircraftNodes = 0;
    std::vector<TechTreeNode> nodes;
    std::vector<WeaponProfile> weaponProfiles;
    std::vector<ProjectileProfile> projectileProfiles;
    std::vector<WarheadProfile> warheadProfiles;
    CapabilityReportStats capabilityStats;
};

TechTreeReport analyzeTechTree(const RulesDatabase& rules, const std::string& house);
TechTreeCountryFilterDecision evaluateTechTreeCountryFilter(
    const RulesDatabase& rules,
    const std::string& house,
    const std::string& owner,
    const std::string& requiredHouses,
    const std::string& forbiddenHouses
);
void printTechTreeReport(std::ostream& output, const TechTreeReport& report);
