bool runRuntimeOreDrillPaletteRenderContractCheck(
    const RuntimeMapRenderContext& context, const std::string& root, std::string& error);

bool runRuntimeOreDrillPaletteSmoke(RuntimeWindowState& state, std::string& error) {
    const char* output = std::getenv("CONCORD_ORE_DRILL_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    RuntimeMapRenderContext context;
    context.rules = &state.rules; context.art = &state.art; context.theater = &state.theater;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    return runRuntimeOreDrillPaletteRenderContractCheck(context, root, error);
}
