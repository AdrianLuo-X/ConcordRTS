#include "RuntimeSkirmishModeDetailsSmoke.inl"

bool runRuntimeSkirmishModesSmoke(const RuntimeConfig& config, std::string& error) {
    int passed = 0;
    std::vector<std::string> failures;
    const auto runCase = [&](const std::string& mode, const std::string& country) {
        if (const char* filter = std::getenv("CONCORD_SKIRMISH_MODE_FILTER"); filter && mode != filter)
            return true;
        if (const char* filter = std::getenv("CONCORD_SKIRMISH_COUNTRY_FILTER"); filter && country != filter)
            return true;
        const bool meat = mode == "MeatGrind";
        const bool naval = mode == "NavalWar";
        const bool wealth = mode == "Megawealth";
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config = config;
        state.config.gameMode = mode;
        state.config.gameModeRulesOverride = runtimeDefaultModeRulesOverride(mode);
        if (naval || wealth)
            state.config.defaultMapPath = (std::filesystem::path(config.localDataRootPath)
                / "resources/mix_exports/multimd" / (naval ? "xmp21s2.map" : "xmp06mw.map")).string();
        state.config.multiplayerActive = false;
        state.config.computerAiEnabled = true;
        state.config.shortGame = true;
        state.config.startingUnits = 6;
        state.config.startingCredits = 200000;
        state.config.techLevel = 10;
        state.config.superweapons = true;
        state.config.crates = false;
        state.config.playerSlots.assign(2, {});
        for (std::size_t index = 0; index < 2; ++index) {
            auto& slot = state.config.playerSlots[index];
            slot.enabled = true;
            slot.human = index == 0;
            slot.local = index == 0;
            slot.country = country;
            slot.team = mode == "TeamGame" ? std::to_string(index + 1) : "-";
            slot.startPosition = static_cast<int>(index) + 1;
        }
        state.runtimeSoundsEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.runtimePreferencePersistenceEnabled = false;
        const auto fail = [&](const std::string& detail) {
            failures.push_back(mode + "/" + country + ": " + detail);
            std::cerr << failures.back() << '\n';
            return false;
        };
        state.report = buildRuntimeStartupReport(state.config, &state.world,
            &state.theater, &state.art, &state.rules, &state.mapFacts);
        state.rulesLoaded = state.report.rulesLoaded;
        state.artLoaded = state.report.artLoaded;
        state.theaterLoaded = state.report.theaterLoaded;
        if (!state.report.success) return fail("original assets failed to load");
        initializeRuntimeNavigation(state);
        initializeRuntimeEconomy(state);
        if (!state.skirmishModeRulesLoaded || !initializeRuntimeSkirmishStart(state))
            return fail("opening failed: " + state.economy.skirmishStatus);
        state.triggers.bootstrapComplete = true;
        state.visionEnabled = true;
        updateRuntimeVisionState(state);
        for (std::size_t index = 0; index < 2; ++index) {
            const auto* stats = state.vision.statsFor(runtimeSkirmishOwnerForSlot(index));
            if (!stats || stats->validCells <= 0 || stats->exploredCells <= 0
                || (stats->exploredCells == stats->validCells) != (mode == "Duel")
                || stats->visibleCells == stats->validCells)
                return fail("opening shroud or ordinary current sight differs from mode rules");
        }
        const bool soviet = runtimeCountryIsSoviet(country);
        const bool yuri = runtimeCountryIsYuri(country);
        const std::string power = yuri ? "YAPOWR" : soviet ? "NAPOWR" : "GAPOWR";
        const std::string refinery = yuri ? "YAREFN" : soviet ? "NAREFN" : "GAREFN";
        const std::string barracks = yuri ? "YABRCK" : soviet ? "NAHAND" : "GAPILE";
        const std::string factory = yuri ? "YAWEAP" : soviet ? "NAWEAP" : "GAWEAP";
        const std::string radar = yuri ? "NAPSIS" : soviet ? "NARADR"
            : country == "Americans" ? "AMRADR" : "GAAIRC";
        const std::string lab = yuri ? "YATECH" : soviet ? "NATECH" : "GATECH";
        const std::string basic = yuri ? "INIT" : soviet ? "E2" : "E1";
        const std::string tank = yuri ? "LTNK" : soviet ? "HTNK" : "MTNK";
        const std::string advanced = yuri ? "MIND" : soviet ? "APOC" : "SREF";
        const std::string shipyard = yuri ? "YAYARD" : soviet ? "NAYARD" : "GAYARD";
        const std::string superweapon = yuri ? "YAPPET" : soviet ? "NAMISL" : "GAWEAT";
        const auto expect = [&](const std::string& id, bool wanted) {
            const auto* item = findRuntimeGameplayCatalogItem(state, state.economy.owner, id);
            if (!item) return !wanted || fail("catalog missing " + id);
            const auto decision = evaluateRuntimeTechTreeDecision(state, *item);
            return decision.canQueue == wanted || fail(id + (wanted ? " remains locked: "
                : " unlocked early: ") + decision.reason);
        };
        const auto build = [&](const std::string& id) {
            const auto* found = findRuntimeGameplayCatalogItem(state, state.economy.owner, id);
            if (!found) return fail("cannot build missing " + id);
            const RuntimeBuildCatalogItem item = *found;
            if (!queueRuntimeBuildOrder(state, item)) return fail(id + ": " + state.economy.status);
            ++state.tick;
            advanceRuntimeProductionEconomy(state);
            if (!completeRuntimeActiveProductionForProbe(state, item))
                return fail(id + " did not begin production");
            ++state.tick;
            advanceRuntimeProductionEconomy(state);
            const auto ready = runtimeOldestReadyPlacement(state);
            if (!ready || !ready->item || ready->item->typeId != id
                || runtimeSelectedPlacement(state).has_value()
                || !beginRuntimePlacementFromUi(state, *ready, true))
                return fail(id + " did not wait for a sidebar click");
            const auto cell = findRuntimePlacementCellNearBase(state, item);
            if (!cell || !placeRuntimeReadyBuilding(state, *cell))
                return fail(id + " placement failed: " + state.economy.status);
            for (int tick = 0; tick < 240; ++tick) {
                ++state.tick;
                advanceRuntimeBuildAnimations(state);
            }
            return true;
        };
        {
            if (!expect(power, false)) return false;
            const auto* base = findObjectById(state, state.economy.skirmishStartObjectId);
            if (!base || base->kind != WorldObjectKind::Unit)
                return fail("match started with a deployed yard");
            const int baseId = base->instanceId;
            state.tick = 10;
            advanceRuntimeSkirmishOutcome(state);
            if (state.skirmishMatchFinished || !deployRuntimeMcv(state, baseId, false))
                return fail("mobile opening was defeated or could not deploy");
            ensureRuntimeComputerAiStates(state);
            auto& enemy = state.computerAiByOwner.at("skirmishplayer2");
            if (!deployRuntimeComputerAiMcvs(state, enemy)
                || !runtimeOwnerHasConstructionYard(state, enemy.owner))
                return fail("computer could not deploy its opening MCV");
        }
        if (naval) {
            for (const auto& object : state.world.getObjects())
                if (object.alive && object.type == tank)
                    return fail("mode-forbidden tank appeared among starting units");
        }
        if (!expect(power, true) || !expect(factory, false) || !expect(lab, false)
            || !expect(basic, false) || !build(power)) return false;
        if (!expect(refinery, !wealth) || !expect(barracks, true)) return false;
        if (!wealth && !build(refinery)) return false;
        if (!build(barracks) || !expect(basic, true) || !expect(factory, true)
            || !expect(tank, false) || !build(factory) || !expect(tank, !naval)
            || !expect(advanced, false) || !build(radar) || !expect(lab, !meat)) return false;
        if (!meat && !build(lab)) return false;
        const auto* yardRules = findRuntimeBuildingTypeById(state.rules, shipyard);
        const bool hasShipyard = !meat && yardRules && yardRules->techLevel >= 0
            && yardRules->techLevel <= 10;
        if (!expect(advanced, !meat && !naval) || !expect(shipyard, hasShipyard)
            || !expect(superweapon, !meat && !naval)) return false;

        // Every explicit mode ban must survive a complete base for both the
        // sidebar and computer players, including units not in preferred lists.
        RuntimeComputerAiState ai;
        ai.owner = state.economy.owner;
        ai.country = country;
        for (const auto& section : state.skirmishModeRules.getSectionNames()) {
            const auto* values = runtimeIniSectionNoCase(state.skirmishModeRules, section);
            const auto raw = runtimeIniSectionOptionalValueNoCase(values, "TechLevel");
            int level = 0;
            if (!raw || !parseRuntimeInt(*raw, level) || (level >= 0 && level <= 10)) continue;
            const auto* unitRules = findRuntimeUnitTypeById(state.rules, section);
            const auto* buildingRules = findRuntimeBuildingTypeById(state.rules, section);
            if ((unitRules && unitRules->techLevel != level)
                || (buildingRules && buildingRules->techLevel != level))
                return fail("core rules did not load the mode technology for " + section);
            if (!expect(section, false)) return false;
            const auto item = runtimeComputerAiBuildItem(state, section, ai.owner);
            if (item && runtimeComputerAiItemAllowed(state, ai, *item))
                return fail("AI bypassed the mode ban for " + section);
        }
        const auto aiFactory = runtimeComputerAiBuildItem(state, factory, ai.owner);
        if (!aiFactory || !runtimeComputerAiItemAllowed(state, ai, *aiFactory))
            return fail("AI did not follow the available factory technology");

        if (!meat && !naval) {
            for (auto& object : state.world.accessObjects()) {
                if (!object.alive || object.owner != state.economy.owner || object.type != lab) continue;
                const std::string owner = object.owner;
                object.owner = runtimeSkirmishOwnerForSlot(1);
                const bool relocked = expect(advanced, false) && expect(superweapon, false);
                object.owner = owner;
                if (!relocked || !expect(advanced, true)) return false;
                break;
            }
        }
        // Construction probes advance their animation directly. Complete the
        // normal vision boundary before saving the newly placed buildings.
        updateRuntimeVisionState(state);
        std::ostringstream saved;
        std::string restoreError;
        if (!writeRuntimeStateToStream(state, saved, restoreError)) return fail(restoreError);
        IniFile saveIni;
        if (!saveIni.loadFromString(saved.str())) return fail("invalid save INI");
        auto restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored, state, state.visionEnabled);
        if (!materializeRuntimeLoadStateFromIni(*restored, saveIni, restoreError)) return fail(restoreError);
        for (std::size_t index = 0; index < 2; ++index) {
            const std::string owner = runtimeSkirmishOwnerForSlot(index);
            if (state.vision.exploredCellIndices(owner)
                != restored->vision.exploredCellIndices(owner))
                return fail("saved opening exploration changed for " + owner);
        }
        if (mode == "Duel") {
            const std::string owner = state.economy.owner;
            restored->vision.resetShroud(owner);
            ++restored->tick;
            updateRuntimeVisionState(*restored);
            const auto* stats = restored->vision.statsFor(owner);
            if (!stats || stats->exploredCells == stats->validCells)
                return fail("Land Rush repeated its opening reveal after shroud reset");
        }
        const auto* restoredAdvanced = findRuntimeGameplayCatalogItem(*restored,
            restored->economy.owner, advanced);
        if (restored->config.gameMode != mode
            || (restoredAdvanced && evaluateRuntimeTechTreeDecision(*restored, *restoredAdvanced).canQueue)
                != (!meat && !naval)) return fail("saved mode/technology changed after load");
        if (naval && !runRuntimeModeNavalProductionSmoke(state, restoreError)) return fail(restoreError);
        if (wealth && !runRuntimeModeOilEconomySmoke(state, restoreError)) return fail(restoreError);

        // A battle must remain live at the opening and finish after the enemy
        // is eliminated. Both use the real mode/team outcome path.
        state.tick = (state.tick / 10 + 1) * 10;
        advanceRuntimeSkirmishOutcome(state);
        if (state.skirmishMatchFinished) return fail("match ended with both bases alive");
        for (auto& object : state.world.accessObjects()) {
            if (object.owner == runtimeSkirmishOwnerForSlot(1)) {
                object.alive = false;
                object.health = 0;
            }
        }
        state.tick += 10;
        if (!advanceRuntimeSkirmishOutcome(state) || !state.skirmishVictory)
            return fail("eliminating the enemy did not win the match");
        ++passed;
        std::cout << "Mode technology passed: " << mode << '/' << country << '\n';
        return true;
    };
    for (const char* mode : {"MeatGrind", "Megawealth", "NavalWar", "Duel",
            "Battle", "FreeForAll", "TeamGame"}) {
        for (const char* country : {"Americans", "Russians", "YuriCountry"})
            runCase(mode, country);
    }
    for (const char* country : {"Alliance", "French", "Germans", "British",
            "Africans", "Arabs", "Confederation"}) {
        runCase("MeatGrind", country);
        runCase("Battle", country);
    }
    std::cout << "Mode technology cases passed=" << passed << " failed=" << failures.size() << '\n';
    if (!failures.empty()) { error = failures.front(); return false; }
    return true;
}
