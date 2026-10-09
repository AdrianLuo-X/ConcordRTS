bool runRuntimeComputerBasePlanSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "AI base targets: " + why; return false; };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    stock->rulesLoaded = stock->artLoaded = stock->theaterLoaded = true;
    rebuildRuntimeTerrainInfoCache(*stock);
    const TerrainCell* land = nullptr;
    for (const auto& cell : stock->world.getTerrainCells()) {
        const auto& info = stock->terrainInfoByCell.at(runtimeCellKey(cell.x, cell.y));
        if (!info.water && !info.bridge && !info.shore && !info.ramp && info.passable) {
            land = &cell; break;
        }
    }
    if (!land) return fail("no flat original land tile");
    const char* outputRoot = std::getenv("CONCORD_AI_BUILDING_TARGETS_DIR");
    const std::string root = outputRoot && *outputRoot ? outputRoot
        : "build/smoke/feature-checks/08-ai-building-targets/checks";
    std::filesystem::create_directories(root);
    std::ofstream records(root + "/construction-records.csv");
    records << std::unitbuf;
    records << "scenario,decision,tick,type,defenses,ordinary_refineries,slave_miners,tech_centers,credits\n";
    const std::string owner = runtimeSkirmishOwnerForSlot(1);
    const std::string key = toLowerAsciiLocal(owner);
    const auto make = [&](const std::string& country, const std::string& difficulty, int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.fixedTickHz = hz; s.config.gameMode = "Battle";
        s.config.gameModeRulesOverride.clear(); s.config.playerSlots.clear();
        s.config.multiplayerActive = s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.config.noCooldown = false; s.config.computerAiEnabled = true; s.config.techLevel = 10;
        RuntimeConfig::PlayerSlot human; human.enabled = human.human = true;
        human.country = "Americans"; human.color = "Gold"; human.startPosition = 1;
        s.config.playerSlots.push_back(human);
        RuntimeConfig::PlayerSlot computer; computer.enabled = true; computer.human = false;
        computer.country = country; computer.difficulty = difficulty; computer.color = "Red"; computer.startPosition = 2;
        s.config.playerSlots.push_back(computer);
        s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater; s.report = stock->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = runtimeSkirmishOwnerForSlot(0); s.economy.skirmishStartApplied = true;
        s.economy.techLevel = 10; s.triggers.bootstrapComplete = true;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 64; ++y) for (int x = 1; x <= 64; ++x) {
            TerrainCell c; c.x = x; c.y = y; c.cellIndex512 = runtimeCellKey(x, y);
            c.tileIndex = land->tileIndex; c.subTileIndex = land->subTileIndex;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        const int side = runtimeBasePlanningSideForCountry(country);
        const char* yard = side == 2 ? "YACNST" : side == 1 ? "NACNST" : "GACNST";
        (void)addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, yard, {32, 32});
        // Keep the human slot alive when a congested factory exit needs real
        // movement ticks; this fixture is not a one-player victory scenario.
        (void)addRuntimeObjectInstanceForOwner(s, s.economy.owner,
            RuntimeBuildKind::Building, "GACNST", {4, 54});
        RuntimeComputerAiState ai; ai.initialized = true; ai.owner = owner; ai.country = country;
        ai.difficulty = difficulty; ai.baseCenter = {32, 32}; ai.credits = 1000000;
        ai.nextAttackTick = ai.nextRepairTick = std::numeric_limits<int>::max();
        s.computerAiByOwner[key] = ai;
        return box;
    };
    const auto step = [&](RuntimeWindowState& s, const std::string& scenario, int decision) {
        auto& ai = s.computerAiByOwner.at(key);
        const int serial = ai.productionSerial;
        s.tick = std::max({s.tick + 1, ai.nextThinkTick, ai.nextBuildTick});
        // Run the complete current frame at each AI deadline, so lighting,
        // support-power and save authorities advance with the construction.
        advanceRuntimeSimulation(s);
        if (ai.status.rfind("clearing production exit for ", 0) == 0) {
            // Do not skip the physical exit or teleport a blocking miner.
            // Pause new AI production while its real movement clears the bay.
            const int deadline = ai.nextBuildTick;
            ai.nextBuildTick = std::numeric_limits<int>::max();
            for (int tick = 0; tick < s.config.fixedTickHz * 5; ++tick) {
                ++s.tick; advanceRuntimeSimulation(s);
            }
            ai.nextBuildTick = deadline;
        }
        const auto m = runtimeComputerAiMetrics(s, ai);
        if (ai.productionSerial != serial) records << scenario << ',' << decision << ',' << s.tick << ','
            << ai.lastBuildType << ',' << m.defenses << ',' << m.ordinaryRefineries << ',' << m.slaveMiners << ','
            << m.techCenters << ',' << ai.credits << '\n';
        return m;
    };
    const auto refineryCount = [](const RuntimeComputerAiMetrics& m, bool slave) {
        return slave ? m.slaveMiners : m.ordinaryRefineries;
    };
    int cases = 0;
    const auto complete = [&](RuntimeWindowState& s, const std::string& scenario,
            int defenseGoal, int refineryGoal, bool slave) {
        auto& ai = s.computerAiByOwner.at(key);
        const auto targets = runtimeComputerAiBaseTargets(s, ai);
        if (targets.defenses != defenseGoal || targets.refineries != refineryGoal || targets.slaveRefineries != slave)
            return fail(scenario + " wrong rule targets");
        int lastSerial = -1;
        for (int i = 0; i < 160; ++i) {
            const auto m = step(s, scenario, i);
            if (m.defenses > defenseGoal || refineryCount(m, slave) > refineryGoal)
                return fail(scenario + " exceeded planned count");
            if (m.defenses == defenseGoal && refineryCount(m, slave) == refineryGoal
                    && m.techCenters == 1 && m.vehicleFactories >= 1 && m.radars >= 1) {
                for (int j = 0; j < 6; ++j) step(s, scenario + "-settled", j);
                const auto settled = runtimeComputerAiMetrics(s, ai);
                if (settled.defenses != defenseGoal || refineryCount(settled, slave) != refineryGoal)
                    return fail(scenario + " did not stop at target");
                std::cout << scenario << " passed: defenses=" << settled.defenses << " refineries="
                    << refineryCount(settled, slave) << " builds=" << ai.structuresBuilt << '\n';
                return true;
            }
            if (lastSerial == ai.productionSerial && ai.status.find("placement blocked") != std::string::npos)
                return fail(scenario + " " + ai.status);
            lastSerial = ai.productionSerial;
        }
        const auto m = runtimeComputerAiMetrics(s, ai);
        return fail(scenario + " did not finish: " + ai.status + " defenses=" + std::to_string(m.defenses)
            + " refineries=" + std::to_string(refineryCount(m, slave)) + " tech=" + std::to_string(m.techCenters));
    };
    const std::array<std::string, 3> countries{"Americans", "Russians", "YuriCountry"};
    const std::array<std::string, 4> difficulties{"Easy", "Medium", "Hard", "Brutal"};
    for (int hz : {30, 60}) for (int side = 0; side < 3; ++side) for (int d = 0; d < 4; ++d) {
        auto box = make(countries[side], difficulties[d], hz); auto& s = *box;
        const int defenseGoal = d == 0 ? 6 : d == 1 ? (side == 0 ? 20 : 22) : 25;
        const int refineryGoal = side == 2 ? (d == 0 ? 2 : d == 1 ? 3 : 4) : (d == 0 ? 1 : d == 1 ? 2 : 3);
        const std::string name = countries[side] + "-" + difficulties[d] + "-" + std::to_string(hz);
        if (!complete(s, name, defenseGoal, refineryGoal, side == 2)) return false;
        ++cases;
        // A real loss must reopen a defense slot and the appropriate refinery
        // slot. A mobile Yuri miner is a valid refinery-slot victim too.
        int defenseId = -1, refineryId = -1;
        for (const auto& object : s.world.getObjects()) {
            if (!object.alive || !runtimeComputerAiOwnerMatches(object, owner)) continue;
            const auto* b = findRuntimeBuildingTypeById(s.rules, object.type);
            const auto* u = findRuntimeUnitType(s.rules, object);
            if (b && runtimeComputerAiBuildingCountsAsDefense(*b)) defenseId = object.instanceId;
            if (side != 2 && b && runtimeBuildingCountsAsRefinery(*b)) refineryId = object.instanceId;
            if (side == 2 && ((u && runtimeUnitIsMobileSlaveMiner(*u))
                    || (b && runtimeBuildingCanGatherResources(*b)))) refineryId = object.instanceId;
        }
        if (defenseId < 0 || refineryId < 0) return fail(name + " missing loss fixture");
        for (const int objectId : {defenseId, refineryId}) {
            RuntimeObjectExitRequest removal;
            removal.objectId = objectId; removal.tick = s.tick;
            removal.kind = RuntimeObjectExitKind::Destroyed;
            removal.reason = "AI base target loss fixture";
            removal.suppressCrew = removal.suppressDeathWeapon = true;
            if (queueRuntimeObjectExit(s, removal) != RuntimeObjectExitQueueResult::Queued)
                return fail(name + " loss was not queued");
            drainRuntimeObjectExitQueue(s);
            if (findObjectById(s, objectId) != nullptr) return fail(name + " loss was not finalized");
        }
        if (!complete(s, name + "-rebuild", defenseGoal, refineryGoal, side == 2)) return false;
        ++cases;
        if (hz == 30 && d == 1) {
            // Save at an unfinished point, with live counts and AI deadlines.
            auto& ai = s.computerAiByOwner.at(key);
            ai.difficulty = "Hard";
            const auto path = root + "/base-" + std::to_string(side) + ".ini";
            if (!saveRuntimeState(s, path, error)) return fail("save " + error);
            auto restored = make(countries[side], "Medium", hz);
            if (!loadRuntimeState(*restored, path, error)) return fail("load " + error);
            for (int i = 0; i < 12; ++i) {
                const auto a = step(s, name + "-live", i);
                const auto b = step(*restored, name + "-restored", i);
                const auto& restoredAi = restored->computerAiByOwner.at(key);
                if (a.defenses != b.defenses || a.ordinaryRefineries != b.ordinaryRefineries
                        || a.slaveMiners != b.slaveMiners || ai.lastBuildType != restoredAi.lastBuildType
                        || ai.productionSerial != restoredAi.productionSerial || ai.credits != restoredAi.credits
                        || ai.nextBuildTick != restoredAi.nextBuildTick)
                    return fail(name + " save/load decisions diverged");
            }
            ++cases;
        }
    }
    const auto overrideRules = [&](RuntimeWindowState& s, const std::string& overlay) {
        IniFile base, extra;
        if (!base.loadFromFile("local_data/rulesmd.ini") || !extra.loadFromString(overlay)) return false;
        base.appendFrom(extra);
        return s.rules.loadFromIni(base);
    };
    // Actual builds using non-stock counts, including zero-defense plans.
    for (int side = 0; side < 3; ++side) for (int d = 0; d < 3; ++d) {
        auto box = make(countries[side], difficulties[d], 30); auto& s = *box;
        if (!overrideRules(s, "[General]\nAlliedBaseDefenseCounts=4,3,0\nSovietBaseDefenseCounts=5,2,0\n"
                "ThirdBaseDefenseCounts=3,4,0\nAIExtraRefineries=3,0,1\nAISlaveMinerNumber=2,1,3\n"))
            return fail("override load");
        const int expected[3][3]{{0,3,4},{0,2,5},{0,4,3}};
        const int refineryGoal = side == 2 ? (d == 0 ? 3 : d == 1 ? 1 : 2) : (d == 0 ? 2 : d == 1 ? 1 : 4);
        if (!complete(s, "override-" + countries[side] + "-" + difficulties[d], expected[side][d], refineryGoal, side == 2))
            return false;
        ++cases;
    }
    {
        auto box = make("YuriCountry", "Hard", 30); auto& s = *box; auto& ai = s.computerAiByOwner.at(key);
        if (!overrideRules(s, "[General]\nAIExtraRefineries=99,98,97\n")) return fail("slave exception overlay");
        if (!complete(s, "slave-ignores-extra", 25, 4, true)) return false;
        // Move all deployed forms into their mobile type, preserving identity.
        for (auto& object : s.world.accessObjects()) {
            const auto* b = findRuntimeBuildingTypeById(s.rules, object.type);
            if (object.alive && b && runtimeBuildingCanGatherResources(*b)) {
                object.type = b->undeploysInto; object.kind = WorldObjectKind::Unit;
            }
        }
        const auto before = runtimeComputerAiMetrics(s, ai);
        if (before.slaveMiners != 4 || before.refineries != 0) return fail("mobile-form counting");
        for (int i = 0; i < 12; ++i) step(s, "all-slaves-mobile", i);
        if (runtimeComputerAiMetrics(s, ai).slaveMiners != 4) return fail("undeployment duplicated refinery");
        ++cases;
    }
    {
        auto box = make("Americans", "Hard", 30); auto& s = *box; auto& ai = s.computerAiByOwner.at(key);
        if (!overrideRules(s, "[General]\nHarvesterUnit=CMIN\n[CMIN]\nOwner=YuriCountry\n")) return fail("harvester owner overlay");
        if (!runtimeComputerAiBaseTargets(s, ai).slaveRefineries) return fail("normal country with no owned HarvesterUnit");
        ai.country = "YuriCountry";
        if (runtimeComputerAiBaseTargets(s, ai).slaveRefineries || runtimeComputerAiBaseTargets(s, ai).refineries != 3)
            return fail("Yuri with owned HarvesterUnit still used slave exception");
        ++cases;
    }
    {
        auto box = make("Americans", "Hard", 30); auto& s = *box; auto& ai = s.computerAiByOwner.at(key);
        ai.credits = 0;
        const auto m = step(s, "insufficient-credit", 0);
        if (m.structures != 1 || ai.structuresBuilt != 0 || ai.stalledBuilds != 1) return fail("low funds advanced plan");
        ai.credits = 1000000;
        if (!complete(s, "credit-recovered", 25, 3, false)) return false;
        ++cases;
    }
    {
        auto box = make("Americans", "Easy", 30); auto& s = *box; auto& ai = s.computerAiByOwner.at(key);
        std::string overlay = "[General]\nAlliedBaseDefenseCounts=8,8,8\n";
        for (const auto& b : stock->rules.getBuildingTypes()) if (runtimeComputerAiBuildingCountsAsDefense(b))
            overlay += "[" + b.id + "]\n" + (b.id == "GAPILL" ? "BuildLimit=2\n" : "TechLevel=-1\n");
        if (!overrideRules(s, overlay)) return fail("build-limit overlay");
        for (int i = 0; i < 60; ++i) step(s, "build-limit", i);
        const auto m = runtimeComputerAiMetrics(s, ai);
        if (m.defenses != 2 || m.techCenters != 1) return fail("build limit ignored or stalled technology");
        ++cases;
    }
    {
        auto box = make("Russians", "Medium", 30); auto& s = *box; auto& ai = s.computerAiByOwner.at(key);
        // Enemy and dead structures must not satisfy this owner's quota.
        (void)addRuntimeObjectInstanceForOwner(s, "Enemy", RuntimeBuildKind::Defense, "NALASR", {5,5});
        (void)addRuntimeObjectInstanceForOwner(s, "Enemy", RuntimeBuildKind::Building, "NAREFN", {8,5});
        auto* dead = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Defense, "NALASR", {15,5});
        if (!dead) return fail("owner fixture"); dead->alive = false;
        const auto m = runtimeComputerAiMetrics(s, ai);
        if (m.defenses != 0 || m.ordinaryRefineries != 0) return fail("enemy/dead counted");
        if (!complete(s, "enemy-and-dead", 22, 2, false)) return false;
        ++cases;
    }
    {
        auto box = make("Americans", "Easy", 30); auto& s = *box; auto& ai = s.computerAiByOwner.at(key);
        // Explicit BuildRefinery list changes the chosen type. The captured
        // construction-yard compatibility is provided by the fixture.
        if (!overrideRules(s, "[AI]\nBuildRefinery=NAREFN\n[NAREFN]\nOwner=Americans\nAIBasePlanningSide=0\nPrerequisite=POWER\n"))
            return fail("refinery-list overlay");
        (void)addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, "GAPOWR", {24,32});
        const auto choice = runtimeComputerAiSelectStructureForRole(s, ai, RuntimeComputerAiStructureRole::Refinery);
        if (!choice || choice->typeId != "NAREFN") return fail("BuildRefinery selection not honored");
        ++cases;
    }
    {
        auto box = make("Americans", "Hard", 30); auto& s = *box; auto& ai = s.computerAiByOwner.at(key);
        const auto hard = runtimeComputerAiBasePlan(ai, runtimeComputerAiBaseTargets(s, ai));
        ai.difficulty = " bRuTaL ";
        if (hard != runtimeComputerAiBasePlan(ai, runtimeComputerAiBaseTargets(s, ai))) return fail("fourth difficulty quantity fork");
        ai.difficulty = "normal";
        if (runtimeComputerAiBaseTargets(s, ai).defenses != 20) return fail("normal alias mapping");
        using Role = RuntimeComputerAiStructureRole;
        if (hard[0] != Role::Power || hard[1] != Role::Refinery
                || std::find(hard.begin(), hard.begin()+4, Role::Defense) != hard.begin()+4)
            return fail("defense inserted before allowed opening range");
        // Stock plan must interleave expansion, not impose all defenses first.
        const auto tech = std::find(hard.begin(), hard.end(), Role::Tech);
        if (tech == hard.end() || std::find(tech, hard.end(), Role::Defense) == hard.end())
            return fail("stock plan still forces all defenses before technology");
        ++cases;
    }
    {
        const std::string base = "[BuildingTypes]\n0=TEST\n[TEST]\nStrength=100\n[General]\n";
        IniFile ini; RulesDatabase baseline;
        if (!ini.loadFromString(base) || !baseline.loadFromIni(ini)) return fail("fallback fixture");
        const auto& b = baseline.getComputerBaseBuildingRules();
        if (b.alliedDefenseCounts != std::array<int,3>{25,20,6} || b.sovietDefenseCounts != std::array<int,3>{25,22,6}
                || b.thirdDefenseCounts != std::array<int,3>{25,22,6} || b.extraRefineries != std::array<int,3>{2,1,0})
            return fail("stock fallbacks");
        ++cases;
        for (const auto* field : {"AlliedBaseDefenseCounts", "SovietBaseDefenseCounts", "ThirdBaseDefenseCounts", "AIExtraRefineries"}) {
            const std::array<int,3> original = std::string(field) == "AIExtraRefineries" ? std::array<int,3>{2,1,0}
                : std::string(field) == "AlliedBaseDefenseCounts" ? std::array<int,3>{25,20,6} : std::array<int,3>{25,22,6};
            for (int index = 0; index < 3; ++index) {
                auto changed = original; ++changed[index];
                RulesDatabase rules;
                const auto text = base + field + "=" + std::to_string(changed[0]) + ',' + std::to_string(changed[1]) + ',' + std::to_string(changed[2]) + '\n';
                if (!ini.loadFromString(text) || !rules.loadFromIni(ini) || rules.getDetailRulesHash() == baseline.getDetailRulesHash())
                    return fail(std::string(field) + " not in detail hash");
                ++cases;
            }
        }
        for (const auto* setting : {"[AI]\nBuildRefinery=NAREFN", "HarvesterUnit=CMIN"}) {
            RulesDatabase rules;
            if (!ini.loadFromString(base + setting + '\n') || !rules.loadFromIni(ini)
                    || rules.getDetailRulesHash() == baseline.getDetailRulesHash()) return fail("list hash");
            ++cases;
        }
        RulesDatabase partial;
        if (!ini.loadFromString(base+"AlliedBaseDefenseCounts=5,-2\n") || !partial.loadFromIni(ini)
                || partial.getComputerBaseBuildingRules().alliedDefenseCounts != std::array<int,3>{5,0,6})
            return fail("partial list fallback and nonnegative counts");
        ++cases;
    }
    std::cout << "AI base targets passed: " << cases << " cases; actual construction records, 3 sides x 4 difficulties x 2 rates, "
        "losses, limits, slave forms, rule overrides, save/load, deterministic plans and compatibility hashes\n";
    return true;
}
