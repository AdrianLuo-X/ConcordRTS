// The host has already filtered concealed Ivan bombs with BombSight. The
// ordinary special-status pass also checks current visibility of the target.
int drawRuntimeIvanBombIcon(HDC dc, const RECT& viewport,
    const WorldObjectInstance& target, const SpecialMechanismRenderSnapshot& snapshot,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    auto* resource = loadRuntimeAuxiliaryShpSpriteFromCache("BOMBCURS",0,context,cache,false);
    const int duration = std::max(1,snapshot.requiredProgress);
    const int progress = std::clamp(snapshot.progress,0,duration);
    const int simulationTick = std::max(0,snapshot.expireTick - duration + progress);
    const int rate = context.rules ? context.rules->getSpecialMechanismRules().ivanIconFlickerRateFrames : 8;
    const int frame = runtimeIvanBombIconFrame(progress,duration,simulationTick,
        context.fixedTickHz,rate,resource && resource->loaded ? resource->frameCount : 13);
    if (frame < 0) return -1;
    // Intent diagnostics also run without assets or a drawing surface. Keep
    // their requested phase, just like the other special-status actors do.
    if (resource == nullptr || !resource->loaded) return frame;
    POINT anchor = screenPoint(runtimeSpecialMechanismObjectRenderPoint(target,camera),viewport,camera);
    anchor.y -= std::max(1,24 * camera.tileHeight / 30);
    // Retail TechnoClass::DrawExtras (0x6F5235) uses the MOUSEPAL.PAL
    // conversion installed at 0x52C1DD, with no house-color remapping.
    // Keep the status marker independent of world-animation detail cutoffs.
    const auto* palette = runtimeNamedPaletteForRender(context,cache,"mousepal.pal");
    if (palette != nullptr) (void)drawRuntimeAuxiliaryShpSpriteWithPalette(dc,viewport,anchor,
        context,camera,cache,"BOMBCURS",frame,*palette,0,0,false,false,RGB(255,255,255),255,true);
    return frame;
}
