#include "GameWorld.h"
#include "IniFile.h"
#include "MapDatabase.h"
#include "RulesDatabase.h"
#include "RuntimeAircraftServiceSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Aircraft service smoke failure: " << message << '\n';
    }
    return condition;
}

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

WorldObjectInstance makeHost(
    int id,
    const std::string& type = "AIRFIELD",
    const std::string& owner = "Alpha"
) {
    WorldObjectInstance host;
    host.instanceId = id;
    host.kind = WorldObjectKind::Structure;
    host.owner = owner;
    host.type = type;
    host.health = 1000;
    host.alive = true;
    host.poweredOperational = true;
    return host;
}

WorldObjectInstance makeAircraft(
    int id,
    int hostId,
    int dockIndex,
    int ammo,
    int health = 100,
    const std::string& type = "JET",
    const std::string& owner = "Alpha"
) {
    WorldObjectInstance aircraft;
    aircraft.instanceId = id;
    aircraft.kind = WorldObjectKind::Unit;
    aircraft.owner = owner;
    aircraft.type = type;
    aircraft.health = health;
    aircraft.alive = true;
    aircraft.authoredAircraft = true;
    aircraft.airborne = false;
    aircraft.dockedToObjectId = hostId;
    aircraft.dockIndex = dockIndex;
    aircraft.currentAmmo = ammo;
    return aircraft;
}

RuntimeAircraftServiceContext makeContext(
    const RulesDatabase& rules,
    const std::vector<WorldObjectInstance>& objects,
    std::map<std::string, int>& credits
) {
    RuntimeAircraftServiceContext context;
    context.rules = &rules;
    context.objects = &objects;
    context.areAllied = [](const std::string& left, const std::string& right) {
        return lowerAscii(left) == lowerAscii(right);
    };
    context.canonicalOwnerId = [](const std::string& owner) {
        return lowerAscii(owner);
    };
    context.creditBalance = [&credits](const std::string& owner) {
        const auto found = credits.find(lowerAscii(owner));
        return found == credits.end() ? 0 : found->second;
    };
    return context;
}

