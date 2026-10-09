#include "TechTreeAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

std::string trimAscii(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    for (char character : value) {
        if (character == ',' || character == ';') {
            const std::string token = trimAscii(current);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            current.clear();
        } else {
            current.push_back(character);
        }
    }

    const std::string token = trimAscii(current);
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

bool equalsAsciiIgnoreCase(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto leftChar = static_cast<unsigned char>(left[index]);
        const auto rightChar = static_cast<unsigned char>(right[index]);
        if (std::tolower(leftChar) != std::tolower(rightChar)) {
            return false;
        }
    }

    return true;
}

bool lessAsciiIgnoreCase(const std::string& left, const std::string& right) {
    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto leftChar = static_cast<unsigned char>(left[index]);
        const auto rightChar = static_cast<unsigned char>(right[index]);
        const int leftLower = std::tolower(leftChar);
        const int rightLower = std::tolower(rightChar);
        if (leftLower != rightLower) {
            return leftLower < rightLower;
        }
    }

    return left.size() < right.size();
}

bool isEmptyListToken(const std::string& value) {
    const std::string normalized = lowerAscii(trimAscii(value));
    return normalized.empty()
        || normalized == "none"
        || normalized == "<none>";
}

bool isTrimmedIntegerToken(const std::string& normalized) {
    if (normalized.empty()) {
        return false;
    }

    std::size_t index = 0;
    if (normalized[index] == '-' || normalized[index] == '+') {
        ++index;
    }

    if (index >= normalized.size()) {
        return false;
    }

    for (; index < normalized.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(normalized[index]))) {
            return false;
        }
    }
    return true;
}

bool containsNormalized(
    const std::vector<std::string>& values,
    const std::string& value
) {
    return std::any_of(
        values.begin(),
        values.end(),
        [&value](const std::string& existing) {
            return equalsAsciiIgnoreCase(existing, value);
        }
    );
}

std::vector<std::string> normalizedTokens(const std::string& value) {
    std::vector<std::string> tokens;
    for (const std::string& token : splitList(value)) {
        if (!containsNormalized(tokens, token)) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::string joinTokens(const std::vector<std::string>& tokens) {
    std::ostringstream output;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            output << ",";
        }
        output << tokens[i];
    }
    return output.str();
}

std::string normalizeListText(const std::string& value) {
    return joinTokens(normalizedTokens(value));
}

const char* yesNo(bool value) {
    return value ? "yes" : "no";
}

std::string dashIfEmpty(const std::string& value) {
    return value.empty() ? "-" : value;
}

std::string canonicalFactory(std::string value) {
    value = trimAscii(value);
    const std::string normalized = lowerAscii(value);
    if (normalized == "buildingtype") {
        return "BuildingType";
    }
    if (normalized == "infantrytype") {
        return "InfantryType";
    }
    if (normalized == "unittype") {
        return "UnitType";
    }
    if (normalized == "aircrafttype") {
        return "AircraftType";
    }
    if (normalized == "vesseltype") {
        return "VesselType";
    }
    return value;
}

bool sideContainsHouse(const SideDefinition& side, const std::string& house) {
    if (equalsAsciiIgnoreCase(side.name, house)) {
        return true;
    }

    return std::any_of(
        side.members.begin(),
        side.members.end(),
        [&house](const std::string& member) {
            return equalsAsciiIgnoreCase(member, house);
        }
    );
}

const SideDefinition* findSideForOwner(
    const RulesDatabase& rules,
    const std::string& owner
) {
    for (const SideDefinition& side : rules.getSides()) {
        if (equalsAsciiIgnoreCase(side.name, owner)
            || sideContainsHouse(side, owner)) {
            return &side;
        }
    }

    return nullptr;
}

std::string fallbackSideForOwner(const std::string& owner) {
    const std::string normalized = lowerAscii(owner);
    if (normalized == "americans"
        || normalized == "british"
        || normalized == "french"
        || normalized == "germans"
        || normalized == "koreans"
        || normalized == "alliance"
        || normalized == "allies"
        || normalized == "allied") {
        return "Allied";
    }

    if (normalized == "russians"
        || normalized == "confederation"
        || normalized == "africans"
        || normalized == "arabs"
        || normalized == "soviet"
        || normalized == "soviets") {
        return "Soviet";
    }

    if (normalized == "yuri"
        || normalized == "yuricountry") {
        return "Yuri";
    }

    return "";
}

int basePlanningSideForHouse(const std::string& house) {
    const std::string side = lowerAscii(fallbackSideForOwner(house));
    if (side == "allied") {
        return 0;
    }
    if (side == "soviet") {
        return 1;
    }
    if (side == "yuri") {
        return 2;
    }
    return -1;
}

std::string sideForOwnerList(
    const RulesDatabase& rules,
    const std::string& ownerList
) {
    std::vector<std::string> sides;
    for (const std::string& owner : normalizedTokens(ownerList)) {
        std::string sideName;
        if (const SideDefinition* side = findSideForOwner(rules, owner)) {
            sideName = side->name;
        } else {
            sideName = fallbackSideForOwner(owner);
        }

        if (!sideName.empty() && !containsNormalized(sides, sideName)) {
            sides.push_back(sideName);
        }
    }

    return joinTokens(sides);
}

