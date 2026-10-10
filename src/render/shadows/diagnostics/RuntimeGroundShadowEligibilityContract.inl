// Complete production drawRuntimeMap oracle. The reference retains the original
// visibility-before-dead-filter order, not a second copy of the optimized loop.
static bool runtimeGroundShadowContractStatsEqual(const RuntimeMapRenderStats& a,
    const RuntimeMapRenderStats& b, std::string& field);

template<class Map>
static bool runtimeGroundShadowContractKeysEqual(const Map& a, const Map& b) {
    if (a.size() != b.size()) return false;
    for (const auto& item : a) if (b.find(item.first) == b.end()) return false;
    return true;
}

static bool runtimeGroundShadowContractImageEqual(const RuntimeShpFrameImage& a,
    const RuntimeShpFrameImage& b) {
    // Separate render caches own different palette allocations. Only the raw
    // address is normalized; table contents, key inputs and validity still match.
    auto left = a, right = b;
    left.packedColorPaletteSource = right.packedColorPaletteSource = nullptr;
    return runtimeVoxelFaceContractImageEqual(left, right);
}

static bool runtimeGroundShadowContractCacheEqual(const RuntimeMapRenderCache& a,
    const RuntimeMapRenderCache& b) {
    if (a.loadedPalettePath != b.loadedPalettePath || a.paletteLoaded != b.paletteLoaded
        || a.paletteLoadAttempted != b.paletteLoadAttempted
        || a.loadedStructurePalettePath != b.loadedStructurePalettePath
        || a.structurePaletteLoaded != b.structurePaletteLoaded
        || a.structurePaletteLoadAttempted != b.structurePaletteLoadAttempted
        || a.voxelPaletteLookupLoadAttempted != b.voxelPaletteLookupLoadAttempted
        || a.voxelPaletteLookupLoaded != b.voxelPaletteLookupLoaded
        || a.voxelPaletteLookup != b.voxelPaletteLookup
        || a.voxelPaletteLookupSourcePath != b.voxelPaletteLookupSourcePath
        || a.voxelPaletteLookupErrorMessage != b.voxelPaletteLookupErrorMessage
        || a.lastTmpTileLoadError != b.lastTmpTileLoadError
        || a.lastColdResourceSequence != b.lastColdResourceSequence
        || a.lastColdResourceType != b.lastColdResourceType
        || a.lastColdResourceName != b.lastColdResourceName
        || a.stableVoxelCacheKeyByLookup != b.stableVoxelCacheKeyByLookup
        || a.terrainTilePathByIndex != b.terrainTilePathByIndex
        || a.missingTerrainTilePathByIndex != b.missingTerrainTilePathByIndex
        || !runtimeGroundShadowContractKeysEqual(a.tmpTilesByPath, b.tmpTilesByPath)
        || !runtimeGroundShadowContractKeysEqual(a.namedPalettesByKey, b.namedPalettesByKey)
        || !runtimeGroundShadowContractKeysEqual(a.voxelFixedScaleByKey, b.voxelFixedScaleByKey)
        || !runtimeGroundShadowContractKeysEqual(a.voxelSourcesByImage, b.voxelSourcesByImage)
        || !runtimeGroundShadowContractKeysEqual(a.voxelObjectsByType, b.voxelObjectsByType)
        || !runtimeGroundShadowContractKeysEqual(a.objectShpBytesByImage, b.objectShpBytesByImage)
        || !runtimeGroundShadowContractKeysEqual(a.auxiliaryShpBytesByImage, b.auxiliaryShpBytesByImage)
        || !runtimeGroundShadowContractKeysEqual(a.objectShpSpritesByType, b.objectShpSpritesByType)
        || !runtimeGroundShadowContractKeysEqual(a.auxiliaryShpSpritesByImage, b.auxiliaryShpSpritesByImage)) return false;
    const auto palettesEqual = [](const auto& x, const auto& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (x[i].r != y[i].r || x[i].g != y[i].g || x[i].b != y[i].b) return false;
        return true;
    };
    if (!palettesEqual(a.palette, b.palette) || !palettesEqual(a.structurePalette, b.structurePalette)
        || !palettesEqual(a.voxelPalette, b.voxelPalette)) return false;
    const auto paletteEntryEqual = [&](const auto& x, const auto& y) {
        return x.key == y.key && x.loadAttempted == y.loadAttempted && x.loaded == y.loaded
            && x.sourcePath == y.sourcePath && palettesEqual(x.palette, y.palette);
    };
    if (!paletteEntryEqual(a.animPalette, b.animPalette) || !paletteEntryEqual(a.unitPalette, b.unitPalette)) return false;
    for (const auto& [key, x] : a.namedPalettesByKey)
        if (!paletteEntryEqual(x, b.namedPalettesByKey.at(key))) return false;
    const auto metadataEqual = [](const RuntimeVoxelMetadata& x, const RuntimeVoxelMetadata& y) {
        return x.imageId == y.imageId && x.vxlPath == y.vxlPath && x.hvaPath == y.hvaPath
            && x.vxlSectionCount == y.vxlSectionCount && x.hvaSectionCount == y.hvaSectionCount
            && x.hvaFrameCount == y.hvaFrameCount && x.vxlFileSize == y.vxlFileSize
            && x.hvaFileSize == y.hvaFileSize && x.vxlSectionSample == y.vxlSectionSample
            && x.hvaSectionSample == y.hvaSectionSample;
    };
    const auto transformsEqual = [](const auto& x, const auto& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (x[i].valid != y[i].valid) return false;
            for (std::size_t j = 0; j < x[i].m.size(); ++j)
                if (!runtimeVoxelFaceContractDoubleEqual(x[i].m[j], y[i].m[j])) return false;
        }
        return true;
    };
    for (const auto& [key, x] : a.tmpTilesByPath) {
        const auto& y = b.tmpTilesByPath.at(key);
        if (x.loadAttempted != y.loadAttempted || x.loaded != y.loaded || x.errorMessage != y.errorMessage)
            return false;
    }
    for (const auto& [key, x] : a.voxelSourcesByImage) {
        const auto& y = b.voxelSourcesByImage.at(key);
        if (x.loadAttempted != y.loadAttempted || x.loaded != y.loaded || x.errorMessage != y.errorMessage
            || x.vxlBytes != y.vxlBytes || x.hvaBytes != y.hvaBytes || !metadataEqual(x.metadata, y.metadata)
            || !transformsEqual(x.firstFrameTransforms, y.firstFrameTransforms)
            || !runtimeGroundShadowContractKeysEqual(x.transformsByFrame, y.transformsByFrame)) return false;
        for (const auto& [frame, transforms] : x.transformsByFrame)
            if (!transformsEqual(transforms, y.transformsByFrame.at(frame))) return false;
    }
    for (const auto& [key, x] : a.voxelFixedScaleByKey) {
        const auto& y = b.voxelFixedScaleByKey.at(key);
        if (x.loadAttempted != y.loadAttempted || x.valid != y.valid || x.category != y.category
            || x.mode != y.mode || x.errorMessage != y.errorMessage
            || !runtimeVoxelFaceContractDoubleEqual(x.fitScale, y.fitScale)
            || !runtimeVoxelFaceContractDoubleEqual(x.assetMaxRangeX, y.assetMaxRangeX)
            || !runtimeVoxelFaceContractDoubleEqual(x.assetMaxRangeY, y.assetMaxRangeY)
            || !runtimeVoxelFaceContractDoubleEqual(x.targetWidth, y.targetWidth)
            || !runtimeVoxelFaceContractDoubleEqual(x.targetHeight, y.targetHeight)) return false;
    }
    for (const auto& [key, x] : a.voxelObjectsByType) {
        const auto& y = b.voxelObjectsByType.at(key);
        if (x.loadAttempted != y.loadAttempted || x.loaded != y.loaded || x.hasSprite != y.hasSprite
            || x.errorMessage != y.errorMessage || x.shadowPixels != y.shadowPixels
            || !metadataEqual(x.metadata, y.metadata)
            || x.shadowSectionRequested != y.shadowSectionRequested
            || x.shadowSectionApplied != y.shadowSectionApplied || x.facingBucket != y.facingBucket
            || x.animationFrameIndex != y.animationFrameIndex
            || !runtimeVoxelFaceContractDoubleEqual(x.rasterFitScale, y.rasterFitScale)
            || !runtimeVoxelFaceContractDoubleEqual(x.projectedRangeX, y.projectedRangeX)
            || !runtimeVoxelFaceContractDoubleEqual(x.projectedRangeY, y.projectedRangeY)
            || !runtimeGroundShadowContractImageEqual(x.image, y.image)
            || x.shadowSpanCache.rebuilds() != y.shadowSpanCache.rebuilds()
            || x.shadowSpanCache.spans().size() != y.shadowSpanCache.spans().size()) return false;
    }
    const auto bytesEqual = [](const auto& x, const auto& y) {
        if (!runtimeGroundShadowContractKeysEqual(x, y)) return false;
        for (const auto& [key, p] : x) {
            const auto& q = y.at(key);
            if (p.loadAttempted != q.loadAttempted || p.loaded != q.loaded || p.bytes != q.bytes
                || p.sourceFingerprint != q.sourceFingerprint || p.resolvedImageId != q.resolvedImageId
                || p.sourcePath != q.sourcePath || p.frameCount != q.frameCount
                || p.errorMessage != q.errorMessage) return false;
        }
        return true;
    };
    const auto spritesEqual = [](const auto& x, const auto& y) {
        if (!runtimeGroundShadowContractKeysEqual(x, y)) return false;
        for (const auto& [key, p] : x) {
            const auto& q = y.at(key);
            if (p.loadAttempted != q.loadAttempted || p.loaded != q.loaded || p.resolvedImageId != q.resolvedImageId
                || p.sourcePath != q.sourcePath || p.frameIndex != q.frameIndex || p.frameCount != q.frameCount
                || p.shadowFrameIndex != q.shadowFrameIndex || p.hasShadowFrame != q.hasShadowFrame
                || p.usedSequenceFrame != q.usedSequenceFrame || p.sourceFingerprint != q.sourceFingerprint
                || p.sequenceName != q.sequenceName || p.errorMessage != q.errorMessage
                || !runtimeGroundShadowContractImageEqual(p.image, q.image)
                || !runtimeGroundShadowContractImageEqual(p.shadowImage, q.shadowImage)) return false;
        }
        return true;
    };
    return bytesEqual(a.objectShpBytesByImage, b.objectShpBytesByImage)
        && bytesEqual(a.auxiliaryShpBytesByImage, b.auxiliaryShpBytesByImage)
        && spritesEqual(a.objectShpSpritesByType, b.objectShpSpritesByType)
        && spritesEqual(a.auxiliaryShpSpritesByImage, b.auxiliaryShpSpritesByImage);
}

