#pragma once

#include "GameWorld.h"
#include "RulesDatabase.h"

#include <algorithm>
#include <limits>

enum class RuntimeInfantryServiceVeto {
    None,
    NotAServiceRelationship,
    SourceInactive,
    TargetInactive,
    NoCharges,
    NoApplicableEffect
};

struct RuntimeInfantryServiceEvaluation {
    bool relationship = false;
    bool usable = false;
    bool heals = false;
    bool promotes = false;
    int capacity = -1;
    RuntimeInfantryServiceVeto veto =
        RuntimeInfantryServiceVeto::NotAServiceRelationship;
};

struct RuntimeInfantryServiceResult {
    bool accepted = false;
    bool healed = false;
    bool promoted = false;
    int healthBefore = 0;
    int healthAfter = 0;
    int veterancyBefore = 0;
    int veterancyAfter = 0;
    int ammoBefore = -1;
    int ammoAfter = -1;
    RuntimeInfantryServiceVeto veto =
        RuntimeInfantryServiceVeto::NotAServiceRelationship;
};

// Returns -1 for an ordinary building, zero for a service building with no
// usable charge, or the finite number of uses. Original Armory logic treats
// its default Ammo=-1 as one use and appears to use the absolute value;
// Hospital requires a positive value.
inline int runtimeInfantryServiceAmmoCapacity(
    const BuildingType* building
) {
    if (building == nullptr || (!building->hospital && !building->armory)) {
        return -1;
    }
    if (building->ammo > 0) {
        return building->ammo;
    }
    if (!building->armory || building->ammo == 0) {
        return 0;
    }
    const long long magnitude = -static_cast<long long>(building->ammo);
    return static_cast<int>(std::min<long long>(
        magnitude,
        std::numeric_limits<int>::max()
    ));
}

inline void initializeRuntimeInfantryServiceAmmo(
    WorldObjectInstance& object,
    const BuildingType* building
) {
    object.currentAmmo = runtimeInfantryServiceAmmoCapacity(building);
}

RuntimeInfantryServiceEvaluation evaluateRuntimeInfantryService(
    const WorldObjectInstance& infantry,
    const WorldObjectInstance& buildingObject,
    const BuildingType* building,
    int infantryMaximumHealth,
    bool sameOwner
);

RuntimeInfantryServiceResult applyRuntimeInfantryService(
    WorldObjectInstance& infantry,
    WorldObjectInstance& buildingObject,
    const BuildingType* building,
    int infantryMaximumHealth,
    bool sameOwner
);

const char* runtimeInfantryServiceVetoName(RuntimeInfantryServiceVeto veto);
