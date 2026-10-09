bool runRuntimeCrewSurvivorsSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "crew survivors: " + why; return false; };
    const auto makeState = [&](int escape = 100) {
        auto state = std::make_unique<RuntimeWindowState>();
        IniFile ini;
        ini.loadFromString(std::string("[General]\nCrewEscape=") + std::to_string(escape) + R"CREW(%
RefundPercent=50%
AlliedSurvivorDivisor=500
SovietSurvivorDivisor=250
ThirdSurvivorDivisor=750
AlliedCrew=E1
SovietCrew=E2
ThirdCrew=INIT
Technician=CTECH
Engineer=ENGINEER
[Sides]
Allied=Americans
Soviet=Russians
ThirdSide=YuriCountry
[BuildingTypes]
0=CREWBASE
1=CREWYARD
2=EMPTYBASE
[CREWBASE]
Strength=1000
Cost=3000
Crewed=yes
Capturable=yes
[CREWYARD]
Strength=1000
Cost=6000
Crewed=yes
Capturable=yes
Factory=BuildingType
[EMPTYBASE]
Strength=1000
Cost=3000
[VehicleTypes]
0=CREWTANK
1=EMPTYTANK
2=CREWTRANSPORT
[CREWTANK]
Strength=400
Cost=1000
Speed=5
SpeedType=Track
MovementZone=Normal
Crewed=yes
[EMPTYTANK]
Strength=400
Cost=1000
Speed=5
[CREWTRANSPORT]
Strength=400
Cost=1000
Speed=5
Crewed=yes
Passengers=5
[InfantryTypes]
0=E1
1=E2
2=INIT
3=CTECH
4=ENGINEER
[E1]
Strength=100
Speed=5
[E2]
Strength=100
Speed=5
[INIT]
Strength=100
Speed=5
[CTECH]
Strength=100
Speed=5
[ENGINEER]
Strength=100
Speed=5
Engineer=yes
[Warheads]
0=C4
[C4]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
)CREW");
        state->rules.loadFromIni(ini); state->rulesLoaded = true;
        state->config = config; state->config.fixedTickHz = 15;
        state->config.simulationRandomSeed = 815;
        state->visionEnabled = false; state->runtimeSuppressPlayerAudioFeedback = true;
        state->economy.owner = "Americans"; state->economy.credits = 100;
        IniFile art;
        art.loadFromString("[CREWBASE]\nFoundation=3x3\n[CREWYARD]\nFoundation=3x3\n[EMPTYBASE]\nFoundation=3x3\n");
        state->art.loadFromIni(art); state->artLoaded = true;
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x; cell.tileIndex = 0;
            state->world.accessTerrainCells().push_back(cell);
            state->terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
        }
        state->terrainByCell = buildRuntimeTerrainLookup(state->world);
        return state;
    };
    const auto add = [](RuntimeWindowState& state, int id, const std::string& type,
        const std::string& owner, bool building = true) {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "Crew" + std::to_string(id);
        object.owner = owner; object.type = type; object.kind = building ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        object.x = 9; object.y = 9; object.cell = runtimeCellKey(9, 9);
        object.health = building ? 1000 : 400; object.mission = "Guard";
        state.world.accessObjects().push_back(std::move(object));
        initializeRuntimeNavigation(state);
    };
    const auto infantry = [](const RuntimeWindowState& state) {
        std::vector<WorldObjectInstance> result;
        for (const auto& object : state.world.getObjects())
            if (object.alive && object.kind == WorldObjectKind::Infantry) result.push_back(object);
        return result;
    };
    const std::array<std::string, 3> owners{"Americans", "Russians", "YuriCountry"};
    const std::array<std::string, 3> types{"E1", "E2", "INIT"};
    const std::array<int, 3> expected{3, 5, 2};
    for (int side = 0; side < 3; ++side) {
        auto storage = makeState(); auto& state = *storage; state.economy.owner = owners[side];
        add(state, 1, "CREWBASE", owners[side]);
        if (!sellRuntimeStructure(state, 1)) return fail("actual sale rejected for " + owners[side]);
        const auto crew = infantry(state);
        if (crew.size() != expected[side] || state.economy.credits != 1600)
            return fail("sale crew/refund for " + owners[side] + " count=" + std::to_string(crew.size()));
        for (const auto& person : crew)
            if (person.owner != owners[side] || person.type != types[side] || person.health != 100 || person.subCell < 0)
                return fail("sale side, full health or infantry placement");
        if (sellRuntimeStructure(state, 1) || infantry(state).size() != crew.size()) return fail("duplicate sale returned crew");
    }
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, "CREWYARD", "Americans");
        if (!applyRuntimeOwnerTransitionBatch(state, {{1, "Americans", "Russians"}},
                RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits, "crewCapture").applied
            || !findObjectById(state, 1)->hasBeenCaptured
            || !applyRuntimeOwnerTransitionBatch(state, {{1, "Russians", "Americans"}},
                RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits, "crewRecapture").applied)
            return fail("capture/recapture did not preserve crew penalty");
        const auto hash = state.world.deterministicSimulationStateHash();
        const auto persistent = runtimePersistentTransientStateHash(state);
        findMutableObjectById(state, 1)->hasBeenCaptured = false;
        if (hash == state.world.deterministicSimulationStateHash() || persistent == runtimePersistentTransientStateHash(state))
            return fail("capture history missing from saved/synchronized state");
        findMutableObjectById(state, 1)->hasBeenCaptured = true;
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(state, saved, error)) return false;
        IniFile ini; ini.loadFromString(saved.str());
        auto loaded = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*loaded, state, false);
        if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) return fail("capture save load: " + error);
        if (!findObjectById(*loaded, 1) || !findObjectById(*loaded, 1)->hasBeenCaptured)
            return fail("saved capture penalty was lost");
        const int version = parseRuntimeIntOrDefault(ini.getValue("RuntimeSave", "Version", "0"), 0);
        auto fields = splitRuntimeSaveFields(ini.getValue("Objects", "0", ""));
        if (fields.size() < 58) return fail("capture save field missing");
        fields[57] = "invalid"; WorldObjectInstance malformed; std::string ignored;
        if (parseRuntimeCompleteSavedObjectRecord(state, fields, 1, version, malformed, ignored)) return fail("bad capture flag accepted");
        fields.resize(57); WorldObjectInstance legacy;
        if (!parseRuntimeCompleteSavedObjectRecord(state, fields, 1, version, legacy, error) || legacy.hasBeenCaptured)
            return fail("legacy object capture default");
        if (!sellRuntimeStructure(*loaded, 1) || infantry(*loaded).size() != 3) return fail("loaded captured yard refund count");
        for (const auto& crew : infantry(*loaded)) if (crew.type == "ENGINEER") return fail("captured yard returned an engineer");
    }
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, "CREWYARD", "Americans");
        RuntimeObjectExitRequest sale; sale.kind = RuntimeObjectExitKind::Sold; sale.objectId = 1;
        int engineers = 0;
        for (int seed = 0; seed < 128; ++seed) {
            state.config.simulationRandomSeed = seed;
            const auto crew = prepareRuntimeCrewSurvivors(state, *findObjectById(state, 1), sale);
            int count = 0; for (const auto& person : crew) count += person.type == "ENGINEER";
            if (crew.size() != 5 || count > 1) return fail("yard crew cap / one-engineer limit");
            engineers += count;
        }
        if (engineers < 20 || engineers >= 128) return fail("yard engineer must be probabilistic");
    }
    for (const auto& type : {"CREWTANK", "EMPTYTANK", "CREWTRANSPORT"}) {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, type, "Russians", false); state.selectedObjectIds = {1};
        findMutableObjectById(state, 1)->health = 0; // Real lethal damage precedes object finalization.
        RuntimeObjectExitRequest death; death.objectId = 1; death.tick = 20;
        death.suppressDeathWeapon = death.destructionEffectsSpawned = true;
        if (!finalizeRuntimeObjectExit(state, death).accepted) return fail("vehicle exit");
        const auto crew = infantry(state);
        if (crew.size() != (std::string(type) == "CREWTANK" ? 1 : 0)) return fail(std::string(type) + " vehicle crew rule");
        if (!crew.empty() && (crew[0].owner != "Russians" || crew[0].type != "E2"
            || crew[0].health < 5 || crew[0].health > 50 || crew[0].subCell < 0
            || state.selectedObjectIds != std::vector<int>{crew[0].instanceId})) return fail("escaped wounded vehicle crew");
        if (!finalizeRuntimeObjectExit(state, death).duplicate || infantry(state).size() != crew.size()) return fail("duplicate death crew");
        if (!crew.empty()) {
            const auto from = runtimeCellPointForObject(crew[0]);
            for (int i = 0; i < 160; ++i) { ++state.tick; advanceRuntimeSimulation(state, false); }
            if (runtimeCellPointForObject(*findObjectById(state, crew[0].instanceId)) == from) return fail("crew did not leave wreck position");
        }
    }
    {
        auto storage = makeState(50); auto& state = *storage;
        add(state, 1, "CREWTANK", "Americans", false);
        RuntimeObjectExitRequest death; death.objectId = 1; death.tick = 15;
        int survived = 0;
        for (int seed = 0; seed < 512; ++seed) {
            state.config.simulationRandomSeed = seed;
            survived += !prepareRuntimeCrewSurvivors(state, *findObjectById(state, 1), death).empty();
        }
        if (survived < 195 || survived > 317) return fail("CrewEscape 50% distribution");
        death.suppressCrew = true;
        if (!finalizeRuntimeObjectExit(state, death).accepted || !infantry(state).empty()) return fail("suicide suppression");
    }
    for (auto kind : {RuntimeObjectExitKind::TemporalDestroyed, RuntimeObjectExitKind::TransformSource,
            RuntimeObjectExitKind::Consumed, RuntimeObjectExitKind::ScriptRemoved}) {
        auto storage = makeState(); auto& state = *storage; add(state, 1, "CREWBASE", "Americans");
        RuntimeObjectExitRequest exit; exit.objectId = 1; exit.kind = kind;
        if (!finalizeRuntimeObjectExit(state, exit).accepted || !infantry(state).empty()) return fail("non-combat exit returned crew");
    }
    {
        auto storage = makeState(); auto& state = *storage; add(state, 1, "CREWBASE", "Americans");
        RuntimeObjectExitRequest death; death.objectId = 1;
        int ordinary = 0, captured = 0, c4 = 0;
        for (int seed = 0; seed < 512; ++seed) {
            state.config.simulationRandomSeed = seed;
            auto source = *findObjectById(state, 1);
            ordinary += static_cast<int>(prepareRuntimeCrewSurvivors(state, source, death).size());
            source.hasBeenCaptured = true;
            captured += static_cast<int>(prepareRuntimeCrewSurvivors(state, source, death).size());
            source.hasBeenCaptured = false; death.c4Explosion = true;
            c4 += static_cast<int>(prepareRuntimeCrewSurvivors(state, source, death).size()); death.c4Explosion = false;
        }
        if (ordinary <= captured || c4 <= ordinary || ordinary <= 0) return fail("building destruction / capture / C4 probabilities");
        // Find an event with survivors, then execute the actual destruction.
        for (int attempt = 0; attempt < 128
            && prepareRuntimeCrewSurvivors(state, *findObjectById(state, 1), death).empty(); ++attempt) ++death.tick;
        const auto count = prepareRuntimeCrewSurvivors(state, *findObjectById(state, 1), death).size();
        if (count == 0 || !finalizeRuntimeObjectExit(state, death).accepted || infantry(state).size() != count) return fail("destroyed foundation crew placement");
    }
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, "CREWBASE", "Americans"); state.tick = 20;
        RuntimeObjectExitRequest death; death.objectId = 1; death.tick = state.tick;
        std::size_t expectedCount = 0; bool distinct = false;
        for (int seed = 0; seed < 128 && !distinct; ++seed) {
            state.config.simulationRandomSeed = seed; death.c4Explosion = false;
            const auto ordinary = prepareRuntimeCrewSurvivors(state, *findObjectById(state, 1), death).size();
            death.c4Explosion = true;
            expectedCount = prepareRuntimeCrewSurvivors(state, *findObjectById(state, 1), death).size();
            distinct = expectedCount > ordinary;
        }
        if (!distinct) return fail("C4-specific fixture");
        SpecialMechanismAction c4; c4.kind = SpecialMechanismActionKind::TimedBombDetonate;
        c4.targetObjectId = 1; c4.objectId = 99; c4.owner = "Russians"; c4.tick = state.tick;
        c4.cell = {9, 9}; c4.damage = 2000; c4.warheadId = "C4"; c4.forceDestruction = true;
        if (!applyRuntimeSpecialMechanismActions(state, {c4})) return fail("C4 detonation rejected");
        (void)drainRuntimeObjectExitQueue(state);
        if (infantry(state).size() != expectedCount) return fail("C4 blast lost its crew cause before exit");
    }
    for (bool water : {false, true}) {
        auto storage = makeState(water ? 100 : 0); auto& state = *storage;
        add(state, 1, "CREWTANK", "Americans", false);
        if (water) for (auto& [cell, info] : state.terrainInfoByCell) {
            info.kind = RuntimeTerrainKind::Water; info.water = true;
        }
        RuntimeObjectExitRequest death; death.objectId = 1; death.destructionEffectsSpawned = true;
        if (!finalizeRuntimeObjectExit(state, death).accepted || !infantry(state).empty()) return fail("zero chance / water must not create crew");
    }
    std::cout << "Crew survivors: three-side sales=yes capture/save=yes yard engineer limit=yes "
        "vehicle escape/wounds/movement=yes death probabilities=yes duplicate/terrain/exclusions=yes\n";
    return true;
}
