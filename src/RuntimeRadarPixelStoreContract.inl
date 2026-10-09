// Include after drawRuntimeRadarTerrainCache and before the existing
// RadarTerrainColorsMemoContract. This compares both real producer branches.
// The primitive oracle calls native SetPixelV directly, never the new writer.
static bool runRuntimeRadarPixelStorePrimitiveContract(std::string& error) {
    const auto fail = [&](const char* detail) {
        error = std::string("radar pixel store primitive: ") + detail; return false;
    };
    HDC screen = GetDC(nullptr);
    RuntimeBackBuffer original, candidate;
    struct Cleanup {
        RuntimeBackBuffer& a; RuntimeBackBuffer& b; HDC screen;
        ~Cleanup() {
            releaseRuntimeBackBuffer(a); releaseRuntimeBackBuffer(b);
            if (screen) ReleaseDC(nullptr, screen);
        }
    } cleanup{original, candidate, screen};
    if (!screen) return fail("screen DC");
    std::uint64_t cases = 0, writes = 0;
    // These are independent native-DC transformations, applied identically to
    // each bitmap. All tests compare full uint32 pixels, including high bytes.
    const auto configure = [](HDC dc, int mode, int w, int h) {
        switch (mode) {
        case 1: return IntersectClipRect(dc, 1, 2, w - 1, h - 1) != ERROR;
        case 2: return SetViewportOrgEx(dc, 2, 1, nullptr) != FALSE;
        case 3: return SetWindowOrgEx(dc, 1, 2, nullptr) != FALSE;
        case 4: return SetMapMode(dc, MM_ANISOTROPIC) != 0
                && SetWindowExtEx(dc, w, h, nullptr) && SetViewportExtEx(dc, w / 2, h / 2, nullptr);
        case 5: return SetGraphicsMode(dc, GM_ADVANCED) != 0;
        case 6: return SetLayout(dc, LAYOUT_RTL) != GDI_ERROR;
        default: return true;
        }
    };
    const auto one = [&](int w, int h, int mode, bool mixed, bool queuedFill, int identity) {
        if (!ensureRuntimeBackBuffer(original, screen, w, h)
            || !ensureRuntimeBackBuffer(candidate, screen, w, h)) return false;
        if (!GdiFlush()) return false;
        auto* oldPixels = static_cast<std::uint32_t*>(original.pixels);
        auto* newPixels = static_cast<std::uint32_t*>(candidate.pixels);
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i)
            oldPixels[i] = newPixels[i] = 0xa5731100U ^ static_cast<std::uint32_t>(i * 7919U);
        // Do not flush here: the writer's one synchronization must see this
        // same unchanged native FillRect, before any CPU store is attempted.
        if (queuedFill) {
            const RECT rect{0, 0, w, h};
            HBRUSH brush = CreateSolidBrush(RGB(2, 8, 10));
            const bool filled = brush && FillRect(original.memoryDc, &rect, brush)
                && FillRect(candidate.memoryDc, &rect, brush);
            if (brush) DeleteObject(brush);
            if (!filled) return false;
        }
        const int oldSaved = SaveDC(original.memoryDc), newSaved = SaveDC(candidate.memoryDc);
        if (oldSaved == 0 || newSaved == 0) return false;
        const bool setup = configure(original.memoryDc, mode, w, h)
            && configure(candidate.memoryDc, mode, w, h);
        RuntimeRadarPixelStoreSummary summary;
        bool equalReturns = setup;
        {
            void* pointer = identity == 1 ? nullptr : identity == 2
                ? static_cast<void*>(newPixels + 1) : candidate.pixels;
            const HBITMAP bitmap = identity == 3 ? original.bitmap : candidate.bitmap;
            const int claimedWidth = identity == 4 ? w - 1 : w;
            RuntimeRadarPixelStore writer(candidate.memoryDc, bitmap, pointer, claimedWidth, h,
                identity == 5 ? false : candidate.topDownDib, false);
            const auto store = [&](int x, int y, COLORREF color) {
                const BOOL expected = SetPixelV(original.memoryDc, x, y, color);
                const BOOL actual = writer.store(x, y, color);
                equalReturns = equalReturns && ((expected != FALSE) == (actual != FALSE));
                ++writes;
            };
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
                store(x, y, RGB((x * 17 + y) & 255, (y * 31 + x) & 255, (x ^ y) & 255));
            store(w - 1, h - 1, RGB(255, 0, 255));
            store(0, 0, RGB(0, 255, 0));
            store(w / 2, h / 2, RGB(0, 0, 0));
            store(w / 2, h / 2, RGB(255, 255, 255));
            if (mixed) {
                // Fallback then overlap: no later CPU write may overtake a
                // queued SetPixelV. A single scope never flushes per store.
                store(w / 2, h / 2, PALETTERGB(10, 20, 30));
                store(w / 2, h / 2, RGB(91, 7, 201));
                store(0, 0, PALETTEINDEX(0));
                store(0, 0, RGB(33, 66, 99));
                store(w - 1, 0, CLR_INVALID);
                store(w - 1, 0, RGB(5, 6, 7));
                store(-1, 0, RGB(1, 2, 3));
                store(w, h - 1, RGB(1, 2, 3));
                store(0, -1, RGB(1, 2, 3));
                store(w - 1, h, RGB(1, 2, 3));
                store(1, 1, RGB(80, 90, 100));
            }
            summary = writer.summary();
        }
        const bool restored = RestoreDC(original.memoryDc, oldSaved)
            && RestoreDC(candidate.memoryDc, newSaved);
        if (!GdiFlush() || !restored || !equalReturns
            || !std::equal(oldPixels, oldPixels + static_cast<std::size_t>(w) * h, newPixels)) return false;
        if (mode != 0 || identity != 0) {
            if (summary.standardDib || summary.directStores != 0 || summary.flushCalls != 0
                || summary.gdiStores != summary.stores) return false;
        } else {
            if (!summary.standardDib || !summary.flushSucceeded || summary.flushCalls != 1
                || summary.directStores == 0) return false;
            if (!mixed && (summary.directStores != summary.stores || summary.gdiStores != 0)) return false;
            if (mixed && (summary.nonRgbStores != 3 || summary.outsideStores != 4
                || summary.gdiStores != 11 || summary.directEnabled)) return false;
        }
        ++cases; return true;
    };
    // First case deliberately has no FillRect: alpha cannot be masked away by
    // clearing the source bitmap, or by comparing only RGB/BitBlt output.
    if (!one(19, 13, 0, false, false, 0)
        || !one(512, 512, 0, false, true, 0)
        || !one(19, 13, 0, true, true, 0)) return fail("native full-DIB/return/alpha/queued-fill/overlap oracle");
    for (int mode = 1; mode <= 6; ++mode)
        if (!one(19, 13, mode, true, true, 0)) return fail("clip/origin/mapping/graphics fallback oracle");
    for (int identity = 1; identity <= 5; ++identity)
        if (!one(19, 13, 0, true, true, identity)) return fail("null/foreign pointer/bitmap/dimensions fallback oracle");
    const auto unusualBitmap = [&](bool bottomUp, WORD bits) {
        const int w = 19, h = 13, stride = ((w * bits + 31) / 32) * 4;
        HDC a = CreateCompatibleDC(screen), b = CreateCompatibleDC(screen);
        HBITMAP aBitmap = nullptr, bBitmap = nullptr;
        HGDIOBJ aOld = nullptr, bOld = nullptr;
        void* aBits = nullptr; void* bBits = nullptr;
        struct Release {
            HDC& a; HDC& b; HBITMAP& ab; HBITMAP& bb; HGDIOBJ& ao; HGDIOBJ& bo;
            ~Release() {
                GdiFlush();
                if (a && ao) SelectObject(a, ao); if (b && bo) SelectObject(b, bo);
                if (ab) DeleteObject(ab); if (bb) DeleteObject(bb);
                if (a) DeleteDC(a); if (b) DeleteDC(b);
            }
        } release{a, b, aBitmap, bBitmap, aOld, bOld};
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = bottomUp ? h : -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = bits; info.bmiHeader.biCompression = BI_RGB;
        if (!a || !b) return false;
        aBitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &aBits, nullptr, 0);
        bBitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bBits, nullptr, 0);
        if (!aBitmap || !bBitmap || !aBits || !bBits) return false;
        aOld = SelectObject(a, aBitmap); bOld = SelectObject(b, bBitmap);
        if (!aOld || !bOld || !GdiFlush()) return false;
        auto* ap = static_cast<unsigned char*>(aBits); auto* bp = static_cast<unsigned char*>(bBits);
        for (int i = 0; i < stride * h; ++i) ap[i] = bp[i] = static_cast<unsigned char>(i * 31 + 77);
        bool returns = true;
        RuntimeRadarPixelStoreSummary summary;
        {
            RuntimeRadarPixelStore writer(b, bBitmap, bBits, w, h, !bottomUp, false);
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                const COLORREF color = RGB(x * 7, y * 11, (x + y) * 5);
                const BOOL expected = SetPixelV(a, x, y, color), actual = writer.store(x, y, color);
                returns = returns && ((expected != FALSE) == (actual != FALSE)); ++writes;
            }
            summary = writer.summary();
        }
        if (!GdiFlush() || !returns || !std::equal(ap, ap + stride * h, bp)
            || summary.standardDib || summary.directStores || summary.flushCalls
            || summary.gdiStores != summary.stores) return false;
        ++cases; return true;
    };
    if (!unusualBitmap(true, 32) || !unusualBitmap(false, 24))
        return fail("bottom-up/non-32-bit raw-byte fallback oracle");
    {
        RuntimeRadarPixelStore nullDc(nullptr, nullptr, nullptr, 0, 0, false, false);
        const BOOL expected = SetPixelV(nullptr, 0, 0, RGB(2, 4, 6));
        const BOOL actual = nullDc.store(0, 0, RGB(2, 4, 6));
        if ((expected != FALSE) != (actual != FALSE) || nullDc.summary().flushCalls
            || nullDc.summary().directStores || nullDc.summary().gdiStores != 1)
            return fail("null DC did not keep original failure");
        ++cases;
    }
    const bool savedReference = runtimeRadarPixelStoreReference();
    {
        RuntimeRadarPixelStoreReferenceScope outer(true);
        if (!runtimeRadarPixelStoreReference()) return fail("reference enable");
        { RuntimeRadarPixelStoreReferenceScope inner(false);
          if (runtimeRadarPixelStoreReference()) return fail("nested reference disable"); }
        if (!runtimeRadarPixelStoreReference()) return fail("nested reference restore");
        RuntimeRadarPixelStore writer(candidate.memoryDc, candidate.bitmap,
            candidate.pixels, candidate.width, candidate.height, candidate.topDownDib);
        if (!writer.summary().reference || writer.summary().flushCalls != 0)
            return fail("reference changed old synchronization");
    }
    if (runtimeRadarPixelStoreReference() != savedReference) return fail("outer reference restore");
    std::cout << "Radar pixel store primitive: cases=" << cases << " nativeOracleWrites=" << writes
        << " raw32BitIncludingAlpha=exact queuedFill=exact clippingMappingIdentityFallback=exact"
        << " rgbPaletteOutsideOverlaps=exact oneFlushPerDirectScope=verified\n";
    return true;
}

