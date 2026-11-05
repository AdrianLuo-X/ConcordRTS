// Issue25/10: original Wall=yes overlays remain terrain-owned values.
// Included after projectile-terrain damage helpers; no WorldObject is promoted.














static bool runtimeOverlayWallSelectionCanAttack(
    RuntimeWindowState& state, const WorldObjectInstance& target) {
    for (int id : runtimeCombatSelection(state)) {
        const auto* source = findObjectById(state, id);
        if (!source || !source->alive || source->health <= 0 || source->owner != state.economy.owner
            || runtimeObjectDisabledByParasite(state, id) || !runtimeObjectHasAttackAmmo(state.rules, *source)) continue;
        const auto profile = runtimeWeaponProfileForObject(state, *source, &target, state.forceAttackHeld);
        if (profile.weapon && profile.weapon->damage > 0 && profile.weapon->range > 0
            && (!profile.projectile || profile.projectile->ag)) return true;
    }
    return false;
}

std::optional<WorldObjectInstance> visibleTerrainWallTargetAtPoint(
    RuntimeWindowState& state, const RECT& viewport, POINT point) {
    if (state.selectedObjectIds.empty() || !PtInRect(&viewport, point)) return std::nullopt;
    const auto& projected = runtimeProjectedObjectsForViewport(state, viewport);
    const auto* normal = findProjectedObjectAtPoint(projected, point);
    // Only an empty foundation fallback can yield to a real visible wall pixel.
    // Opaque structures, mobiles and final foreground aircraft keep their hit priority.
    if (normal && (!normal->usesFootprintHit
        || runtimeSpriteHitSpansContainWin32(normal->structureSpriteHitSpans, point))) return std::nullopt;
    RuntimeMapRenderContext context;
    context.stableRenderConfiguration = true;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = state.artLoaded ? &state.art : nullptr;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
    context.ownerRemapColors = state.ownerRemapColors.empty() ? nullptr : &state.ownerRemapColors;
    context.neutralRemapOwners = state.neutralRemapOwners.empty() ? nullptr : &state.neutralRemapOwners;
    const auto* paint = runtimeGeometryBatchRenderContext();
    if (!paint) {
        refreshRuntimeGateRenderSnapshots(state);
        refreshRuntimeSpecialMechanismRenderSnapshots(state);
    }
    context.gateSnapshots = &state.gateRenderSnapshots;
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    if (runtimeLocalVisionRestricted(state)) {
        context.vision = &state.vision;
        context.visionObserverOwner = state.economy.owner;
    }
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.animationTick = state.tick; context.fixedTickHz = state.config.fixedTickHz;
    context.impactActors = paint ? paint->impactActors : runtimeImpactRenderActorsForRender(state);
    context.selectedObjectIds = &state.selectedObjectIds;
    context.playerCheerUntilTick = &state.playerCommunication.cheerUntilTick;
    if (paint) context = *paint;
    RuntimeGeometryQueryScope geometry(state.world, runtimeRenderPlacementPixelRect(viewport), context, state.camera, state.renderCache, true);
    std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>> spans;
    if (!resolveRuntimeBuildingVisibleHitSpansWin32(state.world, viewport, context, state.camera, state.renderCache, spans))
        return std::nullopt;
    for (const auto& [id, pixels] : spans) {
        if (!runtimeIsOverlayWallTargetId(id) || !runtimeSpriteHitSpansContainWin32(pixels, point)) continue;
        const int key = id - std::numeric_limits<int>::min();
        auto target = runtimeOverlayWallTargetAtCell(state, {key % 512, key / 512});
        if (target && runtimeObjectVisibleToOwner(state, state.economy.owner, *target)
            && runtimeOverlayWallSelectionCanAttack(state, *target)) return target;
    }
    return std::nullopt;
}


