#include "RuntimeHistoryStageRuleMemo.h"

bool normalizeRuntimeLegacyAmmoWithHistoryQueries(RuntimeWindowState& state) {
    const bool reference = runtimeHistoryQueryReference();
    RuntimeHistoryStageRuleMemo<const UnitType*> unitRules(reference);
    RuntimeHistoryStageRuleMemo<const BuildingType*> buildingRules(reference);
    bool changed = false;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        // Both original normalization branches require this sentinel. Dead
        // objects with a sentinel still normalize, exactly as before.
        if (!reference && object.currentAmmo >= 0) continue;
        const UnitType* unit = unitRules.query(object, [&](const auto& value) {
            return runtimeAmmoRulesForObject(state.rules, value);
        });
        if (unit != nullptr && unit->ammo >= 0 && object.currentAmmo < 0) {
            initializeRuntimeObjectAmmo(object, unit);
            changed = true;
            continue;
        }
        const BuildingType* building = object.kind == WorldObjectKind::Structure
            ? buildingRules.query(object, [&](const auto& value) {
                return findRuntimeBuildingTypeById(state.rules, value.type);
            }) : nullptr;
        if (runtimeInfantryServiceAmmoCapacity(building) >= 0 && object.currentAmmo < 0) {
            initializeRuntimeInfantryServiceAmmo(object, building);
            changed = true;
        }
    }
    return changed;
}

bool runtimeHasSpecialCapabilityForHistoryPhase(const RuntimeWindowState& state) {
    const bool reference = runtimeHistoryQueryReference();
    RuntimeHistoryStageRuleMemo<bool> infiniteMindControl(reference);
    RuntimeHistoryStageRuleMemo<const UnitType*> unitRules(reference);
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.alive && infiniteMindControl.query(object, [&](const auto& value) {
                return runtimeHasInfiniteMindControl(state.rules, value);
            })) return true;
        const UnitType* unit = unitRules.query(object, [&](const auto& value) {
            return findRuntimeUnitType(state.rules, value);
        });
        // The old gate deliberately recognizes dead teleporter/disguise/
        // carrier records. Retain that exact gate and every cleanup below it.
        if (unit && (unit->teleporter || (unit->canDisguise && unit->disguiseWhenStill)
            || (!unit->spawns.empty() && unit->spawnsNumber > 0))) return true;
    }
    return false;
}