bool ownerTokenAllowsHouse(
    const RulesDatabase& rules,
    const std::string& owner,
    const std::string& house
) {
    const std::string normalized = lowerAscii(owner);
    if (normalized == "all" || equalsAsciiIgnoreCase(owner, house)) {
        return true;
    }

    for (const SideDefinition& side : rules.getSides()) {
        if (equalsAsciiIgnoreCase(side.name, owner)
            && sideContainsHouse(side, house)) {
            return true;
        }
        if (equalsAsciiIgnoreCase(side.name, house)
            && sideContainsHouse(side, owner)) {
            return true;
        }
    }

    const std::string houseSide = fallbackSideForOwner(house);
    const std::string ownerSide = fallbackSideForOwner(owner);
    return !houseSide.empty()
        && !ownerSide.empty()
        && equalsAsciiIgnoreCase(houseSide, ownerSide);
}

bool ownerAllowsHouse(
    const RulesDatabase& rules,
    const std::string& ownerList,
    const std::string& house
) {
    if (ownerList.empty() || house.empty()) {
        return true;
    }

    for (const std::string& owner : splitList(ownerList)) {
        if (ownerTokenAllowsHouse(rules, owner, house)) {
            return true;
        }
    }
    return false;
}

bool houseRestrictionTokenMatchesHouse(
    const RulesDatabase& rules,
    const std::string& token,
    const std::string& house
) {
    if (equalsAsciiIgnoreCase(token, "all")
        || equalsAsciiIgnoreCase(token, house)) {
        return true;
    }

    // RequiredHouses/ForbiddenHouses are country filters.  A country token
    // such as French must not widen to every Allied country.  Only an
    // explicitly named side token is expanded to that side's members.
    for (const SideDefinition& side : rules.getSides()) {
        if (equalsAsciiIgnoreCase(side.name, token)) {
            return sideContainsHouse(side, house);
        }
    }

    const std::string normalized = lowerAscii(trimAscii(token));
    const bool explicitSideAlias = normalized == "allied"
        || normalized == "allies"
        || normalized == "soviet"
        || normalized == "soviets"
        || normalized == "yuri";
    if (!explicitSideAlias) {
        return false;
    }
    const std::string tokenSide = fallbackSideForOwner(token);
    const std::string houseSide = fallbackSideForOwner(house);
    return !tokenSide.empty()
        && !houseSide.empty()
        && equalsAsciiIgnoreCase(tokenSide, houseSide);
}

bool houseRestrictionAllowsHouse(
    const RulesDatabase& rules,
    const std::string& houseList,
    const std::string& house
) {
    if (houseList.empty() || house.empty()) {
        return true;
    }

    bool hasEffectiveToken = false;
    for (const std::string& owner : splitList(houseList)) {
        if (isEmptyListToken(owner)) {
            continue;
        }

        hasEffectiveToken = true;
        if (houseRestrictionTokenMatchesHouse(rules, owner, house)) {
            return true;
        }
    }
    return !hasEffectiveToken;
}

bool houseRestrictionForbidsHouse(
    const RulesDatabase& rules,
    const std::string& houseList,
    const std::string& house
) {
    if (houseList.empty() || house.empty()) {
        return false;
    }

    for (const std::string& owner : splitList(houseList)) {
        if (isEmptyListToken(owner)) {
            continue;
        }

        if (houseRestrictionTokenMatchesHouse(rules, owner, house)) {
            return true;
        }
    }
    return false;
}

bool prerequisitePresent(const std::string& prerequisite) {
    const std::string normalized = lowerAscii(trimAscii(prerequisite));
    return normalized.empty()
        || normalized == "none"
        || normalized == "<none>"
        || !splitList(prerequisite).empty();
}

bool prerequisitePresent(
    const std::string& prerequisite,
    const std::string& prerequisiteOverride
) {
    return prerequisitePresent(prerequisite)
        && prerequisitePresent(prerequisiteOverride);
}

void assignDeploySequenceHints(TechTreeNode& node) {
    if (node.deployFire
        || node.deployToFire
        || node.isSimpleDeployer
        || !node.deploysInto.empty()
        || !node.undeploysInto.empty()
        || !node.deployWeapon.empty()
        || !node.eliteDeployWeapon.empty()) {
        node.deployedSequence = "Deployed";
        node.deployedFireSequence = "DeployedFire";
        node.deployedIdleSequence = "DeployedIdle";
        node.undeploySequence = "Undeploy";
    }
}

struct ReferenceSet {
    std::vector<std::string> values;
    std::unordered_set<std::string> keys;
};

void reserveReferenceSet(ReferenceSet& references, std::size_t capacity) {
    references.values.reserve(capacity);
    references.keys.reserve(capacity);
}

bool addProfileReference(
    ReferenceSet& references,
    const std::string& value,
    CapabilityReportStats& stats
) {
    const std::string trimmed = trimAscii(value);
    if (trimmed.empty() || isTrimmedIntegerToken(trimmed)) {
        return false;
    }

    const std::string key = lowerAscii(trimmed);
    if (key == "none" || key == "<none>") {
        return false;
    }

    if (references.keys.find(key) != references.keys.end()) {
        ++stats.duplicateReferencesSkipped;
        return false;
    }

    references.keys.insert(key);
    references.values.push_back(trimmed);
    return true;
}

const WeaponType* findWeaponProfileSource(
    const RulesDatabase& rules,
    const std::string& id,
    CapabilityReportStats& stats,
    bool countLookup
) {
    const WeaponType* weapon = rules.findWeaponType(id);
    if (countLookup) {
        if (weapon == nullptr) {
            ++stats.weaponLookupMisses;
        } else {
            ++stats.weaponLookupHits;
        }
    }
    return weapon;
}

const ProjectileType* findProjectileProfileSource(
    const RulesDatabase& rules,
    const std::string& id,
    CapabilityReportStats& stats,
    bool countLookup
) {
    const ProjectileType* projectile = rules.findProjectileType(id);
    if (countLookup) {
        if (projectile == nullptr) {
            ++stats.projectileLookupMisses;
        } else {
            ++stats.projectileLookupHits;
        }
    }
    return projectile;
}

