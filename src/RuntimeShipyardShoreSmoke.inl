bool runRuntimeShipyardShoreSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "shipyard shore: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
    seedRuntimeLoadScratch(s, live, false);
    s.world = live.world; s.world.accessObjects().clear();
    s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
    s.visionEnabled = false; s.config.computerAiEnabled = false;
    s.config.playerSlots.clear(); s.config.multiplayerActive = false;
    s.config.playerCountry = "YuriCountry"; s.economy.owner = "YuriCountry";
    s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
    s.runtimePreferencePersistenceEnabled = false; s.runtimeSoundsEnabled = false;
    s.runtimeSuppressPlayerAudioFeedback = true;
    initializeRuntimeNavigation(s);
    RuntimeSnapshotActiveStateScope active(s);
    const char* output = std::getenv("CONCORD_SHIPYARD_SHORE_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    std::vector<RuntimeCellPoint> beachSites, waterSites;
    for (const auto& terrain : s.world.getTerrainCells()) {
        const RuntimeCellPoint site{terrain.x, terrain.y};
        int beaches = 0; bool wet = true;
        for (int y = 0; y < 4 && wet; ++y) for (int x = 0; x < 4; ++x) {
            const auto it = s.terrainByCell.find(runtimeCellKey(site.x+x, site.y+y));
            if (it == s.terrainByCell.end()) { wet = false; break; }
            const auto info = runtimeCachedTerrainInfoForCell(s, *it->second);
            if (!info.water && !info.shore) { wet = false; break; }
            beaches += info.shore ? 1 : 0;
        }
        if (wet) (beaches ? beachSites : waterSites).push_back(site);
    }
    if (beachSites.empty() || waterSites.empty()) return fail("map needs both beach and open-water foundations");
    int rejected = 0, allowed = 0, captures = 0, failures = 0, placements = 0;
    for (const std::string type : {"GAYARD", "NAYARD", "YAYARD"}) {
        RuntimeBuildCatalogItem item; item.kind = RuntimeBuildKind::Building;
        item.typeId = type; item.waterBound = true; item.naval = true;
        for (const auto site : beachSites) {
            if (runtimeCellCanPlaceBuilding(s, item, site)) ++failures;
            else ++rejected;
        }
        for (const auto site : waterSites) {
            if (!runtimeCellCanPlaceBuilding(s, item, site)) ++failures;
            else ++allowed;
        }
        // The four extreme shoreline sites reproduce each facing of the lake.
        for (int edge = 0; edge < 4; ++edge) {
            const auto score = [edge](const RuntimeCellPoint& c) {
                return edge == 0 ? c.x+c.y : edge == 1 ? -c.x-c.y
                    : edge == 2 ? c.x-c.y : c.y-c.x;
            };
            const auto beach = *std::max_element(beachSites.begin(), beachSites.end(),
                [&](const auto& a, const auto& b) { return score(a) < score(b); });
            const auto water = *std::min_element(waterSites.begin(), waterSites.end(),
                [&](const auto& a, const auto& b) {
                    const auto distance = [&](const auto& c) {
                        const int dx=c.x-beach.x, dy=c.y-beach.y; return dx*dx+dy*dy;
                    };
                    return distance(a) < distance(b);
                });
            std::cout << "Shore site " << type << " edge=" << edge << " beach="
                << beach.x << ',' << beach.y << " water=" << water.x << ',' << water.y << '\n';
            for (int position = 0; position < 2; ++position) {
                const auto site = position == 0 ? beach : water;
                auto* yard = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, type, site);
                if (!yard) return fail(type + " creation");
                yard->owner = s.economy.owner; yard->poweredOperational = true;
                yard->buildAnimTicksTotal = yard->buildAnimTicksRemaining = 0;
                yard->health = runtimeMaxHealthForObject(s, *yard);
                s.tick = 24;
                if (!root.empty()) {
                    const auto stats = drawRuntimeStateOffscreenAtCell(s,
                        {site.x+1, site.y+1}, 640, 520, error,
                        root+"/"+type+"-edge"+std::to_string(edge)+(position==0?"-beach.bmp":"-water.bmp"), false);
                    if (!stats || stats->visibleStructureSprites != 1 || stats->missingStructureSprites != 0)
                        return fail(type + " native sprite capture: " + error);
                    ++captures;
                }
                s.world.accessObjects().clear();
                initializeRuntimeNavigation(s);
            }
            RuntimeBuildCatalogItem power; power.kind = RuntimeBuildKind::Building; power.typeId = "GAPOWR";
            std::optional<RuntimeCellPoint> baseSite;
            for (const auto& terrain : s.world.getTerrainCells()) {
                if (std::abs(terrain.x-water.x) > 9 || std::abs(terrain.y-water.y) > 9) continue;
                const RuntimeCellPoint cell{terrain.x, terrain.y};
                if (runtimeCellCanPlaceBuilding(s, power, cell)) { baseSite = cell; break; }
            }
            if (!baseSite || !addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "GAPOWR", *baseSite))
                return fail("nearby dry-land base fixture");
            initializeRuntimeNavigation(s);
            const auto beachChecks = runtimePlacementCellChecks(s, item, beach);
            const auto waterChecks = runtimePlacementCellChecks(s, item, water);
            if (beachChecks.size() != 16 || waterChecks.size() != 16
                || std::none_of(beachChecks.begin(), beachChecks.end(), [](const auto& c) {
                    return !c.valid && c.reason == "needs water";
                })
                || !std::all_of(waterChecks.begin(), waterChecks.end(), [](const auto& c) { return c.valid; }))
                return fail(type + " preview differs from physical shoreline check");
            initializeRuntimeLocalProductionLanes(s.economy); s.economy.catalog.clear();
            item.cost = 1000; s.economy.credits = 4321;
            if (!stageRuntimeReadyPlacementForProbe(s, item)) return fail("ready shipyard fixture");
            const auto orderHash = runtimeProductionLanesStateHash(s.economy.localProductionLanes);
            if (placeRuntimeReadyBuilding(s, beach) || s.world.getObjects().size() != 1
                || s.economy.credits != 4321
                || runtimeProductionLanesStateHash(s.economy.localProductionLanes) != orderHash)
                return fail(type + " beach click consumed the ready shipyard");
            if (!placeRuntimeReadyBuilding(s, water) || s.world.getObjects().size() != 2
                || runtimeSelectedPlacement(s).has_value() || s.economy.credits != 4321)
                return fail(type + " nearby water placement failed or charged twice");
            ++placements;
            s.world.accessObjects().clear(); initializeRuntimeNavigation(s);
        }
    }
    std::cout << "Shipyard shore: beach foundations rejected=" << rejected
        << ", open-water foundations accepted=" << allowed << ", captures=" << captures
        << ", ready-placement cases=" << placements << ", mismatches=" << failures << '\n';
    return failures == 0 ? true : fail("beach accepted or open water rejected");
}
