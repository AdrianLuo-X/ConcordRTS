bool runRuntimeTriggerReinforcementsSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Trigger reinforcements: " + why; return false; };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config; stock->config.gameMode = "Battle";
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("stock resources unavailable");
    IniFile scenario;
    const auto mapPath = std::filesystem::path(config.localDataRootPath)
        / "resources/mix_exports/mapsmd03/sov01umd.map";
    if (!scenario.loadFromFile(pathString(mapPath))) return fail("original sov01 map unavailable");
    auto originalTeam = parseRuntimeTeamType(scenario, "08DF843C");
    const auto originalForce = parseRuntimeTaskForce(scenario, "0BD06A8C");
    if (!originalTeam.full || originalForce.members.size() != 2
        || originalForce.members[0].unitId != "E2" || originalForce.members[0].count != 12
        || originalForce.members[1].unitId != "SAPC" || originalForce.members[1].count != 1)
        return fail("stock Full/13-member composition");
    const auto makeWorld = [&]() {
        auto result = std::make_unique<RuntimeWindowState>();
        auto& state = *result;
        state.config = config; state.config.gameMode = "Battle"; state.config.fixedTickHz = 30;
        state.config.computerAiEnabled = false; state.config.shortGame = false;
        state.rules = stock->rules; state.art = stock->art; state.rulesLoaded = state.artLoaded = true;
        state.economy.owner = "Americans"; state.visionEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.runtimePreferencePersistenceEnabled = false;
        state.triggers.loaded = state.triggers.bootstrapComplete = true; state.tick = 1;
        for (int y = 1; y <= 36; ++y) for (int x = 1; x <= 36; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = cell.subTileIndex = 0;
            state.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(state);
        RuntimeScriptType hold; hold.id = "reinforcement-hold"; hold.steps = {{5, 100000}};
        state.triggers.scripts[hold.id] = hold;
        state.triggers.campaignBaseCenterByOwner["russians"] = {16, 16};
        return result;
    };
    {
        auto storage = makeWorld(); auto& state = *storage;
        originalTeam.scriptId = "reinforcement-hold";
        state.triggers.teams[originalTeam.id] = originalTeam;
        state.triggers.taskForces[originalForce.id] = originalForce;
        RuntimeTriggerDefinition trigger; trigger.id = "sov01-13";
        RuntimeTriggerAction action; action.type = 7; action.targetId = originalTeam.id;
        if (!fireRuntimeCreateTeamTrigger(state, trigger, action, false)
            || !trigger.fired || !state.triggers.reinforcements.pending.empty()
            || state.world.getObjects().size() != 13 || state.triggers.unitsCreated != 13
            || state.triggers.teamsCreated != 1 || state.triggers.teamMissions.size() != 1)
            return fail("stock 12 E2 plus SAPC not delivered as one team: " + state.triggers.status);
        const WorldObjectInstance* ship = nullptr;
        int passengers = 0;
        for (const auto& object : state.world.getObjects()) {
            if (object.type == "SAPC") ship = &object;
            if (object.type == "E2" && !object.alive && object.mission == "InsideTransport") ++passengers;
        }
        if (!ship || ship->transportPassengerIds.size() != 12 || passengers != 12
            || state.triggers.teamMissions.begin()->second.objectIds.size() != 13)
            return fail("Full=yes did not keep the 12 initial passengers aboard");
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(state, saved, error)) return false;
        IniFile savedIni;
        if (!savedIni.loadFromString(saved.str())) return fail("cargo save parse");
        auto restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored, state, false);
        if (!materializeRuntimeLoadStateFromIni(*restored, savedIni, error)) return false;
        const auto* restoredShip = findObjectById(*restored, ship->instanceId);
        if (!restoredShip || restoredShip->transportPassengerIds.size() != 12
            || restored->world.getObjects().size() != 13) return fail("loaded initial cargo differs");
        std::cout << "Stock sov01 reinforcement: 12 E2 + 1 SAPC=yes Full cargo=yes save=yes\n";
    }
    {
        auto storage = makeWorld(); auto& state = *storage;
        RuntimeTeamType team; team.id = "blocked-team"; team.house = "Russians";
        team.taskForceId = "blocked-force"; team.scriptId = "reinforcement-hold"; team.max = 1;
        RuntimeTaskForce force; force.id = team.taskForceId; force.members = {{20, "HTNK"}};
        state.triggers.teams[team.id] = team; state.triggers.taskForces[force.id] = force;
        for (auto& [key, info] : state.terrainInfoByCell) info.passable = false;
        RuntimeTriggerDefinition trigger; trigger.id = "blocked-trigger";
        RuntimeTriggerAction action; action.type = 7; action.targetId = team.id;
        if (!fireRuntimeCreateTeamTrigger(state, trigger, action, false)
            || !trigger.fired || state.triggers.reinforcements.pending.size() != 1
            || !state.world.getObjects().empty()) return fail("blocked request was lost");
        RuntimeTriggerDefinition duplicate; duplicate.id = "max-one-trigger";
        if (!fireRuntimeCreateTeamTrigger(state, duplicate, action, false)
            || state.triggers.reinforcements.pending.size() != 1
            || state.triggers.reinforcements.nextInstanceId != 2) return fail("pending team ignored Max=1");
        for (int x = 15; x <= 17; ++x) state.terrainInfoByCell[runtimeCellKey(x, 16)].passable = true;
        state.tick = 8;
        advanceRuntimeTriggers(state);
        if (state.world.getObjects().size() != 3 || state.triggers.unitsCreated != 3
            || !state.triggers.teamMissions.empty() || state.triggers.reinforcements.pending.size() != 1)
            return fail("partially blocked team progress incorrect: " + state.triggers.status);
        const std::string queueText = serializeRuntimeTriggerReinforcements(state.triggers.reinforcements);
        RuntimeTriggerReinforcements decoded;
        if (!deserializeRuntimeTriggerReinforcements(queueText, decoded, error)
            || serializeRuntimeTriggerReinforcements(decoded) != queueText) return fail("queue codec roundtrip");
        const std::string beforeInvalid = serializeRuntimeTriggerReinforcements(decoded);
        if (deserializeRuntimeTriggerReinforcements(queueText + " garbage", decoded, error)
            || serializeRuntimeTriggerReinforcements(decoded) != beforeInvalid) return fail("invalid decode changed live queue");
        error.clear();
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(state, saved, error)) return false;
        IniFile savedIni;
        if (!savedIni.loadFromString(saved.str())) return fail("pending save parse");
        auto restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored, state, false);
        if (!materializeRuntimeLoadStateFromIni(*restored, savedIni, error)) return false;
        if (serializeRuntimeTriggerReinforcements(restored->triggers.reinforcements) != queueText)
            return fail("full save lost partial progress or retry deadline");
        std::uint64_t originalHash = 0, changedHash = 0;
        runtimeMultiplayerAuthoritativeMixTriggers(originalHash, state.triggers);
        ++state.triggers.reinforcements.pending.begin()->second.nextAttemptTick;
        runtimeMultiplayerAuthoritativeMixTriggers(changedHash, state.triggers);
        --state.triggers.reinforcements.pending.begin()->second.nextAttemptTick;
        if (originalHash == changedHash) return fail("pending retry omitted from authority hash");
        const int resumeTick = state.triggers.reinforcements.pending.begin()->second.nextAttemptTick;
        for (auto* resumed : {&state, restored.get()}) {
            for (auto& [key, info] : resumed->terrainInfoByCell) info.passable = true;
            resumed->tick = resumeTick - 1;
            advanceRuntimeTriggers(*resumed);
            if (resumed->world.getObjects().size() != 3) return fail("retry deadline reset on load");
            resumed->tick = resumeTick;
            advanceRuntimeTriggers(*resumed);
            if (resumed->world.getObjects().size() != 20
                || !resumed->triggers.reinforcements.pending.empty()
                || resumed->triggers.teamMissions.size() != 1)
                return fail("unblocked queue missed or duplicated members");
            for (int tick = 0; tick < 30; ++tick) { ++resumed->tick; advanceRuntimeTriggers(*resumed); }
            if (resumed->world.getObjects().size() != 20 || resumed->triggers.teamMissions.size() != 1)
                return fail("completed queue issued a second team");
        }
        std::cout << "Pending reinforcement: blocked accepted=yes Max=yes 3/20 saved=yes deadline=yes exact resume=yes authority hash=yes\n";
    }
    {
        auto storage = makeWorld(); auto& state = *storage;
        RuntimeTeamType team; team.id = "large-team"; team.house = "Russians";
        team.taskForceId = "large-force"; team.scriptId = "reinforcement-hold";
        RuntimeTaskForce force; force.id = team.taskForceId; force.members = {{200, "HTNK"}};
        state.triggers.teams[team.id] = team; state.triggers.taskForces[force.id] = force;
        if (!queueRuntimeTriggerReinforcement(state, team, RuntimeCellPoint{16, 16}, 7)
            || state.world.getObjects().size() != 128 || state.triggers.reinforcements.pending.size() != 1)
            return fail("per-tick budget truncated large team");
        ++state.tick; advanceRuntimeTriggers(state);
        if (state.world.getObjects().size() != 200 || !state.triggers.reinforcements.pending.empty())
            return fail("large team failed to finish next tick");
        RuntimeTeamType blocked = team; blocked.id = "cancel-team";
        state.triggers.teams[blocked.id] = blocked;
        for (auto& [key, info] : state.terrainInfoByCell) info.passable = false;
        if (!queueRuntimeTriggerReinforcement(state, blocked, RuntimeCellPoint{16, 16}, 7)) return fail("cancel fixture");
        RuntimeTriggerDefinition trigger;
        RuntimeTriggerAction cancel; cancel.type = 5; cancel.targetId = blocked.id;
        if (!applyRuntimeCampaignAction(state, trigger, cancel)
            || !state.triggers.reinforcements.pending.empty()) return fail("Destroy Team left pending reinforcements");
        std::cout << "Large reinforcement: 200 members=yes bounded work=yes pending cancellation=yes\n";
    }
    std::cout << "Trigger reinforcements: passed\n";
    error.clear();
    return true;
}