const WarheadType* findWarheadProfileSource(
    const RulesDatabase& rules,
    const std::string& id,
    CapabilityReportStats& stats,
    bool countLookup
) {
    const WarheadType* warhead = rules.findWarheadType(id);
    if (countLookup) {
        if (warhead == nullptr) {
            ++stats.warheadLookupMisses;
        } else {
            ++stats.warheadLookupHits;
        }
    }
    return warhead;
}

void collectCapabilityProfiles(
    const RulesDatabase& rules,
    TechTreeReport& report
) {
    const RulesCapabilityCacheStats cacheStats =
        rules.getCapabilityCacheStats();
    report.capabilityStats.reportNodeCount = report.nodes.size();
    report.capabilityStats.objectProfileCount =
        cacheStats.objectProfileCount;
    report.capabilityStats.objectCapabilityCount =
        cacheStats.objectCapabilityCount;
    report.capabilityStats.weaponSetCount = cacheStats.weaponSetCount;
    report.capabilityStats.deployProfileCount = cacheStats.deployProfileCount;
    report.capabilityStats.uiProfileCount = cacheStats.uiProfileCount;
    report.capabilityStats.weaponProfileCount =
        cacheStats.weaponProfileCount;
    report.capabilityStats.projectileProfileCount =
        cacheStats.projectileProfileCount;
    report.capabilityStats.warheadProfileCount =
        cacheStats.warheadProfileCount;
    report.capabilityStats.indexedObjectCount =
        cacheStats.indexedObjectCount;
    report.capabilityStats.indexedWeaponCount =
        cacheStats.indexedWeaponCount;
    report.capabilityStats.indexedProjectileCount =
        cacheStats.indexedProjectileCount;
    report.capabilityStats.indexedWarheadCount =
        cacheStats.indexedWarheadCount;

    ReferenceSet weaponIds;
    ReferenceSet projectileIds;
    ReferenceSet warheadIds;
    reserveReferenceSet(weaponIds, report.nodes.size() * 2);
    reserveReferenceSet(projectileIds, report.nodes.size());
    reserveReferenceSet(warheadIds, report.nodes.size());

    for (const TechTreeNode& node : report.nodes) {
        addProfileReference(
            weaponIds,
            node.weaponSet.primary,
            report.capabilityStats
        );
        addProfileReference(
            weaponIds,
            node.weaponSet.secondary,
            report.capabilityStats
        );
        addProfileReference(
            weaponIds,
            node.weaponSet.elitePrimary,
            report.capabilityStats
        );
        addProfileReference(
            weaponIds,
            node.weaponSet.eliteSecondary,
            report.capabilityStats
        );
        addProfileReference(
            weaponIds,
            node.weaponSet.deploy,
            report.capabilityStats
        );
        addProfileReference(
            weaponIds,
            node.weaponSet.eliteDeploy,
            report.capabilityStats
        );
        addProfileReference(
            weaponIds,
            node.weaponSet.occupy,
            report.capabilityStats
        );
        addProfileReference(
            weaponIds,
            node.weaponSet.eliteOccupy,
            report.capabilityStats
        );
        for (const std::string& weaponId :
                node.weaponSet.indexedWeapons) {
            addProfileReference(
                weaponIds,
                weaponId,
                report.capabilityStats
            );
        }
        for (const std::string& weaponId :
                node.weaponSet.eliteIndexedWeapons) {
            addProfileReference(
                weaponIds,
                weaponId,
                report.capabilityStats
            );
        }
    }

    std::size_t processedWeapons = 0;
    std::size_t processedProjectiles = 0;
    while (processedWeapons < weaponIds.values.size()
        || processedProjectiles < projectileIds.values.size()) {
        ++report.capabilityStats.dependencyPasses;
        while (processedWeapons < weaponIds.values.size()) {
            const WeaponType* weapon =
                findWeaponProfileSource(
                    rules,
                    weaponIds.values[processedWeapons],
                    report.capabilityStats,
                    true
                );
            ++processedWeapons;
            if (weapon == nullptr) {
                continue;
            }

            const WeaponProfile& profile = weapon->profile;
            addProfileReference(
                projectileIds,
                profile.projectile,
                report.capabilityStats
            );
            addProfileReference(
                warheadIds,
                profile.warhead,
                report.capabilityStats
            );
        }

        while (processedProjectiles < projectileIds.values.size()) {
            const ProjectileType* projectile = findProjectileProfileSource(
                rules,
                projectileIds.values[processedProjectiles],
                report.capabilityStats,
                true
            );
            ++processedProjectiles;
            if (projectile == nullptr) {
                continue;
            }

            const ProjectileProfile& profile = projectile->profile;
            addProfileReference(
                weaponIds,
                profile.airburstWeapon,
                report.capabilityStats
            );
            addProfileReference(
                weaponIds,
                profile.shrapnelWeapon,
                report.capabilityStats
            );
        }
    }

    for (const std::string& warheadId : warheadIds.values) {
        static_cast<void>(findWarheadProfileSource(
            rules,
            warheadId,
            report.capabilityStats,
            true
        ));
    }
}

std::string factoryForUnitKind(const std::string& kind) {
    if (kind == "infantry") {
        return "InfantryType";
    }
    if (kind == "vehicle") {
        return "UnitType";
    }
    if (kind == "aircraft") {
        return "AircraftType";
    }
    return "BuildingType";
}

std::string buildingKind(const BuildingType& building) {
    const std::string buildCat = lowerAscii(building.buildCat);
    if (buildCat == "defense") {
        return "defense";
    }
    if (buildCat == "combat"
        && (!building.primaryWeapon.empty()
            || !building.secondaryWeapon.empty()
            || building.power < 0
            || building.wall
            || building.turret)) {
        return "defense";
    }
    return "building";
}

