// Retail TechnoClass::DrawExtras (0x6F52D8..0x6F5340): WRENCH.SHP,
// MOUSEPAL.PAL, and a speed-normalized sweep through its seven frames.
int runtimeBuildingRepairIconFrame(const RuntimeWindowState& state) {
    return concord::render::runtimeBuildingRepairIconFrame(state.tick, state.config.fixedTickHz, state.config.gameSpeed);
}

int runtimeRepairCursorPhase(const RuntimeWindowState& state) {
    return concord::render::runtimeRepairCursorPhase(state.tick, state.config.fixedTickHz);
}

POINT runtimeBuildingRepairIconAnchor(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RECT& viewport) {
    const auto corners = runtimeStructureSelectionFootprintCenters(state, object, viewport);
    return runtimeRenderPlacementNativePoint(concord::render::runtimeBuildingRepairIconAnchor(
        std::span<const POINT>{corners}, runtimeStructureSelectionVerticalHeight(state, object)));
}

int drawRuntimeRepairOverlays(HDC dc, const RECT& viewport, RuntimeWindowState& state,
    const std::vector<RuntimeProjectedObject>& projectedObjects) {
    if (state.repairingStructureTicks.empty()) return 0;
    const int frame = runtimeBuildingRepairIconFrame(state);
    auto* entry = loadRuntimeUiShpFrame(state, "", "wrench", frame);
    const auto* palette = runtimeUiPaletteForEntry(state, entry, "mousepal");
    if (!entry || !palette) return 0;
    const auto target = runtimePixelSurfaceTargetForDc(state, dc);
    int drawn = 0;
    for (const auto& projected : projectedObjects) {
        const auto* object = projected.object;
        if (!object || !object->alive || object->kind != WorldObjectKind::Structure
            || !state.repairingStructureTicks.contains(object->instanceId)) continue;
        const auto anchor = runtimeRenderPlacementPixelPoint(runtimeBuildingRepairIconAnchor(state, *object, viewport));
        const auto& image = entry->image;
        const auto placement = concord::render::runtimePlaceBuildingRepairIcon(
            anchor, runtimeRenderPlacementPixelRect(viewport), image);
        if (!placement) continue;
        const RuntimeShpFrameImage* sprite = &image;
        RuntimeShpFrameImage clipped;
        if (placement->needsCrop) {
            concord::render::cropRuntimeBuildingRepairIcon(image, *placement, clipped);
            sprite = &clipped;
        }
        if (drawRuntimeIndexedImage(dc, runtimeRenderPlacementNativeRect(placement->visible), *sprite, *palette, true, false,
                true, target.pixels, target.width, target.height)) ++drawn;
    }
    return drawn;
}
