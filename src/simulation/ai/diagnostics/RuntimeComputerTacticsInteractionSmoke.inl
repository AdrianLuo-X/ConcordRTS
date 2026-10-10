bool runRuntimeComputerTacticsInteractionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& message) {
        error = "computer tactic interactions: " + message; return false;
    };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config; stock->config.gameMode = "Battle";
    stock->config.gameModeRulesOverride.clear();
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original rules unavailable");
    stock->rulesLoaded = stock->artLoaded = true;
    if (!initializeRuntimeTriggers(*stock)) return fail("original scripts unavailable");
    const std::string humanOwner = runtimeSkirmishOwnerForSlot(0);
    const std::string aiOwner = runtimeSkirmishOwnerForSlot(1);
    const auto makeWorld = [&](const std::string& country) {
        auto probe = std::make_unique<RuntimeWindowState>();
        auto& s = *probe;
        s.config = stock->config; s.config.fixedTickHz = 30;
        s.config.computerAiEnabled = true; s.config.shortGame = false;
        s.config.startingCredits = 0; s.config.playerSlots.resize(2);
        for (int i = 0; i < 2; ++i) {
            auto& slot = s.config.playerSlots[i];
            slot.enabled = true; slot.human = slot.local = i == 0;
            slot.country = i == 0 ? "Americans" : country;
        }
        s.rules = stock->rules; s.art = stock->art;
        s.rulesLoaded = s.artLoaded = true;
        s.visionEnabled = true; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.economy.owner = humanOwner; s.economy.skirmishStartApplied = true;
        RuntimeHumanHouseAuthority authority;
        authority.owner = humanOwner;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(humanOwner);
        authority.localFacade = true; s.humanHouseAuthorities.push_back(authority);
        for (int y = 1; y <= 32; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        s.triggers = stock->triggers;
        s.triggers.computerAiTriggers.clear(); // Isolate the dispatched stock mission.
        s.triggers.bootstrapComplete = true;
        addRuntimeObjectInstanceForOwner(s, humanOwner, RuntimeBuildKind::Building,
            "GACNST", {27, 27});
        addRuntimeObjectInstanceForOwner(s, aiOwner, RuntimeBuildKind::Building,
            country == "YuriCountry" ? "YACNST" : country == "Russians" ? "NACNST" : "GACNST", {1, 27});
        ensureRuntimeComputerAiStates(s);
        for (auto& [owner, ai] : s.computerAiByOwner) ai.nextThinkTick = 10000000;
        initializeRuntimeNavigation(s);
        return probe;
    };
    const auto describe = [](const RuntimeWindowState& s) {
        std::ostringstream out;
        out << " tick=" << s.tick << " status=" << s.simulationStatus;
        for (const auto& [id, mission] : s.triggers.teamMissions)
            out << " team=" << id << " step=" << mission.stepIndex
                << " target=" << mission.targetObjectId << " cell=" << mission.targetCell.x << ',' << mission.targetCell.y << " " << mission.status;
        for (const auto& [id, behavior] : s.behaviors)
            out << " behavior=" << id << ':' << static_cast<int>(behavior.kind) << ':' << static_cast<int>(behavior.state)
                << "->" << behavior.targetObjectId << " auto=" << behavior.autoAcquired;
        for (const auto& object : s.world.getObjects())
            if (object.kind == WorldObjectKind::Infantry)
                out << " " << object.type << "#" << object.instanceId
                    << "@" << object.x << "," << object.y << " " << object.mission
                    << " alive=" << object.alive;
        return out.str();
    };
    for (bool barracks : {true, false}) {
        auto probe = makeWorld("Americans"); auto& s = *probe;
        const std::string teamId = barracks ? "0CADF54C-G" : "0CE1F59C-G";
        auto team = *findRuntimeTriggerTeamById(s, teamId); team.house = aiOwner;
        auto* spy = addRuntimeObjectInstanceForOwner(s, aiOwner,
            RuntimeBuildKind::Infantry, "SPY", {2, 2});
        if (spy == nullptr) return fail("stock Spy unavailable");
        const int spyId = spy->instanceId;
        auto* model = addRuntimeObjectInstanceForOwner(s, humanOwner,
            RuntimeBuildKind::Infantry, "E1", {7, 2});
        if (model == nullptr) return fail("disguise target unavailable");
        model->mission = "Sleep";
        auto* target = addRuntimeObjectInstanceForOwner(s, humanOwner,
            RuntimeBuildKind::Building, barracks ? "GAPILE" : "GAPOWR", {21, 16});
        if (target == nullptr) return fail("infiltration building unavailable");
        const int targetId = target->instanceId;
        updateRuntimeVisionState(s);
        if (runtimeObjectVisibleToOwner(s, aiOwner, *target))
            return fail("infiltration target was not initially behind shroud");
        issueRuntimeTriggerTeamOrder(s, team, {spyId});
        bool copiedDisguise = false, savedEnRoute = false;
        std::unique_ptr<RuntimeWindowState> restored;
        for (int tick = 0; tick < 2600 && findObjectById(s, spyId) != nullptr; ++tick) {
            ++s.tick; advanceRuntimeSimulation(s);
            if (const auto* disguise = s.spies.disguiseState(spyId))
                copiedDisguise = copiedDisguise || (disguise->active
                    && !disguise->observerRelative && disguise->disguiseType == "E1");
            const auto behavior = s.behaviors.find(spyId);
            if (!savedEnRoute && behavior != s.behaviors.end()
                && behavior->second.kind == RuntimeBehaviorKind::Move
                && behavior->second.targetObjectId == targetId) {
                std::ostringstream text;
                if (!writeRuntimeStateToStream(s, text, error)) return false;
                IniFile saved; saved.loadFromString(text.str());
                restored = std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*restored, s, s.visionEnabled);
                if (!materializeRuntimeLoadStateFromIni(*restored, saved, error)) return false;
                savedEnRoute = true;
            }
        }
        const auto effectPresent = [&](const RuntimeWindowState& candidate) {
            return barracks ? candidate.spies.hasVeteranProduction(
                runtimeRepairDepotCanonicalOwnerId(candidate, aiOwner), RuntimeSpyVeteranProduction::Infantry)
                : candidate.spies.blackoutUntilTick(
                    runtimeRepairDepotCanonicalOwnerId(candidate, humanOwner)) > candidate.tick;
        };
        if (!copiedDisguise || !savedEnRoute || findObjectById(s, spyId) != nullptr
            || !effectPresent(s)) return fail("stock Spy script did not infiltrate "
                + std::string(barracks ? "barracks" : "power") + describe(s));
        for (int tick = 0; tick < 2600 && findObjectById(*restored, spyId) != nullptr; ++tick) {
            ++restored->tick; advanceRuntimeSimulation(*restored);
        }
        if (!effectPresent(*restored) || findObjectById(*restored, spyId) != nullptr)
            return fail("saved infiltration did not finish" + describe(*restored));
        std::cout << "Computer Spy: stock " << teamId
            << " disguise=yes physical entry=yes effect=yes save/resume=yes\n";
    }
    {
        auto probe = makeWorld("Russians"); auto& s = *probe;
        auto team = *findRuntimeTriggerTeamById(s, "0C143EFC-G"); team.house = aiOwner;
        addRuntimeObjectInstanceForOwner(s, aiOwner, RuntimeBuildKind::Building, "NAHAND", {4, 18});
        const auto* truck = addRuntimeObjectInstanceForOwner(s, aiOwner,
            RuntimeBuildKind::Unit, "HTK", {6, 14});
        if (truck == nullptr) return fail("stock engineer transport unavailable");
        const int truckId = truck->instanceId;
        std::vector<int> members{truckId};
        for (int x = 2; x < 7; ++x) {
            const auto* engineer = addRuntimeObjectInstanceForOwner(s, aiOwner,
                RuntimeBuildKind::Infantry, "SENGINEER", {x, 11});
            if (engineer == nullptr) return fail("Soviet engineer unavailable");
            members.push_back(engineer->instanceId);
        }
        int yardId = -1;
        for (const auto& object : s.world.getObjects())
            if (object.owner == humanOwner && object.type == "GACNST") yardId = object.instanceId;
        updateRuntimeVisionState(s);
        issueRuntimeTriggerTeamOrder(s, team, members);
        std::unique_ptr<RuntimeWindowState> restored;
        bool loaded = false, unloaded = false;
        for (int tick = 0; tick < 4200; ++tick) {
            ++s.tick; advanceRuntimeSimulation(s);
            const auto* vehicle = findObjectById(s, truckId);
            if (!loaded && vehicle && vehicle->transportPassengerIds.size() == 5) {
                loaded = true;
                std::ostringstream text;
                if (!writeRuntimeStateToStream(s, text, error)) return false;
                IniFile saved; saved.loadFromString(text.str());
                restored = std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*restored, s, s.visionEnabled);
                if (!materializeRuntimeLoadStateFromIni(*restored, saved, error)) return false;
                if (restored->triggers.teamMissions.empty()
                    || !restored->triggers.teamMissions.begin()->second.transportReturnCells.contains(truckId))
                    return fail("loaded transport lost its return destination");
            }
            if (loaded && vehicle && vehicle->transportPassengerIds.empty()) unloaded = true;
            const auto* yard = findObjectById(s, yardId);
            if (yard && yard->owner == aiOwner) break;
        }
        if (!loaded || !unloaded || findObjectById(s, yardId) == nullptr
            || findObjectById(s, yardId)->owner != aiOwner)
            return fail("stock transport engineer script did not steal the construction yard" + describe(s));
        for (int tick = 0; tick < 4200; ++tick) {
            ++restored->tick; advanceRuntimeSimulation(*restored);
            const auto* yard = findObjectById(*restored, yardId);
            if (yard && yard->owner == aiOwner) break;
        }
        if (findObjectById(*restored, yardId) == nullptr || findObjectById(*restored, yardId)->owner != aiOwner)
            return fail("saved transport assault did not capture the yard" + describe(*restored));
        std::cout << "Computer transport: stock 0C143EFC-G load five=yes travel=yes unload=yes capture=yes save/resume=yes\n";
    }
    {
        auto probe = makeWorld("YuriCountry"); auto& s = *probe;
        auto team = *findRuntimeTriggerTeamById(s, "08B95EFC-G"); team.house = aiOwner;
        std::vector<int> members;
        for (int x : {3, 5}) {
            const auto* engineer = addRuntimeObjectInstanceForOwner(s, aiOwner,
                RuntimeBuildKind::Infantry, "YENGINEER", {x, 22});
            if (engineer == nullptr) return fail("Yuri engineer unavailable");
            members.push_back(engineer->instanceId);
        }
        const auto* factory = addRuntimeObjectInstanceForOwner(s, humanOwner,
            RuntimeBuildKind::Building, "GAWEAP", {21, 16});
        if (factory == nullptr) return fail("factory capture target unavailable");
        // Factory=BuildingType makes a construction yard a factory too. The
        // stock target ranking prefers this existing, more valuable yard.
        int factoryId = -1;
        for (const auto& object : s.world.getObjects())
            if (object.owner == humanOwner && object.type == "GACNST") factoryId = object.instanceId;
        if (factoryId < 0) return fail("construction factory unavailable");
        const auto* power = addRuntimeObjectInstanceForOwner(s, humanOwner,
            RuntimeBuildKind::Building, "GAPOWR", {24, 6});
        if (power == nullptr) return fail("power capture target unavailable");
        const int powerId = power->instanceId;
        updateRuntimeVisionState(s);
        issueRuntimeTriggerTeamOrder(s, team, members);
        for (int tick = 0; tick < 4500; ++tick) {
            ++s.tick; advanceRuntimeSimulation(s);
            const auto* f = findObjectById(s, factoryId);
            const auto* p = findObjectById(s, powerId);
            if (f && p && f->owner == aiOwner && p->owner == aiOwner) break;
        }
        if (findObjectById(s, factoryId)->owner != aiOwner
            || findObjectById(s, powerId)->owner != aiOwner)
            return fail("stock engineer script did not capture factory then power" + describe(s));
        std::cout << "Computer engineers: stock 08B95EFC-G physical entry=yes factory then power=yes\n";
    }
    for (int mode = 0; mode <= 4; ++mode) {
        auto probe = makeWorld("Russians"); auto& s = *probe;
        RuntimeScriptType script; script.id = "transport-release-modes";
        script.steps = {{14, 0}, {43, 0}, {5, 100000}};
        s.triggers.scripts[script.id] = script;
        RuntimeTeamType team; team.id = script.id; team.scriptId = script.id; team.house = aiOwner;
        team.transportsReturnOnUnload = mode == 4; s.triggers.teams[team.id] = team;
        std::vector<int> members, trucks, passengers;
        for (int x : {6, 10}) {
            const auto* object = addRuntimeObjectInstanceForOwner(s, aiOwner,
                RuntimeBuildKind::Unit, "HTK", {x, 7});
            if (object == nullptr) return fail("two-transport fixture unavailable");
            members.push_back(object->instanceId); trucks.push_back(object->instanceId);
        }
        for (int x = 3; x < 13; ++x) {
            const auto* object = addRuntimeObjectInstanceForOwner(s, aiOwner,
                RuntimeBuildKind::Infantry, "SENGINEER", {x, 2});
            if (object == nullptr) return fail("transport passenger fixture unavailable");
            members.push_back(object->instanceId); passengers.push_back(object->instanceId);
        }
        updateRuntimeVisionState(s); issueRuntimeTriggerTeamOrder(s, team, members);
        const auto loadCount = [&]() {
            std::size_t count = 0;
            for (int id : trucks) if (const auto* object = findObjectById(s, id))
                count += object->transportPassengerIds.size();
            return count;
        };
        for (int tick = 0; tick < 2000 && loadCount() != 10; ++tick) {
            ++s.tick; advanceRuntimeSimulation(s);
        }
        if (loadCount() != 10 || s.triggers.teamMissions.empty())
            return fail("team could not distribute ten passengers over two trucks" + describe(s));
        ++s.tick; advanceRuntimeSimulation(s); // Finish wait-for-passengers before moving carriers.
        const int missionId = s.triggers.teamMissions.begin()->first;
        if (issueRuntimeTeamMove(s, trucks, {20, 20}) != 2)
            return fail("loaded carriers did not accept movement");
        for (int tick = 0; tick < 1800 && (s.behaviors.contains(trucks[0]) || s.behaviors.contains(trucks[1])); ++tick) {
            ++s.tick; advanceRuntimeSimulation(s);
        }
        if (runtimeCellDistance(runtimeCellPointForObject(*findObjectById(s, trucks[0])), {20, 20}) > 3)
            return fail("loaded carriers did not physically reach unload area");
        for (int id : trucks) {
            const auto* object = findObjectById(s, id);
            for (int y = object->y - 2; y <= object->y + 2; ++y)
                for (int x = object->x - 2; x <= object->x + 2; ++x)
                    s.terrainInfoByCell[runtimeCellKey(x, y)].passable = false;
        }
        if (unloadRuntimeTeamTransports(s, s.triggers.teamMissions.at(missionId), mode % 4)
            || loadCount() != 10 || s.triggers.teamMissions.at(missionId).objectIds.size() != 12)
            return fail("blocked unload released members or skipped cargo");
        for (auto& [key, info] : s.terrainInfoByCell) info.passable = true;
        if (!unloadRuntimeTeamTransports(s, s.triggers.teamMissions.at(missionId), mode % 4)
            || loadCount() != 0) return fail("unload did not resume when exits opened");
        auto retained = s.triggers.teamMissions.at(missionId).objectIds;
        auto expected = mode == 0 ? members : mode == 1 ? trucks
            : (mode == 2 || mode == 4) ? passengers : std::vector<int>{};
        std::sort(retained.begin(), retained.end()); std::sort(expected.begin(), expected.end());
        if (retained != expected) return fail("original transport detach mode retained the wrong members");
        if (mode == 4) {
            for (int tick = 0; tick < 1800 && (s.behaviors.contains(trucks[0]) || s.behaviors.contains(trucks[1])); ++tick) {
                ++s.tick; advanceRuntimeSimulation(s);
            }
            const auto& origins = s.triggers.teamMissions.at(missionId).transportReturnCells;
            for (int id : trucks)
                if (runtimeCellDistance(runtimeCellPointForObject(*findObjectById(s, id)), origins.at(id)) > 1)
                    return fail("released transport did not physically return to its loading point");
        }
    }
    std::cout << "Computer transports: two full trucks=yes blocked unload waits=yes detach modes 0/1/2/3=yes return to loading point=yes\n";
    {
        auto probe = makeWorld("Americans"); auto& s = *probe;
        const auto* tank = addRuntimeObjectInstanceForOwner(s, aiOwner,
            RuntimeBuildKind::Unit, "MTNK", {2, 2});
        if (tank == nullptr) return fail("tank target probe unavailable");
        const int tankId = tank->instanceId;
        auto* first = addRuntimeObjectInstanceForOwner(s, humanOwner,
            RuntimeBuildKind::Building, "GAPOWR", {17, 2});
        if (first == nullptr) return fail("first power unavailable");
        first->health = 1; const int firstId = first->instanceId;
        const auto* second = addRuntimeObjectInstanceForOwner(s, humanOwner,
            RuntimeBuildKind::Building, "GAPOWR", {25, 14});
        if (second == nullptr) return fail("second power unavailable");
        const int secondId = second->instanceId;
        RuntimeScriptType script; script.id = "attack-one-power";
        script.steps = {{0, 9}, {49, 0}}; s.triggers.scripts[script.id] = script;
        RuntimeTeamType team; team.id = script.id; team.scriptId = script.id; team.house = aiOwner;
        s.triggers.teams[team.id] = team;
        RuntimeCampaignAiTriggerDefinition trigger; trigger.id = script.id;
        trigger.currentWeight = 30; trigger.minimumWeight = 10; trigger.maximumWeight = 70;
        s.triggers.campaignAiTriggers.push_back(trigger);
        updateRuntimeVisionState(s);
        issueRuntimeTriggerTeamOrder(s, team, {tankId}, trigger.id);
        for (int tick = 0; tick < 2300 && !s.triggers.teamMissions.empty(); ++tick) {
            ++s.tick; advanceRuntimeSimulation(s);
        }
        if (findObjectById(s, firstId) != nullptr || findObjectById(s, secondId) == nullptr
            || s.triggers.campaignAiTriggers.front().timesCompleted != 1)
            return fail("power attack did not travel, destroy one target and continue" + describe(s));
        std::cout << "Computer attack: power quarry=yes travel through shroud=yes one target then success=yes\n";
    }
    return true;
}
