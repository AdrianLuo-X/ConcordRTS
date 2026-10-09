bool runRuntimeSonicPulseSmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const bool diagnostic = std::getenv("CONCORD_SONIC_PULSE_DIAGNOSTIC") != nullptr;
    const char* output = std::getenv("CONCORD_SONIC_PULSE_OUTPUT");
    const std::string root = output ? output : "build/sonic-pulse";
    std::filesystem::create_directories(root);
    std::string label;
    const auto fail = [&](const std::string& why) { error = "Sonic pulse " + label + ": " + why; return false; };
    if (!diagnostic && !runRuntimeSonicWaveRenderContractCheck(error)) return false;
    IniFile nativeRules, overlay; RulesDatabase fixtureRules;
    if (!nativeRules.loadFromFile(live.report.rulesPath)
        || !overlay.loadFromString("[DRED]\nStrength=100000\n")) return fail("durable target rules");
    nativeRules.appendFrom(overlay);
    if (!fixtureRules.loadFromIni(nativeRules)) return fail("durable target database");
    int cases = 0, saves = 0, paired = 0, captures = 0;
    for (int hz : {15, 30, 60}) for (int rank : {0, 2}) for (bool instant : {false, true}) {
        label = std::to_string(hz) + "-rank" + std::to_string(rank) + (instant ? "-fast" : "-normal");
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false);
        s.rules = fixtureRules; s.rulesLoaded = true;
        s.config.fixedTickHz = hz; s.config.noCooldown = instant;
        s.config.playerSlots.clear(); s.config.playerCountry = "Americans";
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.crates = s.config.shortGame = false;
        s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.visionEnabled = false; s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; initializeRuntimeLocalProductionLanes(s.economy);
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        WorldObjectInstance dolphin; dolphin.instanceId = 1; dolphin.kind = WorldObjectKind::Unit;
        dolphin.type = "DLPH"; dolphin.owner = "Americans"; dolphin.health = 200; dolphin.alive = true;
        dolphin.x = dolphin.y = 75; dolphin.cell = runtimeCellKey(75,75);
        dolphin.facing = dolphin.turretFacing = runtimeFacingFromDelta(1,0);
        dolphin.veterancy = rank; dolphin.mission = "Guard";
        auto target = dolphin; target.instanceId = 2; target.type = "DRED"; target.owner = "Russians";
        // A durable, non-firing target keeps both complete volleys observable.
        target.health = 100000; target.veterancy = 0; target.x = 80; target.cell = runtimeCellKey(80,75);
        target.mission = "Sleep"; target.weaponCooldownTicksRemaining = 100000;
        s.world.accessObjects() = {dolphin,target}; s.selectedObjectIds = {1};
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        RuntimeSnapshotActiveStateScope active(s);
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {80,75}, findObjectById(s,2))) return fail("player attack rejected");
        const auto step = [](RuntimeWindowState& q) { RuntimeSnapshotActiveStateScope scope(q);
            ++q.tick; advanceRuntimeSimulation(q,false); };
        std::map<int,int> born, finished; std::set<int> prior;
        int longest = 0, quiet = 0, maxQuiet = 0, renderedPixels = 0;
        bool middle = false, end = false, gap = false;
        std::unique_ptr<RuntimeWindowState> restored; int restoreUntil = 0;
        const auto capture = [&](const std::string& phase, bool waveExpected) {
            if (hz != 30 || instant) return true;
            const auto stats = drawRuntimeStateOffscreenAtCell(s,{77,75},960,600,error,
                root + "/" + label + "-" + phase + ".bmp",false);
            if (!stats || !stats->rendered) return false;
            ++captures; renderedPixels += stats->sonicWavePixels;
            return diagnostic || (waveExpected ? stats->sonicWavesDrawn > 0 && stats->sonicWavePixels > 0
                : stats->sonicWavesDrawn == 0);
        };
        for (int n = 0; n < hz * 20; ++n) {
            step(s);
            if (restored && s.tick <= restoreUntil) {
                step(*restored); ++paired;
                if (!findObjectById(s,2) || !findObjectById(*restored,2)) return fail("durable target vanished");
                if (findObjectById(s,2)->health != findObjectById(*restored,2)->health)
                    return fail("restored pulse damage differs");
                std::vector<std::string> a,b;
                for (const auto& p:s.projectiles) if(p.renderIntent=="sonicWave") a.push_back(serializeRuntimeProjectileEffect(p));
                for (const auto& p:restored->projectiles) if(p.renderIntent=="sonicWave") b.push_back(serializeRuntimeProjectileEffect(p));
                if (a != b) return fail("restored pulse phase differs");
            }
            std::set<int> now;
            for (const auto& p : s.projectiles) if (p.renderIntent == "sonicWave" && p.attackerObjectId == 1) {
                now.insert(p.actorId); born.try_emplace(p.actorId,p.spawnTick);
                if (!middle && p.progress >= .45) {
                    if (!capture("middle",true)) return fail("wave distortion missing");
                    middle = true;
                    std::ostringstream text; IniFile ini; restored = std::make_unique<RuntimeWindowState>();
                    seedRuntimeLoadScratch(*restored,s,false);
                    if (!writeRuntimeStateToStream(s,text,error) || !ini.loadFromString(text.str())
                        || !materializeRuntimeLoadStateFromIni(*restored,ini,error)) return fail("pulse save: " + error);
                    restoreUntil = s.tick + hz * 2; ++saves;
                }
                if (!end && p.progress >= .90) {
                    if (!capture("end",true)) return fail("advancing wave rendering"); end = true;
                }
            }
            for (int id : prior) if (!now.contains(id)) {
                finished[id] = s.tick; longest = std::max(longest,s.tick-born.at(id));
            }
            if (!born.empty() && now.empty()) {
                ++quiet; maxQuiet = std::max(maxQuiet,quiet);
                if (!gap) { if (!capture("gap",false)) return fail("wave remained during reload"); gap = true; }
            } else quiet = 0;
            prior = now;
            const int expected = rank == 0 ? 2 : 4;
            if (static_cast<int>(finished.size()) >= expected && now.empty() && s.tick > restoreUntil) break;
        }
        if (!findObjectById(s,2)) return fail("durable target vanished before second volley");
        std::cout << "Sonic " << label << " waves=" << born.size() << " finished=" << finished.size()
            << " maxWaveFrames=" << longest * 15.0 / hz << " gapFrames=" << maxQuiet * 15.0 / hz
            << " damage=" << 100000-findObjectById(s,2)->health << " renderedPixels=" << renderedPixels << '\n';
        if (!middle || !end || !gap || finished.size() < (rank == 0 ? 2U : 4U)
            || findObjectById(s,2)->health >= 100000) return fail("complete separate volleys missing");
        if (!diagnostic && (longest > runtimeRuleFramesToTicks(24,hz) || maxQuiet < hz))
            return fail("wave lingered instead of a short pulse");
        ++cases;
    }
    std::cout << "Sonic pulse " << (diagnostic ? "diagnostic" : "passed") << ": cases=" << cases
        << " saves=" << saves << " continuedTicks=" << paired << " captures=" << captures << '\n';
    return true;
}
