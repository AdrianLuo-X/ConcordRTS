static bool runtimeBuildingVisibleHitSpansEqualForContract(const RuntimeBuildingVisibleHitSpans& a,
    const RuntimeBuildingVisibleHitSpans& b) {
    if (a.size() != b.size()) return false;
    for (const auto& [id, spans] : a) {
        const auto found = b.find(id);
        if (found == b.end() || found->second.size() != spans.size()) return false;
        for (std::size_t index = 0; index < spans.size(); ++index) {
            const auto& other = found->second[index];
            if (spans[index].y != other.y || spans[index].left != other.left
                || spans[index].right != other.right) return false;
        }
    }
    return true;
}

#include "RuntimeBuildingDepthRowKernelContract.h"

static bool runRuntimeBuildingDepthPixelCopyContractCheck(std::string& error) {
    const auto fail = [&](const char* message) {
        error = std::string("building depth pixel ownership: ") + message; return false;
    };
    RuntimeMapRenderContext context;
    RuntimeMapCamera camera;
    RuntimeBuildingDepthObjectState object;
    object.instanceId = 77; object.center = {7, 9}; object.mobileVoxel = true;
    const RECT viewport{0, 0, 32, 32};
    std::vector<RuntimePaletteColor> palette(256);
    for (int index = 0; index != 256; ++index)
        palette[index] = {static_cast<unsigned char>(index),
            static_cast<unsigned char>(255 - index), static_cast<unsigned char>(index / 2)};
    struct CollectScope {
        RuntimeBuildingDepthFrameState* previousFrame = runtimeBuildingDepthFrame;
        const RuntimeBuildingDepthObjectState* previousObject = runtimeBuildingDepthObject;
        explicit CollectScope(RuntimeBuildingDepthFrameState& frame, const RuntimeBuildingDepthObjectState& object) {
            runtimeBuildingDepthFrame = &frame; runtimeBuildingDepthObject = &object;
        }
        ~CollectScope() { runtimeBuildingDepthFrame = previousFrame; runtimeBuildingDepthObject = previousObject; }
    };
    for (int marker = 0; marker != 3; ++marker) for (bool compose : {false, true}) {
        std::vector<std::uint32_t> expectedColors;
        for (bool reference : {true, false}) {
            RuntimeBuildingDepthPixelCopyReferenceScope copyMode(reference);
            RuntimeShpFrameImage image;
            image.width = image.globalWidth = 9; image.height = image.globalHeight = 5;
            image.offsetX = -2; image.offsetY = 1;
            image.pixels.resize(45); image.ownerRemapMask.resize(45);
            for (int index = 0; index != 45; ++index) {
                image.pixels[index] = index % 7 ? static_cast<unsigned char>(index + 1) : 0;
                image.ownerRemapMask[index] = index % 3 == 0;
            }
            if (marker == 0) image.rasterCacheKey = "__temporary_or_evicted_pose__";
            if (marker == 1) image.depthLevels.assign(45, 127);
            if (marker == 2) image.rasterFunctionActuallyCalled = "__uncached_raster__";
            const auto expectedPixels = image.pixels;
            RuntimeBuildingDepthFrameState frame;
            frame.geometryOnly = frame.collectOnly = true; frame.composeColors = compose;
            CollectScope collection(frame, object);
            if (!drawRuntimeShpFrame(nullptr, viewport, object.center, context, camera, image,
                palette, true, RGB(30, 80, 170), 7, false, false, 128) || frame.draws.size() != 1)
                return fail("collection did not retain the VXL part");
            auto& draw = frame.draws.front();
            const bool alias = compose && !reference;
            if (!draw.pinnedPixels || *draw.pinnedPixels != expectedPixels
                || draw.pinnedPixels.get() == &image.pixels
                || (draw.pinnedPixels.get() == &draw.image->pixels) != alias
                || (draw.image != &image) != alias || static_cast<bool>(draw.colors) != compose)
                return fail("alias/original-copy branch differs");
            // Deferred replay must survive loss of every source vector and pose
            // field, with no borrowed LRU, temporary or uncached image address.
            image = RuntimeShpFrameImage{};
            if (*draw.pinnedPixels != expectedPixels) return fail("source mutation changed coverage");
            if (compose) {
                RuntimeBuildingDepthColorSampleScope isolated;
                const auto colors = draw.colors();
                if (reference) expectedColors = colors;
                else if (colors != expectedColors) return fail("deferred palette/remap pixels differ");
            }
            auto retained = draw.pinnedPixels;
            frame.draws.clear();
            if (*retained != expectedPixels) return fail("alias did not independently own the complete snapshot");
        }
    }
    return true;
}

