#include "VictoryCondition.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <ostream>
#include <string>

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

std::string ownerKey(const std::string& owner) {
    const std::string trimmed = trimAscii(owner);
    return trimmed.empty() ? "<none>" : trimmed;
}

bool sameOwnerName(const std::string& left, const std::string& right) {
    return lowerAscii(trimAscii(left)) == lowerAscii(trimAscii(right));
}

bool isNeutralOwner(const std::string& owner) {
    const std::string normalized = lowerAscii(trimAscii(owner));
    return normalized.empty()
        || normalized == "<none>"
        || normalized == "none"
        || normalized == "neutral"
        || normalized == "special"
        || normalized == "civilian"
        || normalized == "civilians"
        || normalized == "gaia"
        || normalized == "observer";
}

bool isNonVictoryStructureType(const std::string& type) {
    const std::string normalized = lowerAscii(trimAscii(type));
    return normalized.empty()
        || normalized.rfind("ca", 0) == 0
        || normalized.rfind("cabhut", 0) == 0
        || normalized.find("wall") != std::string::npos
        || normalized.find("fenc") != std::string::npos
        || normalized.find("gate") != std::string::npos
        || normalized == "gasand"
        || normalized == "cafncp"
        || normalized == "cafncw"
        || normalized == "cafncb"
        || normalized == "cabhut";
}

void addObjectToOwnerState(
    VictoryOwnerState& state,
    const WorldObjectInstance& object
) {
    if (!object.alive || object.health == 0) {
        return;
    }

    ++state.aliveObjects;
    switch (object.kind) {
    case WorldObjectKind::Structure:
        ++state.structures;
        if (isNonVictoryStructureType(object.type)) {
            ++state.nonVictoryStructures;
        } else {
            ++state.victoryObjects;
        }
        break;
    case WorldObjectKind::Unit:
        ++state.units;
        ++state.victoryObjects;
        break;
    case WorldObjectKind::Infantry:
        ++state.infantry;
        ++state.victoryObjects;
        break;
    }
}

void finalizeOwnerState(VictoryOwnerState& state) {
    state.neutral = isNeutralOwner(state.owner);
    state.combatEligible = !state.neutral && state.victoryObjects > 0;
    state.defeated =
        !state.neutral
        && state.victoryObjects == 0;
}

} // namespace

VictoryConditionReport buildVictoryConditionReport(
    const GameWorld& world,
    const MapDatabase& map,
    const std::string& trackedHouse
) {
    VictoryConditionReport report;
    report.trackedHouse = trackedHouse.empty() ? "Americans" : trackedHouse;
    report.mapName = map.getSummary().name;
    report.theater = map.getSummary().theater;
    report.triggerCount = static_cast<int>(map.getSummary().triggerCount);
    report.missionMap = report.triggerCount > 0;

    if (!world.getSummary().loaded) {
        report.errorMessage = "GameWorld is not loaded.";
        return report;
    }

    std::map<std::string, VictoryOwnerState> ownersByName;
    for (const WorldObjectInstance& object : world.getObjects()) {
        const std::string owner = ownerKey(object.owner);
        VictoryOwnerState& state = ownersByName[owner];
        state.owner = owner;
        addObjectToOwnerState(state, object);
    }

    report.totalOwners = static_cast<int>(ownersByName.size());
    for (auto& [owner, state] : ownersByName) {
        finalizeOwnerState(state);
        if (state.neutral) {
            ++report.neutralOwners;
        }
        if (state.combatEligible) {
            ++report.activeCombatOwners;
        }

        if (sameOwnerName(owner, report.trackedHouse)) {
            report.trackedHousePresent = true;
            report.trackedHouseDefeated = state.defeated;
        } else if (state.combatEligible) {
            report.remainingEnemyOwners.push_back(owner);
        }

        report.owners.push_back(state);
    }

    report.allEnemiesDefeated = report.remainingEnemyOwners.empty();
    report.skirmishRulesActive = !report.missionMap;
    if (report.skirmishRulesActive) {
        report.skirmishDefeat =
            !report.trackedHousePresent || report.trackedHouseDefeated;
        report.skirmishVictory =
            report.trackedHousePresent
            && !report.trackedHouseDefeated
            && report.allEnemiesDefeated;
    }
    report.success = true;
    return report;
}

void printVictoryConditionReport(
    std::ostream& output,
    const VictoryConditionReport& report
) {
    output << "\n========== Victory Sandbox ==========\n";
    if (!report.success) {
        output << "Success: no\n";
        output << "Reason:  " << report.errorMessage << "\n";
        return;
    }

    output << "House:              " << report.trackedHouse << "\n";
    output << "Map:                "
           << (report.mapName.empty() ? "-" : report.mapName)
           << "\n";
    output << "Theater:            " << report.theater << "\n";
    output << "Triggers:           " << report.triggerCount << "\n";
    output << "Mission map:        "
           << (report.missionMap ? "yes" : "no")
           << "\n";
    output << "Skirmish rules:     "
           << (report.skirmishRulesActive ? "yes" : "no")
           << "\n";
    output << "Owners:             " << report.totalOwners << "\n";
    output << "Neutral owners:     " << report.neutralOwners << "\n";
    output << "Active combat sides: " << report.activeCombatOwners << "\n";
    output << "House present:      "
           << (report.trackedHousePresent ? "yes" : "no")
           << "\n";
    output << "House defeated:     "
           << (report.trackedHouseDefeated ? "yes" : "no")
           << "\n";
    output << "Enemies defeated:   "
           << (report.allEnemiesDefeated ? "yes" : "no")
           << "\n";
    output << "Skirmish victory:   "
           << (report.skirmishVictory ? "yes" : "no")
           << "\n";
    output << "Skirmish defeat:    "
           << (report.skirmishDefeat ? "yes" : "no")
           << "\n";

    if (!report.remainingEnemyOwners.empty()) {
        output << "\nRemaining enemies:\n";
        for (const std::string& owner : report.remainingEnemyOwners) {
            output << "- " << owner << "\n";
        }
    }

    output << "\nOwner states:\n";
    for (const VictoryOwnerState& owner : report.owners) {
        output << "- " << owner.owner
               << " | structures=" << owner.structures
               << " units=" << owner.units
               << " infantry=" << owner.infantry
               << " alive=" << owner.aliveObjects
               << " victoryObjects=" << owner.victoryObjects
               << " nonVictoryStructures=" << owner.nonVictoryStructures
               << " neutral=" << (owner.neutral ? "yes" : "no")
               << " combat=" << (owner.combatEligible ? "yes" : "no")
               << " defeated=" << (owner.defeated ? "yes" : "no")
               << "\n";
    }
}
