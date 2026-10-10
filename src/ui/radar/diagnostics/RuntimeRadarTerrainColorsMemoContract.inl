// Include in RuntimeShell after drawRuntimeRadarTerrainCache and
// seedRuntimeLoadScratch. The reference calls the production resolver/drawer.
#include "ui/radar/RuntimeRadarTerrainColorsMemo.h"
#include "ui/radar/diagnostics/RuntimeRadarSampledTerrainContract.inl"

static bool runRuntimeRadarTerrainColorsMemoPrimitiveContract(std::string& error) {
    const auto fail = [&](const char* detail) {
        error = std::string("radar terrain color memo: ") + detail; return false;
    };
    TheaterDatabase theater;
    RuntimeMapRenderContext context;
    context.theater = &theater;
    context.tileRootPath = "__radar_tiles__";
    context.assetRootPath = "__radar_assets__";
    context.palettePath = "__radar_palette__";
    const auto makeCache = [&] {
        RuntimeMapRenderCache cache;
        cache.loadedPalettePath = context.palettePath;
        cache.paletteLoadAttempted = cache.paletteLoaded = true;
        cache.palette.resize(256);
        cache.terrainTilePathCacheRoot = context.tileRootPath + "|asset="
            + context.assetRootPath + "|theater="
            + toLowerAsciiLocal(theater.getTileExtension());
        RuntimeTmpTileFile mix, loose;
        mix.loadAttempted = mix.loaded = loose.loadAttempted = loose.loaded = true;
        mix.errorMessage = "warm MIX diagnostic";
        mix.images.resize(3); loose.images.resize(1);
        mix.images[0].present = mix.images[0].hasRadarColors = true;
        mix.images[0].radarLeft = {1, 129, 255};
        mix.images[0].radarRight = {254, 128, 0};
        // image 1 is authored empty; image 2 lacks authored radar colors.
        mix.images[2].present = true;
        loose.images[0].present = loose.images[0].hasRadarColors = true;
        loose.images[0].radarLeft = {201, 19, 7};
        loose.images[0].radarRight = {91, 76, 55};
        cache.tmpTilesByPath.emplace("resource:__radar_assets__|radar-memo.tem", std::move(mix));
        cache.tmpTilesByPath.emplace("__radar_loose__.tem", std::move(loose));
        cache.terrainTilePathByIndex.emplace(17, "@mix-resource:radar-memo.tem");
        for (int tile : {0, 18, -1, 65536})
            cache.terrainTilePathByIndex.emplace(tile, "__radar_loose__.tem");
        cache.missingTerrainTilePathByIndex.insert(19);
        return cache;
    };
    auto scalarCache = makeCache(), memoCache = makeCache();
    RuntimeRadarTerrainColorsMemo memo(context, memoCache, false);
    const auto sameColor = [](RuntimePaletteColor a, RuntimePaletteColor b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    };
    const std::array<std::pair<int, int>, 12> inputs{{
        {17, 0}, {18, 0}, {17, 1}, {17, 2}, {17, -1}, {17, 65536},
        {19, 0}, {-1, 0}, {65536, 0}, {65535, -123}, {0, 0}, {17, 0}}};
    for (int pass = 0; pass < 3; ++pass) {
        for (const auto [tile, subTile] : inputs) {
            TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = subTile;
            // Unrelated cells with identical assets must share only RGB data.
            cell.x = pass * 37; cell.y = pass * -19; cell.level = pass + 4;
            cell.hasOverlay = pass == 2; cell.overlayIsGem = pass == 1;
            RuntimePaletteColor expectedLeft{63, 77, 95}, expectedRight{199, 37, 11};
            auto actualLeft = expectedLeft, actualRight = expectedRight;
            scalarCache.lastTmpTileLoadError = memoCache.lastTmpTileLoadError = "intervening diagnostic";
            const bool expected = resolveRuntimeTerrainRadarColors(cell, context, scalarCache,
                expectedLeft, expectedRight);
            const bool actual = memo.resolve(cell, actualLeft, actualRight);
            if (actual != expected || !sameColor(actualLeft, expectedLeft)
                || !sameColor(actualRight, expectedRight)
                || memoCache.lastTmpTileLoadError != scalarCache.lastTmpTileLoadError)
                return fail("production resolver RGB/bool/output-preservation/diagnostic mismatch");
        }
    }
    if (memo.hits() == 0 || memo.scalarCalls() >= memo.queries()
        || memo.size() != 10) return fail("memo did not reuse complete normalized identity");
    // In-place asset replacement requires explicit invalidation, even though
    // results are owned values and have no dangling resource pointers.
    memo.invalidate();
    auto& changed = memoCache.tmpTilesByPath.at("resource:__radar_assets__|radar-memo.tem");
    changed.images[0].radarLeft = {88, 99, 111};
    changed.errorMessage = "reloaded MIX diagnostic";
    TerrainCell cell; cell.tileIndex = 17; cell.subTileIndex = 0;
    RuntimePaletteColor left, right;
    if (!memo.resolve(cell, left, right) || !sameColor(left, changed.images[0].radarLeft)
        || memoCache.lastTmpTileLoadError != changed.errorMessage)
        return fail("explicit invalidation retained replaced asset or diagnostic");
    // Disable/failed-palette behavior must retain false outputs and diagnostics.
    for (int variant = 0; variant < 4; ++variant) {
        auto variantContext = context;
        auto expectedCache = makeCache(), actualCache = makeCache();
        switch (variant) {
        case 0: variantContext.useTileTextures = false; break;
        case 1: variantContext.theater = nullptr; break;
        case 2: variantContext.palettePath.clear(); break;
        case 3: expectedCache.paletteLoaded = actualCache.paletteLoaded = false; break;
        }
        RuntimeRadarTerrainColorsMemo variantMemo(variantContext, actualCache, false);
        for (int pass = 0; pass < 2; ++pass) {
            RuntimePaletteColor a{7, 8, 9}, b{10, 11, 12}, x = a, y = b;
            expectedCache.lastTmpTileLoadError = actualCache.lastTmpTileLoadError = "preserve";
            if (resolveRuntimeTerrainRadarColors(cell, variantContext, expectedCache, a, b)
                    != variantMemo.resolve(cell, x, y)
                || !sameColor(a, x) || !sameColor(b, y)
                || expectedCache.lastTmpTileLoadError != actualCache.lastTmpTileLoadError)
                return fail("rejected context/palette changed output or diagnostic");
        }
    }
    // Exhaust the derived bound using real, cached loose TMP resolutions.
    auto boundedCache = makeCache();
    for (int tile = 100000; tile < 100000 + 2060; ++tile)
        boundedCache.terrainTilePathByIndex.emplace(tile, "__radar_loose__.tem");
    RuntimeRadarTerrainColorsMemo bounded(context, boundedCache, false);
    for (int tile = 100000; tile < 100000 + 2060; ++tile) {
        cell.tileIndex = tile;
        if (!bounded.resolve(cell, left, right) || !sameColor(left, {201, 19, 7}))
            return fail("capacity overflow changed scalar colors");
    }
    const auto previousCalls = bounded.scalarCalls();
    if (bounded.size() != RuntimeRadarTerrainColorsMemo::maximumMemoizedColors
        || !bounded.resolve(cell, left, right) || bounded.scalarCalls() != previousCalls + 1)
        return fail("capacity not bounded or overflow failed to replay scalar");
    {
        RuntimeRadarTerrainColorReferenceScope reference(true);
        RuntimeRadarTerrainColorsMemo raw(context, boundedCache);
        if (!raw.resolve(cell, left, right) || !raw.resolve(cell, left, right)
            || raw.hits() != 0 || raw.size() != 0 || raw.scalarCalls() != 2)
            return fail("independent reference did not bypass memo");
        RuntimeRadarTerrainColorReferenceScope nested(false);
        if (runtimeRadarTerrainColorReference()) return fail("nested reference override");
    }
    std::cout << "Radar color memo: actualResolverPairs=" << memo.queries()
        << " hits=" << memo.hits() << " completeSignedKeysAndFalseOutputs=exact"
        << " MIXDiagnosticsAndInvalidation=exact bounded=2048 reference=independent\n";
    return true;
}

