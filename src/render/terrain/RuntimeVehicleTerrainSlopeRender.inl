const RuntimeTmpSubTileImage* resolveRuntimeTerrainMetadata(
    int tileIndex, int subTileIndex, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache);

const RuntimeShadowReceiverTerrainLookup& ensureRuntimeTerrainCellLookup(
    const GameWorld& world, RuntimeMapRenderCache& cache);

RuntimeVehicleBodyPose runtimeVehicleTerrainPoseForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context) {
    RuntimeVehicleBodyPose pose;
    auto* frame = RuntimeGeometryQueryFrame::active;
    if (!frame || !frame->matchesMetadata(context) || !context.rules
        || object.kind != WorldObjectKind::Unit || !object.alive || object.health <= 0
        || object.airborne || object.submerged || object.authoredAircraft
        || object.dockedToObjectId >= 0 || object.tankBunkerHostId >= 0
        || object.factoryExitHostId >= 0 || object.garrisonHostObjectId >= 0
        || objectOnBridgeForRender(object)) return pose;
    const auto* unit = context.rules->findUnitType("Vehicle", object.type);
    if (!unit || !unit->isTilter || unit->balloonHover) return pose;
    // Pure floating hulls retain their existing water/squid orientation. The
    // stock hover transports can also stand on land and follow its ramps.
    const auto movementMode = runtimeUnitMovementModeProfile(*unit);
    if (movementMode == PathMovementMode::Air || movementMode == PathMovementMode::Naval) return pose;
    const bool terrainReference = runtimePaintGeometryReference();
    if (terrainReference && !frame->vehicleTerrainPrepared) {
        for (const auto& cell : frame->world.getTerrainCells())
            frame->vehicleTerrainCells.try_emplace(runtimeTerrainCellCoordinateKey(cell.x, cell.y), &cell);
        frame->vehicleTerrainPrepared = true;
    }
    const auto& terrain = terrainReference ? frame->vehicleTerrainCells
        : ensureRuntimeTerrainCellLookup(frame->world, frame->cache);
    using Sample = concord::render::RuntimeVehicleTerrainSlopeSample;
    const auto sample = [&](double x, double y) {
        Sample result;
        const int cx = static_cast<int>(std::floor(x + 0.5));
        const int cy = static_cast<int>(std::floor(y + 0.5));
        const auto found = terrain.find(runtimeTerrainCellCoordinateKey(cx, cy));
        if (found == terrain.end()) return result;
        const auto* cell = found->second;
        const auto* image = resolveRuntimeTerrainMetadata(cell->tileIndex, cell->subTileIndex, context, frame->cache);
        if (!image || !image->present || runtimeLandTypeForTmpImage(image) == RuntimeLandTypeWater
            || (cell->hasOverlay && runtimeOverlayLooksLikeBridge(*cell))) return result;
        result.cell = cell; result.ramp = image->rampType;
        result.height = cell->level + concord::render::runtimeVehicleRampHeight(result.ramp, x - cell->x, y - cell->y);
        return result;
    };
    const double x = objectRenderX(object), y = objectRenderY(object);
    if (!std::isfinite(x) || !std::isfinite(y)) return pose;
    const auto center = sample(x, y);
    if (!center.cell) return pose;
    constexpr double supportRadius = concord::render::runtimeVehicleTerrainSupportRadius;
    const auto support = concord::render::runtimeVehicleTerrainSupport(x, y, {
        center,
        sample(x - supportRadius, y), sample(x + supportRadius, y),
        sample(x, y - supportRadius), sample(x, y + supportRadius)
    });
    if (!support) return pose;
    const auto projection = runtimeVxlProjectionParamsForSizing(runtimeVxlRasterSizingForOwner(object.type));
    const double scale = projection.groundYScale / projection.heightYScale;
    return concord::render::runtimeVehicleTerrainPose(*support, scale, objectRenderLevel(object));
}
