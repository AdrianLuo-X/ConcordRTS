// Issue 21: a finished original ruin remains visible underneath a real mobile body.
// Reference worlds only filter their own object copies; live rubble/navigation are unchanged.
bool runRuntimeRubbleUnitForegroundIssue21Smoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene, stage;
    const auto fail=[&](const std::string& why){error="Rubble foreground "+scene+" ["+stage+"]: "+why;return false;};
    const char* outputValue=std::getenv("CONCORD_RUBBLE_UNIT_FOREGROUND_ISSUE21_OUTPUT");
    if(!outputValue||!*outputValue)return fail("output directory required");
    const std::filesystem::path output(outputValue); std::filesystem::create_directories(output);
    std::ofstream rows(output/"cases.csv"), frames(output/"frames.csv"), states(output/"states.csv"), sources(output/"sources.csv");
    if(!rows||!frames||!states||!sources)return fail("CSV open");
    rows<<"case,hz,ruin_type,unit_type,unit_pixels,ruin_pixels,conflicting_overlap,exposed_ruin,unit_mismatches,warm_mismatches,move_ticks,result\n";
    frames<<"case,phase,tick,width,height,camera_x,camera_y,rgb_hash,structures,shp_units,voxel_bodies,voxel_turrets,shadows,shadow_pixels,missing_structures,missing_shp,missing_voxel\n";
    states<<"case,phase,tick,ruin_id,ruin_x,ruin_y,ruin_alive,ruin_hp,rubble,unit_id,unit_x,unit_y,unit_hp,unit_active,unit_slot,collapse_count\n";
    sources<<"case,phase,kind,image,source,vxl_bytes,hva_bytes,source_fingerprint\n";
    auto assets=std::make_unique<RuntimeWindowState>(); assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("original resources unavailable");
    assets->rulesLoaded=assets->report.rulesLoaded;assets->artLoaded=assets->report.artLoaded;assets->theaterLoaded=assets->report.theaterLoaded;
    TerrainCell clear;clear.tileIndex=0;clear.subTileIndex=0;clear.level=0;
    const auto metadata=runtimeTerrainMetadataContext(*assets);
    if(resolveRuntimeTerrainSubTileLandType(clear,metadata,assets->renderCache)!=RuntimeTerrainLandType::Clear
        ||resolveRuntimeTerrainSubTileRampType(clear,metadata,assets->renderCache)!=0)return fail("original flat Clear0 TMP");
    std::ofstream note(output/"fixture.txt");
    note<<"rules="<<assets->report.rulesPath<<"\noriginalTMP=Clear0/sub0/ramp0\nhz=30\n"
        <<"realDestroyed=finalizeRuntimeObjectExit with explicit crew/death-weapon isolation\n"
        <<"actualMove=issueRuntimeMoveCommand and advanceRuntimeSimulation\n"
        <<"pixelOracle=unit-only minus blank; all contexts shadows=false, no selection/UI/ambient actors\n"
        <<"referenceCopies=filter copied object lists only; live rubble and navigation unchanged\n"
        <<"save=not tested; live-building ordering remains existing separate regression\n";
    constexpr int canvasWidth=960,canvasHeight=720;
    const RECT viewport{0,0,canvasWidth,canvasHeight};
    struct Issue21Surface {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ oldBitmap=nullptr;std::uint32_t* pixels=nullptr;
        Issue21Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=canvasWidth;info.bmiHeader.biHeight=-canvasHeight;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            dc=CreateCompatibleDC(nullptr);void* raw=nullptr;if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
            pixels=static_cast<std::uint32_t*>(raw);if(bitmap&&pixels)oldBitmap=SelectObject(dc,bitmap);}
        ~Issue21Surface(){if(oldBitmap&&dc)SelectObject(dc,oldBitmap);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } surface;
    if(!surface.dc||!surface.bitmap||!surface.pixels)return fail("actual 32bit DIB allocation");
    const auto rgbHash=[](const std::vector<std::uint32_t>& pixels){std::uint64_t hash=14695981039346656037ULL;
        for(auto pixel:pixels)for(int shift:{0,8,16}){hash^=(pixel>>shift)&255U;hash*=1099511628211ULL;}return hash;};
    const auto bodyKey=[](const WorldObjectInstance& a){std::ostringstream out;out<<std::setprecision(17)
        <<a.instanceId<<'|'<<a.type<<'|'<<a.owner<<'|'<<a.x<<'|'<<a.y<<'|'<<a.cell<<'|'<<a.alive<<'|'<<a.health
        <<'|'<<a.rubble<<'|'<<a.subCell<<'|'<<a.hasRenderPosition<<'|'<<a.renderX<<'|'<<a.renderY
        <<'|'<<a.renderLevel<<'|'<<a.renderMoveActive<<'|'<<a.level<<'|'<<a.onBridge<<'|'<<a.mission;return out.str();};
    int cases=0,captures=0,draws=0;
    for(const char* ruinType:{"CAWASH01","CAHSE01"})for(const char* unitType:{"E1","MTNK"}){
        scene=std::string(ruinType)+"-"+unitType;stage="setup";
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;seedRuntimeLoadScratch(s,*assets,false);
        s.config=config;s.config.fixedTickHz=30;s.config.playerCountry="Americans";s.config.playerSlots.clear();
        s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.superweapons=s.config.shortGame=false;
        s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.loaded=s.triggers.bootstrapComplete=true;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.mapFacts={};s.mapFactsLoaded=false;s.terrainByCell.clear();s.terrainInfoByCell.clear();
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){TerrainCell t=clear;t.x=x;t.y=y;t.cellIndex512=y*512+x;s.world.accessTerrainCells().push_back(t);
            MapCellFacts fact;fact.x=x;fact.y=y;fact.cellIndex=t.cellIndex512;fact.hasTerrain=true;
            fact.terrain.x=x;fact.terrain.y=y;fact.terrain.tileIndex=t.tileIndex;fact.terrain.subTileIndex=t.subTileIndex;
            fact.terrain.level=fact.heightLevel=t.level;fact.terrainClass="clear";
            s.mapFacts.cellIndexLookup.emplace(fact.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(fact);}
        s.mapFacts.terrainCellCount=s.mapFacts.cells.size();s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();s.mapFactsLoaded=true;
        RuntimeHumanHouseAuthority local;local.owner="Americans";local.canonicalOwner="americans";local.localFacade=true;
        s.humanHouseAuthorities={local};s.ownerRemapColors["Americans"]=RGB(235,210,45);
        if(!addRuntimeObjectInstanceForOwner(s,s.economy.owner,RuntimeBuildKind::Building,"GACNST",{8,8}))return fail("original isolated base");
        auto* ruin=addRuntimeObjectInstanceForOwner(s,"Neutral",RuntimeBuildKind::Building,ruinType,{32,32});
        if(!ruin)return fail("original ruin source");const int ruinId=ruin->instanceId;
        initializeRuntimeNavigation(s);RuntimeSnapshotActiveStateScope active(s);
        const auto* rule=s.rules.findBuildingType(ruinType);const auto* art=s.art.findArtType(ruinType);
        const std::string expectedFoundation=std::string(ruinType)=="CAWASH01"?"4x4":"3x2";
        if(!rule||!rule->leaveRubble||!art||art->foundation!=expectedFoundation)return fail("original LeaveRubble/foundation");
        RuntimeObjectExitRequest exit;exit.objectId=ruinId;exit.kind=RuntimeObjectExitKind::Destroyed;
        exit.tick=s.tick;exit.suppressCrew=true;exit.suppressDeathWeapon=true;
        if(!finalizeRuntimeObjectExit(s,exit).accepted)return fail("actual original destruction");
        int collapseTicks=0;while(!s.buildingCollapses.empty()&&collapseTicks<1800){++s.tick;++collapseTicks;advanceRuntimeEffects(s);}
        if(!s.buildingCollapses.empty()||(std::string(ruinType)=="CAWASH01"&&collapseTicks==0))return fail("natural original collapse completion");
        const auto* dead=findRuntimeObjectByIdIncludingInactive(s,ruinId);
        if(!dead||dead->alive||dead->health!=0||!dead->rubble)return fail("finished real rubble authority");
        auto* unit=addRuntimeObjectInstanceForOwner(s,s.economy.owner,std::string(unitType)=="E1"?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit,unitType,{33,29});
        if(!unit)return fail("original mobile");const int unitId=unit->instanceId;
        const RuntimeCellPoint destination{33,33};
        const auto occupied=buildRuntimeOccupancyLookup(s,unitId);
        if(!runtimeCellIsPassable(s,s.terrainByCell,occupied,destination,*unit))return fail("original navigation must allow ground through dead rubble");
        s.selectedObjectIds={unitId};stage="actual-move";issueRuntimeMoveCommand(s,destination);
        int movedTicks=0;bool arrived=false;
        for(;movedTicks<30*30;++movedTicks){++s.tick;advanceRuntimeSimulation(s);unit=findMutableObjectById(s,unitId);
            if(!unit||!unit->alive)return fail("moving original unit lost");
            if(runtimeCellPointForObject(*unit)==destination&&!unit->renderMoveActive
                &&(!s.behaviors.contains(unitId)||s.behaviors.at(unitId).moveCooldownTicks==0)){arrived=true;++movedTicks;break;}}
        if(!arrived)return fail("native Move arrival deadline");s.selectedObjectIds.clear();
        dead=findRuntimeObjectByIdIncludingInactive(s,ruinId);unit=findMutableObjectById(s,unitId);
        if(!dead||dead->alive||!dead->rubble||dead->health!=0||!s.buildingCollapses.empty())return fail("real finished ruin changed during Move");
        const auto ruinAuthority=bodyKey(*dead),unitAuthority=bodyKey(*unit);const auto objectCount=s.world.getObjects().size();const int frameTick=s.tick;
        const auto recordState=[&](const char* phase){dead=findRuntimeObjectByIdIncludingInactive(s,ruinId);unit=findMutableObjectById(s,unitId);
            if(!dead||!unit)return false;states<<scene<<','<<phase<<','<<s.tick<<','<<ruinId<<','<<dead->x<<','<<dead->y<<','
                <<dead->alive<<','<<dead->health<<','<<dead->rubble<<','<<unitId<<','<<unit->x<<','<<unit->y<<','<<unit->health
                <<','<<unit->renderMoveActive<<','<<unit->subCell<<','<<s.buildingCollapses.size()<<'\n';states.flush();return true;};
        if(!recordState("before-render"))return fail("state records");
        stage="body-pixel-oracle";
        RuntimeMapRenderContext view=runtimeTerrainMetadataContext(s);view.rules=&s.rules;view.art=&s.art;view.theater=&s.theater;
        view.mapFacts=&s.mapFacts;view.assetRootPath=runtimeAssetRootPath(s.config);view.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
        view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);view.palettePath=runtimePalettePath(s.config,s.report.map);
        view.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);view.ownerRemapColors=&s.ownerRemapColors;
        view.animationTick=s.tick;view.fixedTickHz=30;view.shadowsEnabled=false;view.selectedObjectIds=nullptr;view.vision=nullptr;
        view.surfacePixels=surface.pixels;view.surfaceWidth=canvasWidth;view.surfaceHeight=canvasHeight;
        RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=60;camera.tileHeight=30;
        camera.worldOffsetX=-canvasWidth/2;camera.worldOffsetY=33*30-canvasHeight/2;
        const auto shadowFree=[](const RuntimeMapRenderStats& t){return t.shadowsDrawn==0&&t.shadowPixelsDarkened==0
            &&t.shpShadowsDrawn==0&&t.voxelShadowsDrawn==0&&t.structureShadowsDrawn==0&&t.unitShadowsDrawn==0
            &&t.infantryShadowsDrawn==0&&t.buildingAnimationShadowsDrawn==0&&t.projectileShadowsDrawn==0;};
        const auto referenceWorld=[&](bool includeRuin,bool includeUnit){GameWorld copy=s.world;auto& objects=copy.accessObjects();
            objects.erase(std::remove_if(objects.begin(),objects.end(),[&](const auto& a){return(!includeRuin&&a.instanceId==ruinId)
                ||(!includeUnit&&a.instanceId==unitId);}),objects.end());return copy;};
        auto blankWorld=referenceWorld(false,false),unitWorld=referenceWorld(false,true),
            ruinWorld=referenceWorld(true,false),combinedWorld=referenceWorld(true,true);
        const auto paint=[&](const char* phase,bool includeRuin,bool includeUnit,GameWorld& reference,RuntimeMapRenderCache& cache){
            auto drawCamera=camera;std::fill(surface.pixels,surface.pixels+canvasWidth*canvasHeight,0x00242c30U);
            const auto stats=drawRuntimeMap(surface.dc,viewport,reference,view,drawCamera,cache);GdiFlush();++draws;
            if(!stats.rendered||!shadowFree(stats)||stats.missingStructureSprites||stats.missingObjectShpSprites||stats.missingVoxelProxies)
                throw std::runtime_error(std::string(phase)+" original draw/shadows/missing source");
            if(includeRuin&&stats.visibleStructureSprites<=0)throw std::runtime_error("original ruin not drawn");
            if(includeUnit&&((std::string(unitType)=="E1"&&stats.visibleObjectShpSprites<=0)
                ||(std::string(unitType)=="MTNK"&&(stats.visibleVoxelBodies<=0||stats.visibleVoxelTurrets<=0))))
                throw std::runtime_error("original mobile body/turret not drawn");
            std::vector<std::uint32_t> pixels(surface.pixels,surface.pixels+canvasWidth*canvasHeight);
            frames<<scene<<','<<phase<<','<<s.tick<<','<<canvasWidth<<','<<canvasHeight<<','<<camera.worldOffsetX<<','<<camera.worldOffsetY
                <<','<<rgbHash(pixels)<<','<<stats.visibleStructureSprites<<','<<stats.visibleObjectShpSprites<<','<<stats.visibleVoxelBodies
                <<','<<stats.visibleVoxelTurrets<<','<<stats.shadowsDrawn<<','<<stats.shadowPixelsDarkened<<','<<stats.missingStructureSprites
                <<','<<stats.missingObjectShpSprites<<','<<stats.missingVoxelProxies<<'\n';frames.flush();
            if(std::string(phase)=="unit-only"||std::string(phase)=="ruin-only"){
                for(const auto& [key,value]:cache.objectShpBytesByImage)if(value.loaded)
                    sources<<scene<<','<<phase<<",SHP,"<<value.resolvedImageId<<','<<std::quoted(value.sourcePath)<<",0,0,"<<value.sourceFingerprint<<'\n';
                for(const auto& [key,value]:cache.voxelSourcesByImage)if(value.loaded)
                    sources<<scene<<','<<phase<<",VXL,"<<value.metadata.imageId<<','<<std::quoted(value.metadata.vxlPath+'|'+value.metadata.hvaPath)
                        <<','<<value.vxlBytes.size()<<','<<value.hvaBytes.size()<<",0\n";
                sources.flush();
            }
            return pixels;
        };
        auto blankCache=std::make_unique<RuntimeMapRenderCache>(),unitCache=std::make_unique<RuntimeMapRenderCache>(),
            ruinCache=std::make_unique<RuntimeMapRenderCache>(),combinedCache=std::make_unique<RuntimeMapRenderCache>();
        std::vector<std::uint32_t> blank,unitOnly,ruinOnly,combined,warm;
        if(std::string(unitType)=="MTNK"&&!runtimeGroundVehicleUsesBuildingDepth(*unit,combinedWorld,view,*unitCache))
            return fail("actual flat original TMP/body depth qualification");
        try{blank=paint("blank",false,false,blankWorld,*blankCache);unitOnly=paint("unit-only",false,true,unitWorld,*unitCache);
            ruinOnly=paint("ruin-only",true,false,ruinWorld,*ruinCache);combined=paint("combined-cold",true,true,combinedWorld,*combinedCache);
            warm=paint("combined-warm",true,true,combinedWorld,*combinedCache);}
        catch(const std::exception& detail){return fail(detail.what());}
        std::uint64_t unitPixels=0,ruinPixels=0,overlapPixels=0,exposedPixels=0,bodyMismatch=0,warmMismatch=0;
        for(std::size_t p=0;p<blank.size();++p){const auto b=blank[p]&0xffffffU,u=unitOnly[p]&0xffffffU,r=ruinOnly[p]&0xffffffU,c=combined[p]&0xffffffU;
            const bool isUnit=u!=b,isRuin=r!=b;unitPixels+=isUnit;ruinPixels+=isRuin;
            if(isUnit){bodyMismatch+=c!=u;if(isRuin&&u!=r)++overlapPixels;}
            else if(isRuin&&c==r)++exposedPixels;
            warmMismatch+=(warm[p]&0xffffffU)!=c;}
        rows<<scene<<",30,"<<ruinType<<','<<unitType<<','<<unitPixels<<','<<ruinPixels<<','<<overlapPixels<<','<<exposedPixels
            <<','<<bodyMismatch<<','<<warmMismatch<<','<<movedTicks<<','<<(!unitPixels||!ruinPixels||!overlapPixels||!exposedPixels||bodyMismatch||warmMismatch?"FAIL":"PASS")<<'\n';rows.flush();
        if(!unitPixels||!ruinPixels||!overlapPixels||!exposedPixels)return fail("non-vacuous original unit/ruin/conflicting overlap/exposed witness");
        if(bodyMismatch||warmMismatch)return fail("unit pixels="+std::to_string(bodyMismatch)+" cold/warm="+std::to_string(warmMismatch));
        dead=findRuntimeObjectByIdIncludingInactive(s,ruinId);unit=findMutableObjectById(s,unitId);
        if(!dead||!unit||bodyKey(*dead)!=ruinAuthority||bodyKey(*unit)!=unitAuthority||s.tick!=frameTick
            ||s.world.getObjects().size()!=objectCount||!s.buildingCollapses.empty()||!recordState("after-render"))return fail("draw changed actual authority");
        std::vector<std::uint32_t> sheet(canvasWidth*3*canvasHeight);
        for(int y=0;y<canvasHeight;++y){std::copy_n(combined.begin()+y*canvasWidth,canvasWidth,sheet.begin()+y*canvasWidth*3);
            std::copy_n(unitOnly.begin()+y*canvasWidth,canvasWidth,sheet.begin()+y*canvasWidth*3+canvasWidth);
            std::copy_n(ruinOnly.begin()+y*canvasWidth,canvasWidth,sheet.begin()+y*canvasWidth*3+canvasWidth*2);}
        if(!writeRuntimeSmokeBmp((output/(scene+"-combined-unit-ruin.bmp")).generic_string(),sheet.data(),canvasWidth*3,canvasHeight,error))return false;
        ++captures;++cases;
    }
    if(cases!=4||captures!=4||draws!=20)return fail("finite counters");
    std::cout<<"Rubble foreground issue21: cases=4 hz=30 actualMoves=4 draws=20 BMP=4 originalSHP/VXL "
        <<"conflictingOverlap/unitPixels/exposedRuin/coldWarm/authority/shadowFree passed\n";return true;
}
