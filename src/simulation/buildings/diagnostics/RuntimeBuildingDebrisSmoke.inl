bool runRuntimeBuildingDebrisSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "BuildingDebris: " + why; return false; };
    const char* requested = std::getenv("CONCORD_BUILDING_DEBRIS_DIR");
    const std::string root = requested && *requested ? requested
        : "build/smoke/feature-checks/13-building-debris/checks";
    std::filesystem::create_directories(root);
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("stock resources unavailable");
    IniFile source;
    if (!source.loadFromFile("local_data/rulesmd.ini")) return fail("original rules unavailable");
    const auto make = [&](int hz, int level) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = stock->config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.computerAiEnabled = s.config.noCooldown = s.config.multiplayerActive = s.config.crates = false;
        s.config.shortGame = s.config.superweapons = false; s.config.gameMode = "Battle";
        s.config.gameModeRulesOverride.clear();
        s.report = stock->report; s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        // The renderer needs the loaded map summary even with synthetic cells.
        s.world = stock->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.economy.credits = 10000; s.economy.techLevel = 10; initializeRuntimeLocalProductionLanes(s.economy);
        for (int y = 1; y <= 32; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = cell.subTileIndex = 0; cell.level = level;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s); return box;
    };
    const auto ids = [](const std::vector<const ArtType*>& pool) {
        std::vector<std::string> result;
        for (const auto* art : pool) result.push_back(art->id);
        return result;
    };
    const auto sameProjectiles = [](const RuntimeWindowState& a, const RuntimeWindowState& b) {
        if (a.projectiles.size() != b.projectiles.size()) return false;
        for (std::size_t i = 0; i < a.projectiles.size(); ++i)
            if (serializeRuntimeProjectileEffect(a.projectiles[i]) != serializeRuntimeProjectileEffect(b.projectiles[i])) return false;
        return true;
    };
    int buildings = 0, customLists = 0, draws = 0, fragments = 0, deaths = 0, snapshots = 0, savedRuns = 0, boundaries = 0;
    std::ofstream samples(root + "/building-lists.csv");
    samples << "building,custom_list,min,max,samples,smallest,largest,distinct_types\n";
    auto poolProbe = make(30,0); auto& probe = *poolProbe;
    const auto defaults = splitRuntimeTokenList(source.getValue("General","MetallicDebris",""));
    if (defaults.size() != 20 || probe.rules.getCombatDetailRules().metallicDebris != source.getValue("General","MetallicDebris",""))
        return fail("stock global list");
    WorldObjectInstance destroyed; destroyed.instanceId = 1000; destroyed.kind = WorldObjectKind::Structure;
    destroyed.owner = "Americans"; destroyed.x = destroyed.y = 16; destroyed.cell = runtimeCellKey(16,16);
    for (const auto& type : probe.rules.getBuildingTypes()) {
        auto expected = splitRuntimeTokenList(source.getValue(type.id,"DebrisAnims",""));
        const bool custom = !expected.empty(); if (custom) ++customLists; else expected = defaults;
        if (type.debrisAnims != source.getValue(type.id,"DebrisAnims","")) return fail(type.id + " raw authored list");
        if (ids(runtimeBuildingDebrisArtPool(probe,type)) != expected) return fail(type.id + " pool sequence differs");
        destroyed.type = type.id; int smallest = INT_MAX, largest = 0; std::unordered_set<std::string> seen;
        for (int sample = 0; sample < 64; ++sample) {
            probe.tick = sample; probe.projectiles.clear();
            const int count = spawnRuntimeBuildingDebris(probe,destroyed,type,nullptr);
            const int low = type.maxDebris <= 0 ? 0 : std::min(type.minDebris,type.maxDebris-1);
            const int high = type.maxDebris <= 0 ? 0 : std::max(type.minDebris,type.maxDebris-1);
            if (count < low || count > high || count != static_cast<int>(probe.projectiles.size()))
                return fail(type.id + " retail count interval");
            smallest = std::min(smallest,count); largest = std::max(largest,count);
            for (const auto& p : probe.projectiles) {
                if (std::find(expected.begin(),expected.end(),p.projectileId) == expected.end()) return fail(type.id + " foreign fragment");
                const auto* art = probe.art.findArtType(p.projectileId);
                if (!art || p.impactAnimId != art->expireAnim || p.trailerAnimId != art->trailerAnim
                    || p.projectileImageId != (art->image.empty() ? art->id : art->image)) return fail(type.id + " fragment artwork metadata");
                RuntimeProjectileEffect restored;
                if (!parseRuntimeProjectileEffect(serializeRuntimeProjectileEffect(p),restored)
                    || serializeRuntimeProjectileEffect(restored) != serializeRuntimeProjectileEffect(p)) return fail(type.id + " fragment record");
                seen.insert(p.projectileId); ++snapshots; ++fragments;
            }
            ++draws;
        }
        samples << type.id << ',' << custom << ',' << type.minDebris << ',' << type.maxDebris << ",64," << smallest << ',' << largest << ',' << seen.size() << '\n';
        ++buildings;
    }
    if (buildings != 399 || customLists != 166) return fail("unexpected stock building coverage");
    std::cout << "Stock building debris lists passed: buildings=" << buildings << " own=" << customLists << " draws=" << draws << '\n';
    // Small independent INI fixtures cover parsing, explicit lists, duplicate
    // probability entries and the unusual equal/reversed count endpoints.
    const auto parse = [](const std::string& general, const std::string& building) {
        IniFile ini; ini.loadFromString("[General]\n" + general + "\n[BuildingTypes]\n0=TEST\n[TEST]\n" + building);
        RulesDatabase rules; rules.loadFromIni(ini); return rules;
    };
    const auto basic = parse("MetallicDebris=DBRS10LG,DBRS10SM", "MinDebris=4\nMaxDebris=6\nDebrisAnims=DBRI-WM1,DBRI-WM1,DBRIS1SM");
    probe.rules = basic;
    const auto* fixture = probe.rules.findBuildingType("TEST");
    if (!fixture || ids(runtimeBuildingDebrisArtPool(probe,*fixture)) != std::vector<std::string>{"DBRI-WM1","DBRI-WM1","DBRIS1SM"})
        return fail("duplicate entries or non-DBRIS names");
    ++boundaries;
    for (const auto& entry : std::vector<std::pair<std::string,std::vector<std::string>>>{
        {"",{"DBRS10LG","DBRS10SM"}}, {"DebrisAnims=none,<none>",{"DBRS10LG","DBRS10SM"}},
        {"DebrisAnims=UnavailableDebrisAsset",{}}, {"DebrisAnims=DBRIS1SM\nDebrisTypes=TIRE\nDebrisMaximums=9",{}},
        {"DebrisAnims=DBRIS1SM\nDebrisTypes=none,<none>",{"DBRIS1SM"}},
        {"DebrisAnims=DBRIS1SM\nDebrisMaximums=99,1",{"DBRIS1SM"}},
        {"DebrisAnims=DBRIS1SM,none,UnavailableDebrisAsset,DBRI-WM1",{"DBRIS1SM","DBRI-WM1"}}}) {
        probe.rules = parse("MetallicDebris=DBRS10LG,DBRS10SM",entry.first);
        if (ids(runtimeBuildingDebrisArtPool(probe,*probe.rules.findBuildingType("TEST"))) != entry.second)
            return fail("fallback / voxel isolation: " + entry.first);
        ++boundaries;
    }
    probe.rules = parse("","MinDebris=1\nMaxDebris=5");
    if (!runtimeBuildingDebrisArtPool(probe,*probe.rules.findBuildingType("TEST")).empty()) return fail("invented default list");
    ++boundaries;
    struct Range { int minimum, maximum, low, high; };
    for (const Range r : {Range{-4,-1,0,0}, {0,0,0,0}, {0,1,0,0}, {0,2,0,1}, {1,1,0,1}, {4,6,4,5}, {5,5,4,5}, {9,2,8,9}, {25,28,25,27}}) {
        probe.rules = parse("MetallicDebris=DBRIS1SM", "MinDebris="+std::to_string(r.minimum)+"\nMaxDebris="+std::to_string(r.maximum));
        const auto& rule = *probe.rules.findBuildingType("TEST"); destroyed.type = "TEST";
        int minimumObserved = INT_MAX, maximumObserved = 0;
        for (int sample = 0; sample < 128; ++sample) {
            probe.tick = sample; probe.projectiles.clear();
            const int count = spawnRuntimeBuildingDebris(probe,destroyed,rule,nullptr);
            if (count < r.low || count > r.high) return fail("independent count boundary");
            minimumObserved = std::min(minimumObserved,count); maximumObserved = std::max(maximumObserved,count);
        }
        if (minimumObserved != r.low || maximumObserved != r.high) return fail("count endpoint never sampled");
        ++boundaries;
    }
    const auto baseline = parse("MetallicDebris=DBRIS1SM", "MinDebris=4\nMaxDebris=6\nDebrisAnims=DBRIS1SM");
    for (const std::string& change : {"MinDebris=3", "MaxDebris=7", "DebrisAnims=DBRIS2SM", "DebrisTypes=TIRE", "DebrisMaximums=2"}) {
        IniFile ini; ini.loadFromString("[General]\nMetallicDebris=DBRIS1SM\n[BuildingTypes]\n0=TEST\n[TEST]\nMinDebris=4\nMaxDebris=6\nDebrisAnims=DBRIS1SM");
        IniFile overlay; overlay.loadFromString("[TEST]\n"+change); ini.appendFrom(overlay);
        RulesDatabase altered; altered.loadFromIni(ini);
        if (baseline.getDetailRulesHash() == altered.getDetailRulesHash()) return fail("compatibility ignores " + change);
        ++boundaries;
    }
    if (baseline.getDetailRulesHash() == parse("MetallicDebris=DBRIS2SM", "MinDebris=4\nMaxDebris=6\nDebrisAnims=DBRIS1SM").getDetailRulesHash())
        return fail("compatibility ignores MetallicDebris");
    ++boundaries;
    // Destruction through the normal object-exit path, followed by live travel,
    // a real save/load and continued impacts. Include building-collapse art.
    for (const char* id : {"GACNST","GAPOWR","GAPILE","NACNST","YACNST","GATECH","CASANF01","CAHOSP"})
        for (int hz : {30,60}) for (int level : {0,2}) {
        auto box = make(hz,level); auto& s = *box;
        const auto* rule = s.rules.findBuildingType(id); if (!rule) return fail(std::string("missing ")+id);
        WorldObjectInstance building = destroyed; building.type = id; building.instanceId = 1000;
        building.level = level; building.alive = true; building.health = rule->strength;
        s.world.accessObjects().push_back(building);
        RuntimeObjectExitRequest request; request.objectId = 1000; request.kind = RuntimeObjectExitKind::Destroyed;
        request.tick = s.tick; request.suppressCrew = request.suppressDeathWeapon = true;
        if (!finalizeRuntimeObjectExit(s,request).accepted) return fail(std::string(id)+" normal destruction route");
        const int low = std::min(rule->minDebris,rule->maxDebris-1), high = std::max(rule->minDebris,rule->maxDebris-1);
        if (static_cast<int>(s.projectiles.size()) < low || static_cast<int>(s.projectiles.size()) > high)
            return fail(std::string(id)+" destruction count");
        for (int tick = 0; tick < 4; ++tick) { ++s.tick; advanceRuntimeEffects(s); }
        const auto savedProjectiles = s.projectiles;
        const bool expectsExpiration = std::any_of(savedProjectiles.begin(),savedProjectiles.end(),
            [](const RuntimeProjectileEffect& p) { return !p.impactAnimId.empty(); });
        const std::string path = root + "/debris-" + id + '-' + std::to_string(hz) + '-' + std::to_string(level) + ".ini";
        if (!saveRuntimeState(s,path,error)) return fail("save " + error);
        auto restored = make(hz,level);
        if (!loadRuntimeState(*restored,path,error)) return fail("load " + error);
        if (!sameProjectiles(s,*restored)) return fail(std::string(id)+" saved fragment state");
        ++savedRuns;
        if (hz == 30 && level == 0) {
            const auto rendered = drawRuntimeStateOffscreenAtCell(s,{16,16},640,480,error,
                root + "/" + id + "-debris.bmp",false,false,-1,nullptr,false,nullptr,false,true);
            if (!rendered || rendered->projectileActorTravelDrawn < 1) return fail(std::string(id)+" original debris not rendered: "+error);
        }
        bool expiration = false;
        for (int tick = 0; tick < 150; ++tick) {
            ++s.tick; advanceRuntimeEffects(s); ++restored->tick; advanceRuntimeEffects(*restored);
            if (!sameProjectiles(s,*restored)) return fail(std::string(id)+" continued saved fragment trajectory");
            for (const auto& effect : s.impactEffects)
                for (const auto& p : savedProjectiles) if (!p.impactAnimId.empty() && effect.animId == p.impactAnimId) expiration = true;
        }
        if (!s.projectiles.empty() || expiration != expectsExpiration) return fail(std::string(id)+" fragment landing/ExpireAnim");
        ++deaths;
        std::cout << "Debris destruction/save passed: " << id << " hz=" << hz << " level=" << level << '\n';
    }
    RuntimeMapRenderContext context; context.rules = &stock->rules; context.art = &stock->art;
    context.assetRootPath = runtimeAssetRootPath(stock->config);
    context.palettePath = runtimePalettePath(stock->config,stock->report.map);
    context.structurePalettePath = runtimeStructurePalettePath(stock->config,stock->report.map);
    if (!runRuntimeBuildingDebrisPaletteContractCheck(context,error)) return false;
    std::cout << "BuildingDebris passed: buildings=" << buildings << " custom=" << customLists << " draws=" << draws
        << " fragments=" << fragments << " records=" << snapshots << " deaths=" << deaths
        << " saves=" << savedRuns << " boundaries=" << boundaries << "; stock lists, count interval, rendering and persistence.\n";
    return true;
}