void finalizeNodeForHouse(
    const RulesDatabase& rules,
    TechTreeNode& node,
    const std::string& house
) {
    node.owner = normalizeListText(node.owner);
    node.requiredHouses = normalizeListText(node.requiredHouses);
    node.forbiddenHouses = normalizeListText(node.forbiddenHouses);
    node.side = sideForOwnerList(rules, node.owner);
    node.prerequisite = normalizeListText(node.prerequisite);
    node.prerequisiteOverride = normalizeListText(node.prerequisiteOverride);
    node.requiredFactory = canonicalFactory(node.requiredFactory);
    node.producesFactory = canonicalFactory(node.producesFactory);
    const TechTreeCountryFilterDecision countryFilter =
        evaluateTechTreeCountryFilter(
            rules,
            house,
            node.owner,
            node.requiredHouses,
            node.forbiddenHouses
        );
    node.ownerAllowed = countryFilter.ownerAllowed;
    node.requiredHousesAllowed = countryFilter.requiredHousesAllowed;
    node.forbiddenHousesAllowed = countryFilter.forbiddenHousesAllowed;
    const int housePlanningSide = basePlanningSideForHouse(house);
    node.planningSideAllowed = node.aiBasePlanningSide < 0
        || housePlanningSide < 0
        || node.aiBasePlanningSide == housePlanningSide;
    node.techAllowed = node.techLevel >= 0 && node.techLevel <= 10;
    node.hasPrerequisite =
        prerequisitePresent(node.prerequisite, node.prerequisiteOverride);
    node.buildableByRules =
        node.ownerAllowed
        && node.requiredHousesAllowed
        && node.forbiddenHousesAllowed
        && node.planningSideAllowed
        && node.techAllowed
        && node.hasPrerequisite
        && node.cost >= 0;

    if (node.buildableByRules) {
        node.blockedReason = "available by static rules";
        return;
    }

    std::vector<std::string> reasons;
    if (!node.ownerAllowed) {
        reasons.push_back("owner");
    }
    if (!node.requiredHousesAllowed) {
        reasons.push_back("requiredHouses");
    }
    if (!node.forbiddenHousesAllowed) {
        reasons.push_back("forbiddenHouses");
    }
    if (!node.planningSideAllowed) {
        reasons.push_back("aiBasePlanningSide");
    }
    if (!node.techAllowed) {
        reasons.push_back("techLevel");
    }
    if (!node.hasPrerequisite) {
        reasons.push_back("prerequisite");
    }
    if (node.cost < 0) {
        reasons.push_back("cost");
    }

    std::ostringstream message;
    for (std::size_t i = 0; i < reasons.size(); ++i) {
        if (i > 0) {
            message << ",";
        }
        message << reasons[i];
    }
    node.blockedReason = message.str();
}

void addNode(
    TechTreeReport& report,
    const RulesDatabase& rules,
    TechTreeNode node
) {
    finalizeNodeForHouse(rules, node, report.house);
    if (node.kind == "building") {
        ++report.buildingNodes;
    } else if (node.kind == "defense") {
        ++report.defenseNodes;
    } else if (node.kind == "infantry") {
        ++report.infantryNodes;
    } else if (node.kind == "vehicle") {
        ++report.vehicleNodes;
    } else if (node.kind == "aircraft") {
        ++report.aircraftNodes;
    }

    if (node.buildableByRules) {
        ++report.buildableNodes;
    } else {
        ++report.blockedNodes;
    }
    report.nodes.push_back(std::move(node));
    report.totalNodes = static_cast<int>(report.nodes.size());
}

TechTreeNode nodeFromBuilding(const BuildingType& building) {
    TechTreeNode node;
    node.kind = buildingKind(building);
    node.id = building.id;
    node.owner = building.owner;
    node.requiredHouses = building.requiredHouses;
    node.forbiddenHouses = building.forbiddenHouses;
    node.aiBasePlanningSide = building.aiBasePlanningSide;
    node.image = building.image;
    node.primaryWeapon = building.primaryWeapon;
    node.secondaryWeapon = building.secondaryWeapon;
    node.elitePrimaryWeapon = building.elitePrimaryWeapon;
    node.eliteSecondaryWeapon = building.eliteSecondaryWeapon;
    node.deploysInto = building.deploysInto;
    node.undeploysInto = building.undeploysInto;
    node.deployWeapon = building.deployWeapon;
    node.eliteDeployWeapon = building.eliteDeployWeapon;
    node.deployWeaponSource = building.deployWeaponSource;
    node.eliteDeployWeaponSource = building.eliteDeployWeaponSource;
    node.deployer = building.deployer;
    node.deployFire = building.deployFire;
    node.deployToFire = building.deployToFire;
    node.isSimpleDeployer = building.isSimpleDeployer;
    node.deployTime = building.deployTime;
    node.noMovingFire = building.noMovingFire;
    node.repairable = building.repairable;
    node.clickRepairable = building.clickRepairable;
    node.capturable = building.capturable;
    node.needsEngineer = building.needsEngineer;
    node.bridgeRepairHut = building.bridgeRepairHut;
    node.canBeOccupied = building.canBeOccupied;
    node.canOccupyFire = building.canOccupyFire;
    node.distributedFire = building.distributedFire;
    node.pip = building.pip;
    node.pipScale = building.pipScale;
    node.pipsDrawForAll = building.pipsDrawForAll;
    node.strength = building.strength;
    node.power = building.power;
    node.powered = building.powered;
    node.prerequisite = building.prerequisite;
    node.prerequisiteOverride = building.prerequisiteOverride;
    node.requiredFactory = "BuildingType";
    node.producesFactory = building.factory;
    node.techLevel = building.techLevel;
    node.cost = building.cost;
    node.capability = building.capability;
    node.weaponSet = building.weaponSet;
    node.deployProfile = building.deployProfile;
    node.occupyProfile = building.occupyProfile;
    node.engineerProfile = building.engineerProfile;
    node.transportProfile = building.transportProfile;
    node.buildingCapability = building.buildingCapability;
    node.uiProfile = building.uiProfile;
    assignDeploySequenceHints(node);
    return node;
}

