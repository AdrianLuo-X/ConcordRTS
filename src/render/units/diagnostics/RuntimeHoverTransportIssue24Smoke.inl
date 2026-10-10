// Issue24: four original VXL types, two controlled simulation-time phases.
// Reuses the existing issue22 renderer-TU DIB/raw TMP tools, never a new lift helper.
namespace RuntimeHoverTransportIssue24 {
namespace Original = RuntimeVehicleGroundShadowIssue22;
struct Scene { const char* id; const char* owner; bool water; bool hover; };

GameWorld makeWorld(const Scene& scene, int tileIndex, int subTileIndex,
    int strength, std::string& error) {
    IniFile ini; MapDatabase map; GameWorld world;
    if (!ini.loadFromString("[Basic]\nName=Issue24FiniteHover\n[Map]\nTheater=TEMPERATE\nSize=0,0,128,128\nLocalSize=54,54,29,29\n")
        || !map.loadFromIni(ini) || !world.loadFromMap(map)) {
        error = "issue24 isolated map load failed"; return world;
    }
    for (int y = 54; y <= 82; ++y) for (int x = 54; x <= 82; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = tileIndex; cell.subTileIndex = subTileIndex; cell.level = 0;
        world.accessTerrainCells().push_back(cell);
    }
    WorldObjectInstance unit; unit.instanceId = 1; unit.kind = WorldObjectKind::Unit;
    unit.type = scene.id; unit.owner = scene.owner; unit.alive = true; unit.health = strength;
    unit.x = unit.y = 65; unit.cell = 65065; unit.level = 0; unit.subCell = -1;
    unit.renderX = unit.renderY = 65; unit.renderLevel = 0;
    unit.facing = unit.turretFacing = 96; unit.mission = "Guard";
    world.accessObjects().push_back(unit);
    return world;
}
}

