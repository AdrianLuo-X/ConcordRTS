// Capture actual stock refinery artwork and smoke together for visual review.
bool runRuntimeRefineryPresentationSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const char* output = std::getenv("CONCORD_REFINERY_PRESENTATION_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    for (const char* type : {"GAREFN", "NAREFN", "YAREFN"}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false);
        s.visionEnabled = false;
        s.config.computerAiEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.world.accessObjects().clear();
        s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear();
        for (int y = 40; y <= 88; ++y) for (int x = 40; x <= 88; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = 0;
            cell.level = 2;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s);
        auto* refinery = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, type, {64,64});
        if (!refinery) { error = "refinery fixture missing"; return false; }
        const int refineryId = refinery->instanceId;
        refinery->buildAnimTicksRemaining = refinery->buildAnimTicksTotal = 0;
        refinery->level = 2;
        refinery->mission = "Guard";
        rebuildRuntimeEconomyPower(s);
        recordRuntimeResourceDeliveryToStorage(s, 20, 0, 500);
        applySelection(s, {refineryId}, false, false);
        const auto& ui = materializeRuntimeUiStateSnapshot(s);
        if (!ui.selectedPipScale.empty() || ui.selectedPipTotal != 0
            || runtimeCargoPipDisplayForObject(s, *refinery).available
            || s.economy.storedResourceValue != 500) {
            error = std::string(type) + " unwanted refinery pips or lost ore value"; return false;
        }
        spawnRuntimeRefinerySmokeEffects(s, *refinery, *refinery);
        const auto* rules = s.rules.findBuildingType(type);
        if (!rules || s.particles.systems().size() != rules->refinerySmoke.offsets.size()) {
            error = std::string(type) + " chimney emitters missing"; return false;
        }
        for (int tick = 1; tick <= 24; ++tick) {
            ++s.tick;
            (void)s.particles.tick(s.rules);
            if (tick != 1 && tick != 24) continue;
            const auto stats = drawRuntimeStateOffscreenAtCell(s, {64,64}, 800, 600, error,
                root.empty() ? "" : root + "/" + type + "-" + std::to_string(tick) + ".bmp",
                false, true);
            if (!stats || !stats->rendered || stats->missingStructureSprites
                || stats->particleActorShpDrawn <= 0 || stats->particleActorFallbackDrawn
                || s.lastSelectedCargoPipSegments) {
                error = std::string(type) + " refinery/smoke artwork or pip rendering failed: " + error;
                return false;
            }
        }
        std::cout << "Refinery presentation: " << type << " ore=$500 pips=0 chimneys="
                  << s.economy.lastRefinerySmokeEffects << " native smoke drawn\n";
    }
    return true;
}