static bool runRuntimePaintBuildingDepthReuseContractCheck(std::string& error) {
    if (!runRuntimeBuildingDepthRowKernelContractCheck(error)) return false;
    if (!runRuntimeBuildingDepthPixelCopyContractCheck(error)) return false;
    const auto fail = [&](const char* message) { error = std::string("paint building depth reuse: ") + message; return false; };
    RuntimePaintGeometryReferenceScope bodyMode(false);
    RuntimePaintBuildingDepthReferenceScope depthMode(false);
    RuntimeBuildingDepthBufferReferenceScope bufferMode(false);
    RuntimeEmptyBuildingDepthReferenceScope emptyMode(false);
    GameWorld world;
    RuntimeMapRenderCache cache, otherCache;
    RuntimeMapRenderContext context;
    context.stableRenderConfiguration = true; context.animationTick = 31;
    context.captureBuildingDepthHitSpans = true;
    RuntimeMapCamera camera; camera.initialized = true;
    const RECT viewport{2, 3, 14, 11};
    std::vector<std::uint32_t> surface(16 * 12);
    context.surfacePixels = surface.data(); context.surfaceWidth = 16; context.surfaceHeight = 12;
    RuntimeShpFrameImage shape; shape.width = shape.height = 1; shape.pixels = {70};
    std::vector<RuntimeShpFrameImage> images(6);
    RuntimeBuildingDepthFrameState paint, query;
    for (auto* frame : {&paint, &query}) {
        RuntimeBuildingDepthRasterReferenceScope rasterMode(frame == &query);
        frame->viewport = viewport; frame->width = 12; frame->zShape = &shape;
        frame->pixels.resize(12 * 8); frame->composeColors = frame == &paint;
        if (frame->composeColors) frame->frozenOwners.insert(11);
        for (int layer = 0; layer != 6; ++layer) {
            auto& image = images[layer]; image.width = 8; image.height = 6;
            image.pixels.assign(48, 1);
            for (int pixel = layer; pixel < 48; pixel += 7) image.pixels[pixel] = 0;
            RuntimeBuildingDepthDraw draw;
            draw.image = &image; draw.rect = {viewport.left + layer % 3 - 1,
                viewport.top + layer % 2 - 1, viewport.left + layer % 3 + 7, viewport.top + layer % 2 + 5};
            draw.instanceId = 10 + layer; draw.ySort = layer; draw.sequence = layer;
            draw.opacity = layer == 1 ? 128 : 255;
            draw.description.width = 8; draw.description.height = 6;
            draw.description.canvasWidth = 8; draw.description.canvasHeight = 6;
            draw.description.foundationWidth = 8; draw.description.foundationHeight = 1;
            draw.description.nativeFrameTopY = layer;
            draw.description.layer = static_cast<RuntimeBuildingDepthLayer>(layer);
            draw.colors = [layer] { return std::vector<std::uint32_t>(48, 0x00206090U + layer); };
            frame->draws.push_back(std::move(draw));
            frame->participatingIds.insert(10 + layer);
        }
        frame->participatingIds.insert(999); // Preserve fully occluded/empty entries.
        resolveRuntimeBuildingDepthDraws(*frame);
    }
    RuntimeBuildingVisibleHitSpans expected;
    if (!extractRuntimeBuildingVisibleHitSpans(query, expected, context.animationTick)) return fail("reference extraction");
    for (std::size_t index = 0; index < paint.pixels.size(); ++index)
        if (paint.pixels[index].visibleOwner != query.pixels[index].visibleOwner
            || paint.pixels[index].depthOwner != query.pixels[index].depthOwner
            || paint.pixels[index].depth != query.pixels[index].depth)
            return fail("color/alpha/frozen composition changed ownership");
    auto compact = query;
    compact.requestedViewport = viewport; compact.compactOwnershipBuffer = true;
    compact.resolved = false; compact.pixels.clear();
    resolveRuntimeBuildingDepthDraws(compact);
    RuntimeBuildingVisibleHitSpans compactSpans;
    if (compact.pixels.size() >= query.pixels.size()
        || !extractRuntimeBuildingVisibleHitSpans(compact, compactSpans, context.animationTick)
        || !runtimeBuildingVisibleHitSpansEqualForContract(compactSpans, expected))
        return fail("compact bounds did not preserve complete hit result");
    for (LONG y = viewport.top; y < viewport.bottom; ++y) for (LONG x = viewport.left; x < viewport.right; ++x) {
        const auto& original = query.pixels[static_cast<std::size_t>(y - viewport.top) * query.width + x - viewport.left];
        RuntimeBuildingDepthPixel value;
        if (x >= compact.viewport.left && x < compact.viewport.right
            && y >= compact.viewport.top && y < compact.viewport.bottom)
            value = compact.pixels[static_cast<std::size_t>(y - compact.viewport.top) * compact.width + x - compact.viewport.left];
        if (original.depth != value.depth || original.depthOwner != value.depthOwner
            || original.visibleOwner != value.visibleOwner) return fail("compact ownership differs from full viewport");
    }
    auto diagnostic = query;
    diagnostic.requestedViewport = viewport; diagnostic.compactOwnershipBuffer = true;
    diagnostic.participatingIds.insert(-1); diagnostic.pixels.clear(); diagnostic.resolved = false;
    resolveRuntimeBuildingDepthDraws(diagnostic);
    if (!EqualRect(&diagnostic.viewport, &viewport) || diagnostic.pixels.size() != query.pixels.size())
        return fail("background-sentinel diagnostic was cropped");
    {
        RuntimeGeometryQueryScope outer(world, viewport, context, camera, cache);
        auto* owner = RuntimeGeometryQueryFrame::active;
        {
            RuntimeGeometryQueryScope map(world, viewport, context, camera, cache);
            if (runtimePaintBuildingDepthMemo(world, viewport, context, camera, cache) != owner)
                return fail("compatible publisher did not share owner");
            publishRuntimePaintBuildingVisibleHitSpans(*owner, paint, context.animationTick, cache);
        }
        // Destroying or changing the source frame cannot change value-owned spans.
        paint.pixels.clear(); paint.draws.clear(); paint.participatingIds.clear();
        RuntimeBuildingVisibleHitSpans actual;
        if (!findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual)
            || !runtimeBuildingVisibleHitSpansEqualForContract(actual, expected)
            || !actual.contains(999) || !actual.at(999).empty()) return fail("completed value result differs");
        actual.clear();
        if (!findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual)
            || !runtimeBuildingVisibleHitSpansEqualForContract(actual, expected)) return fail("consumer moved cached result");
        for (int variant = 0; variant != 9; ++variant) {
            auto changed = context;
            switch (variant) {
                case 0: ++changed.animationTick; break;
                case 1: changed.effectOnlyProbe = true; break;
                case 2: changed.surfacePixels = nullptr; break;
                case 3: changed.surfaceWidth = viewport.right - 1; break;
                case 4: changed.seeHiddenObjects = false; break;
                case 5: changed.visionObserverOwner = "other"; break;
                case 6: changed.stableRenderConfiguration = false; break;
                case 7: changed.chaosBodyTint = true; break;
                case 8: changed.assetRootPath = "other"; break;
            }
            if (findRuntimePaintBuildingVisibleHitSpans(world, viewport, changed, camera, cache, actual))
                return fail("incompatible context borrowed result");
        }
        auto moved = camera; ++moved.worldOffsetX;
        auto clipped = viewport; ++clipped.left;
        GameWorld otherWorld;
        if (findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, moved, cache, actual)
            || findRuntimePaintBuildingVisibleHitSpans(world, clipped, context, camera, cache, actual)
            || findRuntimePaintBuildingVisibleHitSpans(otherWorld, viewport, context, camera, cache, actual)
            || findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, otherCache, actual))
            return fail("camera/viewport/world/cache identity changed");
        {
            RuntimePaintBuildingDepthReferenceScope reference(true);
            if (findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual))
                return fail("independent reference borrowed result");
        }
        {
            RuntimePaintGeometryReferenceScope reference(true);
            if (findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual))
                return fail("body reference borrowed depth result");
        }
        {
            struct ExternalDepth { RuntimeBuildingDepthFrameState* previous = runtimeBuildingDepthFrame;
                explicit ExternalDepth(RuntimeBuildingDepthFrameState& frame) { runtimeBuildingDepthFrame = &frame; }
                ~ExternalDepth() { runtimeBuildingDepthFrame = previous; } } external(query);
            if (findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual))
                return fail("external scene-Z borrowed result");
        }
        {
            RuntimeSpriteGeometryCapture capture;
            RuntimeSpriteGeometryScope geometry(&capture);
            if (findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual))
                return fail("geometry capture borrowed result");
        }
        RuntimeBuildingVisibleHitSpans limited;
        if (extractRuntimeBuildingVisibleHitSpans(query, limited, context.animationTick, 0) || !limited.empty())
            return fail("bounded extraction did not fall back");
        RuntimeBuildingDepthFrameState empty;
        empty.viewport = viewport; empty.width = 12; empty.zShape = &shape;
        empty.pixels.resize(12 * 8); empty.resolved = true;
        publishRuntimePaintBuildingVisibleHitSpans(*owner, empty, context.animationTick, cache);
        if (!findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual)
            || !actual.empty()) return fail("valid empty result lost success contract");
        auto zeroBuffer = empty;
        zeroBuffer.requestedViewport = viewport; zeroBuffer.compactOwnershipBuffer = true;
        zeroBuffer.resolved = false; zeroBuffer.participatingIds.insert(999);
        resolveRuntimeBuildingDepthDraws(zeroBuffer);
        if (!zeroBuffer.pixels.empty()
            || !extractRuntimeBuildingVisibleHitSpans(zeroBuffer, actual, context.animationTick)
            || actual.size() != 1 || !actual.contains(999) || !actual.at(999).empty())
            return fail("empty compact scene lost participant contract");
    }
    RuntimeGeometryQueryScope nextPaint(world, viewport, context, camera, cache);
    RuntimeBuildingVisibleHitSpans actual;
    if (findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual))
        return fail("result escaped paint lifetime");
    return true;
}

