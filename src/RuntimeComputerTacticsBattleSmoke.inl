bool runRuntimeComputerTacticsBattleSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& text) { error = "computer normal opening: " + text; return false; };
    for (const std::string country : {"Americans", "Russians", "YuriCountry"}) {
        const bool diagnoseStall = std::getenv("CONCORD_COMPUTER_OPENING_STALL_DIAGNOSTIC") != nullptr;
        if (diagnoseStall && country != "YuriCountry") continue;
        const bool resumeStalled = std::getenv("CONCORD_COMMAND_SMOKE_COMPUTER_OPENING_RESUME_ONLY") != nullptr;
        if (resumeStalled && country != "YuriCountry") continue;
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope scope(state);
        state.config = config; state.config.gameMode = "Battle"; state.config.gameModeRulesOverride.clear();
        state.config.startingCredits = 10000; state.config.startingUnits = 0;
        state.config.techLevel = 10; state.config.shortGame = false;
        state.config.crates = false; state.config.superweapons = false; state.config.computerAiEnabled = true;
        state.config.fixedTickHz = 30; state.config.playerCountry = "Americans";
        state.config.playerSlots.clear(); state.config.playerSlots.resize(2);
        for (int i = 0; i < 2; ++i) {
            auto& slot = state.config.playerSlots[i]; slot.enabled = true; slot.human = slot.local = i == 0;
            slot.country = i == 0 ? "Americans" : country; slot.difficulty = "Hard";
            slot.startPosition = i + 1; slot.team = "-"; slot.name = i == 0 ? "Stationary opponent" : "Computer";
        }
        state.runtimeSuppressPlayerAudioFeedback = true; state.runtimePreferencePersistenceEnabled = false;
        state.report = buildRuntimeStartupReport(state.config, &state.world, &state.theater,
            &state.art, &state.rules, &state.mapFacts);
        state.theaterLoaded = state.report.theaterLoaded; state.artLoaded = state.report.artLoaded;
        state.rulesLoaded = state.report.rulesLoaded;
        if (!state.report.success) return fail(country + " original map/rules unavailable");
        initializeRuntimeNavigation(state); initializeRuntimeEconomy(state);
        if (!initializeRuntimeSkirmishStart(state) || !initializeRuntimeTriggers(state))
            return fail(country + " normal skirmish setup failed");
        if (!deployRuntimeMcv(state,state.economy.skirmishStartObjectId,false))
            return fail(country+" stationary opponent deployment");
        if (state.triggers.computerAiTriggers.size() != 165) return fail(country + " not using the complete original AI catalog");
        if (resumeStalled) {
            std::ifstream file("build/smoke/gameplay/05-probe/opening-YuriCountry-stalled-30000.ini");
            std::ostringstream text; text << file.rdbuf(); IniFile saved;
            saved.loadFromString(text.str());
            if (!file || !materializeRuntimeLoadStateFromIni(state, saved, error)) return false;
            const auto key = toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(1));
            const int initialIncome = state.computerAiByOwner.at(key).incomeEarned;
            const int initialBuilt = state.computerAiByOwner.at(key).unitsBuilt;
            if (const auto* blocked = findObjectById(state, 49)) {
                const auto occupied = buildRuntimeOccupancyLookup(state, blocked->instanceId, nullptr, nullptr);
                std::cout << "Production obstruction: " << blocked->type << " at=" << blocked->x << ',' << blocked->y << '\n';
                for (auto cell : runtimeNeighborsOf(runtimeCellPointForObject(*blocked))) {
                    const auto occupant = occupied.find(runtimeCellKey(cell.x, cell.y));
                    std::cout << "Exit neighbor " << cell.x << ',' << cell.y
                        << " occupied=" << (occupant == occupied.end() ? -1 : occupant->second)
                        << " passable=" << runtimeCellTransitionIsPassable(state, state.terrainByCell, occupied,
                            runtimeCellPointForObject(*blocked), cell, *blocked, -1, nullptr, false)
                        << " factory-passage=" << runtimeCellTransitionIsPassable(state, state.terrainByCell, occupied,
                            runtimeCellPointForObject(*blocked), cell, *blocked, 17, nullptr, false) << '\n';
                }
            }
            for (const auto& [id, task] : state.slaveMinerTasks) {
                const auto* worker = findObjectById(state, id);
                const auto* parent = findObjectById(state, task.refineryId);
                if (!worker || !parent) continue;
                const auto ore = findRuntimeHarvesterResourceApproach(state, *worker, task.targetOre);
                const auto entry = findRuntimeEngineerEntryCell(state, *worker, *parent, "slave-delivery", 2);
                std::cout << "Stalled worker " << id << " phase=" << runtimeSlaveMinerPhaseName(task.phase)
                    << " reachable ore=" << (ore ? std::to_string(ore->x) + "," + std::to_string(ore->y) : "none")
                    << " return=" << (entry ? std::to_string(entry->x) + "," + std::to_string(entry->y) : "none") << '\n';
            }
            if (std::getenv("CONCORD_COMPUTER_EXIT_DIAGNOSTIC")) return fail("exit diagnosis requested");
            for (int frame = 0; frame < 3600; ++frame) {
                ++state.tick; advanceRuntimeSimulation(state);
                const auto& ai = state.computerAiByOwner.at(key);
                if (ai.incomeEarned > initialIncome && ai.unitsBuilt > initialBuilt) {
                    std::cout << "Yuri stalled opening resumed: carried ore delivered=yes paid vehicle completed=yes tick=" << state.tick << '\n';
                    return true;
                }
            }
            std::ostringstream textAfter; std::string detail;
            if (writeRuntimeStateToStream(state, textAfter, detail)) {
                std::ofstream out("build/smoke/gameplay/05-probe/opening-YuriCountry-recovery.ini"); out << textAfter.str();
            }
            return fail("stalled opening did not recover: " + state.computerAiByOwner.at(key).status
                + " income=" + std::to_string(initialIncome) + ":" + std::to_string(state.computerAiByOwner.at(key).incomeEarned));
        }
        const std::string owner = runtimeSkirmishOwnerForSlot(1), key = toLowerAsciiLocal(owner);
        const std::string opponent = runtimeSkirmishOwnerForSlot(0);
        // Keep an idle observer's starting base alive long enough to see the
        // computer's complete opening. Computer money, factories, production,
        // ore, damage and movement use the unmodified normal simulation.
        int opponentBase = -1;
        for (auto& object : state.world.accessObjects()) if (object.alive && object.owner == opponent) {
            if (object.kind == WorldObjectKind::Structure || toUpperAsciiLocal(object.type) == "AMCV") {
                object.health = 1000000; opponentBase = object.instanceId; break;
            }
        }
        if (opponentBase < 0) return fail("stationary opponent has no starting base");
        // An unarmed airborne observer prevents engineer or mind-control
        // victories from ending observation before the guard team is built.
        const auto* base = findObjectById(state, opponentBase);
        RuntimeCellPoint observationCell{base ? base->x : 0, base ? base->y : 0};
        double observationDistance = 0;
        for (const auto& cell : state.world.getTerrainCells()) {
            const auto info = runtimeCachedTerrainInfoForCell(state, cell);
            if (!info.passable || info.water || info.cliff) continue;
            const double distance = base ? runtimeCellDistance({cell.x, cell.y}, runtimeCellPointForObject(*base)) : 0;
            if (distance > observationDistance) { observationDistance = distance; observationCell = {cell.x, cell.y}; }
        }
        auto* observer = base ? addRuntimeObjectInstanceForOwner(state, opponent,
            RuntimeBuildKind::Unit, "SPYP", observationCell) : nullptr;
        if (!observer) return fail("unarmed observation unit unavailable");
        observer->health = 1000000; observer->mission = "Sleep";
        observer->airborne = true; observer->dockedToObjectId = -1;
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        state.visionEnabled = true; updateRuntimeVisionState(state);
        std::set<std::string> launchedTypes;
        bool offense = false, defense = false, damage = false;
        int lastStructures = 0, lastIncome = 0, lastBuilt = 0;
        if (diagnoseStall) std::cout << "Yuri opening initialized: starting real frames\n";
        for (int frame = 0; frame < 30000; ++frame) {
            const auto started = diagnoseStall ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            ++state.tick; advanceRuntimeSimulation(state);
            if (diagnoseStall && std::chrono::steady_clock::now() - started > std::chrono::milliseconds(500)) {
                std::ostringstream saved; std::string saveError;
                if (writeRuntimeStateToStream(state, saved, saveError)) {
                    std::ofstream out("build/smoke/gameplay/opening-YuriCountry-slow-frame.ini"); out << saved.str();
                }
                std::cout << "Yuri slow frame tick=" << state.tick << " status=" << state.computerAiByOwner.at(key).status
                    << " elapsedMs=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()
                    << " workers=" << state.slaveMinerTasks.size() << " save=" << saveError << '\n';
                return fail("slow-frame diagnostic saved");
            }
            const auto ai = state.computerAiByOwner.find(key);
            if (ai == state.computerAiByOwner.end()) continue;
            lastStructures = ai->second.structuresBuilt; lastIncome = ai->second.incomeEarned;
            lastBuilt = ai->second.infantryBuilt + ai->second.unitsBuilt;
            if (const auto tactics = state.triggers.computerTacticsByOwner.find(key);
                tactics != state.triggers.computerTacticsByOwner.end()) {
                for (const auto& [id, team] : tactics->second.teams) if (team.launched) {
                    launchedTypes.insert(team.teamTypeId);
                    if (team.baseDefense) defense = true; else offense = true;
                }
            }
            const auto* target = findRuntimeObjectByIdIncludingInactive(state, opponentBase);
            damage |= target && (target->health < 1000000 || target->owner == owner);
            if (frame % 3000 == 2999) std::cout << "Computer normal opening progress: " << country
                << " tick=" << state.tick << " structures=" << lastStructures << " income=" << lastIncome
                << " built=" << lastBuilt << " teamTypes=" << launchedTypes.size() << " damage=" << damage << '\n';
            if (lastStructures >= 6 && lastIncome > 0 && lastBuilt >= 6 && offense && defense && damage) break;
            if (state.skirmishMatchFinished) break;
        }
        if (lastStructures < 6 || lastIncome <= 0 || lastBuilt < 6 || !offense || !defense || !damage
            || state.triggers.unitsCreated != 0 || state.triggers.unsupportedScriptSteps != 0) {
            std::ostringstream saved; std::string saveError;
            if (writeRuntimeStateToStream(state, saved, saveError)) {
                std::ofstream out(std::filesystem::path("build/smoke/gameplay/05-probe") / ("opening-" + country + ".ini"));
                out << saved.str();
            }
            return fail(country + " unfinished tick=" + std::to_string(state.tick) + " structures=" + std::to_string(lastStructures)
                + " income=" + std::to_string(lastIncome) + " built=" + std::to_string(lastBuilt)
                + " defense=" + std::to_string(defense) + " offense=" + std::to_string(offense) + " damage=" + std::to_string(damage));
        }
        std::cout << "Computer normal opening: " << country << " full stock catalog=yes normal starting money=yes"
            << " built own base=yes real mining=yes paid guard and offensive teams=yes physical attack=yes"
            << " distinct teams=" << launchedTypes.size() << " tick=" << state.tick << '\n';
    }
    return true;
}
