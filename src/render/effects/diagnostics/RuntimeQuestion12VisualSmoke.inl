bool runRuntimeQuestion12VisualSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& live = *storage;
    live.config = config;
    live.report = buildRuntimeStartupReport(config, &live.world, &live.theater,
        &live.art, &live.rules, &live.mapFacts);
    if (!live.report.success) { error = "Question 12 original startup failed"; return false; }
    live.rulesLoaded = live.artLoaded = live.theaterLoaded = true;
    live.runtimeSoundsEnabled = false; live.runtimeSuppressPlayerAudioFeedback = true;
    if (live.rules.getInfantryDeathAnim(9) != "GENDEATH"
        || live.rules.getInfantryDeathAnim(10) != "BRUTDIE") {
        error = "Brute melee and mutation death mapping reversed"; return false;
    }
    if (!runRuntimeInfantryDeathSmokeProbe(live, error)) return false;
    RuntimeMapRenderContext context;
    context.rules = &live.rules; context.art = &live.art; context.theater = &live.theater;
    context.assetRootPath = runtimeAssetRootPath(config);
    context.structureRootPath = runtimeStructureRootPath(config, live.report.map);
    context.palettePath = runtimePalettePath(config, live.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(config, live.report.map);
    context.fixedTickHz = config.fixedTickHz;
    if (!runRuntimeBruteDeathRenderContractCheck(context, error)
        || !runRuntimeFloatingDiskSmokeProbe(live, error)
        || !runRuntimePrismPresentationSmokeProbe(live, error)) return false;
    std::cout << "Question 12 combat visuals passed: Brute melee death, distinct mutation,"
        << " original animation pixels, translucent floating disk beam and prism regression\n";
    return true;
}
