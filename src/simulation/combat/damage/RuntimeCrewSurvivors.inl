#include "simulation/combat/damage/RuntimeCrewSurvivors.h"

struct RuntimeCrewSurvivorSpawn {
    std::string owner;
    std::string type;
    RuntimeCellPoint cell;
    RuntimeCellPoint scatter;
    int health = 1;
    bool selected = false;
};

int runtimeCrewSide(const RuntimeWindowState& state, const std::string& owner) {
    const auto country = toUpperAsciiLocal(trimAsciiLocal(runtimeCountryForOwner(state, owner)));
    const auto& sides = state.rules.getSides();
    for (std::size_t side = 0; side < std::min<std::size_t>(3, sides.size()); ++side)
        for (const auto& member : sides[side].members)
            if (toUpperAsciiLocal(trimAsciiLocal(member)) == country) return static_cast<int>(side);
    return runtimeBasePlanningSideForCountry(country);
}

std::vector<RuntimeCrewSurvivorSpawn> prepareRuntimeCrewSurvivors(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeObjectExitRequest& request) {
    std::vector<RuntimeCrewSurvivorSpawn> result;
    const bool sold = request.kind == RuntimeObjectExitKind::Sold;
    const bool structure = object.kind == WorldObjectKind::Structure;
    if ((!sold && request.kind != RuntimeObjectExitKind::Destroyed) || request.suppressCrew
        || !object.alive || object.rubble || object.garrisonHostObjectId >= 0
        || state.multiplayerDefeatedOwners.contains(runtimeCanonicalHumanHouseOwner(object.owner))) return result;
    const auto* building = structure ? findRuntimeBuildingTypeById(state.rules, object.type) : nullptr;
    // The retail vehicle branch excludes transports (Passengers != 0),
    // aircraft, and Crewed=no. Passengers are separate world objects.
    const auto* vehicle = object.kind == WorldObjectKind::Unit && !object.authoredAircraft
        ? state.rules.findUnitType("UnitType", object.type) : nullptr;
    if (structure ? (!building || !building->crewed)
        : (sold || !vehicle || !vehicle->crewed || vehicle->passengers != 0)) return result;

    const auto& rules = state.rules.getCrewSurvivorRules();
    const int side = runtimeCrewSide(state, object.owner);
    int remaining = structure ? runtimeBuildingCrewCount(building, rules,
        state.rules.getRefundPercent(), side, object.hasBeenCaptured) : 1;
    if (remaining <= 0) return result;
    RuntimeCrewRandom random{runtimeCombatRandomMix(state.config.simulationRandomSeed)
        ^ runtimeCombatRandomMix(static_cast<std::uint32_t>(object.instanceId))
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(request.tick)) << 32)
        ^ 0x4352455753555256ULL};
    if (!structure && !random.chance(rules.escapeChance)) return result;
    auto cells = structure ? runtimeStructureFootprintCellsForObject(state, object)
        : std::vector<RuntimeCellPoint>{runtimeCellPointForObject(object)};
    if (cells.empty()) return result;
    const bool armed = building ? (!building->primaryWeapon.empty() || !building->secondaryWeapon.empty())
        : (!vehicle->primaryWeapon.empty() || !vehicle->secondaryWeapon.empty());
    const bool constructionYard = building && toUpperAsciiLocal(trimAsciiLocal(building->factory)) == "BUILDINGTYPE";
    const bool selected = !structure && std::find(state.selectedObjectIds.begin(), state.selectedObjectIds.end(),
        object.instanceId) != state.selectedObjectIds.end();
    bool engineerReturned = false;
    const auto append = [&](RuntimeCellPoint cell) {
        // Selling a construction yard may replace one crew member with an
        // engineer. The retail sale loop allows at most one; capture disables it.
        std::string type;
        if (constructionYard && !object.hasBeenCaptured && (!sold || !engineerReturned)
            && random.range(0, 99) < 25) type = rules.engineer;
        else type = side < 0 || (armed && random.range(0, 99) < 15)
            ? rules.technician : rules.crewTypes[side];
        const auto* infantry = state.rules.findUnitType("InfantryType", type);
        if (!infantry) return;
        const int maximum = std::max(1, infantry->strength);
        const int woundedMaximum = structure ? maximum : std::max(1, maximum / 2);
        const int health = sold ? maximum : random.range(std::min(5, woundedMaximum), woundedMaximum);
        RuntimeCellPoint offset = formationOffsetForIndex(random.range(1, 8), 1);
        result.push_back({object.owner, type, cell, {cell.x + offset.x * 2, cell.y + offset.y * 2}, health, selected});
        engineerReturned = engineerReturned || infantry->engineer;
        --remaining;
    };
    if (sold) {
        const int count = remaining;
        for (int i = 0; i < count; ++i) append(cells[random.range(0, static_cast<int>(cells.size()) - 1)]);
    } else if (structure) {
        // Building destruction rolls once per foundation cell, up to the
        // same crew limit. C4 and capture modify this independently of CrewEscape.
        const auto bomb = state.specialMechanisms.timedBombs().find(object.instanceId);
        const bool c4 = request.c4Explosion || (bomb != state.specialMechanisms.timedBombs().end()
            && bomb->second.kind == TimedBombKind::C4);
        const int upper = (c4 ? 1 : 2) + (object.hasBeenCaptured ? 6 : 0);
        for (const auto& cell : cells) {
            if (remaining <= 0) break;
            if (random.range(0, upper) == 1) append(cell);
        }
    } else append(cells.front());
    return result;
}

void spawnRuntimeCrewSurvivors(RuntimeWindowState& state,
    const std::vector<RuntimeCrewSurvivorSpawn>& survivors) {
    // This runs after the source has released its footprint and all pointer
    // users in object finalization have finished; additions can grow the vector.
    for (const auto& survivor : survivors) {
        WorldObjectInstance probe;
        probe.instanceId = nextRuntimeObjectInstanceId(state);
        probe.kind = WorldObjectKind::Infantry; probe.type = survivor.type;
        probe.owner = survivor.owner; probe.x = survivor.cell.x; probe.y = survivor.cell.y;
        const auto cell = findRuntimePassableCellNear(state, probe, survivor.cell, 0);
        if (!cell) continue;
        auto* infantry = addRuntimeObjectInstanceForOwner(state, survivor.owner,
            RuntimeBuildKind::Infantry, survivor.type, *cell, std::nullopt, false);
        if (!infantry) continue;
        infantry->health = survivor.health;
        state.cloakObservedHealthByObject[infantry->instanceId] = infantry->health;
        if (survivor.selected) state.selectedObjectIds.push_back(infantry->instanceId);
        const auto scatter = findRuntimePassableCellNear(state, *infantry, survivor.scatter, 1);
        if (scatter && *scatter != *cell) {
            RuntimeObjectBehavior behavior;
            behavior.kind = RuntimeBehaviorKind::Move;
            behavior.state = RuntimeBehaviorState::Queued;
            behavior.moveTarget = *scatter;
            state.behaviors[infantry->instanceId] = std::move(behavior);
        }
        invalidateRuntimeTickOccupancyCache(state);
        state.navigationRefreshPending = true;
    }
}
