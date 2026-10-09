// Discharge pose is chosen from the weapon actually fired. Sampling the
// generic Attack pose confuses Boris' rifle with his designator and samples
// a deployed infantryman's idle frame instead of his firing frame.
WorldObjectInstance runtimeWeaponFiringPose(const RuntimeWindowState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile) {
    WorldObjectInstance pose = attacker;
    if (pose.kind != WorldObjectKind::Infantry) return pose;
    bool secondary = profile.slot == RuntimeWeaponSlot::Secondary
        || profile.slot == RuntimeWeaponSlot::EliteSecondary
        || ((profile.slot == RuntimeWeaponSlot::Indexed || profile.slot == RuntimeWeaponSlot::EliteIndexed)
            && profile.indexedWeaponIndex == 1);
    if (const auto* unit = findRuntimeUnitType(state.rules, attacker)) {
        if (profile.slot == RuntimeWeaponSlot::OpenTransport)
            secondary = unit->openTransportWeapon == 1 || unit->openTransportWeapon == 3;
    }
    pose.mission = pose.deployed || profile.deployed
        || profile.slot == RuntimeWeaponSlot::Deploy || profile.slot == RuntimeWeaponSlot::EliteDeploy
        ? "DeployedFire" : secondary ? "SecondaryFire" : "Attack";
    return pose;
}