static bool runRuntimePaintBuildingDepthReuseAssetContractCheck(const GameWorld& sourceWorld,
    const RuntimeMapRenderContext& source, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "paint building depth reuse assets: " + message; return false; };
    RuntimePaintGeometryReferenceScope bodyMode(false);
    RuntimeEmptyBuildingDepthReferenceScope emptyMode(false);
    GameWorld world = sourceWorld;
    world.accessObjects().clear(); world.accessTerrainObjects().clear(); world.accessSmudges().clear();
    RuntimeMapRenderContext context = source;
    context.stableRenderConfiguration = true; context.captureBuildingDepthHitSpans = true;
    context.vision = nullptr; context.visionObserverOwner.clear(); context.seeHiddenObjects = true;
    context.specialMechanismSnapshots = nullptr; context.gateSnapshots = nullptr;
    context.projectileActors = nullptr; context.impactActors = nullptr; context.wrecks = nullptr;
    context.buildingCollapses = nullptr; context.particleSnapshots = nullptr;
    context.particleBeamSnapshots = nullptr; context.localLightSnapshots = nullptr;
    context.selectedObjectIds = nullptr; context.playerCheerUntilTick = nullptr;
    context.uiResources = nullptr; context.lightingTint = nullptr; context.lightingProfile = nullptr;
    context.detailStatsEnabled = false; context.coarseStageTimingEnabled = false;
    context.useTileTextures = false; context.effectOnlyProbe = false;
    RuntimeMapRenderCache optimized, reference;
    WorldObjectInstance ground;
    ground.instanceId = 940004; ground.type = "MTNK"; ground.kind = WorldObjectKind::Unit;
    ground.alive = true; ground.health = 1000; ground.owner = "Americans"; ground.mission = "Guard";
    ground.x = ground.y = 20;
    // Retain the supplied terrain/facts and look for the actual flat Track/Wheel
    // eligibility. Synthetic coverage above separately exercises every layer.
    bool groundEligible = false;
    std::size_t examined = 0;
    for (const auto& cell : world.getTerrainCells()) {
        if (++examined > 2048) break;
        ground.x = cell.x; ground.y = cell.y; ground.level = cell.level;
        if (runtimeGroundVehicleUsesBuildingDepth(ground, world, context, optimized)) {
            groundEligible = true; break;
        }
    }
    const auto add = [&](const char* type, WorldObjectKind kind, int x, int y) {
        WorldObjectInstance object;
        object.instanceId = 940000 + static_cast<int>(world.getObjects().size());
        object.type = type; object.kind = kind; object.owner = "Americans";
        object.x = x; object.y = y; object.level = ground.level; object.cell = y * 512 + x;
        object.alive = object.poweredOperational = true; object.health = 1000; object.mission = "Guard";
        object.facing = 64; object.turretFacing = 160;
        world.accessObjects().push_back(object);
    };
    add("GAPOWR", WorldObjectKind::Structure, ground.x, ground.y);
    add("GAWEAP", WorldObjectKind::Structure, ground.x + 1, ground.y);
    add("NATBNK", WorldObjectKind::Structure, ground.x, ground.y + 1);
    add("GAYARD", WorldObjectKind::Structure, ground.x + 1, ground.y + 1);
    world.accessObjects().push_back(ground);
    add("DRED", WorldObjectKind::Unit, ground.x, ground.y);
    add("SHAD", WorldObjectKind::Unit, ground.x + 1, ground.y + 1);
    add("MTNK", WorldObjectKind::Unit, ground.x, ground.y + 1);
    auto& objects = world.accessObjects();
    objects[0].workAnimationActive = true;
    objects[6].airborne = objects[6].authoredAircraft = true;
    objects[6].flightLevel = objects[6].level = 4;
    objects[6].factoryExitHostId = objects[1].instanceId;
    objects[6].factoryExitAirborne = true; objects[1].factoryExitAirborne = true;
    objects[7].tankBunkerHostId = objects[2].instanceId;
    objects[2].tankBunkerOccupantId = objects[7].instanceId;
    const auto baseline = world.getObjects();
    constexpr int width = 640, height = 480;
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
    if (!dc || !bitmap || !raw) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return fail("surface allocation");
    }
    const HGDIOBJ old = SelectObject(dc, bitmap);
    struct Cleanup { HDC dc; HBITMAP bitmap; HGDIOBJ old;
        ~Cleanup() { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); } } cleanup{dc, bitmap, old};
    auto* pixels = static_cast<std::uint32_t*>(raw);
    context.surfacePixels = pixels; context.surfaceWidth = width; context.surfaceHeight = height;
    std::vector<std::uint32_t> expectedPixels;
    RuntimeBuildingVisibleHitSpans expectedSpans;
    bool expectedResult = false;
    std::vector<SpecialMechanismRenderSnapshot> frozen;
    std::vector<RuntimeBuildingCollapseState> collapses;
    std::uint64_t reused = 0;
    for (int variant = 0; variant != 9; ++variant) {
        world.accessObjects() = baseline;
        context.animationTick = source.animationTick + variant + 1;
        frozen.clear(); collapses.clear();
        context.specialMechanismSnapshots = &frozen; context.buildingCollapses = &collapses;
        context.captureBuildingDepthHitSpans = variant != 7;
        context.surfacePixels = variant == 8 ? nullptr : pixels;
        context.surfaceWidth = variant == 8 ? 0 : width;
        context.surfaceHeight = variant == 8 ? 0 : height;
        world.accessObjects()[1].factoryExitAnimationStartTick = context.animationTick - 10;
        world.accessObjects()[1].factoryExitAnimationEndTick = context.animationTick + 30;
        world.accessObjects()[6].factoryExitAnimationStartTick = context.animationTick - 10;
        world.accessObjects()[6].factoryExitAnimationEndTick = context.animationTick + 30;
        if (variant == 1) world.accessObjects()[0].cloaked = true;
        if (variant == 2) {
            SpecialMechanismRenderSnapshot snapshot; snapshot.kind = "temporal";
            snapshot.targetObjectId = baseline[0].instanceId;
            snapshot.requiredProgress = 10000; snapshot.progress = 100;
            frozen.push_back(snapshot);
        }
        if (variant == 3) {
            auto& sold = world.accessObjects()[0]; sold.alive = false; sold.mission = "Sold";
            sold.buildAnimTicksTotal = 10; sold.buildAnimTicksRemaining = 5;
        }
        if (variant == 4) {
            auto& destroyed = world.accessObjects()[0]; destroyed.alive = false; destroyed.health = 0;
            RuntimeBuildingCollapseState collapse; collapse.objectId = destroyed.instanceId;
            collapse.animId = "GAPOWRDM"; collapse.startTick = context.animationTick; collapse.frameCount = 8;
            collapses.push_back(collapse);
        }
        if (variant == 6) for (auto& object : world.accessObjects()) object.x = object.y = 1000000;
        const RECT viewport = variant == 5 ? RECT{17, 11, 483, 349} : RECT{0, 0, width, height};
        RuntimeMapCamera camera; camera.initialized = true;
        TerrainCell anchor; anchor.x = ground.x; anchor.y = ground.y; anchor.level = ground.level;
        const auto projected = projectCell(anchor, camera);
        camera.worldOffsetX = projected.x - (viewport.right - viewport.left) / 2;
        camera.worldOffsetY = projected.y - 160;
        // Compare the complete original path, all accelerated paths, and the
        // original copies alone with every other acceleration still enabled.
        for (int pass = 0; pass != 3; ++pass) {
            const bool fast = pass != 0;
            RuntimePaintBuildingDepthReferenceScope mode(!fast);
            RuntimeBuildingDepthBufferReferenceScope bufferMode(!fast);
            RuntimeBuildingDepthRasterReferenceScope rasterMode(!fast);
            RuntimeBuildingDepthPixelCopyReferenceScope copyMode(pass != 1);
            RuntimePaintTerrainTileReferenceScope terrainMode(!fast);
            RuntimeMapRenderCache& cache = fast ? optimized : reference;
            const auto recordsBefore = cache.geometryBuildingDepthPaintRecords;
            const auto hitsBefore = cache.geometryBuildingDepthMemoHits;
            RuntimeGeometryQueryScope paint(world, viewport, context, camera, cache);
            std::fill_n(pixels, static_cast<std::size_t>(width) * height, 0xa1407199U);
            auto drawCamera = camera;
            drawRuntimeMap(dc, viewport, world, context, drawCamera, cache);
            GdiFlush();
            if (!fast) expectedPixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height);
            else if (!std::equal(expectedPixels.begin(), expectedPixels.end(), pixels))
                return fail("map pixels differ on variant " + std::to_string(variant));
            RuntimeBuildingVisibleHitSpans actual;
            const bool result = resolveRuntimeBuildingVisibleHitSpans(world, viewport, context, camera, cache, actual);
            if (!fast) { expectedResult = result; expectedSpans = actual; }
            else {
                if (result != expectedResult || !runtimeBuildingVisibleHitSpansEqualForContract(actual, expectedSpans))
                    return fail("visible key/spans differ on variant " + std::to_string(variant));
                if (variant != 7 && variant != 8) {
                    if (cache.geometryBuildingDepthPaintRecords != recordsBefore + 1
                        || cache.geometryBuildingDepthMemoHits != hitsBefore + 1)
                        return fail("map-to-hit reuse was not exercised on variant " + std::to_string(variant));
                    ++reused;
                } else if (cache.geometryBuildingDepthPaintRecords != recordsBefore
                    || cache.geometryBuildingDepthMemoHits != hitsBefore)
                    return fail("paint without a query request did extra capture work");
                RuntimeBuildingVisibleHitSpans direct;
                {
                    RuntimePaintBuildingDepthReferenceScope bypass(true);
                    const bool directResult = resolveRuntimeBuildingVisibleHitSpans(world, viewport, context, camera, cache, direct);
                    if (result != directResult || !runtimeBuildingVisibleHitSpansEqualForContract(actual, direct))
                        return fail("independent original replay differs");
                }
                if (variant == 0 && groundEligible && !actual.contains(ground.instanceId))
                    return fail("eligible overlapping ground vehicle did not participate");
                if (variant == 0 && (!actual.contains(baseline[5].instanceId)
                    || actual.contains(baseline[6].instanceId)))
                    return fail("naval/air eligibility changed");
                if (variant == 4 && actual.contains(baseline[0].instanceId))
                    return fail("active collapse retained normal depth participation");
                if (variant == 6 && !actual.empty()) return fail("offscreen scene lost valid empty result");
                if (variant == 6 && cache.lastBuildingDepthBufferPixels != 0)
                    return fail("offscreen structure scene retained whole viewport buffer");
            }
        }
        RuntimeBuildingVisibleHitSpans afterPaint;
        const auto hits = optimized.geometryBuildingDepthMemoHits;
        {
            RuntimePaintBuildingDepthReferenceScope normal(false);
            resolveRuntimeBuildingVisibleHitSpans(world, viewport, context, camera, optimized, afterPaint);
        }
        if (optimized.geometryBuildingDepthMemoHits != hits
            || !runtimeBuildingVisibleHitSpansEqualForContract(afterPaint, expectedSpans))
            return fail("post-paint query retained borrowed state");
    }
    return reused == 14 || fail("reuse fixture was not exercised");
}
