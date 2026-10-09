// These checks use the renderer's existing direct replay as the independent
// geometry reference. The paint reference switch also disables observation so
// exact destination pixels can be compared in the same process.
static bool runRuntimePaintTerrainTileMemoContractCheck(std::string& error) {
    RuntimePaintTerrainTileReferenceScope optimized(false);
    RuntimePaintGeometryReferenceScope bodies(false);
    const auto fail = [&](const char* message) {
        error = std::string("paint terrain tile memo: ") + message; return false;
    };
    GameWorld world;
    TheaterDatabase theater, otherTheater;
    RuntimeMapRenderCache cache, otherCache;
    RuntimeMapRenderContext context;
    context.stableRenderConfiguration = true; context.theater = &theater;
    context.tileRootPath = "__paint_tiles__"; context.assetRootPath = "__paint_assets__";
    RuntimeMapCamera camera;
    const RECT viewport{0, 0, 32, 32};
    const std::string resourceName = "__tile__.tem";
    const std::string resourceKey = "resource:" + normalizedLowerPath(context.assetRootPath)
        + "|" + resourceName;
    auto& mix = cache.tmpTilesByPath.try_emplace(resourceKey).first->second;
    mix.loadAttempted = mix.loaded = true; mix.errorMessage = "cached resource diagnostic";
    auto& loose = cache.tmpTilesByPath.try_emplace("__loose__.tem").first->second;
    loose.loadAttempted = loose.loaded = true;
    cache.terrainTilePathCacheRoot = runtimeTerrainTileLookupCacheRoot(context);
    cache.terrainTilePathByIndex.emplace(17, std::string(runtimeTerrainMixResourcePrefix) + resourceName);
    cache.terrainTilePathByIndex.emplace(18, "__loose__.tem");
    cache.missingTerrainTilePathByIndex.insert(19);
    {
        RuntimeGeometryQueryScope paint(world, viewport, context, camera, cache);
        if (loadRuntimeTerrainTileForIndex(17, context, cache) != &mix)
            return fail("first cached MIX resolution");
        if (loadRuntimeTerrainTileForIndex(18, context, cache) != &loose
            || loadRuntimeTerrainTileForIndex(19, context, cache) != nullptr)
            return fail("first loose or missing resolution");
        for (int index = 0; index != 512; ++index)
            cache.tmpTilesByPath.try_emplace("__insert_only_" + std::to_string(index));
        RuntimeGeometryQueryScope nested(world, viewport, context, camera, cache);
        const auto hits = cache.geometryTerrainTileMemoHits;
        cache.lastTmpTileLoadError = "other resource";
        if (loadRuntimeTerrainTileForIndex(17, context, cache) != &mix
            || cache.lastTmpTileLoadError != mix.errorMessage)
            return fail("MIX pointer or diagnostic changed after rehash");
        cache.lastTmpTileLoadError = "preserve current diagnostic";
        if (loadRuntimeTerrainTileForIndex(18, context, cache) != &loose
            || loadRuntimeTerrainTileForIndex(19, context, cache) != nullptr
            || cache.lastTmpTileLoadError != "preserve current diagnostic"
            || cache.geometryTerrainTileMemoHits != hits + 3)
            return fail("loose/missing hit changed diagnostic or was not memoized");
        RuntimeTmpTileFile* file = &mix; bool updatesError = false;
        if (!findRuntimeGeometryTerrainTile(19, context, cache, file, updatesError) || file)
            return fail("cached null was mistaken for absent resolution");
        for (int variant = 0; variant != 8; ++variant) {
            auto changed = context;
            switch (variant) {
                case 0: changed.theater = &otherTheater; break;
                case 1: changed.tileRootPath += "different"; break;
                case 2: changed.assetRootPath += "different"; break;
                case 3: changed.structureRootPath += "different"; break;
                case 4: changed.visionObserverOwner = "temporary observer"; break;
                case 5: ++changed.animationTick; break;
                case 6: changed.stableRenderConfiguration = false; break;
                case 7: changed.effectOnlyProbe = true; break;
            }
            if (findRuntimeGeometryTerrainTile(17, changed, cache, file, updatesError))
                return fail("incompatible source context borrowed TMP pointer");
        }
        if (findRuntimeGeometryTerrainTile(17, context, otherCache, file, updatesError))
            return fail("different resource cache borrowed TMP pointer");
        {
            RuntimePaintTerrainTileReferenceScope reference(true);
            if (findRuntimeGeometryTerrainTile(17, context, cache, file, updatesError))
                return fail("independent reference borrowed TMP pointer");
        }
        {
            RuntimePaintGeometryReferenceScope reference(true);
            if (findRuntimeGeometryTerrainTile(17, context, cache, file, updatesError))
                return fail("body reference borrowed TMP pointer");
        }
        auto changed = context; changed.tileRootPath += "other paint source";
        RuntimeGeometryQueryScope incompatible(world, viewport, changed, camera, cache);
        if (findRuntimeGeometryTerrainTile(17, changed, cache, file, updatesError))
            return fail("incompatible nested scope reused outer pool");
    }
    {
        RuntimeGeometryQueryScope next(world, viewport, context, camera, cache);
        RuntimeTmpTileFile* file = nullptr; bool updatesError = false;
        if (findRuntimeGeometryTerrainTile(17, context, cache, file, updatesError))
            return fail("TMP resolution survived paint boundary");
        for (int index = 0; index != 4097; ++index)
            rememberRuntimeGeometryTerrainTile(index, context, cache, nullptr, false);
        if (RuntimeGeometryQueryFrame::active->terrainTiles.size() != 4096
            || findRuntimeGeometryTerrainTile(4096, context, cache, file, updatesError))
            return fail("derived TMP memo exceeded capacity or evicted old entries");
    }
    return true;
}

