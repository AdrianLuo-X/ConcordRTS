// Original Wall=True overlays are scene-Z writers, unlike ore, fences inferred
// from names, or ordinary building templates. Included after overlay drawing
// and terrain metadata helpers; ownership is private to the immediate replay.
static const OverlayType* runtimeTerrainDepthWallRule(const TerrainCell& cell,
    const RuntimeMapRenderContext& context) {
    if (!cell.hasOverlay || !context.rules) return nullptr;
    const auto& overlays = context.rules->getOverlayTypes();
    // OverlayPack stores a zero-based registry ordinal. listIndex is the INI
    // key, which contains gaps; matching it can turn an unrelated rock into
    // a wall. Resolved map identity is authoritative when it is available.
    if (cell.overlayIndex >= 0 && cell.overlayIndex < static_cast<int>(overlays.size())) {
        const auto& type = overlays[static_cast<std::size_t>(cell.overlayIndex)];
        if (cell.overlayType.empty() || toLowerAscii(type.id) == toLowerAscii(cell.overlayType))
            return type.wall ? &type : nullptr;
    }
    if (!cell.overlayType.empty()) {
        const auto identity = toLowerAscii(cell.overlayType);
        for (const auto& type : overlays)
            if (toLowerAscii(type.id) == identity) return type.wall ? &type : nullptr;
    }
    return nullptr;
}

static bool runtimeWorldHasTerrainDepthWalls(const GameWorld& world,
    const RuntimeMapRenderContext& context) {
    return std::any_of(world.getTerrainCells().begin(), world.getTerrainCells().end(),
        [&](const TerrainCell& cell) { return runtimeTerrainDepthWallRule(cell, context) != nullptr; });
}

static void drawRuntimeTerrainWallDepthSprites(const GameWorld& world,
    const RECT& viewport, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    RuntimeBuildingDepthFrameState& frame, HDC dc = nullptr) {
    ensureRuntimeMapFactLookupCache(context, cache);
    for (const auto& cell : world.getTerrainCells()) {
        const auto* fact = runtimeMapFactForTerrainCell(context, cache, cell);
        TerrainCell combined;
        const TerrainCell* renderCell = &cell;
        if (fact && fact->hasOverlay) {
            combined = runtimeTerrainCellWithMapFactOverlay(cell, fact);
            renderCell = &combined;
        }
        const auto* rule = runtimeTerrainDepthWallRule(*renderCell, context);
        if (!rule || cell.x < 0 || cell.y < 0 || cell.x >= 512 || cell.y >= 512) continue;
        const auto center = screenPoint(projectCell(cell, camera), viewport, camera);
        const RECT bounds{center.x - camera.tileWidth, center.y - camera.tileHeight * 4,
            center.x + camera.tileWidth, center.y + camera.tileHeight * 3};
        if (!rectIntersects(bounds, viewport)) continue;
        WorldObjectInstance wall;
        wall.kind = WorldObjectKind::Structure; wall.type = rule->id;
        wall.x = cell.x; wall.y = cell.y; wall.level = cell.level;
        wall.instanceId = std::numeric_limits<int>::min() + cell.y * 512 + cell.x;
        wall.alive = wall.poweredOperational = true; wall.health = 1;
        if (frame.resolved && !frame.participatingIds.contains(wall.instanceId)) continue;
        RuntimeBuildingDepthObjectScope objectScope(wall, center, context);
        RuntimeBuildingDepthPartScope part(RuntimeBuildingDepthLayer::WallSprite);
        const auto firstDraw = frame.draws.size();
        const auto& image = renderCell->overlayImage.empty() ? renderCell->overlayType : renderCell->overlayImage;
        const bool drawn = drawRuntimeStandaloneOverlayShpSprite(dc, viewport, center, context,
            camera, cache, image, renderCell->overlayFrame, 0, 0, false, false, true);
        if (!frame.resolved && drawn && frame.draws.size() > firstDraw) {
            frame.participatingIds.insert(wall.instanceId);
            frame.occludingStructureIds.insert(wall.instanceId);
        }
    }
}
// These retail registry facts are independent of the lookup implementation:
// CAKRMW's INI key collides with LUNRK1's actual OverlayPack ordinal.
bool runRuntimeNeutralWallIdentityIssue25Contract(
    const RuntimeMapRenderContext& context, std::string& error) {
    error.clear();
    const auto fail = [&](const std::string& reason) {
        error = "issue25 neutral wall identity: " + reason;
        return false;
    };
    if (!context.rules) return fail("stock rules missing");
    const auto& overlays = context.rules->getOverlayTypes();
    if (overlays.size() <= 244) return fail("stock overlay registry too short");
    const auto& kremlin = overlays[240];
    const auto& lunarRock = overlays[244];
    if (toLowerAscii(kremlin.id) != "cakrmw" || kremlin.listIndex != 244 || !kremlin.wall
        || toLowerAscii(lunarRock.id) != "lunrk1" || lunarRock.listIndex != 248 || lunarRock.wall)
        return fail("retail CAKRMW ordinal240/key244 or LUNRK1 ordinal244/key248 facts differ");
    struct IdentityCase { int ordinal; const char* identity; bool wall; const char* label; };
    const IdentityCase cases[] = {
        {240, "CAKRMW", true, "CAKRMW-with-id"},
        {240, "", true, "CAKRMW-without-id"},
        {244, "LUNRK1", false, "LUNRK1-with-id"},
        {244, "", false, "LUNRK1-without-id"}
    };
    int checks = 0;
    for (const auto& test : cases) {
        TerrainCell cell;
        cell.hasOverlay = true;
        cell.overlayIndex = test.ordinal;
        cell.overlayType = test.identity;
        const auto* actual = runtimeTerrainDepthWallRule(cell, context);
        if (test.wall ? actual != &kremlin : actual != nullptr)
            return fail(std::string(test.label) + " classified against the wrong retail identity");
        ++checks;
    }
    if (checks != 4) return fail("distinct identity checks missing");
    std::cout << "Issue25 neutral wall identity: checks=" << checks
        << " CAKRMW=ordinal240/key244 wall=yes present/absent-id"
        << " LUNRK1=ordinal244/key248 wall=no present/absent-id passed\n";
    return true;
}