TechTreeNode nodeFromUnit(const UnitType& unit, const std::string& kind) {
    TechTreeNode node;
    node.kind = kind;
    node.id = unit.id;
    node.owner = unit.owner;
    node.requiredHouses = unit.requiredHouses;
    node.forbiddenHouses = unit.forbiddenHouses;
    node.image = unit.image;
    node.primaryWeapon = unit.primaryWeapon;
    node.secondaryWeapon = unit.secondaryWeapon;
    node.elitePrimaryWeapon = unit.elitePrimaryWeapon;
    node.eliteSecondaryWeapon = unit.eliteSecondaryWeapon;
    node.deploysInto = unit.deploysInto;
    node.undeploysInto = unit.undeploysInto;
    node.deployWeapon = unit.deployWeapon;
    node.eliteDeployWeapon = unit.eliteDeployWeapon;
    node.deployWeaponSource = unit.deployWeaponSource;
    node.eliteDeployWeaponSource = unit.eliteDeployWeaponSource;
    node.deploySound = unit.deploySound;
    node.undeploySound = unit.undeploySound;
    node.deployingAnim = unit.deployingAnim;
    node.voiceDeploy = unit.voiceDeploy;
    node.occupyWeapon = unit.occupyWeapon;
    node.eliteOccupyWeapon = unit.eliteOccupyWeapon;
    node.pip = unit.pip;
    node.pipScale = unit.pipScale;
    node.occupyPip = unit.occupyPip;
    node.deployer = unit.deployer;
    node.deployFire = unit.deployFire;
    node.deployToFire = unit.deployToFire;
    node.isSimpleDeployer = unit.isSimpleDeployer;
    node.deployTime = unit.deployTime;
    node.undeployDelay = unit.undeployDelay;
    node.openTransportWeapon = unit.openTransportWeapon;
    node.deployToLand = unit.deployToLand;
    node.deployedCrushable = unit.deployedCrushable;
    node.noMovingFire = unit.noMovingFire;
    node.engineer = unit.engineer;
    node.bridgeRepair = unit.bridgeRepair;
    node.repairable = unit.repairable;
    node.occupier = unit.occupier;
    node.prerequisite = unit.prerequisite;
    node.prerequisiteOverride = unit.prerequisiteOverride;
    node.requiredFactory = factoryForUnitKind(kind);
    node.techLevel = unit.techLevel;
    node.cost = unit.cost;
    node.strength = unit.strength;
    node.capability = unit.capability;
    node.weaponSet = unit.weaponSet;
    node.deployProfile = unit.deployProfile;
    node.occupyProfile = unit.occupyProfile;
    node.engineerProfile = unit.engineerProfile;
    node.transportProfile = unit.transportProfile;
    node.uiProfile = unit.uiProfile;
    assignDeploySequenceHints(node);
    return node;
}

int kindSortOrder(const std::string& kind) {
    if (kind == "building") {
        return 0;
    }
    if (kind == "defense") {
        return 1;
    }
    if (kind == "infantry") {
        return 2;
    }
    if (kind == "vehicle") {
        return 3;
    }
    if (kind == "aircraft") {
        return 4;
    }
    return 5;
}

void sortNodes(TechTreeReport& report) {
    std::stable_sort(
        report.nodes.begin(),
        report.nodes.end(),
        [](const TechTreeNode& left, const TechTreeNode& right) {
            const int leftKind = kindSortOrder(left.kind);
            const int rightKind = kindSortOrder(right.kind);
            if (leftKind != rightKind) {
                return leftKind < rightKind;
            }
            if (left.techLevel != right.techLevel) {
                return left.techLevel < right.techLevel;
            }
            return lessAsciiIgnoreCase(left.id, right.id);
        }
    );
}

void appendListSeparator(std::ostream& output, bool& hasValue) {
    if (hasValue) {
        output << ",";
    }
    hasValue = true;
}

void appendTextField(
    std::ostream& output,
    bool& hasValue,
    const char* name,
    const std::string& value
) {
    if (value.empty()) {
        return;
    }

    appendListSeparator(output, hasValue);
    output << name << ":" << value;
}

void appendFlagField(
    std::ostream& output,
    bool& hasValue,
    const char* name,
    bool value
) {
    if (!value) {
        return;
    }

    appendListSeparator(output, hasValue);
    output << name;
}

void appendIntField(
    std::ostream& output,
    bool& hasValue,
    const char* name,
    int value
) {
    appendListSeparator(output, hasValue);
    output << name << ":" << value;
}

void writeObjectCapabilitySummary(
    std::ostream& output,
    const ObjectCapability& capability
) {
    bool hasValue = false;
    output << " | capability=";
    appendFlagField(output, hasValue, "movable", capability.movable);
    appendFlagField(output, hasValue, "infantry", capability.infantry);
    appendFlagField(output, hasValue, "vehicle", capability.vehicle);
    appendFlagField(output, hasValue, "aircraft", capability.aircraft);
    appendFlagField(output, hasValue, "building", capability.building);
    appendFlagField(output, hasValue, "naval", capability.naval);
    appendFlagField(output, hasValue, "waterBound", capability.waterBound);
    appendFlagField(output, hasValue, "turret", capability.turret);
    appendFlagField(
        output,
        hasValue,
        "canPassiveAquire",
        capability.canPassiveAquire
    );
    appendFlagField(output, hasValue, "canRetaliate", capability.canRetaliate);
    if (!hasValue) {
        output << "-";
    }
}

