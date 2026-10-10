// Compare the new derived geometry/run caches with the preceding native
// renderer in one process. Timings are deliberately not used as assertions.
bool runRuntimeShroudPerformanceContractCheck(const RuntimeMapRenderContext& assets,
    std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "Shroud performance contract: " + why; return false;
    };
    std::uint32_t randomState = 0xd94b61a7U;
    const auto random = [&]() {
        randomState ^= randomState << 13; randomState ^= randomState >> 17;
        randomState ^= randomState << 5; return randomState;
    };
    std::size_t runCases = 0, retainedRunPixels = 0;
    for (int pattern = 0; pattern < 256; ++pattern) {
        RuntimeVisionOverlayTile tile;
        std::vector<std::uint32_t> expected(4224), actual(expected.size());
        for (auto& pixel : expected) pixel = random();
        actual = expected;
        for (std::uint32_t i = 0; i < 4096U; ++i) {
            if ((i + pattern) % 71U == 0U) continue;
            const std::uint32_t alpha = i % 64U < 48U ? 255U
                : (i + static_cast<std::uint32_t>(pattern)) & 255U;
            // Includes opaque non-black fills, fractional RGB blends, alpha
            // bytes in source pixels, discontinuous offsets and row boundaries.
            const std::uint32_t tint = (i / 64U) % 3U == 0U ? 0U
                : (i / 64U) * 0x03170bU & 0x00ffffffU;
            tile.encodedPixels.push_back((static_cast<std::uint64_t>(i + 64U) << 32U)
                | (alpha << 24U) | tint);
        }
        runtimeBuildVisionOpaqueRuns(tile);
        RuntimeMapRenderStats stats;
        runtimeApplyVisionEncodedPixels(tile.encodedPixels, expected.data());
        runtimeApplyVisionTilePixels(tile, actual.data(), stats, false);
        if (expected != actual) return fail("opaque-run pixels differ");
        if (tile.opaqueRuns.size() > tile.encodedPixels.size() / 8U
            || tile.opaqueRuns.capacity() > 512U)
            return fail("opaque-run cache exceeded its tile bound");
        retainedRunPixels += stats.visionOpaqueRunPixels;
        ++runCases;
        // Rebuild a heavily explored tile: old spans must not survive a shorter
        // source list or apply to untouched pixels outside the remaining list.
        tile.encodedPixels.resize(static_cast<std::size_t>(pattern));
        runtimeBuildVisionOpaqueRuns(tile);
        for (auto& pixel : expected) pixel = random();
        actual = expected;
        runtimeApplyVisionEncodedPixels(tile.encodedPixels, expected.data());
        runtimeApplyVisionTilePixels(tile, actual.data(), stats, false);
        if (expected != actual) return fail("shortened tile retained stale opaque runs");
        ++runCases;
    }
    if (retainedRunPixels == 0) return fail("opaque-run fast path was not exercised");

    GameWorld world;
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.level = (x * 7 + y * 13) % 9;
        world.accessTerrainCells().push_back(cell);
    }
    RuntimeVisionSystem vision;
    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"Observer", {"Observer"}}, {"Enemy", {"Enemy"}}};
    vision.update(world, nullptr, observers, 1);
    vision.exploreArea("Observer", 64, 64, 12);
    RuntimeMapRenderContext context = assets;
    context.vision = &vision; context.visionObserverOwner = "Observer";
    context.surfaceWidth = 512; context.surfaceHeight = 384;
    RuntimeMapCamera camera; camera.initialized = true;
    camera.tileWidth = 60; camera.tileHeight = 30;
    camera.worldOffsetX = -256; camera.worldOffsetY = 1728;
    RECT viewport{3, 5, 509, 381};
    RuntimeMapRenderCache optimized, reference;
    auto& layer = optimized.terrainLayer;
    layer.built = true; layer.key = "shroud-performance-TMP";
    layer.width = 900; layer.height = 700;
    layer.originX = -420; layer.originY = 1600;
    layer.pixels.assign(900 * 700, 0x00ffffffU);
    layer.terrainCellIndices.assign(900 * 700, -1);
    for (int y = 0; y < layer.height; ++y) for (int x = 0; x < layer.width; ++x)
        if ((x / 23 + y / 19) % 5 == 0)
            layer.terrainCellIndices[y * layer.width + x] = (58 + y / 35) * 512 + 58 + x / 51;
    reference.terrainLayer = layer;
    std::size_t paints = 0, geometryHits = 0, geometryMisses = 0, boundedViews = 0;
    std::size_t runBytesPeak = 0;
    const auto check = [&](const char* label, RuntimeMapRenderCache& aCache,
        RuntimeMapRenderCache& bCache) {
        const std::size_t count = static_cast<std::size_t>(context.surfaceWidth) * context.surfaceHeight;
        std::vector<std::uint32_t> a(count), b(count);
        for (std::size_t i = 0; i < count; ++i) a[i] = b[i] = random();
        RuntimeMapRenderStats aStats, bStats;
        {
            RuntimeShroudPerformanceReferenceScope fast(false);
            context.surfacePixels = a.data();
            applyRuntimeVisionOverlay(nullptr, viewport, world, context, camera, aCache, aStats);
        }
        {
            RuntimeShroudPerformanceReferenceScope old(true);
            context.surfacePixels = b.data();
            applyRuntimeVisionOverlay(nullptr, viewport, world, context, camera, bCache, bStats);
        }
        if (a != b || aStats.visionVisibleCells != bStats.visionVisibleCells
            || aStats.visionFoggedCells != bStats.visionFoggedCells
            || aStats.visionShroudedCells != bStats.visionShroudedCells
            || aStats.visionOverlayPixels != bStats.visionOverlayPixels) {
            const auto mismatch = std::mismatch(a.begin(), a.end(), b.begin());
            error = std::string("Shroud performance contract: ") + label
                + " pixel=" + std::to_string(mismatch.first - a.begin()); return false;
        }
        if (aCache.visionOverlay.shroudedCellMask != bCache.visionOverlay.shroudedCellMask
            || aCache.visionOverlay.incremental.mask != bCache.visionOverlay.incremental.mask
            || aCache.visionOverlay.incremental.tiles.size() != bCache.visionOverlay.incremental.tiles.size())
            return fail(std::string(label) + " whole shroud mask differs");
        for (std::size_t i = 0; i < aCache.visionOverlay.incremental.tiles.size(); ++i)
            if (aCache.visionOverlay.incremental.tiles[i].encodedPixels
                != bCache.visionOverlay.incremental.tiles[i].encodedPixels)
                return fail(std::string(label) + " encoded shroud pixel sequence differs");
        geometryHits += aStats.visionGeometryCacheHits;
        geometryMisses += aStats.visionGeometryCacheMisses;
        runBytesPeak = std::max(runBytesPeak, aStats.visionOpaqueRunCacheBytes);
        if (aCache.visionOverlay.incremental.nativeSources.size()
            < bCache.visionOverlay.incremental.nativeSources.size()) ++boundedViews;
        ++paints;
        return true;
    };
    if (!check("cold", optimized, reference) || !check("warm", optimized, reference)) return false;
    if (!optimized.nativeShroudAtlas.loaded) return fail("native atlas missing");
    for (int tick = 2; tick <= 20; ++tick) {
        vision.exploreArea("Observer", 55 + tick % 16, 60 + tick % 9, 2);
        if (tick % 3 == 0) vision.resetShroudArea("Observer", 62, 65, 3);
        if (!check("exploration/reset", optimized, reference)) return false;
        camera.worldOffsetX += tick % 2 ? 7 : -11;
        camera.worldOffsetY += tick % 2 ? -9 : 5;
        if (!check("pan", optimized, reference)) return false;
    }
    RuntimeMapRenderCache copiedOptimized, copiedReference;
    copiedOptimized.terrainLayer = optimized.terrainLayer;
    copiedOptimized.visionOverlay = optimized.visionOverlay;
    copiedReference.terrainLayer = reference.terrainLayer;
    copiedReference.visionOverlay = reference.visionOverlay;
    const auto keptMask = optimized.visionOverlay.incremental.mask;
    vision.resetShroudArea("Observer", 70, 70, 4);
    if (!check("copied", copiedOptimized, copiedReference)
        || optimized.visionOverlay.incremental.mask != keptMask)
        return fail("copied cache shares mutable mask data");
    if (!check("original after copy", optimized, reference)) return false;
    context.visionObserverOwner = "Enemy";
    if (!check("observer switch", optimized, reference)) return false;
    context.visionObserverOwner = "Observer";
    for (const auto [w, h] : std::array<std::pair<int, int>, 5>{{
            {15, 7}, {48, 24}, {60, 30}, {72, 36}, {18, 9}}}) {
        camera.tileWidth = w; camera.tileHeight = h;
        camera.worldOffsetX = -210; camera.worldOffsetY = 64 * h - 150;
        context.surfaceWidth = 533; context.surfaceHeight = 401;
        viewport = RECT{-7, -3, 520, 395};
        if (!check("odd/even scale and clipping", optimized, reference)) return false;
    }
    for (const auto [x, y] : std::array<std::pair<int, int>, 6>{{
            {0, 0}, {0, 127}, {127, 0}, {127, 127}, {-12, -12}, {140, 140}}}) {
        camera.tileWidth = 60; camera.tileHeight = 30;
        camera.worldOffsetX = (x - y) * 30 - 255;
        camera.worldOffsetY = (x + y) * 15 - 190;
        if (!check("map edges and outside map", optimized, reference)) return false;
    }
    world.accessTerrainCells()[64 * 128 + 65].level += 3;
    optimized.terrainLayer.key += "-geometry-edit";
    reference.terrainLayer.key = optimized.terrainLayer.key;
    if (!check("in-place terrain level edit", optimized, reference)) return false;
    world.accessTerrainCells().push_back(world.accessTerrainCells()[20 * 128 + 20]);
    optimized.terrainLayer.key += "-duplicate-cell";
    reference.terrainLayer.key = optimized.terrainLayer.key;
    if (!check("terrain storage/duplicate cell", optimized, reference)) return false;
    viewport = RECT{80, 20, 81, 25};
    if (!check("one-pixel viewport", optimized, reference)) return false;
    optimized.terrainLayer.built = reference.terrainLayer.built = false;
    viewport = RECT{0, 0, 533, 401};
    if (!check("ownership removed", optimized, reference)) return false;
    vision.reset(); vision.update(world, nullptr, observers, 30);
    if (!check("vision storage replaced", optimized, reference)) return false;
    if (geometryHits == 0 || geometryMisses < 3 || boundedViews == 0)
        return fail("geometry reuse/invalidation/bounded viewport not exercised");
    std::cout << "Shroud performance contract: runCases=" << runCases << " paints=" << paints
        << " geometryHits=" << geometryHits << " geometryMisses=" << geometryMisses
        << " boundedViews=" << boundedViews << " runBytesPeak=" << runBytesPeak
        << " pixelsAndCounts=exact copiedCache=independent\n";
    return true;
}