static bool runRuntimeGroundShadowEligibilityAssetContractCheck(
    const RuntimeMapRenderContext& source, std::string& error) {
    const auto fail = [&](const std::string& detail) {
        error = "ground shadow eligibility: " + detail; return false;
    };
    if (!source.rules || !source.art || !source.theater) return fail("actual stock context missing");
    constexpr int width = 800, height = 600;
    constexpr std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    const RECT viewport{0, 0, width, height};
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0) : nullptr;
    if (!dc || !bitmap || !raw) {
        if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc);
        return fail("DIB allocation");
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    struct Cleanup {
        HDC dc; HBITMAP bitmap; HGDIOBJ old;
        ~Cleanup() { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); }
    } cleanup{dc, bitmap, old};
    auto* pixels = static_cast<std::uint32_t*>(raw);
    std::uint64_t passes = 0, savedHistoryQueries = 0;
    for (int scene = 0; scene != 9; ++scene) {
        IniFile ini; MapDatabase map; GameWorld world;
        if (!ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\nLocalSize=0,0,32,32\n")
            || !map.loadFromIni(ini) || !world.loadFromMap(map, source.rules)) return fail("actual fixture map");
        if (world.getTerrainCells().empty()) for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            world.accessTerrainCells().push_back(cell);
        }
        auto make = [](int id, const char* type, WorldObjectKind kind, int x, int y) {
            WorldObjectInstance object; object.instanceId = id; object.type = type; object.kind = kind;
            object.owner = "Observer"; object.alive = true; object.health = 1000;
            object.mission = "Guard"; object.x = x; object.y = y;
            object.facing = 64; object.turretFacing = 128; return object;
        };
        world.accessObjects() = {
            make(1, "MTNK", WorldObjectKind::Unit, 8, 8),
            make(2, "E1", WorldObjectKind::Infantry, 10, 8),
            make(3, "GAPOWR", WorldObjectKind::Structure, 11, 10),
            make(4, "MTNK", WorldObjectKind::Unit, 8, 10)
        };
        world.accessObjects()[3].alive = false; world.accessObjects()[3].health = 0;
        std::vector<RuntimeBuildingCollapseState> collapses;
        std::vector<RuntimeWreckState> wrecks;
        std::vector<SpecialMechanismRenderSnapshot> snapshots;
        if (scene == 1) for (int n = 0; n != 10000; ++n) {
            auto dead = make(100 + n, "MTNK", WorldObjectKind::Unit, n % 30, n / 30 % 30);
            dead.alive = false; dead.health = 0; dead.owner = n % 2 ? "Enemy" : " Observer ";
            world.accessObjects().push_back(std::move(dead));
        }
        if (scene == 2) {
            auto& rubble = world.accessObjects()[2]; rubble.alive = false; rubble.health = 0; rubble.rubble = true;
        }
        if (scene == 3) {
            auto& selling = world.accessObjects()[2]; selling.alive = false; selling.mission = "Sold";
            selling.buildAnimTicksTotal = 12; selling.buildAnimTicksRemaining = 8;
        }
        if (scene == 4) {
            auto& collapsed = world.accessObjects()[2]; collapsed.alive = false; collapsed.health = 0;
            RuntimeBuildingCollapseState collapse; collapse.objectId = collapsed.instanceId;
            collapse.animId = "GAPOWR"; collapse.startTick = 42; collapse.frameCount = 100; collapses.push_back(collapse);
        }
        if (scene == 5) {
            world.accessObjects()[3].mission = "CrashingWreck";
            RuntimeWreckState detached; detached.object = world.getObjects()[0]; detached.object.instanceId = 50;
            detached.x = 9; detached.y = 10; detached.startTick = 42; detached.startLevel = 2;
            wrecks.push_back(detached); detached.object.instanceId = 51; detached.sinking = true; wrecks.push_back(detached);
        }
        if (scene == 7) {
            world.accessObjects()[0].type = "FV";
            world.accessObjects()[0].transportPassengerIds = {2};
            world.accessObjects()[1].alive = false;
            world.accessObjects()[1].mission = "InsideTransport";
            SpecialMechanismRenderSnapshot docked; docked.kind = "carrierChildDocked";
            docked.phase = "Docked"; docked.objectId = 1; snapshots.push_back(docked);
        }
        if (scene == 8) {
            world.accessObjects()[0].onBridge = true; world.accessObjects()[0].level = 4;
        }
        RuntimeVisionSystem vision;
        const std::vector<RuntimeVisionObserverDefinition> observers{{"Observer", {"Observer"}}, {"Enemy", {"Enemy"}}};
        if (scene == 6) {
            world.accessObjects()[0].owner = "Enemy";
            world.accessObjects()[2].owner = "Enemy";
            (void)vision.update(world, source.rules, observers, 1);
            world.accessObjects()[1].x = 1; world.accessObjects()[1].y = 1;
            world.accessObjects()[0].x = 25; world.accessObjects()[0].y = 25;
            (void)vision.update(world, source.rules, observers, 2);
            if (vision.visibilityFor("Observer", 25, 25) != RuntimeCellVisibility::Shrouded
                || vision.visibilityFor("Observer", 11, 10) != RuntimeCellVisibility::Fogged)
                return fail("genuine shroud/fog fixture absent");
        } else (void)vision.update(world, source.rules, observers, 42);
        RuntimeMapRenderContext context = source;
        context.animationTick = 42; context.fixedTickHz = 30; context.stableRenderConfiguration = true;
        context.vision = &vision; context.visionObserverOwner = "Observer"; context.mapFacts = nullptr;
        context.projectileActors = nullptr; context.impactActors = nullptr;
        context.wrecks = &wrecks; context.buildingCollapses = &collapses;
        context.specialMechanismSnapshots = &snapshots; context.gateSnapshots = nullptr;
        context.particleSnapshots = nullptr; context.particleBeamSnapshots = nullptr; context.localLightSnapshots = nullptr;
        context.selectedObjectIds = nullptr; context.playerCheerUntilTick = nullptr; context.uiResources = nullptr;
        context.lightingTint = nullptr; context.lightingProfile = nullptr;
        context.detailStatsEnabled = false; context.coarseStageTimingEnabled = false; context.effectOnlyProbe = false;
        context.shadowsEnabled = true; context.captureBuildingDepthHitSpans = false;
        context.surfacePixels = pixels; context.surfaceWidth = width; context.surfaceHeight = height;
        RuntimeMapCamera camera; camera.initialized = true;
        camera.worldOffsetX = -width / 2; camera.worldOffsetY = -80;
        RuntimeMapRenderCache reference, optimized;
        for (int pass = 0; pass != 3; ++pass) {
            const auto revision = vision.revisionFor("Observer");
            RuntimeMapRenderStats expectedStats; RuntimeGroundShadowEligibilityObservation expectedObservation;
            std::vector<std::uint32_t> expectedPixels;
            for (bool fast : {false, true}) {
                RuntimeGroundShadowEligibilityReferenceScope mode(!fast);
                RuntimeGroundShadowEligibilityObservation observation;
                RuntimeGroundShadowEligibilityObservationScope observe(observation);
                auto& cache = fast ? optimized : reference;
                auto drawCamera = camera;
                GdiFlush(); std::fill_n(pixels, pixelCount, 0xA53B7091U);
                const auto stats = drawRuntimeMap(dc, viewport, world, context, drawCamera, cache);
                GdiFlush();
                if (!stats.rendered) return fail("actual map was not drawn");
                if (!fast) {
                    expectedPixels.assign(pixels, pixels + pixelCount);
                    expectedStats = stats; expectedObservation = observation;
                    if (scene == 0 && (!stats.visibleVoxelBodies || !stats.shadowsDrawn))
                        return fail("real stock VXL/shadow path absent");
                    continue;
                }
                std::string field;
                if (!std::equal(expectedPixels.begin(), expectedPixels.end(), pixels)
                    || !runtimeGroundShadowContractStatsEqual(expectedStats, stats, field)
                    || !runtimeGroundShadowContractCacheEqual(reference, optimized))
                    return fail("scene=" + std::to_string(scene) + " pass=" + std::to_string(pass) + " pixels/stats/cache " + field);
                if (observation.deadRejected != expectedObservation.deadRejected
                    || observation.eligible != expectedObservation.eligible
                    || observation.crashingEligible != expectedObservation.crashingEligible
                    || observation.sellingEligible != expectedObservation.sellingEligible
                    || observation.collapseEligible != expectedObservation.collapseEligible
                    || expectedObservation.visibilityQueries != observation.visibilityQueries + observation.deadRejected
                    || vision.revisionFor("Observer") != revision)
                    return fail("actual shadow query count/retained eligibility/vision state changed");
                if (scene == 1) {
                    if (observation.deadRejected != 10001
                        || expectedObservation.visibilityQueries - observation.visibilityQueries != 10001)
                        return fail("10000 ordinary history objects did not avoid exactly 10000 additional queries");
                    savedHistoryQueries += 10000;
                }
                if (scene == 3 && observation.sellingEligible != 1) return fail("selling exception skipped");
                if (scene == 4 && (observation.collapseEligible != 1 || !stats.buildingCollapseSprites))
                    return fail("actual active collapse exception skipped");
                if (scene == 5 && (observation.crashingEligible != 2 || observation.retainedWreckQueries != 1))
                    return fail("world/detached crashing and rejected sinking paths absent");
                if (scene == 7 && observation.deadRejected != 2) return fail("transport passenger not rejected");
                if (scene == 8 && !stats.unitShadowsDrawn) return fail("real stock deferred bridge-shadow pass absent");
                ++passes;
            }
        }
    }
    std::cout << "Ground shadow eligibility: actual stock full raw32 DIB passes=" << passes
        << " savedHistoryQueries=" << savedHistoryQueries << " non-timing stats/resources exact passed\n";
    return true;
}

