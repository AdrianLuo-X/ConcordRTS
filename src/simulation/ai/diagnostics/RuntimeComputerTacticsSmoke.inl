// These checks cover the catalog and script continuation foundation. The full
// tactics suite also needs paid assembly and the actual specialist missions.
bool runRuntimeComputerTacticsFoundationSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& message) {
        error = "computer tactics foundation: " + message; return false;
    };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->config.gameMode = "Battle";
    stock->config.gameModeRulesOverride.clear();
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("stock resources unavailable");
    stock->rulesLoaded = stock->artLoaded = true;
    if (!initializeRuntimeTriggers(*stock)) return fail("stock team definitions unavailable");
    if (stock->triggers.computerAiTriggers.size() != 165
        || !stock->triggers.campaignAiTriggers.empty())
        return fail("stock skirmish catalog not loaded separately from scenario AI");
    const auto& rules = stock->rules.getComputerTacticsRules();
    if (rules.teamDelays != std::array<int, 3>{2000, 2500, 3500}
        || rules.totalTeamCap != std::array<int, 3>{30, 30, 30}
        || rules.minimumDefensiveTeams != std::array<int, 3>{1, 1, 1}
        || rules.maximumDefensiveTeams != std::array<int, 3>{2, 2, 2}
        || !rules.useMinimumDefenseRule || rules.dissolveUnfilledTeamDelay != 5000)
        return fail("authored team timing and limits were not loaded");
    IniFile aiIni, mapIni;
    if (!loadRuntimeIniSource(stock->config, RuntimeCampaignIniKind::Ai, aiIni))
        return fail("original AI source unavailable");
    mapIni.loadFromString("[AITriggerTypesEnable]\n0D084A0C-G=no\n");
    loadRuntimeComputerTacticTriggers(*stock, &aiIni, mapIni);
    const auto disabled = std::find_if(stock->triggers.computerAiTriggers.begin(),
        stock->triggers.computerAiTriggers.end(), [](const auto& definition) {
            return definition.id == "0D084A0C-G";
        });
    if (disabled == stock->triggers.computerAiTriggers.end() || disabled->enabled)
        return fail("map cannot disable a global airstrike team");
    mapIni.loadFromString("[Basic]\nIgnoreGlobalAITriggers=yes\n[AITriggerTypes]\nLOCAL="
        + aiIni.getValue("AITriggerTypes", "0D084A0C-G", "") + "\n");
    loadRuntimeComputerTacticTriggers(*stock, &aiIni, mapIni);
    if (stock->triggers.computerAiTriggers.size() != 1
        || stock->triggers.computerAiTriggers.front().id != "LOCAL")
        return fail("map-local team list did not replace ignored global triggers");

    auto probe = std::make_unique<RuntimeWindowState>();
    auto& s = *probe;
    s.config = stock->config; s.config.fixedTickHz = 30;
    s.config.computerAiEnabled = true;
    s.config.shortGame = false; s.config.startingCredits = 0;
    s.config.playerSlots.resize(2);
    for (int i = 0; i < 2; ++i) {
        s.config.playerSlots[i].enabled = true;
        s.config.playerSlots[i].human = s.config.playerSlots[i].local = i == 0;
        s.config.playerSlots[i].country = "Americans";
    }
    s.rules = stock->rules; s.rulesLoaded = true;
    s.art = stock->art; s.artLoaded = true;
    s.visionEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
    s.runtimePreferencePersistenceEnabled = false;
    s.economy.owner = runtimeSkirmishOwnerForSlot(0);
    s.economy.skirmishStartApplied = true;
    const std::string aiOwner = runtimeSkirmishOwnerForSlot(1);
    RuntimeHumanHouseAuthority authority;
    authority.owner = s.economy.owner;
    authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
    authority.localFacade = true;
    s.humanHouseAuthorities.push_back(authority);
    for (int y = 1; y <= 16; ++y) for (int x = 1; x <= 16; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
        cell.tileIndex = cell.subTileIndex = 0;
        s.world.accessTerrainCells().push_back(cell);
    }
    auto* tank = addRuntimeObjectInstanceForOwner(s, aiOwner,
        RuntimeBuildKind::Unit, "MTNK", {2, 2});
    if (tank == nullptr) return fail("movement member unavailable");
    const int tankId = tank->instanceId;
    addRuntimeObjectInstanceForOwner(s, s.economy.owner,
        RuntimeBuildKind::Building, "GACNST", {12, 2});
    ensureRuntimeComputerAiStates(s);
    for (auto& [owner, ai] : s.computerAiByOwner) ai.nextThinkTick = 10000000;
    initializeRuntimeNavigation(s);
    s.triggers.loaded = s.triggers.bootstrapComplete = true;
    RuntimeScriptType script;
    script.id = "success-followed-by-move";
    script.steps = {{49, 0}, {4, 10010}, {49, 0}};
    s.triggers.scripts[script.id] = script;
    RuntimeTeamType team; team.id = "success-move-team"; team.scriptId = script.id;
    team.house = aiOwner;
    s.triggers.teams[team.id] = team;
    RuntimeCampaignAiTriggerDefinition trigger;
    trigger.id = "success-move-trigger";
    trigger.currentWeight = 30; trigger.minimumWeight = 10; trigger.maximumWeight = 70;
    s.triggers.campaignAiTriggers.push_back(trigger);
    issueRuntimeTriggerTeamOrder(s, team, {tankId}, trigger.id);
    if (s.triggers.teamMissions.empty() || !s.behaviors.contains(tankId))
        return fail("Register Success stopped the following movement order");
    if (!s.triggers.teamMissions.begin()->second.achievedGreatSuccess
        || s.triggers.campaignAiTriggers.front().timesCompleted != 0)
        return fail("success registration ended the team before its later orders");
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(s, saved, error)) return false;
    IniFile savedIni;
    if (!savedIni.loadFromString(saved.str())) return fail("mid-script save unavailable");
    auto restored = std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*restored, s, s.visionEnabled);
    if (!materializeRuntimeLoadStateFromIni(*restored, savedIni, error)) return false;
    if (restored->triggers.teamMissions.empty()
        || !restored->triggers.teamMissions.begin()->second.achievedGreatSuccess)
        return fail("saved team forgot its registered success");
    for (int tick = 0; tick < 1000 && !s.triggers.teamMissions.empty(); ++tick) {
        ++s.tick;
        advanceRuntimeSimulation(s);
    }
    tank = findMutableObjectById(s, tankId);
    const auto& result = s.triggers.campaignAiTriggers.front();
    if (tank == nullptr || runtimeCellPointForObject(*tank) != RuntimeCellPoint{10, 10}
        || !s.triggers.teamMissions.empty() || result.timesCompleted != 1
        || result.timesFailed != 0 || result.currentWeight != 50)
        return fail("post-success movement or single success credit did not finish");
    for (int tick = 0; tick < 1000 && !restored->triggers.teamMissions.empty(); ++tick) {
        ++restored->tick;
        advanceRuntimeSimulation(*restored);
    }
    const auto* loadedTank = findObjectById(*restored, tankId);
    if (loadedTank == nullptr || runtimeCellPointForObject(*loadedTank) != RuntimeCellPoint{10, 10}
        || restored->triggers.campaignAiTriggers.front().timesCompleted != 1)
        return fail("loaded computer team did not continue its remaining orders");
    RuntimeScriptType loop; loop.id = "one-based-loop";
    loop.steps = {{4, 2002}, {4, 10010}, {6, 1}};
    s.triggers.scripts[loop.id] = loop;
    team.scriptId = loop.id;
    issueRuntimeTriggerTeamOrder(s, team, {tankId});
    int laps = 0; bool reachedFirst = false;
    for (int frame = 0; frame < 2400 && laps < 2; ++frame) {
        ++s.tick; advanceRuntimeSimulation(s);
        const auto* mover = findObjectById(s, tankId);
        if (!mover) return fail("looping tank disappeared");
        const auto cell = runtimeCellPointForObject(*mover);
        if (cell == RuntimeCellPoint{2, 2}) reachedFirst = true;
        if (reachedFirst && cell == RuntimeCellPoint{10, 10}) { ++laps; reachedFirst = false; }
    }
    if (laps != 2) return fail("script 6,1 did not repeat its first movement line");
    std::cout << "Computer tactics foundation: stock catalog=165 map override=yes "
        "authored limits=yes computer movement after success=yes save/resume=yes single outcome=yes one-based jump repeats movement=yes\n";
    return true;
}
