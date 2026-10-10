// Issue 25.1: BEHIND markers must follow the resolved ground/building pixels.
// Unlike the legacy marker fixture, this uses a real flat Clear TMP and matching
// MapFacts, and proves every actor is a participant in the production depth pass.
bool runRuntimeGroundOcclusionMarkerIssue25Smoke(const RuntimeConfig& config, std::string& error) {
    const char* requested = std::getenv("CONCORD_ISSUE25_GROUND_MARKER_OUTPUT");
    const std::filesystem::path root = requested && *requested ? requested
        : "build/smoke/feature-set-25/01-ground-markers";
    std::filesystem::create_directories(root);
    std::string label;
    const auto fail = [&](const std::string& why) {
        error = "Issue25 ground marker " + label + ": " + why;
        std::cout << error << '\n';
        return false;
    };
    try {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config = config;
        state.config.playerSlots.clear();
        state.config.gameModeRulesOverride.clear();
        state.config.computerAiEnabled = state.config.multiplayerActive = false;
        state.config.crates = state.config.shortGame = false;
        state.report = buildRuntimeStartupReport(state.config, &state.world,
            &state.theater, &state.art, &state.rules, &state.mapFacts);
        if (!state.report.success) return fail("stock startup");
        state.rulesLoaded = state.artLoaded = state.theaterLoaded = true;
        state.visionEnabled = false;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.economy.owner = "Americans";
        state.ownerRemapColors["Americans"] = RGB(235, 210, 45);
        state.report.map.size.clear();
        state.report.map.localSize.clear();
        state.report.map.width = -1;

        const auto metadata = runtimeTerrainMetadataContext(state);
        const int clearSet = state.theater.getGeneralTileSetIndex("ClearTile");
        int clearTile = -1;
        for (int tile = 0; tile < static_cast<int>(state.theater.getTotalTileSlots()); ++tile) {
            TerrainCell cell;
            cell.tileIndex = tile;
            cell.subTileIndex = 0;
            if (state.theater.resolveTileIndex(tile).tileSetIndex == clearSet
                && resolveRuntimeTerrainSubTileLandType(cell, metadata, state.renderCache)
                    == RuntimeTerrainLandType::Clear
                && resolveRuntimeTerrainSubTileRampType(cell, metadata, state.renderCache) == 0) {
                clearTile = tile;
                break;
            }
        }
        if (clearTile < 0) return fail("original flat Clear TMP unavailable");
        state.world.accessObjects().clear();
        state.world.accessTerrainCells().clear();
        state.world.accessTerrainObjects().clear();
        state.world.accessSmudges().clear();
        state.mapFacts = {};
        for (int y = 10; y <= 36; ++y) for (int x = 10; x <= 36; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = clearTile;
            cell.subTileIndex = 0;
            state.world.accessTerrainCells().push_back(cell);
            MapCellFacts fact;
            fact.x = x;
            fact.y = y;
            fact.cellIndex = cell.cellIndex512;
            fact.hasTerrain = true;
            fact.terrain.x = x;
            fact.terrain.y = y;
            fact.terrain.tileIndex = clearTile;
            fact.terrain.subTileIndex = 0;
            fact.terrain.level = fact.heightLevel = 0;
            fact.terrainClass = "clear";
            state.mapFacts.cellIndexLookup.emplace(fact.cellIndex, state.mapFacts.cells.size());
            state.mapFacts.cells.push_back(fact);
        }
        state.mapFacts.terrainCellCount = state.mapFacts.lookupIndexSize = state.mapFacts.cells.size();
        state.mapFacts.contentHash = 0x2501BE01;
        state.mapFactsLoaded = true;
        if (!state.rules.findBuildingType("GAREFN") || !state.rules.findUnitType("infantry", "E1")
            || !state.rules.findUnitType("vehicle", "MTNK")) return fail("original actor rules unavailable");

        constexpr int width = 640, height = 480, buildingId = 25131, mobileId = 25132;
        const RECT viewport{0, 0, width, height};
        struct Surface {
            HDC dc = nullptr;
            HBITMAP bitmap = nullptr;
            HGDIOBJ previous = nullptr;
            std::uint32_t* pixels = nullptr;
            Surface() {
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = width;
                info.bmiHeader.biHeight = -height;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                dc = CreateCompatibleDC(nullptr);
                void* raw = nullptr;
                if (dc) bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
                pixels = static_cast<std::uint32_t*>(raw);
                if (bitmap && pixels) previous = SelectObject(dc, bitmap);
            }
            ~Surface() {
                if (previous && dc) SelectObject(dc, previous);
                if (bitmap) DeleteObject(bitmap);
                if (dc) DeleteDC(dc);
            }
        } surface;
        if (!surface.dc || !surface.pixels) return fail("DIB allocation");
        RuntimeMapRenderContext view;
        view.rules = &state.rules;
        view.art = &state.art;
        view.theater = &state.theater;
        view.mapFacts = &state.mapFacts;
        view.assetRootPath = runtimeAssetRootPath(state.config);
        view.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
        view.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
        view.palettePath = runtimePalettePath(state.config, state.report.map);
        view.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
        view.ownerRemapColors = &state.ownerRemapColors;
        view.animationTick = 17;
        view.fixedTickHz = 30;
        view.useTileTextures = view.shadowsEnabled = false;
        view.surfacePixels = surface.pixels;
        view.surfaceWidth = width;
        view.surfaceHeight = height;
        RuntimeSnapshotActiveStateScope active(state);
        auto cache = std::make_unique<RuntimeMapRenderCache>();
        std::ofstream rows(root / "issue25-ground-markers.csv");
        rows << "mobile,pose,zoom,qualified,depth_participant,body_pixels,overlap,hidden_pixels,"
            "enabled_markers,disabled_markers,missing,marker_pixels\n";
        // Isolate the original refinery body only in an in-memory ART overlay.
        // Keeping the complete source preserves untouched infantry/VXL sequences.
        IniFile originalArtIni, bodyOverlay;
        if (!loadRuntimeIniSource(state.config, RuntimeCampaignIniKind::Art, originalArtIni))
            return fail("original ART source for body-only controls");
        const auto* originalRefineryArt = state.art.findArtType("GAREFN");
        if (!originalRefineryArt) return fail("original refinery ART");
        std::string isolate = "[GAREFN]\nBibShape=\n";
        for (const auto* slots : {&originalRefineryArt->activeAnimations,
                &originalRefineryArt->specialAnimations, &originalRefineryArt->auxiliaryAnimations})
            for (const auto& slot : *slots) isolate += slot.key + "=\n";
        if (!bodyOverlay.loadFromString(isolate)) return fail("body-only ART overlay");
        originalArtIni.appendFrom(bodyOverlay);
        ArtDatabase bodyOnlyArt;
        if (!bodyOnlyArt.loadFromIni(originalArtIni)) return fail("body-only ART fixture");
        const auto* isolatedRefinery = bodyOnlyArt.findArtType("GAREFN");
        if (!isolatedRefinery || !isolatedRefinery->bibShape.empty()
            || !isolatedRefinery->activeAnimations.empty() || !isolatedRefinery->specialAnimations.empty()
            || !isolatedRefinery->auxiliaryAnimations.empty()) return fail("body isolation retained opaque layers");
        const auto* reactorRules = state.rules.findBuildingType("YAPOWR");
        const auto* reactorArt = state.art.findArtType("YAPOWR");
        if (!reactorRules || !reactorRules->infantryAbsorb || reactorRules->passengers != 5
            || !reactorArt || reactorArt->foundation != "2x2")
            return fail("stock YAPOWR alpha control contract");
        std::ofstream controlRows(root / "issue25-ground-marker-opacity.csv");
        controlRows << "mobile,control,qualified,depth_participant,partial_pixels,layered_mobile_pixels,"
            "enabled_markers,disabled_markers,marker_pixels,changed_from_opaque\n";
        const auto giArchive = RuntimeBuildingDepthOracle::readPinned(
            std::filesystem::current_path() / "local_data/resources/mix_exports/ra2/conquer.mix",
            "e0e4be2af64ce35da8e9c865b32ab56b342329de24d3d0a75b1c636f6928d714");
        constexpr std::size_t giOffset = 9819548, giLength = 186032;
        if (giOffset + giLength > giArchive.size()) return fail("original GI range");
        RuntimeBuildingDepthOracle::Bytes giBytes(giArchive.begin() + giOffset,
            giArchive.begin() + giOffset + giLength);
        if (RuntimeBuildingDepthOracle::sha256(giBytes)
                != "bceaee9c270d6382d80d509f2c4b2893c10565f779096b3add37a4d5321e8be1")
            return fail("original GI SHA-256");
        // N0 is Infantry Walk8, independently audited against original bytes.
        auto selectedGI = giBytes;
        std::copy_n(giBytes.begin() + 8 + 8 * 24, 24, selectedGI.begin() + 8);
        const auto originalGI = RuntimeBuildingDepthOracle::decodeFrameZero(selectedGI);
        if (originalGI.canvasWidth != 78 || originalGI.canvasHeight != 66
            || originalGI.x != 31 || originalGI.y != 9
            || originalGI.width != 16 || originalGI.height != 28
            || std::count_if(originalGI.pixels.begin(), originalGI.pixels.end(),
                [](unsigned char pixel) { return pixel != 0; }) != 257)
            return fail("original GI Walk8 shape");
        const auto refineryArchive = RuntimeBuildingDepthOracle::readPinned(
            std::filesystem::current_path() / "local_data/resources/mix_exports/ra2/generic.mix",
            "dbe8d76687c1787e4f17396cdf6c88fe507b916031261492fa6375fe944f2b70");
        // Independently audited original GGREFN.SHP entry, after pinning its archive.
        constexpr std::size_t refineryOffset = 13656606, refineryLength = 27328;
        if (refineryArchive.size() != 15810386
            || refineryOffset + refineryLength > refineryArchive.size())
            return fail("original generic refinery archive range differs");
        const RuntimeBuildingDepthOracle::Bytes originalRefineryBytes(
            refineryArchive.begin() + refineryOffset,
            refineryArchive.begin() + refineryOffset + refineryLength);
        if (RuntimeBuildingDepthOracle::sha256(originalRefineryBytes)
            != "2efe8ebec22dc70c58d59d2b453352bf4ee21ebf98e27a28a666b195fbf50b93")
            return fail("original generic GGREFN.SHP fingerprint differs");
        const auto originalRefinery = RuntimeBuildingDepthOracle::decodeFrameZero(originalRefineryBytes);
        const auto originalBuildingZ = RuntimeBuildingDepthOracle::decodeFrameZero(
            RuntimeBuildingDepthOracle::retailTemplate(std::filesystem::current_path()));

        std::vector<POINT> originalGISamples;
        for (int row = 0; row < 5; ++row) for (int column = 0; column < 5; ++column) {
            const int x = std::min(originalGI.width - 1, (2 * column + 1) * originalGI.width / 10);
            const int y = std::min(originalGI.height - 1, (2 * row + 1) * originalGI.height / 10);
            if (originalGI.pixels[static_cast<std::size_t>(y) * originalGI.width + x])
                originalGISamples.push_back({x, y});
        }
        if (originalGISamples.size() != 14) return fail("original GI sample witness count");

        std::ofstream hiddenRows(root / "issue25-ground-marker-hidden.csv");
        hiddenRows << "mobile,phase,normal_body_pixels,before_depth,after_depth,off_body_pixels,on_body_pixels,"
            "off_markers,on_markers,before_clicks,after_clicks\n";
        int hiddenCases = 0;

        int controlCases = 0;

        // Select one legal rear fixture from original opaque coverage alone.
        // Fresh solo caches exclude prior scale/animation/control artefacts.
        struct RearBodyInput {
            RuntimeShpFrameImage image;
            std::string key;
            double fitScale = 0;
        };
        std::array<RearBodyInput, 2> rearBodyInputs;
        const auto rawRearCoverage = [&](const RuntimeShpFrameImage& image,
                double rx, double ry, int zoom) {
            const int mx = static_cast<int>(std::lround((rx - ry) * zoom / 2.0));
            const int my = static_cast<int>(std::lround((rx + ry) * zoom / 4.0));
            const int left = mx - image.globalWidth / 2 + image.offsetX;
            const int top = my - image.globalHeight + image.offsetY;
            const int bx = -originalRefinery.canvasWidth / 2 + originalRefinery.x;
            const int by = 40 * zoom / 4 - originalRefinery.canvasHeight / 2 + originalRefinery.y;
            const int templateX = originalRefinery.x - originalRefinery.canvasWidth / 2
                + 198 - 30 * (4 - 3) + 30;
            const int templateY = originalRefinery.y - originalRefinery.canvasHeight / 2
                + 446 - 15 * (4 + 3 - 2) + 15;
            const int templateRows = std::min(originalRefinery.height, originalBuildingZ.height - templateY)
                - std::max(0, -templateY);
            const int nativeTop = 600 - originalRefinery.canvasHeight / 2 + originalRefinery.y;
            int samples = 0, covered = 0;
            for (int row = 0; row < 5; ++row) for (int column = 0; column < 5; ++column) {
                const int x = std::min(image.width - 1, (2 * column + 1) * image.width / 10);
                const int y = std::min(image.height - 1, (2 * row + 1) * image.height / 10);
                if (!image.pixels[static_cast<std::size_t>(y) * image.width + x]) continue;
                ++samples;
                const int u = left + x - bx, v = top + y - by;
                if (u < 0 || u >= originalRefinery.width || v < 0 || v >= originalRefinery.height
                    || !originalRefinery.pixels[static_cast<std::size_t>(v) * originalRefinery.width + u]) continue;
                const int zx = templateX + u, zy = templateY + v;
                if (zx < 0 || zx >= originalBuildingZ.width || zy < 0 || zy >= originalBuildingZ.height)
                    throw std::runtime_error("original refinery sample outside raw BUILDNGZ");
                const int rawShape = originalBuildingZ.pixels[static_cast<std::size_t>(zy) * originalBuildingZ.width + zx];
                int shapeDepth = rawShape ? rawShape - 65 : 0;
                if (shapeDepth > 127) shapeDepth -= 256;
                if (shapeDepth < -128) shapeDepth += 256;
                const int buildingDepth = 32768 - nativeTop - templateRows - 1 - shapeDepth;
                if (buildingDepth < 0 || buildingDepth >= 32768)
                    throw std::runtime_error("original opaque body has invalid native scene-Z");
                ++covered;
            }
            return std::pair{samples, covered};
        };
        for (int index = 0; index < 2; ++index) {
            const int zoom = index ? 60 : 48;
            WorldObjectInstance probe;
            probe.kind = WorldObjectKind::Unit;
            probe.type = "MTNK";
            probe.instanceId = mobileId;
            probe.owner = "Americans";
            probe.x = probe.y = 19;
            probe.cell = runtimeCellKey(19, 19);
            probe.hasRenderPosition = true;
            probe.renderX = probe.renderY = 19;
            probe.subCell = -1;
            probe.facing = 0;
            probe.turretFacing = 64;
            probe.alive = probe.poweredOperational = true;
            probe.health = runtimeMaxHealthForObject(state, probe);
            probe.mission = "Guard";
            GameWorld probeWorld = state.world;
            probeWorld.accessObjects() = {probe};
            RuntimeMapCamera probeCamera;
            probeCamera.initialized = true;
            probeCamera.tileWidth = zoom;
            probeCamera.tileHeight = zoom / 2;
            probeCamera.worldOffsetX = -width / 2;
            probeCamera.worldOffsetY = 22 * zoom / 2 - height / 2;
            auto probeCache = std::make_unique<RuntimeMapRenderCache>();
            auto probeView = view;
            probeView.seeHiddenObjects = false;
            const auto probeStats = drawRuntimeMap(surface.dc, viewport, probeWorld,
                probeView, probeCamera, *probeCache);
            GdiFlush();
            if (!probeStats.rendered) return fail("original solo body probe render");
            const RuntimeVoxelCacheEntry* body = nullptr;
            std::string bodyKey;
            int matchingBodies = 0;
            for (const auto& [key, raster] : probeCache->voxelObjectsByType) {
                if (!raster.loaded || !raster.hasSprite || raster.animationFrameIndex != 0
                    || raster.shadowSectionRequested != 0 || raster.facingBucket != 28
                    || toLowerAsciiLocal(raster.metadata.imageId) != "gtnk"
                    || !toLowerAsciiLocal(key).starts_with("mtnk|")) continue;
                ++matchingBodies;
                if (!body || raster.vehicleBodyPoseUse > body->vehicleBodyPoseUse) {
                    body = &raster;
                    bodyKey = key;
                }
            }
            if (!body || (!body->vehicleBodyPoseUse && matchingBodies != 1))
                return fail("fresh solo actual GTNK body cache is ambiguous");
            const RuntimeVoxelSourceCacheEntry* source = nullptr;
            for (const auto& [key, candidate] : probeCache->voxelSourcesByImage)
                if (candidate.loaded && candidate.metadata.vxlPath == body->metadata.vxlPath) source = &candidate;
            if (!source || RuntimeBuildingDepthOracle::sha256(source->vxlBytes)
                    != "88b326908acb5c2a98860d8f65169e8745ae858c5cbfe98e0bb2f283f49a3b81"
                || RuntimeBuildingDepthOracle::sha256(source->hvaBytes)
                    != "4a15fc9c2633667812465c4b08f28cab5d595ae4f80eddbb77d1a9f9ed0480a4")
                return fail("original GTNK body VXL/HVA fingerprint differs");
            rearBodyInputs[index] = {body->image, bodyKey, body->rasterFitScale};
            const auto& image = body->image;
            std::cout << "Issue25 original MTNK raster: zoom=" << zoom << " key=" << bodyKey
                << " fit=" << body->rasterFitScale << " global=" << image.globalWidth << 'x' << image.globalHeight
                << " crop=" << image.offsetX << ',' << image.offsetY << ',' << image.width << ',' << image.height
                << " poseUse=" << body->vehicleBodyPoseUse << " matching=" << matchingBodies << '\n';
            std::vector<std::uint32_t> rawMask(image.pixels.size());
            for (std::size_t p = 0; p < rawMask.size(); ++p) rawMask[p] = image.pixels[p] ? 0xffffffU : 0;
            if (!writeRuntimeSmokeBmp(pathString(root / ("MTNK-original-body-z" + std::to_string(zoom) + ".bmp")),
                    rawMask.data(), image.width, image.height, error)) return false;
        }
        constexpr std::array<std::array<double, 2>, 15> rearCandidates{{
            {19.0, 19.75}, {18.75, 20.0}, {19.25, 19.5}, {19.0, 19.5}, {18.5, 20.25},
            {18.75, 19.75}, {19.5, 19.25}, {19.25, 19.25}, {18.25, 20.5}, {19.0, 19.875},
            {18.875, 20.0}, {18.625, 20.25}, {18.375, 20.5}, {18.0, 20.75}, {19.0, 19.0}
        }};
        std::ofstream rearRows(root / "issue25-ground-marker-rear-original.csv");
        rearRows << "rx,ry,zoom,raw_samples,raw_covered,legal,rear_event\n";
        std::array<double, 2> rearPosition{};
        double bestRearCoverage = -1;
        for (const auto& candidate : rearCandidates) {
            const double rx = candidate[0], ry = candidate[1];
            const bool legal = std::floor(rx) < 20 || std::floor(ry) < 20;
            const bool rearEvent = (rx + ry + 1) * 256 < 40 * 256;
            bool majority = legal && rearEvent;
            double minimumCoverage = 1;
            for (int index = 0; index < 2; ++index) {
                const int zoom = index ? 60 : 48;
                const auto [samples, covered] = rawRearCoverage(rearBodyInputs[index].image, rx, ry, zoom);
                rearRows << rx << ',' << ry << ',' << zoom << ',' << samples << ',' << covered
                    << ',' << legal << ',' << rearEvent << '\n';
                majority = majority && samples > 0 && covered * 2 > samples;
                minimumCoverage = std::min(minimumCoverage, samples ? covered / static_cast<double>(samples) : 0);
            }
            if (majority && minimumCoverage > bestRearCoverage) {
                bestRearCoverage = minimumCoverage;
                rearPosition = candidate;
            }
        }
        rearRows.flush();
        if (bestRearCoverage < 0) return fail("finite legal original rear poses lack two-zoom majority");
        std::cout << "Issue25 original MTNK selected rear: rx=" << rearPosition[0] << " ry=" << rearPosition[1]
            << " minimumCoverage=" << bestRearCoverage << '\n';

        int cases = 0;
        for (bool infantry : {true, false}) for (bool front : {false, true}) for (int zoom : {48, 60}) {
            const std::string type = infantry ? "E1" : "MTNK";
            label = type + (front ? "-front" : "-behind") + "-z" + std::to_string(zoom);
            WorldObjectInstance building;
            building.kind = WorldObjectKind::Structure;
            building.type = "GAREFN";
            building.instanceId = buildingId;
            building.owner = "Americans";
            building.x = building.y = 20;
            building.cell = runtimeCellKey(20, 20);
            building.alive = building.poweredOperational = true;
            building.health = runtimeMaxHealthForObject(state, building);
            building.mission = "Guard";
            WorldObjectInstance mobile;
            mobile.kind = infantry ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            mobile.type = type;
            mobile.instanceId = mobileId;
            mobile.owner = "Americans";
            mobile.x = front ? (infantry ? 22 : 21) : infantry ? 20 : static_cast<int>(std::floor(rearPosition[0]));
            mobile.y = front ? 23 : infantry ? 19 : static_cast<int>(std::floor(rearPosition[1]));
            mobile.cell = runtimeCellKey(mobile.x, mobile.y);
            mobile.hasRenderPosition = true;
            mobile.renderX = front ? mobile.x + .25 : infantry ? 19.875 : rearPosition[0];
            mobile.renderY = front ? mobile.y + .25 : infantry ? 19.625 : rearPosition[1];
            mobile.renderLevel = 0;
            mobile.subCell = infantry ? 4 : -1;
            mobile.facing = 0;
            mobile.turretFacing = 64;
            mobile.alive = mobile.poweredOperational = true;
            mobile.health = runtimeMaxHealthForObject(state, mobile);
            mobile.mission = "Guard";
            if (infantry) {
                mobile.renderMoveActive = true;
                mobile.movementAnimationDistance = 0;
            }
            state.world.accessObjects() = {building, mobile};
            cache = std::make_unique<RuntimeMapRenderCache>();
            state.camera.initialized = true;
            state.camera.tileWidth = zoom;
            state.camera.tileHeight = zoom / 2;
            state.camera.worldOffsetX = -width / 2;
            state.camera.worldOffsetY = 22 * zoom / 2 - height / 2;
            const bool qualified = infantry
                ? runtimeGroundInfantryUsesBuildingDepth(mobile, state.world, view, *cache)
                : runtimeGroundVehicleUsesBuildingDepth(mobile, state.world, view, *cache);
            if (!qualified) return fail("real TMP/MapFacts ground qualification rejected");
            std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>> visible;
            if (!resolveRuntimeBuildingVisibleHitSpans(state.world, viewport, view, state.camera,
                    *cache, visible) || !visible.contains(mobileId) || !visible.contains(buildingId))
                return fail("actor did not enter production building depth");

            GameWorld renderWorld = state.world;
            const auto paint = [&](const std::vector<WorldObjectInstance>& objects, bool markers) {
                renderWorld.accessObjects() = objects;
                auto context = view;
                context.seeHiddenObjects = markers;
                auto camera = state.camera;
                const auto stats = drawRuntimeMap(surface.dc, viewport, renderWorld, context, camera, *cache);
                if (!stats.rendered) throw std::runtime_error("production render");
                GdiFlush();
                return std::pair{stats, std::vector<std::uint32_t>(surface.pixels,
                    surface.pixels + static_cast<std::size_t>(width) * height)};
            };
            const auto blank = paint({}, false).second;
            const auto buildingOnly = paint({building}, false).second;
            const auto mobileOnly = paint({mobile}, false).second;
            const auto disabled = paint({building, mobile}, false);
            const auto enabled = paint({building, mobile}, true);
            int bodyPixels = 0, overlap = 0, hiddenPixels = 0, markerPixels = 0;
            for (std::size_t p = 0; p < blank.size(); ++p) {
                const auto rgb = [](std::uint32_t color) { return color & 0xffffffU; };
                if (rgb(mobileOnly[p]) != rgb(blank[p])) {
                    ++bodyPixels;
                    if (rgb(buildingOnly[p]) != rgb(blank[p])) {
                        ++overlap;
                        if (rgb(disabled.second[p]) == rgb(buildingOnly[p])
                            && rgb(mobileOnly[p]) != rgb(buildingOnly[p])) ++hiddenPixels;
                    }
                }
                if (rgb(disabled.second[p]) != rgb(enabled.second[p])) ++markerPixels;
            }
            rows << type << ',' << (front ? "front" : "behind") << ',' << zoom << ','
                << qualified << ",1," << bodyPixels << ',' << overlap << ',' << hiddenPixels << ','
                << enabled.first.behindMarkerSprites << ',' << disabled.first.behindMarkerSprites << ','
                << enabled.first.behindMarkerMissing << ',' << markerPixels << '\n';
            rows.flush();
            if (!writeRuntimeSmokeBmp(pathString(root / (label + "-enabled.bmp")), enabled.second.data(), width, height, error) || !writeRuntimeSmokeBmp(pathString(root / (label + "-disabled.bmp")), disabled.second.data(), width, height, error)) return false;
            if (!bodyPixels || !overlap || (!front && !hiddenPixels))
                return fail("fixed original actors have no real body/occlusion witness");
            if (!infantry && !front) {
                // The original read-only mobile event precedes the building:
                // a later opaque body writes scene Z over these raw samples.
                // This proves fixture coverage without looking at marker stats.
                if ((mobile.renderX + mobile.renderY + 1) * 256 >= 40 * 256)
                    return fail("fixed original rear event is not before the refinery body");
                const auto& originalInput = rearBodyInputs[zoom == 60 ? 1 : 0];
                const auto originalBody = cache->voxelObjectsByType.find(originalInput.key);
                if (originalBody == cache->voxelObjectsByType.end() || !originalBody->second.loaded
                    || originalBody->second.image.pixels != originalInput.image.pixels
                    || originalBody->second.image.globalWidth != originalInput.image.globalWidth
                    || originalBody->second.image.globalHeight != originalInput.image.globalHeight
                    || originalBody->second.image.offsetX != originalInput.image.offsetX
                    || originalBody->second.image.offsetY != originalInput.image.offsetY)
                    return fail("current production body raster differs from original solo input");
                const auto [rawSamples, rawCovered] = rawRearCoverage(originalInput.image,
                    mobile.renderX, mobile.renderY, zoom);
                std::cout << "Issue25 original MTNK rear witness: zoom=" << zoom
                    << " rx=" << mobile.renderX << " ry=" << mobile.renderY
                    << " samples=" << rawSamples << " opaqueCovered=" << rawCovered << '\n';
                if (rawSamples <= 0 || rawCovered * 2 < rawSamples)
                    return fail("fixed original rear body sample coverage lacks a majority");
            }

            if (enabled.first.behindMarkerSprites != (front ? 0 : 1)
                || disabled.first.behindMarkerSprites != 0
                || enabled.first.behindMarkerMissing || disabled.first.behindMarkerMissing)
                return fail("markers enabled=" + std::to_string(enabled.first.behindMarkerSprites)
                    + " disabled=" + std::to_string(disabled.first.behindMarkerSprites)
                    + " missing=" + std::to_string(enabled.first.behindMarkerMissing));
            if (front ? markerPixels != 0 : markerPixels == 0)
                return fail("option pixel effect does not match marker ownership");

            if (!front && zoom == 60) {
                const auto difference = [](const auto& a, const auto& b) {
                    int count = 0;
                    for (std::size_t p = 0; p < a.size(); ++p)
                        count += (a[p] & 0xffffffU) != (b[p] & 0xffffffU);
                    return count;
                };
                const auto control = [&](const char* name, const WorldObjectInstance& host,
                        const WorldObjectInstance& actor, const RuntimeMapRenderContext& controlView,
                        RuntimeMapRenderCache& controlCache, int wanted, int partial, int layered,
                        const std::vector<std::uint32_t>* opaqueReference = nullptr) {
                    const std::string controlLabel = type + "-" + name;
                    state.world.accessObjects() = {host, actor};
                    const bool admitted = infantry
                        ? runtimeGroundInfantryUsesBuildingDepth(actor, state.world, controlView, controlCache)
                        : runtimeGroundVehicleUsesBuildingDepth(actor, state.world, controlView, controlCache);
                    std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>> participants;
                    if (!admitted || !resolveRuntimeBuildingVisibleHitSpans(state.world, viewport,
                            controlView, state.camera, controlCache, participants)
                        || !participants.contains(mobileId) || !participants.contains(buildingId))
                        return fail(controlLabel + " lacks real production depth participation");
                    GameWorld controlWorld = state.world;
                    const auto image = [&](bool seeHidden) {
                        auto context = controlView;
                        context.seeHiddenObjects = seeHidden;
                        auto camera = state.camera;
                        const auto stats = drawRuntimeMap(surface.dc, viewport, controlWorld,
                            context, camera, controlCache);
                        if (!stats.rendered) throw std::runtime_error(controlLabel + " render");
                        GdiFlush();
                        return std::pair{stats, std::vector<std::uint32_t>(surface.pixels,
                            surface.pixels + static_cast<std::size_t>(width) * height)};
                    };
                    const auto off = image(false), on = image(true);
                    const int markerDifference = difference(off.second, on.second);
                    const int changed = opaqueReference ? difference(off.second, *opaqueReference) : 0;
                    controlRows << type << ',' << name << ",1,1," << partial << ',' << layered << ','
                        << on.first.behindMarkerSprites << ',' << off.first.behindMarkerSprites << ','
                        << markerDifference << ',' << changed << '\n';
                    controlRows.flush();
                    if (!writeRuntimeSmokeBmp(pathString(root / (controlLabel + "-enabled.bmp")),
                            on.second.data(), width, height, error)
                        || !writeRuntimeSmokeBmp(pathString(root / (controlLabel + "-disabled.bmp")),
                            off.second.data(), width, height, error)) return false;
                    if (on.first.behindMarkerSprites != wanted || off.first.behindMarkerSprites
                        || on.first.behindMarkerMissing || off.first.behindMarkerMissing
                        || (wanted ? !markerDifference : markerDifference != 0)
                        || (opaqueReference && !changed))
                        return fail(controlLabel + " markers=" + std::to_string(on.first.behindMarkerSprites)
                            + " off=" + std::to_string(off.first.behindMarkerSprites)
                            + " optionPixels=" + std::to_string(markerDifference)
                            + " changed=" + std::to_string(changed));
                    ++controlCases;
                    return true;
                };
                const auto preview = [&](const WorldObjectInstance& host,
                        const RuntimeMapRenderContext& previewView, RuntimeMapRenderCache& previewCache,
                        std::uint32_t background) {
                    std::fill_n(surface.pixels, static_cast<std::size_t>(width) * height, background);
                    if (!drawRuntimeObjectPreviewSprite(surface.dc, viewport, host, previewView,
                            state.camera, previewCache)) throw std::runtime_error("original body preview");
                    GdiFlush();
                    return std::vector<std::uint32_t>(surface.pixels,
                        surface.pixels + static_cast<std::size_t>(width) * height);
                };

                // Selection and ownership do not change the original depth.
                // The base matrix above already uses unselected local bodies.
                if (infantry) {
                    auto enemy = mobile;
                    enemy.owner = "Russians";
                    state.ownerRemapColors["Russians"] = RGB(230, 45, 40);
                    if (!control("enemy-unselected", building, enemy, view, *cache, 1, 0, 0))
                        return false;
                } else {
                    std::vector<int> selected{mobileId};
                    auto selectedView = view;
                    selectedView.selectedObjectIds = &selected;
                    if (!control("own-selected", building, mobile, selectedView, *cache, 1, 0, 0))
                        return false;
                }


                // Exactly the existing temporal rendering snapshot: whole-body 128.
                SpecialMechanismRenderSnapshot freeze;
                freeze.kind = "temporal";
                freeze.targetObjectId = buildingId;
                freeze.requiredProgress = 10000;
                freeze.progress = 100;
                std::vector<SpecialMechanismRenderSnapshot> snapshots{freeze};
                auto frozenView = view;
                frozenView.specialMechanismSnapshots = &snapshots;
                auto frozenCache = std::make_unique<RuntimeMapRenderCache>();
                if (!control("frozen128", building, mobile, frozenView, *frozenCache, 0, 0, 0,
                        &disabled.second)) return false;

                auto bodyView = view;
                bodyView.art = &bodyOnlyArt;
                auto bodyCache = std::make_unique<RuntimeMapRenderCache>();
                const auto opaqueBlack = preview(building, bodyView, *bodyCache, 0);
                const auto opaqueWhite = preview(building, bodyView, *bodyCache, 0xffffffU);
                // Original SHP/VXL body rasters, independent of marker state.
                // The black/white body-only preview proves opaque coverage at
                // the original raster sample locations before the scene assertion.
                std::vector<POINT> bodySamplePoints;
                const double bodyRx = infantry ? mobile.renderX + .25 : mobile.renderX;
                const double bodyRy = infantry ? mobile.renderY + .25 : mobile.renderY;
                const int bodyCx = static_cast<int>(std::lround((bodyRx - bodyRy) * zoom / 2.0))
                    - state.camera.worldOffsetX;
                const int bodyMy = static_cast<int>(std::lround((bodyRx + bodyRy) * zoom / 4.0))
                    - state.camera.worldOffsetY;
                if (infantry) {
                    const int left = bodyCx - originalGI.canvasWidth / 2 + originalGI.x;
                    const int top = bodyMy + zoom / 4 - originalGI.canvasHeight + originalGI.y;
                    for (const auto& sample : originalGISamples)
                        bodySamplePoints.push_back({left + sample.x, top + sample.y});
                } else {
                    const RuntimeShpFrameImage* rawBody = nullptr;
                    std::uint64_t rawBodyUse = 0;
                    for (const auto& [key, raster] : cache->voxelObjectsByType) {
                        if (!raster.loaded || !raster.hasSprite || raster.animationFrameIndex != 0
                            || raster.facingBucket != 28 || !toLowerAsciiLocal(key).starts_with("mtnk|")
                            || toLowerAsciiLocal(key).starts_with("mtnk|turret|")
                            || toLowerAsciiLocal(key).starts_with("mtnk|barrel|")) continue;
                        if (!rawBody || raster.vehicleBodyPoseUse >= rawBodyUse) {
                            rawBody = &raster.image;
                            rawBodyUse = raster.vehicleBodyPoseUse;
                        }
                    }
                    if (!rawBody || rawBody->width <= 0 || rawBody->height <= 0
                        || rawBody->pixels.size() < static_cast<std::size_t>(rawBody->width) * rawBody->height)
                        return fail("original MTNK body raster unavailable for opacity samples");
                    const int left = bodyCx - rawBody->globalWidth / 2 + rawBody->offsetX;
                    const int top = bodyMy - rawBody->globalHeight + rawBody->offsetY;
                    for (int row = 0; row < 5; ++row) for (int column = 0; column < 5; ++column) {
                        const int x = std::min(rawBody->width - 1, (2 * column + 1) * rawBody->width / 10);
                        const int y = std::min(rawBody->height - 1, (2 * row + 1) * rawBody->height / 10);
                        if (rawBody->pixels[static_cast<std::size_t>(y) * rawBody->width + x])
                            bodySamplePoints.push_back({left + x, top + y});
                    }
                }
                int bodyOpaqueSamples = 0;
                for (const auto& point : bodySamplePoints) {
                    if (point.x < 0 || point.x >= width || point.y < 0 || point.y >= height) continue;
                    const auto p = static_cast<std::size_t>(point.y) * width + point.x;
                    bodyOpaqueSamples += (opaqueBlack[p] & 0xffffffU) == (opaqueWhite[p] & 0xffffffU);
                }
                std::cout << "Issue25 original body opacity witness: " << type
                    << " samples=" << bodySamplePoints.size()
                    << " opaqueSamples=" << bodyOpaqueSamples << '\n';
                if (bodySamplePoints.empty() || bodyOpaqueSamples * 2 < static_cast<int>(bodySamplePoints.size()))
                    return fail("body-only original raster samples lack a majority of opaque original building coverage");

                if (!control("body-opaque255", building, mobile, bodyView, *bodyCache, 1, 0, bodyOpaqueSamples))
                    return false;
                auto cloaked = building;
                cloaked.cloaked = true;
                const auto cloakBlack = preview(cloaked, bodyView, *bodyCache, 0);
                const auto cloakWhite = preview(cloaked, bodyView, *bodyCache, 0xffffffU);
                int cloakPartial = 0, cloakMobile = 0;
                for (std::size_t p = 0; p < blank.size(); ++p) {
                    const auto black = cloakBlack[p] & 0xffffffU, white = cloakWhite[p] & 0xffffffU;
                    if (black == white || (black == 0 && white == 0xffffffU)) continue;
                    ++cloakPartial;
                    if ((opaqueBlack[p] & 0xffffffU) == (opaqueWhite[p] & 0xffffffU)
                        && (mobileOnly[p] & 0xffffffU) != (blank[p] & 0xffffffU)) ++cloakMobile;
                }
                if (!cloakPartial || !cloakMobile)
                    return fail("body cloak144 has no independently visible partial/mobile overlap");
                if (!control("body-cloak144", cloaked, mobile, bodyView, *bodyCache, 0,
                        cloakPartial, cloakMobile)) return false;

                // Legal stock reactor occupancy: A is opaque; B has 2*255/5=102.
                // Black/white invariance proves opaque coverage. Changed color
                // between 0/2 occupants proves an actual alpha layer on that base.
                if (infantry) {
                auto reactor = building;
                reactor.type = "YAPOWR";
                reactor.health = reactorRules->strength;
                auto reactorMobile = mobile;

                auto reactorCache = std::make_unique<RuntimeMapRenderCache>();
                const auto emptyBlack = preview(reactor, view, *reactorCache, 0);
                const auto emptyWhite = preview(reactor, view, *reactorCache, 0xffffffU);
                reactor.transportPassengerIds = {25141, 25142};
                const auto occupiedBlack = preview(reactor, view, *reactorCache, 0);
                const auto occupiedWhite = preview(reactor, view, *reactorCache, 0xffffffU);
                // Choose from a fixed legal perimeter list using original GI
                // samples and independent previews, never production marker stats.
                // The selected fixture still has to satisfy final scene-Z marker=1.
                {
                    bool found = false;
                    for (const auto& coordinate : std::array<std::array<double, 2>, 9>{{
                            {20.125, 19.875}, {20.0, 19.75}, {19.875, 21.125},
                            {19.875, 20.875}, {21.0, 19.875}, {21.5, 19.875},
                            {19.875, 20.125}, {19.875, 20.5}, {20.125, 21.875}}}) {
                        const double rx = coordinate[0], ry = coordinate[1];
                        // Reactor foundation [20,22)x[20,22): every tested point
                        // must be outside it, including the fractional location.
                        if (rx >= 20 && rx < 22 && ry >= 20 && ry < 22) continue;
                        const int cx = static_cast<int>(std::lround((rx - ry) * zoom / 2.0))
                            - state.camera.worldOffsetX;
                        const int my = static_cast<int>(std::lround((rx + ry) * zoom / 4.0))
                            - state.camera.worldOffsetY;
                        const int left = cx - originalGI.canvasWidth / 2 + originalGI.x;
                        const int top = my + zoom / 4 - originalGI.canvasHeight + originalGI.y;
                        int opaqueSamples = 0, alphaOnOpaqueSamples = 0;
                        for (const auto& sample : originalGISamples) {
                            const int px = left + sample.x, py = top + sample.y;
                            if (px < 0 || px >= width || py < 0 || py >= height) continue;
                            const auto p = static_cast<std::size_t>(py) * width + px;
                            const auto a0 = emptyBlack[p] & 0xffffffU, a1 = emptyWhite[p] & 0xffffffU;
                            const auto b0 = occupiedBlack[p] & 0xffffffU, b1 = occupiedWhite[p] & 0xffffffU;
                            if (a0 == a1 && b0 == b1) {
                                ++opaqueSamples;
                                if (a0 != b0) ++alphaOnOpaqueSamples;
                            }
                        }
                        if (opaqueSamples * 2 < static_cast<int>(originalGISamples.size())
                            || alphaOnOpaqueSamples == 0) continue;
                        reactorMobile.x = static_cast<int>(std::floor(rx));
                        reactorMobile.y = static_cast<int>(std::floor(ry));
                        reactorMobile.cell = runtimeCellKey(reactorMobile.x, reactorMobile.y);
                        reactorMobile.renderX = rx - .25;
                        reactorMobile.renderY = ry - .25;
                        std::cout << "Issue25 original reactor marker witness: rx=" << rx << " ry=" << ry
                            << " opaqueSamples=" << opaqueSamples
                            << " alphaOnOpaqueSamples=" << alphaOnOpaqueSamples << '\n';
                        found = true;
                        break;
                    }
                    if (!found) return fail("fixed legal reactor perimeter has no original alpha-on-opaque sample witness");
                }

                GameWorld soloWorld = state.world;
                soloWorld.accessObjects() = {reactorMobile};
                auto soloContext = view;
                soloContext.seeHiddenObjects = false;
                auto soloCamera = state.camera;
                if (!drawRuntimeMap(surface.dc, viewport, soloWorld, soloContext, soloCamera,
                        *reactorCache).rendered) return fail("reactor mobile original solo render");
                GdiFlush();
                const std::vector<std::uint32_t> reactorMobileOnly(surface.pixels,
                    surface.pixels + static_cast<std::size_t>(width) * height);
                int reactorPartial = 0, layeredMobile = 0;
                for (std::size_t p = 0; p < blank.size(); ++p) {
                    const auto a0 = emptyBlack[p] & 0xffffffU, a1 = emptyWhite[p] & 0xffffffU;
                    const auto b0 = occupiedBlack[p] & 0xffffffU, b1 = occupiedWhite[p] & 0xffffffU;
                    if (b0 != b1 && !(b0 == 0 && b1 == 0xffffffU)) ++reactorPartial;
                    if (a0 == a1 && b0 == b1 && a0 != b0
                        && (reactorMobileOnly[p] & 0xffffffU) != (blank[p] & 0xffffffU))
                        ++layeredMobile;
                }
                if (!layeredMobile)
                    return fail("stock YAPOWR A255+B102 lacks independent layered mobile witness: partial="
                        + std::to_string(reactorPartial) + " layered=" + std::to_string(layeredMobile));
                if (!control("reactor-opaque-plus-alpha102", reactor, reactorMobile, view,
                        *reactorCache, 1, reactorPartial, layeredMobile)) return false;
                }
                // A real chrono request supplies the departure state. The
                // supported persisted InTransit phase uses the same hidden
                // body contract, independently of Behind option and selection.
                const std::string chronoType = infantry ? "CLEG" : "CMIN";
                auto chronoActor = mobile;
                chronoActor.type = chronoType;
                chronoActor.health = runtimeMaxHealthForObject(state, chronoActor);
                state.world.accessObjects() = {building, chronoActor};
                auto chronoCache = std::make_unique<RuntimeMapRenderCache>();
                const bool chronoNormal = infantry
                    ? runtimeGroundInfantryUsesBuildingDepth(chronoActor, state.world, view, *chronoCache)
                    : runtimeGroundVehicleUsesBuildingDepth(chronoActor, state.world, view, *chronoCache);
                std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>> normalParticipants;
                if (!chronoNormal || !resolveRuntimeBuildingVisibleHitSpans(state.world, viewport,
                        view, state.camera, *chronoCache, normalParticipants)
                    || !normalParticipants.contains(mobileId) || !normalParticipants.contains(buildingId))
                    return fail(chronoType + " normal body must participate before hidden control");
                GameWorld soloChrono = state.world;
                soloChrono.accessObjects() = {chronoActor};
                auto normalView = view;
                normalView.seeHiddenObjects = false;
                auto normalCamera = state.camera;
                if (!drawRuntimeMap(surface.dc, viewport, soloChrono, normalView,
                        normalCamera, *chronoCache).rendered)
                    return fail(chronoType + " original normal solo body");
                GdiFlush();
                const std::vector<std::uint32_t> normalBody(surface.pixels,
                    surface.pixels + static_cast<std::size_t>(width) * height);
                const int normalPixels = difference(normalBody, blank);
                if (!normalPixels) return fail(chronoType + " normal source body unavailable");

                RuntimeSpecialMechanismState chronoMechanism;
                chronoMechanism.reset(state.rules.getSpecialMechanismRules());
                auto departure = state.world.getObjects();
                const SpecialCellPredicate legal = [&](int x, int y, int) {
                    return state.mapFacts.cellIndexLookup.contains(runtimeCellKey(x, y))
                        && (x < 20 || x >= 24 || y < 20 || y >= 23);
                };
                const SpecialCellPredicate free = [](int, int, int) { return false; };
                std::string chronoReason;
                if (!chronoMechanism.requestChronoMove(departure, state.rules, mobileId,
                        {25, 19}, 0, view.animationTick, legal, free, {}, chronoReason, view.fixedTickHz))
                    return fail(chronoType + " legal departure request: " + chronoReason);
                std::ostringstream chronoSaved;
                chronoMechanism.save(chronoSaved);
                for (const std::string phase : {"WarpOut", "InTransit"}) {
                    label = chronoType + "-hidden-" + phase;
                    std::string serialized = chronoSaved.str();
                    const auto phaseAt = serialized.find("|WarpOut|");
                    if (phaseAt == std::string::npos) return fail("departure state serialization");
                    serialized.replace(phaseAt, std::string("|WarpOut|").size(), "|" + phase + "|");
                    IniFile persistedChrono;
                    state.specialMechanisms.reset(state.rules.getSpecialMechanismRules());
                    if (!persistedChrono.loadFromString(serialized)
                        || !state.specialMechanisms.load(persistedChrono, chronoReason)
                        || !state.specialMechanisms.isChronoInTransit(mobileId))
                        return fail("legal persisted hidden phase: " + chronoReason);
                    state.specialMechanismsInitialized = true;
                    state.specialMechanismRenderSnapshots = state.specialMechanisms.renderSnapshots(
                        view.animationTick, view.fixedTickHz);
                    if (state.specialMechanismRenderSnapshots.size() != 1
                        || state.specialMechanismRenderSnapshots.front().kind != "chrono"
                        || state.specialMechanismRenderSnapshots.front().phase != phase
                        || !state.specialMechanismRenderSnapshots.front().resourceId.empty())
                        return fail("actual hidden snapshot contract");
                    state.world.accessObjects() = departure;
                    // Navigation can normalize render poses. Restore the exact
                    // source objects before both projection and actual paint.
                    initializeRuntimeNavigation(state);
                    state.world.accessObjects() = departure;
                    state.structureFootprints = buildRuntimeStructureFootprints(state);
                    state.tick = view.animationTick;
                    invalidateRuntimeObjectProjectionCache(state);
                    auto hiddenView = view;
                    hiddenView.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
                    std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>> hiddenBefore, hiddenAfter;
                    if (!resolveRuntimeBuildingVisibleHitSpans(state.world, viewport, hiddenView,
                            state.camera, *chronoCache, hiddenBefore)
                        || hiddenBefore.contains(mobileId) || !hiddenBefore.contains(buildingId))
                        return fail("hidden body published a pre-paint depth participant");
                    const auto projectedBefore = runtimeProjectedObjectsForViewport(state, viewport);
                    const auto hiddenPaint = [&](bool withActor, bool seeBehind) {
                        GameWorld scene = state.world;
                        if (!withActor) scene.accessObjects() = {building};
                        auto context = hiddenView;
                        context.seeHiddenObjects = seeBehind;
                        auto camera = state.camera;
                        const auto stats = drawRuntimeMap(surface.dc, viewport, scene, context,
                            camera, *chronoCache);
                        if (!stats.rendered) throw std::runtime_error(label + " hidden render");
                        GdiFlush();
                        return std::pair{stats, std::vector<std::uint32_t>(surface.pixels,
                            surface.pixels + static_cast<std::size_t>(width) * height)};
                    };
                    const auto absent = hiddenPaint(false, false);
                    const auto off = hiddenPaint(true, false), on = hiddenPaint(true, true);
                    const int offPixels = difference(off.second, absent.second);
                    const int onPixels = difference(on.second, absent.second);
                    invalidateRuntimeObjectProjectionCache(state);
                    const auto projectedAfter = runtimeProjectedObjectsForViewport(state, viewport);
                    if (!resolveRuntimeBuildingVisibleHitSpans(state.world, viewport, hiddenView,
                            state.camera, *chronoCache, hiddenAfter))
                        return fail("hidden post-paint depth resolution");
                    int beforeClicks = 0, afterClicks = 0;
                    for (std::size_t p = 0; p < normalBody.size(); ++p) {
                        if ((normalBody[p] & 0xffffffU) == (blank[p] & 0xffffffU)) continue;
                        const POINT point{static_cast<LONG>(p % width), static_cast<LONG>(p / width)};
                        const auto* a = findProjectedObjectAtPoint(projectedBefore, point);
                        const auto* b = findProjectedObjectAtPoint(projectedAfter, point);
                        beforeClicks += a && a->object && a->object->instanceId == mobileId;
                        afterClicks += b && b->object && b->object->instanceId == mobileId;
                    }
                    hiddenRows << chronoType << ',' << phase << ',' << normalPixels << ','
                        << hiddenBefore.contains(mobileId) << ',' << hiddenAfter.contains(mobileId) << ','
                        << offPixels << ',' << onPixels << ',' << off.first.behindMarkerSprites << ','
                        << on.first.behindMarkerSprites << ',' << beforeClicks << ',' << afterClicks << '\n';
                    hiddenRows.flush();
                    if (!writeRuntimeSmokeBmp(pathString(root / (label + "-enabled.bmp")),
                            on.second.data(), width, height, error)
                        || !writeRuntimeSmokeBmp(pathString(root / (label + "-disabled.bmp")),
                            off.second.data(), width, height, error)) return false;
                    if (offPixels || onPixels || off.first.behindMarkerSprites || on.first.behindMarkerSprites
                        || off.first.behindMarkerMissing || on.first.behindMarkerMissing
                        || hiddenAfter.contains(mobileId) || beforeClicks || afterClicks)
                        return fail("hidden body leaked pixels, depth, marker or click");
                    ++hiddenCases;
                }
                state.specialMechanisms.reset(state.rules.getSpecialMechanismRules());
                state.specialMechanismsInitialized = false;
                state.specialMechanismRenderSnapshots.clear();
                state.world.accessObjects() = {building, mobile};
            cache = std::make_unique<RuntimeMapRenderCache>();
                invalidateRuntimeObjectProjectionCache(state);

            }

            ++cases;
        }
        hiddenRows.close();
        if (!hiddenRows.good() || hiddenCases != 4) return fail("hidden control evidence/count");
        controlRows.close();
        if (!controlRows.good()) return fail("opacity control evidence write");
        rows.close();
        if (!rows.good()) return fail("evidence write");
        std::cout << "Issue25 ground marker: cases=" << cases << " stateAndOpacityControls=" << controlCases << " hiddenControls=" << hiddenCases
            << " original E1/MTNK, real Clear TMP/MapFacts, depth participants,"
            " behind=1 front=0 disabled=0 and marker pixels PASS\n";
        error.clear();
        return true;
    } catch (const std::exception& detail) {
        return fail(detail.what());
    }
}