static bool runtimeGroundShadowContractStatsEqual(const RuntimeMapRenderStats& a,
    const RuntimeMapRenderStats& b, std::string& field) {
    if (a.mapLightSources != b.mapLightSources) { field = "mapLightSources"; return false; }
    if (a.mapLightReceivers != b.mapLightReceivers) { field = "mapLightReceivers"; return false; }
    if (a.buildingCollapseSprites != b.buildingCollapseSprites) { field = "buildingCollapseSprites"; return false; }
    if (a.buildingCollapseMissing != b.buildingCollapseMissing) { field = "buildingCollapseMissing"; return false; }
    if (a.wreckBodySprites != b.wreckBodySprites) { field = "wreckBodySprites"; return false; }
    if (a.sinkingBodySprites != b.sinkingBodySprites) { field = "sinkingBodySprites"; return false; }
    if (a.behindMarkerSprites != b.behindMarkerSprites) { field = "behindMarkerSprites"; return false; }
    if (a.behindMarkerMissing != b.behindMarkerMissing) { field = "behindMarkerMissing"; return false; }
    if (a.behindMarkerFrame != b.behindMarkerFrame) { field = "behindMarkerFrame"; return false; }
    if (a.parachuteSprites != b.parachuteSprites) { field = "parachuteSprites"; return false; }
    if (a.parachuteMissing != b.parachuteMissing) { field = "parachuteMissing"; return false; }
    if (a.tankBunkerFrontLayers != b.tankBunkerFrontLayers) { field = "tankBunkerFrontLayers"; return false; }
    if (a.tankBunkerFrontFrame != b.tankBunkerFrontFrame) { field = "tankBunkerFrontFrame"; return false; }
    if (a.rendered != b.rendered) { field = "rendered"; return false; }
    if (a.lightingTintApplied != b.lightingTintApplied) { field = "lightingTintApplied"; return false; }
    if (a.lightingTintedPixels != b.lightingTintedPixels) { field = "lightingTintedPixels"; return false; }
    if (a.visibleTerrainCells != b.visibleTerrainCells) { field = "visibleTerrainCells"; return false; }
    if (a.texturedTerrainCells != b.texturedTerrainCells) { field = "texturedTerrainCells"; return false; }
    if (a.fallbackTerrainCells != b.fallbackTerrainCells) { field = "fallbackTerrainCells"; return false; }
    if (a.authoredEmptyTerrainSubTiles != b.authoredEmptyTerrainSubTiles) { field = "authoredEmptyTerrainSubTiles"; return false; }
    if (a.visibleSmudges != b.visibleSmudges) { field = "visibleSmudges"; return false; }
    if (a.smudgeSprites != b.smudgeSprites) { field = "smudgeSprites"; return false; }
    if (a.smudgeMissing != b.smudgeMissing) { field = "smudgeMissing"; return false; }
    if (a.visibleOverlayCells != b.visibleOverlayCells) { field = "visibleOverlayCells"; return false; }
    if (a.resourceOverlayCells != b.resourceOverlayCells) { field = "resourceOverlayCells"; return false; }
    if (a.resourceOverlayOreCells != b.resourceOverlayOreCells) { field = "resourceOverlayOreCells"; return false; }
    if (a.resourceOverlayGemCells != b.resourceOverlayGemCells) { field = "resourceOverlayGemCells"; return false; }
    if (a.resourceOverlaySprites != b.resourceOverlaySprites) { field = "resourceOverlaySprites"; return false; }
    if (a.resourceOverlayFallbacks != b.resourceOverlayFallbacks) { field = "resourceOverlayFallbacks"; return false; }
    if (a.resourceOverlayMissing != b.resourceOverlayMissing) { field = "resourceOverlayMissing"; return false; }
    if (a.resourceOverlayOreMissing != b.resourceOverlayOreMissing) { field = "resourceOverlayOreMissing"; return false; }
    if (a.resourceOverlayGemMissing != b.resourceOverlayGemMissing) { field = "resourceOverlayGemMissing"; return false; }
    if (a.resourceOverlayFallbackProxy != b.resourceOverlayFallbackProxy) { field = "resourceOverlayFallbackProxy"; return false; }
    if (a.resourceOverlayBridgeRejected != b.resourceOverlayBridgeRejected) { field = "resourceOverlayBridgeRejected"; return false; }
    if (a.resourceOverlayCandidateRejected != b.resourceOverlayCandidateRejected) { field = "resourceOverlayCandidateRejected"; return false; }
    if (a.resourceOverlayRuntimeOverridesMapFact != b.resourceOverlayRuntimeOverridesMapFact) { field = "resourceOverlayRuntimeOverridesMapFact"; return false; }
    if (a.resourceOverlayRuntimeClearsMapFact != b.resourceOverlayRuntimeClearsMapFact) { field = "resourceOverlayRuntimeClearsMapFact"; return false; }
    if (a.resourceOverlayRuntimeIdentityOverridesMapFact != b.resourceOverlayRuntimeIdentityOverridesMapFact) { field = "resourceOverlayRuntimeIdentityOverridesMapFact"; return false; }
    if (a.rockOverlaySpritesCentered != b.rockOverlaySpritesCentered) { field = "rockOverlaySpritesCentered"; return false; }
    if (a.lastResourceOverlayFrame != b.lastResourceOverlayFrame) { field = "lastResourceOverlayFrame"; return false; }
    if (a.lastResourceOverlayCandidates != b.lastResourceOverlayCandidates) { field = "lastResourceOverlayCandidates"; return false; }
    if (a.lastResourceOverlayFailure != b.lastResourceOverlayFailure) { field = "lastResourceOverlayFailure"; return false; }
    if (a.bridgeOverlayCells != b.bridgeOverlayCells) { field = "bridgeOverlayCells"; return false; }
    if (a.bridgeOverlaySprites != b.bridgeOverlaySprites) { field = "bridgeOverlaySprites"; return false; }
    if (a.bridgeOverlayTerrainBaked != b.bridgeOverlayTerrainBaked) { field = "bridgeOverlayTerrainBaked"; return false; }
    if (a.bridgeOverlayDeferred != b.bridgeOverlayDeferred) { field = "bridgeOverlayDeferred"; return false; }
    if (a.bridgeOverlayDeferredDrawn != b.bridgeOverlayDeferredDrawn) { field = "bridgeOverlayDeferredDrawn"; return false; }
    if (a.bridgeOverlayMissing != b.bridgeOverlayMissing) { field = "bridgeOverlayMissing"; return false; }
    if (a.bridgeOverlayFallbackFrameUsed != b.bridgeOverlayFallbackFrameUsed) { field = "bridgeOverlayFallbackFrameUsed"; return false; }
    if (a.bridgeOverlayLevelMismatch != b.bridgeOverlayLevelMismatch) { field = "bridgeOverlayLevelMismatch"; return false; }
    if (a.bridgeOverlayHighCells != b.bridgeOverlayHighCells) { field = "bridgeOverlayHighCells"; return false; }
    if (a.bridgeOverlayLowCells != b.bridgeOverlayLowCells) { field = "bridgeOverlayLowCells"; return false; }
    if (a.bridgeOverlayRuntimeFrameOverridesMapFact != b.bridgeOverlayRuntimeFrameOverridesMapFact) { field = "bridgeOverlayRuntimeFrameOverridesMapFact"; return false; }
    if (a.bridgeOverlayRuntimeFrameMismatches != b.bridgeOverlayRuntimeFrameMismatches) { field = "bridgeOverlayRuntimeFrameMismatches"; return false; }
    if (a.bridgeOverlayHighAnchorAdjusted != b.bridgeOverlayHighAnchorAdjusted) { field = "bridgeOverlayHighAnchorAdjusted"; return false; }
    if (a.bridgeOverlayHighTerminalUnderlays != b.bridgeOverlayHighTerminalUnderlays) { field = "bridgeOverlayHighTerminalUnderlays"; return false; }
    if (a.visibleTerrainObjects != b.visibleTerrainObjects) { field = "visibleTerrainObjects"; return false; }
    if (a.terrainObjectSprites != b.terrainObjectSprites) { field = "terrainObjectSprites"; return false; }
    if (a.terrainObjectAnimatedSprites != b.terrainObjectAnimatedSprites) { field = "terrainObjectAnimatedSprites"; return false; }
    if (a.terrainObjectAnimationFrames != b.terrainObjectAnimationFrames) { field = "terrainObjectAnimationFrames"; return false; }
    if (a.preCullWorldObjects != b.preCullWorldObjects) { field = "preCullWorldObjects"; return false; }
    if (a.culledWorldObjects != b.culledWorldObjects) { field = "culledWorldObjects"; return false; }
    if (a.sortedWorldObjects != b.sortedWorldObjects) { field = "sortedWorldObjects"; return false; }
    if (a.visibleWorldObjects != b.visibleWorldObjects) { field = "visibleWorldObjects"; return false; }
    if (a.visionHiddenWorldObjects != b.visionHiddenWorldObjects) { field = "visionHiddenWorldObjects"; return false; }
    if (a.visionHiddenTerrainObjects != b.visionHiddenTerrainObjects) { field = "visionHiddenTerrainObjects"; return false; }
    if (a.visionHiddenProjectileActors != b.visionHiddenProjectileActors) { field = "visionHiddenProjectileActors"; return false; }
    if (a.visionHiddenImpactActors != b.visionHiddenImpactActors) { field = "visionHiddenImpactActors"; return false; }
    if (a.visionVisibleCells != b.visionVisibleCells) { field = "visionVisibleCells"; return false; }
    if (a.visionFoggedCells != b.visionFoggedCells) { field = "visionFoggedCells"; return false; }
    if (a.visionShroudedCells != b.visionShroudedCells) { field = "visionShroudedCells"; return false; }
    if (a.visionOverlayPixels != b.visionOverlayPixels) { field = "visionOverlayPixels"; return false; }
    if (a.visionOverlayCacheHits != b.visionOverlayCacheHits) { field = "visionOverlayCacheHits"; return false; }
    if (a.visionOverlayCacheMisses != b.visionOverlayCacheMisses) { field = "visionOverlayCacheMisses"; return false; }
    if (a.visionGeometryCacheHits != b.visionGeometryCacheHits) { field = "visionGeometryCacheHits"; return false; }
    if (a.visionGeometryCacheMisses != b.visionGeometryCacheMisses) { field = "visionGeometryCacheMisses"; return false; }
    if (a.visionOpaqueRunPixels != b.visionOpaqueRunPixels) { field = "visionOpaqueRunPixels"; return false; }
    if (a.visionOpaqueRunCacheBytes != b.visionOpaqueRunCacheBytes) { field = "visionOpaqueRunCacheBytes"; return false; }
    if (a.airborneForegroundBodiesDrawn != b.airborneForegroundBodiesDrawn) { field = "airborneForegroundBodiesDrawn"; return false; }
    if (a.dockedAircraftForegroundObjects != b.dockedAircraftForegroundObjects) { field = "dockedAircraftForegroundObjects"; return false; }
    if (a.repairDepotForegroundObjects != b.repairDepotForegroundObjects) { field = "repairDepotForegroundObjects"; return false; }
    if (a.repairDepotAnimationLayers != b.repairDepotAnimationLayers) { field = "repairDepotAnimationLayers"; return false; }
    if (a.repairDepotAnimationForegroundLayers != b.repairDepotAnimationForegroundLayers) { field = "repairDepotAnimationForegroundLayers"; return false; }
    if (a.repairDepotAnimationFrame != b.repairDepotAnimationFrame) { field = "repairDepotAnimationFrame"; return false; }
    if (a.repairDepotAnimationPhase != b.repairDepotAnimationPhase) { field = "repairDepotAnimationPhase"; return false; }
    if (a.repairDepotAnimationImage != b.repairDepotAnimationImage) { field = "repairDepotAnimationImage"; return false; }
    if (a.aircraftBuildingClearanceAdjustments != b.aircraftBuildingClearanceAdjustments) { field = "aircraftBuildingClearanceAdjustments"; return false; }
    if (a.aircraftMaxBuildingClearanceLiftPixels != b.aircraftMaxBuildingClearanceLiftPixels) { field = "aircraftMaxBuildingClearanceLiftPixels"; return false; }
    if (a.shadowCandidates != b.shadowCandidates) { field = "shadowCandidates"; return false; }
    if (a.shadowsDrawn != b.shadowsDrawn) { field = "shadowsDrawn"; return false; }
    if (a.shadowPixelsDarkened != b.shadowPixelsDarkened) { field = "shadowPixelsDarkened"; return false; }
    if (a.worldObjectShadowOrderTraceCount != b.worldObjectShadowOrderTraceCount) { field = "worldObjectShadowOrderTraceCount"; return false; }
    if (a.worldObjectShadowOrderTraceHash != b.worldObjectShadowOrderTraceHash) { field = "worldObjectShadowOrderTraceHash"; return false; }
    if (a.shpShadowsDrawn != b.shpShadowsDrawn) { field = "shpShadowsDrawn"; return false; }
    if (a.voxelShadowsDrawn != b.voxelShadowsDrawn) { field = "voxelShadowsDrawn"; return false; }
    if (a.shpShadowMasksResolved != b.shpShadowMasksResolved) { field = "shpShadowMasksResolved"; return false; }
    if (a.voxelShadowMasksResolved != b.voxelShadowMasksResolved) { field = "voxelShadowMasksResolved"; return false; }
    if (a.generatedShadowFallbacks != b.generatedShadowFallbacks) { field = "generatedShadowFallbacks"; return false; }
    if (a.structureShadowsDrawn != b.structureShadowsDrawn) { field = "structureShadowsDrawn"; return false; }
    if (a.unitShadowsDrawn != b.unitShadowsDrawn) { field = "unitShadowsDrawn"; return false; }
    if (a.infantryShadowsDrawn != b.infantryShadowsDrawn) { field = "infantryShadowsDrawn"; return false; }
    if (a.aircraftShadowsDrawn != b.aircraftShadowsDrawn) { field = "aircraftShadowsDrawn"; return false; }
    if (a.buildingAnimationShadowsDrawn != b.buildingAnimationShadowsDrawn) { field = "buildingAnimationShadowsDrawn"; return false; }
    if (a.projectileShadowCandidates != b.projectileShadowCandidates) { field = "projectileShadowCandidates"; return false; }
    if (a.projectileShadowsDrawn != b.projectileShadowsDrawn) { field = "projectileShadowsDrawn"; return false; }
    if (a.projectileShadowPixelsDarkened != b.projectileShadowPixelsDarkened) { field = "projectileShadowPixelsDarkened"; return false; }
    if (a.waterWakeEligibleObjects != b.waterWakeEligibleObjects) { field = "waterWakeEligibleObjects"; return false; }
    if (a.waterWakeEmissions != b.waterWakeEmissions) { field = "waterWakeEmissions"; return false; }
    if (a.waterWakeActiveActors != b.waterWakeActiveActors) { field = "waterWakeActiveActors"; return false; }
    if (a.waterWakeSpritesDrawn != b.waterWakeSpritesDrawn) { field = "waterWakeSpritesDrawn"; return false; }
    if (a.waterWakeSpritesCulled != b.waterWakeSpritesCulled) { field = "waterWakeSpritesCulled"; return false; }
    if (a.waterWakeSpritesMissing != b.waterWakeSpritesMissing) { field = "waterWakeSpritesMissing"; return false; }
    if (a.waterWakeSuppressedNotMoving != b.waterWakeSuppressedNotMoving) { field = "waterWakeSuppressedNotMoving"; return false; }
    if (a.waterWakeSuppressedNotWater != b.waterWakeSuppressedNotWater) { field = "waterWakeSuppressedNotWater"; return false; }
    if (a.waterWakeSuppressedSubmerged != b.waterWakeSuppressedSubmerged) { field = "waterWakeSuppressedSubmerged"; return false; }
    if (a.waterWakeFrameCount != b.waterWakeFrameCount) { field = "waterWakeFrameCount"; return false; }
    if (a.waterWakeFrameDelayTicks != b.waterWakeFrameDelayTicks) { field = "waterWakeFrameDelayTicks"; return false; }
    if (a.lastWaterWakeFrame != b.lastWaterWakeFrame) { field = "lastWaterWakeFrame"; return false; }
    if (a.lastWaterWakeImageId != b.lastWaterWakeImageId) { field = "lastWaterWakeImageId"; return false; }
    if (a.lastWaterWakeSource != b.lastWaterWakeSource) { field = "lastWaterWakeSource"; return false; }
    if (a.lastWaterWakePaletteSource != b.lastWaterWakePaletteSource) { field = "lastWaterWakePaletteSource"; return false; }
    if (a.shadowsSuppressed != b.shadowsSuppressed) { field = "shadowsSuppressed"; return false; }
    if (a.shadowsMissing != b.shadowsMissing) { field = "shadowsMissing"; return false; }
    if (a.voxelShadowIndexApplied != b.voxelShadowIndexApplied) { field = "voxelShadowIndexApplied"; return false; }
    if (a.voxelShadowIndexFallbacks != b.voxelShadowIndexFallbacks) { field = "voxelShadowIndexFallbacks"; return false; }
    if (a.visibleStructureSprites != b.visibleStructureSprites) { field = "visibleStructureSprites"; return false; }
    if (a.missingStructureSprites != b.missingStructureSprites) { field = "missingStructureSprites"; return false; }
    if (a.gateSpecialZOverlaySprites != b.gateSpecialZOverlaySprites) { field = "gateSpecialZOverlaySprites"; return false; }
    if (a.gateSpecialZOverlayMissing != b.gateSpecialZOverlayMissing) { field = "gateSpecialZOverlayMissing"; return false; }
    if (a.structureWallOverlaySprites != b.structureWallOverlaySprites) { field = "structureWallOverlaySprites"; return false; }
    if (a.structureWallOverlayFallbacks != b.structureWallOverlayFallbacks) { field = "structureWallOverlayFallbacks"; return false; }
    if (a.structureWallConnectionMasksSeen != b.structureWallConnectionMasksSeen) { field = "structureWallConnectionMasksSeen"; return false; }
    if (a.structureWallOverlayFramesSeen != b.structureWallOverlayFramesSeen) { field = "structureWallOverlayFramesSeen"; return false; }
    if (a.lastStructureWallConnectionMask != b.lastStructureWallConnectionMask) { field = "lastStructureWallConnectionMask"; return false; }
    if (a.lastStructureWallOverlayFrame != b.lastStructureWallOverlayFrame) { field = "lastStructureWallOverlayFrame"; return false; }
    if (a.visibleObjectShpSprites != b.visibleObjectShpSprites) { field = "visibleObjectShpSprites"; return false; }
    if (a.missingObjectShpSprites != b.missingObjectShpSprites) { field = "missingObjectShpSprites"; return false; }
    if (a.unloadingClassObjectSprites != b.unloadingClassObjectSprites) { field = "unloadingClassObjectSprites"; return false; }
    if (a.unloadingClassForegroundObjects != b.unloadingClassForegroundObjects) { field = "unloadingClassForegroundObjects"; return false; }
    if (a.unloadingClassSpecialForegroundLayers != b.unloadingClassSpecialForegroundLayers) { field = "unloadingClassSpecialForegroundLayers"; return false; }
    if (a.unloadingClassTurretSprites != b.unloadingClassTurretSprites) { field = "unloadingClassTurretSprites"; return false; }
    if (a.unloadingClassTurretSuppressedObjects != b.unloadingClassTurretSuppressedObjects) { field = "unloadingClassTurretSuppressedObjects"; return false; }
    if (a.unloadingClassDiagnostic != b.unloadingClassDiagnostic) { field = "unloadingClassDiagnostic"; return false; }
    if (a.visibleVoxelProxies != b.visibleVoxelProxies) { field = "visibleVoxelProxies"; return false; }
    if (a.missingVoxelProxies != b.missingVoxelProxies) { field = "missingVoxelProxies"; return false; }
    if (a.visibleVoxelBodies != b.visibleVoxelBodies) { field = "visibleVoxelBodies"; return false; }
    if (a.visibleVoxelTurrets != b.visibleVoxelTurrets) { field = "visibleVoxelTurrets"; return false; }
    if (a.visibleVoxelBarrels != b.visibleVoxelBarrels) { field = "visibleVoxelBarrels"; return false; }
    if (a.visibleVoxelBarrelCues != b.visibleVoxelBarrelCues) { field = "visibleVoxelBarrelCues"; return false; }
    if (a.missingVoxelBarrels != b.missingVoxelBarrels) { field = "missingVoxelBarrels"; return false; }
    if (a.missingVoxelTurrets != b.missingVoxelTurrets) { field = "missingVoxelTurrets"; return false; }
    if (a.voxelTurretFacingFromTurret != b.voxelTurretFacingFromTurret) { field = "voxelTurretFacingFromTurret"; return false; }
    if (a.voxelTurretFacingFromBody != b.voxelTurretFacingFromBody) { field = "voxelTurretFacingFromBody"; return false; }
    if (a.voxelBodyMaxWidth != b.voxelBodyMaxWidth) { field = "voxelBodyMaxWidth"; return false; }
    if (a.voxelBodyMaxHeight != b.voxelBodyMaxHeight) { field = "voxelBodyMaxHeight"; return false; }
    if (a.voxelTurretMaxWidth != b.voxelTurretMaxWidth) { field = "voxelTurretMaxWidth"; return false; }
    if (a.voxelTurretMaxHeight != b.voxelTurretMaxHeight) { field = "voxelTurretMaxHeight"; return false; }
    if (a.voxelBarrelMaxWidth != b.voxelBarrelMaxWidth) { field = "voxelBarrelMaxWidth"; return false; }
    if (a.voxelBarrelMaxHeight != b.voxelBarrelMaxHeight) { field = "voxelBarrelMaxHeight"; return false; }
    if (a.structureTurretMaxWidth != b.structureTurretMaxWidth) { field = "structureTurretMaxWidth"; return false; }
    if (a.structureTurretMaxHeight != b.structureTurretMaxHeight) { field = "structureTurretMaxHeight"; return false; }
    if (a.firstStructureTurretFacing != b.firstStructureTurretFacing) { field = "firstStructureTurretFacing"; return false; }
    if (a.firstStructureTurretScreenOffsetX != b.firstStructureTurretScreenOffsetX) { field = "firstStructureTurretScreenOffsetX"; return false; }
    if (a.firstStructureTurretScreenOffsetY != b.firstStructureTurretScreenOffsetY) { field = "firstStructureTurretScreenOffsetY"; return false; }
    if (!runtimeVoxelFaceContractDoubleEqual(a.firstStructureTurretFitScale, b.firstStructureTurretFitScale)) { field = "firstStructureTurretFitScale"; return false; }
    if (a.firstStructureTurretType != b.firstStructureTurretType) { field = "firstStructureTurretType"; return false; }
    if (a.firstStructureTurretImage != b.firstStructureTurretImage) { field = "firstStructureTurretImage"; return false; }
    if (a.animatedShpSprites != b.animatedShpSprites) { field = "animatedShpSprites"; return false; }
    if (a.sequenceFrameHits != b.sequenceFrameHits) { field = "sequenceFrameHits"; return false; }
    if (a.sequenceFrameFallbacks != b.sequenceFrameFallbacks) { field = "sequenceFrameFallbacks"; return false; }
    if (a.sequenceReadyHits != b.sequenceReadyHits) { field = "sequenceReadyHits"; return false; }
    if (a.sequenceGuardHits != b.sequenceGuardHits) { field = "sequenceGuardHits"; return false; }
    if (a.sequenceWalkHits != b.sequenceWalkHits) { field = "sequenceWalkHits"; return false; }
    if (a.sequenceShovelHits != b.sequenceShovelHits) { field = "sequenceShovelHits"; return false; }
    if (a.sequenceFireHits != b.sequenceFireHits) { field = "sequenceFireHits"; return false; }
    if (a.sequenceIdleHits != b.sequenceIdleHits) { field = "sequenceIdleHits"; return false; }
    if (a.sequenceIdle1Hits != b.sequenceIdle1Hits) { field = "sequenceIdle1Hits"; return false; }
    if (a.sequenceIdle2Hits != b.sequenceIdle2Hits) { field = "sequenceIdle2Hits"; return false; }
    if (a.sequenceDeployHits != b.sequenceDeployHits) { field = "sequenceDeployHits"; return false; }
    if (a.sequenceDeployedHits != b.sequenceDeployedHits) { field = "sequenceDeployedHits"; return false; }
    if (a.sequenceDeployedFireHits != b.sequenceDeployedFireHits) { field = "sequenceDeployedFireHits"; return false; }
    if (a.sequenceWalkAfterStop != b.sequenceWalkAfterStop) { field = "sequenceWalkAfterStop"; return false; }
    if (a.buildingArtAnimationLayers != b.buildingArtAnimationLayers) { field = "buildingArtAnimationLayers"; return false; }
    if (a.buildingSpecialAnimationLayers != b.buildingSpecialAnimationLayers) { field = "buildingSpecialAnimationLayers"; return false; }
    if (a.buildingArtAnimationMissing != b.buildingArtAnimationMissing) { field = "buildingArtAnimationMissing"; return false; }
    if (a.buildingPoweredAnimationLayers != b.buildingPoweredAnimationLayers) { field = "buildingPoweredAnimationLayers"; return false; }
    if (a.buildingLowPowerAnimationLayers != b.buildingLowPowerAnimationLayers) { field = "buildingLowPowerAnimationLayers"; return false; }
    if (a.buildingNeutralAnimationSuppressed != b.buildingNeutralAnimationSuppressed) { field = "buildingNeutralAnimationSuppressed"; return false; }
    if (a.buildingOfflineHiddenAnimationLayers != b.buildingOfflineHiddenAnimationLayers) { field = "buildingOfflineHiddenAnimationLayers"; return false; }
    if (a.buildingOfflinePausedVisibleAnimationLayers != b.buildingOfflinePausedVisibleAnimationLayers) { field = "buildingOfflinePausedVisibleAnimationLayers"; return false; }
    if (a.buildingOfflineContinuingAnimationLayers != b.buildingOfflineContinuingAnimationLayers) { field = "buildingOfflineContinuingAnimationLayers"; return false; }
    if (a.buildingWorkAnimationLayers != b.buildingWorkAnimationLayers) { field = "buildingWorkAnimationLayers"; return false; }
    if (a.firstBuildingWorkAnimationFrame != b.firstBuildingWorkAnimationFrame) { field = "firstBuildingWorkAnimationFrame"; return false; }
    if (a.buildingFactoryVehicleDoorAnimationLayers != b.buildingFactoryVehicleDoorAnimationLayers) { field = "buildingFactoryVehicleDoorAnimationLayers"; return false; }
    if (a.buildingFactoryAirRoofAnimationLayers != b.buildingFactoryAirRoofAnimationLayers) { field = "buildingFactoryAirRoofAnimationLayers"; return false; }
    if (a.buildingShipyardProductionAnimationLayers != b.buildingShipyardProductionAnimationLayers) { field = "buildingShipyardProductionAnimationLayers"; return false; }
    if (a.buildingShipyardIdleAnimationLayers != b.buildingShipyardIdleAnimationLayers) { field = "buildingShipyardIdleAnimationLayers"; return false; }
    if (a.shipyardProductionAnimationFrame != b.shipyardProductionAnimationFrame) { field = "shipyardProductionAnimationFrame"; return false; }
    if (a.shipyardProductionAnimationImage != b.shipyardProductionAnimationImage) { field = "shipyardProductionAnimationImage"; return false; }
    if (a.firstBuildingArtAnimationFrame != b.firstBuildingArtAnimationFrame) { field = "firstBuildingArtAnimationFrame"; return false; }
    if (a.firstBuildingOfflinePausedVisibleFrame != b.firstBuildingOfflinePausedVisibleFrame) { field = "firstBuildingOfflinePausedVisibleFrame"; return false; }
    if (a.firstBuildingOfflineContinuingFrame != b.firstBuildingOfflineContinuingFrame) { field = "firstBuildingOfflineContinuingFrame"; return false; }
    if (a.buildingDamageFireOffsets != b.buildingDamageFireOffsets) { field = "buildingDamageFireOffsets"; return false; }
    if (a.buildingDamageFireDrawn != b.buildingDamageFireDrawn) { field = "buildingDamageFireDrawn"; return false; }
    if (a.buildingDamageFireCulled != b.buildingDamageFireCulled) { field = "buildingDamageFireCulled"; return false; }
    if (a.buildingDamageFireMissing != b.buildingDamageFireMissing) { field = "buildingDamageFireMissing"; return false; }
    if (a.firstBuildingDamageFireFrame != b.firstBuildingDamageFireFrame) { field = "firstBuildingDamageFireFrame"; return false; }
    if (a.structureTurretAnimAttempts != b.structureTurretAnimAttempts) { field = "structureTurretAnimAttempts"; return false; }
    if (a.structureTurretAnimDrawn != b.structureTurretAnimDrawn) { field = "structureTurretAnimDrawn"; return false; }
    if (a.structureTurretAnimMissing != b.structureTurretAnimMissing) { field = "structureTurretAnimMissing"; return false; }
    if (a.structureTurretAnimVoxel != b.structureTurretAnimVoxel) { field = "structureTurretAnimVoxel"; return false; }
    if (a.structureTurretAnimShp != b.structureTurretAnimShp) { field = "structureTurretAnimShp"; return false; }
    if (a.structureTurretCuboidRasters != b.structureTurretCuboidRasters) { field = "structureTurretCuboidRasters"; return false; }
    if (a.structureTurretPointRasters != b.structureTurretPointRasters) { field = "structureTurretPointRasters"; return false; }
    if (a.structureRearAnchorObjects != b.structureRearAnchorObjects) { field = "structureRearAnchorObjects"; return false; }
    if (a.structureMultiCellAnchorObjects != b.structureMultiCellAnchorObjects) { field = "structureMultiCellAnchorObjects"; return false; }
    if (a.structureBaseGlobalRectObjects != b.structureBaseGlobalRectObjects) { field = "structureBaseGlobalRectObjects"; return false; }
    if (a.structureActiveGlobalRectObjects != b.structureActiveGlobalRectObjects) { field = "structureActiveGlobalRectObjects"; return false; }
    if (a.structureCullingRectDiagnostics != b.structureCullingRectDiagnostics) { field = "structureCullingRectDiagnostics"; return false; }
    if (a.structureRectDiagnostic != b.structureRectDiagnostic) { field = "structureRectDiagnostic"; return false; }
    if (a.buildingSpecialAnimationImage != b.buildingSpecialAnimationImage) { field = "buildingSpecialAnimationImage"; return false; }
    if (a.buildingSpecialAnimationResolved != b.buildingSpecialAnimationResolved) { field = "buildingSpecialAnimationResolved"; return false; }
    if (a.buildingSpecialAnimationSource != b.buildingSpecialAnimationSource) { field = "buildingSpecialAnimationSource"; return false; }
    if (a.firstBuildingArtAnimationImage != b.firstBuildingArtAnimationImage) { field = "firstBuildingArtAnimationImage"; return false; }
    if (a.firstBuildingDamageFireImage != b.firstBuildingDamageFireImage) { field = "firstBuildingDamageFireImage"; return false; }
    if (a.firstBuildingOfflinePausedVisibleImage != b.firstBuildingOfflinePausedVisibleImage) { field = "firstBuildingOfflinePausedVisibleImage"; return false; }
    if (a.firstBuildingOfflineContinuingImage != b.firstBuildingOfflineContinuingImage) { field = "firstBuildingOfflineContinuingImage"; return false; }
    if (a.firstBuildingWorkAnimationImage != b.firstBuildingWorkAnimationImage) { field = "firstBuildingWorkAnimationImage"; return false; }
    if (a.structureTurretDiagnostic != b.structureTurretDiagnostic) { field = "structureTurretDiagnostic"; return false; }
    if (a.missingVoxelBarrelDiagnostic != b.missingVoxelBarrelDiagnostic) { field = "missingVoxelBarrelDiagnostic"; return false; }
    if (a.voxelSpriteDiagnostic != b.voxelSpriteDiagnostic) { field = "voxelSpriteDiagnostic"; return false; }
    if (a.ownerRemapObjects != b.ownerRemapObjects) { field = "ownerRemapObjects"; return false; }
    if (a.playerRemapObjects != b.playerRemapObjects) { field = "playerRemapObjects"; return false; }
    if (a.ownerRemapDeferredObjects != b.ownerRemapDeferredObjects) { field = "ownerRemapDeferredObjects"; return false; }
    if (a.neutralRemapObjects != b.neutralRemapObjects) { field = "neutralRemapObjects"; return false; }
    if (a.unexpectedBlueRemapObjects != b.unexpectedBlueRemapObjects) { field = "unexpectedBlueRemapObjects"; return false; }
    if (a.uiResourceDecoded != b.uiResourceDecoded) { field = "uiResourceDecoded"; return false; }
    if (a.uiResourceUnsupported != b.uiResourceUnsupported) { field = "uiResourceUnsupported"; return false; }
    if (a.uiResourceCandidates != b.uiResourceCandidates) { field = "uiResourceCandidates"; return false; }
    if (a.uiResourceMissing != b.uiResourceMissing) { field = "uiResourceMissing"; return false; }
    if (a.uiPaletteCandidates != b.uiPaletteCandidates) { field = "uiPaletteCandidates"; return false; }
    if (a.uiPaletteDecoded != b.uiPaletteDecoded) { field = "uiPaletteDecoded"; return false; }
    if (a.uiPaletteUnsupported != b.uiPaletteUnsupported) { field = "uiPaletteUnsupported"; return false; }
    if (a.uiSidebarResourceCandidates != b.uiSidebarResourceCandidates) { field = "uiSidebarResourceCandidates"; return false; }
    if (a.uiRadarResourceCandidates != b.uiRadarResourceCandidates) { field = "uiRadarResourceCandidates"; return false; }
    if (a.uiCameoResourceCandidates != b.uiCameoResourceCandidates) { field = "uiCameoResourceCandidates"; return false; }
    if (a.uiPipResourceCandidates != b.uiPipResourceCandidates) { field = "uiPipResourceCandidates"; return false; }
    if (a.uiPowerResourceCandidates != b.uiPowerResourceCandidates) { field = "uiPowerResourceCandidates"; return false; }
    if (a.uiFormat0Unsupported != b.uiFormat0Unsupported) { field = "uiFormat0Unsupported"; return false; }
    if (a.detailStatsEnabled != b.detailStatsEnabled) { field = "detailStatsEnabled"; return false; }
    if (a.coarseStageTimingEnabled != b.coarseStageTimingEnabled) { field = "coarseStageTimingEnabled"; return false; }
    if (a.sequenceFrameCacheHits != b.sequenceFrameCacheHits) { field = "sequenceFrameCacheHits"; return false; }
    if (a.sequenceFrameCacheMisses != b.sequenceFrameCacheMisses) { field = "sequenceFrameCacheMisses"; return false; }
    if (a.projectilePaletteCacheHits != b.projectilePaletteCacheHits) { field = "projectilePaletteCacheHits"; return false; }
    if (a.projectilePaletteCacheMisses != b.projectilePaletteCacheMisses) { field = "projectilePaletteCacheMisses"; return false; }
    if (a.auxiliaryPaletteCacheHits != b.auxiliaryPaletteCacheHits) { field = "auxiliaryPaletteCacheHits"; return false; }
    if (a.auxiliaryPaletteCacheMisses != b.auxiliaryPaletteCacheMisses) { field = "auxiliaryPaletteCacheMisses"; return false; }
    if (a.auxiliaryShpBytesCacheHits != b.auxiliaryShpBytesCacheHits) { field = "auxiliaryShpBytesCacheHits"; return false; }
    if (a.auxiliaryShpBytesCacheMisses != b.auxiliaryShpBytesCacheMisses) { field = "auxiliaryShpBytesCacheMisses"; return false; }
    if (a.auxiliaryShpFrameCacheHits != b.auxiliaryShpFrameCacheHits) { field = "auxiliaryShpFrameCacheHits"; return false; }
    if (a.auxiliaryShpFrameCacheMisses != b.auxiliaryShpFrameCacheMisses) { field = "auxiliaryShpFrameCacheMisses"; return false; }
    if (a.voxelCacheHits != b.voxelCacheHits) { field = "voxelCacheHits"; return false; }
    if (a.voxelCacheMisses != b.voxelCacheMisses) { field = "voxelCacheMisses"; return false; }
    if (a.buildingAnimationLayerCacheHits != b.buildingAnimationLayerCacheHits) { field = "buildingAnimationLayerCacheHits"; return false; }
    if (a.buildingAnimationLayerCacheMisses != b.buildingAnimationLayerCacheMisses) { field = "buildingAnimationLayerCacheMisses"; return false; }
    if (a.projectileTypeCacheHits != b.projectileTypeCacheHits) { field = "projectileTypeCacheHits"; return false; }
    if (a.projectileTypeCacheMisses != b.projectileTypeCacheMisses) { field = "projectileTypeCacheMisses"; return false; }
    if (a.auxiliaryEffectKindCacheHits != b.auxiliaryEffectKindCacheHits) { field = "auxiliaryEffectKindCacheHits"; return false; }
    if (a.auxiliaryEffectKindCacheMisses != b.auxiliaryEffectKindCacheMisses) { field = "auxiliaryEffectKindCacheMisses"; return false; }
    if (a.projectileShpAttempts != b.projectileShpAttempts) { field = "projectileShpAttempts"; return false; }
    if (a.projectileShpDrawn != b.projectileShpDrawn) { field = "projectileShpDrawn"; return false; }
    if (a.projectileShpSkippedInvisible != b.projectileShpSkippedInvisible) { field = "projectileShpSkippedInvisible"; return false; }
    if (a.projectileShpMissing != b.projectileShpMissing) { field = "projectileShpMissing"; return false; }
    if (a.projectilePaletteHits != b.projectilePaletteHits) { field = "projectilePaletteHits"; return false; }
    if (a.projectilePaletteFallbacks != b.projectilePaletteFallbacks) { field = "projectilePaletteFallbacks"; return false; }
    if (a.projectilePaletteStructureFallbacks != b.projectilePaletteStructureFallbacks) { field = "projectilePaletteStructureFallbacks"; return false; }
    if (a.projectileActorSnapshots != b.projectileActorSnapshots) { field = "projectileActorSnapshots"; return false; }
    if (a.missileExhaustDrawn != b.missileExhaustDrawn) { field = "missileExhaustDrawn"; return false; }
    if (a.missileExhaustHeadDrawn != b.missileExhaustHeadDrawn) { field = "missileExhaustHeadDrawn"; return false; }
    if (a.squidGrappleDrawn != b.squidGrappleDrawn) { field = "squidGrappleDrawn"; return false; }
    if (a.squidGrappleMissing != b.squidGrappleMissing) { field = "squidGrappleMissing"; return false; }
    if (a.squidSplashDrawn != b.squidSplashDrawn) { field = "squidSplashDrawn"; return false; }
    if (a.squidSplashMissing != b.squidSplashMissing) { field = "squidSplashMissing"; return false; }
    if (a.missileExhaustMissing != b.missileExhaustMissing) { field = "missileExhaustMissing"; return false; }
    if (a.projectileLineTrailSegments != b.projectileLineTrailSegments) { field = "projectileLineTrailSegments"; return false; }
    if (a.projectileLineTrailPixels != b.projectileLineTrailPixels) { field = "projectileLineTrailPixels"; return false; }
    if (a.projectileDirectionalSprites != b.projectileDirectionalSprites) { field = "projectileDirectionalSprites"; return false; }
    if (a.projectileActorTravelDrawn != b.projectileActorTravelDrawn) { field = "projectileActorTravelDrawn"; return false; }
    if (a.sonicWavesDrawn != b.sonicWavesDrawn) { field = "sonicWavesDrawn"; return false; }
    if (a.sonicWavePixels != b.sonicWavePixels) { field = "sonicWavePixels"; return false; }
    if (a.projectileActorVoxelDrawn != b.projectileActorVoxelDrawn) { field = "projectileActorVoxelDrawn"; return false; }
    if (a.projectileActorImageResolved != b.projectileActorImageResolved) { field = "projectileActorImageResolved"; return false; }
    if (a.projectileActorTravelMissing != b.projectileActorTravelMissing) { field = "projectileActorTravelMissing"; return false; }
    if (a.projectileActorPaletteMissing != b.projectileActorPaletteMissing) { field = "projectileActorPaletteMissing"; return false; }
    if (a.projectileActorDecodeMissing != b.projectileActorDecodeMissing) { field = "projectileActorDecodeMissing"; return false; }
    if (a.projectileActorExpired != b.projectileActorExpired) { field = "projectileActorExpired"; return false; }
    if (a.projectileActorCulled != b.projectileActorCulled) { field = "projectileActorCulled"; return false; }
    if (a.projectileActorInvisibleSkipped != b.projectileActorInvisibleSkipped) { field = "projectileActorInvisibleSkipped"; return false; }
    if (a.impactActorSnapshots != b.impactActorSnapshots) { field = "impactActorSnapshots"; return false; }
    if (a.impactActorDrawn != b.impactActorDrawn) { field = "impactActorDrawn"; return false; }
    if (a.impactActorHideIfNoOreSuppressed != b.impactActorHideIfNoOreSuppressed) { field = "impactActorHideIfNoOreSuppressed"; return false; }
    if (a.particleActorSnapshots != b.particleActorSnapshots) { field = "particleActorSnapshots"; return false; }
    if (a.particleActorOmitted != b.particleActorOmitted) { field = "particleActorOmitted"; return false; }
    if (a.particleActorShpAttempts != b.particleActorShpAttempts) { field = "particleActorShpAttempts"; return false; }
    if (a.particleActorShpDrawn != b.particleActorShpDrawn) { field = "particleActorShpDrawn"; return false; }
    if (a.particleActorFallbackDrawn != b.particleActorFallbackDrawn) { field = "particleActorFallbackDrawn"; return false; }
    if (a.particleActorMissing != b.particleActorMissing) { field = "particleActorMissing"; return false; }
    if (a.particleActorCulled != b.particleActorCulled) { field = "particleActorCulled"; return false; }
    if (a.visionHiddenParticleActors != b.visionHiddenParticleActors) { field = "visionHiddenParticleActors"; return false; }
    if (a.particleBeamSnapshots != b.particleBeamSnapshots) { field = "particleBeamSnapshots"; return false; }
    if (a.particleBeamsDrawn != b.particleBeamsDrawn) { field = "particleBeamsDrawn"; return false; }
    if (a.localLightSnapshots != b.localLightSnapshots) { field = "localLightSnapshots"; return false; }
    if (a.localLightsApplied != b.localLightsApplied) { field = "localLightsApplied"; return false; }
    if (a.localLightPixels != b.localLightPixels) { field = "localLightPixels"; return false; }
    if (a.radiationTintCells != b.radiationTintCells) { field = "radiationTintCells"; return false; }
    if (a.radiationTintPixels != b.radiationTintPixels) { field = "radiationTintPixels"; return false; }
    if (a.lastParticleFrame != b.lastParticleFrame) { field = "lastParticleFrame"; return false; }
    if (a.lastParticleImage != b.lastParticleImage) { field = "lastParticleImage"; return false; }
    if (a.particleSnapshotSignature != b.particleSnapshotSignature) { field = "particleSnapshotSignature"; return false; }
    if (a.projectileRenderIntentActors != b.projectileRenderIntentActors) { field = "projectileRenderIntentActors"; return false; }
    if (a.impactRenderIntentActors != b.impactRenderIntentActors) { field = "impactRenderIntentActors"; return false; }
    if (a.particleRenderIntentActors != b.particleRenderIntentActors) { field = "particleRenderIntentActors"; return false; }
    if (a.specialMechanismRenderActors != b.specialMechanismRenderActors) { field = "specialMechanismRenderActors"; return false; }
    if (a.renderIntentMissing != b.renderIntentMissing) { field = "renderIntentMissing"; return false; }
    if (a.renderIntentHash != b.renderIntentHash) { field = "renderIntentHash"; return false; }
    if (a.projectileTravelDrawn != b.projectileTravelDrawn) { field = "projectileTravelDrawn"; return false; }
    if (a.trailerAnimAttempts != b.trailerAnimAttempts) { field = "trailerAnimAttempts"; return false; }
    if (a.trailerAnimDrawn != b.trailerAnimDrawn) { field = "trailerAnimDrawn"; return false; }
    if (a.trailerAnimMissing != b.trailerAnimMissing) { field = "trailerAnimMissing"; return false; }
    if (a.impactAnimAttempts != b.impactAnimAttempts) { field = "impactAnimAttempts"; return false; }
    if (a.impactAnimDrawn != b.impactAnimDrawn) { field = "impactAnimDrawn"; return false; }
    if (a.impactAnimMissing != b.impactAnimMissing) { field = "impactAnimMissing"; return false; }
    if (a.weaponAnimAttempts != b.weaponAnimAttempts) { field = "weaponAnimAttempts"; return false; }
    if (a.weaponAnimDrawn != b.weaponAnimDrawn) { field = "weaponAnimDrawn"; return false; }
    if (a.weaponAnimMissing != b.weaponAnimMissing) { field = "weaponAnimMissing"; return false; }
    if (a.paletteAnimPaletteHits != b.paletteAnimPaletteHits) { field = "paletteAnimPaletteHits"; return false; }
    if (a.paletteFirersPaletteHits != b.paletteFirersPaletteHits) { field = "paletteFirersPaletteHits"; return false; }
    if (a.paletteExplicitHits != b.paletteExplicitHits) { field = "paletteExplicitHits"; return false; }
    if (a.paletteArtHits != b.paletteArtHits) { field = "paletteArtHits"; return false; }
    if (a.paletteColorHits != b.paletteColorHits) { field = "paletteColorHits"; return false; }
    if (a.paletteUnitFallbacks != b.paletteUnitFallbacks) { field = "paletteUnitFallbacks"; return false; }
    if (a.paletteIsoFallbacks != b.paletteIsoFallbacks) { field = "paletteIsoFallbacks"; return false; }
    if (a.paletteUnknownHits != b.paletteUnknownHits) { field = "paletteUnknownHits"; return false; }
    if (a.mapFactsAvailable != b.mapFactsAvailable) { field = "mapFactsAvailable"; return false; }
    if (a.mapFactOverlayCells != b.mapFactOverlayCells) { field = "mapFactOverlayCells"; return false; }
    if (a.mapFactResourceCells != b.mapFactResourceCells) { field = "mapFactResourceCells"; return false; }
    if (a.mapFactBridgeCells != b.mapFactBridgeCells) { field = "mapFactBridgeCells"; return false; }
    if (a.mapFactFallbackCells != b.mapFactFallbackCells) { field = "mapFactFallbackCells"; return false; }
    if (a.mapFactHits != b.mapFactHits) { field = "mapFactHits"; return false; }
    if (a.mapFactMisses != b.mapFactMisses) { field = "mapFactMisses"; return false; }
    if (a.terrainLayerCacheHits != b.terrainLayerCacheHits) { field = "terrainLayerCacheHits"; return false; }
    if (a.terrainLayerCacheMisses != b.terrainLayerCacheMisses) { field = "terrainLayerCacheMisses"; return false; }
    if (a.mapFactLookupCacheRebuilds != b.mapFactLookupCacheRebuilds) { field = "mapFactLookupCacheRebuilds"; return false; }
    return true;
}