WorldObjectInstance* findObject(
    std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    for (WorldObjectInstance& object : objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

void applyActions(
    std::vector<WorldObjectInstance>& objects,
    std::map<std::string, int>& credits,
    const std::vector<RuntimeAircraftServiceAction>& actions
) {
    for (const RuntimeAircraftServiceAction& action : actions) {
        WorldObjectInstance* aircraft = findObject(objects, action.aircraftId);
        if (aircraft == nullptr) {
            continue;
        }
        if (action.kind == RuntimeAircraftServiceActionKind::ReloadAmmo) {
            aircraft->currentAmmo = action.ammoAfter;
        } else {
            aircraft->health = action.healthAfter;
            credits[lowerAscii(action.owner)] -= action.creditCost;
        }
    }
}

std::vector<RuntimeAircraftServiceAction> actionsOfKind(
    const std::vector<RuntimeAircraftServiceAction>& actions,
    RuntimeAircraftServiceActionKind kind
) {
    std::vector<RuntimeAircraftServiceAction> filtered;
    std::copy_if(
        actions.begin(),
        actions.end(),
        std::back_inserter(filtered),
        [kind](const RuntimeAircraftServiceAction& action) {
            return action.kind == kind;
        }
    );
    return filtered;
}

const WorldObjectInstance* findWorldObject(
    const GameWorld& world,
    const std::string& type,
    bool authoredAircraft
) {
    for (const WorldObjectInstance& object : world.getObjects()) {
        if (object.type == type
            && object.authoredAircraft == authoredAircraft) {
            return &object;
        }
    }
    return nullptr;
}

bool testRulesAndMapInitialization(
    const IniFile& rulesIni,
    const RulesDatabase& rules,
    const std::string& mapPath
) {
    bool success = true;
    const UnitType* defaultAircraft = rules.findUnitType(
        "Aircraft",
        "DEFAULTAIR"
    );
    const UnitType* defaultVehicle = rules.findUnitType(
        "Vehicle",
        "DEFAULTVEH"
    );
    const UnitType* jet = rules.findUnitType("Aircraft", "JET");
    const UnitType* clamp = rules.findUnitType("Aircraft", "CLAMPAIR");
    const UnitType* noReload = rules.findUnitType(
        "Aircraft",
        "NORELOADAIR"
    );
    const UnitType* explicitVehicle = rules.findUnitType(
        "Vehicle",
        "EXPLICITVEH"
    );
    success &= expect(
        defaultAircraft != nullptr
            && defaultAircraft->ammo == -1
            && defaultAircraft->initialAmmo == -1
            && defaultAircraft->manualReload,
        "stock-like aircraft defaults"
    );
    success &= expect(
        defaultVehicle != nullptr
            && defaultVehicle->ammo == -1
            && defaultVehicle->initialAmmo == -1
            && !defaultVehicle->manualReload,
        "non-aircraft ManualReload default"
    );
    success &= expect(
        jet != nullptr
            && jet->ammo == 4
            && jet->initialAmmo == 2
            && jet->manualReload,
        "aircraft Ammo/InitialAmmo parse"
    );
    success &= expect(
        clamp != nullptr && clamp->ammo == 3 && clamp->initialAmmo == 99,
        "InitialAmmo remains authored until instance hydration"
    );
    success &= expect(
        noReload != nullptr && !noReload->manualReload,
        "ManualReload=no parse"
    );
    success &= expect(
        explicitVehicle != nullptr && explicitVehicle->manualReload,
        "explicit non-aircraft ManualReload=yes parse"
    );

    IniFile mapIni;
    success &= expect(mapIni.loadFromFile(mapPath), "map fixture load");
    MapDatabase map;
    success &= expect(
        map.loadFromIni(mapIni, &rulesIni),
        "map database load"
    );
    GameWorld world;
    success &= expect(
        world.loadFromMap(map, &rules),
        "rules-aware GameWorld load"
    );
    const WorldObjectInstance* unitJet = findWorldObject(world, "JET", false);
    const WorldObjectInstance* unitVehicle = findWorldObject(
        world,
        "VEH",
        false
    );
    const WorldObjectInstance* aircraftJet = findWorldObject(
        world,
        "JET",
        true
    );
    const WorldObjectInstance* clampAircraft = findWorldObject(
        world,
        "CLAMPAIR",
        true
    );
    const WorldObjectInstance* unlimitedAircraft = findWorldObject(
        world,
        "UNLIMITEDAIR",
        true
    );
    const WorldObjectInstance* noReloadAircraft = findWorldObject(
        world,
        "NORELOADAIR",
        true
    );
    success &= expect(
        unitJet != nullptr && unitJet->currentAmmo == 2,
        "[Units] vehicle-first AircraftTypes fallback hydration"
    );
    success &= expect(
        unitVehicle != nullptr && unitVehicle->currentAmmo == 1,
        "vehicle finite ammo hydration"
    );
    success &= expect(
        aircraftJet != nullptr && aircraftJet->currentAmmo == 2,
        "aircraft InitialAmmo hydration"
    );
    success &= expect(
        clampAircraft != nullptr && clampAircraft->currentAmmo == 3,
        "InitialAmmo clamps to Ammo"
    );
    success &= expect(
        unlimitedAircraft != nullptr && unlimitedAircraft->currentAmmo == -1,
        "unlimited ammo sentinel hydration"
    );
    success &= expect(
        noReloadAircraft != nullptr && noReloadAircraft->currentAmmo == 1,
        "ManualReload does not alter initial magazine"
    );

    GameWorld legacyWorld;
    success &= expect(legacyWorld.loadFromMap(map), "legacy world load");
    const WorldObjectInstance* legacyJet = findWorldObject(
        legacyWorld,
        "JET",
        true
    );
    success &= expect(
        legacyJet != nullptr && legacyJet->currentAmmo == -1,
        "rules-absent load preserves unlimited sentinel"
    );
    return success;
}

bool testReloadOrdering(const RulesDatabase& rules) {
    bool success = true;
    std::vector<WorldObjectInstance> objects{
        makeAircraft(13, 100, 3, 3),
        makeHost(100),
        makeAircraft(11, 100, 1, 1),
        makeAircraft(10, 100, 0, 0),
        makeAircraft(12, 100, 2, 2)
    };
    std::map<std::string, int> credits{{"alpha", 1000}};
    RuntimeAircraftServiceContext context = makeContext(rules, objects, credits);
    RuntimeAircraftServiceSystemState service(10);
    const RuntimeAircraftServiceReconcileReport report =
        service.reconcile(context);
    success &= expect(
        report.contextValid
            && service.hosts().size() == 1U
            && service.dockBindings().size() == 4U,
        "four pads reconcile"
    );
    for (std::size_t index = 0; index < service.dockBindings().size(); ++index) {
        success &= expect(
            service.dockBindings()[index].dockIndex == static_cast<int>(index)
                && service.dockBindings()[index].aircraftId
                    == 10 + static_cast<int>(index),
            "binding order host/dock/aircraft"
        );
    }

    std::vector<RuntimeAircraftServiceAction> actions = service.tick(context, 0);
    success &= expect(
        actions.empty() && service.remainingReloadTicks(100) == 2,
        "reload timer arms without an immediate pulse"
    );
    actions = service.tick(context, 2);
    std::vector<RuntimeAircraftServiceAction> reloads = actionsOfKind(
        actions,
        RuntimeAircraftServiceActionKind::ReloadAmmo
    );
    success &= expect(
        reloads.size() == 1U
            && reloads[0].aircraftId == 10
            && reloads[0].ammoBefore == 0
            && reloads[0].ammoAfter == 1,
        "zero-ammo first aircraft gets the whole first pulse"
    );
    applyActions(objects, credits, actions);

    actions = service.tick(context, 4);
    reloads = actionsOfKind(
        actions,
        RuntimeAircraftServiceActionKind::ReloadAmmo
    );
    success &= expect(reloads.size() == 4U, "next pulse reloads in parallel");
    for (std::size_t index = 0; index < reloads.size(); ++index) {
        success &= expect(
            reloads[index].aircraftId == 10 + static_cast<int>(index)
                && reloads[index].ammoAfter
                    == std::min(4, reloads[index].ammoBefore + 1),
            "parallel reload order and clamp"
        );
    }
    applyActions(objects, credits, actions);
    for (const int tick : {6, 8, 10}) {
        actions = service.tick(context, tick);
        applyActions(objects, credits, actions);
    }
    success &= expect(
        findObject(objects, 10)->currentAmmo == 4
            && findObject(objects, 11)->currentAmmo == 4
            && findObject(objects, 12)->currentAmmo == 4
            && findObject(objects, 13)->currentAmmo == 4,
        "reload clamps every magazine at capacity"
    );
    success &= expect(
        service.tick(context, 12).empty(),
        "full magazines do not emit service actions"
    );
    return success;
}

bool testRepairCreditsAndPower(const RulesDatabase& rules) {
    bool success = true;
    std::vector<WorldObjectInstance> objects{
        makeHost(100),
        makeAircraft(10, 100, 0, 4, 80),
        makeAircraft(11, 100, 1, 4, 80)
    };
    std::map<std::string, int> credits{{"alpha", 15}};
    RuntimeAircraftServiceContext context = makeContext(rules, objects, credits);
    RuntimeAircraftServiceSystemState service(10);
    std::vector<RuntimeAircraftServiceAction> actions = service.tick(context, 0);
    const std::vector<RuntimeAircraftServiceAction> repairs = actionsOfKind(
        actions,
        RuntimeAircraftServiceActionKind::Repair
    );
    const int cost = std::accumulate(
        repairs.begin(),
        repairs.end(),
        0,
        [](int total, const RuntimeAircraftServiceAction& action) {
            return total + action.creditCost;
        }
    );
    success &= expect(
        repairs.size() == 2U
            && repairs[0].aircraftId == 10
            && repairs[0].healedHitPoints == 10
            && repairs[0].creditCost == 10
            && repairs[1].aircraftId == 11
            && repairs[1].healedHitPoints == 5
            && repairs[1].creditCost == 5
            && cost == 15,
        "repair uses ordered shadow credit ledger"
    );
    applyActions(objects, credits, actions);
    success &= expect(
        credits["alpha"] == 0
            && findObject(objects, 10)->health == 90
            && findObject(objects, 11)->health == 85,
        "repair actions apply health and owner credit cost"
    );

    credits["alpha"] = 100;
    success &= expect(service.tick(context, 1).empty(), "repair cooldown tick");
    findObject(objects, 100)->poweredOperational = false;
    success &= expect(service.tick(context, 3).empty(), "power outage pauses service");
    success &= expect(
        service.remainingRepairTicks(100) == 1,
        "power outage does not consume repair timer"
    );
    findObject(objects, 100)->poweredOperational = true;
    actions = service.tick(context, 4);
    success &= expect(
        actionsOfKind(actions, RuntimeAircraftServiceActionKind::Repair).size()
            == 2U,
        "repair resumes from paused timer after power returns"
    );
    return success;
}

bool testEligibilityAndReconcile(const RulesDatabase& rules) {
    bool success = true;
    std::vector<WorldObjectInstance> objects{
        makeHost(100),
        makeHost(101, "WRONGHOST"),
        makeAircraft(30, 100, 0, 1),
        makeAircraft(20, 100, 0, 1),
        makeAircraft(40, 101, 0, 1),
        makeAircraft(50, 100, 1, 1, 100, "JET", "Enemy"),
        makeAircraft(60, 100, 2, 1),
        makeAircraft(70, 100, 3, 0, 100, "NORELOADAIR")
    };
    findObject(objects, 60)->airborne = true;
    std::map<std::string, int> credits{{"alpha", 1000}, {"enemy", 1000}};
    RuntimeAircraftServiceContext context = makeContext(rules, objects, credits);
    RuntimeAircraftServiceSystemState service(10);
    RuntimeAircraftServiceReconcileReport report = service.reconcile(context);
    success &= expect(
        report.duplicateDockBindings == 1U
            && service.hosts().size() == 1U
            && service.dockBindings().size() == 2U
            && service.dockBindings()[0].aircraftId == 20
            && service.dockBindings()[1].aircraftId == 70,
        "duplicate pad keeps lowest id; enemy/airborne/wrong host reject"
    );
    service.tick(context, 0);
    success &= expect(
        service.tick(context, 2).size() == 1U
            && service.tick(context, 3).empty(),
        "ManualReload=no aircraft is not reloaded"
    );

    findObject(objects, 20)->dockIndex = 1;
    report = service.reconcile(context);
    success &= expect(
        service.dockBindings().size() == 3U
            && report.addedBindings >= 1U,
        "dock movement reconciles from world mirror"
    );
    findObject(objects, 100)->type = "AIRFIELDALT";
    report = service.reconcile(context);
    success &= expect(report.resetHosts == 1U, "host type change resets timers");
    findObject(objects, 100)->owner = "Captured";
    report = service.reconcile(context);
    success &= expect(
        report.resetHosts == 1U && service.dockBindings().empty(),
        "host capture resets service and rejects non-allied aircraft"
    );
    findObject(objects, 100)->alive = false;
    report = service.reconcile(context);
    success &= expect(
        report.removedHosts == 1U && service.hosts().empty(),
        "dead host is removed"
    );
    return success;
}

bool testPadAircraftVehicleCompatibility() {
    bool success = true;
    IniFile rulesIni;
    success &= expect(
        rulesIni.loadFromString(
            "[General]\n"
            "PadAircraft= pAdVeH, dUaL\n"
            "ReloadRate=.001\n"
            "RepairPercent=10%\n"
            "RepairStep=10\n"
            "[InfantryTypes]\n"
            "[VehicleTypes]\n"
            "0=PADVEH\n"
            "1=ROADVEH\n"
            "2=DUAL\n"
            "3=BLOCKEDDUAL\n"
            "[AircraftTypes]\n"
            "0=DUAL\n"
            "1=BLOCKEDDUAL\n"
            "[BuildingTypes]\n"
            "0=AIRFIELD\n"
            "[OverlayTypes]\n"
            "[Tiberiums]\n"
            "[TerrainTypes]\n"
            "[Warheads]\n"
            "[PADVEH]\n"
            "Strength=100\n"
            "Cost=100\n"
            "Ammo=3\n"
            "ManualReload=yes\n"
            "[ROADVEH]\n"
            "Strength=100\n"
            "Cost=100\n"
            "Ammo=3\n"
            "ManualReload=yes\n"
            "[DUAL]\n"
            "Strength=100\n"
            "Cost=100\n"
            "Ammo=3\n"
            "[BLOCKEDDUAL]\n"
            "Strength=100\n"
            "Cost=100\n"
            "Ammo=3\n"
            "[AIRFIELD]\n"
            "Strength=1000\n"
            "UnitReload=yes\n"
            "NumberOfDocks=5\n"
        ),
        "PadAircraft vehicle compatibility rules parse"
    );
    RulesDatabase rules;
    success &= expect(
        rules.loadFromIni(rulesIni),
        "PadAircraft vehicle compatibility rules load"
    );
    if (!success) {
        return false;
    }

    std::vector<WorldObjectInstance> objects{
        makeHost(100),
        makeAircraft(10, 100, 0, 0, 80, "PADVEH"),
        makeAircraft(11, 100, 1, 0, 80, "ROADVEH"),
        makeAircraft(12, 100, 2, 0, 80, "DUAL"),
        makeAircraft(13, 100, 3, 0, 80, "DUAL"),
        makeAircraft(14, 100, 4, 0, 80, "BLOCKEDDUAL")
    };
    for (const int vehicleId : {10, 11, 12, 14}) {
        findObject(objects, vehicleId)->authoredAircraft = false;
    }

    std::map<std::string, int> credits{{"alpha", 1000}};
    RuntimeAircraftServiceContext context = makeContext(
        rules,
        objects,
        credits
    );
    RuntimeAircraftServiceSystemState service(10);
    const RuntimeAircraftServiceReconcileReport report =
        service.reconcile(context);
    success &= expect(
        report.contextValid
            && service.dockBindings().size() == 3U
            && service.dockBindings()[0].aircraftId == 10
            && service.dockBindings()[1].aircraftId == 12
            && service.dockBindings()[2].aircraftId == 13,
        "PadAircraft admits listed VehicleTypes, rejects unlisted vehicles, "
        "and preserves exact duplicate namespace identity"
    );

    std::vector<RuntimeAircraftServiceAction> actions =
        service.tick(context, 0);
    std::vector<RuntimeAircraftServiceAction> repairs = actionsOfKind(
        actions,
        RuntimeAircraftServiceActionKind::Repair
    );
    success &= expect(
        repairs.size() == 3U
            && repairs[0].aircraftId == 10
            && repairs[1].aircraftId == 12
            && repairs[2].aircraftId == 13,
        "VehicleTypes PadAircraft participates in ordered repair service"
    );
    applyActions(objects, credits, actions);

    actions = service.tick(context, 1);
    std::vector<RuntimeAircraftServiceAction> reloads = actionsOfKind(
        actions,
        RuntimeAircraftServiceActionKind::ReloadAmmo
    );
    success &= expect(
        reloads.size() == 1U && reloads[0].aircraftId == 10,
        "listed VehicleTypes ManualReload rule drives the first reload pulse"
    );
    applyActions(objects, credits, actions);

    actions = service.tick(context, 2);
    reloads = actionsOfKind(
        actions,
        RuntimeAircraftServiceActionKind::ReloadAmmo
    );
    success &= expect(
        reloads.size() == 2U
            && reloads[0].aircraftId == 10
            && reloads[1].aircraftId == 13,
        "duplicate type uses VehicleTypes defaults for Unit records and "
        "AircraftTypes defaults for explicit aircraft"
    );
    return success;
}

bool testSaveLoadAndAtomicFailure(const RulesDatabase& rules) {
    bool success = true;
    std::vector<WorldObjectInstance> objects{
        makeHost(100),
        makeAircraft(10, 100, 0, 1),
        makeAircraft(11, 100, 1, 2)
    };
    std::map<std::string, int> credits{{"alpha", 1000}};
    RuntimeAircraftServiceContext context = makeContext(rules, objects, credits);
    RuntimeAircraftServiceSystemState service(10);
    service.tick(context, 0);
    service.tick(context, 1);
    success &= expect(
        service.remainingReloadTicks(100) == 1
            && service.lastAdvancedTick() == 1,
        "mid-reload timer setup"
    );

    std::ostringstream savedOutput;
    service.save(savedOutput);
    const std::string saved = savedOutput.str();
    IniFile savedIni;
    success &= expect(savedIni.loadFromString(saved), "saved state parses");
    RuntimeAircraftServiceSystemState restored;
    std::string error;
    success &= expect(
        restored.load(savedIni, context, &error),
        "saved state reloads: " + error
    );
    success &= expect(
        restored.remainingReloadTicks(100) == 1
            && restored.lastAdvancedTick() == 1
            && restored.stateHash() == service.stateHash(),
        "mid timers and hash roundtrip"
    );

    const std::uint64_t atomicHash = restored.stateHash();
    std::string duplicate = saved;
    const std::size_t bindingOne = duplicate.find(
        "[AircraftServiceRuntime.Binding.1]"
    );
    const std::size_t duplicateDock = duplicate.find(
        "DockIndex=1",
        bindingOne
    );
    success &= expect(
        bindingOne != std::string::npos
            && duplicateDock != std::string::npos,
        "duplicate corruption edit point"
    );
    if (duplicateDock != std::string::npos) {
        duplicate.replace(duplicateDock, std::string("DockIndex=1").size(),
            "DockIndex=0");
    }
    IniFile duplicateIni;
    success &= expect(duplicateIni.loadFromString(duplicate), "bad save parses");
    success &= expect(
        !restored.load(duplicateIni, context, &error)
            && restored.stateHash() == atomicHash,
        "duplicate saved pad is rejected transactionally"
    );

    std::string overCap = saved;
    const std::size_t countPosition = overCap.find("BindingCount=2");
    if (countPosition != std::string::npos) {
        overCap.replace(
            countPosition,
            std::string("BindingCount=2").size(),
            "BindingCount=16385"
        );
    }
    IniFile overCapIni;
    success &= expect(overCapIni.loadFromString(overCap), "cap save parses");
    success &= expect(
        !restored.load(overCapIni, context, &error)
            && restored.stateHash() == atomicHash,
        "binding cap is strict and atomic"
    );
    return success;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: concord_aircraft_service_system_smoke "
                     "<rules.ini> <map.map>\n";
        return 2;
    }

    IniFile rulesIni;
    bool success = expect(
        rulesIni.loadFromFile(argv[1]),
        "rules fixture load"
    );
    RulesDatabase rules;
    success &= expect(rules.loadFromIni(rulesIni), "rules database load");
    if (!success) {
        return 1;
    }

    success &= testRulesAndMapInitialization(rulesIni, rules, argv[2]);
    success &= testReloadOrdering(rules);
    success &= testRepairCreditsAndPower(rules);
    success &= testEligibilityAndReconcile(rules);
    success &= testPadAircraftVehicleCompatibility();
    success &= testSaveLoadAndAtomicFailure(rules);
    return success ? 0 : 1;
}