static bool runRuntimePaintGeometryReuseContractCheck(std::string& error) {
    if (!runRuntimePaintTerrainTileMemoContractCheck(error)) return false;
    RuntimePaintGeometryReferenceScope optimized(false);
    const auto fail = [&](const char* message) {
        error = std::string("paint geometry reuse: ") + message; return false;
    };
    GameWorld world;
    world.accessObjects().resize(2);
    auto& object = world.accessObjects()[0];
    object.instanceId = 7; object.type = "__PAINT__";
    RuntimeMapRenderContext context;
    context.stableRenderConfiguration = true; context.animationTick = 42;
    RuntimeMapCamera camera;
    RuntimeMapRenderCache cache;
    const RECT viewport{1, 2, 641, 402}, expected{30, 40, 60, 80};
    const POINT center{50, 65};
    {
        RuntimeGeometryQueryScope paint(world, viewport, context, camera, cache);
        const auto* outer = RuntimeGeometryQueryFrame::active;
        if (runtimeGeometryBatchRenderContext() != &outer->identity) return fail("active context getter");
        {
            RuntimeGeometryQueryScope map(world, viewport, context, camera, cache);
            auto* nested = RuntimeGeometryQueryFrame::active;
            if (nested == outer || nested->memoOwner != outer
                || !runtimeGeometryPaintRecordingEnabled(object, context, camera, cache))
                return fail("compatible map did not share paint lifetime");
            rememberRuntimeGeometryMobileBounds(object, center, context, camera, cache, expected, true, true);
        }
        auto interaction = *runtimeGeometryBatchRenderContext();
        interaction.surfaceWidth = 999; interaction.surfaceHeight = 888;
        interaction.chaosBodyTint = true; interaction.airstrikeBodyColorAdd = 123;
        interaction.detailStatsEnabled = true; interaction.coarseStageTimingEnabled = true;
        {
            RuntimeGeometryQueryScope overlay(world, viewport, interaction, camera, cache, true);
            RECT bounds{}; bool resolved = false;
            if (!findRuntimeGeometryMobileBounds(object, center, interaction, camera, cache, bounds, resolved)
                || !resolved || !EqualRect(&bounds, &expected)) return fail("map-to-overlay bounds differ");
            auto copiedObserver = object;
            copiedObserver.owner = "Disguise"; copiedObserver.type = "OtherBody";
            if (findRuntimeGeometryMobileBounds(copiedObserver, center, interaction, camera, cache, bounds, resolved))
                return fail("temporary observer identity borrowed original body");
            auto differentCenter = center; ++differentCenter.y;
            if (findRuntimeGeometryMobileBounds(object, differentCenter, interaction, camera, cache, bounds, resolved))
                return fail("changed draw center matched");
            auto changedCamera = camera; ++changedCamera.tileHeight;
            if (runtimeGeometryBodyMemo(object, interaction, changedCamera, cache)) return fail("changed camera matched");
            RuntimePaintGeometryReferenceScope direct(true);
            if (runtimeGeometryBodyMemo(object, interaction, camera, cache)) return fail("reference borrowed body");
        }
        for (int variant = 0; variant != 10; ++variant) {
            auto changed = context;
            switch (variant) {
                case 0: ++changed.animationTick; break;
                case 1: ++changed.fixedTickHz; break;
                case 2: --changed.visualDetail; break;
                case 3: changed.seeHiddenObjects = false; break;
                case 4: changed.visionObserverOwner = "different owner"; break;
                case 5: changed.assetRootPath = "different root"; break;
                case 6: changed.structurePalettePath = "different palette"; break;
                case 7: changed.shadowsEnabled = false; break;
                case 8: changed.stableRenderConfiguration = false; break;
                case 9: changed.effectOnlyProbe = true; break;
            }
            RuntimeGeometryQueryScope mismatch(world, viewport, changed, camera, cache);
            if (RuntimeGeometryQueryFrame::active->memoOwner == outer)
                return fail("changed visual dependency shared outer body");
        }
    }
    object.facing = 128; object.turretFacing = 255; object.health = 3;
    object.mission = "Move"; object.movementAnimationDistance = 12.5;
    {
        RuntimeGeometryQueryScope next(world, viewport, context, camera, cache);
        RECT bounds{}; bool resolved = false;
        if (findRuntimeGeometryMobileBounds(object, center, context, camera, cache, bounds, resolved))
            return fail("body survived paint or simulation boundary");
        if (runtimeGeometryPaintRecordingEnabled(object, context, camera, cache))
            return fail("standalone batch added unused paint observation");
    }
    return true;
}

