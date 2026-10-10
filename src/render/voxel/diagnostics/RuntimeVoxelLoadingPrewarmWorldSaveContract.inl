// Uses the actual stock 48-unit fixture and the complete authoritative writer.
bool runRuntimeVoxelLoadingPrewarmWorldSaveContract(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "voxel loading prewarm world contract: " + why;
        return false;
    };
    auto state = std::make_unique<RuntimeWindowState>();
    state->config = config;
    state->report = buildRuntimeStartupReport(config, &state->world, &state->theater,
        &state->art, &state->rules, &state->mapFacts);
    if (!state->report.success || !state->report.artLoaded || !state->report.rulesLoaded)
        return fail("stock startup unavailable");
    state->theaterLoaded = state->report.theaterLoaded;
    state->artLoaded = state->report.artLoaded;
    state->rulesLoaded = state->report.rulesLoaded;
    RuntimeSnapshotActiveStateScope active(*state);
    std::vector<int> ids;
    if (!prepareRuntimeMassMovementWorld(*state, 48, ids, error, false, true)) return false;
    if (ids.size() != 48) return fail("stock mixed fixture count");
    state->config.defaultMapPath = "VoxelLoadingPrewarmWorldContract.map";
    const auto frame = drawRuntimeStateOffscreenAtCell(*state, RuntimeCellPoint{24, 24},
        1024, 640, error, "", false);
    if (!frame || !frame->rendered) return fail("ordinary first-frame resource loading: " + error);

    RuntimeMapRenderContext context;
    context.theater = state->theaterLoaded ? &state->theater : nullptr;
    context.art = state->artLoaded ? &state->art : nullptr;
    context.rules = state->rulesLoaded ? &state->rules : nullptr;
    context.mapFacts = state->mapFactsLoaded ? &state->mapFacts : nullptr;
    context.ownerRemapColors = state->ownerRemapColors.empty() ? nullptr : &state->ownerRemapColors;
    context.neutralRemapOwners = state->neutralRemapOwners.empty() ? nullptr : &state->neutralRemapOwners;
    context.tileRootPath = runtimeTileRootPath(state->config, state->report.map);
    context.palettePath = runtimePalettePath(state->config, state->report.map);
    context.assetRootPath = runtimeAssetRootPath(state->config);
    context.structureRootPath = runtimeStructureRootPath(state->config, state->report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state->config, state->report.map);
    context.animationTick = state->tick;
    context.fixedTickHz = state->config.fixedTickHz;
    context.stableRenderConfiguration = state->stableRuntimeResourceTracking;

    RuntimePlannerPassabilityContractDetail::Authority before, after;
    if (!RuntimePlannerPassabilityContractDetail::capture(*state, before, error)) return false;
    // Renderer contract runs on this actual completed stock state. Capture the
    // complete writer again immediately after the pair, before prewarm mutates
    // only its derived cache. Do not adjust any authority clock or map identity.
    auto repairCamera = state->camera;
    if (!runRuntimeGroundVehicleRepairRecordsWorldContractCheck(state->world, context, repairCamera, error)) return false;
    if (!RuntimePlannerPassabilityContractDetail::capture(*state, after, error)) return false;
    if (before != after) return fail("repair records renderer changed complete world/navigation/light/superweapon clocks or saved bytes");
    std::cout << "Repair records world: actualMixed48 fullWorldAndSave=yes completedTick=" << state->tick << " pass\n";
    const auto initialTick = state->tick;
    const auto initialMap = state->report.map.path;
    RuntimeVoxelLoadingPrewarmStats stats;
    prewarmRuntimeCurrentWorldNormalVoxels(state->world, context, state->renderCache,
        RuntimeVoxelLoadingPrewarmLimits{}, stats);
    if (!RuntimePlannerPassabilityContractDetail::capture(*state, after, error)) return false;
    if (before != after || initialTick != state->tick || initialMap != state->report.map.path)
        return fail("complete world, navigation authority, tick, map or saved bytes changed");
    if (!stats.newEntries || stats.referenceSkipped || stats.paletteNotReadySkipped
        || stats.cacheDisabledSkipped || stats.nestedSkipped
        || stats.newEntries > 2048 || stats.attempts > 4096
        || stats.addedPayloadBytes > 64U * 1024U * 1024U)
        return fail("actual normal voxel admission or retained-payload bound missing");
    std::cout << "Voxel loading prewarm world: actualMixed48 fullWorldAndSave=yes newEntries="
        << stats.newEntries << " payloadBytes=" << stats.addedPayloadBytes << " pass\n";
    error.clear();
    return true;
}
