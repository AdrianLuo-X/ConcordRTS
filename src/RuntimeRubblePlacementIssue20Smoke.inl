// Issue 20 only: original dead Structure rubble blocks construction, not navigation.
// The ready order is staged through the existing probe API, not natural production.
bool runRuntimeRubblePlacementIssue20Smoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene, stage;
    const auto fail = [&](const std::string& why) {
        error = "Rubble placement " + scene + " [" + stage + "]: " + why; return false;
    };
    const char* outputValue = std::getenv("CONCORD_RUBBLE_PLACEMENT_ISSUE20_OUTPUT");
    if (!outputValue || !*outputValue) return fail("output directory required");
    const std::filesystem::path output(outputValue);
    std::filesystem::create_directories(output);
    std::ofstream cases(output / "cases.csv"), actors(output / "actors.csv"),
        queries(output / "queries.csv"), searches(output / "searches.csv"), commits(output / "commits.csv");
    if (!cases || !actors || !queries || !searches || !commits) return fail("CSV open");
    cases << "case,hz,type,exit,foundation_width,foundation_height,leave_rubble,collapse_ticks,result\n";
    actors << "case,stage,tick,id,type,x,y,alive,hp,rubble,collapse_count,crew_suppressed,death_weapon_suppressed\n";
    queries << "case,stage,tick,x,y,blocked,in_range,preview_valid,fast_default,fast_reused,wrapper_default,wrapper_reused,nav_override,nav_reused,sidebar_reused,rubble_cells\n";
    searches << "case,search,x,y,valid,disjoint\n";
    commits << "case,stage,x,y,accepted,objects_before,objects_after,credits_before,credits_after,serial_before,serial_after,lanes_before,lanes_after\n";
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original assets unavailable");
    assets->rulesLoaded = assets->report.rulesLoaded; assets->artLoaded = assets->report.artLoaded;
    assets->theaterLoaded = assets->report.theaterLoaded;
    TerrainCell clear; clear.tileIndex = 0; clear.subTileIndex = 0; clear.level = 0;
    const auto metadata = runtimeTerrainMetadataContext(*assets);
    if (resolveRuntimeTerrainSubTileLandType(clear, metadata, assets->renderCache) != RuntimeTerrainLandType::Clear
        || resolveRuntimeTerrainSubTileRampType(clear, metadata, assets->renderCache) != 0)
        return fail("original Clear0 flat TMP required");
    std::ofstream provenance(output / "fixture.txt");
    provenance << "rules=" << assets->report.rulesPath << "\noriginal_clear_tile=0\noriginal_clear_subtile=0\nhz=30\n"
        << "ready=stageRuntimeReadyPlacementForProbe; not natural production\n"
        << "destroyed=finalizeRuntimeObjectExit; original LeaveRubble/DestroyAnim; crew/death weapon isolated\n"
        << "persistence=not tested\nBMP=actual offscreen placement views; no pixel oracle or issue21 claim\n";
    struct Spec { const char* type; int w, h; RuntimeObjectExitKind exit; const char* exitName; bool rubble; };
    const Spec specs[] = {
        {"CAWASH01",4,4,RuntimeObjectExitKind::Destroyed,"Destroyed",true},
        {"CAHSE01",3,2,RuntimeObjectExitKind::Destroyed,"Destroyed",true},
        {"GAPOWR",2,2,RuntimeObjectExitKind::Destroyed,"Destroyed",false},
        {"CAHSE01",3,2,RuntimeObjectExitKind::Sold,"Sold",false},
        {"CAHSE01",3,2,RuntimeObjectExitKind::TemporalDestroyed,"TemporalDestroyed",false}
    };
    int completed = 0, rejected = 0, placed = 0, searchCount = 0, captures = 0;
    const RuntimeCellPoint origin{32,32}, positive{32,29};
    for (const auto& spec : specs) {
        scene = std::string(spec.type) + "-" + spec.exitName; stage = "setup";
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, *assets, false);
        s.config = config; s.config.fixedTickHz = 30; s.config.playerCountry = "YuriCountry";
        s.config.playerSlots.clear(); s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.crates = s.config.superweapons = false;
        s.config.shortGame = s.config.noCooldown = false; s.config.mcvRepacks = true;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "YuriCountry";
        s.economy.skirmishStartApplied = true; s.economy.credits = 100000;
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFactsLoaded = false;
        s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        for (int y=0; y<64; ++y) for (int x=0; x<64; ++x) {
            TerrainCell cell = clear; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            s.world.accessTerrainCells().push_back(cell);
        }
        if (!addRuntimeObjectInstanceForOwner(s,s.economy.owner,RuntimeBuildKind::Building,"YACNST",{26,30})
            || !addRuntimeObjectInstanceForOwner(s,s.economy.owner,RuntimeBuildKind::Building,"YACNST",{37,30}))
            return fail("original base anchors");
        initializeRuntimeNavigation(s); initializeRuntimeLocalProductionLanes(s.economy);
        RuntimeSnapshotActiveStateScope active(s);
        addRuntimeBuildCatalogItem(s,RuntimeBuildKind::Building,"YAPOWR");
        const auto catalog = runtimeProductionCatalogLookup(s,"YAPOWR");
        if (!catalog || !catalog->item) return fail("original ready catalogue");
        const RuntimeBuildCatalogItem item = *catalog->item;
        const auto* targetRule = s.rules.findBuildingType("YAPOWR");
        const auto* sourceRule = s.rules.findBuildingType(spec.type);
        const auto* sourceArt = s.art.findArtType(spec.type);
        const auto* targetArt = s.art.findArtType("YAPOWR");
        if (!sourceRule || !sourceArt || !targetRule || !targetArt
            || sourceArt->foundation != std::to_string(spec.w)+"x"+std::to_string(spec.h)
            || targetArt->foundation != "2x2" || item.cost != targetRule->cost
            || sourceRule->leaveRubble != (std::string(spec.type)!="GAPOWR"))
            return fail("original loaded rule/art dimensions or cost");
        const auto targetFootprint = runtimeStructureFootprintForBuildItem(s,item);
        if (targetFootprint.width != 2 || targetFootprint.height != 2) return fail("target footprint cache");
        const RuntimeCellPoint probes[] = {origin, {origin.x-1,origin.y-1},
            {origin.x+spec.w-1,origin.y-1}, {origin.x-1,origin.y+spec.h-1},
            {origin.x+spec.w-1,origin.y+spec.h-1}};
        for (const auto p : probes) if (!runtimeBuildingPlacementInRange(s,item,p)
            || !runtimeCellCanPlaceSidebarBuilding(s,item,p)) return fail("pre-death legal corner/range");
        if (!runtimeCellCanPlaceSidebarBuilding(s,item,positive)) return fail("legal nearby positive");
        auto* source = addRuntimeObjectInstanceForOwner(s,"Neutral",RuntimeBuildKind::Building,spec.type,origin);
        if (!source) return fail("original source"); const int sourceId = source->instanceId;
        if (runtimeStructureFootprintForObject(s,*source).width!=spec.w
            || runtimeStructureFootprintForObject(s,*source).height!=spec.h) return fail("source footprint cache");
        // Expected original rectangle is independent of the new rubble-set helper.
        std::unordered_set<int> rectangle;
        for (int dy=0;dy<spec.h;++dy) for (int dx=0;dx<spec.w;++dx)
            rectangle.insert(runtimeCellKey(origin.x+dx,origin.y+dy));
        const auto observe = [&]() {
            const auto* a=findRuntimeObjectByIdIncludingInactive(s,sourceId);
            if (!a) return false;
            actors << scene << ',' << stage << ',' << s.tick << ',' << a->instanceId << ',' << a->type << ','
                << a->x << ',' << a->y << ',' << a->alive << ',' << a->health << ',' << a->rubble << ','
                << s.buildingCollapses.size() << ",1,1\n"; actors.flush(); return true;
        };
        // C++20 dependent expressions preserve the edition's existing committedCellsOverride parameter.
        const auto fastReuse = [&](const auto& st,const auto& occ,const RuntimeCellPoint p,const auto& rubble) {
            if constexpr (requires { runtimeCellCanPlaceBuildingFast(st,item,p,occ,st.economy.owner,-1,nullptr,&rubble); })
                return runtimeCellCanPlaceBuildingFast(st,item,p,occ,st.economy.owner,-1,nullptr,&rubble);
            else return runtimeCellCanPlaceBuildingFast(st,item,p,occ,st.economy.owner,-1,&rubble);
        };
        const auto wrapperReuse = [&](const auto& st,const auto* occ,const RuntimeCellPoint p,const auto& rubble) {
            if constexpr (requires { runtimeCellCanPlaceBuilding(st,item,p,-1,occ,st.economy.owner,nullptr,&rubble); })
                return runtimeCellCanPlaceBuilding(st,item,p,-1,occ,st.economy.owner,nullptr,&rubble);
            else return runtimeCellCanPlaceBuilding(st,item,p,-1,occ,st.economy.owner,&rubble);
        };
        const auto check = [&](const RuntimeCellPoint p,bool blocked,bool expectRubble) {
            const auto rubble = runtimeBuildingRubblePlacementCells(s);
            if (rubble != (expectRubble ? rectangle : std::unordered_set<int>{})) return fail("exact rubble rectangle");
            const auto occupied = buildRuntimeOccupancyLookup(s);
            invalidateRuntimeTickOccupancyCache(s);
            const auto& nav = runtimeTickOccupancyLookup(s);
            if (expectRubble) for (int key:rectangle) if (nav.contains(key)) return fail("navigation map unexpectedly gained rubble");
            const bool a=runtimeCellCanPlaceBuildingFast(s,item,p,occupied,s.economy.owner);
            const bool b=fastReuse(s,occupied,p,rubble), c=runtimeCellCanPlaceBuilding(s,item,p);
            const bool d=wrapperReuse(s,static_cast<const std::unordered_map<int,int>*>(nullptr),p,rubble);
            const bool e=runtimeCellCanPlaceBuilding(s,item,p,-1,&nav,s.economy.owner);
            const bool f=wrapperReuse(s,&nav,p,rubble);
            const bool g=runtimeCellCanPlaceSidebarBuilding(s,item,p,&nav,s.economy.owner,&rubble);
            const bool range=runtimeBuildingPlacementInRange(s,item,p);
            s.economy.placementCell=p;
            const bool preview=runtimePlacementPreviewValid(s);
            const auto cells=runtimePlacementCellChecks(s,item,p);
            if (cells.size()!=4 || !range) return fail("full proposed footprint/base-range");
            int intersecting=0;
            for (const auto& cell:cells) {
                const bool inside=rectangle.contains(runtimeCellKey(cell.cell.x,cell.cell.y));
                if (inside) ++intersecting;
                if (cell.valid != !(blocked && inside)
                    || (blocked && inside && cell.reason!="occupied")) return fail("preview cell/reason differs original rectangle");
            }
            queries << scene << ',' << stage << ',' << s.tick << ',' << p.x << ',' << p.y << ',' << blocked << ','
                << range << ',' << preview << ',' << a << ',' << b << ',' << c << ',' << d << ',' << e << ','
                << f << ',' << g << ',' << rubble.size() << '\n'; queries.flush();
            if (!intersecting || a!=!blocked || b!=a || c!=a || d!=a || e!=a || f!=a || g!=a || preview!=a)
                return fail("default/reused/navigation/preview mismatch");
            return true;
        };
        if (!stageRuntimeReadyPlacementForProbe(s,item)) return fail("original staged ready lane");
        stage="live"; if (!observe() || !check(origin,true,false)) return false;
        stage="actual-exit";
        RuntimeObjectExitRequest exit; exit.objectId=sourceId; exit.kind=spec.exit; exit.tick=s.tick;
        exit.suppressCrew=true; exit.suppressDeathWeapon=true;
        if (!finalizeRuntimeObjectExit(s,exit).accepted) return fail("original exit rejected");
        source=findMutableObjectById(s,sourceId);
        const auto* dead=findRuntimeObjectByIdIncludingInactive(s,sourceId);
        if (!dead || dead->alive || dead->rubble!=spec.rubble || dead->x!=origin.x || dead->y!=origin.y
            || (spec.exit==RuntimeObjectExitKind::Destroyed && dead->health!=0)) return fail("actual exit authority");
        if (!observe()) return fail("retained source authority");
        const auto attempt = [&](const RuntimeCellPoint p,bool accept) {
            const auto before=runtimeSelectedPlacement(s);
            if (!before || !before->item) return fail("ready selection absent");
            const auto serial=before->order.serial;
            const auto count=s.world.getObjects().size(); const int credits=s.economy.credits;
            const auto hash=runtimeProductionLanesStateHash(s.economy.localProductionLanes);
            const bool actual=placeRuntimeReadyBuilding(s,p);
            const auto after=runtimeSelectedPlacement(s);
            const auto nextHash=runtimeProductionLanesStateHash(s.economy.localProductionLanes);
            commits << scene << ',' << stage << ',' << p.x << ',' << p.y << ',' << actual << ',' << count << ','
                << s.world.getObjects().size() << ',' << credits << ',' << s.economy.credits << ',' << serial << ','
                << (after?after->order.serial:0) << ',' << hash << ',' << nextHash << '\n'; commits.flush();
            if (actual!=accept || s.economy.credits!=credits) return fail("actual placement/debit");
            if (!accept) {
                if (s.world.getObjects().size()!=count || nextHash!=hash || !after || after->order.serial!=serial
                    || !after->item || after->item->typeId!=item.typeId) return fail("blocked commit consumed ready order");
                ++rejected;
            } else {
                const auto& placedObject=s.world.getObjects().back();
                if (s.world.getObjects().size()!=count+1 || after || placedObject.type!=item.typeId
                    || !placedObject.alive || placedObject.x!=p.x || placedObject.y!=p.y
                    || runtimeStructureFootprintForObject(s,placedObject).width!=2
                    || runtimeStructureFootprintForObject(s,placedObject).height!=2 || nextHash==hash)
                    return fail("ready commit original building/consumption");
                ++placed;
            }
            return true;
        };
        if (spec.rubble && !attempt(origin,false)) return false;
        int collapseTicks=0;
        stage="collapse-completed";
        while (!s.buildingCollapses.empty() && collapseTicks<30*60) {
            ++s.tick; ++collapseTicks; advanceRuntimeEffects(s);
        }
        if (!s.buildingCollapses.empty()) return fail("original collapse deadline");
        if (std::string(spec.type)=="CAWASH01" && collapseTicks<=0) return fail("original timed collapse missing");
        if (!observe()) return fail("source disappeared after effects");
        for (const auto p:probes) if (!check(p,spec.rubble,spec.rubble)) return false;
        if (spec.rubble) {
            if (!attempt(origin,false)) return false;
            const auto disjoint = [&](RuntimeCellPoint p,int w,int h) {
                for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(rectangle.contains(runtimeCellKey(p.x+x,p.y+y)))return false;
                return true;
            };
            const auto logSearch = [&](const char* name,std::optional<RuntimeCellPoint> result,bool mcv) {
                if (!result) return fail(std::string(name)+" no legal result");
                const RuntimeStructureFootprint fp=mcv?runtimeStructureFootprintForBuildItem(s,RuntimeBuildCatalogItem{RuntimeBuildKind::Building,"YACNST"}):targetFootprint;
                const RuntimeCellPoint topLeft=mcv?runtimePlacementTopLeftForAnchor(*result,fp):*result;
                const bool apart=disjoint(topLeft,fp.width,fp.height);
                searches << scene << ',' << name << ',' << result->x << ',' << result->y << ",1," << apart << '\n';
                if (!apart) return fail(std::string(name)+" selected original rubble footprint");
                return true;
            };
            const auto nearBase=findRuntimePlacementCellNearBase(s,item);
            if (!nearBase || !runtimeCellCanPlaceSidebarBuilding(s,item,*nearBase)
                || !logSearch("near-base",nearBase,false)) return fail("near-base legal result"); ++searchCount;
            const auto nearPlacement20=findRuntimePlacementCellNear(s,item,origin,8,s.economy.owner);
            if (!nearPlacement20 || !runtimeCellCanPlaceBuilding(s,item,*nearPlacement20) || !logSearch("near",nearPlacement20,false)) return fail("near legal result"); ++searchCount;
            auto* mcv=addRuntimeObjectInstanceForOwner(s,s.economy.owner,RuntimeBuildKind::Unit,"PCV",{origin.x+1,origin.y+spec.h});
            if (!mcv) return fail("original deploy-search MCV"); const int mcvId=mcv->instanceId;
            RuntimeBuildCatalogItem yard; yard.kind=RuntimeBuildKind::Building; yard.typeId="YACNST";
            const auto result=findRuntimeMcvDeployCell(s,*mcv,yard);
            const auto yardFootprint=runtimeStructureFootprintForBuildItem(s,yard);
            if (yardFootprint.width!=4 || yardFootprint.height!=4 || !result
                || !runtimeCellCanPlaceBuilding(s,yard,runtimePlacementTopLeftForAnchor(*result,yardFootprint),mcvId)
                || !logSearch("mcv",result,true)) return fail("MCV center search legal result"); ++searchCount;
        }
        const RuntimeCellPoint successCell=spec.rubble?positive:origin;
        stage=spec.rubble?"neighbor-ready-positive":"released-foundation-positive"; s.economy.placementCell=successCell;
        if (!runtimeCellCanPlaceSidebarBuilding(s,item,successCell) || !runtimePlacementPreviewValid(s)) return fail("actual positive placement lost");
        if (completed==0) {
            const auto capture=[&](const char* name,RuntimeCellPoint cell) {
                s.economy.placementCell=cell;
                auto rendered=drawRuntimeStateOffscreenAtCell(s,{33,33},960,720,error,
                    (output/name).generic_string(),false,true,-1,nullptr,false,nullptr,false,false);
                if (!rendered) return false; ++captures; return true;
            };
            if (!capture("rubble-preview.bmp",origin) || !capture("neighbor-preview.bmp",positive)) return false;
        }
        if (!attempt(successCell,true)) return false;
        dead=findRuntimeObjectByIdIncludingInactive(s,sourceId);
        if (!dead || dead->alive || dead->rubble!=spec.rubble || dead->x!=origin.x || dead->y!=origin.y)
            return fail("placement altered original inactive source");
        cases << scene << ",30," << spec.type << ',' << spec.exitName << ',' << spec.w << ',' << spec.h << ','
            << spec.rubble << ',' << collapseTicks << ",PASS\n"; cases.flush(); ++completed;
    }
    if (completed!=5 || rejected!=4 || placed!=5 || searchCount!=6 || captures!=2) return fail("finite counters");
    std::cout << "Rubble placement issue20: cases=5 hz=30 blockedCommits=4 placed=5 searches=6 BMP=2 "
        << "originalFootprints/default/reused/navOverride/preview/readyConsumption/collapse/nonrubble passed\n";
    return true;
}
