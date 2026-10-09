// Revealed opaque terrain must not acquire a permanent black band where height
// projection leaves holes between top-surface diamonds. Use original TMP pixels
// as the visible-color oracle, independently of the shroud projection grid.
bool runRuntimeNativeCliffShroudContract(const RuntimeMapRenderContext& assets, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Native cliff shroud: " + why; return false; };
    const std::filesystem::path root(assets.assetRootPath);
    IniFile mapIni, theaterIni;
    MapDatabase map; GameWorld world; TheaterDatabase theater;
    const auto theaterPath = root / "ra2md/localmd/temperatmd.ini";
    if (!mapIni.loadFromFile((root / "multimd/2peaks.map").string())
        || !map.loadFromIni(mapIni) || !world.loadFromMap(map)
        || !theaterIni.loadFromFile(theaterPath.string())
        || !theater.loadFromIni(theaterIni, theaterPath.string())) return fail("native map/theater assets");
    RuntimeMapRenderContext context = assets;
    context.theater = &theater; context.palettePath = (root / "palettes/isotem.pal").string();
    context.useTileTextures = true;
    RuntimeMapRenderCache selectionCache;
    std::unordered_set<int> validCells;
    for (const auto& cell : world.getTerrainCells()) validCells.insert(cell.y * 512 + cell.x);
    std::vector<TerrainCell> cliffs;
    for (const auto& cell : world.getTerrainCells()) {
        const auto reference = theater.resolveTileIndex(cell.tileIndex);
        if (!containsText(reference.tileSetName, "Cliff") && !containsText(reference.tileSetName, "cliff")) continue;
        const auto* image = resolveTerrainCellImage(cell, context, selectionCache);
        if (!image || image->extraWidth <= 0 || image->extraHeight <= 0) continue;
        bool interior = true;
        for (int dy = -12; dy <= 12 && interior; ++dy) for (int dx = -12; dx <= 12; ++dx)
            if (!validCells.contains((cell.y + dy) * 512 + cell.x + dx)) { interior = false; break; }
        if (!interior || std::any_of(cliffs.begin(), cliffs.end(), [&](const auto& other) {
                return std::abs(cell.x - other.x) + std::abs(cell.y - other.y) < 16;
            })) continue;
        cliffs.push_back(cell);
        if (cliffs.size() == 3) break;
    }
    if (cliffs.size() < 3) return fail("three interior native cliff faces were not found");
    RuntimeVisionSystem vision;
    vision.update(world, nullptr, {{"Observer", {"Observer"}}}, 1);
    context.vision = &vision; context.visionObserverOwner = "Observer";
    constexpr int width = 720, height = 480;
    context.surfaceWidth = width; context.surfaceHeight = height;
    const RECT viewport{3,5,width-3,height-5};
    const char* output = std::getenv("CONCORD_NATIVE_SHROUD_OUTPUT");
    const std::filesystem::path outputRoot = output && *output ? output : "build/native-shroud-smoke";
    std::uint64_t revealedPixels = 0, hiddenPixels = 0;
    int cases = 0;
    for (std::size_t scene = 0; scene < cliffs.size(); ++scene)
        for (int zoom : {48,60,72}) for (int pan : {0,1}) {
        const auto& cliff = cliffs[scene];
        const std::string label = "cliff-" + std::to_string(scene) + "-z" + std::to_string(zoom) + "-p" + std::to_string(pan);
        RuntimeMapCamera camera; camera.initialized = true;
        camera.tileWidth = zoom; camera.tileHeight = zoom / 2;
        const auto center = projectCell(cliff,camera);
        camera.worldOffsetX = center.x - width / 2 + pan * 11;
        camera.worldOffsetY = center.y - height / 2 + pan * 7;
        RuntimeMapRenderCache cache;
        if (!buildTerrainLayer(world,context,camera,width,height,cache)
            || cache.terrainLayer.texturedTerrainCells == 0) return fail(label + " lacks native terrain");
        std::vector<std::uint32_t> base(width * height,0), actual, cold;
        context.surfacePixels = base.data();
        if (!drawTerrainLayerToViewport(nullptr,viewport,context,camera,cache.terrainLayer)) return fail(label + " terrain draw");
        const auto paint = [&](std::vector<std::uint32_t>& pixels, RuntimeMapRenderCache& renderCache) {
            pixels = base; context.surfacePixels = pixels.data(); RuntimeMapRenderStats stats;
            applyRuntimeVisionOverlay(nullptr,viewport,world,context,camera,renderCache,stats);
            return renderCache.visionOverlay.nativeShroudFingerprint != 0;
        };
        const auto knownPixelsUnchanged = [&]() {
            std::size_t wrong = 0;
            for (int y = 64; y < height - 64; ++y) for (int x = 64; x < width - 64; ++x) {
                const int owner = runtimeVisionTileOwnerAt(cache.terrainLayer,viewport,camera,x,y);
                if (owner < 0 || !validCells.contains(owner)) continue;
                ++revealedPixels;
                const auto pixel = static_cast<std::size_t>(y) * width + x;
                if ((actual[pixel] & 0xffffffU) != (base[pixel] & 0xffffffU)) ++wrong;
            }
            if (wrong) {
                writeRuntimeBgraBmp(outputRoot / (label + "-terrain.bmp"),width,height,base);
                writeRuntimeBgraBmp(outputRoot / (label + "-revealed.bmp"),width,height,actual);
                error = "Native cliff shroud: " + label + " still shades " + std::to_string(wrong)
                    + " explored terrain pixels at " + std::to_string(cliff.x) + "," + std::to_string(cliff.y);
            }
            return wrong == 0;
        };
        vision.revealMap("Observer");
        if (!paint(actual,cache) || !knownPixelsUnchanged()) return false;
        if (pan == 0) writeRuntimeBgraBmp(outputRoot / (label + "-revealed.bmp"),width,height,actual);
        vision.resetShroudArea("Observer",cliff.x,cliff.y,3);
        if (!paint(actual,cache)) return fail(label + " reset draw");
        RuntimeVisionGridView view; vision.gridViewFor("Observer",view);
        std::uint64_t sceneHidden = 0;
        for (int y = viewport.top; y < viewport.bottom; ++y) for (int x = viewport.left; x < viewport.right; ++x) {
            const int owner = runtimeVisionTileOwnerAt(cache.terrainLayer,viewport,camera,x,y);
            if (owner < 0 || view.visibilityAtIndex(owner) != RuntimeCellVisibility::Shrouded) continue;
            if ((actual[static_cast<std::size_t>(y) * width + x] & 0xffffffU) != 0)
                return fail(label + " reveals unexplored cliff pixels");
            ++sceneHidden;
        }
        if (!sceneHidden) return fail(label + " has no concealed cliff witness");
        hiddenPixels += sceneHidden;
        RuntimeMapRenderCache reference; reference.terrainLayer = cache.terrainLayer;
        if (!paint(cold,reference) || cold != actual) return fail(label + " reset cache differs from cold paint");
        if (pan == 0) writeRuntimeBgraBmp(outputRoot / (label + "-reshrouded.bmp"),width,height,actual);
        vision.revealMap("Observer");
        if (!paint(actual,cache) || !knownPixelsUnchanged()) return false;
        ++cases;
    }
    std::cout << "Native cliff reveal: cases=" << cases << " revealedPixels=" << revealedPixels
        << " concealedPixels=" << hiddenPixels << " originalTMP=yes reveal/reset/cold/pan/zoom=exact\n";
    return true;
}
