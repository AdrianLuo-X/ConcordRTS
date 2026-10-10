struct RuntimeAirImpactBodyCenter {
    bool captured = false;
    int screenX = 0;
    int screenY = 0;
    int fixedX = 0;
    int fixedY = 0;
    RuntimeParticleAirbornePresentation presentation;
    int spriteOffsetY = 0;
};

RuntimeAirImpactBodyCenter captureRuntimeAirImpactBodyCenter(
    RuntimeWindowState& state, const WorldObjectInstance& target,
    const RuntimeProjectileTargetPosition& hit
) {
    RuntimeAirImpactBodyCenter center;
    if (!target.alive || !target.airborne || !state.artLoaded) return center;
    // Hits can arrive between paints. Refresh the real missile presentation
    // at this pre-damage pose instead of using a previous paint's pitch cache.
    const auto* unit = findRuntimeUnitType(state.rules, target);
    if (unit && unit->missileSpawn) refreshRuntimeSpecialMechanismRenderSnapshots(state);
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = &state.art;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;
    context.impactActors = runtimeImpactRenderActorsForRender(state);
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    center.captured = resolveRuntimeAirImpactBodyCenterOffset(state.world, target,
        context, state.renderCache, hit.x, hit.y, hit.level,
        center.screenX, center.screenY, center.fixedX, center.fixedY,
        center.presentation, center.spriteOffsetY);
    return center;
}

void applyRuntimeAirImpactBodyCenter(
    RuntimeImpactEffect& impact, const RuntimeAirImpactBodyCenter* center
) {
    if (!center || !center->captured) return;
    impact.screenOffsetX = center->screenX;
    impact.screenOffsetY = center->screenY;
    impact.nativeFixedX = center->fixedX;
    impact.nativeFixedY = center->fixedY;
    impact.bodyCenterAnchored = true;
    impact.bodyCenterPresentation = center->presentation;
    impact.bodyCenterSpriteOffsetY = center->spriteOffsetY;
}