static bool runRuntimeRadarBorrowedGapVisionContract(std::string& error, std::uint64_t& checks) {
    const auto fail = [&](const char* detail) {
        error = std::string("radar borrowed gap visibility: ") + detail; return false;
    };
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString("[InfantryTypes]\n0=RADARSCOUT\n[BuildingTypes]\n0=RADARGAP\n"
        "[RADARSCOUT]\nSight=3\nStrength=100\n"
        "[RADARGAP]\nSight=1\nStrength=500\nGapGenerator=yes\nGapRadiusInCells=4\nPowered=yes\n")
        || !rules.loadFromIni(ini)) return fail("actual rules fixture parse");
    const auto* gapRules = rules.findBuildingType("RADARGAP");
    if (!gapRules || !gapRules->gapGenerator || gapRules->gapRadiusInCells != 4)
        return fail("actual gap rule readback");
    GameWorld world;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        if (x == 5 && y == 6) continue; // Invalid-cell hole inside the sight footprint.
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        world.accessTerrainCells().push_back(cell);
    }
    TerrainCell edge; edge.x = edge.y = 511; edge.cellIndex512 = 512 * 512 - 1;
    world.accessTerrainCells().push_back(edge);
    WorldObjectInstance scout;
    scout.instanceId = 1; scout.kind = WorldObjectKind::Infantry; scout.type = "RADARSCOUT";
    scout.owner = "Alpha"; scout.x = scout.y = 4; scout.alive = true; scout.health = 100;
    world.accessObjects().push_back(scout);
    auto gap = scout;
    gap.instanceId = 2; gap.kind = WorldObjectKind::Structure; gap.type = "RADARGAP";
    gap.owner = "Enemy"; gap.x = gap.y = 20; gap.health = 500; gap.poweredOperational = true;
    world.accessObjects().push_back(gap);
    RuntimeVisionSystem vision;
    const std::vector<RuntimeVisionObserverDefinition> observers{{"Alpha", {}}, {"Enemy", {}}};
    vision.update(world, &rules, observers, 1);
    if (!vision.revealMap("Alpha")) return fail("actual permanent exploration setup");
    vision.update(world, &rules, observers, 2);
    RuntimeVisionGridView borrowed;
    if (!vision.gridViewFor(" ALPHA ", borrowed) || !borrowed.gapShroudedCells
        || borrowed.gapShroudedCells[20 * 512 + 20] == 0
        || vision.visibilityFor("Alpha", 4, 4) != RuntimeCellVisibility::Visible
        || vision.visibilityFor("Alpha", 10, 10) != RuntimeCellVisibility::Fogged
        || vision.visibilityFor("Alpha", 20, 20) != RuntimeCellVisibility::Shrouded
        || vision.visibilityFor("Alpha", 5, 6) != RuntimeCellVisibility::Shrouded)
        return fail("actual visible/fogged/gap/invalid-hole fixture");
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        ++checks;
        if (vision.visibilityFor("Alpha", x, y)
            != RuntimeVisionSystem::visibilityForGridView(borrowed, x, y))
            return fail("actual gap precedence differs from original owner query");
    }
    ++checks;
    if (vision.visibilityFor("Alpha", 511, 511)
        != RuntimeVisionSystem::visibilityForGridView(borrowed, 511, 511))
        return fail("last physical grid cell differs");
    return true;
}

