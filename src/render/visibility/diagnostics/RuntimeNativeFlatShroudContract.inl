// On level ground, TMP ownership must not change the retail edge chosen from
// explored cells. Compare actual native terrain against the same archive mask
// on the geometric grid; the archive's opacity is independently checked above.
bool runRuntimeNativeFlatShroudContract(const RuntimeMapRenderContext& assets, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Native flat shroud: " + why; return false; };
    const std::filesystem::path root(assets.assetRootPath);
    IniFile theaterIni; TheaterDatabase theater;
    const auto theaterPath = root / "ra2md/localmd/temperatmd.ini";
    if (!theaterIni.loadFromFile(theaterPath.string())
        || !theater.loadFromIni(theaterIni, theaterPath.string())) return fail("native theater");
    GameWorld world;
    for (int y = 1; y <= 48; ++y) for (int x = 1; x <= 48; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = cell.subTileIndex = cell.level = 0; world.accessTerrainCells().push_back(cell);
    }
    RuntimeVisionSystem vision; vision.update(world, nullptr, {{"Observer", {"Observer"}}}, 1);
    RuntimeMapRenderContext context = assets;
    context.theater = &theater; context.palettePath = (root / "palettes/isotem.pal").string();
    context.useTileTextures = true; context.vision = &vision; context.visionObserverOwner = "Observer";
    constexpr int width = 720, height = 480; const RECT viewport{0, 0, width, height};
    context.surfaceWidth = width; context.surfaceHeight = height;
    const char* output = std::getenv("CONCORD_NATIVE_SHROUD_OUTPUT");
    const std::filesystem::path outputRoot = output && *output ? output : "build/native-shroud-smoke";
    std::filesystem::create_directories(outputRoot);
    std::uint64_t examined = 0, softened = 0; int cases = 0;
    for (int direction = 0; direction < 4; ++direction) for (int zoom : {48, 60, 72}) for (int pan : {0, 1}) {
        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = zoom; camera.tileHeight = zoom / 2;
        TerrainCell center; center.x = center.y = 24;
        const auto projected = projectCell(center, camera);
        camera.worldOffsetX = projected.x - width / 2 + pan * 11;
        camera.worldOffsetY = projected.y - height / 2 + pan * 7;
        RuntimeMapRenderCache actualCache;
        if (!buildTerrainLayer(world, context, camera, width, height, actualCache)
            || !runtimeVisionTileHasOwnership(actualCache.terrainLayer)) return fail("native flat TMP ownership");
        std::vector<std::uint32_t> base(width * height), actual, expected;
        context.surfacePixels = base.data();
        if (!drawTerrainLayerToViewport(nullptr, viewport, context, camera, actualCache.terrainLayer))
            return fail("flat terrain draw");
        std::vector<int> explored;
        for (const auto& cell : world.getTerrainCells()) {
            const bool known = direction == 0 ? cell.x + cell.y <= 48
                : direction == 1 ? cell.x + cell.y >= 48
                : direction == 2 ? cell.x <= cell.y : cell.x <= 24;
            if (known) explored.push_back(cell.cellIndex512);
        }
        vision.resetShroud("Observer"); vision.restoreExploredCellIndices("Observer", explored);
        RuntimeMapRenderCache reference; // No ownership supplementation; exact flat cell geometry.
        const auto paint = [&](std::vector<std::uint32_t>& pixels, RuntimeMapRenderCache& cache) {
            pixels = base; context.surfacePixels = pixels.data(); RuntimeMapRenderStats stats;
            applyRuntimeVisionOverlay(nullptr, viewport, world, context, camera, cache, stats);
            return cache.visionOverlay.nativeShroudFingerprint != 0;
        };
        if (!paint(actual, actualCache) || !paint(expected, reference)) return fail("native atlas unavailable");
        RuntimeVisionGridView view; vision.gridViewFor("Observer", view);
        std::size_t wrong = 0, sceneSoft = 0;
        for (int y = 64; y < height - 64; ++y) for (int x = 64; x < width - 64; ++x) {
            const int owner = runtimeVisionTileOwnerAt(actualCache.terrainLayer, viewport, camera, x, y);
            if (owner < 0 || view.visibilityAtIndex(owner) == RuntimeCellVisibility::Shrouded) continue;
            const auto p = static_cast<std::size_t>(y) * width + x; ++examined;
            if ((actual[p] & 0xffffffU) != (expected[p] & 0xffffffU)) ++wrong;
            if ((expected[p] & 0xffffffU) && expected[p] != base[p]) ++sceneSoft;
        }
        const std::string label = "flat-edge-" + std::to_string(direction) + "-z" + std::to_string(zoom) + "-p" + std::to_string(pan);
        if (wrong || pan == 0) {
            writeRuntimeBgraBmp(outputRoot / (label + "-actual.bmp"), width, height, actual);
            writeRuntimeBgraBmp(outputRoot / (label + "-retail.bmp"), width, height, expected);
        }
        if (wrong) return fail(label + " changed " + std::to_string(wrong) + " explored edge pixels");
        if (sceneSoft < 100) return fail(label + " missing native gradient witness");
        softened += sceneSoft; ++cases;
    }
    std::cout << "Native flat edges: cases=" << cases << " exploredPixels=" << examined
        << " nativeGradientPixels=" << softened << " TMP/geometric masks=exact directions=4 zooms=3 pans=2\n";
    return true;
}
