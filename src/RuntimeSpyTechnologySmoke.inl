bool runRuntimeSpyTechnologySmokeProbe(std::string& error) {
    const auto fail = [&](const std::string& message) { error = message; return false; };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    state.config.fixedTickHz = 30;
    state.config.techLevel = 10;
    state.config.playerCountry = "Player";
    state.config.defaultMapPath = "spy-technology-probe";
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.visionEnabled = false;
    IniFile ini;
    if (!ini.loadFromString(R"TECH(
[General]
PrerequisiteBarracks=TECH_BARRACKS
PrerequisiteTech=LAB_ALLIED,LAB_SOVIET,LAB_THIRD
[InfantryTypes]
0=SPY
1=GI
2=CCOMAND
3=CIVAN
4=PTROOP
[BuildingTypes]
0=TECH_BARRACKS
1=LAB_ALLIED
2=LAB_SOVIET
3=LAB_THIRD
[SPY]
Strength=100
Speed=6
Agent=yes
Infiltrate=yes
[GI]
Strength=100
Speed=5
TechLevel=1
Cost=100
Owner=Player
Prerequisite=BARRACKS
[CCOMAND]
Strength=100
Speed=5
TechLevel=1
Cost=100
Owner=Player
Prerequisite=BARRACKS
RequiresStolenAlliedTech=yes
[CIVAN]
Strength=100
Speed=5
TechLevel=1
Cost=100
Owner=Player
Prerequisite=BARRACKS
RequiresStolenSovietTech=yes
[PTROOP]
Strength=100
Speed=5
TechLevel=1
Cost=100
Owner=Player
Prerequisite=BARRACKS
RequiresStolenThirdTech=yes
[TECH_BARRACKS]
Strength=1000
Factory=InfantryType
Foundation=1x1
Spyable=yes
[LAB_ALLIED]
Strength=1000
Foundation=1x1
Spyable=yes
AIBasePlanningSide=0
[LAB_SOVIET]
Strength=1000
Foundation=1x1
Spyable=yes
AIBasePlanningSide=1
[LAB_THIRD]
Strength=1000
Foundation=1x1
Spyable=yes
AIBasePlanningSide=2
)TECH") || !state.rules.loadFromIni(ini)) return fail("fixture rules failed");
    state.rulesLoaded = true;
    state.economy.owner = "Player";
    state.economy.techLevel = 10;
    state.economy.credits = 10000;
    state.economy.skirmishStartApplied = true;
    state.triggers.bootstrapComplete = true;
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
        state.world.accessTerrainCells().push_back(cell);
    }
    initializeRuntimeNavigation(state);
    const auto add = [&](RuntimeBuildKind kind, const std::string& type,
                         const std::string& owner, RuntimeCellPoint cell) {
        auto* object = addRuntimeObjectInstanceForOwner(state, owner, kind, type, cell);
        if (!object) return -1;
        object->buildAnimTicksTotal = object->buildAnimTicksRemaining = 0;
        object->mission = "Guard";
        return object->instanceId;
    };
    if (add(RuntimeBuildKind::Building, "TECH_BARRACKS", "Player", {3, 3}) < 0
        || add(RuntimeBuildKind::Building, "TECH_BARRACKS", "Enemy", {3, 18}) < 0)
        return fail("could not create barracks");
    const char* unitTypes[] = {"CCOMAND", "CIVAN", "PTROOP", "GI"};
    const char* labTypes[] = {"LAB_ALLIED", "LAB_SOVIET", "LAB_THIRD"};
    std::vector<RuntimeBuildCatalogItem> items;
    for (const char* type : unitTypes) {
        RuntimeBuildCatalogItem item;
        item.kind = RuntimeBuildKind::Infantry;
        item.typeId = type;
        item.owner = "Player";
        item.prerequisite = "BARRACKS";
        item.requiredFactory = "InfantryType";
        item.cost = 100;
        item.buildTicks = 30;
        item.techLevel = 1;
        items.push_back(item);
    }
    state.economy.catalog = items;
    if (!runtimeBuildItemUnlockedForSidebar(state, items[3]))
        return fail("ordinary infantry did not unlock with a completed barracks");
    for (int side = 0; side < 3; ++side) {
        if (runtimeBuildItemUnlockedForSidebar(state, items[side])
            || evaluateRuntimeTechTreeDecision(state, items[side]).canStart
            || queueRuntimeBuildOrder(state, items[side]))
            return fail(std::string(unitTypes[side]) + " bypassed the infiltration requirement");
    }
    RuntimeComputerAiState ai;
    ai.owner = "Player"; ai.country = "Player";
    if (runtimeComputerAiItemAllowed(state, ai, items[0]))
        return fail("AI bypassed stolen technology before infiltration");
    for (int side = 0; side < 3; ++side) {
        const RuntimeCellPoint labCell{12, 4 + side * 5};
        const int labId = add(RuntimeBuildKind::Building, labTypes[side], "Enemy", labCell);
        const int spyId = add(RuntimeBuildKind::Infantry, "SPY", "Player", {labCell.x - 1, labCell.y});
        if (labId < 0 || spyId < 0) return fail("could not create infiltration actors");
        // All three lab types share an owner; the building type decides the reward.
        state.skirmishCountryByOwner["enemy"] = "Russians";
        applySelection(state, {spyId}, false, false);
        if (!queueRuntimeSpyInfiltration(state, *findObjectById(state, labId)))
            return fail("laboratory infiltration command was rejected");
        if (completeRuntimeSpyInfiltration(state, spyId)
            || runtimeBuildItemUnlockedForSidebar(state, items[side]))
            return fail("technology unlocked before the spy entered the laboratory");
        auto* spy = findMutableObjectById(state, spyId);
        spy->x = labCell.x; spy->y = labCell.y; spy->cell = runtimeCellKey(spy->x, spy->y);
        clearRuntimeRenderPosition(*spy);
        if (!completeRuntimeSpyInfiltration(state, spyId) || findObjectById(state, spyId))
            return fail("laboratory entry did not consume the spy and commit the reward");
        for (int candidate = 0; candidate < 3; ++candidate) {
            if (runtimeBuildItemUnlockedForSidebar(state, items[candidate]) != (candidate <= side))
                return fail("laboratory unlocked the wrong side's production option");
        }
        if (!queueRuntimeBuildOrder(state, items[side]))
            return fail("infiltration unlock did not reach the real production queue");
        if (state.spies.hasStolenTechnology("Enemy", static_cast<RuntimeSpySide>(side)))
            return fail("laboratory owner incorrectly received the infiltrator's technology");
    }
    if (!runtimeComputerAiItemAllowed(state, ai, items[0]))
        return fail("AI could not use its earned stolen technology");
    ai.owner = "Enemy";
    if (runtimeComputerAiItemAllowed(state, ai, items[0]))
        return fail("one house's stolen technology leaked into another house's production");

    std::ostringstream saved;
    if (!writeRuntimeStateToStream(state, saved, error)) return false;
    if (saved.str().find("StolenTechMask=7") == std::string::npos)
        return fail("runtime save omitted earned technology");
    IniFile savedIni;
    RuntimeSpySystemState restored;
    if (!savedIni.loadFromString(saved.str())
        || !restored.load(savedIni, makeRuntimeSpyContext(state), state.tick, &error)
        || restored.stateHash() != state.spies.stateHash())
        return fail("earned technology did not survive runtime persistence: " + error);
    state.spies = std::move(restored);
    for (int side = 0; side < 3; ++side) {
        if (!runtimeBuildItemUnlockedForSidebar(state, items[side]))
            return fail("reloaded technology did not preserve production eligibility");
    }
    std::cout << "Spy technology: barracks-only=locked entry=required three-labs=correct "
                 "captured-lab=correct owner-isolation=ok production/AI/save-load=ok\n";
    error.clear();
    return true;
}
