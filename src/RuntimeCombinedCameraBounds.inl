// This result exists only for the current clamp. Both envelopes read the same
// terrain/projection; no terrain identity or revision is cached across calls.
struct RuntimeCombinedCameraBounds {
    RuntimeCameraWorldBounds playable;
    RuntimeCameraWorldBounds full;
};

inline thread_local int runtimeCameraBoundsPassReferenceOverride = -1;
bool runtimeCameraBoundsPassReference() {
    if (runtimeCameraBoundsPassReferenceOverride >= 0)
        return runtimeCameraBoundsPassReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_CAMERA_BOUNDS_PASS_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
struct RuntimeCameraBoundsPassReferenceScope {
    int previous = runtimeCameraBoundsPassReferenceOverride;
    explicit RuntimeCameraBoundsPassReferenceScope(bool reference) {
        runtimeCameraBoundsPassReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeCameraBoundsPassReferenceScope() {
        runtimeCameraBoundsPassReferenceOverride = previous;
    }
    RuntimeCameraBoundsPassReferenceScope(const RuntimeCameraBoundsPassReferenceScope&) = delete;
    RuntimeCameraBoundsPassReferenceScope& operator=(const RuntimeCameraBoundsPassReferenceScope&) = delete;
};

RuntimeCombinedCameraBounds runtimeCombinedCameraWorldBounds(
    const RuntimeWindowState& state,
    const RuntimeRadarProjection* completedProjection = nullptr
) {
    RuntimeCombinedCameraBounds result;
    const RuntimeRadarProjection projection = completedProjection != nullptr
        ? *completedProjection : buildRuntimeRadarProjection(state);
    const int halfWidth = std::max(1, state.camera.tileWidth / 2);
    const int halfHeight = std::max(1, state.camera.tileHeight / 2);
    const auto append = [](RuntimeCameraWorldBounds& bounds,
                           const RuntimeCameraWorldBounds& cell) {
        if (!bounds.valid) {
            bounds = cell;
        } else {
            bounds.left = std::min(bounds.left, cell.left);
            bounds.top = std::min(bounds.top, cell.top);
            bounds.right = std::max(bounds.right, cell.right);
            bounds.bottom = std::max(bounds.bottom, cell.bottom);
        }
    };
    for (const TerrainCell& terrain : state.world.getTerrainCells()) {
        // Preserve the original integer projection and truncation order.
        const int projectedX = ((terrain.x - terrain.y) * state.camera.tileWidth) / 2;
        const int projectedY = ((terrain.x + terrain.y) * state.camera.tileHeight) / 2
            - ((terrain.level * state.camera.tileHeight) / 2);
        const RuntimeCameraWorldBounds cell{projectedX - halfWidth,
            projectedY - halfHeight, projectedX + halfWidth,
            projectedY + halfHeight, true};
        append(result.full, cell);
        if (!projection.valid || runtimeRadarTerrainCellIntersectsProjection(projection, terrain))
            append(result.playable, cell);
    }
    // A disjoint LocalSize uses the original complete decoded envelope.
    if (!result.playable.valid) result.playable = result.full;
    return result;
}

RuntimeCombinedCameraBounds runtimeMemoizedCameraWorldBounds(const RuntimeWindowState& state) {
    // Preserve the projection builder's original cache behavior and its actual
    // returned values. Every terrain geometry value is checked on every hit.
    const auto projection = buildRuntimeRadarProjection(state);
    const RuntimeCameraGeometryProjection key{projection.valid, projection.minX,
        projection.minY, projection.maxX, projection.maxY, projection.fullMapWidth};
    const auto& cells = state.world.getTerrainCells();
    const auto fromMemo = [](const RuntimeCameraGeometryBounds& b) {
        return RuntimeCameraWorldBounds{b.left,b.top,b.right,b.bottom,b.valid};
    };
    if (const auto hit = state.cameraGeometryMemo.find(cells,state.camera.tileWidth,
        state.camera.tileHeight,key))
        return {fromMemo(hit->playable),fromMemo(hit->full)};
    const auto result = runtimeCombinedCameraWorldBounds(state,&projection);
    const auto toMemo = [](const RuntimeCameraWorldBounds& b) {
        return RuntimeCameraGeometryBounds{b.left,b.top,b.right,b.bottom,b.valid};
    };
    state.cameraGeometryMemo.store(cells,state.camera.tileWidth,state.camera.tileHeight,key,
        {toMemo(result.playable),toMemo(result.full)});
    return result;
}
