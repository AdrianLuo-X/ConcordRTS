// Question collections 15 / 02 and 17 / 03: accurate sprite hits and a
// stable, rectangular selection cage with independently anchored health pips.
// The oracle below draws actual production building layers to two backgrounds;
// it never obtains its expected pixel envelope from the geometry collector.
bool runRuntimeBuildingSelectionAlignmentSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) {
        error = "building alignment: " + why;
        return false;
    };
    const char* requested = std::getenv("CONCORD_BUILDING_ALIGNMENT_DIR");
    const std::string root = requested && *requested ? requested
        : "build/smoke/feature-set-15/02-buildings/checks";
    std::filesystem::create_directories(root);
    const auto csv = [](const std::string& value) {
        std::string result = "\"";
        for (char character : value) {
            if (character == '"') result += '"';
            result += character;
        }
        return result + '"';
    };
    struct Surface {
        int width = 0, height = 0;
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous = nullptr;
        std::uint32_t* pixels = nullptr;
        Surface(int w, int h) : width(w), height(h) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
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
        bool valid() const { return dc && bitmap && pixels && previous; }
        std::size_t size() const { return static_cast<std::size_t>(width) * height; }
    };
    constexpr int probeWidth = 1024, probeHeight = 768;
    constexpr int atlasWidth = 1280, atlasHeight = 960;
    constexpr int tileWidth = 256, tileHeight = 240, atlasColumns = 5, atlasCapacity = 20;
    constexpr std::uint32_t atlasBackground = 0x00242c30U;
    Surface surface(probeWidth, probeHeight), atlas(atlasWidth, atlasHeight), overlay(probeWidth, probeHeight);
    if (!surface.valid() || !atlas.valid() || !overlay.valid()) return fail("cannot allocate independent render surfaces");
    std::fill(atlas.pixels, atlas.pixels + atlas.size(), atlasBackground);

    auto box = std::make_unique<RuntimeWindowState>();
    auto& s = *box;
    s.config = config;
    s.config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
    s.config.gameMode = "Battle";
    s.config.gameModeRulesOverride.clear();
    s.config.playerSlots.clear();
    s.config.computerAiEnabled = s.config.multiplayerActive = s.config.crates = false;
    s.report = buildRuntimeStartupReport(s.config, &s.world, &s.theater, &s.art, &s.rules, &s.mapFacts);
    if (!s.report.success) return fail("original rules/art/map startup failed");
    s.rulesLoaded = s.artLoaded = s.theaterLoaded = s.mapFactsLoaded = true;
    s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
    s.runtimeSuppressPlayerAudioFeedback = true;
    s.triggers.bootstrapComplete = true;
    s.economy.owner = "Americans";
    s.ownerRemapColors[s.economy.owner] = RGB(60, 130, 255);
    s.world.accessObjects().clear();
    s.world.accessTerrainObjects().clear();
    s.world.accessSmudges().clear();
    // Use the same authored foundations as the live runtime, including all
    // occupied cells of large/non-square buildings.
    s.structureFootprints = buildRuntimeStructureFootprints(s);
    // A flat fixture makes terrain height independent of the original map.
    s.report.map.size.clear();
    s.report.map.localSize.clear();
    s.report.map.width = -1;
    s.world.accessTerrainCells().clear();
    for (int y = 48; y <= 76; ++y) for (int x = 26; x <= 54; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
        s.world.accessTerrainCells().push_back(cell);
    }
    rebuildRuntimeTerrainInfoCache(s);
    auto context = runtimeTerrainMetadataContext(s);
    context.rules = &s.rules;
    context.art = &s.art;
    context.theater = &s.theater;
    context.assetRootPath = runtimeAssetRootPath(s.config);
    context.structureRootPath = runtimeStructureRootPath(s.config, s.report.map);
    context.palettePath = runtimePalettePath(s.config, s.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
    context.ownerRemapColors = &s.ownerRemapColors;
    context.gateSnapshots = &s.gateRenderSnapshots;
    context.fixedTickHz = s.config.fixedTickHz;
    context.shadowsEnabled = false;
    context.surfacePixels = surface.pixels;
    context.surfaceWidth = probeWidth;
    context.surfaceHeight = probeHeight;
    const RECT viewport{0, 0, probeWidth, probeHeight};

    IniFile original;
    if (!original.loadFromFile("local_data/rulesmd.ini")) return fail("original registry missing");
    const auto* registry = original.getSection("BuildingTypes");
    if (!registry) return fail("BuildingTypes registry missing");
    std::map<std::string, std::vector<std::string>> registered;
    for (const auto& entry : *registry) registered[entry.second].push_back(entry.first);
    if (registry->size() != 403 || registered.size() != 402)
        return fail("stock registry changed; audit every entry before adjusting the coverage expectation");

    std::ofstream inventory(root + "/building-inventory.csv");
    std::ofstream poses(root + "/building-poses.csv");
    std::ofstream atlasIndex(root + "/atlas-index.csv");
    std::ofstream failures(root + "/failures.csv");
    std::ofstream resourceAudit(root + "/missing-resource-audit.csv");
    if (!inventory || !poses || !atlasIndex || !failures || !resourceAudit) return fail("cannot open evidence files");
    resourceAudit << "id,expected_missing,loaded,frame_count,source,loader_error\n";
    inventory << "id,registry_indices,status,image,art,foundation,art_height,tech_level,poses,rendered_poses,missing_poses\n";
    poses << "id,pose,tick,tile_width,level,facing,drawn,pixels,outside_sprite,outside_frame,missed_clicks,clipped,"
          << "actual_left,actual_top,actual_right,actual_bottom,sprite_left,sprite_top,sprite_right,sprite_bottom,"
          << "frame_left,frame_top,frame_right,frame_bottom\n";
    atlasIndex << "page,row,column,id,status,scale,original_width,original_height\n";
    failures << "id,pose,reason\n";
    int atlasPage = 0, atlasSlot = 0, typesPictured = 0;
    std::vector<std::pair<std::string, std::string>> atlasLabels;
    int missingRuleTypes = 0, invisibleTypes = 0, nullArtTypes = 0;
    int loadedTypes = 0, renderedTypes = 0, missingTypes = 0;
    int caseCount = 0, renderedCases = 0, missingCases = 0, failureCount = 0;
    std::uint64_t testedPixels = 0, pixelClicks = 0, footprintClicks = 0, outsideChecks = 0;
    std::uint64_t transparentChecks = 0, overlapChecks = 0;
    int worldCases = 0, liftedWorldCases = 0, hiddenFactoryCases = 0, partialFactoryCases = 0;
    std::uint64_t worldPixelClicks = 0, clippedBodyChecks = 0;
    int sameTickCacheChecks = 0;
    int stableGeometryChecks = 0, stableRasterChecks = 0, regularFrameChecks = 0, healthLayoutChecks = 0;
    std::uint64_t bunkerFrontChecks = 0, bunkerTankChecks = 0;
    std::vector<unsigned char> touched(surface.size(), 0);
    const auto recordFailure = [&](const std::string& id, const std::string& pose, const std::string& why) {
        failures << csv(id) << ',' << csv(pose) << ',' << csv(why) << '\n';
        ++failureCount;
        if (failureCount <= 16) std::cerr << "Building " << id << ' ' << pose << ": " << why << '\n';
    };
    const auto flushAtlas = [&]() {
        if (atlasSlot == 0) return true;
        // Finish all direct bitmap writes before drawing any labels. Keep the
        // text in an opaque, clipped header so GDI batching and adjacent art
        // cannot leave partially overwritten names in the exported atlas.
        GdiFlush();
        SetBkMode(atlas.dc, OPAQUE);
        SetBkColor(atlas.dc, RGB(36, 44, 48));
        SetTextAlign(atlas.dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
        for (std::size_t index = 0; index < atlasLabels.size(); ++index) {
            const int left = static_cast<int>(index % atlasColumns) * tileWidth;
            const int top = static_cast<int>(index / atlasColumns) * tileHeight;
            const RECT header{left, top, left + tileWidth, top + 25};
            SetTextColor(atlas.dc, RGB(235, 241, 245));
            const auto& label = atlasLabels[index];
            ExtTextOutA(atlas.dc, left + 8, top + 5, ETO_OPAQUE | ETO_CLIPPED, &header,
                label.first.c_str(), static_cast<UINT>(label.first.size()), nullptr);
            if (!label.second.empty()) {
                RECT description{left + 8, top + 28, left + tileWidth - 8, top + 65};
                SetTextColor(atlas.dc, RGB(180, 186, 190));
                DrawTextA(atlas.dc, label.second.c_str(), static_cast<int>(label.second.size()),
                    &description, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
            }
        }
        GdiFlush();
        if (!writeRuntimeSmokeBmp(root + "/building-atlas-" + std::to_string(atlasPage)
                + ".bmp", atlas.pixels, atlasWidth, atlasHeight, error)) return false;
        ++atlasPage; atlasSlot = 0;
        atlasLabels.clear();
        std::fill(atlas.pixels, atlas.pixels + atlas.size(), atlasBackground);
        return true;
    };
    const auto addAtlas = [&](const std::string& id, const std::string& status, const RECT* image) {
        const int left = (atlasSlot % atlasColumns) * tileWidth;
        const int top = (atlasSlot / atlasColumns) * tileHeight;
        double scale = 0.0;
        int originalWidth = 0, originalHeight = 0;
        if (image && image->right > image->left && image->bottom > image->top) {
            originalWidth = image->right - image->left;
            originalHeight = image->bottom - image->top;
            scale = std::min({1.0, (tileWidth - 16.0) / originalWidth, (tileHeight - 40.0) / originalHeight});
            const int width = std::max(1, static_cast<int>(std::lround(originalWidth * scale)));
            const int height = std::max(1, static_cast<int>(std::lround(originalHeight * scale)));
            const int dx = left + (tileWidth - width) / 2;
            const int dy = top + 24 + (tileHeight - 32 - height) / 2;
            GdiFlush();
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const int sx = image->left + std::min(originalWidth - 1, x * originalWidth / width);
                const int sy = image->top + std::min(originalHeight - 1, y * originalHeight / height);
                if (sx >= 0 && sx < probeWidth && sy >= 0 && sy < probeHeight)
                    atlas.pixels[static_cast<std::size_t>(dy + y) * atlasWidth + dx + x]
                        = surface.pixels[static_cast<std::size_t>(sy) * probeWidth + sx];
            }
        }
        atlasLabels.emplace_back(id, image ? "" : status);
        atlasIndex << atlasPage << ',' << atlasSlot / atlasColumns << ',' << atlasSlot % atlasColumns
                   << ',' << csv(id) << ',' << csv(status) << ',' << scale << ',' << originalWidth << ',' << originalHeight << '\n';
        ++atlasSlot; ++typesPictured;
        return atlasSlot != atlasCapacity || flushAtlas();
    };
    const auto setCameraAndTerrain = [&](int tile, int level) {
        s.camera = {};
        s.camera.initialized = true;
        s.camera.tileWidth = tile;
        s.camera.tileHeight = tile / 2;
        for (auto& cell : s.world.accessTerrainCells()) cell.level = level;
        rebuildRuntimeTerrainInfoCache(s);
        const auto origin = runtimeProjectCellXY(38, 61, level, s.camera);
        s.camera.worldOffsetX = origin.x - probeWidth / 2;
        s.camera.worldOffsetY = origin.y - 440;
        // Projection applies the live camera clamp. Do so before the world
        // pixel oracle too; otherwise small zooms compare different cameras.
        ensureRuntimeCameraForViewport(s, viewport);
    };
    const auto baseObject = [&](const BuildingType& type) {
        WorldObjectInstance object;
        object.instanceId = 15002;
        object.kind = WorldObjectKind::Structure;
        object.type = type.id; object.owner = s.economy.owner;
        object.alive = true; object.health = std::max(1, type.strength);
        object.x = 38; object.y = 61; object.cell = runtimeCellKey(object.x, object.y);
        object.mission = "Guard";
        object.facing = object.turretFacing = 64;
        return object;
    };
    struct Scenario {
        std::string name;
        int tick = 0, tile = 60, level = 0, facing = 64;
        bool damaged = false, working = false, offline = false, roof = false;
        int buildupRemaining = 0;
    };
    for (const auto& [id, indices] : registered) {
        std::string indexText;
        for (const auto& index : indices) indexText += (indexText.empty() ? "" : "|") + index;
        const auto* type = s.rules.findBuildingType(id);
        if (!type) {
            ++missingRuleTypes;
            inventory << csv(id) << ',' << csv(indexText) << ",missing-rules,,,,,0,0,0,0\n";
            if (original.getSection(id)) recordFailure(id, "registry", "rules section exists but runtime loader omitted it");
            if (!addAtlas(id, "missing rules section", nullptr)) return false;
            continue;
        }
        ++loadedTypes;
        auto object = baseObject(*type);
        const auto* art = runtimeArtTypeForObject(s, object);
        const std::string image = type->image.empty() ? id : type->image;
        const auto writeInventory = [&](const std::string& status, int cases, int rendered, int missing) {
            inventory << csv(id) << ',' << csv(indexText) << ',' << csv(status) << ',' << csv(image) << ','
                      << csv(art ? art->id : "") << ',' << csv(art ? art->foundation : "") << ','
                      << (art ? art->height : 0) << ',' << type->techLevel << ',' << cases << ',' << rendered << ',' << missing << '\n';
        };
        if (type->invisibleInGame) {
            ++invisibleTypes;
            s.world.accessObjects() = {object}; setCameraAndTerrain(60, 0);
            invalidateRuntimeObjectProjectionCache(s);
            if (!buildProjectedObjects(s, viewport).empty())
                recordFailure(id, "hidden", "InvisibleInGame object acquired an interaction projection");
            writeInventory("invisible-map-building", 0, 0, 0);
            if (!addAtlas(id, "invisible map light", nullptr)) return false;
            continue;
        }
        if (toLowerAsciiLocal(image) == "null") {
            ++nullArtTypes;
            s.world.accessObjects() = {object}; setCameraAndTerrain(60, 0);
            invalidateRuntimeObjectProjectionCache(s);
            if (!buildProjectedObjects(s, viewport).empty())
                recordFailure(id, "hidden", "null-art tile object acquired an interaction projection");
            writeInventory("null-art-tile-only", 0, 0, 0);
            if (!addAtlas(id, "null art / tile only", nullptr)) return false;
            continue;
        }
        std::vector<Scenario> scenarios{
            {"healthy", 0, 60, 0, 64},
            {"healthy-small-raised", 17, 48, 2, 0},
            {"healthy-large", 43, 72, 0, 192},
            {"damaged", 29, 60, 2, 128, true},
            {"working", 17, 60, 0, 96, false, true},
            {"offline", 43, 60, 2, 160, false, false, true}
        };
        if (art && !art->buildup.empty() && !type->wall) {
            scenarios.push_back({"buildup-first", 0, 60, 0, 64, false, false, false, false, 120});
            scenarios.push_back({"buildup-middle", 60, 60, 0, 64, false, false, false, false, 60});
            scenarios.push_back({"buildup-last", 119, 60, 0, 64, false, false, false, false, 1});
        }
        if (!type->turretAnim.empty()) for (int facing = 0; facing < 256; facing += 8)
            scenarios.push_back({"turret-" + std::to_string(facing), 47, 60, 0, facing});
        if (!type->factory.empty()) {
            scenarios.push_back({"factory-door", 17, 60, 0, 64, false, true});
            scenarios.push_back({"factory-roof", 17, 60, 0, 64, false, true, false, true});
        }
        if (id == "GAWEAP" || id == "GACNST")
            scenarios.push_back({"uneven-foundation", 17, 60, 0, 96});
        int typeCases = 0, typeRendered = 0, typeMissing = 0;
        bool pictured = false;
        std::map<int, std::vector<LONG>> referenceSelection;
        std::map<int, std::vector<std::array<LONG, 3>>> referenceOverlay;
        for (const auto& scene : scenarios) {
            ++caseCount; ++typeCases;
            object = baseObject(*type);
            object.level = scene.level;
            object.facing = object.turretFacing = scene.facing;
            object.health = scene.damaged ? std::max(1, type->strength / 4) : std::max(1, type->strength);
            object.workAnimationActive = scene.working;
            object.poweredOperational = !scene.offline;
            object.powerAnimationPausedAtTick = scene.offline ? 7 : -1;
            object.mission = scene.working ? (art && art->isAnimDelayedFire ? "DelayedFire" : "Attack") : "Guard";
            object.chargeTurretStartTick = scene.working ? 0 : -1;
            object.structureRecoilStartTick = scene.name.find("turret-") == 0 ? scene.tick - 1 : -1;
            object.buildAnimTicksTotal = scene.buildupRemaining > 0 ? 120 : 0;
            object.buildAnimTicksRemaining = scene.buildupRemaining;
            if (scene.name.find("factory-") == 0) {
                object.factoryExitAnimationStartTick = 1;
                object.factoryExitAnimationEndTick = 90;
                object.factoryExitAirborne = scene.roof;
            }
            s.tick = scene.tick;
            context.animationTick = scene.tick;
            s.world.accessObjects() = {object};
            s.selectedObjectIds = {object.instanceId};
            setCameraAndTerrain(scene.tile, scene.level);
            if (scene.name == "uneven-foundation") {
                for (auto& cell : s.world.accessTerrainCells())
                    if (cell.x == object.x + 1 && cell.y == object.y + 1) cell.level += 2;
                rebuildRuntimeTerrainInfoCache(s);
            }
            invalidateRuntimeObjectProjectionCache(s);
            const auto projected = buildProjectedObjects(s, viewport);
            const RuntimeProjectedObject* projectedObject = projected.empty() ? nullptr : &projected.front();
            if (projected.size() != 1) recordFailure(id, scene.name, "expected one visible interaction projection");
            std::fill(touched.begin(), touched.end(), 0);
            bool drew = true;
            // Two complementary backgrounds detect black or white artwork too.
            for (std::uint32_t background : {0x00ffffffU, 0x00000000U}) {
                std::fill(surface.pixels, surface.pixels + surface.size(), background);
                drew = drawRuntimeObjectPreviewSprite(surface.dc, viewport, object, context, s.camera, s.renderCache) && drew;
                GdiFlush();
                for (std::size_t index = 0; index < surface.size(); ++index)
                    if ((surface.pixels[index] & 0x00ffffffU) != background) touched[index] = 1;
            }
            RECT actual{};
            bool actualValid = false, clipped = false;
            std::uint64_t pixels = 0, outsideSprite = 0, outsideFrame = 0, missedClicks = 0;
            RECT sprite{}, frame{};
            RuntimeStructureSelectionFrameGeometry frameGeometry;
            int selectionHeight = 0;
            if (projectedObject) {
                sprite = projectedObject->spriteBounds;
                selectionHeight = runtimeStructureSelectionVerticalHeight(s, object);
                frameGeometry = runtimeStructureSelectionFrameGeometry(runtimeStructureSelectionFootprintCenters(s, object, viewport),
                    projectedObject->footprintHalfWidth, projectedObject->footprintHalfHeight, selectionHeight);
                if (frameGeometry.valid) frame = {frameGeometry.left.x, frameGeometry.top.y - frameGeometry.verticalHeight,
                    frameGeometry.right.x + 1, frameGeometry.bottom.y
                        + runtimeStructureFrameFoundationOverlap(projectedObject->footprintHalfHeight) + 1};
            }
            for (int y = 0; y < probeHeight; ++y) for (int x = 0; x < probeWidth; ++x) {
                if (!touched[static_cast<std::size_t>(y) * probeWidth + x]) continue;
                const POINT point{x, y};
                if (!actualValid) { actual = {x, y, x + 1, y + 1}; actualValid = true; }
                else {
                    actual.left = std::min<LONG>(actual.left, x); actual.top = std::min<LONG>(actual.top, y);
                    actual.right = std::max<LONG>(actual.right, x + 1); actual.bottom = std::max<LONG>(actual.bottom, y + 1);
                }
                ++pixels; ++testedPixels;
                if (!PtInRect(&sprite, point)) ++outsideSprite;
                if (!PtInRect(&frame, point)) ++outsideFrame;
                const auto* hit = findProjectedObjectAtPoint(projected, point);
                if (!hit || !hit->object || hit->object->instanceId != object.instanceId) ++missedClicks;
                ++pixelClicks;
                if (x == 0 || y == 0 || x + 1 == probeWidth || y + 1 == probeHeight) clipped = true;
            }
            if (actualValid) {
                ++renderedCases; ++typeRendered;
                if (!drew || outsideSprite || missedClicks || clipped || !frameGeometry.valid) {
                    recordFailure(id, scene.name, "pixels=" + std::to_string(pixels)
                        + " outsideSprite=" + std::to_string(outsideSprite)
                        + " outsideFrame=" + std::to_string(outsideFrame)
                        + " missedClicks=" + std::to_string(missedClicks)
                        + " clipped=" + std::to_string(clipped) + " drawn=" + std::to_string(drew));
                }
                if (projectedObject) {
                    for (const POINT point : projectedObject->footprintHitCenters) {
                        const auto* hit = findProjectedObjectAtPoint(projected, point);
                        if (!hit || !hit->object || hit->object->instanceId != object.instanceId)
                            recordFailure(id, scene.name, "ground footprint center stopped selecting building");
                        ++footprintClicks;
                    }
                    RECT extent = projectedObject->bounds;
                    UnionRect(&extent, &extent, &actual);
                    for (const POINT point : {POINT{extent.left - 8, extent.top - 8},
                            POINT{extent.right + 8, extent.top - 8}, POINT{extent.left - 8, extent.bottom + 8},
                            POINT{extent.right + 8, extent.bottom + 8}}) {
                        if (findProjectedObjectAtPoint(projected, point))
                            recordFailure(id, scene.name, "empty ground outside footprint/body selects building");
                        ++outsideChecks;
                    }
                    // Empty sky/roof corners inside a tall building's large
                    // envelope must stay empty. Keep three pixels away from
                    // artwork to allow a small forgiving edge, and retain the
                    // separately authored foundation as a valid click area.
                    std::vector<POINT> transparentPoints;
                    for (LONG y = actual.top; y < actual.bottom && transparentPoints.size() < 16; y += 7) {
                        for (LONG x = actual.left; x < actual.right && transparentPoints.size() < 16; x += 7) {
                            bool clear = true;
                            for (LONG sy = std::max<LONG>(0, y - 3); sy <= std::min<LONG>(probeHeight - 1, y + 3) && clear; ++sy)
                                for (LONG sx = std::max<LONG>(0, x - 3); sx <= std::min<LONG>(probeWidth - 1, x + 3); ++sx)
                                    if (touched[static_cast<std::size_t>(sy) * probeWidth + sx]) { clear = false; break; }
                            if (!clear) continue;
                            const POINT point{x, y};
                            for (const auto& foot : projectedObject->footprintHitCenters)
                                if (pointInRuntimeIsoDiamond(point, foot, projectedObject->footprintHalfWidth + 3,
                                        projectedObject->footprintHalfHeight + 3)) { clear = false; break; }
                            if (!clear) continue;
                            transparentPoints.push_back(point);
                            if (findProjectedObjectAtPoint(projected, point))
                                recordFailure(id, scene.name, "transparent sky/roof corner inside visual envelope captures click");
                            ++transparentChecks;
                        }
                    }
                    // Use the real building projection and real opaque/clear
                    // samples, inserting a small foreground/background target
                    // in explicit painter order. This isolates hit ordering
                    // from movement/collision and requires alpha-aware building
                    // hits in both orders; rectangle-only geometry fails it.
                    WorldObjectInstance other = object;
                    other.instanceId = 15003; other.kind = WorldObjectKind::Infantry; other.type = "E1";
                    const auto checkOverlap = [&](POINT point, bool opaque) {
                        RuntimeProjectedObject target;
                        target.object = &other;
                        target.center = target.renderCenter = point;
                        target.bounds = target.spriteBounds = {point.x - 2, point.y - 2, point.x + 3, point.y + 3};
                        const std::vector<RuntimeProjectedObject> buildingInFront{target, *projectedObject};
                        const auto* behindHit = findProjectedObjectAtPoint(buildingInFront, point);
                        const int expected = opaque ? object.instanceId : other.instanceId;
                        if (!behindHit || !behindHit->object || behindHit->object->instanceId != expected)
                            recordFailure(id, scene.name, opaque ? "opaque building failed to cover target behind it"
                                : "transparent building masked target behind it");
                        const std::vector<RuntimeProjectedObject> targetInFront{*projectedObject, target};
                        const auto* frontHit = findProjectedObjectAtPoint(targetInFront, point);
                        if (!frontHit || !frontHit->object || frontHit->object->instanceId != other.instanceId)
                            recordFailure(id, scene.name, "foreground target lost click to building behind it");
                        overlapChecks += 2;
                    };
                    if (!transparentPoints.empty()) checkOverlap(transparentPoints.front(), false);
                    bool opaqueChecked = false;
                    for (LONG y = actual.top; y < actual.bottom && !opaqueChecked; ++y)
                        for (LONG x = actual.left; x < actual.right; ++x)
                            if (touched[static_cast<std::size_t>(y) * probeWidth + x]) {
                                checkOverlap({x, y}, true); opaqueChecked = true; break;
                            }
                }
            } else {
                ++missingCases; ++typeMissing;
                // A completely blank first buildup frame is legitimate. Record
                // it distinctly from a resource that could not be resolved.
                if (!drew && type->techLevel >= 0 && scene.buildupRemaining == 0)
                    recordFailure(id, scene.name, "available stock building has no drawable resource");
            }
            #include "RuntimeBuildingSelectionStabilitySmoke.inl"
            poses << csv(id) << ',' << csv(scene.name) << ',' << scene.tick << ',' << scene.tile << ',' << scene.level
                  << ',' << scene.facing << ',' << drew << ',' << pixels << ',' << outsideSprite << ',' << outsideFrame
                  << ',' << missedClicks << ',' << clipped << ',' << actual.left << ',' << actual.top << ',' << actual.right
                  << ',' << actual.bottom << ',' << sprite.left << ',' << sprite.top << ',' << sprite.right << ',' << sprite.bottom
                  << ',' << frame.left << ',' << frame.top << ',' << frame.right << ',' << frame.bottom << '\n';
            if (!pictured && actualValid) {
                RECT picture = actual;
                if (frameGeometry.valid && projectedObject) {
                    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 140, 255));
                    const HGDIOBJ oldPen = pen ? SelectObject(surface.dc, pen) : nullptr;
                    drawRuntimeStructureFootprintCornerFrame(surface.dc, runtimeStructureSelectionFootprintCenters(s, object, viewport),
                        projectedObject->footprintHalfWidth, projectedObject->footprintHalfHeight, selectionHeight);
                    if (oldPen) SelectObject(surface.dc, oldPen);
                    if (pen) DeleteObject(pen);
                    UnionRect(&picture, &picture, &frame);
                }
                InflateRect(&picture, 5, 5);
                GdiFlush();
                if (!addAtlas(id, "rendered-" + scene.name, &picture)) return false;
                if (art && (art->height >= 16 || id == "GAWEAP" || id == "GTGCAN" || id == "ATESLA" || id == "TESLA")) {
                    if (!writeRuntimeSmokeBmp(root + "/detail-" + id + ".bmp", surface.pixels,
                            probeWidth, probeHeight, error)) return false;
                }
                pictured = true;
            }
        }
        if (typeRendered) ++renderedTypes;
        else ++missingTypes;
        writeInventory(typeRendered ? "rendered" : "missing-or-empty-resource", typeCases, typeRendered, typeMissing);
        if (!pictured && !addAtlas(id, "missing / empty artwork", nullptr)) return false;
        if (typeRendered == 0) {
            // These five stock registrations have no resolvable SHP in the
            // supplied original assets. Keep every pose and an explicit loader
            // audit; all other visible types must render at least one pose.
            const bool expectedMissing = id == "CASIN01E" || id == "CASIN01N"
                || id == "CASIN01S" || id == "CASIN01W" || id == "GADUMY";
            bool sawMissingSource = false;
            const std::string keyPart = "|" + toLowerAsciiLocal(id) + "|";
            for (const auto& [key, entry] : s.renderCache.objectShpBytesByImage) {
                if (key.find(keyPart) == std::string::npos) continue;
                resourceAudit << csv(id) << ',' << expectedMissing << ',' << entry.loaded << ','
                    << entry.frameCount << ',' << csv(entry.sourcePath) << ',' << csv(entry.errorMessage) << '\n';
                sawMissingSource = sawMissingSource || (entry.loadAttempted && !entry.loaded && entry.bytes.empty());
            }
            if (!expectedMissing || !sawMissingSource)
                recordFailure(id, "type", "visible original type has no rendered pose without an audited missing-source exception");
        }
        std::cout << "building " << id << " poses=" << typeCases << " rendered=" << typeRendered
                  << " missingOrEmpty=" << typeMissing << '\n';
    }
    // Mixed scenes are checked against the actual full world compositor, not
    // the geometry helper. The only difference between the two renders is the
    // aircraft. This catches final-pass building clearance and roof clipping
    // that an isolated body/preview test cannot observe.
    std::ofstream worldRows(root + "/world-aircraft-overlap.csv");
    worldRows << "building,aircraft,phase,level,tile_width,world_lift,projection,changed_pixels,missed_clicks,clip_bottom,partial\n";
    const auto checkWorldAircraft = [&](const std::string& buildingId, const std::string& aircraftId,
            bool exiting, bool closed, int level, int tile) {
        const auto* buildingType = s.rules.findBuildingType(buildingId);
        const auto* aircraftType = s.rules.findUnitType("AircraftType", aircraftId);
        const auto* vehicleType = s.rules.findUnitType("UnitType", aircraftId);
        const auto* infantryType = s.rules.findUnitType("InfantryType", aircraftId);
        const UnitType* mobileType = aircraftType ? aircraftType : vehicleType ? vehicleType : infantryType;
        if (!buildingType || !mobileType) { recordFailure(aircraftId, "world", "mixed-scene stock type missing"); return; }
        auto host = baseObject(*buildingType);
        host.instanceId = 15100;
        host.workAnimationActive = true;
        host.factoryExitAnimationStartTick = exiting ? 0 : -1;
        host.factoryExitAnimationEndTick = exiting ? 500 : -1;
        host.factoryExitAirborne = exiting;
        WorldObjectInstance aircraft;
        aircraft.instanceId = 15101; aircraft.type = aircraftId; aircraft.owner = s.economy.owner;
        aircraft.kind = infantryType ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        aircraft.authoredAircraft = aircraftType != nullptr;
        aircraft.alive = true; aircraft.health = std::max(1, mobileType->strength);
        aircraft.x = host.x + 1; aircraft.y = host.y + 1;
        aircraft.cell = runtimeCellKey(aircraft.x, aircraft.y);
        aircraft.level = level; aircraft.flightLevel = level;
        aircraft.airborne = true; aircraft.mission = "Guard";
        aircraft.facing = aircraft.turretFacing = 64;
        aircraft.factoryExitHostId = exiting ? host.instanceId : -1;
        s.tick = closed ? 0 : 180;
        context.animationTick = s.tick;
        context.specialMechanismSnapshots = nullptr;
        context.gateSnapshots = &s.gateRenderSnapshots;
        context.mapFacts = nullptr;
        setCameraAndTerrain(tile, 0);
        s.world.accessObjects() = {host};
        std::fill(surface.pixels, surface.pixels + surface.size(), atlasBackground);
        const auto baselineStats = drawRuntimeMap(surface.dc, viewport, s.world, context, s.camera, s.renderCache);
        GdiFlush();
        const std::vector<std::uint32_t> baseline(surface.pixels, surface.pixels + surface.size());
        s.world.accessObjects() = {host, aircraft};
        std::fill(surface.pixels, surface.pixels + surface.size(), atlasBackground);
        const auto actualStats = drawRuntimeMap(surface.dc, viewport, s.world, context, s.camera, s.renderCache);
        GdiFlush();
        const int renderedCameraX = s.camera.worldOffsetX, renderedCameraY = s.camera.worldOffsetY;
        invalidateRuntimeObjectProjectionCache(s);
        const auto projected = buildProjectedObjects(s, viewport);
        if (s.camera.worldOffsetX != renderedCameraX || s.camera.worldOffsetY != renderedCameraY)
            recordFailure(aircraftId, "world-camera", "projection changed the camera after the pixel oracle rendered");
        const auto found = std::find_if(projected.begin(), projected.end(), [&](const auto& item) {
            return item.object && item.object->instanceId == aircraft.instanceId;
        });
        const bool hasProjection = found != projected.end();
        const auto placement = runtimeObjectWorldRenderPlacement(aircraft, s.world, viewport, context, s.camera, s.renderCache);
        RECT fullBody{};
        const bool bodyResolved = resolveRuntimeMobileObjectSpriteBounds(aircraft, placement.center,
            context, s.camera, s.renderCache, fullBody);
        std::uint64_t pixels = 0, missed = 0;
        for (int y = 0; y < probeHeight; ++y) for (int x = 0; x < probeWidth; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * probeWidth + x;
            if ((baseline[index] & 0x00ffffffU) == (surface.pixels[index] & 0x00ffffffU)) continue;
            ++pixels; ++worldPixelClicks;
            const POINT point{x, y};
            const auto* hit = findProjectedObjectAtPoint(projected, point);
            if (!hit || !hit->object || hit->object->instanceId != aircraft.instanceId
                || !hasProjection || !PtInRect(&found->selectionBounds, point)) ++missed;
        }
        std::uint64_t nativeOccludedPixels = 0;
        if (exiting && !closed && bodyResolved) {
            const auto producer = std::find_if(projected.begin(), projected.end(), [&](const auto& item) {
                return item.object && item.object->instanceId == host.instanceId;
            });
            if (producer != projected.end()) {
                for (const auto& span : producer->structureForegroundHitSpans) {
                    if (span.y < fullBody.top || span.y >= fullBody.bottom) continue;
                    for (LONG x = std::max(span.left, fullBody.left); x < std::min(span.right, fullBody.right); ++x) {
                        if (x < 0 || x >= probeWidth || span.y < 0 || span.y >= probeHeight) continue;
                        ++nativeOccludedPixels;
                        const auto index = static_cast<std::size_t>(span.y) * probeWidth + x;
                        if ((baseline[index] & 0x00ffffffU) != (surface.pixels[index] & 0x00ffffffU))
                            recordFailure(aircraftId, "roof-foreground", "aircraft painted over native factory foreground");
                        const auto* hit = findProjectedObjectAtPoint(projected, {x, span.y});
                        if (hit && hit->object && hit->object->instanceId == aircraft.instanceId)
                            recordFailure(aircraftId, "roof-foreground", "native wall pixel selected the occluded aircraft");
                    }
                }
            }
        }
        const bool partial = exiting && !closed && pixels > 0 && nativeOccludedPixels > 0;
        const std::string phase = !exiting ? "overflight" : closed ? "roof-closed" : "roof-opening";
        ++worldCases;
        if (!baselineStats.rendered || !actualStats.rendered || !bodyResolved)
            recordFailure(aircraftId, phase, "world scene or original aircraft artwork did not render");
        if (missed) recordFailure(aircraftId, phase, "actual world pixels missed by selection: " + std::to_string(missed));
        if (!exiting) {
            const POINT baseCenter = runtimeObjectBodyAnchorForRender(aircraft, viewport, context, s.camera);
            if (pixels == 0 || !hasProjection
                || baseCenter.y - placement.center.y != actualStats.aircraftMaxBuildingClearanceLiftPixels)
                recordFailure(aircraftId, phase, "world overflight pixels or final compositor lift disagree with interaction");
            if (actualStats.aircraftBuildingClearanceAdjustments > 0) ++liftedWorldCases;
        } else if (closed) {
            if (pixels || hasProjection) recordFailure(aircraftId, phase, "aircraft hidden inside producer remained visible or targetable");
            else ++hiddenFactoryCases;
            // No invalidation and no tick advance: leaving the producer must
            // refresh the previously empty projection on the field signature.
            s.world.accessObjects()[1].factoryExitHostId = -1;
            const auto released = buildProjectedObjects(s, viewport);
            if (std::none_of(released.begin(), released.end(), [&](const auto& item) {
                    return item.object && item.object->instanceId == aircraft.instanceId;
                })) recordFailure(aircraftId, phase, "same-tick factory host release retained hidden cache");
            ++sameTickCacheChecks;
            s.world.accessObjects()[1].factoryExitHostId = host.instanceId;
        } else {
            if (partial) ++partialFactoryCases;
            if (hasProjection && found->selectionBounds.bottom > placement.bodyViewport.bottom)
                recordFailure(aircraftId, phase, "selection frame extends into factory-hidden body");
            if (bodyResolved) for (LONG y = std::max(fullBody.top, placement.bodyViewport.bottom);
                    y < fullBody.bottom; y += 2) for (LONG x = fullBody.left; x < fullBody.right; x += 2) {
                const auto* hit = findProjectedObjectAtPoint(projected, {x, y});
                if (hit && hit->object && hit->object->instanceId == aircraft.instanceId)
                    recordFailure(aircraftId, phase, "hidden part below factory roof captures click");
                ++clippedBodyChecks;
            }
            if (hasProjection && pixels > 0) {
                s.world.accessObjects()[0].factoryExitAnimationStartTick = s.tick;
                const auto shut = buildProjectedObjects(s, viewport);
                if (std::any_of(shut.begin(), shut.end(), [&](const auto& item) {
                        return item.object && item.object->instanceId == aircraft.instanceId;
                    })) recordFailure(aircraftId, phase, "same-tick closed roof retained visible cache");
                s.world.accessObjects()[0].factoryExitAnimationStartTick = 0;
                const auto reopened = buildProjectedObjects(s, viewport);
                if (std::none_of(reopened.begin(), reopened.end(), [&](const auto& item) {
                        return item.object && item.object->instanceId == aircraft.instanceId;
                    })) recordFailure(aircraftId, phase, "same-tick reopened roof retained hidden cache");
                sameTickCacheChecks += 2;
            }
        }
        worldRows << buildingId << ',' << aircraftId << ',' << phase << ',' << level << ',' << tile << ','
                  << actualStats.aircraftMaxBuildingClearanceLiftPixels << ',' << hasProjection << ',' << pixels
                  << ',' << missed << ',' << placement.bodyViewport.bottom << ',' << partial << '\n';
        if ((buildingId == "CACHIG05" && level == 0) || closed || partial) {
            const std::string path = root + "/world-" + buildingId + "-" + aircraftId + "-" + phase
                + "-" + std::to_string(level) + ".bmp";
            if (!writeRuntimeSmokeBmp(path, surface.pixels, probeWidth, probeHeight, error))
                recordFailure(aircraftId, phase, "cannot save mixed-scene picture");
        }
    };
    int worldIndex = 0;
    for (const char* building : {"CACHIG05", "GAWEAP", "GTGCAN"})
        for (const char* aircraft : {"ORCA", "ZEP", "JUMPJET"}) for (int level : {0, 2})
            checkWorldAircraft(building, aircraft, false, false, level, std::array<int, 3>{48, 60, 72}[worldIndex++ % 3]);
    for (const char* aircraft : {"ORCA", "ZEP"}) {
        checkWorldAircraft("GAWEAP", aircraft, true, true, 0, 60);
        for (int level = 0; level <= 6; ++level) checkWorldAircraft("GAWEAP", aircraft, true, false, level, 60);
    }
    if (liftedWorldCases == 0 || hiddenFactoryCases != 2 || partialFactoryCases == 0)
        recordFailure("world", "coverage", "missing real lifted, fully hidden or partly exposed aircraft scene");
    // The bunker has three actual passes: rear wall, occupant, front wall.
    // Compare the world against the same host without its tank but with both
    // authored wall layers, then check the remaining visible tank pixels and
    // the front-wall pixels that cross the tank's envelope independently.
    if (const auto* bunkerType = s.rules.findBuildingType("NATBNK")) {
        auto host = baseObject(*bunkerType);
        host.instanceId = 15200; host.tankBunkerOccupantId = 15201;
        host.tankBunkerAnimationStartTick = 0;
        WorldObjectInstance tank;
        tank.instanceId = 15201; tank.type = "MTNK"; tank.owner = s.economy.owner;
        tank.kind = WorldObjectKind::Unit; tank.alive = true; tank.health = 400;
        tank.x = host.x + 1; tank.y = host.y + 1; tank.cell = runtimeCellKey(tank.x, tank.y);
        tank.hasRenderPosition = true; tank.renderX = tank.x; tank.renderY = tank.y;
        tank.tankBunkerHostId = host.instanceId; tank.facing = tank.turretFacing = 160;
        tank.mission = "Guard";
        s.tick = context.animationTick = 180;
        setCameraAndTerrain(60, 0);
        s.world.accessObjects() = {host};
        (void)drawRuntimeMap(surface.dc, viewport, s.world, context, s.camera, s.renderCache);
        (void)drawRuntimeObjectPreviewSprite(surface.dc, viewport, host, context, s.camera, s.renderCache);
        GdiFlush();
        const std::vector<std::uint32_t> hostPixels(surface.pixels, surface.pixels + surface.size());
        s.world.accessObjects() = {host, tank};
        const auto stats = drawRuntimeMap(surface.dc, viewport, s.world, context, s.camera, s.renderCache);
        GdiFlush();
        invalidateRuntimeObjectProjectionCache(s);
        const auto projected = buildProjectedObjects(s, viewport);
        const auto hostProjection = std::find_if(projected.begin(), projected.end(), [&](const auto& item) {
            return item.object && item.object->instanceId == host.instanceId;
        });
        const auto tankProjection = std::find_if(projected.begin(), projected.end(), [&](const auto& item) {
            return item.object && item.object->instanceId == tank.instanceId;
        });
        std::uint64_t tankMisses = 0, frontMisses = 0;
        if (!stats.rendered || stats.tankBunkerFrontLayers <= 0 || hostProjection == projected.end()
            || tankProjection == projected.end()) recordFailure("NATBNK", "occupied", "three-pass world bunker fixture missing");
        else {
            for (int y = 0; y < probeHeight; ++y) for (int x = 0; x < probeWidth; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * probeWidth + x;
                const POINT point{x, y};
                if ((hostPixels[index] & 0x00ffffffU) != (surface.pixels[index] & 0x00ffffffU)) {
                    const auto* hit = findProjectedObjectAtPoint(projected, point);
                    if (!hit || !hit->object || hit->object->instanceId != tank.instanceId) ++tankMisses;
                    ++bunkerTankChecks;
                } else if (PtInRect(&tankProjection->spriteBounds, point)
                    && runtimeSpriteHitSpansContain(hostProjection->structureForegroundHitSpans, point)) {
                    const auto* hit = findProjectedObjectAtPoint(projected, point);
                    if (!hit || !hit->object || hit->object->instanceId != host.instanceId) ++frontMisses;
                    ++bunkerFrontChecks;
                }
            }
            if (tankMisses || frontMisses || !bunkerTankChecks || !bunkerFrontChecks)
                recordFailure("NATBNK", "occupied", "actual tank/front-wall hits disagree: tank=" + std::to_string(tankMisses)
                    + " front=" + std::to_string(frontMisses) + " tankPixels=" + std::to_string(bunkerTankChecks)
                    + " frontPixels=" + std::to_string(bunkerFrontChecks));
        }
        if (!writeRuntimeSmokeBmp(root + "/world-NATBNK-occupied.bmp", surface.pixels, probeWidth, probeHeight, error)) return false;
    } else recordFailure("NATBNK", "occupied", "original bunker type missing");
    if (!flushAtlas()) return false;
    if (loadedTypes != 398 || missingRuleTypes != 4 || invisibleTypes != 16 || nullArtTypes != 1
        || typesPictured != 402 || renderedTypes != 376 || missingTypes != 5)
        recordFailure("registry", "coverage", "expected 398 loaded + 4 absent, 376 rendered + 5 missing assets, 16 invisible, 1 null-art and 402 atlas entries");
    std::ofstream summary(root + "/summary.json");
    summary << "{\"registryRows\":" << registry->size() << ",\"uniqueTypes\":" << registered.size()
            << ",\"loadedTypes\":" << loadedTypes << ",\"missingRuleTypes\":" << missingRuleTypes
            << ",\"invisibleTypes\":" << invisibleTypes << ",\"nullArtTypes\":" << nullArtTypes
            << ",\"renderedTypes\":" << renderedTypes << ",\"missingResourceTypes\":" << missingTypes
            << ",\"poses\":" << caseCount << ",\"renderedPoses\":" << renderedCases
            << ",\"missingOrEmptyPoses\":" << missingCases << ",\"testedPixels\":" << testedPixels
            << ",\"pixelClicks\":" << pixelClicks << ",\"footprintClicks\":" << footprintClicks
            << ",\"outsideChecks\":" << outsideChecks << ",\"transparentChecks\":" << transparentChecks
            << ",\"overlapChecks\":" << overlapChecks << ",\"atlasEntries\":" << typesPictured
            << ",\"worldCases\":" << worldCases << ",\"liftedWorldCases\":" << liftedWorldCases
            << ",\"hiddenFactoryCases\":" << hiddenFactoryCases << ",\"partialFactoryCases\":" << partialFactoryCases
            << ",\"worldPixelClicks\":" << worldPixelClicks << ",\"clippedBodyChecks\":" << clippedBodyChecks
            << ",\"sameTickCacheChecks\":" << sameTickCacheChecks << ",\"bunkerFrontChecks\":" << bunkerFrontChecks
            << ",\"bunkerTankChecks\":" << bunkerTankChecks
            << ",\"stableGeometryChecks\":" << stableGeometryChecks << ",\"stableRasterChecks\":" << stableRasterChecks
            << ",\"regularFrameChecks\":" << regularFrameChecks << ",\"healthLayoutChecks\":" << healthLayoutChecks
            << ",\"atlasPages\":" << atlasPage << ",\"failures\":" << failureCount << "}\n";
    if (failureCount) return fail(std::to_string(failureCount) + " failures; see " + root + "/failures.csv");
    std::cout << "Building alignment passed: uniqueTypes=" << registered.size() << " renderedTypes=" << renderedTypes
              << " poses=" << caseCount << " pixelClicks=" << pixelClicks << " atlasEntries=" << typesPictured << '\n';
    return true;
}
