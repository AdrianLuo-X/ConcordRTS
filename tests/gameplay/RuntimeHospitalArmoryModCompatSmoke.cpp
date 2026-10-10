#include "world/model/GameWorld.h"
#include "core/io/IniFile.h"
#include "data/maps/MapDatabase.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/buildings/RuntimeInfantryServiceSystem.h"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Hospital/Armory mod compatibility failure: "
                  << message << '\n';
    }
    return condition;
}

WorldObjectInstance* findObject(GameWorld& world, const std::string& type) {
    for (WorldObjectInstance& object : world.accessObjects()) {
        if (object.type == type) {
            return &object;
        }
    }
    return nullptr;
}

bool loadRules(const std::string& path, IniFile& ini, RulesDatabase& rules) {
    return ini.loadFromFile(path) && rules.loadFromIni(ini);
}

bool stockRulesRemainOptIn(const std::string& path) {
    IniFile ini;
    RulesDatabase rules;
    if (!loadRules(path, ini, rules)) {
        return expect(false, "stock rules could not be loaded");
    }
    for (const std::string& id : {"CAMACH", "CATHOSP", "CAHOSP"}) {
        const BuildingType* building = rules.findBuildingType(id);
        if (!expect(
                building != nullptr
                    && !building->hospital
                    && !building->armory
                    && runtimeInfantryServiceAmmoCapacity(building) == -1,
                id + " unexpectedly opted into legacy entry service"
            )) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) {
        std::cerr << "usage: concord_hospital_armory_mod_compat_smoke "
                     "<rules> <map> [--stock <rulesmd.ini>]\n";
        return 2;
    }

    IniFile rulesIni;
    RulesDatabase rules;
    if (!loadRules(argv[1], rulesIni, rules)) {
        std::cerr << "Hospital/Armory fixture rules failed to load\n";
        return 3;
    }
    IniFile mapIni;
    MapDatabase map;
    GameWorld world;
    if (!mapIni.loadFromFile(argv[2])
        || !map.loadFromIni(mapIni, &rulesIni)
        || !world.loadFromMap(map, &rules)) {
        std::cerr << "Hospital/Armory fixture map failed to load\n";
        return 4;
    }

    const BuildingType* hospitalRules = rules.findBuildingType("MODHOSP");
    const BuildingType* armoryRules = rules.findBuildingType("MODARM");
    const BuildingType* defaultArmoryRules =
        rules.findBuildingType("MODARMDEFAULT");
    const BuildingType* plainRules = rules.findBuildingType("MODPLAIN");
    WorldObjectInstance* hospital = findObject(world, "MODHOSP");
    WorldObjectInstance* armory = findObject(world, "MODARM");
    WorldObjectInstance* defaultArmory = findObject(world, "MODARMDEFAULT");
    WorldObjectInstance* plain = findObject(world, "MODPLAIN");
    WorldObjectInstance* infantry = findObject(world, "MODINF");
    WorldObjectInstance* nontrainable = findObject(world, "MODNOTRAIN");

    bool success = true;
    success &= expect(
        hospitalRules != nullptr && hospitalRules->hospital
            && !hospitalRules->armory && hospitalRules->ammo == 2
            && hospitalRules->buildingCapability.ammo == 2,
        "Hospital/Ammo parsing and capability mirror"
    );
    success &= expect(
        armoryRules != nullptr && armoryRules->armory
            && !armoryRules->hospital && armoryRules->ammo == 2
            && armoryRules->buildingCapability.ammo == 2,
        "Armory/Ammo parsing and capability mirror"
    );
    success &= expect(
        hospital != nullptr && hospital->currentAmmo == 2
            && armory != nullptr && armory->currentAmmo == 2
            && defaultArmory != nullptr && defaultArmory->currentAmmo == 1
            && plain != nullptr && plain->currentAmmo == -1,
        "rules-aware map hydration of finite service charges"
    );
    success &= expect(
        defaultArmoryRules != nullptr
            && runtimeInfantryServiceAmmoCapacity(defaultArmoryRules) == 1
            && plainRules != nullptr
            && runtimeInfantryServiceAmmoCapacity(plainRules) == -1,
        "Armory default-one-use and ordinary-building isolation"
    );
    if (!success || infantry == nullptr || nontrainable == nullptr
        || hospital == nullptr || armory == nullptr || plain == nullptr) {
        return 1;
    }

    const std::uint64_t beforeHealHash =
        world.deterministicSimulationStateHash();
    const RuntimeInfantryServiceResult healed =
        applyRuntimeInfantryService(
            *infantry,
            *hospital,
            hospitalRules,
            100,
            true
        );
    success &= expect(
        healed.accepted && healed.healed && !healed.promoted
            && infantry->health == 100 && hospital->currentAmmo == 1
            && world.deterministicSimulationStateHash() != beforeHealHash,
        "Hospital full-heal, charge consumption, and world hash"
    );

    nontrainable->veterancy = 0;
    nontrainable->veterancyExperience = 77;
    const std::uint64_t beforePromotionHash =
        world.deterministicSimulationStateHash();
    const RuntimeInfantryServiceResult promoted =
        applyRuntimeInfantryService(
            *nontrainable,
            *armory,
            armoryRules,
            120,
            true
        );
    success &= expect(
        promoted.accepted && !promoted.healed && promoted.promoted
            && nontrainable->veterancy == 2
            && nontrainable->veterancyExperience == 0
            && armory->currentAmmo == 1
            && world.deterministicSimulationStateHash()
                != beforePromotionHash,
        "Armory promotes Trainable=no infantry to Elite and is hashed"
    );

    const WorldObjectInstance plainBefore = *plain;
    const WorldObjectInstance infantryBefore = *infantry;
    const RuntimeInfantryServiceResult ignored =
        applyRuntimeInfantryService(
            *infantry,
            *plain,
            plainRules,
            100,
            true
        );
    success &= expect(
        !ignored.accepted
            && ignored.veto
                == RuntimeInfantryServiceVeto::NotAServiceRelationship
            && plain->currentAmmo == plainBefore.currentAmmo
            && infantry->health == infantryBefore.health
            && infantry->veterancy == infantryBefore.veterancy,
        "unset fields preserve ordinary building and infantry behavior"
    );

    hospital->currentAmmo = 0;
    infantry->health = 25;
    const RuntimeInfantryServiceResult exhausted =
        applyRuntimeInfantryService(
            *infantry,
            *hospital,
            hospitalRules,
            100,
            true
        );
    success &= expect(
        !exhausted.accepted
            && exhausted.veto == RuntimeInfantryServiceVeto::NoCharges
            && infantry->health == 25 && hospital->currentAmmo == 0,
        "exhausted Hospital is a no-op"
    );

    if (argc == 5) {
        success &= expect(
            std::string(argv[3]) == "--stock"
                && stockRulesRemainOptIn(argv[4]),
            "stock RA2/YR legacy fields remain opt-in"
        );
    }

    if (success) {
        std::cout << "Hospital/Armory mod compatibility smoke: "
                     "parse=yes hydration=yes hospital=full-heal "
                     "armory=elite charges=yes stock-isolated=yes hash=yes\n";
    }
    return success ? 0 : 1;
}