void alignRuntimeWeaponMuzzleAtCamera(RuntimeWindowState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile,
    int firingFacing, RuntimeProjectileSourcePosition& source, const RuntimeMapCamera& camera) {
    if (!state.artLoaded) return;
    const auto flh = runtimeWeaponFireFlh(state, attacker, profile, runtimeArtTypeForObject(state, attacker));
    if (!flh && !findRuntimeUnitType(state.rules, attacker)
        && !findRuntimeBuildingType(state.rules, attacker)) return;
    RuntimeMapRenderContext context;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = &state.art;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.animationTick = state.tick;
    context.fixedTickHz = state.config.fixedTickHz;
    context.impactActors = runtimeImpactRenderActorsForRender(state);
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    const auto firingPose = runtimeWeaponFiringPose(state, attacker, profile);
    const auto port = runtimeOpenTransportMuzzle(state, attacker, profile);
    const auto& visibleSource = port ? *port->host : firingPose;
    const auto attachmentFlh = port ? port->flh : flh.value_or(RuntimeWeaponFireFlh{});
    // The caller supplies an unscrolled reference projection.
    const RECT viewport{0, 0, 1024, 768};
    const auto placement = runtimeObjectWorldRenderPlacement(visibleSource, state.world,
        viewport, context, camera, state.renderCache);
    if (profile.occupied) {
        const auto* art = runtimeArtTypeForObject(state, attacker);
        if (art && profile.occupantMuzzleIndex >= 0
            && profile.occupantMuzzleIndex < static_cast<int>(art->muzzleFlashes.size())) {
            const auto pixel = art->muzzleFlashes[profile.occupantMuzzleIndex];
            RuntimeProjectileRenderActor anchor;
            anchor.hasCurrentPosition = true;
            anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
            const auto logical = runtimeProjectileActorScreenPoint(anchor, viewport, camera);
            // ART window positions are native bitmap pixels relative to
            // the center-authored structure canvas at every camera zoom.
            source.screenOffsetX = placement.center.x + pixel.x - logical.x;
            source.screenOffsetY = placement.center.y + pixel.y - logical.y;
        }
        return;
    }
    if (!port && attacker.kind == WorldObjectKind::Infantry) {
        POINT native{};
        if (resolveRuntimeInfantryWeaponMuzzlePoint(firingPose, placement.center, context, camera, state.renderCache, native)) {
            RuntimeProjectileRenderActor anchor;
            anchor.hasCurrentPosition = true;
            anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
            const auto logical = runtimeProjectileActorScreenPoint(anchor, viewport, camera);
            source.screenOffsetX = native.x - logical.x;
            source.screenOffsetY = native.y - logical.y;
            return;
        }
        RECT body{};
        if (resolveRuntimeObjectShpSpriteBounds(firingPose, {0, 0}, context,
                camera, state.renderCache, body))
            source.screenOffsetY += (body.top + body.bottom) / 2;
        const auto ordinary = runtimeObjectBodyAnchorForRender(firingPose, viewport, context, camera);
        source.screenOffsetX += placement.center.x - ordinary.x;
        source.screenOffsetY += placement.center.y - ordinary.y;
        return;
    }
    POINT marine{};
    if (!port && resolveRuntimeMarineWeaponMuzzlePoint(firingPose, placement.center, context,
            camera, state.renderCache, marine)) {
        RuntimeProjectileRenderActor anchor;
        anchor.hasCurrentPosition = true;
        anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
        const auto logical = runtimeProjectileActorScreenPoint(anchor, viewport, camera);
        source.screenOffsetX = marine.x - logical.x;
        source.screenOffsetY = marine.y - logical.y;
        return;
    }
    POINT structure{};
    const bool secondary = profile.slot == RuntimeWeaponSlot::Secondary
        || profile.slot == RuntimeWeaponSlot::EliteSecondary
        || ((profile.slot == RuntimeWeaponSlot::Indexed || profile.slot == RuntimeWeaponSlot::EliteIndexed)
            && profile.indexedWeaponIndex == 1);
    if (!port && resolveRuntimeStructureWeaponMuzzlePoint(firingPose, placement.center, context,
            camera, state.renderCache, attachmentFlh.forwardLeptons, attachmentFlh.lateralLeptons,
            attachmentFlh.heightLeptons, firingFacing, secondary, structure)) {
        RuntimeProjectileRenderActor anchor;
        anchor.hasCurrentPosition = true;
        anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
        const auto logical = runtimeProjectileActorScreenPoint(anchor, viewport, camera);
        source.screenOffsetX = structure.x - logical.x;
        source.screenOffsetY = structure.y - logical.y;
        return;
    }
    POINT modelMuzzle{};
    int lateral = attachmentFlh.lateralLeptons;
    if (runtimeCachedCapabilityProfileForObject(state, attacker).gattling) {
        const auto burst = state.combatDetails.bursts.find(attacker.instanceId);
        if (burst != state.combatDetails.bursts.end() && profile.weapon
            && burst->second.weaponId == profile.weapon->id && burst->second.nextShotIndex % 2 != 0)
            lateral = -lateral;
    }
    if (resolveRuntimeVoxelWeaponMuzzlePoint(visibleSource, placement.center, context,
            camera, state.renderCache, attachmentFlh.forwardLeptons, lateral, attachmentFlh.heightLeptons,
            port ? std::max(0, visibleSource.facing) : firingFacing, modelMuzzle, !port)) {
        RuntimeProjectileRenderActor anchor;
        anchor.currentX = source.x; anchor.currentY = source.y; anchor.currentLevel = source.z;
        anchor.hasCurrentPosition = true;
        const POINT logical = runtimeProjectileActorScreenPoint(anchor, viewport, camera);
        source.screenOffsetX = modelMuzzle.x - logical.x;
        source.screenOffsetY = modelMuzzle.y - logical.y;
    } else {
        const auto ordinary = runtimeObjectBodyAnchorForRender(visibleSource, viewport, context, camera);
        source.screenOffsetX += placement.center.x - ordinary.x;
        source.screenOffsetY += placement.center.y - ordinary.y;
    }
}

// SHP/VXL rasters retain their pixel size when tile projection changes. Store
// the affine correction as an unscaled bitmap term and a projected world term.
void alignRuntimeWeaponMuzzleToSprite(RuntimeWindowState& state,
    const WorldObjectInstance& attacker, const RuntimeWeaponProfile& profile,
    int firingFacing, RuntimeProjectileSourcePosition& source) {
    RuntimeMapCamera camera; camera.initialized = true;
    auto reference = source;
    alignRuntimeWeaponMuzzleAtCamera(state, attacker, profile, firingFacing, reference, camera);
    auto doubled = source;
    doubled.screenOffsetX = 2 * source.screenOffsetX - source.nativeFixedX;
    doubled.screenOffsetY = 2 * source.screenOffsetY - source.nativeFixedY;
    camera.tileWidth = 120; camera.tileHeight = 60;
    alignRuntimeWeaponMuzzleAtCamera(state, attacker, profile, firingFacing, doubled, camera);
    source.screenOffsetX = reference.screenOffsetX;
    source.screenOffsetY = reference.screenOffsetY;
    source.nativeFixedX = 2 * reference.screenOffsetX - doubled.screenOffsetX;
    source.nativeFixedY = 2 * reference.screenOffsetY - doubled.screenOffsetY;
}
