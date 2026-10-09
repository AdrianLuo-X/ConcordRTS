// Runs only the contact-shadow renderer contract with original stock assets.
// The renderer contract owns its clean geometry, raw-pixel oracle and output.
bool runRuntimeInfantryContactShadowSmoke(RuntimeWindowState& live, std::string& error) {
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    seedRuntimeLoadScratch(state, live, false);
    RuntimeSnapshotActiveStateScope active(state);
    state.config.fixedTickHz = 30;
    state.config.computerAiEnabled = state.config.multiplayerActive = false;
    state.config.noCooldown = state.config.crates = state.config.superweapons = false;
    state.config.playerSlots.clear();
    state.config.gameModeRulesOverride.clear();
    state.runtimePreferencePersistenceEnabled = state.runtimeSoundsEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.visionEnabled = false;
    const auto original = std::filesystem::path(state.config.localDataRootPath)
        / "resources/mix_exports/ra2md/localmd";
    IniFile rulesIni, artIni;
    if (!rulesIni.loadFromFile((original / "rulesmd.ini").string())
        || !artIni.loadFromFile((original / "artmd.ini").string())
        || !state.rules.loadFromIni(rulesIni) || !state.art.loadFromIni(artIni)) {
        error = "infantry contact shadow: original rules/art unavailable";
        return false;
    }
    state.rulesLoaded = state.artLoaded = true;
    state.world.accessObjects().clear();
    state.world.accessTerrainObjects().clear();
    state.world.accessSmudges().clear();
    state.selectedObjectIds.clear();
    auto context = runtimeTerrainMetadataContext(state);
    context.rules = &state.rules;
    context.art = &state.art;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.ownerRemapColors = &state.ownerRemapColors;
    context.neutralRemapOwners = &state.neutralRemapOwners;
    context.animationTick = 0;
    context.fixedTickHz = 30;
    context.shadowsEnabled = true;
    context.vision = nullptr;
    context.mapFacts = nullptr;
    return runRuntimeInfantryContactShadowContractCheck(state.world, context, error);
}
