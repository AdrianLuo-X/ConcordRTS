#include "EconomySandbox.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum class EconomyOrderKind {
    Building,
    Infantry,
    Vehicle
};

struct FactionBuildPlan {
    std::vector<std::string> buildings;
    std::string infantryId;
    std::string vehicleId;
};

struct BuildingInstance {
    int instanceId = 0;
    const BuildingType* type = nullptr;
    EconomyGridPoint position;
};

struct UnitInstance {
    int instanceId = 0;
    const UnitType* type = nullptr;
    EconomyGridPoint position;
};

struct EconomyOrder {
    EconomyOrderKind kind = EconomyOrderKind::Building;
    std::string typeId;
    int cost = 0;
    int remainingTicks = 0;
};

struct HouseState {
    std::string id;
    int credits = 0;
    int powerProduced = 0;
    int powerUsed = 0;
    int nextInstanceId = 1;
    int harvestCountdown = 0;
    int oreLoadsDelivered = 0;
    std::vector<BuildingInstance> buildings;
    std::vector<UnitInstance> units;
    std::deque<EconomyOrder> queue;
    std::optional<EconomyOrder> currentOrder;
};

std::string toUpper(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        }
    );
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> splitCommaList(const std::string& value) {
    std::vector<std::string> tokens;
    std::stringstream input(value);
    std::string token;

    while (std::getline(input, token, ',')) {
        token = trim(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    return tokens;
}

std::string formatPoint(const EconomyGridPoint& point) {
    return "(" + std::to_string(point.x) + "," + std::to_string(point.y) + ")";
}

void appendEvent(
    EconomySandboxReport& report,
    int tick,
    const std::string& text
) {
    report.events.push_back({tick, text});
}

const BuildingType* findBuildingType(
    const RulesDatabase& rules,
    const std::string& id
) {
    const auto& buildings = rules.getBuildingTypes();
    const auto it = std::find_if(
        buildings.begin(),
        buildings.end(),
        [&id](const BuildingType& building) {
            return building.id == id;
        }
    );
    return it == buildings.end() ? nullptr : &*it;
}

const UnitType* findUnitType(const RulesDatabase& rules, const std::string& id) {
    if (const UnitType* unit = rules.findUnitType("InfantryType", id)) {
        return unit;
    }
    if (const UnitType* unit = rules.findUnitType("UnitType", id)) {
        return unit;
    }
    return rules.findUnitType("AircraftType", id);
}

bool isInfantryType(const RulesDatabase& rules, const std::string& id) {
    return rules.findUnitType("InfantryType", id) != nullptr;
}

int buildTimeTicks(int cost) {
    return std::max(1, (std::max(0, cost) + 99) / 100);
}

EconomyGridPoint buildingPlacement(std::size_t placedCount) {
    return {
        6 + static_cast<int>((placedCount % 4) * 3),
        4 + static_cast<int>((placedCount / 4) * 3)
    };
}

EconomyGridPoint unitRallyPoint(std::size_t producedCount) {
    return {
        8 + static_cast<int>(producedCount % 5),
        10 + static_cast<int>(producedCount / 5)
    };
}

bool chooseFactionPlan(
    const std::string& constructionYardId,
    const EconomySandboxConfig& config,
    FactionBuildPlan& plan
) {
    if (constructionYardId == "GACNST") {
        plan.buildings = {"GAPOWR", "GAREFN", "GAPILE", "GAWEAP"};
        plan.infantryId = config.infantryId.empty() ? "E1" : config.infantryId;
        plan.vehicleId = config.vehicleId.empty() ? "MTNK" : config.vehicleId;
        return true;
    }

    if (constructionYardId == "NACNST") {
        plan.buildings = {"NAPOWR", "NAREFN", "NAHAND", "NAWEAP"};
        plan.infantryId = config.infantryId.empty() ? "E2" : config.infantryId;
        plan.vehicleId = config.vehicleId.empty() ? "HTNK" : config.vehicleId;
        return true;
    }

    if (constructionYardId == "YACNST") {
        plan.buildings = {"YAPOWR", "YAREFN", "YABRCK", "YAWEAP"};
        plan.infantryId = config.infantryId.empty() ? "INIT" : config.infantryId;
        plan.vehicleId = config.vehicleId.empty() ? "LTNK" : config.vehicleId;
        return true;
    }

    return false;
}

bool hasBuilding(const HouseState& house, const std::string& id) {
    return std::any_of(
        house.buildings.begin(),
        house.buildings.end(),
        [&id](const BuildingInstance& building) {
            return building.type != nullptr && building.type->id == id;
        }
    );
}

bool hasFactory(const HouseState& house, const std::string& factory) {
    return std::any_of(
        house.buildings.begin(),
        house.buildings.end(),
        [&factory](const BuildingInstance& building) {
            return building.type != nullptr && building.type->factory == factory;
        }
    );
}

bool hasRefinery(const HouseState& house) {
    return std::any_of(
        house.buildings.begin(),
        house.buildings.end(),
        [](const BuildingInstance& building) {
            return building.type != nullptr
                && (building.type->refinery || building.type->buildCat == "Resource");
        }
    );
}

bool hasPrerequisiteToken(const HouseState& house, const std::string& token) {
    const std::string normalized = toUpper(token);

    if (normalized == "POWER") {
        return house.powerProduced > 0;
    }
    if (normalized == "PROC") {
        return hasRefinery(house);
    }
    if (normalized == "BARRACKS") {
        return hasFactory(house, "InfantryType");
    }
    if (normalized == "FACTORY") {
        return hasFactory(house, "UnitType");
    }

    for (const BuildingInstance& building : house.buildings) {
        if (building.type != nullptr && toUpper(building.type->id) == normalized) {
            return true;
        }
    }

    return false;
}

bool prerequisitesMet(const HouseState& house, const std::string& prerequisites) {
    for (const std::string& token : splitCommaList(prerequisites)) {
        if (!hasPrerequisiteToken(house, token)) {
            return false;
        }
    }
    return true;
}

void addBuildingInstance(
    EconomySandboxReport& report,
    HouseState& house,
    int tick,
    const BuildingType& type,
    EconomyGridPoint position,
    bool freePlacement
) {
    BuildingInstance building;
    building.instanceId = house.nextInstanceId++;
    building.type = &type;
    building.position = position;
    house.buildings.push_back(building);

    if (type.power >= 0) {
        house.powerProduced += type.power;
    } else {
        house.powerUsed += -type.power;
    }

    std::ostringstream line;
    line << "#"
         << building.instanceId
         << " "
         << type.id
         << " placed at "
         << formatPoint(position);
    if (freePlacement) {
        line << " for free";
    }
    line << " power="
         << house.powerProduced
         << "/"
         << house.powerUsed;
    appendEvent(report, tick, line.str());
}

void addUnitInstance(
    EconomySandboxReport& report,
    HouseState& house,
    int tick,
    const UnitType& type,
    EconomyGridPoint position,
    const std::string& reason
) {
    UnitInstance unit;
    unit.instanceId = house.nextInstanceId++;
    unit.type = &type;
    unit.position = position;
    house.units.push_back(unit);

    std::ostringstream line;
    line << "#"
         << unit.instanceId
         << " "
         << type.id
         << " created at "
         << formatPoint(position)
         << " "
         << reason;
    appendEvent(report, tick, line.str());
}

void queueOrder(
    EconomySandboxReport& report,
    HouseState& house,
    int tick,
    EconomyOrderKind kind,
    const std::string& typeId,
    int cost
) {
    house.queue.push_back({kind, typeId, cost, buildTimeTicks(cost)});

    std::ostringstream line;
    line << "queued "
         << (kind == EconomyOrderKind::Building
             ? "building "
             : (kind == EconomyOrderKind::Infantry ? "infantry " : "vehicle "))
         << typeId
         << " cost="
         << cost;
    appendEvent(report, tick, line.str());
}

bool canStartOrder(
    const RulesDatabase& rules,
    const HouseState& house,
    const EconomyOrder& order,
    std::string& reason
) {
    if (house.credits < order.cost) {
        reason = "waiting for credits";
        return false;
    }

    if (order.kind == EconomyOrderKind::Building) {
        const BuildingType* building = findBuildingType(rules, order.typeId);
        if (building == nullptr) {
            reason = "missing building type";
            return false;
        }
        if (!hasFactory(house, "BuildingType")) {
            reason = "missing construction yard";
            return false;
        }
        if (!prerequisitesMet(house, building->prerequisite)) {
            reason = "waiting for prerequisite " + building->prerequisite;
            return false;
        }
        return true;
    }

    const UnitType* unit = findUnitType(rules, order.typeId);
    if (unit == nullptr) {
        reason = "missing unit type";
        return false;
    }

    const char* requiredFactory =
        order.kind == EconomyOrderKind::Infantry ? "InfantryType" : "UnitType";
    if (!hasFactory(house, requiredFactory)) {
        reason = std::string("missing factory ") + requiredFactory;
        return false;
    }
    if (!prerequisitesMet(house, unit->prerequisite)) {
        reason = "waiting for prerequisite " + unit->prerequisite;
        return false;
    }

    return true;
}

void startNextOrder(
    EconomySandboxReport& report,
    const RulesDatabase& rules,
    HouseState& house,
    int tick
) {
    if (house.currentOrder.has_value() || house.queue.empty()) {
        return;
    }

    EconomyOrder order = house.queue.front();
    std::string reason;
    if (!canStartOrder(rules, house, order, reason)) {
        if (tick == 0 || tick % 60 == 0) {
            appendEvent(report, tick, order.typeId + " " + reason);
        }
        return;
    }

    house.queue.pop_front();
    house.credits -= order.cost;
    house.currentOrder = order;

    std::ostringstream line;
    line << "started "
         << order.typeId
         << " cost="
         << order.cost
         << " credits="
         << house.credits
         << " buildTicks="
         << order.remainingTicks;
    appendEvent(report, tick, line.str());
}

void completeCurrentOrder(
    EconomySandboxReport& report,
    const RulesDatabase& rules,
    const EconomySandboxConfig& config,
    HouseState& house,
    int tick
) {
    if (!house.currentOrder.has_value()) {
        return;
    }

    EconomyOrder order = *house.currentOrder;
    house.currentOrder.reset();

    if (order.kind == EconomyOrderKind::Building) {
        const BuildingType* building = findBuildingType(rules, order.typeId);
        if (building == nullptr) {
            return;
        }

        const EconomyGridPoint position = buildingPlacement(house.buildings.size());
        addBuildingInstance(report, house, tick, *building, position, false);

        if (!building->freeUnit.empty()) {
            if (const UnitType* freeUnit = findUnitType(rules, building->freeUnit)) {
                addUnitInstance(
                    report,
                    house,
                    tick,
                    *freeUnit,
                    {position.x + 1, position.y + 1},
                    "from " + building->id + " FreeUnit"
                );
                if (freeUnit->harvester) {
                    house.harvestCountdown = std::max(1, config.harvestTripTicks);
                    appendEvent(
                        report,
                        tick,
                        freeUnit->id + " begins ore route to refinery"
                    );
                }
            }
        }
        return;
    }

    const UnitType* unit = findUnitType(rules, order.typeId);
    if (unit == nullptr) {
        return;
    }

    addUnitInstance(
        report,
        house,
        tick,
        *unit,
        unitRallyPoint(house.units.size()),
        order.kind == EconomyOrderKind::Infantry
            ? "from infantry factory"
            : "from vehicle factory"
    );
}

void processCurrentOrder(
    EconomySandboxReport& report,
    const RulesDatabase& rules,
    const EconomySandboxConfig& config,
    HouseState& house,
    int tick
) {
    if (!house.currentOrder.has_value()) {
        return;
    }

    --house.currentOrder->remainingTicks;
    if (house.currentOrder->remainingTicks <= 0) {
        completeCurrentOrder(report, rules, config, house, tick);
    }
}

bool ownsHarvester(const HouseState& house) {
    return std::any_of(
        house.units.begin(),
        house.units.end(),
        [](const UnitInstance& unit) {
            return unit.type != nullptr && unit.type->harvester;
        }
    );
}

void processHarvesting(
    EconomySandboxReport& report,
    const EconomySandboxConfig& config,
    HouseState& house,
    int tick
) {
    if (!ownsHarvester(house) || !hasRefinery(house)) {
        return;
    }

    if (house.harvestCountdown <= 0) {
        house.harvestCountdown = std::max(1, config.harvestTripTicks);
    }

    --house.harvestCountdown;
    if (house.harvestCountdown > 0) {
        return;
    }

    house.credits += std::max(0, config.harvestCredits);
    ++house.oreLoadsDelivered;

    std::ostringstream line;
    line << "harvester delivered ore +"
         << config.harvestCredits
         << " credits="
         << house.credits
         << " loads="
         << house.oreLoadsDelivered;
    appendEvent(report, tick, line.str());

    house.harvestCountdown = std::max(1, config.harvestTripTicks);
}

int countInfantry(const HouseState& house, const RulesDatabase& rules) {
    return static_cast<int>(std::count_if(
        house.units.begin(),
        house.units.end(),
        [&rules](const UnitInstance& unit) {
            return unit.type != nullptr && isInfantryType(rules, unit.type->id);
        }
    ));
}

int countCombatVehicles(const HouseState& house, const RulesDatabase& rules) {
    return static_cast<int>(std::count_if(
        house.units.begin(),
        house.units.end(),
        [&rules](const UnitInstance& unit) {
            return unit.type != nullptr
                && !unit.type->harvester
                && !isInfantryType(rules, unit.type->id);
        }
    ));
}

void fillReportObjects(EconomySandboxReport& report, const HouseState& house) {
    report.buildings.clear();
    for (const BuildingInstance& building : house.buildings) {
        if (building.type == nullptr) {
            continue;
        }
        report.buildings.push_back({
            building.instanceId,
            "building",
            building.type->id,
            building.position
        });
    }

    report.units.clear();
    for (const UnitInstance& unit : house.units) {
        if (unit.type == nullptr) {
            continue;
        }
        report.units.push_back({
            unit.instanceId,
            unit.type->harvester ? "harvester" : "unit",
            unit.type->id,
            unit.position
        });
    }
}

bool objectiveComplete(const HouseState& house, const RulesDatabase& rules) {
    return house.queue.empty()
        && !house.currentOrder.has_value()
        && house.powerProduced >= house.powerUsed
        && hasFactory(house, "BuildingType")
        && hasFactory(house, "InfantryType")
        && hasFactory(house, "UnitType")
        && hasRefinery(house)
        && ownsHarvester(house)
        && countInfantry(house, rules) >= 1
        && countCombatVehicles(house, rules) >= 1
        && house.oreLoadsDelivered >= 2;
}

} // namespace

EconomySandboxReport runEconomySandbox(
    const RulesDatabase& rules,
    const EconomySandboxConfig& config
) {
    EconomySandboxReport report;
    report.houseId = config.houseId;
    report.mcvId = config.mcvId;
    report.startingCredits = std::max(0, config.startingCredits);

    const UnitType* mcv = findUnitType(rules, config.mcvId);
    if (mcv == nullptr) {
        report.errorMessage = "Unknown MCV unit type: " + config.mcvId;
        return report;
    }

    if (mcv->deploysInto.empty()) {
        report.errorMessage = "Unit " + config.mcvId + " has no DeploysInto building.";
        return report;
    }

    const BuildingType* constructionYard =
        findBuildingType(rules, mcv->deploysInto);
    if (constructionYard == nullptr) {
        report.errorMessage =
            "MCV " + config.mcvId + " deploys into missing building ["
            + mcv->deploysInto + "].";
        return report;
    }

    FactionBuildPlan plan;
    if (!chooseFactionPlan(constructionYard->id, config, plan)) {
        report.errorMessage =
            "No economy build plan for construction yard " + constructionYard->id;
        return report;
    }

    HouseState house;
    house.id = config.houseId;
    house.credits = report.startingCredits;
    report.constructionYardId = constructionYard->id;
    report.success = true;

    appendEvent(
        report,
        0,
        "created house " + house.id
            + " credits=" + std::to_string(house.credits)
    );

    const int mcvInstanceId = house.nextInstanceId++;
    appendEvent(
        report,
        0,
        "selected #" + std::to_string(mcvInstanceId)
            + " " + mcv->id + " at (4,4)"
    );
    appendEvent(
        report,
        0,
        "#" + std::to_string(mcvInstanceId)
            + " " + mcv->id + " deploy command -> "
            + constructionYard->id
    );
    addBuildingInstance(
        report,
        house,
        0,
        *constructionYard,
        {4, 4},
        true
    );

    for (const std::string& buildingId : plan.buildings) {
        const BuildingType* building = findBuildingType(rules, buildingId);
        if (building == nullptr) {
            report.success = false;
            report.errorMessage = "Missing build-plan building: " + buildingId;
            return report;
        }
        queueOrder(report, house, 0, EconomyOrderKind::Building, buildingId, building->cost);
    }

    const UnitType* infantry = findUnitType(rules, plan.infantryId);
    if (infantry == nullptr) {
        report.success = false;
        report.errorMessage = "Missing build-plan infantry: " + plan.infantryId;
        return report;
    }
    queueOrder(report, house, 0, EconomyOrderKind::Infantry, infantry->id, infantry->cost);

    const UnitType* vehicle = findUnitType(rules, plan.vehicleId);
    if (vehicle == nullptr) {
        report.success = false;
        report.errorMessage = "Missing build-plan vehicle: " + plan.vehicleId;
        return report;
    }
    queueOrder(report, house, 0, EconomyOrderKind::Vehicle, vehicle->id, vehicle->cost);

    const int maxTicks = std::max(1, config.maxTicks);
    for (int tick = 0; tick < maxTicks; ++tick) {
        report.ticksSimulated = tick + 1;
        processHarvesting(report, config, house, tick);
        startNextOrder(report, rules, house, tick);
        processCurrentOrder(report, rules, config, house, tick);

        if (objectiveComplete(house, rules)) {
            report.objectiveComplete = true;
            appendEvent(report, tick, "economy objective complete");
            break;
        }
    }

    report.finalCredits = house.credits;
    report.powerProduced = house.powerProduced;
    report.powerUsed = house.powerUsed;
    report.oreLoadsDelivered = house.oreLoadsDelivered;
    report.infantryProduced = countInfantry(house, rules);
    report.combatVehiclesProduced = countCombatVehicles(house, rules);
    report.harvestersOwned = static_cast<int>(std::count_if(
        house.units.begin(),
        house.units.end(),
        [](const UnitInstance& unit) {
            return unit.type != nullptr && unit.type->harvester;
        }
    ));
    report.winner = report.objectiveComplete ? house.id : "none";
    fillReportObjects(report, house);

    return report;
}
