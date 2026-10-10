// Issue 22/19. A sale changes dynamic occupancy, not the immutable map.
// CPU timings are diagnostic; preservation of an already-warm terrain/static
// path cache is the deterministic performance contract. Cold paints use a
// separate value-owned cache and never evict the naturally warmed one.
bool runRuntimeSellPerformanceSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* output = std::getenv("CONCORD_SELL_PERFORMANCE_OUTPUT");
    const std::filesystem::path root = output && *output ? output : "build/issue22_19_sell_performance";
    const char* selected = std::getenv("CONCORD_SELL_PERFORMANCE_CASE");
    const std::string filter = selected ? selected : "";
    std::filesystem::create_directories(root / "native-original");
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Sell performance " + label + " [" + stage + "]: " + why; return false;
    };
    std::ofstream cases(root / "cases.csv"), ticks(root / "sale_ticks.csv"), draws(root / "draws.csv"),
        crew(root / "crew.csv"), checkpoints(root / "checkpoints.csv"), movement(root / "movement.csv"),
        assetsCsv(root / "assets.csv");
    if (!cases || !ticks || !draws || !crew || !checkpoints || !movement || !assetsCsv) return fail("output unavailable");
    cases << "case,hz,type,terrain_cells,dense_units,original_hp,cost,refund,crew,command_ms,next_draw_ms,cache_retained,path_retained,terrain_misses,reverse_ticks,ordinary14,canonical_extension,route_controls,result\n";
    ticks << std::setprecision(17) << "case,branch,stage,tick,elapsed,remaining,total,expected_frame,hp,alive,credits,crew_count,simulation_ms,effects_ms,economy_ms,build_ms,move_ms,attack_ms,path_ms\n";
    draws << std::setprecision(17) << "case,stage,tick,cache,cpu_ms,pixel_hash,terrain_hits,terrain_misses,terrain_ms,terrain_blit_ms,objects_ms,structures_ms,sort_ms,shp_bytes_hits,shp_bytes_misses,shp_bytes_ms,shp_frame_hits,shp_frame_misses,shp_frame_ms,body,shadow,turret,layers,expected_frame,control_wall_count,buildup_frame_entries,buildup_byte_entries\n";
    crew << std::setprecision(17) << "case,branch,stage,tick,object,type,owner,hp,alive,x,y,z,body_x,body_y,body_z,mission\n";
    checkpoints << "case,format,tick,hz,source,remaining,total,credits,crew_count,result\n";
    movement << std::setprecision(17) << "case,branch,role,tick,object,x,y,z,body_x,body_y,body_z,hp,moving,settled\n";
    assetsCsv << "type,buildup,source,source_fnv,raw_frames,visible_frames,legacy_delay,total_ticks,refund_percent,crew_divisor\n";
    IniFile raw, rawArt; RulesDatabase stockRules;
    if (!raw.loadFromFile(live.report.rulesPath) || !stockRules.loadFromIni(raw)
        || !rawArt.loadFromFile(live.report.artPath)) return fail("original rules/art unavailable");
    const auto rawInt = [&](const std::string& section, const char* key) {
        return parseRuntimeIntOrDefault(raw.getValue(section, key, ""), -1);
    };
    if (raw.getValue("General", "RefundPercent", "") != "50%"
        || rawInt("General", "AlliedSurvivorDivisor") != 500
        || raw.getValue("General", "AlliedCrew", "") != "E1"
        || raw.getValue("General", "Technician", "") != "CTECH"
        || rawInt("E1", "Strength") != 125 || rawInt("CTECH", "Strength") != 50)
        return fail("original refund/crew declarations differ");
    const std::uint64_t originalRulesHash = stockRules.getDetailRulesHash();
    RuntimeConfig assetConfig = live.config;
    assetConfig.defaultMapPath = pathString(std::filesystem::path(live.config.localDataRootPath) / "maps" / "concord_flat_lake.map");
    auto originalMap = std::make_unique<RuntimeWindowState>(); originalMap->config = assetConfig;
    originalMap->report = buildRuntimeStartupReport(assetConfig, &originalMap->world, &originalMap->theater,
        &originalMap->art, &originalMap->rules, &originalMap->mapFacts);
    if (!originalMap->report.success || originalMap->world.getTerrainCells().size() != 114960
        || !originalMap->world.getSummary().loaded) return fail("formal lake map must contain its actual 114960 cells");
    originalMap->rules = stockRules; originalMap->rulesLoaded = originalMap->artLoaded = originalMap->theaterLoaded = true;
    std::ostringstream modeText;
    for (const auto& t : stockRules.getTiberiumTypes()) modeText << '[' << t.id << "]\nGrowthPercentage=0\nSpreadPercentage=0\n";
    const auto resourcesValid = [&](const RuntimeWindowState& s) {
        if (!s.rulesLoaded || s.rules.getDetailRulesHash() != originalRulesHash || s.config.noCooldown
            || !s.skirmishModeRulesLoaded || s.particles.fixedTickHz() != 30) return false;
        for (const auto& t : stockRules.getTiberiumTypes())
            if (runtimeOreRuleProbability(s, t, false) != 0.0 || runtimeOreRuleProbability(s, t, true) != 0.0) return false;
        return true;
    };
    const auto describe = [](const RuntimeWindowState& s, int id) {
        const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
        if (!o) return std::string("missing#") + std::to_string(id);
        std::ostringstream out; out << std::setprecision(17) << o->type << '#' << id << " HP=" << o->health
            << " alive=" << o->alive << " logical=" << o->x << ',' << o->y << ',' << o->level
            << " body=" << runtimeObjectRenderX(*o) << ',' << runtimeObjectRenderY(*o) << ',' << runtimeDrawLevelForObject(*o)
            << " mission=" << o->mission << " sale=" << o->buildAnimTicksRemaining << '/' << o->buildAnimTicksTotal;
        return out.str();
    };
    const auto pixelHash = [](const std::vector<std::uint32_t>& pixels) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto p : pixels) { hash ^= p & 0xffffffU; hash *= 1099511628211ULL; }
        return hash;
    };
    struct OriginalBuildup {
        std::string image, source; std::filesystem::path copy;
        std::uint64_t fingerprint = 0; int rawFrames = 0, frames = 0, delay = 0, total = 0;
        std::map<int, RuntimeShpFrameImage> decoded;
    };
    std::map<std::string, OriginalBuildup> originals;
    // Independent MIX bytes/header. These buildup ids have no auxiliary ART
    // override; neither timing nor expected pixels use the production cache.
    for (const std::string type : {"GAPOWR", "GTGCAN"}) {
        const auto* b = stockRules.findBuildingType(type);
        if (!b || b->unsellable || b->strength != (type == "GAPOWR" ? 750 : 900)
            || b->cost != (type == "GAPOWR" ? 800 : 2000) || b->crewed != (type == "GAPOWR")
            || rawInt(type, "Strength") != b->strength || rawInt(type, "Cost") != b->cost
            || rawArt.getValue(type, "Foundation", "") != "2x2"
            || !parseRuntimeBool(rawArt.getValue(type, "FreeBuildup", "no"))) return fail("original typed building: " + type);
        auto& data = originals[type]; data.image = rawArt.getValue(type, "Buildup", "");
        if (data.image.empty() || !rawArt.getValue(data.image, "Image", "").empty()
            || parseRuntimeBool(rawArt.getValue(data.image, "NewTheater", "no"))) return fail("unexpected original buildup alias: " + type);
        const std::string filename = toLowerAsciiLocal(data.image) + ".shp";
        std::vector<std::uint8_t> bytes;
        for (const char* directory : {"ra2md", "ra2"}) {
            const auto directoryPath = std::filesystem::path(runtimeAssetRootPath(assetConfig)) / directory;
            if (!std::filesystem::is_directory(directoryPath)) continue;
            std::vector<std::filesystem::path> archives;
            for (const auto& f : std::filesystem::directory_iterator(directoryPath))
                if (f.is_regular_file() && toLowerAsciiLocal(f.path().extension().string()) == ".mix") archives.push_back(f.path());
            std::sort(archives.begin(), archives.end());
            for (const auto& p : archives) {
                MixArchive archive; if (!archive.loadFromPath(p)) continue;
                const auto* entry = archive.findEntryByName(filename); if (!entry) continue;
                if (!archive.readEntryData(*entry, bytes, error)) return fail("original buildup MIX read: " + error);
                data.source = pathString(p) + "!" + filename; break;
            }
            if (!data.source.empty()) break;
        }
        if (bytes.size() < 8 || data.source.empty()) return fail("original buildup bytes not found: " + filename);
        data.rawFrames = int(bytes[6]) | int(bytes[7]) << 8; data.frames = data.rawFrames / 2;
        if (data.frames < 2) return fail("original buildup frame header: " + type);
        const double originalMinutes = std::stod(raw.getValue("General", "BuildupTime", ""));
        data.delay = static_cast<int>(originalMinutes * 900.0 / data.frames);
        data.total = (std::max(1, data.frames * data.delay) * 30 + 14) / 15;
        if (data.frames < 2 || data.total < 4) return fail("original buildup frame/tick header: " + type);
        data.fingerprint = 14695981039346656037ULL;
        for (const auto byte : bytes) data.fingerprint = (data.fingerprint ^ byte) * 1099511628211ULL;
        data.copy = root / "native-original" / filename;
        std::ofstream file(data.copy, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!file) return fail("original byte snapshot unavailable");
        assetsCsv << type << ',' << data.image << ',' << data.source << ',' << data.fingerprint << ',' << data.rawFrames
            << ',' << data.frames << ',' << data.delay << ',' << data.total << ",0.5,500\n";
    }
    const auto nativeStep = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick;
        const auto started = std::chrono::steady_clock::now(); advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    };
    const auto facade = [](RuntimeWindowState& s) {
        s.economy.owner = "Americans"; s.config.playerCountry = "Americans";
        s.skirmishCountryByOwner = {{"americans", "Americans"}, {"skirmishplayer2", "Russians"}};
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority h;
        h.owner = h.country = "Americans"; h.canonicalOwner = "americans"; h.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(h)); initializeRuntimeOfflineGameplayRouting(s);
    };
    int completed = 0, ordinaryCount = 0, canonicalCount = 0, reverseCount = 0, drawCount = 0, routeCount = 0;
    std::vector<std::string> cacheFailures;
    for (bool lake : {false, true}) for (const std::string type : {"GAPOWR", "GTGCAN"}) {
        label = std::string(lake ? "lake-" : "small-") + type + "-30";
        if (!filter.empty() && label != filter) continue;
        stage = "original fixture";
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box; seedRuntimeLoadScratch(s, *originalMap, false);
        s.config.fixedTickHz = 30; s.config.noCooldown = s.config.multiplayerActive = s.config.computerAiEnabled = false;
        s.config.shortGame = s.config.crates = s.config.superweapons = false; s.config.startingUnits = 0;
        s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.playerSlots.clear();
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true; local.country = "Americans"; local.name = "Americans";
        RuntimeConfig::PlayerSlot other; other.enabled = true; other.country = "Russians"; other.name = "Quiet opponent";
        s.config.playerSlots = {local, other}; s.campaignMode = false; s.tick = 200;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        if (!lake) {
            auto originalCells = std::move(s.world.accessTerrainCells()); s.world.accessTerrainCells().clear();
            for (const auto& c : originalCells) if (c.x >= 130 && c.x <= 170 && c.y >= 130 && c.y <= 170) s.world.accessTerrainCells().push_back(c);
            s.mapFacts = {}; s.mapFactsLoaded = false; s.report.map.size.clear(); s.report.map.localSize.clear();
            s.report.map.width = s.report.map.height = -1;
        }
        s.triggers = {}; s.computerAiByOwner.clear(); s.computerMiningByOwner.clear(); s.skirmishMatchFinished = s.skirmishVictory = false;
        s.skirmishResult.clear(); s.selectedObjectIds.clear(); s.behaviors.clear(); s.humanHouseAuthorities.clear();
        s.structureFootprints.clear(); s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        RuntimeSnapshotActiveStateScope active(s); initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s);
        const auto add = [&](RuntimeBuildKind kind, const char* id, RuntimeCellPoint at, const char* owner = "Americans") {
            auto* o = addRuntimeObjectInstanceForOwner(s, owner, kind, id, at); if (!o) return -1;
            o->mission = "Sleep"; o->buildAnimTicksRemaining = o->buildAnimTicksTotal = 0; return o->instanceId;
        };
        const int source = add(RuntimeBuildKind::Building, type.c_str(), {150, 150});
        const int enemy = add(RuntimeBuildKind::Building, "GAPOWR", lake ? RuntimeCellPoint{330, 330} : RuntimeCellPoint{131, 169}, "SkirmishPlayer2");
        std::array<int, 3> witnesses{{add(RuntimeBuildKind::Unit, "MTNK", {148, 150}),
            add(RuntimeBuildKind::Unit, "MTNK", {138, 144}), add(RuntimeBuildKind::Unit, "MTNK", {160, 137})}};
        std::vector<int> walls; for (int y = 139; y <= 141; ++y) walls.push_back(add(RuntimeBuildKind::Building, "GAWALL", {160, y}));
        std::set<int> denseIds;
        if (lake) for (int y = 151; y <= 166; ++y) for (int x = 316; x <= 347; ++x) {
            const int id = add(RuntimeBuildKind::Infantry, "E1", {x, y}); if (id < 0) return fail("full-HP dense original infantry unavailable"); denseIds.insert(id);
        }
        if (source < 0 || enemy < 0 || std::any_of(witnesses.begin(), witnesses.end(), [](int id) { return id < 0; })
            || std::any_of(walls.begin(), walls.end(), [](int id) { return id < 0; })) return fail("registered original actors unavailable");
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); s.economy.credits = 10000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.loaded = s.triggers.bootstrapComplete = true; facade(s);
        s.skirmishModeRulesLoaded = s.skirmishModeRules.loadFromString(modeText.str()); s.skirmishModeAlliesAllowed = true;
        s.particles.reset(30); s.particlesInitialized = s.particles.alignEmptyStateTick(s.tick);
        ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeSuperWeaponsInitialized(s); ensureRuntimeLightingInitialized(s); rebuildRuntimeEconomyPower(s);
        if (!resourcesValid(s) || !s.particlesInitialized || s.world.getTerrainCells().size() != (lake ? 114960U : 1681U)
            || findObjectById(s, source)->health != (type == "GAPOWR" ? 750 : 900)
            || findObjectById(s, enemy)->health != 750 || denseIds.size() != (lake ? 512U : 0U)) return fail("original fixture/clock/count differs");
        std::set<int> beforeIds; for (const auto& o : s.world.getObjects()) beforeIds.insert(o.instanceId);
        const int initialCredits = s.economy.credits, expectedRefund = rawInt(type, "Cost") / 2;
        const int expectedCrew = type == "GAPOWR" ? std::clamp(expectedRefund / 500, 1, 5) : 0;
        auto& original = originals.at(type); const int total = original.total;
        const std::string framePrefix = toLowerAsciiLocal(original.image) + '|';
        const auto buildupCacheEntries = [&](const RuntimeMapRenderCache& cache) {
            std::pair<std::size_t, std::size_t> count{};
            for (const auto& [key, entry] : cache.auxiliaryShpSpritesByImage)
                if (key.rfind(framePrefix, 0) == 0) ++count.first;
            for (const auto& [key, entry] : cache.auxiliaryShpBytesByImage)
                if (key.rfind(framePrefix, 0) == 0) ++count.second;
            return count;
        };
        constexpr int width = 960, height = 640; const RuntimeCellPoint cameraCell{151, 151};
        const auto draw = [&](RuntimeWindowState& q, const std::string& phase, bool cold, int expectedFrame,
            std::vector<std::uint32_t>& pixels, double& milliseconds, bool saveBmp = false) -> std::optional<RuntimeMapRenderStats> {
            struct IndependentColdCache {
                RuntimeWindowState& s; RuntimeMapRenderCache saved; bool enabled;
                IndependentColdCache(RuntimeWindowState& state, bool use) : s(state), enabled(use) {
                    if (enabled) { saved = std::move(s.renderCache); s.renderCache = RuntimeMapRenderCache{}; }
                }
                ~IndependentColdCache() { if (enabled) s.renderCache = std::move(saved); }
            } isolated(q, cold);
            const auto started = std::chrono::steady_clock::now();
            const auto stats = drawRuntimeStateOffscreenAtCell(q, cameraCell, width, height, error,
                saveBmp ? pathString(root / (label + '-' + phase + ".bmp")) : "", false, false, -1, &pixels);
            milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            if (!stats || !stats->rendered) { fail("real map render: " + error); return std::nullopt; }
            if (expectedFrame >= 0) {
                if (!original.decoded.contains(expectedFrame)) {
                    RuntimeShpFrameImage image; if (!decodeRuntimeShpFrameFile(pathString(original.copy), expectedFrame, image, error)) {
                        fail("independent raw buildup decode: " + error); return std::nullopt;
                    }
                    original.decoded.emplace(expectedFrame, std::move(image));
                }
                const auto& rawFrame = original.decoded.at(expectedFrame); bool found = false;
                for (const auto& [key, entry] : q.renderCache.auxiliaryShpSpritesByImage)
                    if (key.rfind(framePrefix, 0) == 0 && entry.loaded && entry.frameIndex == expectedFrame) {
                        if (entry.sourceFingerprint != original.fingerprint || entry.image.pixels != rawFrame.pixels
                            || entry.image.width != rawFrame.width || entry.image.height != rawFrame.height
                            || entry.image.globalWidth != rawFrame.globalWidth || entry.image.globalHeight != rawFrame.globalHeight
                            || entry.image.offsetX != rawFrame.offsetX || entry.image.offsetY != rawFrame.offsetY) {
                            fail("rendered buildup differs from independent original frame " + std::to_string(expectedFrame)); return std::nullopt;
                        }
                        found = true;
                    }
                if (!found || stats->structureTurretAnimDrawn || stats->buildingArtAnimationLayers) {
                    fail("reverse frame absent or live turret/layer retained"); return std::nullopt;
                }
            }
            const auto buildupEntries = buildupCacheEntries(q.renderCache);
            draws << label << ',' << phase << ',' << q.tick << ',' << (cold ? "independent-cold" : "natural-warm") << ',' << milliseconds
                << ',' << pixelHash(pixels) << ',' << stats->terrainLayerCacheHits << ',' << stats->terrainLayerCacheMisses << ',' << stats->terrainMs
                << ',' << stats->terrainBlitMs << ',' << stats->worldObjectsMs << ',' << stats->structureSpriteMs << ',' << stats->worldObjectSortMs
                << ',' << stats->auxiliaryShpBytesCacheHits << ',' << stats->auxiliaryShpBytesCacheMisses << ',' << stats->auxiliaryShpBytesCacheMissMs
                << ',' << stats->auxiliaryShpFrameCacheHits << ',' << stats->auxiliaryShpFrameCacheMisses << ',' << stats->auxiliaryShpFrameCacheMissMs
                << ',' << stats->visibleStructureSprites << ',' << stats->structureShadowsDrawn << ',' << stats->structureTurretAnimDrawn
                << ',' << stats->buildingArtAnimationLayers << ',' << expectedFrame << ',' << walls.size()
                << ',' << buildupEntries.first << ',' << buildupEntries.second << '\n'; ++drawCount; return stats;
        };
        const auto comparePaints = [&](const std::string& phase, int frame, bool save = false) {
            std::vector<std::uint32_t> warm, cold, again; double a = 0, b = 0, c = 0;
            if (!draw(s, phase, false, frame, warm, a, save) || !draw(s, phase, true, frame, cold, b)
                || !draw(s, phase + "-warm-repeat", false, frame, again, c)) return false;
            if (warm != cold || warm != again) return fail("same tick cold/warm pixels differ: " + phase);
            return true;
        };
        stage = "warm pre-sale";
        std::vector<std::uint32_t> discard; double ignored = 0;
        if (!draw(s, "prewarm", false, -1, discard, ignored) || !comparePaints("before", -1)) return false;
        const auto& pathCells = runtimeStaticPathTerrainSnapshot(s);
        const auto* pathData = pathCells.data(); const auto pathSize = pathCells.size();
        const auto layerKey = s.renderCache.terrainLayer.key;
        const auto* layerPixels = s.renderCache.terrainLayer.pixels.data();
        const int missesBefore = s.renderCache.terrainLayerCacheMisses;
        if (!s.renderCache.terrainLayer.built || !s.pathTerrainCache.valid || pathSize != s.world.getTerrainCells().size()) return fail("warm map/static path was not primed");
        stage = "real UI sell"; s.selectedObjectIds = {source};
        const auto commandStart = std::chrono::steady_clock::now();
        const bool accepted = dispatchRuntimeStructureToolFromUi(s, RuntimeSidebarTool::Sell, source);
        const double commandMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - commandStart).count();
        const auto* sold = findRuntimeObjectByIdIncludingInactive(s, source);
        if (!accepted || !sold || sold->alive || sold->rubble || sold->mission != "Sold" || !s.selectedObjectIds.empty()
            || s.economy.credits != initialCredits + expectedRefund || sold->buildAnimTicksRemaining != total
            || sold->buildAnimTicksTotal != total || sold->health != rawInt(type, "Strength")) return fail("UI sale/refund/timer: " + describe(s, source));
        std::set<int> crewIds;
        for (const auto& o : s.world.getObjects()) if (!beforeIds.contains(o.instanceId)) {
            if (o.kind != WorldObjectKind::Infantry || !o.alive || o.owner != "Americans" || (o.type != "E1" && o.type != "CTECH")
                || o.health != rawInt(o.type, "Strength")) return fail("original full-HP crew differs: " + describe(s, o.instanceId));
            crewIds.insert(o.instanceId);
        }
        if (crewIds.size() != static_cast<std::size_t>(expectedCrew)) return fail("original crew count differs");
        const bool cacheRetained = s.renderCache.terrainLayer.built && s.renderCache.terrainLayer.key == layerKey
            && s.renderCache.terrainLayer.pixels.data() == layerPixels;
        const bool pathRetained = s.pathTerrainCache.valid && s.pathTerrainCache.staticCells.size() == pathSize
            && s.pathTerrainCache.staticCells.data() == pathData;
        double nextDrawMs = 0; const auto nextStats = draw(s, "next-draw", false, original.frames - 1, discard, nextDrawMs);
        if (!nextStats) return false;
        const int addedMisses = s.renderCache.terrainLayerCacheMisses - missesBefore;
        if (!cacheRetained || !pathRetained || addedMisses != 0) {
            std::ostringstream why; why << label << " warmed map invalidated: terrain=" << cacheRetained
                << " staticPath=" << pathRetained << " nextTerrainMisses=" << addedMisses
                << " commandMs=" << commandMs << " nextDrawMs=" << nextDrawMs;
            cacheFailures.push_back(why.str());
        }
        const auto population = s.world.getObjects().size();
        if (dispatchRuntimeStructureToolFromUi(s, RuntimeSidebarTool::Sell, source) || s.economy.credits != initialCredits + expectedRefund
            || s.world.getObjects().size() != population) return fail("duplicate UI sale repeated refund/crew");
        std::unique_ptr<RuntimeWindowState> ordinary, canonical; int sceneOrdinary = 0, sceneCanonical = 0;
        const auto replicas = [&]() { return std::array<RuntimeWindowState*, 3>{{&s, ordinary.get(), canonical.get()}}; };
        const auto verify = [&](RuntimeWindowState& q, const char* branch, int elapsed, double simulationMs) {
            const auto* o = findRuntimeObjectByIdIncludingInactive(q, source); const int remaining = total - elapsed;
            const auto* opposition = findObjectById(q, enemy);
            if (!resourcesValid(q) || !o || o->alive || o->health != rawInt(type, "Strength") || o->mission != "Sold"
                || o->buildAnimTicksRemaining != remaining || o->buildAnimTicksTotal != (remaining ? total : 0)
                || q.economy.credits != initialCredits + expectedRefund || q.world.getObjects().size() != population
                || !opposition || opposition->health != 750 || q.skirmishMatchFinished) return fail(std::string(branch) + " sale continuation: " + describe(q, source));
            int actualCrew = 0;
            for (int id : crewIds) {
                const auto* c = findObjectById(q, id); if (!c || c->owner != "Americans" || c->health != rawInt(c->type, "Strength"))
                    return fail(std::string(branch) + " survivor identity/HP changed: " + describe(q, id));
                ++actualCrew;
                crew << label << ',' << branch << ',' << stage << ',' << q.tick << ',' << c->instanceId << ',' << c->type << ',' << c->owner
                    << ',' << c->health << ',' << c->alive << ',' << c->x << ',' << c->y << ',' << c->level << ',' << runtimeObjectRenderX(*c)
                    << ',' << runtimeObjectRenderY(*c) << ',' << runtimeDrawLevelForObject(*c) << ',' << c->mission << '\n';
            }
            for (int id : denseIds) { const auto* c = findObjectById(q, id); if (!c || c->health != 125) return fail("dense full-HP control changed"); }
            for (int id : walls) { const auto* w = findObjectById(q, id); if (!w || w->health != 300 || w->type != "GAWALL") return fail("wall identity/HP changed"); }
            const int expectedFrame = remaining ? original.frames - 1 - std::min(original.frames - 1, elapsed * original.frames / total) : -1;
            ticks << label << ',' << branch << ',' << stage << ',' << q.tick << ',' << elapsed << ',' << remaining << ',' << o->buildAnimTicksTotal
                << ',' << expectedFrame << ',' << o->health << ',' << o->alive << ',' << q.economy.credits << ',' << actualCrew << ',' << simulationMs
                << ',' << q.perf.lastSimEffectsMs << ',' << q.perf.lastSimEconomyMs << ',' << q.perf.lastSimBuildMs << ',' << q.perf.lastSimMoveMs
                << ',' << q.perf.lastSimAttackMs << ',' << q.perf.lastSimPathMs << '\n'; return true;
        };
        stage = "original reverse animation";
        for (int elapsed = 0; elapsed <= total; ++elapsed) {
            if (elapsed) {
                for (const auto& b : {std::pair<const char*, RuntimeWindowState*>{"original", &s}, {"ordinary14", ordinary.get()}, {"canonical-extension", canonical.get()}})
                    if (b.second && !verify(*b.second, b.first, elapsed, nativeStep(*b.second))) return false;
            } else if (!verify(s, "original", 0, 0)) return false;
            const int frame = elapsed < total ? original.frames - 1 - std::min(original.frames - 1, elapsed * original.frames / total) : -1;
            double paintMs = 0; const auto rendered = draw(s, "reverse", false, frame, discard, paintMs); if (!rendered) return false;
            if (elapsed == total) {
                // The original walls remain legitimate global structure draws,
                // including their offscreen overlay resource hits.
                if (runtimeStructureIsSelling(*findRuntimeObjectByIdIncludingInactive(s, source))
                    || rendered->visibleStructureSprites != static_cast<int>(walls.size())
                    || rendered->structureShadowsDrawn || rendered->structureTurretAnimDrawn) {
                    std::ostringstream why; why << "sale completion body=" << rendered->visibleStructureSprites
                        << " expectedControlWalls=" << walls.size() << " shadow=" << rendered->structureShadowsDrawn
                        << " turret=" << rendered->structureTurretAnimDrawn;
                    return fail(why.str());
                }
                // FreeBuildup applies only when the last user of this image has
                // naturally finished. No manual eviction or cold cache is used.
                for (const auto& b : {std::pair<const char*, RuntimeWindowState*>{"original", &s},
                        {"ordinary14", ordinary.get()}, {"canonical-extension", canonical.get()}}) if (b.second) {
                    const auto entries = buildupCacheEntries(b.second->renderCache);
                    if (entries.first || entries.second) {
                        std::ostringstream why; why << b.first << " completed FreeBuildup image=" << original.image
                            << " frames=" << entries.first << " bytes=" << entries.second;
                        return fail(why.str());
                    }
                }
            }
            if (elapsed == 0 || elapsed == total / 2 || elapsed == total - 1 || elapsed == total)
                if (!comparePaints("reverse-" + std::to_string(elapsed), frame, elapsed == total / 2 && lake)) return false;
            if (elapsed == total / 2) {
                stage = "mid-sale strict checkpoint";
                std::ostringstream payload, normalized; IniFile base, extension;
                if (!writeRuntimeStateToStream(s, payload, error) || !base.loadFromString(payload.str())
                    || base.getValue("RuntimeSave", "Version", "") != "14") return fail("ordinary14 writer/parser: " + error);
                const auto ordinaryPath = root / (label + "-mid-ordinary14.ini");
                { std::ofstream file(ordinaryPath, std::ios::binary); file << payload.str(); if (!file) return fail("ordinary save artifact"); }
                if (!writeRuntimeMultiplayerSnapshotExtension(s, payload, error) || !extension.loadFromString(payload.str())
                    || !writeRuntimeMultiplayerSnapshotCanonicalIni(extension, normalized, error) || !extension.loadFromString(normalized.str())) return fail("canonical extension writer/parser: " + error);
                { std::ofstream file(root / (label + "-mid-canonical-extension.ini"), std::ios::binary); file << normalized.str(); if (!file) return fail("canonical save artifact"); }
                for (bool exact : {false, true}) {
                    auto copy = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy, s, false); RuntimeSnapshotActiveStateScope restore(*copy);
                    if (!materializeRuntimeLoadStateFromIni(*copy, base, error)
                        || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*copy, extension, error))) return fail("strict mid-sale restore: " + error);
                    copy->runtimeSoundsEnabled = copy->runtimePreferencePersistenceEnabled = copy->visionEnabled = false; facade(*copy);
                    if (copy->tick != s.tick || !verify(*copy, exact ? "canonical-extension" : "ordinary14", elapsed, 0)) return false;
                    checkpoints << label << ',' << (exact ? "ordinary14+canonical-extension" : "ordinary14") << ',' << copy->tick
                        << ",30," << source << ',' << total - elapsed << ',' << total << ',' << copy->economy.credits << ',' << crewIds.size() << ",PASS\n";
                    if (exact) { canonical = std::move(copy); ++sceneCanonical; ++canonicalCount; }
                    else { ordinary = std::move(copy); ++sceneOrdinary; ++ordinaryCount; }
                }
                stage = "restored reverse animation";
            }
            ++reverseCount;
        }
        // Ordinary14 deliberately retains its existing general path rebuild
        // semantics. Only sale identity/timer/payment are compared above;
        // these separate real orders prove the locally updated collision map.
        stage = "real freed-footprint ore and wall routes";
        const std::array<RuntimeCellPoint, 3> goals{{{152, 150}, {142, 144}, {160, 144}}};
        const std::array<const char*, 3> roles{{"freed-foundation", "original-ore", "unchanged-wall"}};
        for (auto* q : replicas()) if (q) for (std::size_t i = 0; i < witnesses.size(); ++i) {
            RuntimeSnapshotActiveStateScope route(*q); q->selectedObjectIds = {witnesses[i]};
            if (!dispatchRuntimeTargetOrderFromUi(*q, RuntimeMultiplayerGameplayTargetOrderKind::Move, goals[i])
                || q->lastCommand.acceptedCount != 1) return fail("actual route UI rejected: " + describe(*q, witnesses[i]));
        }
        std::array<bool, 3> crossed{}; bool arrived = false;
        for (int n = 0; n <= 30 * 60; ++n) {
            arrived = true;
            for (const auto& b : {std::pair<const char*, RuntimeWindowState*>{"original", &s}, {"ordinary14", ordinary.get()}, {"canonical-extension", canonical.get()}}) if (b.second) {
                auto& q = *b.second;
                for (std::size_t i = 0; i < witnesses.size(); ++i) {
                    const auto* o = findObjectById(q, witnesses[i]); if (!o || o->health != 300) return fail("original witness health/identity changed");
                    const double x = runtimeObjectRenderX(*o), y = runtimeObjectRenderY(*o);
                    for (int id : walls) { const auto* wall = findObjectById(q, id);
                        if (wall && x > wall->x - .5 + 1e-8 && x < wall->x + .5 - 1e-8
                            && y > wall->y - .5 + 1e-8 && y < wall->y + .5 - 1e-8) return fail("real body traversed unchanged wall"); }
                    if (b.second == &s && i == 0 && x >= 149.5 && x <= 151.5 && std::abs(y - 150) < .5) crossed[i] = true;
                    const auto cell = q.terrainByCell.find(runtimeCellKey(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))));
                    if (b.second == &s && i == 1 && cell != q.terrainByCell.end() && cell->second->overlayIsOre) crossed[i] = true;
                    if (b.second == &s && i == 2 && o->y >= 139 && o->y <= 141 && o->x != 160) crossed[i] = true;
                    const bool settled = o->x == goals[i].x && o->y == goals[i].y && !o->renderMoveActive && !q.behaviors.contains(o->instanceId)
                        && std::abs(x - goals[i].x) < 1e-8 && std::abs(y - goals[i].y) < 1e-8;
                    movement << label << ',' << b.first << ',' << roles[i] << ',' << q.tick << ',' << o->instanceId << ',' << o->x << ',' << o->y
                        << ',' << o->level << ',' << x << ',' << y << ',' << runtimeDrawLevelForObject(*o) << ',' << o->health << ',' << o->renderMoveActive << ',' << settled << '\n';
                    arrived = arrived && settled;
                }
            }
            if (arrived) break;
            for (auto* q : replicas()) if (q) nativeStep(*q);
        }
        if (!arrived || !std::all_of(crossed.begin(), crossed.end(), [](bool yes) { return yes; })) return fail("natural route did not reach its exact goal/required surface");
        routeCount += 3;
        if (s.economy.credits != initialCredits + expectedRefund || s.world.getObjects().size() != population) return fail("post-sale duplicate money/crew");
        const bool performancePass = cacheRetained && pathRetained && addedMisses == 0;
        cases << std::setprecision(17) << label << ",30," << type << ',' << s.world.getTerrainCells().size() << ',' << denseIds.size()
            << ',' << rawInt(type, "Strength") << ',' << rawInt(type, "Cost") << ',' << expectedRefund << ',' << expectedCrew << ',' << commandMs << ',' << nextDrawMs
            << ',' << cacheRetained << ',' << pathRetained << ',' << addedMisses << ',' << total << ',' << sceneOrdinary << ',' << sceneCanonical << ",3,"
            << (performancePass ? "PASS" : "FAIL_CACHE") << '\n';
        ++completed; std::cout << "Sell performance " << label << " cache=" << cacheRetained << " path=" << pathRetained
            << " terrainMisses=" << addedMisses << " commandMs=" << commandMs << " nextDrawMs=" << nextDrawMs
            << " refund=" << expectedRefund << " crew=" << expectedCrew << " ticks=" << total << " correctness=PASS\n";
    }
    if (!completed || (filter.empty() && (completed != 4 || ordinaryCount != 4 || canonicalCount != 4 || routeCount != 12))) return fail("complete exact scene/checkpoint/route count differs");
    if (!cacheFailures.empty()) {
        std::ostringstream detail; detail << cacheFailures.size() << " immutable-map performance contract failures";
        for (const auto& why : cacheFailures) detail << "\n" << why;
        return fail(detail.str());
    }
    std::cout << "Sell performance passed: " << completed << " real UI scenes, " << reverseCount << " reverse ticks, " << drawCount
        << " map paints, " << ordinaryCount << " ordinary14 + " << canonicalCount << " canonical extensions, " << routeCount
        << " genuine surface routes; CPU timings, not display FPS\n";
    return true;
}