static bool runRuntimeRadarTerrainColorsMemoContract(RuntimeWindowState& live, std::string& error) {
    if (!runRuntimeRadarPixelStorePrimitiveContract(error)
        || !runRuntimeRadarPixelStoreActualContract(live, error)) return false;
    if (!runRuntimeRadarSampledTerrainPrimitiveContract(error)
        || !runRuntimeRadarPixelStoreActualContract(live, error, true)) return false;
    if (!runRuntimeRadarTerrainColorsMemoPrimitiveContract(error)) return false;
    std::uint64_t gapVisionChecks = 0;
    if (!runRuntimeRadarBorrowedGapVisionContract(error, gapVisionChecks)) return false;
    const auto fail = [&](const char* detail) {
        error = std::string("radar terrain color memo pixels: ") + detail; return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    seedRuntimeLoadScratch(probe, live, true);
    if (probe.world.getTerrainCells().empty() || !probe.theaterLoaded)
        return fail("actual map/theater fixture required");
    probe.config.playerSlots.clear();
    probe.config.playerCountry = "Americans";
    probe.economy.owner = "RadarMemoObserverA";
    probe.vision.reset();
    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"RadarMemoObserverA", {}}, {"RadarMemoObserverB", {}}};
    probe.vision.update(probe.world, &probe.rules, observers, probe.tick);
    probe.vision.resetShroud(probe.economy.owner);
    HDC screen = GetDC(nullptr);
    RuntimeBackBuffer destination;
    struct Cleanup {
        RuntimeWindowState& state; RuntimeBackBuffer& destination; HDC screen;
        ~Cleanup() {
            releaseRuntimeBackBuffer(destination);
            releaseRuntimeBackBuffer(state.radarTerrainBuffer);
            if (screen) ReleaseDC(nullptr, screen);
        }
    } cleanup{probe, destination, screen};
    if (!screen || !ensureRuntimeBackBuffer(destination, screen, 256, 192))
        return fail("actual radar DIB allocation");
    struct Snapshot {
        std::vector<std::uint32_t> pixels;
        std::array<int, 6> resources;
        std::uint64_t resourceHash;
        std::string key, tmpError;
        RuntimeRadarTerrainColorMemoSummary memo;
        std::size_t memoInstances = 0;
    };
    const auto draw = [&](bool reference, bool forceRebuild, const RECT& rect,
        Snapshot& snapshot) {
        RuntimeRadarTerrainColorReferenceScope mode(reference);
        RuntimeRadarTerrainColorMemoObserverScope observed(
            [](void* output, const RuntimeRadarTerrainColorMemoSummary& summary) {
                auto& snapshot = *static_cast<Snapshot*>(output);
                snapshot.memo.queries += summary.queries;
                snapshot.memo.hits += summary.hits;
                snapshot.memo.scalarCalls += summary.scalarCalls;
                snapshot.memo.entries += summary.entries;
                snapshot.memo.reference = summary.reference;
                ++snapshot.memoInstances;
            }, &snapshot);
        if (forceRebuild) probe.radarTerrainKey.clear();
        auto* pixels = static_cast<std::uint32_t*>(destination.pixels);
        std::fill(pixels, pixels + 256 * 192, 0x005f7311U);
        const auto projection = buildRuntimeRadarProjection(probe);
        if (!drawRuntimeRadarTerrainCache(destination.memoryDc, probe, rect, projection)) return false;
        GdiFlush();
        snapshot.pixels.assign(pixels, pixels + 256 * 192);
        snapshot.resources = {probe.lastRadarResourceCells, probe.lastRadarResourceOreCells,
            probe.lastRadarResourceGemCells, probe.lastRadarResourcePixels,
            probe.lastRadarResourceRuleColors, probe.lastRadarResourceFallbackColors};
        snapshot.resourceHash = probe.lastRadarResourceHash;
        snapshot.key = probe.radarTerrainKey;
        snapshot.tmpError = probe.renderCache.lastTmpTileLoadError;
        return true;
    };
    const auto same = [](const Snapshot& a, const Snapshot& b) {
        return a.pixels == b.pixels && a.resources == b.resources
            && a.resourceHash == b.resourceHash && a.key == b.key && a.tmpError == b.tmpError;
    };
    const auto projection = buildRuntimeRadarProjection(probe);
    std::size_t centerIndex = probe.world.getTerrainCells().size();
    double centerDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < probe.world.getTerrainCells().size(); ++i) {
        const auto& terrain = probe.world.getTerrainCells()[i];
        if (!runtimeRadarTerrainCellIntersectsProjection(projection, terrain)) continue;
        const auto dxy = runtimeRadarDxyPoint(terrain.x, terrain.y, projection.fullMapWidth);
        const double distance = std::abs(dxy.x - (projection.minX + projection.maxX) / 2.0)
            + std::abs(dxy.y - (projection.minY + projection.maxY) / 2.0);
        if (distance < centerDistance) { centerDistance = distance; centerIndex = i; }
    }
    if (centerIndex == probe.world.getTerrainCells().size())
        return fail("actual radar has no in-projection terrain cell");
    const auto center = probe.world.getTerrainCells()[centerIndex];
    int pairs = 0;
    std::uint64_t borrowedVisionChecks = 0;
    const auto compareBorrowedVision = [&](const std::string& owner, bool completeGrid) {
        const std::uint64_t revision = probe.vision.revisionFor(owner);
        RuntimeVisionGridView borrowed;
        // Test the same canonical-owner lookup as scalar visibilityFor, with
        // whitespace/case aliases before borrowing just once for this pass.
        const bool available = probe.vision.gridViewFor(
            "  " + toUpperAsciiLocal(owner) + "  ", borrowed);
        if (available != probe.vision.hasObserver(owner)
            || available != borrowed.available()
            || (available && (borrowed.revision != revision || borrowed.cellCount != 512U * 512U)))
            return fail("borrowed observer identity/revision/grid domain");
        const auto compare = [&](int x, int y) {
            const auto expected = probe.vision.visibilityFor(owner, x, y);
            const auto actual = RuntimeVisionSystem::visibilityForGridView(borrowed, x, y);
            ++borrowedVisionChecks;
            return expected == actual;
        };
        // Compare the full physical visibility grid, including holes where no
        // actual world TerrainCell exists, rather than only displayed pixels.
        if (completeGrid) {
            for (int y = 0; y < 512; ++y) for (int x = 0; x < 512; ++x)
                if (!compare(x, y)) return fail("full physical grid differs from original owner visibility");
        } else {
            for (const auto& terrain : probe.world.getTerrainCells())
                if (!compare(terrain.x, terrain.y))
                    return fail("complete world terrain differs from original owner visibility");
        }
        const std::array<int, 8> edges{{std::numeric_limits<int>::min(), -1000, -1,
            0, 511, 512, 1000, std::numeric_limits<int>::max()}};
        for (int y : edges) for (int x : edges)
            if (!compare(x, y)) return fail("signed/outside/512-boundary query changed original semantics");
        if (probe.vision.revisionFor(owner) != revision)
            return fail("borrowed/scalar visibility lookup changed authoritative revision");
        RuntimeVisionGridView after;
        if (probe.vision.gridViewFor(owner, after) != available
            || after.validCells != borrowed.validCells || after.visibleCells != borrowed.visibleCells
            || after.exploredCells != borrowed.exploredCells || after.gapShroudedCells != borrowed.gapShroudedCells
            || after.layoutEpoch != borrowed.layoutEpoch || after.revision != borrowed.revision)
            return fail("read-only comparison invalidated borrowed storage");
        return true;
    };
    bool actualResourceProbe = false;
    for (int scene = 0; scene < 8; ++scene) {
        switch (scene) {
        case 0: probe.visionEnabled = false; break;
        case 1:
            probe.visionEnabled = true;
            if (!runtimeLocalVisionRestricted(probe)
                || probe.vision.visibilityFor(probe.economy.owner, center.x, center.y)
                    != RuntimeCellVisibility::Shrouded)
                return fail("actual shrouded observer fixture");
            break;
        case 2:
            if (!probe.vision.exploreArea(probe.economy.owner, center.x, center.y, 5))
                return fail("scripted local exploration did not change actual mask");
            if (probe.vision.visibilityFor(probe.economy.owner, center.x, center.y)
                == RuntimeCellVisibility::Shrouded) return fail("scripted explored center still shrouded");
            break;
        case 3:
            if (!probe.vision.revealMap(probe.economy.owner))
                return fail("full reveal did not change actual mask");
            break;
        case 4:
            if (!probe.vision.resetShroudArea(probe.economy.owner, center.x, center.y, 3))
                return fail("reshroud did not change actual mask");
            break;
        case 5:
            probe.economy.owner = "RadarMemoObserverB";
            probe.radarTerrainKey.clear();
            if (probe.vision.visibilityFor(probe.economy.owner, center.x, center.y)
                != RuntimeCellVisibility::Shrouded) return fail("second observer borrowed first mask");
            break;
        case 6:
            probe.economy.owner = "RadarMemoObserverA";
            probe.visionEnabled = false;
            setRuntimeResourceTerrainBails(probe.world.accessTerrainCells()[centerIndex], "GEM", 8);
            ++probe.resourceStateRevision;
            actualResourceProbe = true;
            break;
        case 7:
            probe.world.accessTerrainCells()[centerIndex].tileIndex = 65535;
            probe.world.accessTerrainCells()[centerIndex].subTileIndex = -17;
            break;
        }
        if (!compareBorrowedVision(probe.economy.owner, scene == 4)
            || !compareBorrowedVision(probe.economy.owner == "RadarMemoObserverA"
                ? "RadarMemoObserverB" : "RadarMemoObserverA", false)
            || !compareBorrowedVision("RadarMemoAbsentObserver", false)
            || !compareBorrowedVision("", false)) return false;
        const RECT rect = scene % 2 ? RECT{7, 9, 187, 129} : RECT{3, 5, 224, 152};
        Snapshot original, actual, warm;
        if (!draw(true, true, rect, original) || !draw(false, true, rect, actual)
            || !same(original, actual)) return fail("complete original/optimized DIB or resource diagnostics differ");
        if (original.memoInstances != 1 || actual.memoInstances != 1
            || original.memo.queries == 0 || original.memo.queries != actual.memo.queries
            || !original.memo.reference || actual.memo.reference
            || original.memo.hits != 0 || original.memo.scalarCalls != original.memo.queries
            || original.memo.entries != 0 || actual.memo.hits == 0
            || actual.memo.scalarCalls >= actual.memo.queries
            || actual.memo.queries != actual.memo.hits + actual.memo.scalarCalls
            || actual.memo.entries > RuntimeRadarTerrainColorsMemo::maximumMemoizedColors)
            return fail("actual original/optimized radar rebuild did not exercise independent branches/reuse");
        if (!draw(false, false, rect, warm) || !same(actual, warm))
            return fail("warm radar cache differs from actual rebuild");
        if (warm.memoInstances != 0 || warm.memo.queries != 0)
            return fail("warm cache unexpectedly constructed/rebuilt radar terrain colors");
        ++pairs;
    }
    if (!actualResourceProbe || pairs != 8) return fail("actual visibility/resource scenes not exercised");
    std::cout << "Radar color memo pixels: originalRebuildPairs=" << pairs
        << " warmCachePairs=" << pairs
        << " borrowedVisionOriginalOwnerChecks=" << borrowedVisionChecks
        << " activeGapOriginalOwnerChecks=" << gapVisionChecks
        << " fullDIB=exact resourceCountersAndHash=exact"
        << " full512GridHolesSignedEdgesMissingObserverAndOwnerAliases=exact"
        << " explorationRevealReshroudObserverAndResize=exact\n";
    return true;
}
