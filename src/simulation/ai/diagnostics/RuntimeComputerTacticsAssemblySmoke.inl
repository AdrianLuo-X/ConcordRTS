bool runRuntimeComputerTacticsAssemblySmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& message) { error = "computer tactic assembly: " + message; return false; };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config; stock->config.gameMode = "Battle"; stock->config.gameModeRulesOverride.clear();
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original rules unavailable");
    stock->rulesLoaded = stock->artLoaded = true;
    if (!initializeRuntimeTriggers(*stock)) return fail("original AI unavailable");
    const std::string human = runtimeSkirmishOwnerForSlot(0), owner = runtimeSkirmishOwnerForSlot(1);
    const std::string ownerKey = toLowerAsciiLocal(owner);
    const auto makeWorld = [&](const std::string& country, const std::string& difficulty,
        const std::vector<std::string>& triggers) {
        auto probe = std::make_unique<RuntimeWindowState>(); auto& s = *probe;
        s.config = stock->config; s.config.fixedTickHz = 30;
        s.config.computerAiEnabled = true; s.config.shortGame = false; s.config.startingCredits = 40000;
        s.config.playerSlots.resize(2);
        for (int i = 0; i < 2; ++i) {
            auto& slot = s.config.playerSlots[i]; slot.enabled = true; slot.human = slot.local = i == 0;
            slot.country = i == 0 ? "Americans" : country; slot.difficulty = difficulty;
        }
        s.rules = stock->rules; s.art = stock->art; s.rulesLoaded = s.artLoaded = true;
        s.visionEnabled = true; s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        s.economy.owner = human; s.economy.credits = 40000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true;
        s.skirmishCountryByOwner[toLowerAsciiLocal(human)] = "Americans";
        s.skirmishCountryByOwner[ownerKey] = country;
        s.skirmishDifficultyByOwner[ownerKey] = difficulty;
        RuntimeHumanHouseAuthority authority; authority.owner = human;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(human); authority.localFacade = true;
        s.humanHouseAuthorities.push_back(authority);
        for (int y = 1; y <= 40; ++y) for (int x = 1; x <= 50; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        s.triggers = stock->triggers; s.triggers.bootstrapComplete = true;
        std::erase_if(s.triggers.computerAiTriggers, [&](const auto& trigger) {
            return std::find(triggers.begin(), triggers.end(), trigger.id) == triggers.end();
        });
        const bool soviet = country == "Russians", yuri = country == "YuriCountry";
        const auto add = [&](const std::string& house, const std::string& type, RuntimeCellPoint cell) {
            return addRuntimeObjectInstanceForOwner(s, house, RuntimeBuildKind::Building, type, cell);
        };
        add(owner, yuri ? "YACNST" : soviet ? "NACNST" : "GACNST", {2, 2});
        add(owner, yuri ? "YAPOWR" : soviet ? "NAPOWR" : "GAPOWR", {2, 7});
        add(owner, yuri ? "YAPOWR" : soviet ? "NAPOWR" : "GAPOWR", {12, 7});
        add(owner, yuri ? "YABRCK" : soviet ? "NAHAND" : "GAPILE", {2, 12});
        add(owner, yuri ? "YAWEAP" : soviet ? "NAWEAP" : "GAWEAP", {8, 12});
        add(owner, yuri ? "NAPSIS" : soviet ? "NARADR" : "GAAIRC", {8, 2});
        add(owner, yuri ? "YATECH" : soviet ? "NATECH" : "GATECH", {2, 19});
        if (yuri) {
            const int miners = s.rules.getComputerMiningRules().slaveMinerNumber[
                runtimeComputerMiningDifficultyIndex(difficulty)];
            for (int i = 0; i < miners; ++i)
                addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Unit, "SMIN", {18 + 3 * i, 8});
        }
        add(human, "GACNST", {41, 30}); add(human, "GAAIRC", {33, 30});
        add(human, "GAPILE", {34, 20}); add(human, "GAPOWR", {40, 18});
        add(human, "GAREFN", {43, 23});
        auto* infantry = addRuntimeObjectInstanceForOwner(s, human, RuntimeBuildKind::Infantry, "E1", {25, 15});
        if (infantry) infantry->mission = "Sleep";
        ensureRuntimeComputerAiStates(s);
        auto& ai = s.computerAiByOwner.at(ownerKey);
        ai.nextThinkTick = 0; ai.nextBuildTick = 10000000; ai.nextRepairTick = 10000000;
        initializeRuntimeNavigation(s); updateRuntimeVisionState(s);
        return probe;
    };
    const auto describe = [&](const RuntimeWindowState& s) {
        std::ostringstream out; out << " tick=" << s.tick << " status=" << s.computerAiByOwner.at(ownerKey).status;
        const auto found = s.triggers.computerTacticsByOwner.find(ownerKey);
        if (found != s.triggers.computerTacticsByOwner.end()) {
            for (const auto& [id, team] : found->second.teams)
                out << " team=" << id << " members=" << team.objectIds.size() << " missing=" << team.missingMembers.size()
                    << " launched=" << team.launched;
            for (const auto& order : found->second.production)
                if (!order.typeId.empty()) out << " order=" << order.typeId << ':' << order.remainingTicks;
        }
        return out.str();
    };
    const auto tick = [&](RuntimeWindowState& s) { ++s.tick; advanceRuntimeSimulation(s); };
    if (std::getenv("CONCORD_COMMAND_SMOKE_COMPUTER_TACTICS_BEHAVIOR_ONLY")) {
#include "simulation/ai/diagnostics/RuntimeComputerTacticsBehaviorSmoke.inl"
        return true;
    }
    if (std::getenv("CONCORD_COMMAND_SMOKE_COMPUTER_CAPTURE_FATE_ONLY")) {
#include "simulation/ai/diagnostics/RuntimeComputerCaptureFateSmoke.inl"
        return true;
    }
    if (std::getenv("CONCORD_COMMAND_SMOKE_COMPUTER_TACTICS_SUPER_LOSS_ONLY")) {
#include "simulation/ai/diagnostics/RuntimeComputerTacticsSuperLossSmoke.inl"
        return true;
    }
    if (std::getenv("CONCORD_COMMAND_SMOKE_COMPUTER_TACTICS_NAVAL_ONLY")) {
#include "simulation/ai/diagnostics/RuntimeComputerTacticsNavalSmoke.inl"
        return true;
    }
    if (std::getenv("CONCORD_COMMAND_SMOKE_COMPUTER_TACTICS_LIFECYCLE_ONLY")) {
#include "simulation/ai/diagnostics/RuntimeComputerTacticsLifecycleSmoke.inl"
        return true;
    }
    if (std::getenv("CONCORD_COMMAND_SMOKE_COMPUTER_TACTICS_COOPERATION_ONLY")) {
#include "simulation/ai/diagnostics/RuntimeComputerTacticsCooperationSmoke.inl"
        return true;
    }
    {
        auto probe = makeWorld("Americans", "Easy", {"0D084B5C-G"}); auto& s = *probe;
        const auto item = runtimeComputerAiBuildItem(s, "ORCA", owner);
        if (!item) return fail("Harrier absent");
        std::cout << "Computer assembly: Harrier cost=" << item->cost << " ticks=" << item->buildTicks << '\n';
        tick(s);
        auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
        if (tactics.teams.size() != 1 || tactics.production[2].typeId != "ORCA"
            || s.computerAiByOwner.at(ownerKey).credits != 40000 - item->cost
            || s.computerAiByOwner.at(ownerKey).unitsBuilt != 0)
            return fail("selection did not start one paid delayed Harrier" + describe(s));
        const int fullTicks = tactics.production[2].remainingTicks;
        for (int i = 0; i < std::max(1, fullTicks / 2); ++i) tick(s);
        if (s.computerAiByOwner.at(ownerKey).unitsBuilt != 0) return fail("Harrier appeared before production completed");
        std::ostringstream save;
        if (!writeRuntimeStateToStream(s, save, error)) return false;
        IniFile saved; saved.loadFromString(save.str());
        auto restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored, s, s.visionEnabled);
        if (!materializeRuntimeLoadStateFromIni(*restored, saved, error)) return false;
        std::ostringstream before, after;
        writeRuntimeComputerTactics(before, s); writeRuntimeComputerTactics(after, *restored);
        if (before.str() != after.str() || restored->computerAiByOwner.at(ownerKey).credits != s.computerAiByOwner.at(ownerKey).credits)
            return fail("mid-production save lost paid progress or reservations");
        for (auto* world : {&s, restored.get()}) {
            bool launched = false;
            for (int i = 0; i < 4 * fullTicks + 1200; ++i) {
                tick(*world);
                auto& current = world->triggers.computerTacticsByOwner.at(ownerKey);
                current.nextTriggerTick = 10000000; // One authored team per fixture.
                for (const auto& [id, team] : current.teams) launched |= team.launched;
                if (launched) break;
            }
            if (!launched || world->computerAiByOwner.at(ownerKey).unitsBuilt != 2
                || world->computerAiByOwner.at(ownerKey).credits != 40000 - 2 * item->cost)
                return fail("two paid Harriers did not assemble and launch" + describe(*world));
            std::set<std::pair<int, int>> pads;
            for (const auto& object : world->world.getObjects()) {
                if (!object.alive || object.owner != owner || object.type != "ORCA") continue;
                if (object.dockedToObjectId < 0 || object.dockIndex < 0
                    || !pads.emplace(object.dockedToObjectId, object.dockIndex).second
                    || !runtimeComputerTacticReservesObject(*world, object.instanceId))
                    return fail("produced aircraft lost its individual support pad or team");
            }
            if (pads.size() != 2) return fail("wrong aircraft count at launch");
        }
        std::cout << "Computer assembly: stock Easy Harrier team=yes real production time=yes two paid aircraft=yes unique pads=yes mid-build save/resume=yes\n";
        std::map<int, int> lastAmmo, shots, reloads;
        for (const auto& object : s.world.getObjects())
            if (object.alive && object.owner == owner && object.type == "ORCA") lastAmmo[object.instanceId] = object.currentAmmo;
        bool repeatedSorties = false;
        for (int i = 0; i < 4800 && !repeatedSorties; ++i) {
            tick(s);
            for (auto& [id, ammo] : lastAmmo) {
                const auto* aircraft = findObjectById(s, id);
                if (!aircraft) continue;
                if (ammo > aircraft->currentAmmo) ++shots[id];
                if (ammo < aircraft->currentAmmo) ++reloads[id];
                ammo = aircraft->currentAmmo;
            }
            repeatedSorties = lastAmmo.size() == 2;
            for (const auto& [id, ammo] : lastAmmo) repeatedSorties &= shots[id] >= 2 && reloads[id] >= 1;
        }
        if (!repeatedSorties) {
            std::string detail = describe(s);
            for (const auto& [id, ammo] : lastAmmo) {
                const auto* aircraft = findObjectById(s, id);
                detail += " aircraft=" + std::to_string(id) + " shots=" + std::to_string(shots[id])
                    + " reloads=" + std::to_string(reloads[id]) + " ammo=" + std::to_string(ammo);
                if (aircraft) detail += " mission=" + aircraft->mission + "@" + std::to_string(aircraft->x) + "," + std::to_string(aircraft->y);
            }
            return fail("produced aircraft did not return, reload and attack again" + detail);
        }
        std::cout << "Computer aircraft: both produced Harriers fired, returned to real pads, reloaded and flew a second sortie=yes\n";
    }
    // Country and difficulty are independent of side; Korea's unique team
    // must not leak to a different Allied country or to its disabled difficulty.
    for (const auto& [country, difficulty, expected] : std::vector<std::tuple<std::string, std::string, bool>>{
        {"Americans", "Hard", false}, {"Alliance", "Easy", false}, {"Alliance", "Hard", true}}) {
        auto probe = makeWorld(country, difficulty, {"0C8C645C-G"}); tick(*probe);
        const auto& teams = probe->triggers.computerTacticsByOwner.at(ownerKey).teams;
        if ((!teams.empty()) != expected) return fail("Korean team country/difficulty gate failed");
    }
    {
        auto probe = makeWorld("Americans", "Hard", {"0CA6621C-G", "0CADDEDC-G"}); tick(*probe);
        auto& tactics = probe->triggers.computerTacticsByOwner.at(ownerKey);
        if (tactics.teams.size() != 1 || !tactics.teams.begin()->second.baseDefense)
            return fail("minimum base defense was not selected before a Spy");
    }
    std::cout << "Computer selection: Korea country/difficulty restrictions=yes original base defense priority=yes\n";
    for (const auto& [country, difficulty, triggerId, expectedTeam] :
        std::vector<std::tuple<std::string, std::string, std::string, std::string>>{
            {"Americans", "Hard", "0CADDEDC-G", "0CADF54C-G"},
            {"Russians", "Medium", "0D53430C-G", "0C143EFC-G"},
            {"YuriCountry", "Hard", "08B9586C-G", "08B95EFC-G"}}) {
        auto probe = makeWorld(country, difficulty, {triggerId}); auto& s = *probe;
        const auto* teamType = findRuntimeTriggerTeamById(s, expectedTeam);
        if (!teamType) return fail("specialist team missing");
        int expectedCost = 0, expectedMembers = 0;
        for (const auto& member : s.triggers.taskForces.at(teamType->taskForceId).members) {
            const auto item = runtimeComputerAiBuildItem(s, member.unitId, owner);
            if (!item) return fail("specialist production item missing");
            expectedCost += member.count * item->cost; expectedMembers += member.count;
        }
        bool launched = false, effect = false;
        int yard = -1, power = -1;
        for (const auto& object : s.world.getObjects()) if (object.owner == human) {
            if (object.type == "GACNST") yard = object.instanceId;
            if (object.type == "GAPOWR") power = object.instanceId;
        }
        for (int i = 0; i < 12000 && !effect; ++i) {
            tick(s);
            auto& tactics = s.triggers.computerTacticsByOwner.at(ownerKey);
            if (!tactics.teams.empty()) tactics.nextTriggerTick = 10000000;
            for (const auto& [id, team] : tactics.teams) launched |= team.launched;
            if (country == "Americans") effect = s.spies.hasVeteranProduction(
                runtimeRepairDepotCanonicalOwnerId(s, owner), RuntimeSpyVeteranProduction::Infantry);
            else {
                const auto* captured = findObjectById(s, country == "YuriCountry" ? power : yard);
                effect = captured && captured->owner == owner;
            }
        }
        const auto& ai = s.computerAiByOwner.at(ownerKey);
        if (!launched || !effect || ai.creditsSpent != expectedCost
            || ai.infantryBuilt + ai.unitsBuilt != expectedMembers)
            return fail("paid stock specialist did not complete its mission: " + country + describe(s)
                + " spent=" + std::to_string(ai.creditsSpent) + "/" + std::to_string(expectedCost));
        std::cout << "Computer specialist: " << country << " stock " << expectedTeam
            << " selected=yes paid full team=yes factory production=yes launched=yes original entry/capture effect=yes\n";
    }
    return true;
}