void writeUiSummary(std::ostream& output, const UiProfile& uiProfile) {
    if (uiProfile.pip.empty()
        && uiProfile.pipScale.empty()
        && uiProfile.occupyPip.empty()
        && !uiProfile.pipsDrawForAll) {
        return;
    }

    bool hasValue = false;
    output << " | ui=";
    appendTextField(output, hasValue, "pip", uiProfile.pip);
    appendTextField(output, hasValue, "pipScale", uiProfile.pipScale);
    appendTextField(output, hasValue, "occupyPip", uiProfile.occupyPip);
    appendFlagField(
        output,
        hasValue,
        "pipsDrawForAll",
        uiProfile.pipsDrawForAll
    );
}

void writeWeaponExtrasSummary(
    std::ostream& output,
    const WeaponSet& weaponSet
) {
    if (weaponSet.deploy.empty()
        && weaponSet.eliteDeploy.empty()
        && weaponSet.occupy.empty()
        && weaponSet.eliteOccupy.empty()
        && weaponSet.openTransportWeapon < 0
        && weaponSet.indexedWeaponCount <= 0) {
        return;
    }

    bool hasValue = false;
    output << " | weaponExtras=";
    appendTextField(output, hasValue, "deploy", weaponSet.deploy);
    appendTextField(output, hasValue, "eliteDeploy", weaponSet.eliteDeploy);
    appendTextField(output, hasValue, "occupy", weaponSet.occupy);
    appendTextField(output, hasValue, "eliteOccupy", weaponSet.eliteOccupy);
    if (weaponSet.openTransportWeapon >= 0) {
        appendIntField(
            output,
            hasValue,
            "openTransportWeapon",
            weaponSet.openTransportWeapon
        );
    }
    if (weaponSet.indexedWeaponCount > 0) {
        appendIntField(
            output,
            hasValue,
            "indexedWeaponCount",
            weaponSet.indexedWeaponCount
        );
    }
}

void writeBuildingSummary(
    std::ostream& output,
    const BuildingCapability& capability
) {
    bool hasValue = false;
    output << " | building=";
    appendIntField(output, hasValue, "power", capability.power);
    appendFlagField(output, hasValue, "powered", capability.powered);
    appendTextField(output, hasValue, "factory", capability.factoryType);
    appendFlagField(
        output,
        hasValue,
        "constructionYard",
        capability.constructionYard
    );
    appendFlagField(output, hasValue, "refinery", capability.refinery);
    appendFlagField(
        output,
        hasValue,
        "barracksFactory",
        capability.barracksFactory
    );
    appendFlagField(
        output,
        hasValue,
        "vehicleFactory",
        capability.vehicleFactory
    );
    appendFlagField(
        output,
        hasValue,
        "aircraftFactory",
        capability.aircraftFactory
    );
    appendFlagField(output, hasValue, "navalFactory", capability.navalFactory);
    appendFlagField(output, hasValue, "radar", capability.radar);
    appendFlagField(output, hasValue, "spySat", capability.spySat);
    appendTextField(output, hasValue, "art", "foundation/anim");
}

void addUniqueNode(
    TechTreeReport& report,
    const RulesDatabase& rules,
    std::unordered_set<std::string>& seen,
    TechTreeNode node
) {
    const std::string key = lowerAscii(node.id);
    if (seen.find(key) != seen.end()) {
        return;
    }

    seen.insert(key);
    addNode(report, rules, std::move(node));
}

} // namespace

TechTreeCountryFilterDecision evaluateTechTreeCountryFilter(
    const RulesDatabase& rules,
    const std::string& house,
    const std::string& owner,
    const std::string& requiredHouses,
    const std::string& forbiddenHouses
) {
    TechTreeCountryFilterDecision decision;
    const std::string normalizedOwner = normalizeListText(owner);
    const std::string normalizedRequired = normalizeListText(requiredHouses);
    const std::string normalizedForbidden = normalizeListText(forbiddenHouses);
    decision.ownerAllowed = ownerAllowsHouse(rules, normalizedOwner, house);
    decision.requiredHousesAllowed = houseRestrictionAllowsHouse(
        rules,
        normalizedRequired,
        house
    );
    decision.forbiddenHousesAllowed = !houseRestrictionForbidsHouse(
        rules,
        normalizedForbidden,
        house
    );
    decision.visible = decision.ownerAllowed
        && decision.requiredHousesAllowed
        && decision.forbiddenHousesAllowed;

    std::vector<std::string> reasons;
    if (!decision.ownerAllowed) {
        reasons.push_back("owner");
    }
    if (!decision.requiredHousesAllowed) {
        reasons.push_back("requiredHouses");
    }
    if (!decision.forbiddenHousesAllowed) {
        reasons.push_back("forbiddenHouses");
    }
    decision.blockedReason = joinTokens(reasons);
    return decision;
}

