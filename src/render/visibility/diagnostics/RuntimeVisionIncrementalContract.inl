bool runRuntimeVisionIncrementalContractCheck(std::string& error) {
    std::uint32_t randomState = 0x710ce3a5U;
    const auto random = [&]() {
        randomState ^= randomState << 13; randomState ^= randomState >> 17;
        randomState ^= randomState << 5; return randomState;
    };
    int kernelCases = 0;
    for (const auto [width, height] : std::array<std::pair<int, int>, 8>{{
            {1,1}, {2,5}, {5,2}, {63,65}, {64,64}, {65,67}, {129,131}, {320,240}}}) {
        for (int pattern = 0; pattern < 4; ++pattern) {
            const int stride = width + 9;
            const RECT viewport{3, 2, width + 3, height + 2};
            const std::size_t count = static_cast<std::size_t>(stride) * (height + 7);
            std::vector<unsigned char> mask(count);
            std::vector<std::uint32_t> actual(count), expected(count);
            for (std::size_t i = 0; i < count; ++i) {
                mask[i] = pattern == 0 ? 254 : pattern == 1 ? 255
                    : pattern == 2 ? (random() % 3 ? 254 : 0)
                    : (random() % 3 == 0 ? 0 : random() % 2 ? 254 : 255);
                actual[i] = expected[i] = random();
            }
            std::vector<unsigned char> horizontal(count), softened(count);
            runtimeReferenceHorizontalShroudBlur(mask, horizontal, stride,
                viewport.left, viewport.top, viewport.right, viewport.bottom);
            runtimeReferenceVerticalShroudBlur(horizontal, softened, stride,
                viewport.left, viewport.top, viewport.right, viewport.bottom);
            RuntimeMapCamera camera; camera.worldOffsetX = -129; camera.worldOffsetY = -259;
            RuntimeVisionOverlayCache cache;
            const RuntimeVisionGridView emptyView;
            const RuntimeTerrainLayerCache emptyLayer;
            const GameWorld emptyWorld;
            if (!runtimeBuildIncrementalVisionOverlay(cache, emptyView, emptyLayer,
                    emptyWorld, viewport, camera, stride, height + 7, mask)) {
                error = "incremental shroud kernel did not initialize"; return false;
            }
            for (const auto& tile : cache.incremental.tiles)
                runtimeApplyVisionEncodedPixels(tile.encodedPixels, actual.data());
            const auto& colors = runtimeVisionShroudColors();
            std::size_t shaded = 0;
            for (int y = viewport.top; y < viewport.bottom; ++y)
                for (int x = viewport.left; x < viewport.right; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y) * stride + x;
                    if (!mask[index]) continue;
                    const auto& color = colors[static_cast<std::size_t>((y - viewport.top
                        + camera.worldOffsetY) & 127) * 64
                        + ((x - viewport.left + camera.worldOffsetX) & 63)];
                    const bool solid = mask[index] == 255 || softened[index] >= 220;
                    expected[index] = solid ? color.color : runtimeBlendVisionShroud(expected[index],
                        color.color, std::clamp(int(softened[index]) + color.mottle, 0, 250));
                    ++shaded;
                }
            if (actual != expected || shaded != cache.incremental.encodedPixelCount) {
                error = "incremental shroud tiled kernel pixels differ: " + std::to_string(width)
                    + "x" + std::to_string(height) + " pattern=" + std::to_string(pattern);
                return false;
            }
            ++kernelCases;
        }
    }

    GameWorld world;
    for (int y = 0; y <= 40; ++y) for (int x = 0; x <= 40; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.level = (x * 13 + y * 7) % 5;
        world.accessTerrainCells().push_back(cell);
    }
    IniFile ini; RulesDatabase rules;
    if (!ini.loadFromString("[InfantryTypes]\n0=__TILE_SCOUT__\n[BuildingTypes]\n0=__TILE_GAP__\n"
            "[__TILE_SCOUT__]\nSight=2\nStrength=100\n"
            "[__TILE_GAP__]\nSight=1\nStrength=500\nGapGenerator=yes\nGapRadiusInCells=3\nPowered=yes\n")
        || !rules.loadFromIni(ini)) {
        error = "incremental shroud fixture rules did not load"; return false;
    }
    WorldObjectInstance scout; scout.instanceId = 1; scout.kind = WorldObjectKind::Infantry;
    scout.type = "__TILE_SCOUT__"; scout.owner = "Observer"; scout.x = scout.y = 4;
    scout.health = 100; scout.alive = true;
    world.accessObjects().push_back(scout);
    WorldObjectInstance gap; gap.instanceId = 2; gap.kind = WorldObjectKind::Structure;
    gap.type = "__TILE_GAP__"; gap.owner = "Enemy"; gap.x = gap.y = 20;
    gap.alive = true; gap.health = 500; gap.poweredOperational = false;
    world.accessObjects().push_back(gap);
    RuntimeVisionSystem vision;
    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"Observer", {"Observer"}}, {"Enemy", {"Enemy"}}};
    vision.update(world, &rules, observers, 1);
    RuntimeMapRenderContext context;
    context.vision = &vision; context.visionObserverOwner = "Observer"; context.rules = &rules;
    context.surfaceWidth = 512; context.surfaceHeight = 384;
    RuntimeMapCamera camera; camera.initialized = true;
    camera.tileWidth = 12; camera.tileHeight = 6;
    camera.worldOffsetX = -256; camera.worldOffsetY = -20;
    RECT viewport{3, 5, 509, 381};
    RuntimeMapRenderCache cache;
    auto& terrain = cache.terrainLayer;
    terrain.built = true; terrain.key = "incremental-shroud-owned-TMP";
    terrain.width = 512; terrain.height = 384;
    terrain.originX = camera.worldOffsetX; terrain.originY = camera.worldOffsetY;
    terrain.pixels.assign(512 * 384, 0x00ffffffU);
    terrain.terrainCellIndices.assign(512 * 384, -1);
    for (int y = 0; y < 384; ++y) for (int x = 0; x < 512; ++x)
        if ((x / 23 + y / 19) % 5 == 0)
            terrain.terrainCellIndices[y * 512 + x] = ((y / 12) % 41) * 512 + (x / 16) % 41;
    // This owner is far from its geometric diamond and crosses tile edges.
    for (int x = 58; x < 134; ++x) terrain.terrainCellIndices[311 * 512 + x] = 40 * 512 + 40;
    int paint = 0;
    int partialUpdates = 0;
    const auto check = [&](const char* label, RuntimeMapRenderCache& actualCache) {
        const std::size_t count = static_cast<std::size_t>(context.surfaceWidth) * context.surfaceHeight;
        std::vector<std::uint32_t> actual(count), expected(count);
        ++paint;
        for (std::size_t i = 0; i < count; ++i) actual[i] = expected[i] = random();
        RuntimeMapRenderCache reference; reference.terrainLayer = actualCache.terrainLayer;
        RuntimeMapRenderStats a, b;
        {
            RuntimeFrameScalingReferenceScope optimized(false);
            context.surfacePixels = actual.data();
            applyRuntimeVisionOverlay(nullptr, viewport, world, context, camera, actualCache, a);
        }
        {
            RuntimeFrameScalingReferenceScope original(true);
            context.surfacePixels = expected.data();
            applyRuntimeVisionOverlay(nullptr, viewport, world, context, camera, reference, b);
        }
        if (actual != expected || a.visionVisibleCells != b.visionVisibleCells
            || a.visionFoggedCells != b.visionFoggedCells || a.visionShroudedCells != b.visionShroudedCells
            || a.visionOverlayPixels != b.visionOverlayPixels) {
            const auto different = std::mismatch(actual.begin(), actual.end(), expected.begin());
            error = std::string("incremental shroud pixels/counts differ: ") + label
                + " pixel=" + std::to_string(different.first - actual.begin());
            return false;
        }
        const auto& incremental = actualCache.visionOverlay.incremental;
        const std::uint64_t area = static_cast<std::uint64_t>(std::max(0, incremental.right - incremental.left))
            * std::max(0, incremental.bottom - incremental.top);
        if (a.visionOverlayCacheMisses && incremental.rebuiltMaskPixels > 0
            && incremental.rebuiltEffectPixels < area) ++partialUpdates;
        return true;
    };
    if (!check("cold", cache) || !check("warm", cache)) return false;
    for (int tick = 2; tick <= 25; ++tick) {
        auto& moving = world.accessObjects()[0];
        moving.x = 4 + tick % 11; moving.y = 4 + tick % 7;
        auto& movingGap = world.accessObjects()[1];
        movingGap.poweredOperational = tick % 4 != 0;
        movingGap.x = 17 + tick % 5; movingGap.y = 17 + tick % 3;
        vision.update(world, &rules, observers, tick);
        vision.exploreArea("Observer", 18 + tick % 13, 17 + tick % 11, 1);
        if (tick % 3 == 0) vision.resetShroudArea("Observer", 29, 27, 2);
        if (!check("moving sight, exploration and reversible Gap", cache)) return false;
    }
    vision.exploreArea("Observer", 40, 40, 1);
    if (!check("distant TMP owner and tile-crossing overhang", cache)) return false;
    RuntimeMapRenderCache copied;
    copied.terrainLayer = cache.terrainLayer; copied.visionOverlay = cache.visionOverlay;
    const auto keptMask = cache.visionOverlay.incremental.mask;
    vision.resetShroudArea("Observer", 40, 40, 1);
    if (!check("copied cache", copied) || cache.visionOverlay.incremental.mask != keptMask
        || !check("original after copied cache", cache)) {
        if (error.empty()) error = "incremental shroud cache copies share mutable state";
        return false;
    }
    context.visionObserverOwner = "Enemy";
    if (!check("owner switched", cache)) return false;
    context.visionObserverOwner = "Observer";
    if (!check("owner restored", cache)) return false;
    camera.worldOffsetX -= 7; camera.worldOffsetY += 9;
    if (!check("cold camera", cache) || !check("warm camera", cache)) return false;
    context.surfaceWidth = 533; context.surfaceHeight = 401;
    viewport = RECT{-7, -3, 520, 395};
    if (!check("resize and negative clipped viewport", cache)) return false;
    cache.terrainLayer.key += "-replaced";
    cache.terrainLayer.terrainCellIndices[311 * 512 + 63] = 4 * 512 + 4;
    if (!check("TMP ownership replacement", cache)) return false;
    cache.terrainLayer.built = false;
    if (!check("terrain layer removed", cache)) return false;
    camera.tileWidth = 18; camera.tileHeight = 9;
    if (!check("tile scale changed", cache)) return false;
    viewport = RECT{80, 20, 81, 25};
    if (!check("one-pixel viewport", cache)) return false;
    viewport = RECT{0, 0, 533, 401};
    vision.reset(); vision.update(world, &rules, observers, 30);
    if (!check("vision storage replaced", cache)) return false;
    if (!runtimeMovementReferenceForRender() && partialUpdates == 0) {
        error = "incremental shroud fixture did not prove partial pixel work"; return false;
    }
    std::cout << "Incremental shroud: kernelCases=" << kernelCases << " paints=" << paint
        << " partialUpdates=" << partialUpdates << " pixelsAndCounts=exact\n";
    return true;
}