bool runRuntimeHoverTransportIssue24Smoke(const RuntimeMapRenderContext& sourceContext,
    std::string& error) {
    using namespace RuntimeHoverTransportIssue24;
    error.clear();
    try {
        const char* outputValue = std::getenv("CONCORD_HOVER_TRANSPORT_ISSUE24_OUTPUT");
        const char* expectation = std::getenv("CONCORD_HOVER_TRANSPORT_ISSUE24_EXPECT_LIFT");
        if (!outputValue || !*outputValue || !expectation
            || (std::string(expectation) != "0" && std::string(expectation) != "1")) {
            error = "issue24 private OUTPUT and exact EXPECT_LIFT=0/1 required"; return false;
        }
        const bool expectLift = std::string(expectation) == "1";
        const std::filesystem::path output(outputValue);
        std::filesystem::create_directories(output / "native-original");
        if (std::filesystem::exists(output / "frames.csv")) { error = "issue24 output exists"; return false; }
        if (!sourceContext.theater || !sourceContext.rules || !sourceContext.art
            || sourceContext.palettePath.empty() || sourceContext.structurePalettePath.empty()
            || sourceContext.theater->getTileExtension() != ".tem") {
            error = "original TEM rules/art/palettes required"; return false;
        }
        RuntimeMapRenderContext context = sourceContext;
        context.mapFacts = nullptr; context.projectileActors = nullptr; context.impactActors = nullptr;
        context.wrecks = nullptr; context.buildingCollapses = nullptr;
        context.particleSnapshots = nullptr; context.particleBeamSnapshots = nullptr;
        context.localLightSnapshots = nullptr; context.specialMechanismSnapshots = nullptr;
        context.selectedObjectIds = nullptr; context.playerCheerUntilTick = nullptr;
        context.gateSnapshots = nullptr; context.vision = nullptr;
        context.lightingTint = nullptr; context.lightingProfile = nullptr; context.uiResources = nullptr;
        context.fixedTickHz = 30; context.detailStatsEnabled = true;
        context.shadowsEnabled = true; context.surfaceTopDownOwned = true;
        RuntimeMapRenderCache originalCache;
        int clearIndex = -1, waterIndex = -1;
        const int clearSet = context.theater->getGeneralTileSetIndex("ClearTile");
        const int waterSet = context.theater->getGeneralTileSetIndex("WaterSet");
        for (const auto& set : context.theater->getTileSets()) {
            if (set.index == clearSet) clearIndex = set.startTileIndex;
            if (set.index == waterSet) waterIndex = set.startTileIndex;
        }
        Original::OriginalTile clearTile, waterTile;
        if (clearIndex < 0 || waterIndex < 0
            || !Original::readTile(clearIndex, context, originalCache, output / "native-original", clearTile, error)
            || !Original::readTile(waterIndex, context, originalCache, output / "native-original", waterTile, error)) return false;
        int clearSub = -1, waterSub = -1;
        for (int i = 0; i < static_cast<int>(clearTile.subtiles.size()); ++i) {
            const auto& sub = clearTile.subtiles[i];
            if (sub.present && sub.height == 0 && sub.ramp == 0 && sub.land != 9) { clearSub = i; break; }
        }
        for (int i = 0; i < static_cast<int>(waterTile.subtiles.size()); ++i) {
            const auto& sub = waterTile.subtiles[i];
            if (sub.present && sub.height == 0 && sub.ramp == 0 && sub.land == 9) { waterSub = i; break; }
        }
        if (clearSub < 0 || waterSub < 0) { error = "original flat Clear/Water TMP records missing"; return false; }
        Original::Surface surface;
        if (!surface.valid()) { error = "issue24 owned DIB failed"; return false; }
        context.surfacePixels = surface.pixels; context.surfaceWidth = Original::surfaceWidth;
        context.surfaceHeight = Original::surfaceHeight;
        const RECT viewport{0, 0, Original::surfaceWidth, Original::surfaceHeight};
        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = 60; camera.tileHeight = 30;
        camera.worldOffsetX = -Original::surfaceWidth / 2;
        camera.worldOffsetY = 65 * camera.tileHeight - Original::surfaceHeight / 2;
        std::ofstream cases(output / "cases.csv"), frames(output / "frames.csv"), assets(output / "assets.csv");
        if (!cases || !frames || !assets) { error = "issue24 CSV creation failed"; return false; }
        cases << "type,water,hover,tile,subtile,phase,tick,hz,expected_lift,actual_lift,anchor_x,anchor_y,sort_x,sort_y,body_mismatches,combined_mismatches,warm_mismatches,shadow_phase_mismatches,geometry_pixels,geometry_outside,authority_before,authority_after,expect_lift,fix_accepted\n";
        frames << "type,phase,pass,pixel_hash,terrain_hits,terrain_misses,voxel_bodies,voxel_turrets,shadow_pixels,width,height,camera_x,camera_y\n";
        assets << "kind,type,resource,source,raw_bytes,raw_hash,width,height,shadow_bytes,shadow_hash\n";
        for (const auto* tile : {&clearTile, &waterTile}) assets << "TMP,all,"
            << Original::quoted(tile->fileName) << ',' << Original::quoted(tile->source) << ',' << tile->bytes.size()
            << ',' << Original::bytesHash(tile->bytes.data(), tile->bytes.size()) << ',' << tile->gridWidth << ','
            << tile->gridHeight << ",0,0\n";
        const std::array<Scene, 4> scenes{{{"LCRF","Americans",false,true},
            {"SAPC","Russians",true,true},{"YHVR","YuriCountry",false,true},{"MTNK","Americans",false,false}}};
        const std::array<const char*, 6> passes{{"cold","warm","shadow-off-body","isolated-actual-shadow","original-mask-and-body-expected","original-body-expected"}};
        int captured = 0, geometryChecks = 0;
        for (const auto& scene : scenes) {
            const auto* type = context.rules->findUnitType("Vehicle", scene.id);
            if (!type || type->strength <= 0 || (scene.hover && (!type->naval || type->passengers != 12
                || !equalAsciiNoCase(type->speedType, "Hover")
                || !equalAsciiNoCase(type->locomotor, "{4A582742-9839-11d1-B709-00A024DDAFD1}")))
                || (!scene.hover && type->naval)) { error = "original type profile differs"; return false; }
            const auto& tile = scene.water ? waterTile : clearTile;
            const int subTile = scene.water ? waterSub : clearSub;
            GameWorld world = makeWorld(scene, tile.tileIndex, subTile, type->strength, error);
            if (!error.empty() || !world.getSummary().loaded || world.getTerrainCells().size() != 841
                || world.getObjects().size() != 1) { if (error.empty()) error = "isolated hover world shape"; return false; }
            const auto& unit = world.getObjects().front();
            const POINT groundCenter{Original::surfaceWidth / 2, Original::surfaceHeight / 2};
            const auto authorityBefore = world.deterministicSimulationStateHash();
            std::vector<std::uint32_t> firstShadow;
            POINT firstSort{};
            std::array<std::vector<std::uint32_t>, 2> sheets;
            for (int phase = 0; phase < 2; ++phase) {
                // Independent approved native-pixel extrema: 0.5s ->9, 1.5s ->6.
                context.animationTick = phase == 0 ? 15 : 45;
                const int expectedLift = expectLift && scene.hover ? (phase == 0 ? 9 : 6) : 0;
                const POINT expectedBodyCenter{groundCenter.x, groundCenter.y - expectedLift};
                RuntimeMapRenderCache cache;
                const POINT publicAnchor = runtimeObjectBodyAnchorForRender(unit, viewport, context, camera);
                const auto placement = runtimeObjectWorldRenderPlacement(unit, world, viewport, context, camera, cache);
                if (publicAnchor.x != expectedBodyCenter.x || publicAnchor.y != expectedBodyCenter.y
                    || placement.center.x != publicAnchor.x || placement.center.y != publicAnchor.y) {
                    error = std::string(scene.id) + " unscoped actual body/hit anchor lift differs"; return false;
                }
                if (phase == 0) firstSort = placement.sortPoint;
                else if (placement.sortPoint.x != firstSort.x || placement.sortPoint.y != firstSort.y) {
                    error = "hover changed original ground sort"; return false;
                }
                RuntimeMapRenderContext alternate = context;
                alternate.animationTick = phase == 0 ? 45 : 15;
                {
                    RuntimeGeometryQueryScope foreign(world, viewport, alternate, camera, cache);
                    const POINT actual = runtimeObjectBodyAnchorForRender(unit, viewport, context, camera);
                    if (actual.x != publicAnchor.x || actual.y != publicAnchor.y) {
                        error = "public anchor borrowed foreign animation time"; return false;
                    }
                }
                RuntimeMapRenderContext sixty = context; sixty.fixedTickHz = 60; sixty.animationTick *= 2;
                const POINT sixtyAnchor = runtimeObjectBodyAnchorForRender(unit, viewport, sixty, camera);
                RuntimeMapCamera halfCamera = camera; halfCamera.tileWidth = 30; halfCamera.tileHeight = 15;
                halfCamera.worldOffsetY = 65 * halfCamera.tileHeight - Original::surfaceHeight / 2;
                const POINT halfAnchor = runtimeObjectBodyAnchorForRender(unit, viewport, context, halfCamera);
                const int halfLift = expectLift && scene.hover ? (phase == 0 ? 5 : 3) : 0;
                if (sixtyAnchor.x != publicAnchor.x || sixtyAnchor.y != publicAnchor.y
                    || halfAnchor.x != groundCenter.x || halfAnchor.y != groundCenter.y - halfLift) {
                    error = "same simulation time/camera scale hover mismatch"; return false;
                }
                std::array<std::vector<std::uint32_t>, 6> images;
                std::array<RuntimeMapRenderStats, 6> stats{};
                int actualShadowPixels = 0;
                for (int pass = 0; pass < 3; ++pass) {
                    surface.clear(); context.shadowsEnabled = pass != 2;
                    stats[pass] = drawRuntimeMap(surface.dc, viewport, world, context, camera, cache);
                    images[pass] = surface.capture(); ++captured;
                    if (!stats[pass].rendered || stats[pass].visibleVoxelBodies != 1
                        || (scene.hover && stats[pass].visibleVoxelTurrets != 0)
                        || (!scene.hover && stats[pass].visibleVoxelTurrets <= 0)) {
                        error = std::string(scene.id) + " original VXL body/parts missing"; return false;
                    }
                }
                context.shadowsEnabled = true;
                if (stats[0].terrainLayerCacheMisses <= 0 || stats[1].terrainLayerCacheHits <= 0
                    || stats[0].voxelShadowsDrawn != 1 || stats[0].generatedShadowFallbacks != 0
                    || stats[2].shadowsDrawn != 0 || stats[2].shadowPixelsDarkened != 0) {
                    error = "cold/warm/original shadow/disabled draw contract"; return false;
                }
                auto* voxel = loadRuntimeVoxelFromCache(unit, context, cache);
                if (!voxel || !voxel->hasSprite || voxel->shadowPixels.empty()
                    || std::none_of(voxel->shadowPixels.begin(), voxel->shadowPixels.end(), [](unsigned char value){return value != 0;})) {
                    error = "original hover VXL shadow template absent"; return false;
                }
                if (phase == 0) {
                    if (!Original::writeBytes(output / "native-original" / (std::string(scene.id) + "-shadow.pal8"), voxel->shadowPixels)) {
                        error = "original shadow export/readback failed"; return false;
                    }
                    assets << "VXL-shadow," << scene.id << ',' << Original::quoted(voxel->metadata.imageId) << ','
                        << Original::quoted(voxel->metadata.vxlPath) << ',' << voxel->metadata.vxlFileSize << ",0,"
                        << voxel->image.width << ',' << voxel->image.height << ',' << voxel->shadowPixels.size() << ','
                        << Original::bytesHash(voxel->shadowPixels.data(), voxel->shadowPixels.size()) << '\n';
                }
                for (int pass = 3; pass < 6; ++pass) {
                    surface.clear();
                    if (!drawTerrainLayerToViewport(surface.dc, viewport, context, camera, cache.terrainLayer)) {
                        error = "original underlying terrain not drawn"; return false;
                    }
                    RuntimeGeometryQueryScope geometry(world, viewport, context, camera, cache);
                    if (pass == 3) {
                        const auto shadow = drawRuntimeWorldObjectShadow(surface.dc, viewport, unit, groundCenter,
                            context, camera, cache, true);
                        if (!shadow.drawn || !shadow.voxelMask || shadow.generatedFallback) {
                            error = "actual original shadow missing"; return false;
                        }
                        actualShadowPixels = shadow.pixelsDarkened;
                    } else {
                        if (pass == 4) {
                            int originalPixels = 0;
                            const POINT maskCenter = runtimeVoxelModelDrawCenter(groundCenter, camera);
                            if (!drawRuntimeVoxelShadowMask(surface.dc, viewport, maskCenter, context, camera,
                                voxel->image, voxel->shadowPixels, false, &originalPixels, nullptr)) {
                                error = "independent original shadow mask invalid"; return false;
                            }
                            originalPixels += drawRuntimeAttachedVoxelShadows(surface.dc, viewport, unit, maskCenter,
                                context, camera, cache, voxel->rasterFitScale);
                            if (originalPixels <= 0) { error = "original grounded mask empty"; return false; }
                        }
                        if (!tryDrawVoxelSprite(surface.dc, viewport, unit, expectedBodyCenter, context, camera, cache)) {
                            error = "original VXL body oracle failed"; return false;
                        }
                    }
                    images[pass] = surface.capture(); ++captured;
                }
                RuntimeMobileSpriteBoundsProbe geometryProbe;
                if (!verifyRuntimeMobileObjectSpriteBounds(unit, publicAnchor, context, camera, cache, geometryProbe)
                    || !geometryProbe.exactMatch || geometryProbe.renderedPixels <= 0 || geometryProbe.outsidePixels != 0) {
                    error = "lifted actual body/hit geometry differs"; return false;
                }
                ++geometryChecks;
                const auto warmDifferences = Original::differences(images[0], images[1]);
                const auto bodyDifferences = Original::differences(images[2], images[5]);
                const auto combinedDifferences = Original::differences(images[0], images[4]);
                const auto shadowPhaseDifferences = phase == 0 ? 0 : Original::differences(firstShadow, images[3]);
                if (phase == 0) firstShadow = images[3];
                const auto authorityAfter = world.deterministicSimulationStateHash();
                for (int pass = 0; pass < 6; ++pass) frames << scene.id << ',' << phase << ',' << passes[pass] << ','
                    << Original::pixelHash(images[pass]) << ',' << stats[pass].terrainLayerCacheHits << ','
                    << stats[pass].terrainLayerCacheMisses << ',' << stats[pass].visibleVoxelBodies << ','
                    << stats[pass].visibleVoxelTurrets << ',' << (pass == 3 ? actualShadowPixels : stats[pass].shadowPixelsDarkened)
                    << ',' << Original::surfaceWidth << ',' << Original::surfaceHeight << ','
                    << camera.worldOffsetX << ',' << camera.worldOffsetY << '\n';
                cases << scene.id << ',' << scene.water << ',' << scene.hover << ',' << tile.tileIndex << ',' << subTile
                    << ',' << phase << ',' << context.animationTick << ',' << context.fixedTickHz << ',' << expectedLift
                    << ',' << groundCenter.y - publicAnchor.y << ',' << publicAnchor.x << ',' << publicAnchor.y
                    << ',' << placement.sortPoint.x << ',' << placement.sortPoint.y << ',' << bodyDifferences
                    << ',' << combinedDifferences << ',' << warmDifferences << ',' << shadowPhaseDifferences << ','
                    << geometryProbe.renderedPixels << ',' << geometryProbe.outsidePixels << ',' << authorityBefore
                    << ',' << authorityAfter << ',' << expectLift << ',' << (expectLift && bodyDifferences == 0 && combinedDifferences == 0) << '\n';
                if (warmDifferences || bodyDifferences || combinedDifferences || shadowPhaseDifferences
                    || authorityBefore != authorityAfter) {
                    error = std::string(scene.id) + " exact body/grounded shadow/cache/authority failed"; return false;
                }
                sheets[phase].resize(static_cast<std::size_t>(Original::surfaceWidth * 3) * Original::surfaceHeight);
                for (int panel = 0; panel < 3; ++panel) for (int y = 0; y < Original::surfaceHeight; ++y)
                    std::copy_n(images[panel == 2 ? 3 : panel == 1 ? 2 : 0].data() + static_cast<std::size_t>(y) * Original::surfaceWidth,
                        Original::surfaceWidth, sheets[phase].data() + static_cast<std::size_t>(y) * Original::surfaceWidth * 3 + panel * Original::surfaceWidth);
                std::cout << "Issue24 hover: type=" << scene.id << " tick=" << context.animationTick << " lift="
                    << groundCenter.y - publicAnchor.y << " shadowHash=" << Original::pixelHash(images[3])
                    << " bodyMismatch=" << bodyDifferences << " combinedMismatch=" << combinedDifferences << '\n';
            }
            std::vector<std::uint32_t> sheet;
            sheet.insert(sheet.end(), sheets[0].begin(), sheets[0].end());
            sheet.insert(sheet.end(), sheets[1].begin(), sheets[1].end());
            if (!Original::writeBitmap(output / (std::string(scene.id) + "-two-phase.bmp"),
                Original::surfaceWidth * 3, Original::surfaceHeight * 2, sheet)) {
                error = "two-phase actual BMP write failed"; return false;
            }
        }
        cases.close(); frames.close(); assets.close();
        if (cases.fail() || frames.fail() || assets.fail()) { error = "issue24 CSV close failed"; return false; }
        std::cout << "Issue24 hover " << (expectLift ? "strict original-pixel PASS" : "BEFORE DIAGNOSTIC ONLY")
            << ": scenes=4 phases=8 captures=" << captured << " geometryChecks=" << geometryChecks
            << " BMPs=4 originalTMPs=2 originalShadowTemplates=4 fixAccepted=" << expectLift << '\n';
        return captured == 48 && geometryChecks == 8;
    } catch (const std::exception& exception) { error = std::string("Issue24 hover: ") + exception.what(); return false; }
}
