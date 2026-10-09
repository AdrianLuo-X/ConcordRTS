// Retail TechnoClass::DrawExtras (0x6F52D8..0x6F5340): WRENCH.SHP,
// MOUSEPAL.PAL, and a speed-normalized sweep through its seven frames.
int runtimeBuildingRepairIconFrame(const RuntimeWindowState& state) {
    const int retailSpeed = 6 - std::clamp(state.config.gameSpeed, 0, 6);
    const int period = std::max(2, runtimeRuleNormalizedAnimationDelay(14, retailSpeed) / 4);
    const auto phase = runtimeRuleFrameAtTick(state.tick, state.config.fixedTickHz) % period;
    return static_cast<int>(phase * 6 / (period - 1));
}

int runtimeRepairCursorPhase(const RuntimeWindowState& state) {
    return static_cast<int>((runtimeRuleFrameAtTick(state.tick, state.config.fixedTickHz) / 4) % 20);
}

POINT runtimeBuildingRepairIconAnchor(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RECT& viewport) {
    const auto corners = runtimeStructureSelectionFootprintCenters(state, object, viewport);
    POINT anchor{};
    for (const auto& corner : corners) { anchor.x += corner.x; anchor.y += corner.y; }
    anchor.x /= static_cast<LONG>(corners.size());
    anchor.y /= static_cast<LONG>(corners.size());
    // A stable point on the building, independent of its cropped idle/damage
    // animation bounds. Keep the native 55x43 canvas and its center hotspot.
    anchor.y -= runtimeStructureSelectionVerticalHeight(state, object) / 2;
    return anchor;
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
        const POINT anchor = runtimeBuildingRepairIconAnchor(state, *object, viewport);
        const auto& image = entry->image;
        const int left = anchor.x - image.globalWidth / 2 + image.offsetX;
        const int top = anchor.y - image.globalHeight / 2 + image.offsetY;
        const RECT rect{left, top, left + image.width, top + image.height};
        RECT visible{};
        if (!IntersectRect(&visible, &rect, &viewport)) continue;
        const RuntimeShpFrameImage* sprite = &image;
        RuntimeShpFrameImage clipped;
        if (!EqualRect(&visible, &rect)) {
            // CPU sprite blits do not inherit the HDC clip. Crop only at the
            // viewport edge so the wrench cannot paint over the sidebar.
            clipped.width = visible.right - visible.left;
            clipped.height = visible.bottom - visible.top;
            clipped.pixels.reserve(static_cast<std::size_t>(clipped.width) * clipped.height);
            for (int y = visible.top - top; y < visible.bottom - top; ++y) {
                const auto first = image.pixels.begin() + y * image.width + visible.left - left;
                clipped.pixels.insert(clipped.pixels.end(), first, first + clipped.width);
            }
            sprite = &clipped;
        }
        if (drawRuntimeIndexedImage(dc, visible, *sprite, *palette, true, false,
                true, target.pixels, target.width, target.height)) ++drawn;
    }
    return drawn;
}
