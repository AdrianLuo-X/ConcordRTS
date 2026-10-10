POINT runtimeMissileDeckOffsetAtCamera(RuntimeWindowState& state,
    const WorldObjectInstance& parent, int slot, const RuntimeMapCamera& camera) {
    RuntimeWeaponProfile launcher;
    launcher.slot = RuntimeWeaponSlot::Primary;
    if (const auto* unit = findRuntimeUnitType(state.rules, parent)) {
        const auto* primary = state.rules.findWeaponType(unit->primaryWeapon);
        const auto* secondary = state.rules.findWeaponType(unit->secondaryWeapon);
        if ((!primary || !primary->spawner) && secondary && secondary->spawner)
            launcher.slot = parent.veterancy >= 2 && !unit->eliteSecondaryWeapon.empty()
                ? RuntimeWeaponSlot::EliteSecondary : RuntimeWeaponSlot::Secondary;
        else if (parent.veterancy >= 2 && !unit->elitePrimaryWeapon.empty())
            launcher.slot = RuntimeWeaponSlot::ElitePrimary;
    }
    const auto* art = runtimeArtTypeForObject(state, parent);
    auto flh = runtimeWeaponFireFlh(state, parent, launcher, art);
    if (!flh) return {};
    const auto second = art ? parseRuntimeWeaponFireFlh(art->secondSpawnOffset)
        : std::optional<RuntimeWeaponFireFlh>{};
    if (second && slot > 0) {
        flh->forwardLeptons += second->forwardLeptons;
        flh->lateralLeptons += second->lateralLeptons;
        flh->heightLeptons += second->heightLeptons;
    } else if (slot % 2 != 0) flh->lateralLeptons = -flh->lateralLeptons;
    RuntimeMapRenderContext context;
    context.art = &state.art; context.rules = &state.rules;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.animationTick = state.tick; context.fixedTickHz = state.config.fixedTickHz;
    context.impactActors = runtimeImpactRenderActorsForRender(state);
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    const RECT viewport{0, 0, 1024, 768};
    const auto placement = runtimeObjectWorldRenderPlacement(parent, state.world, viewport, context, camera, state.renderCache);
    POINT native{};
    if (!resolveRuntimeVoxelWeaponMuzzlePoint(parent, placement.center, context, camera, state.renderCache,
            flh->forwardLeptons, flh->lateralLeptons, flh->heightLeptons,
            std::max(0, parent.facing), native, false)) return {};
    const auto deck = runtimeMissileDeckPosition(state, parent, slot);
    RuntimeProjectileRenderActor logical;
    logical.hasCurrentPosition = true;
    logical.currentX = deck.x; logical.currentY = deck.y; logical.currentLevel = deck.z;
    const auto point = runtimeProjectileActorScreenPoint(logical, viewport, camera);
    return {native.x - point.x, native.y - point.y};
}

struct RuntimeMissileDeckPresentation {
    int x = 0, y = 0, fixedX = 0, fixedY = 0;
};
RuntimeMissileDeckPresentation runtimeMissileDeckPresentationOffset(RuntimeWindowState& state,
    const WorldObjectInstance& parent, int slot) {
    RuntimeMapCamera camera; camera.initialized = true;
    const auto reference = runtimeMissileDeckOffsetAtCamera(state, parent, slot, camera);
    camera.tileWidth = 120; camera.tileHeight = 60;
    const auto doubled = runtimeMissileDeckOffsetAtCamera(state, parent, slot, camera);
    return {reference.x, reference.y, 2 * reference.x - doubled.x, 2 * reference.y - doubled.y};
}
