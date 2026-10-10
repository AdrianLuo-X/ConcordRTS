// Normalized original CellClass::GetFloorHeight (0x47B3A0) coefficients:
// gx, gy, base, cap. TMP's packed screen-depth bytes are not slope heights.
double runtimeVehicleRampHeight(unsigned char ramp, double localX, double localY) {
    // Retail special ramps 17--20 have a constant half-level collision floor.
    // Their image corners must not be turned into a spurious saddle tilt.
    if (ramp >= 17 && ramp <= 20) return 0.5;
    static constexpr std::array<double, 4> planes[] = {
        {0,0,0,0}, {1,0,0,1}, {0,1,0,1}, {-1,0,1,1}, {0,-1,1,1},
        {1,1,-1,1}, {-1,1,0,1}, {-1,-1,1,1}, {1,-1,0,1},
        {1,1,0,1}, {-1,1,1,1}, {-1,-1,2,1}, {1,-1,1,1},
        {1,1,0,2}, {-1,1,1,2}, {-1,-1,2,2}, {1,-1,1,2}
    };
    const auto& plane = planes[ramp < std::size(planes) ? ramp : 0];
    const double u = localX + 0.5, v = localY + 0.5;
    return std::clamp(plane[0] * u + plane[1] * v + plane[2], 0.0, plane[3]);
}

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
    struct Sample { const TerrainCell* cell = nullptr; unsigned char ramp = 0; double height = 0.0; };
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
        result.height = cell->level + runtimeVehicleRampHeight(result.ramp, x - cell->x, y - cell->y);
        return result;
    };
    const double x = objectRenderX(object), y = objectRenderY(object);
    if (!std::isfinite(x) || !std::isfinite(y)) return pose;
    const auto center = sample(x, y);
    if (!center.cell) return pose;
    constexpr double supportRadius = 0.35;
    auto left = sample(x - supportRadius, y), right = sample(x + supportRadius, y);
    auto back = sample(x, y - supportRadius), front = sample(x, y + supportRadius);
    const auto supportedHeight = [&](Sample& point, double px, double py) {
        const double fallback = center.cell->level + runtimeVehicleRampHeight(center.ramp,
            px - center.cell->x, py - center.cell->y);
        if (!point.cell) { point.ramp = center.ramp; return fallback; }
        if (point.cell != center.cell) {
            // A cliff or missing bank is not a continuous road slope. Check
            // both tiles at their common edge before blending a support point.
            const double bx = point.cell->x == center.cell->x ? x
                : (point.cell->x + center.cell->x) * 0.5;
            const double by = point.cell->y == center.cell->y ? y
                : (point.cell->y + center.cell->y) * 0.5;
            const double a = center.cell->level + runtimeVehicleRampHeight(center.ramp,
                bx - center.cell->x, by - center.cell->y);
            const double b = point.cell->level + runtimeVehicleRampHeight(point.ramp,
                bx - point.cell->x, by - point.cell->y);
            if (std::abs(a - b) > 0.0001) { point.ramp = center.ramp; return fallback; }
        }
        return point.height;
    };
    const double hx0 = supportedHeight(left, x - supportRadius, y);
    const double hx1 = supportedHeight(right, x + supportRadius, y);
    const double hy0 = supportedHeight(back, x, y - supportRadius);
    const double hy1 = supportedHeight(front, x, y + supportRadius);
    if (center.ramp == 0 && left.ramp == 0 && right.ramp == 0 && back.ramp == 0 && front.ramp == 0)
        return pose;
    const auto projection = runtimeVxlProjectionParamsForSizing(runtimeVxlRasterSizingForOwner(object.type));
    const double scale = projection.groundYScale / projection.heightYScale;
    const double gx = (hx1 - hx0) / (2.0 * supportRadius) * scale;
    const double gy = (hy1 - hy0) / (2.0 * supportRadius) * scale;
    pose.roll = pose.terrainRoll = std::atan(gy);
    pose.pitch = pose.terrainPitch = -std::atan(gx * std::cos(pose.roll));
    pose.terrainHeight = center.height - objectRenderLevel(object);
    pose.terrainSupported = true;
    return pose;
}
