#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

// Exercise both routes of the actual pose helper, including dormant render
// fields that must remain byte-identical when the original helper is a no-op.
bool runRuntimeJumpjetHistoryPoseContract(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "jumpjet history pose equivalence: " + why;
        return false;
    };
    const auto pose = [](const WorldObjectInstance& object) {
        return std::make_tuple(object.instanceId, object.hasRenderPosition, object.renderMoveActive,
            std::bit_cast<std::uint64_t>(object.renderX), std::bit_cast<std::uint64_t>(object.renderY),
            std::bit_cast<std::uint64_t>(object.renderLevel), object.flightLevel, object.level,
            object.onBridge, object.renderOnBridge, object.subCell);
    };
    const std::string unicodeType = "\xE5\x8D\x95\xE4\xBD\x8D";
    const auto loadRules = [&](RuntimeWindowState& state, bool noWobbles) {
        IniFile ini;
        const auto profile = [&](const std::string& type, const std::string& extra = std::string()) {
            return "[" + type + "]\nStrength=100\nSpeed=5\nJumpJet=yes\nSpeedType=Winged\nMovementZone=Fly\n"
                "JumpjetHeight=500\nJumpjetSpeed=12\nJumpjetClimb=5\nJumpjetAccel=2\n"
                "JumpjetTurnRate=5\nJumpjetWobbles=128\nJumpjetDeviation=64\nJumpjetNoWobbles="
                + std::string(noWobbles ? "yes\n" : "no\n") + extra;
        };
        const std::string text = "[InfantryTypes]\n0=FLIER\n1=PLAIN\n2=SPAWN\n3=MISSILE\n4="
            + unicodeType + "\n[VehicleTypes]\n0=FLIER\n"
            + profile("FLIER") + profile(unicodeType) + profile("SPAWN", "Spawned=yes\n")
            + profile("MISSILE", "MissileSpawn=yes\n")
            + "[PLAIN]\nStrength=100\nSpeed=5\nSpeedType=Foot\nMovementZone=Normal\n";
        if (!ini.loadFromString(text) || !state.rules.loadFromIni(ini)) return false;
        // The same type string in the vehicle namespace must not inherit the
        // infantry namespace's flight qualification.
        auto* vehicle = const_cast<UnitType*>(state.rules.findUnitType("Vehicle", "FLIER"));
        const auto* infantry = state.rules.findUnitType("Infantry", "FLIER");
        if (!vehicle || !infantry || !runtimeUnitUsesJumpjetMovement(*infantry)) return false;
        vehicle->jumpJet = false;
        state.rulesLoaded = true;
        return true;
    };
    const auto makeState = [&](RuntimeWindowState& state) {
        if (!loadRules(state, false)) return false;
        state.tick = 17; state.config.fixedTickHz = 30;
        state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSoundsEnabled = false;
        for (int y = 0; y != 8; ++y) for (int x = 0; x != 8; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.level = (x + y) % 3;
            cell.cellIndex512 = runtimeCellKey(x, y);
            state.world.accessTerrainCells().push_back(cell);
        }
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        for (int id = 1; id <= 16; ++id) {
            WorldObjectInstance object; object.instanceId = id;
            object.kind = WorldObjectKind::Infantry; object.type = "FLIER";
            object.owner = "Player"; object.alive = true; object.health = 100;
            object.x = 2; object.y = 3; object.cell = runtimeCellKey(2, 3);
            object.airborne = true; object.mission = "Guard";
            object.hasRenderPosition = false; object.renderMoveActive = false;
            object.renderX = -13.25; object.renderY = -0.0; object.renderLevel = -2.5;
            object.flightLevel = 9; object.level = 8; object.subCell = 3;
            object.onBridge = object.renderOnBridge = true;
            if (id == 2 || id == 15) { object.alive = false; object.health = 0; }
            if (id == 3) object.airborne = false;
            if (id == 5) object.type = "PLAIN";
            if (id == 6) object.health = 0; // Alive/zero-health still receives the original pose.
            if (id == 7) object.type = "SPAWN";
            if (id == 8) object.type = "MISSILE";
            if (id == 9) object.type.clear();
            if (id == 10 || id == 16) object.type = unicodeType;
            if (id == 11) object.mission = "SupportFlightEast";
            if (id == 13) object.kind = WorldObjectKind::Unit;
            if (id == 14 || id == 15) object.type = "MISSING";
            state.world.accessObjects().push_back(object);
            if (id == 4 || id == 16) continue;
            RuntimeJumpjetFlight flight;
            flight.height = 2 * runtimeJumpjetCellQ16 + 500 * runtimeJumpjetQ16;
            flight.fromX = 2; flight.fromY = 3; flight.toX = 3; flight.toY = 3;
            flight.length = runtimeJumpjetCellQ16; flight.progress = flight.length / 4;
            flight.wobblePhase = 16384; flight.active = id != 12;
            flight.lastTick = 16; flight.verticalTick = 16;
            state.jumpjetFlights.emplace(id, flight);
        }
        state.jumpjetFlights.emplace(999999, RuntimeJumpjetFlight{}); // Orphan state is untouched by pose application.
        for (int index = 0; index != 10000; ++index) {
            WorldObjectInstance dead; dead.instanceId = 1000 + index;
            dead.kind = WorldObjectKind::Unit; dead.type = index % 2 ? "FLIER" : "MISSING";
            dead.alive = false; dead.health = 0; dead.airborne = index % 3 == 0;
            dead.hasRenderPosition = true; dead.renderX = -0.0; dead.renderY = 7.25;
            dead.renderLevel = 4.5; dead.onBridge = dead.renderOnBridge = true;
            state.world.accessObjects().push_back(std::move(dead));
        }
        return true;
    };
    auto referenceStorage = std::make_unique<RuntimeWindowState>();
    auto optimizedStorage = std::make_unique<RuntimeWindowState>();
    auto& reference = *referenceStorage; auto& optimized = *optimizedStorage;
    if (!makeState(reference) || !makeState(optimized)) return fail("authored rule/terrain fixture");
    std::size_t checkedPoses = 0;
    for (int step = 0; step != 12; ++step) {
        for (auto* state : {&reference, &optimized}) {
            auto& objects = state->world.accessObjects();
            // Fixture transitions must locate the historical object too:
            // the gameplay helper excludes the dead actor before revival.
            auto* first = state->objectIndex.find(objects, 1);
            if (!first) return fail("first flight identity disappeared at step " + std::to_string(step));
            if (step == 1) { first->alive = false; first->health = 0; }
            if (step == 2) { first->alive = true; first->health = 100; }
            if (step == 3) first->airborne = false;
            if (step == 4) first->airborne = true;
            if (step == 5) state->jumpjetFlights.erase(10);
            if (step == 6) state->jumpjetFlights[10] = state->jumpjetFlights.at(1);
            if (step == 7) first->type = "PLAIN";
            if (step == 8) first->type = "FLIER";
            if (step == 9 && !loadRules(*state, true)) return fail("same-address rules reload");
            if (step == 10) {
                std::reverse(objects.begin(), objects.end());
                state->objectIndex = RuntimeObjectIndex{};
                const auto* unit = state->rules.findUnitType("Infantry", "FLIER");
                if (!unit) return fail("reload infantry profile disappeared");
                const_cast<UnitType*>(unit)->jumpJet = false;
            }
            if (step == 11) {
                if (!loadRules(*state, false)) return fail("restored rules reload");
                std::rotate(objects.begin(), objects.begin() + 17, objects.end());
                state->objectIndex = RuntimeObjectIndex{};
            }
        }
        const auto referenceFlights = serializeRuntimeJumpjetFlights(reference.jumpjetFlights);
        const auto optimizedFlights = serializeRuntimeJumpjetFlights(optimized.jumpjetFlights);
        using Pose = decltype(pose(reference.world.getObjects().front()));
        std::vector<Pose> before;
        before.reserve(reference.world.getObjects().size());
        for (const auto& object : reference.world.getObjects()) before.push_back(pose(object));
        {
            RuntimeSnapshotActiveStateScope active(reference);
            RuntimeHistoryQueryReferenceScope route(true);
            for (auto& object : reference.world.accessObjects()) applyRuntimeJumpjetPose(reference, object);
        }
        {
            RuntimeSnapshotActiveStateScope active(optimized);
            RuntimeHistoryQueryReferenceScope route(false);
            for (auto& object : optimized.world.accessObjects()) applyRuntimeJumpjetPose(optimized, object);
        }
        if (referenceFlights != serializeRuntimeJumpjetFlights(reference.jumpjetFlights)
            || optimizedFlights != serializeRuntimeJumpjetFlights(optimized.jumpjetFlights)
            || referenceFlights != optimizedFlights) return fail("pose application changed flight authority");
        if (reference.world.deterministicSimulationStateHash() != optimized.world.deterministicSimulationStateHash())
            return fail("complete world hash differs at step " + std::to_string(step));
        const auto& oldObjects = reference.world.getObjects(); const auto& newObjects = optimized.world.getObjects();
        if (oldObjects.size() != newObjects.size()) return fail("world population differs");
        for (std::size_t index = 0; index != oldObjects.size(); ++index) {
            if (pose(oldObjects[index]) != pose(newObjects[index]))
                return fail("exact pose bits differ at step/id " + std::to_string(step) + "/"
                    + std::to_string(oldObjects[index].instanceId));
            if (oldObjects[index].instanceId >= 1000 && pose(oldObjects[index]) != before[index])
                return fail("dormant history pose changed");
            ++checkedPoses;
        }
        if (step == 0) {
            for (int id : {2, 3, 4, 5, 7, 8, 9, 11, 13, 14, 15, 16})
                if (pose(oldObjects[static_cast<std::size_t>(id - 1)]) != before[static_cast<std::size_t>(id - 1)])
                    return fail("no-op fixture changed pose for id " + std::to_string(id));
            if (!oldObjects[0].hasRenderPosition || !oldObjects[0].renderMoveActive
                || oldObjects[0].renderX != 2.25 || oldObjects[0].renderY != 3.0
                || !oldObjects[5].hasRenderPosition || !oldObjects[9].hasRenderPosition
                || !oldObjects[11].hasRenderPosition || oldObjects[11].renderMoveActive)
                return fail("live/zero-health/unicode/stationary flight fixture did not receive a pose");
            if (oldObjects[0].renderLevel != static_cast<double>(reference.jumpjetFlights.at(1).height)
                / runtimeJumpjetCellQ16 + 64.0 / 256.0)
                return fail("cruise wobble fixture was not exercised");
        }
        if (step == 9) {
            const auto* noWobble = findObjectById(reference, 1);
            if (!noWobble || noWobble->renderLevel
                != static_cast<double>(reference.jumpjetFlights.at(1).height) / runtimeJumpjetCellQ16)
                return fail("same-address rules reload did not remove the authored wobble");
        }
    }
    std::cout << "Jumpjet history pose equivalence: checkedPoses=" << checkedPoses
        << " worldObjects=" << optimized.world.getObjects().size()
        << " exactPoseBits/flightAuthority/fullWorld/noFlight/dead/grounded/liveZeroHealth/type/revive/reorder/rulesReload=yes\n";
    error.clear();
    return true;
}
