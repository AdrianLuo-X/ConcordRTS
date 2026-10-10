#include "simulation/scenarios/diagnostics/RuntimeUnholyTechTreeSmoke.inl"

bool runRuntimeUnholyStartSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& reason) { error = reason; return false; };
    int cases = 0;
    for (const char* country : {"Americans", "Russians", "YuriCountry"}) {
        for (bool aiEnabled : {false, true}) for (bool shortGame : {false, true}) {
            auto storage = std::make_unique<RuntimeWindowState>();
            auto& state = *storage;
            state.config = config;
            state.config.gameMode = "UnholyAlliance";
            state.config.gameModeRulesOverride = "MPUnholyMD.ini";
            state.config.multiplayerActive = false;
            // This production probe requires an active order before completion.
            state.config.noCooldown = false;
            state.config.computerAiEnabled = aiEnabled;
            state.config.shortGame = shortGame;
            state.config.startingUnits = 0;
            state.config.startingCredits = 100000;
            state.config.techLevel = 10;
            state.config.superweapons = true;
            state.config.crates = false;
            state.config.playerSlots.clear();
            state.config.playerSlots.resize(2);
            for (std::size_t index = 0; index < 2; ++index) {
                auto& slot = state.config.playerSlots[index];
                slot.enabled = true;
                slot.human = index == 0;
                slot.local = index == 0;
                slot.country = country; // Same country must still be an opponent.
                slot.team = "-";
                slot.startPosition = static_cast<int>(index) + 1;
            }
            state.runtimeSoundsEnabled = false;
            state.runtimeSuppressPlayerAudioFeedback = true;
            state.runtimePreferencePersistenceEnabled = false;
            state.report = buildRuntimeStartupReport(state.config, &state.world,
                &state.theater, &state.art, &state.rules, &state.mapFacts);
            state.rulesLoaded = state.report.rulesLoaded;
            state.artLoaded = state.report.artLoaded;
            state.theaterLoaded = state.report.theaterLoaded;
            if (!state.report.success) return fail("original map/rules could not load");
            initializeRuntimeNavigation(state);
            initializeRuntimeEconomy(state);
            if (!initializeRuntimeSkirmishStart(state))
                return fail(state.economy.skirmishStatus);
            state.triggers.bootstrapComplete = true;
            const std::string localOwner = runtimeSkirmishOwnerForSlot(0);
            const std::string enemyOwner = runtimeSkirmishOwnerForSlot(1);
            const auto baseAssets = [&](const std::string& owner) {
                int count = 0;
                for (const auto& object : state.world.getObjects())
                    if (object.owner == owner && runtimeSkirmishObjectSurvives(state, object)
                        && runtimeSkirmishObjectCountsForShortGame(state, object)) ++count;
                return count;
            };
            if (baseAssets(localOwner) != 3 || baseAssets(enemyOwner) != 3
                || runtimeOwnersAreAllied(state, localOwner, enemyOwner)
                || runtimeOwnerHasConstructionYard(state,localOwner)
                || runtimeOwnerHasConstructionYard(state,enemyOwner))
                return fail("each opposing player must start with three undeployed MCVs");
            const char* nativePower = std::string(country) == "Americans" ? "GAPOWR"
                : std::string(country) == "Russians" ? "NAPOWR" : "YAPOWR";
            const auto aiPower = runtimeComputerAiBuildItem(state, nativePower, enemyOwner);
            RuntimeComputerAiState aiProbe;
            aiProbe.owner = enemyOwner;
            aiProbe.country = country;
            if (!aiPower.has_value() || runtimeComputerAiItemAllowed(state, aiProbe, *aiPower))
                return fail(std::string(country) + " power unlocked before MCV deployment");
            // The original ROT=5 MCV turns through 19 retail logic frames.
            // Allow those frames plus the AI opening cadence, rather than
            // treating thirty fixed updates as an instant deployment.
            const int openingTicks = std::max(30,
                runtimeRuleFramesToTicks(32, state.config.fixedTickHz));
            for (state.tick = 1; state.tick <= openingTicks; ++state.tick) {
                // Match the real simulation order: an AI deployment request
                // may first turn the mobile base before creating its yard.
                advanceRuntimeMcvDeployTurns(state);
                advanceRuntimeComputerAi(state);
                advanceRuntimeSkirmishOutcome(state);
                if (state.skirmishMatchFinished || baseAssets(localOwner) < 3
                    || baseAssets(enemyOwner) < 3)
                    return fail(std::string(country) + " lost a base or ended during opening ticks");
            }
            if (runtimeComputerAiItemAllowed(state,aiProbe,*aiPower)!=aiEnabled)
                return fail(std::string(country)+" AI opening deployment/technology mismatch");
            if (!aiEnabled && !shortGame && !runRuntimeUnholyTechTreeSmoke(state, error))
                return false;
            for (auto& object : state.world.accessObjects()) if (object.owner == enemyOwner) {
                object.alive = false;
                object.health = 0;
            }
            // Outcome checks run every ten ticks; technology construction
            // advances time, so use the next scheduled check after it.
            state.tick = (state.tick / 10 + 1) * 10;
            if (!advanceRuntimeSkirmishOutcome(state) || !state.skirmishVictory)
                return fail("eliminating the opponent did not produce a real victory");
            ++cases;
        }
    }
    std::cout << "Unholy opening: " << cases << " cases; Allied/Soviet/Yuri, same-country enemies, "
        "AI on/off, Short Game on/off, three bases per owner through deployment; elimination victory passed\n";
    return true;
}
