#include "simulation/buildings/RuntimeInfantryServiceSystem.h"

#include <algorithm>

RuntimeInfantryServiceEvaluation evaluateRuntimeInfantryService(
    const WorldObjectInstance& infantry,
    const WorldObjectInstance& buildingObject,
    const BuildingType* building,
    int infantryMaximumHealth,
    bool sameOwner
) {
    RuntimeInfantryServiceEvaluation evaluation;
    evaluation.capacity = runtimeInfantryServiceAmmoCapacity(building);
    evaluation.relationship = infantry.kind == WorldObjectKind::Infantry
        && buildingObject.kind == WorldObjectKind::Structure
        && building != nullptr
        && (building->hospital || building->armory)
        && sameOwner;
    if (!evaluation.relationship) {
        evaluation.veto =
            RuntimeInfantryServiceVeto::NotAServiceRelationship;
        return evaluation;
    }
    if (!infantry.alive || infantry.health <= 0) {
        evaluation.veto = RuntimeInfantryServiceVeto::SourceInactive;
        return evaluation;
    }
    if (!buildingObject.alive || buildingObject.health <= 0
        || buildingObject.rubble) {
        evaluation.veto = RuntimeInfantryServiceVeto::TargetInactive;
        return evaluation;
    }
    if (evaluation.capacity <= 0 || buildingObject.currentAmmo <= 0) {
        evaluation.veto = RuntimeInfantryServiceVeto::NoCharges;
        return evaluation;
    }

    evaluation.heals = building->hospital
        && infantry.health < std::max(1, infantryMaximumHealth);
    evaluation.promotes = building->armory && infantry.veterancy < 2;
    if (!evaluation.heals && !evaluation.promotes) {
        evaluation.veto = RuntimeInfantryServiceVeto::NoApplicableEffect;
        return evaluation;
    }

    evaluation.usable = true;
    evaluation.veto = RuntimeInfantryServiceVeto::None;
    return evaluation;
}

RuntimeInfantryServiceResult applyRuntimeInfantryService(
    WorldObjectInstance& infantry,
    WorldObjectInstance& buildingObject,
    const BuildingType* building,
    int infantryMaximumHealth,
    bool sameOwner
) {
    RuntimeInfantryServiceResult result;
    result.healthBefore = infantry.health;
    result.healthAfter = infantry.health;
    result.veterancyBefore = infantry.veterancy;
    result.veterancyAfter = infantry.veterancy;
    result.ammoBefore = buildingObject.currentAmmo;
    result.ammoAfter = buildingObject.currentAmmo;

    const RuntimeInfantryServiceEvaluation evaluation =
        evaluateRuntimeInfantryService(
            infantry,
            buildingObject,
            building,
            infantryMaximumHealth,
            sameOwner
        );
    result.veto = evaluation.veto;
    if (!evaluation.usable) {
        return result;
    }

    if (evaluation.heals) {
        infantry.health = std::max(1, infantryMaximumHealth);
        result.healed = true;
    }
    if (evaluation.promotes) {
        // Armory promotion is unconditional for infantry, including types
        // authored with Trainable=no in the original engine.
        infantry.veterancy = 2;
        infantry.veterancyExperience = 0;
        result.promoted = true;
    }
    buildingObject.currentAmmo = std::max(0, buildingObject.currentAmmo - 1);

    result.accepted = true;
    result.healthAfter = infantry.health;
    result.veterancyAfter = infantry.veterancy;
    result.ammoAfter = buildingObject.currentAmmo;
    result.veto = RuntimeInfantryServiceVeto::None;
    return result;
}

const char* runtimeInfantryServiceVetoName(RuntimeInfantryServiceVeto veto) {
    switch (veto) {
    case RuntimeInfantryServiceVeto::None:
        return "none";
    case RuntimeInfantryServiceVeto::NotAServiceRelationship:
        return "not-service-relationship";
    case RuntimeInfantryServiceVeto::SourceInactive:
        return "source-inactive";
    case RuntimeInfantryServiceVeto::TargetInactive:
        return "target-inactive";
    case RuntimeInfantryServiceVeto::NoCharges:
        return "no-charges";
    case RuntimeInfantryServiceVeto::NoApplicableEffect:
        return "no-applicable-effect";
    }
    return "unknown";
}