TechTreeReport analyzeTechTree(const RulesDatabase& rules, const std::string& house) {
    TechTreeReport report;
    report.house = house.empty() ? "Americans" : house;
    std::unordered_set<std::string> seenNodes;

    for (const BuildingType& building : rules.getBuildingTypes()) {
        addUniqueNode(report, rules, seenNodes, nodeFromBuilding(building));
    }
    for (const UnitType& unit : rules.getInfantryTypes()) {
        addUniqueNode(report, rules, seenNodes, nodeFromUnit(unit, "infantry"));
    }
    for (const UnitType& unit : rules.getVehicleTypes()) {
        addUniqueNode(report, rules, seenNodes, nodeFromUnit(unit, "vehicle"));
    }
    for (const UnitType& unit : rules.getAircraftTypes()) {
        addUniqueNode(report, rules, seenNodes, nodeFromUnit(unit, "aircraft"));
    }

    sortNodes(report);
    collectCapabilityProfiles(rules, report);
    return report;
}

void printTechTreeReport(std::ostream& output, const TechTreeReport& report) {
    output << "\n========== Tech Tree ==========\n";
    output << "House:     " << report.house << "\n";
    output << "Total:     " << report.totalNodes << "\n";
    output << "Buildable: " << report.buildableNodes << "\n";
    output << "Blocked:   " << report.blockedNodes << "\n";
    output << "Kinds:     buildings=" << report.buildingNodes
           << " defense=" << report.defenseNodes
           << " infantry=" << report.infantryNodes
           << " vehicles=" << report.vehicleNodes
           << " aircraft=" << report.aircraftNodes
           << "\n";
    output << "Capability cache: nodes="
           << report.capabilityStats.reportNodeCount
           << " objectProfiles="
           << report.capabilityStats.objectProfileCount
           << " objectCapabilities="
           << report.capabilityStats.objectCapabilityCount
           << " weaponSets="
           << report.capabilityStats.weaponSetCount
           << " deployProfiles="
           << report.capabilityStats.deployProfileCount
           << " uiProfiles="
           << report.capabilityStats.uiProfileCount
           << " weapons="
           << report.capabilityStats.weaponProfileCount
           << " projectiles="
           << report.capabilityStats.projectileProfileCount
           << " warheads="
           << report.capabilityStats.warheadProfileCount
           << "\n";
    output << "Capability indexes: objects="
           << report.capabilityStats.indexedObjectCount
           << " weapons="
           << report.capabilityStats.indexedWeaponCount
           << " projectiles="
           << report.capabilityStats.indexedProjectileCount
           << " warheads="
           << report.capabilityStats.indexedWarheadCount
           << "\n";
    output << "Capability lookups: weapon hit/miss="
           << report.capabilityStats.weaponLookupHits
           << "/"
           << report.capabilityStats.weaponLookupMisses
           << " projectile hit/miss="
           << report.capabilityStats.projectileLookupHits
           << "/"
           << report.capabilityStats.projectileLookupMisses
           << " warhead hit/miss="
           << report.capabilityStats.warheadLookupHits
           << "/"
           << report.capabilityStats.warheadLookupMisses
           << " duplicateRefsSkipped="
           << report.capabilityStats.duplicateReferencesSkipped
           << " dependencyPasses="
           << report.capabilityStats.dependencyPasses
           << "\n\n";

    for (const TechTreeNode& node : report.nodes) {
        output << node.kind
               << " " << node.id
               << " | tech=" << node.techLevel
               << " | cost=" << node.cost
               << " | strength=" << node.strength
               << " | power=" << node.power
               << " | powered=" << yesNo(node.powered)
               << " | owner=" << (node.owner.empty() ? "-" : node.owner)
               << " | requiredHouses="
               << (node.requiredHouses.empty() ? "-" : node.requiredHouses)
               << " | forbiddenHouses="
               << (node.forbiddenHouses.empty() ? "-" : node.forbiddenHouses)
               << " | image=" << (node.image.empty() ? "-" : node.image)
               << " | primary=" << (node.primaryWeapon.empty() ? "-" : node.primaryWeapon)
               << " | secondary="
               << (node.secondaryWeapon.empty() ? "-" : node.secondaryWeapon)
               << " | elitePrimary="
               << (node.elitePrimaryWeapon.empty() ? "-" : node.elitePrimaryWeapon)
               << " | eliteSecondary="
               << (node.eliteSecondaryWeapon.empty() ? "-" : node.eliteSecondaryWeapon)
               << " | side=" << (node.side.empty() ? "-" : node.side)
               << " | prereq=" << (node.prerequisite.empty() ? "-" : node.prerequisite)
               << " | prereqOverride="
               << (node.prerequisiteOverride.empty() ? "-" : node.prerequisiteOverride)
               << " | factory=" << node.requiredFactory
               << " | produces="
               << (node.producesFactory.empty() ? "-" : node.producesFactory);
        writeObjectCapabilitySummary(output, node.capability);
        writeUiSummary(output, node.uiProfile);
        output << " | "
               << (node.buildableByRules ? "buildable" : "blocked")
               << " | "
               << node.blockedReason;
        writeWeaponExtrasSummary(output, node.weaponSet);
        if (node.transportProfile.canTransport
            || node.transportProfile.openTransportWeapon >= 0) {
            output << " | transport=passengers:"
                   << node.transportProfile.passengers
                   << ",size:" << node.transportProfile.size
                   << ",unloadingClass:"
                   << dashIfEmpty(node.transportProfile.unloadingClass)
                   << ",openTransportWeapon:"
                   << (node.transportProfile.openTransportWeapon < 0
                       ? "-"
                       : std::to_string(node.transportProfile.openTransportWeapon))
                   << ",airportBound:"
                   << yesNo(node.transportProfile.airportBound)
                   << ",dock:" << dashIfEmpty(node.transportProfile.dock);
        }
        if (node.capability.building) {
            writeBuildingSummary(output, node.buildingCapability);
        }
        if (node.deployProfile.canDeploy) {
            output << " | deployProfile=canDeploy="
                   << yesNo(node.deployProfile.canDeploy)
                   << " deployer=" << yesNo(node.deployProfile.deployer)
                   << " deployFire=" << yesNo(node.deployProfile.deployFire)
                   << " deployToFire=" << yesNo(node.deployProfile.deployToFire)
                   << " simple=" << yesNo(node.deployProfile.isSimpleDeployer)
                   << " noMovingFire=" << yesNo(node.deployProfile.noMovingFire)
                   << " deploysInto="
                   << dashIfEmpty(node.deployProfile.deploysInto)
                   << " undeploysInto="
                   << dashIfEmpty(node.deployProfile.undeploysInto)
                   << " deployWeapon="
                   << dashIfEmpty(node.deployProfile.deployWeapon)
                   << " deployWeaponSource="
                   << dashIfEmpty(node.deployProfile.deployWeaponSource)
                   << " eliteDeployWeapon="
                   << dashIfEmpty(node.deployProfile.eliteDeployWeapon)
                   << " eliteDeployWeaponSource="
                   << dashIfEmpty(node.deployProfile.eliteDeployWeaponSource)
                   << " deploySequence="
                   << dashIfEmpty(node.deployProfile.deploySequence)
                   << " deployIdleSequence="
                   << dashIfEmpty(node.deployProfile.deployIdleSequence)
                   << " deployFireSequence="
                   << dashIfEmpty(node.deployProfile.deployFireSequence)
                   << " deployedSequence="
                   << dashIfEmpty(node.deployProfile.deployedSequence)
                   << " deployedFireSequence="
                   << dashIfEmpty(node.deployProfile.deployedFireSequence)
                   << " deployedIdleSequence="
                   << dashIfEmpty(node.deployProfile.deployedIdleSequence)
                   << " undeploySequence="
                   << dashIfEmpty(node.deployProfile.undeploySequence)
                   << " deployTime=" << node.deployProfile.deployTime
                   << " undeployDelay="
                   << (node.deployProfile.undeployDelay < 0
                       ? "-"
                       : std::to_string(node.deployProfile.undeployDelay))
                   << " deployToLand=" << yesNo(node.deployProfile.deployToLand)
                   << " deployFacing="
                   << (node.deployProfile.deployFacing < 0
                       ? "-"
                       : std::to_string(node.deployProfile.deployFacing))
                   << " unloadingClass="
                   << dashIfEmpty(node.deployProfile.unloadingClass)
                   << " deployedCrushable="
                   << yesNo(node.deployProfile.deployedCrushable)
                   << " deployingAnim="
                   << dashIfEmpty(node.deployProfile.deployingAnim)
                   << " voiceDeploy="
                   << dashIfEmpty(node.deployProfile.voiceDeploy)
                   << " deploySound="
                   << dashIfEmpty(node.deployProfile.deploySound)
                   << " undeploySound="
                   << dashIfEmpty(node.deployProfile.undeploySound);
        }
        if (node.engineer
            || node.bridgeRepair
            || !node.repairable
            || !node.clickRepairable
            || !node.capturable
            || node.needsEngineer
            || node.bridgeRepairHut
            || node.occupier
            || node.canBeOccupied
            || node.canOccupyFire
            || node.distributedFire
            || !node.occupyWeapon.empty()
            || !node.eliteOccupyWeapon.empty()
            || node.openTransportWeapon >= 0) {
            output << " | engineer=" << yesNo(node.engineer)
                   << " bridgeRepair=" << yesNo(node.bridgeRepair)
                   << " repairable=" << yesNo(node.repairable)
                   << " clickRepairable=" << yesNo(node.clickRepairable)
                   << " capturable=" << yesNo(node.capturable)
                   << " needsEngineer=" << yesNo(node.needsEngineer)
                   << " bridgeRepairHut=" << yesNo(node.bridgeRepairHut)
                   << " occupier=" << yesNo(node.occupier)
                   << " canBeOccupied=" << yesNo(node.canBeOccupied)
                   << " canOccupyFire=" << yesNo(node.canOccupyFire)
                   << " distributedFire=" << yesNo(node.distributedFire)
                   << " occupyWeapon="
                   << (node.occupyWeapon.empty() ? "-" : node.occupyWeapon)
                   << " eliteOccupyWeapon="
                   << (node.eliteOccupyWeapon.empty() ? "-" : node.eliteOccupyWeapon)
                   << " openTransportWeapon="
                   << (node.openTransportWeapon < 0
                       ? "-"
                       : std::to_string(node.openTransportWeapon))
                   << " occupyPip="
                   << (node.occupyPip.empty() ? "-" : node.occupyPip);
        }
        output << "\n";
    }

    output << "\n========== Capability Profiles ==========\n";
    output << "Referenced lookups: weapons="
           << report.capabilityStats.weaponLookupHits
           << " projectiles=" << report.capabilityStats.projectileLookupHits
           << " warheads=" << report.capabilityStats.warheadLookupHits
           << "\n";
    output << "Evidence: weapon/projectile/warhead rows are loaded from rules sections "
           << "through cached normalized lookup; deploy weapon sources come from "
           << "object WeaponSet/DeployProfile fields; projectile image/trailer "
           << "and impact fields are profile-derived.\n\n";
    output << "UI resources: rules/art expose Cameo/AltCameo through ArtDatabase links, "
           << "Pip/PipScale/OccupyPip/PipsDrawForAll/Strength/Power/Powered/Radar/SpySat "
           << "through RulesDatabase profiles, and PoweredLight/LowPower/SuperLowPower "
           << "through BuildingAnimProfile; sidebar power/health resource filenames "
           << "are not rules/art fields and must be located by the asset layer.\n\n";
}
