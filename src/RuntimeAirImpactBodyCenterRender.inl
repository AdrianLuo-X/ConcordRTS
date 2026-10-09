namespace {

bool runtimeAirImpactBodyCenterAtCamera(
    const GameWorld& world, const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    double hitX, double hitY, double hitLevel,
    const RuntimeMapCamera& camera, int& offsetX, int& offsetY,
    const SpecialMechanismRenderSnapshot* missile
) {
    const RECT viewport{0, 0, 1024, 768};
    RuntimeGeometryQueryScope query(world, viewport, context, camera, cache);
    // Roof clearance is piecewise in camera scale. Freeze it separately instead
    // of fitting its max/clamp branches into the two-point affine correction.
    POINT bodyCenter = runtimeObjectBodyAnchorForRender(object, viewport, context, camera);
    if (missile != nullptr) {
        // Match drawRuntimeSpawnedMissileBody's continuous pitched pose. It
        // has neither the ordinary aircraft lift nor a roof-clearance lift.
        bodyCenter = runtimeVoxelModelDrawCenter(screenPoint(projectCellXY(
            missile->sourceX, missile->sourceY, missile->sourceLevel, camera),
            viewport, camera), camera);
        bodyCenter.x += runtimeMuzzlePixelX(missile->sourceScreenOffsetX,
            camera, missile->sourceNativeFixedX);
        bodyCenter.y += runtimeMuzzlePixelY(missile->sourceScreenOffsetY,
            camera, missile->sourceNativeFixedY);
    }

    RuntimeSpriteGeometryCapture geometry;
    RuntimeSpriteGeometryScope capture(&geometry);
    const LONG low = std::numeric_limits<LONG>::lowest() / 2;
    const LONG high = std::numeric_limits<LONG>::max() / 2;
    const RECT unclipped{low, low, high, high};
    // Replay the real body layers at the real flight pose, without appending
    // the separate parachute or terrain/bridge shadow receiver.
    bool drawn = false;
    if (missile != nullptr) {
        drawn = drawRuntimeVoxelImageSprite(nullptr, unclipped, bodyCenter,
            context, camera, cache, missile->resourceId,
            missile->resourceId + "|missile", missile->missileFacing,
            0, 0, 0, runtimeOwnerHasRendererRemapColor(context, missile->owner),
            runtimeOwnerRemapColor(context, missile->owner), nullptr,
            0.0, 255, missile->missilePitch);
    } else if (const auto* disguise = runtimeCamouflageSnapshot(context, object)) {
        drawn = drawRuntimeCamouflageBody(nullptr, unclipped, bodyCenter,
            context, camera, cache, *disguise);
    } else if (const auto* disguise = runtimeSpyDisguiseSnapshot(context, object.instanceId)) {
        const auto projected = runtimeSpyBodyProjection(object, *disguise);
        drawn = drawRuntimeSpyDisguiseBody(nullptr, unclipped, projected,
            bodyCenter, context, camera, cache, *disguise);
    } else {
        drawn = object.kind == WorldObjectKind::Unit && objectUsesVoxelArt(object, context)
            ? tryDrawVoxelSprite(nullptr, unclipped, object, bodyCenter, context, camera, cache)
            : tryDrawObjectShpSprite(nullptr, unclipped, object, bodyCenter, context, camera, cache);
    }
    if (!drawn || !geometry.valid) return false;

    const POINT logical = screenPoint(projectCellXY(hitX, hitY, hitLevel, camera), viewport, camera);
    const auto centerX = (static_cast<long long>(geometry.bounds.left) + geometry.bounds.right) / 2;
    const auto centerY = (static_cast<long long>(geometry.bounds.top) + geometry.bounds.bottom) / 2;
    const auto x = centerX - logical.x;
    const auto y = centerY - logical.y;
    // Match the existing impact presentation record's bounded pixel fields.
    if (x < -4096 || x > 4096 || y < -4096 || y > 4096) return false;
    offsetX = static_cast<int>(x);
    offsetY = static_cast<int>(y);
    return true;
}

} // namespace

bool resolveRuntimeAirImpactBodyCenterOffset(
    const GameWorld& world, const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    double hitX, double hitY, double hitLevel,
    int& screenX, int& screenY, int& fixedX, int& fixedY,
    RuntimeParticleAirbornePresentation& presentation, int& spriteOffsetY
) {
    if (!object.airborne || object.kind == WorldObjectKind::Structure
        || !std::isfinite(hitX) || !std::isfinite(hitY) || !std::isfinite(hitLevel)) return false;
    RuntimeRendererEnvironmentScope environment;
    auto probe = context;
    probe.surfacePixels = nullptr;
    probe.surfaceWidth = probe.surfaceHeight = 0;
    probe.surfaceTopDownOwned = false;
    const SpecialMechanismRenderSnapshot* missile = nullptr;
    const auto* unit = findObjectUnitTypeForRender(probe, object);
    if (unit != nullptr && unit->missileSpawn) {
        if (probe.specialMechanismSnapshots == nullptr) return false;
        for (const auto& snapshot : *probe.specialMechanismSnapshots) {
            if (snapshot.kind == "spawnedMissile" && snapshot.objectId == object.instanceId) {
                missile = &snapshot;
                break;
            }
        }
        if (missile == nullptr) return false;
    }
    RuntimeParticleAirbornePresentation frozenPresentation;
    if (missile == nullptr && !captureRuntimeAircraftAirbornePresentation(world, object, probe, cache,
            frozenPresentation)) return false;
    RuntimeMapCamera camera;
    camera.initialized = true;
    int referenceX = 0, referenceY = 0, doubledX = 0, doubledY = 0;
    if (!runtimeAirImpactBodyCenterAtCamera(world, object, probe, cache,
            hitX, hitY, hitLevel, camera, referenceX, referenceY, missile)) return false;
    camera.tileWidth = 120;
    camera.tileHeight = 60;
    if (!runtimeAirImpactBodyCenterAtCamera(world, object, probe, cache,
            hitX, hitY, hitLevel, camera, doubledX, doubledY, missile)) return false;
    screenX = referenceX;
    screenY = referenceY;
    fixedX = 2 * referenceX - doubledX;
    fixedY = 2 * referenceY - doubledY;
    presentation = std::move(frozenPresentation);
    spriteOffsetY = missile != nullptr ? 0 : -runtimeAircraftSpriteLiftPixels(object,
        runtimeObjectLooksAircraftForRender(object, probe));
    return true;
}