// Actual producer snapshots are intentionally independent cold/warm states.
// Both sides use the SAME terrain/INI/dense optimizations, so changing only
// pixel-store mode must not alter any resource/cache counter or diagnostic.
static bool runRuntimeRadarPixelStoreActualContract(RuntimeWindowState& live, std::string& error, bool sampledTerrainOracle = false) {
    const auto fail = [&](const char* detail) {
        error = std::string("radar pixel store actual: ") + detail; return false;
    };
    auto oldStorage = std::make_unique<RuntimeWindowState>();
    auto newStorage = std::make_unique<RuntimeWindowState>();
    auto& old = *oldStorage; auto& current = *newStorage;
    seedRuntimeLoadScratch(old, live, true); seedRuntimeLoadScratch(current, live, true);
    if (old.world.getTerrainCells().empty() || !old.theaterLoaded) return fail("native map/TMP fixture required");
    namespace pixelFs = std::filesystem;
    std::error_code ec;
    const auto root = pixelFs::temp_directory_path(ec) / ("concord-radar-pixels-"
        + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (ec || !pixelFs::create_directory(root, ec) || ec) return fail("isolated rules directory");
    const auto write = [&](const std::string& text) {
        std::ofstream file(root / "rulesmd.ini", std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size())); return static_cast<bool>(file);
    };
    const std::string first = "[Colors]\nGold=25,255,255\nOrange=90,180,220\n[General]\nDummy=one\n";
    const std::string second = "[Colors]\nGold=95,255,255\nOrange=30,180,220\n[General]\nDummy=two\n";
    if (!write(first)) return fail("isolated rules colors");
    IniFile resourceRules;
    if (!resourceRules.loadFromString("[Tiberiums]\n0=RadarPixelOre\n1=RadarPixelGems\n"
        "[RadarPixelOre]\nImage=1\nColor=Gold\nValue=25\n"
        "[RadarPixelGems]\nImage=2\nColor=Orange\nValue=50\n")) return fail("resource rules INI");
    const std::vector<RuntimeVisionObserverDefinition> observers{{"RadarPixelObserver", {}}};
    for (RuntimeWindowState* state : {&old, &current}) {
        state->renderCache = RuntimeMapRenderCache{};
        state->config.modRootPath = root.string(); state->config.campaignSourceId = "yr";
        state->config.playerSlots.clear(); state->config.playerCountry = "Americans";
        state->economy.owner = "RadarPixelObserver";
        if (!state->rules.loadFromIni(resourceRules)) return fail("resource rules load");
        state->rulesLoaded = true;
        state->terrainByCell = buildRuntimeTerrainLookup(state->world);
    }
    const auto projection = buildRuntimeRadarProjection(old);
    // Pick the resource and hole witnesses from the NORMAL-sized raster, so
    // the later hot-edit/fog comparisons cannot silently miss those cells.
    const auto layout = runtimeRadarLayout(RECT{0, 0, 221, 147}, projection);
    std::array<TerrainCell, 4> samples{}; std::size_t sampleCount = 0;
    std::array<POINT, 4> samplePixels{};
    for (int y = 0; y < 147 && sampleCount < samples.size(); ++y)
        for (int x = 0; x < 221 && sampleCount < samples.size(); ++x) {
            if (layout.scaleX <= 0 || layout.scaleY <= 0) continue;
            const RuntimeRadarDxyPoint dxy{
                projection.minX + (static_cast<double>(x) + 0.5) / layout.scaleX,
                projection.minY + (static_cast<double>(y) + 0.5) / layout.scaleY};
            if (!runtimeRadarProjectionContains(projection, dxy)) continue;
            const int tx = static_cast<int>(std::floor((dxy.x + dxy.y + 2.0) / 2.0));
            const int ty = static_cast<int>(std::floor((dxy.y - dxy.x + 2.0 * projection.fullMapWidth) / 2.0));
            if (tx < 0 || ty < 0 || tx >= 512 || ty >= 512) continue;
            const auto found = old.terrainByCell.find(runtimeCellKey(tx, ty));
            if (found == old.terrainByCell.end()) continue;
            bool separated = true;
            for (std::size_t i = 0; i < sampleCount; ++i)
                if (std::max(std::abs(samples[i].x - tx), std::abs(samples[i].y - ty)) < 8) separated = false;
            if (separated) {
                samples[sampleCount] = *found->second; samplePixels[sampleCount] = POINT{x, y}; ++sampleCount;
            }
        }
    if (sampleCount != samples.size()) return fail("four actually sampled separated physical terrain cells");
    const auto terrainFor = [](RuntimeWindowState& state, const TerrainCell& wanted) -> TerrainCell* {
        for (auto& cell : state.world.accessTerrainCells())
            if (cell.x == wanted.x && cell.y == wanted.y) return &cell;
        return nullptr;
    };
    for (RuntimeWindowState* state : {&old, &current}) {
        auto& cells = state->world.accessTerrainCells();
        auto hole = std::find_if(cells.begin(), cells.end(), [&](const TerrainCell& cell) {
            return cell.x == samples[3].x && cell.y == samples[3].y;
        });
        if (hole == cells.end()) return fail("sampled hole fixture");
        cells.erase(hole); // Fixture memory only; no file is removed.
        auto* ore = terrainFor(*state, samples[0]); auto* gems = terrainFor(*state, samples[1]);
        if (!ore || !gems) return fail("resource terrain fixture survived hole edit");
        setRuntimeResourceTerrainBails(*ore, "GOLD", 8); setRuntimeResourceTerrainBails(*gems, "GEM", 8);
        ++state->resourceStateRevision;
        state->terrainByCell = buildRuntimeTerrainLookup(state->world);
        state->vision.reset(); state->vision.update(state->world, &state->rules, observers, state->tick);
        state->vision.resetShroud(state->economy.owner); state->visionEnabled = false;
    }
    // A later nonresource duplicate must overwrite color but must NOT clear
    // the earlier resource membership/count. Also retain a genuinely unseen
    // resource whose legacy key is absent from the normal radar's sampler.
    if (sampledTerrainOracle) {
        std::unordered_set<int> rasterKeys;
        for (int y = layout.rect.top; y < layout.rect.bottom; ++y)
            for (int x = layout.rect.left; x < layout.rect.right; ++x) {
                const RuntimeRadarDxyPoint dxy{
                    projection.minX + (static_cast<double>(x - layout.rect.left) + 0.5) / layout.scaleX,
                    projection.minY + (static_cast<double>(y - layout.rect.top) + 0.5) / layout.scaleY};
                if (!runtimeRadarProjectionContains(projection, dxy)) continue;
                rasterKeys.insert(runtimeCellKey(static_cast<int>(std::floor((dxy.x + dxy.y + 2.0) / 2.0)),
                    static_cast<int>(std::floor((dxy.y - dxy.x + 2.0 * projection.fullMapWidth) / 2.0))));
            }
        const auto unused = std::find_if(old.world.getTerrainCells().begin(), old.world.getTerrainCells().end(),
            [&](const TerrainCell& cell) {
                return runtimeRadarTerrainCellIntersectsProjection(projection, cell)
                    && !rasterKeys.contains(runtimeCellKey(cell.x, cell.y))
                    && !cell.hasOverlay;
            });
        if (unused == old.world.getTerrainCells().end()) return fail("normal native map requires an unsampled resource witness");
        const TerrainCell unusedCell = *unused;
        for (RuntimeWindowState* state : {&old, &current}) {
            auto* resource = terrainFor(*state, unusedCell);
            auto* ore = terrainFor(*state, samples[0]);
            if (!resource || !ore) return fail("duplicate and unsampled resource source");
            setRuntimeResourceTerrainBails(*resource, "GOLD", 8);
            TerrainCell duplicate = *ore;
            duplicate.hasOverlay = duplicate.overlayIsOre = duplicate.overlayIsGem = false;
            duplicate.tileIndex = 65535; duplicate.subTileIndex = -17;
            state->world.accessTerrainCells().push_back(duplicate);
            ++state->resourceStateRevision;
            state->terrainByCell = buildRuntimeTerrainLookup(state->world);
            state->vision.update(state->world, &state->rules, observers, state->tick);
            state->vision.resetShroud(state->economy.owner);
        }
    }
    std::optional<RuntimeRadarProjection> projectionOverride;
    HDC screen = GetDC(nullptr);
    RuntimeBackBuffer oldTarget, newTarget;
    struct Cleanup {
        RuntimeWindowState& a; RuntimeWindowState& b; RuntimeBackBuffer& x; RuntimeBackBuffer& y; HDC screen;
        ~Cleanup() {
            releaseRuntimeBackBuffer(x); releaseRuntimeBackBuffer(y);
            releaseRuntimeBackBuffer(a.radarTerrainBuffer); releaseRuntimeBackBuffer(b.radarTerrainBuffer);
            if (screen) ReleaseDC(nullptr, screen);
        }
    } cleanup{old, current, oldTarget, newTarget, screen};
    if (!screen || !ensureRuntimeBackBuffer(oldTarget, screen, 512, 512)
        || !ensureRuntimeBackBuffer(newTarget, screen, 512, 512)) return fail("full 512 squared destination DIB");
    struct Snapshot {
        std::vector<std::uint32_t> target, radar;
        std::array<int, 6> resources{};
        std::vector<std::uint64_t> counters, sizes, rulesCounters;
        std::unordered_map<std::string, int> paletteSources;
        std::vector<std::string> diagnostics;
        std::uint64_t hash = 0;
        std::string key;
        bool returned = false;
        RuntimeRadarPixelStoreSummary store;
        RuntimeRadarSampledTerrainSummary sampled;
        unsigned sampledInstances = 0;
        unsigned instances = 0; int mutation = 0, savedDc = 0; HDC dc = nullptr;
        bool setup = true;
    };
    const auto captureCache = [](const RuntimeWindowState& state, Snapshot& out) {
        const auto& c = state.renderCache;
        // Every renderer cache hit/miss pair, the cumulative geometry/rebuild
        // counters and pose/cold resource sequences; elapsed timings excluded.
        out.counters = {static_cast<std::uint64_t>(c.terrainLayerCacheHits), static_cast<std::uint64_t>(c.terrainLayerCacheMisses),
            static_cast<std::uint64_t>(c.sequenceFrameCacheHits), static_cast<std::uint64_t>(c.sequenceFrameCacheMisses),
            static_cast<std::uint64_t>(c.projectilePaletteCacheHits), static_cast<std::uint64_t>(c.projectilePaletteCacheMisses),
            static_cast<std::uint64_t>(c.auxiliaryPaletteCacheHits), static_cast<std::uint64_t>(c.auxiliaryPaletteCacheMisses),
            static_cast<std::uint64_t>(c.auxiliaryShpBytesCacheHits), static_cast<std::uint64_t>(c.auxiliaryShpBytesCacheMisses),
            static_cast<std::uint64_t>(c.auxiliaryShpFrameCacheHits), static_cast<std::uint64_t>(c.auxiliaryShpFrameCacheMisses),
            static_cast<std::uint64_t>(c.voxelCacheHits), static_cast<std::uint64_t>(c.voxelCacheMisses),
            static_cast<std::uint64_t>(c.buildingAnimationLayerCacheHits), static_cast<std::uint64_t>(c.buildingAnimationLayerCacheMisses),
            static_cast<std::uint64_t>(c.projectileTypeCacheHits), static_cast<std::uint64_t>(c.projectileTypeCacheMisses),
            static_cast<std::uint64_t>(c.auxiliaryEffectKindCacheHits), static_cast<std::uint64_t>(c.auxiliaryEffectKindCacheMisses),
            static_cast<std::uint64_t>(c.vehicleBodyPoseCacheEntries), c.vehicleBodyPoseUseSequence, c.vehicleBodyPoseCacheEvictions,
            c.lastColdResourceSequence, static_cast<std::uint64_t>(c.mapFactLookupCacheRebuilds),
            c.geometryMobileBoundsMemoHits, c.geometryMobileBoundsMemoMisses, c.geometryMobileBoundsPaintRecords,
            c.geometryStructureMemoHits, c.geometryStructureMemoMisses, c.geometryBuildingDepthMemoHits,
            c.geometryBuildingDepthMemoMisses, c.geometryBuildingDepthPaintRecords,
            c.geometryTerrainTileMemoHits, c.geometryTerrainTileMemoMisses,
            static_cast<std::uint64_t>(c.paletteColorHits), static_cast<std::uint64_t>(c.paletteUnitFallbacks),
            static_cast<std::uint64_t>(c.paletteIsoFallbacks), static_cast<std::uint64_t>(c.paletteUnknownHits)};
        out.sizes = {c.terrainColorsByTileIndex.size(), c.palette.size(), c.structurePalette.size(),
            c.namedPalettesByKey.size(), c.tmpTilesByPath.size(), c.terrainTilePathByIndex.size(),
            c.missingTerrainTilePathByIndex.size(), c.objectShpSpritesByType.size(), c.objectShpBytesByImage.size(),
            c.auxiliaryShpSpritesByImage.size(), c.auxiliaryShpBytesByImage.size(), c.voxelSourcesByImage.size(),
            c.voxelObjectsByType.size(), c.stableVoxelCacheKeyByLookup.size(), c.buildingAnimationLayersByKey.size(),
            c.vehicleBodyPoseUses.size(), c.voxelFixedScaleByKey.size(), c.sequenceFrameMetadataByKey.size(),
            c.projectilePaletteSourceByKey.size(), c.auxiliaryPaletteSourceByKey.size(),
            c.projectileTypeIndexByImage.size(), c.auxiliaryEffectKindByImage.size(), c.mixArchivesByPath.size(),
            c.mixArchivePaths.size(), c.looseResourcePathsByName.size()};
        const auto& m = state.radarRulesColorsMemo;
        out.rulesCounters = {m.queries(), m.hits(), m.readCalls(), m.parseCalls(), m.buildCalls(),
            static_cast<std::uint64_t>(m.sourceBytes()), static_cast<std::uint64_t>(m.valid())};
        out.paletteSources = c.paletteSourceHitsByName;
        out.diagnostics = {c.lastTmpTileLoadError, c.voxelPaletteLookupErrorMessage,
            c.lastColdResourceType, c.lastColdResourceName, c.loadedPalettePath, c.loadedStructurePalettePath,
            c.voxelPaletteLookupSourcePath, c.terrainTilePathCacheRoot};
    };
    const auto draw = [&](RuntimeWindowState& state, RuntimeBackBuffer& target, bool reference,
        bool rebuild, const RECT& rect, int mutation, Snapshot& out) {
        RuntimeRadarPixelStoreReferenceScope pixelMode(sampledTerrainOracle ? false : reference);
        RuntimeRadarSampledTerrainReferenceScope sampledMode(sampledTerrainOracle
            ? reference : runtimeRadarSampledTerrainReference());
        RuntimeRadarSampledTerrainObserverScope sampledObserved(
            [](void* context, const RuntimeRadarSampledTerrainSummary& summary) {
                auto& snapshot = *static_cast<Snapshot*>(context);
                snapshot.sampled = summary; ++snapshot.sampledInstances;
            }, &out);
        RuntimeRadarTerrainColorReferenceScope terrainMode(false);
        RuntimeRadarRulesColorReferenceScope rulesMode(false);
        RuntimeRadarCellColorsReferenceScope cellsMode(false);
        out.mutation = mutation;
        RuntimeRadarPixelStoreObserverScope observed({&out,
            [](void* context, HDC dc, HBITMAP, int w, int h) noexcept {
                auto& s = *static_cast<Snapshot*>(context); s.dc = dc;
                if (!s.mutation) return;
                s.savedDc = SaveDC(dc);
                s.setup = s.savedDc != 0;
                if (!s.setup) return;
                switch (s.mutation) {
                case 1: s.setup = IntersectClipRect(dc, 2, 3, w - 2, h - 3) != ERROR; break;
                case 2: s.setup = SetViewportOrgEx(dc, 1, 2, nullptr) != FALSE; break;
                case 3: s.setup = SetWindowOrgEx(dc, 2, 1, nullptr) != FALSE; break;
                case 4: s.setup = SetGraphicsMode(dc, GM_ADVANCED) != 0; break;
                case 5: s.setup = SetLayout(dc, LAYOUT_RTL) != GDI_ERROR; break;
                }
            },
            [](void* context, const RuntimeRadarPixelStoreSummary& summary) noexcept {
                auto& s = *static_cast<Snapshot*>(context); s.store = summary; ++s.instances;
                if (s.savedDc) s.setup = (RestoreDC(s.dc, s.savedDc) != FALSE) && s.setup;
            }});
        if (rebuild) state.radarTerrainKey.clear();
        if (!GdiFlush()) return false;
        auto* pixels = static_cast<std::uint32_t*>(target.pixels);
        for (std::size_t i = 0; i < 512U * 512U; ++i)
            pixels[i] = 0xa55f7311U ^ static_cast<std::uint32_t>(i * 97U);
        // Seed the actual internal radar DIB's alpha too. The producer still
        // performs its original FillRect; the candidate must synchronize it.
        if (state.radarTerrainBuffer.pixels && rebuild) {
            auto* inner = static_cast<std::uint32_t*>(state.radarTerrainBuffer.pixels);
            std::fill(inner, inner + static_cast<std::size_t>(state.radarTerrainBuffer.width)
                * state.radarTerrainBuffer.height, 0xca721593U);
        }
        const auto worldBefore = state.world.deterministicSimulationStateHash();
        const auto visionBefore = state.vision.revisionFor(state.economy.owner);
        out.returned = drawRuntimeRadarTerrainCache(target.memoryDc, state, rect, projectionOverride ? *projectionOverride : buildRuntimeRadarProjection(state));
        if (!GdiFlush() || !out.setup
            || state.world.deterministicSimulationStateHash() != worldBefore
            || state.vision.revisionFor(state.economy.owner) != visionBefore) return false;
        out.target.assign(pixels, pixels + 512U * 512U);
        auto* inner = static_cast<const std::uint32_t*>(state.radarTerrainBuffer.pixels);
        if (inner) out.radar.assign(inner, inner + static_cast<std::size_t>(state.radarTerrainBuffer.width)
            * state.radarTerrainBuffer.height);
        out.resources = {state.lastRadarResourceCells, state.lastRadarResourceOreCells, state.lastRadarResourceGemCells,
            state.lastRadarResourcePixels, state.lastRadarResourceRuleColors, state.lastRadarResourceFallbackColors};
        out.hash = state.lastRadarResourceHash; out.key = state.radarTerrainKey;
        captureCache(state, out); return true;
    };
    const auto same = [](const Snapshot& a, const Snapshot& b) {
        return a.returned == b.returned && a.target == b.target && a.radar == b.radar
            && a.resources == b.resources && a.hash == b.hash && a.key == b.key
            && a.counters == b.counters && a.sizes == b.sizes && a.rulesCounters == b.rulesCounters
            && a.paletteSources == b.paletteSources && a.diagnostics == b.diagnostics;
    };
    std::vector<std::uint32_t> originalResourcePixels;
    std::uint64_t pairs = 0, actualDirectStores = 0, actualNativeStores = 0;
    std::uint64_t sampledPositivePairs = 0, effectOnlyVisits = 0, omittedVisibilityQueries = 0;
    std::string brokenPalettePath;
    for (int scene = 0; scene < 8; ++scene) {
        if (scene == 4) {
            const auto before = pixelFs::last_write_time(root / "rulesmd.ini", ec);
            if (ec || first.size() != second.size() || !write(second)) return fail("same-size rule hot edit");
            pixelFs::last_write_time(root / "rulesmd.ini", before, ec);
            if (ec) return fail("same-mtime rule hot edit");
        }
        if (scene == 7) {
            // 65535 is the authored-empty sentinel, normalized to tile/subtile
            // 0 by the original resolver. Invalid indices/subtiles also do not
            // prove a TMP decoding error. Use a real, valid theater slot and
            // actual corrupt bytes, in this contract's existing isolated root.
            const auto reference = old.theater.resolveTileIndex(0);
            if (!reference.validTileSet || reference.candidateFileNames.empty())
                return fail("broken TMP requires a valid original theater tile 0");
            RuntimeConfig isolated = old.config;
            isolated.localDataRootPath = root.string();
            const pixelFs::path palettePath(runtimePalettePath(isolated, old.report.map));
            const pixelFs::path tileRoot(runtimeTileRootPath(isolated, old.report.map));
            const auto insideFixture = [&](const pixelFs::path& path) {
                const auto relative = path.lexically_normal().lexically_relative(root.lexically_normal());
                if (relative.empty() || relative.is_absolute()) return false;
                for (const auto& part : relative) if (part == "..") return false;
                return true;
            };
            if (palettePath.empty() || tileRoot.empty() || !insideFixture(palettePath)
                || !insideFixture(tileRoot)) return fail("broken TMP/PAL paths escaped isolated fixture");
            pixelFs::create_directories(palettePath.parent_path(), ec);
            if (ec) return fail("isolated PAL parent");
            pixelFs::create_directories(tileRoot, ec);
            if (ec) return fail("isolated TMP parent");
            const auto createBytes = [&](const pixelFs::path& path, const char* data, std::size_t size) {
                // Generate each explicit new fixture file once. Never replace
                // an existing resource or remove any generated evidence file.
                if (!insideFixture(path) || pixelFs::exists(path, ec) || ec) return false;
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file.write(data, static_cast<std::streamsize>(size)); return static_cast<bool>(file);
            };
            const std::array<char, 768> validPalette{};
            if (!createBytes(palettePath, validPalette.data(), validPalette.size()))
                return fail("isolated valid PAL fixture");
            std::unordered_set<std::string> names;
            const char corruptByte = '\0';
            for (const auto& name : reference.candidateFileNames) {
                const pixelFs::path fileName(name);
                if (fileName.empty() || fileName != fileName.filename())
                    return fail("theater TMP candidate is not a plain fixture filename");
                if (names.insert(toLowerAsciiLocal(name)).second
                    && !createBytes(tileRoot / fileName, &corruptByte, 1))
                    return fail("isolated real corrupt TMP fixture");
            }
            brokenPalettePath = pathString(palettePath);
        }
        for (RuntimeWindowState* state : {&old, &current}) {
            switch (scene) {
            case 1: state->visionEnabled = true; break;
            case 2:
                if (!state->vision.revealMap(state->economy.owner)) return fail("visible reveal fixture");
                break;
            case 3:
                state->vision.update(state->world, &state->rules, observers, state->tick + 1);
                if (!state->vision.resetShroudArea(state->economy.owner, samples[2].x, samples[2].y, 1))
                    return fail("fogged resource plus sampled shroud fixture");
                break;
            case 4: state->visionEnabled = false; break;
            case 7: {
                if (!state->theater.resolveTileIndex(0).validTileSet)
                    return fail("both producers require the same valid broken TMP tile");
                state->config.localDataRootPath = root.string();
                // Keep physical coordinates, resource overlays and vision
                // unchanged. Every terrain lookup uses the same valid failed
                // asset, so a later healthy MIX lookup cannot erase its error.
                for (auto& cell : state->world.accessTerrainCells()) {
                    cell.tileIndex = 0; cell.subTileIndex = 0;
                }
                break;
            }
            }
        }
        if (scene == 1 && (!runtimeLocalVisionRestricted(old)
            || old.vision.visibilityFor(old.economy.owner, samples[0].x, samples[0].y) != RuntimeCellVisibility::Shrouded))
            return fail("actual shrouded source readback");
        if (scene == 2 && old.vision.visibilityFor(old.economy.owner, samples[0].x, samples[0].y) != RuntimeCellVisibility::Visible)
            return fail("actual visible source readback");
        if (scene == 3 && (old.vision.visibilityFor(old.economy.owner, samples[0].x, samples[0].y) != RuntimeCellVisibility::Fogged
            || old.vision.visibilityFor(old.economy.owner, samples[1].x, samples[1].y) != RuntimeCellVisibility::Fogged
            || old.vision.visibilityFor(old.economy.owner, samples[2].x, samples[2].y) != RuntimeCellVisibility::Shrouded))
            return fail("actual fogged ore/gem and sampled shroud source readback");
        // A complete actual 512x512 radar raster on the representative initial
        // scene; other scenes retain normal-sized raster work but compare the
        // ENTIRE 512x512 raw destination, including untouched alpha margins.
        const RECT rect = scene == 0 ? RECT{0, 0, 512, 512} : RECT{3, 5, 224, 152};
        const int mutation = scene == 5 ? 1 : scene == 6 ? 2 : 0;
        Snapshot a, b;
        if (!draw(old, oldTarget, true, true, rect, mutation, a)
            || !draw(current, newTarget, false, true, rect, mutation, b)
            || !a.returned || !same(a, b)) return fail("full raw DIB/counters/errors/authority native-store mismatch");
        if (sampledTerrainOracle) {
            if (a.sampledInstances != 1 || b.sampledInstances != 1 || a.sampled.active
                || a.sampled.effectOnlyVisits || a.store.reference || b.store.reference)
                return fail("independent sampled-mask full-loop reference not exercised");
            if (scene == 0) {
                if (b.sampled.active || b.sampled.effectOnlyVisits || a.sampled.colorVisits != b.sampled.colorVisits)
                    return fail("actual 512 squared cap did not fall back to original full loop");
            } else {
                if (!b.sampled.active || b.sampled.sampledKeys == 0 || b.sampled.effectOnlyVisits == 0
                    || a.sampled.colorVisits != b.sampled.colorVisits + b.sampled.effectOnlyVisits
                    || a.sampled.visibilityQueries < b.sampled.visibilityQueries)
                    return fail("native partial-raster pruning did not preserve complete asset visits");
                ++sampledPositivePairs;
                effectOnlyVisits += b.sampled.effectOnlyVisits;
                omittedVisibilityQueries += a.sampled.visibilityQueries - b.sampled.visibilityQueries;
            }
            if (a.store.stores != b.store.stores || a.store.stores == 0)
                return fail("sampled-mask changed original raster stores");
        } else if (a.instances != 1 || b.instances != 1 || !a.store.reference || b.store.reference
            || a.store.directStores != 0 || a.store.flushCalls != 0 || a.store.gdiStores != a.store.stores
            || a.store.stores != b.store.stores || a.store.stores == 0) return fail("actual original/new pixel branches not exercised");
        if (mutation) {
            if (b.store.standardDib || b.store.directStores || b.store.flushCalls
                || b.store.gdiStores != b.store.stores) return fail("actual private-DC abnormal fallback not exercised");
        } else if (!b.store.standardDib || !b.store.flushSucceeded || b.store.flushCalls != 1
            || b.store.directStores != b.store.stores || b.store.gdiStores != 0)
            return fail("actual native DIB direct path/one synchronization not exercised");
        if (scene == 0 && (a.resources[1] == 0 || a.resources[2] == 0 || a.resources[3] == 0 || a.resources[4] == 0))
            return fail("actual nonzero sampled ore/gem/rule-colored resource pixels required");
        if (scene == 1 && (a.resources[0] != 0 || a.resources[3] != 0)) return fail("shroud revealed resource counts/pixels");
        if (scene == 2 && (a.resources[1] == 0 || a.resources[2] == 0 || a.resources[3] == 0
            || (a.radar[static_cast<std::size_t>(samplePixels[3].y) * 221U + samplePixels[3].x] & 0x00ffffffU) != 0x0002080aU))
            return fail("normal-sized actual resource witnesses or untouched sampled-hole FillRect pixel");
        if (scene == 2) originalResourcePixels = a.target;
        if (scene == 4 && (a.target == originalResourcePixels || a.rulesCounters[3] < 2))
            return fail("same-size same-mtime hot edit did not visibly reparse real resource colors");
        if (scene == 7) {
            if (a.diagnostics[0].rfind("TMP file is too small: ", 0) != 0
                || a.diagnostics[0].find(pathString(root)) == std::string::npos
                || !old.renderCache.paletteLoaded || !current.renderCache.paletteLoaded
                || old.renderCache.palette.size() != 256 || current.renderCache.palette.size() != 256
                || old.renderCache.loadedPalettePath != brokenPalettePath
                || current.renderCache.loadedPalettePath != brokenPalettePath)
                return fail("real isolated TMP decode error or successful prerequisite PAL not exercised");
            const auto failedReadback = [&](const RuntimeWindowState& state) {
                for (const auto& entry : state.renderCache.tmpTilesByPath)
                    if (entry.first.rfind("resource:", 0) == 0 && entry.second.loadAttempted
                        && !entry.second.loaded && entry.second.errorMessage == a.diagnostics[0]) return true;
                return false;
            };
            if (!failedReadback(old) || !failedReadback(current))
                return fail("original TMP byte decoder failed-cache readback missing");
        }
        actualDirectStores += b.store.directStores; actualNativeStores += a.store.gdiStores; ++pairs;
        Snapshot warmA, warmB;
        if (!draw(old, oldTarget, true, false, rect, 0, warmA)
            || !draw(current, newTarget, false, false, rect, 0, warmB)
            || !same(warmA, warmB) || !same(a, warmA) || warmA.instances || warmB.instances)
            return fail("cache hit rerasterized/reloaded/counter-changed or failed full-DIB equality");
        ++pairs;
    }
    // Mapping fallbacks other than viewport-origin are checked through the
    // actual private-radar hook too, with full counters/alpha equality.
    for (int mutation : {3, 4, 5}) {
        Snapshot a, b; const RECT rect{7, 9, 187, 129};
        if (!draw(old, oldTarget, true, true, rect, mutation, a)
            || !draw(current, newTarget, false, true, rect, mutation, b) || !same(a, b)
            || b.store.directStores || b.store.flushCalls || b.store.gdiStores != b.store.stores)
            return fail("actual window-origin/graphics-mode/mirrored-layout fallback full-DIB oracle");
        ++pairs;
    }
    // Exercise actual legacy fallback and alias keys, not just membership
    // arithmetic. Both sources are independent fixture copies. This does not
    // assert such malformed coordinates occur in an ordinary authored map.
    if (sampledTerrainOracle) {
        for (int aliasCase = 0; aliasCase < 2; ++aliasCase) {
            RuntimeRadarProjection forced;
            forced.valid = true; forced.fullMapWidth = 1;
            if (aliasCase == 0) {
                forced.minX = 0; forced.maxX = 1100;
                forced.minY = 1020; forced.maxY = 2100;
            } else {
                forced.minX = -100; forced.maxX = -80;
                forced.minY = 0; forced.maxY = 20;
            }
            const auto smallLayout = runtimeRadarLayout(RECT{0, 0, 221, 147}, forced);
            const int px = aliasCase ? 100 : 5, py = aliasCase ? 70 : 5;
            const RuntimeRadarDxyPoint dxy{
                forced.minX + (static_cast<double>(px) + 0.5) / smallLayout.scaleX,
                forced.minY + (static_cast<double>(py) + 0.5) / smallLayout.scaleY};
            TerrainCell firstCell = samples[0];
            firstCell.x = static_cast<int>(std::floor((dxy.x + dxy.y + 2.0) / 2.0));
            firstCell.y = static_cast<int>(std::floor((dxy.y - dxy.x + 2.0) / 2.0));
            firstCell.tileIndex = firstCell.subTileIndex = 0;
            setRuntimeResourceTerrainBails(firstCell, "GOLD", 8);
            TerrainCell lastCell = firstCell;
            if (aliasCase == 0) { lastCell.x += 1000; --lastCell.y; }
            else { --lastCell.x; ++lastCell.y; }
            lastCell.hasOverlay = lastCell.overlayIsOre = lastCell.overlayIsGem = false;
            const int aliasKey = runtimeCellKey(firstCell.x, firstCell.y);
            if (aliasKey != runtimeCellKey(lastCell.x, lastCell.y)
                || !runtimeRadarTerrainCellIntersectsProjection(forced, firstCell)
                || !runtimeRadarTerrainCellIntersectsProjection(forced, lastCell)
                || (aliasCase == 0 && (firstCell.x < 512 || firstCell.y < 0))
                || (aliasCase == 1 && aliasKey != -1))
                return fail("actual noncanonical or signed legacy alias witness");
            for (RuntimeWindowState* state : {&old, &current}) {
                state->world.accessTerrainCells() = {firstCell, lastCell};
                state->terrainByCell = buildRuntimeTerrainLookup(state->world);
                state->terrainInfoByCell.clear(); state->visionEnabled = false;
                ++state->resourceStateRevision;
            }
            projectionOverride = forced;
            Snapshot a, b;
            if (!draw(old, oldTarget, true, true, RECT{3,5,224,152}, 0, a)
                || !draw(current, newTarget, false, true, RECT{3,5,224,152}, 0, b)
                || !same(a,b) || !b.sampled.active || a.resources[0] != 1
                || a.resources[3] == 0 || a.store.stores == 0)
                return fail("actual fallback-key last-writer color/resource-OR full raw-DIB mismatch");
            ++pairs;
        }
        projectionOverride.reset();
    }
    // No terrain but a valid authored projection: the original background
    // rebuild, true BitBlt return and zero resource stats must remain exact.
    for (RuntimeWindowState* state : {&old, &current}) {
        state->world.accessTerrainCells().clear(); state->terrainByCell.clear();
    }
    Snapshot noTerrainA, noTerrainB;
    if (!draw(old, oldTarget, true, true, RECT{0, 0, 512, 512}, 0, noTerrainA)
        || !draw(current, newTarget, false, true, RECT{0, 0, 512, 512}, 0, noTerrainB)
        || !same(noTerrainA, noTerrainB) || !noTerrainA.returned
        || noTerrainA.resources != std::array<int, 6>{} || noTerrainA.instances != 1 || noTerrainB.instances != 1
        || noTerrainA.store.stores || noTerrainB.store.stores || (!sampledTerrainOracle && noTerrainA.store.flushCalls)
        || noTerrainB.store.flushCalls != 1 || !noTerrainB.store.standardDib)
        return fail("empty terrain authored projection/background/zero counters");
    ++pairs;
    // Also remove authored projection metadata: preserve original early false
    // return and untouched target bytes, without creating a writer/loading.
    for (RuntimeWindowState* state : {&old, &current}) {
        state->report.map.size.clear(); state->report.map.localSize.clear(); state->report.map.width = 0;
        state->radarProjectionCacheValid = false;
    }
    Snapshot emptyA, emptyB;
    if (!draw(old, oldTarget, true, false, RECT{0, 0, 512, 512}, 0, emptyA)
        || !draw(current, newTarget, false, false, RECT{0, 0, 512, 512}, 0, emptyB)
        || !same(emptyA, emptyB) || emptyA.returned || emptyA.instances || emptyB.instances)
        return fail("empty terrain/projection original return/untouched-DIB/counters");
    ++pairs;
    if (sampledTerrainOracle) {
        if (sampledPositivePairs != 7 || effectOnlyVisits == 0 || omittedVisibilityQueries == 0)
            return fail("actual partial-shroud sampled-key omission positive witnesses missing");
        std::cout << "Radar sampled terrain actual: producerPairs=" << pairs
            << " nativePrunedPairs=" << sampledPositivePairs << " effectOnlyVisits=" << effectOnlyVisits
            << " omittedVisibilityQueries=" << omittedVisibilityQueries
            << " full512SquaredRawDestinationAndInternalDIB=exact allCacheCountersDiagnostics=exact"
            << " duplicateLastWriterAndResourceOR=exact signedAndNoncanonicalAliasPairs=2"
            << " sampledAndUnsampledOreGemShroudFogHotEditBrokenTMP=verified capFallback=262144\n";
        return true;
    }
    std::cout << "Radar pixel store actual: producerPairs=" << pairs
        << " full512SquaredRawDestinationAndInternalDIB=exact allCacheCountersDiagnostics=exact"
        << " actuallySampledHoleShroudVisibleFogOreGemHotEditBrokenTMP=verified"
        << " warmHitNoWriter=verified abnormalPrivateDCFallback=exact worldVisionAuthority=unchanged"
        << " directStores=" << actualDirectStores << " nativeReferenceStores=" << actualNativeStores
        << " fixture=" << root.string() << '\n';
    return true;
}
