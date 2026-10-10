bool runRuntimePlannerInvisibleBuildingContract(std::string& error) {
    auto storage = std::make_unique<RuntimeWindowState>();
    auto foreignStorage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    auto& foreign = *foreignStorage;
    IniFile ini;
    if (!ini.loadFromString("[BuildingTypes]\n0=LIGHT\n1=SOLID\n"
        "[LIGHT]\nInvisibleInGame=yes\nStrength=100\n"
        "[SOLID]\nInvisibleInGame=no\nStrength=100\n")
        || !state.rules.loadFromIni(ini) || !foreign.rules.loadFromIni(ini)) {
        error = "planner invisible buildings: fixture rules"; return false;
    }
    state.rulesLoaded = foreign.rulesLoaded = true;
    for (int i = 0; i < 1000; ++i) {
        WorldObjectInstance object;
        object.instanceId = i % 73; // Duplicate IDs retain the original set union.
        object.kind = i % 3 ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        object.type = i % 7 ? "LIGHT" : "SOLID";
        object.alive = i % 5 != 0; object.health = i % 11 ? 100 : 0;
        object.x = i % 9; object.y = i % 13;
        state.world.accessObjects().push_back(object);
        object.kind = WorldObjectKind::Unit;
        foreign.world.accessObjects().push_back(object);
    }
    const auto before = state.world.deterministicSimulationStateHash();
    const auto expected = [&] {
        RuntimeHistoryQueryReferenceScope reference(true);
        return runtimeGateSubcellContext(state);
    }();
    if (expected.ignoredWholeCellObjectIds.empty()) {
        error = "planner invisible buildings: fixture has no invisible structures"; return false;
    }
    for (bool initialized : {false, true}) {
        state.gatesInitialized = initialized;
        RuntimePlannerInvisibleBuildingScope scope(state);
        if (runtimeGateSubcellContext(state).ignoredWholeCellObjectIds != expected.ignoredWholeCellObjectIds
            || !runtimeGateSubcellContext(foreign).ignoredWholeCellObjectIds.empty()) {
            error = "planner invisible buildings: context/state identity mismatch"; return false;
        }
        {
            RuntimePlannerInvisibleBuildingScope nested(foreign);
            if (runtimeGateSubcellContext(state).ignoredWholeCellObjectIds != expected.ignoredWholeCellObjectIds
                || !runtimeGateSubcellContext(foreign).ignoredWholeCellObjectIds.empty()) {
                error = "planner invisible buildings: nested context mismatch"; return false;
            }
        }
        if (runtimeGateSubcellContext(state).ignoredWholeCellObjectIds != expected.ignoredWholeCellObjectIds) {
            error = "planner invisible buildings: nested restoration mismatch"; return false;
        }
    }
    if (before != state.world.deterministicSimulationStateHash()) {
        error = "planner invisible buildings: query changed world authority"; return false;
    }
    std::cout << "Planner invisible building context: dead/type/duplicate/state/nested/authority=yes\n";
    error.clear(); return true;
}