static bool runRuntimePaintGeometryReuseAssetContractCheck(const RuntimeMapRenderContext& source,
    std::vector<WorldObjectInstance> probes, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "paint geometry reuse assets: " + message; return false;
    };
    const auto sameSpans = [](const auto& a, const auto& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t index = 0; index < a.size(); ++index)
            if (a[index].y != b[index].y || a[index].left != b[index].left || a[index].right != b[index].right)
                return false;
        return true;
    };
    IniFile ini;
    MapDatabase map;
    GameWorld world;
    if (!ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\nLocalSize=0,0,32,32\n")
        || !map.loadFromIni(ini) || !world.loadFromMap(map, source.rules)) return fail("fixture map");
    world.accessObjects() = std::move(probes);
    for (std::size_t index = 0; index < world.accessObjects().size(); ++index) {
        auto& object = world.accessObjects()[index];
        object.x = 3 + static_cast<int>(index % 6) * 2;
        object.y = 3 + static_cast<int>(index / 6) * 2;
        object.renderX = object.x; object.renderY = object.y;
        object.factoryExitHostId = -1;
    }
    RuntimeMapRenderContext context = source;
    context.vision = nullptr; context.visionObserverOwner.clear(); context.seeHiddenObjects = true;
    context.stableRenderConfiguration = true;
    context.specialMechanismSnapshots = nullptr; context.gateSnapshots = nullptr;
    context.projectileActors = nullptr; context.impactActors = nullptr; context.wrecks = nullptr;
    context.buildingCollapses = nullptr; context.particleSnapshots = nullptr;
    context.particleBeamSnapshots = nullptr; context.localLightSnapshots = nullptr;
    context.selectedObjectIds = nullptr; context.playerCheerUntilTick = nullptr;
    context.uiResources = nullptr; context.lightingTint = nullptr; context.lightingProfile = nullptr;
    context.detailStatsEnabled = false; context.coarseStageTimingEnabled = false;
    constexpr int width = 800, height = 600;
    const RECT viewport{0, 0, width, height};
    RuntimeMapCamera camera;
    camera.initialized = true; camera.worldOffsetX = -width / 2; camera.worldOffsetY = -80;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* rawPixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (!dc || !bitmap || !rawPixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return fail("pixel surface allocation");
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    struct SurfaceCleanup {
        HDC dc; HBITMAP bitmap; HGDIOBJ old;
        ~SurfaceCleanup() { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); }
    } cleanup{dc, bitmap, oldBitmap};
    auto* pixels = static_cast<std::uint32_t*>(rawPixels);
    constexpr std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    context.surfacePixels = pixels; context.surfaceWidth = width; context.surfaceHeight = height;
    RuntimeMapRenderCache optimized, reference;
    std::vector<std::uint32_t> expectedPixels;
    std::size_t checkedBodies = 0;
    for (int frame = 0; frame != 4; ++frame) {
        for (auto& object : world.accessObjects()) {
            object.facing = (object.facing + 17) & 255;
            object.turretFacing = (object.turretFacing + 29) & 255;
            object.movementAnimationDistance += 0.125;
            if (frame == 2 && object.kind == WorldObjectKind::Infantry) object.cloaked = true;
        }
        ++context.animationTick;
        for (bool fast : {false, true}) {
            RuntimePaintGeometryReferenceScope mode(!fast);
            RuntimeMapRenderCache& cache = fast ? optimized : reference;
            RuntimeGeometryQueryScope paint(world, viewport, context, camera, cache);
            std::fill_n(pixels, pixelCount, 0x00407199U);
            auto drawCamera = camera;
            drawRuntimeMap(dc, viewport, world, context, drawCamera, cache);
            GdiFlush();
            if (!fast) expectedPixels.assign(pixels, pixels + pixelCount);
            else if (!std::equal(expectedPixels.begin(), expectedPixels.end(), pixels))
                return fail("observation changed destination pixels on frame " + std::to_string(frame));
            if (!fast) continue;
            RuntimeGeometryQueryScope overlay(world, viewport, context, camera, cache, true);
            for (const auto& object : world.getObjects()) {
                const auto placement = runtimeObjectWorldRenderPlacement(object, world, viewport, context, camera, cache);
                if (object.kind == WorldObjectKind::Structure) {
                    RuntimeStructureSpriteGeometry actual, expected;
                    const bool found = resolveRuntimeStructureSpriteGeometry(object, placement.center, context, camera, cache, actual);
                    bool direct = false;
                    {
                        RuntimePaintGeometryReferenceScope replay(true);
                        direct = resolveRuntimeStructureSpriteGeometry(object, placement.center, context, camera, cache, expected);
                    }
                    if (found != direct || !EqualRect(&actual.bounds, &expected.bounds)
                        || actual.layerCount != expected.layerCount || !sameSpans(actual.hitSpans, expected.hitSpans)
                        || !sameSpans(actual.foregroundHitSpans, expected.foregroundHitSpans))
                        return fail("structure body/spans differ: " + object.type);
                } else {
                    RECT actual{}, expected{};
                    const bool found = resolveRuntimeMobileObjectSpriteBounds(object, placement.center, context, camera, cache, actual);
                    bool direct = false;
                    {
                        RuntimePaintGeometryReferenceScope replay(true);
                        direct = resolveRuntimeMobileObjectSpriteBounds(object, placement.center, context, camera, cache, expected);
                    }
                    if (found != direct || !EqualRect(&actual, &expected)) return fail("mobile body differs: " + object.type);
                    // A local observer projection must replay its changed identity
                    // even when the original object has already populated the pool.
                    auto observer = object; observer.type = "E1"; observer.kind = WorldObjectKind::Infantry;
                    observer.owner = "Russians"; observer.airborne = false; observer.submerged = false; observer.cloaked = false;
                    const auto hits = cache.geometryMobileBoundsMemoHits;
                    resolveRuntimeMobileObjectSpriteBounds(observer, placement.center, context, camera, cache, actual);
                    {
                        RuntimePaintGeometryReferenceScope replay(true);
                        resolveRuntimeMobileObjectSpriteBounds(observer, placement.center, context, camera, cache, expected);
                    }
                    if (cache.geometryMobileBoundsMemoHits != hits || !EqualRect(&actual, &expected))
                        return fail("temporary observer did not retain exact fallback");
                }
                ++checkedBodies;
            }
        }
    }
    if (!checkedBodies || !optimized.geometryMobileBoundsPaintRecords || !optimized.geometryMobileBoundsMemoHits)
        return fail("paint-to-interaction path was not exercised");
    if (!optimized.geometryStructureMemoHits) return fail("structure replay reuse was not exercised");
    return true;
}
