bool beginRuntimeBuildingCollapse(
    RuntimeWindowState& state, const WorldObjectInstance& object, const BuildingType& building
) {
    if (runtimeBuildingCollapseForObject(&state.buildingCollapses, object.instanceId,
            state.tick, state.config.fixedTickHz)) return true;
    if (!object.alive || object.kind != WorldObjectKind::Structure) return false;
    auto animations = splitRuntimeTokenList(building.destroyAnim);
    animations.erase(std::remove_if(animations.begin(), animations.end(), [&](const auto& id) {
        return state.art.findArtType(id) == nullptr;
    }), animations.end());
    if (animations.empty()) return false;
    std::uint64_t seed = runtimeStableHashString("buildingDestroyAnim");
    runtimeStableHashMix(seed, object.instanceId); runtimeStableHashMix(seed, state.tick);
    const auto& anim = animations[seed % animations.size()];
    const auto* art = state.art.findArtType(anim);
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = &state.art; context.rules = &state.rules;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    RuntimeShpFrameImage image;
    std::string resolved, source, error;
    int frames = 0;
    if (!loadRuntimeAuxiliaryShpFrameImage(context, state.renderCache, anim, 0,
            image, resolved, source, error, art->shadow, &frames) || frames <= 0) return false;
    state.buildingCollapses.push_back({object.instanceId, anim, state.tick, frames,
        art->rate > 0 ? art->rate : 150});
    if (!art->reportSound.empty()) emitRuntimeSound(state, "sound:" + art->reportSound, runtimeSpatialAudioPositionForObject(object), true);
    return true;
}

bool advanceRuntimeBuildingCollapses(RuntimeWindowState& state) {
    const bool changed = !state.buildingCollapses.empty();
    std::erase_if(state.buildingCollapses, [&](const auto& collapse) {
        return runtimeBuildingCollapseFrame(collapse, state.tick, state.config.fixedTickHz) >= collapse.frameCount;
    });
    return changed;
}
