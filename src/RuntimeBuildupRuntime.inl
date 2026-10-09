RuntimeBuildupTiming runtimeStructureBuildupTiming(
    RuntimeWindowState& state, const WorldObjectInstance& object,
    std::string* resolvedSource = nullptr
) {
    int frames = 0;
    const ArtType* art = runtimeArtTypeForObject(state, object);
    const BuildingType* building = findRuntimeBuildingType(state.rules, object);
    if (art != nullptr && !trimAsciiLocal(art->buildup).empty()) {
        RuntimeMapRenderContext context;
        context.theater = state.theaterLoaded ? &state.theater : nullptr;
        context.art = state.artLoaded ? &state.art : nullptr;
        context.rules = state.rulesLoaded ? &state.rules : nullptr;
        context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
        context.assetRootPath = runtimeAssetRootPath(state.config);
        context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
        context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
        context.palettePath = runtimePalettePath(state.config, state.report.map);
        RuntimeShpFrameImage first;
        std::string resolvedId, source, error;
        int rawFrames = 0;
        if (loadRuntimeAuxiliaryShpFrameImage(context, state.renderCache, art->buildup,
                0, first, resolvedId, source, error, false, &rawFrames, art)) {
            frames = runtimeBuildupFrameCount(rawFrames,
                building != nullptr && building->gate, art->gateStages);
            if (resolvedSource != nullptr) *resolvedSource = source;
        }
    }
    return runtimeBuildupTiming(state.rules.getProductionRules().buildupTime, frames,
        state.config.fixedTickHz);
}

void beginRuntimeStructureSaleAnimation(RuntimeWindowState& state, WorldObjectInstance& object) {
    const BuildingType* building = findRuntimeBuildingType(state.rules, object);
    const auto timing = runtimeStructureBuildupTiming(state, object);
    int total = timing.fixedTicks;
    int remaining = total;
    if (object.buildAnimTicksTotal > 0 && object.buildAnimTicksRemaining > 0) {
        // Cancel unfinished construction from the stage already on screen.
        // Keep its original cadence; do not flash the fully built last frame.
        total = object.buildAnimTicksTotal;
        const int stage = runtimeBuildupFrameAtProgress(timing.frames, total,
            object.buildAnimTicksRemaining, state.config.fixedTickHz, false);
        remaining = timing.frames > 1
            ? static_cast<int>((static_cast<std::int64_t>(stage) + 1) * total / timing.frames) : 0;
    }
    if (timing.frames <= 1 || (building != nullptr && building->wall)) total = remaining = 0;
    object.buildAnimTicksTotal = remaining > 0 ? total : 0;
    object.buildAnimTicksRemaining = remaining;
    object.rubble = false;
}
