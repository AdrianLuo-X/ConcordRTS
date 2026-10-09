// Issue25.13: original NASAM pedestal, shared original SAM raster and real AA.
// This is an anchor-registration oracle, not an independent VXL rasterizer.
// The upper vertex is the project's 1x1 foundation coordinate contract,
// supported by the supplied reference and independently decoded body/MK art.
#pragma optimize("", off)
bool runRuntimePatriotTurretIssue25Smoke(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeBuildingDepthOracle;
    std::cout<<std::unitbuf;
    const char* requested=std::getenv("CONCORD_PATRIOT_TURRET_OUTPUT");
    const std::filesystem::path root=requested&&*requested?requested:"build/smoke/feature-set-25/13-patriot-turret";
    std::filesystem::create_directories(root);std::string label;
    const auto fail=[&](const std::string& why){error="Issue25 Patriot "+label+": "+why;std::cout<<error<<'\n';return false;};
    try {
        auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
        assets->config.playerSlots.clear();assets->config.gameModeRulesOverride.clear();
        assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
        if(!assets->report.success)return fail("original startup: "+assets->report.errorMessage);
        assets->rulesLoaded=assets->artLoaded=assets->theaterLoaded=true;
        if(toLowerAsciiLocal(assets->report.map.theater)!="temperate")return fail("audited original temperate assets required");
        for(const char* name:{"CONCORD_VXL_PROJ_SCREEN_X","CONCORD_VXL_PROJ_GROUND_Y","CONCORD_VXL_PROJ_HEIGHT_Y",
            "CONCORD_VXL_SCREEN_X_SCALE_MUL","CONCORD_VXL_SCREEN_Y_SCALE_MUL","CONCORD_VXL_HEIGHT_Y_SCALE_MUL",
            "CONCORD_VXL_TURRET_PIXEL_OFFSET_X","CONCORD_VXL_TURRET_PIXEL_OFFSET_Y","CONCORD_VXL_DEBUG"})
            if(std::getenv(name))return fail(std::string("original unmodified raster configuration required: ")+name);
        const auto nativeRoot=std::filesystem::current_path()/"local_data/resources/mix_exports";
        const auto rawRules=readPinned(nativeRoot/"ra2md/localmd/rulesmd.ini",
            "06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05");
        const auto rawArt=readPinned(nativeRoot/"ra2md/localmd/artmd.ini",
            "e1f0378394313c04ebbd5073f47785ee3e46f1b3c62d65724e8f3c310ee7ba31");
        const auto palette=readPinned(nativeRoot/"extracted_palettes/cache_unittem.pal",
            "ed785e62eed291480f3198dd44f6b656ebe3a9b75e9f641944d710abc6bde3e3");
        IniFile ri,ai;if(!ri.loadFromString(std::string(rawRules.begin(),rawRules.end()))
            ||!ai.loadFromString(std::string(rawArt.begin(),rawArt.end()))||palette.size()!=768)return fail("original INI/palette");
        const auto yes=[](const std::string& value){const auto s=toLowerAsciiLocal(trimAsciiLocal(value));return s=="yes"||s=="true";};
        const auto* building=assets->rules.findBuildingType("NASAM");const auto* art=assets->art.findArtType("NASAM");
        const auto* weapon=assets->rules.findWeaponType("RedEye2");const auto* projectile=assets->rules.findProjectileType("AAHeatSeeker");
        const auto* targetRule=assets->rules.findUnitType("UnitType","DISK");
        if(!building||!art||!weapon||!projectile||!targetRule||building->strength!=900||building->rot!=8||!building->powered
            ||building->primaryWeapon!="RedEye2"||building->turretAnim!="SAM"||!building->turretAnimIsVoxel
            ||building->turretAnimX||building->turretAnimY||building->turretAnimZAdjust!=-20||art->foundation!="1x1"
            ||ri.getValue("NASAM","Strength")!="900"||ri.getValue("NASAM","ROT")!="8"||!yes(ri.getValue("NASAM","Powered"))
            ||ri.getValue("NASAM","TurretAnim")!="SAM"||!yes(ri.getValue("NASAM","TurretAnimIsVoxel"))
            ||!ri.getValue("NASAM","TurretAnimX").empty()||!ri.getValue("NASAM","TurretAnimY").empty()
            ||ri.getValue("NASAM","TurretAnimZAdjust")!="-20"||ai.getValue("NASAM","Foundation")!="1x1"
            ||ai.getValue("NASAM","Buildup")!="NASAMMK"||ai.getValue("NASAM","PrimaryFireFLH")!="90,50,100"
            ||weapon->damage!=75||weapon->rof!=55||weapon->range!=12||weapon->projectile!="AAHeatSeeker"||weapon->warhead!="SAMWH"
            ||ri.getValue("RedEye2","Damage")!="75"||ri.getValue("RedEye2","ROF")!="55"||ri.getValue("RedEye2","Range")!="12"
            ||targetRule->strength!=600||ri.getValue("DISK","Strength")!="600"||ri.getValue("DISK","JumpjetHeight")!="750")
            return fail("original Patriot/SAM/FLH/AA weapon/target input");
        const auto local=readPinned(nativeRoot/"ra2/local.mix","d748797f59a652a296d3825e1703b8781380f43eba7b3d1825477752a395ba08");
        const auto generic=readPinned(nativeRoot/"ra2/generic.mix","dbe8d76687c1787e4f17396cdf6c88fe507b916031261492fa6375fe944f2b70");
        const auto isotemp=readPinned(nativeRoot/"ra2/isotemp.mix","68a1c51c4e21ba76556d7aa460fd9fe034858f7b916769aa8166b40b9ce9c1d4");
        const auto slice=[](const Bytes& source,std::size_t at,std::size_t length,const char* hash){
            if(at>source.size()||length>source.size()-at)throw std::runtime_error("original MIX range");
            Bytes result(source.begin()+at,source.begin()+at+length);const auto actual=sha256(result);if(actual!=hash)throw std::runtime_error("original asset slice SHA at="+std::to_string(at)+" length="+std::to_string(length)+" expected="+hash+" actual="+actual);return result;};
        const auto vxl=slice(local,8596340,25161,"33ce1bf91cd968ac9b52f66686e57ca7581d5ea1e69df4620979b5fdcbb36c34");
        const auto hva=slice(local,8967812,88,"17cdede3aae9ab4115c60c7df162d68aa03eaccfd4b111661d32853e169558c8");
        const auto bodyBytes=slice(generic,13710782,4272,"c2311ddd597e80c36984c84d391aed0350307c3764c43e81f044cbc4250e69f9");
        const auto mkBytes=slice(isotemp,27375822,15216,"1a0c8692ea0b85291ccfdaa94f85c232032452411d3f058530d86f909d6e4a73");
        const auto frameAt=[](const Bytes& raw,int frame){if(frame<0||unsigned(frame)>=little(raw,6,2))throw std::runtime_error("original frame range");
            Bytes selected=raw;std::copy_n(raw.begin()+8+std::size_t(frame)*24,24,selected.begin()+8);return decodeFrameZero(selected);};
        const auto body=frameAt(bodyBytes,0),mk=frameAt(mkBytes,7);
        if(body.canvasWidth!=98||body.canvasHeight!=68||body.x!=26||body.y!=30||body.width!=44||body.height!=30
            ||mk.canvasWidth!=98||mk.canvasHeight!=68||mk.x!=26||mk.y!=4||mk.width!=44||mk.height!=56
            ||sha256(body.pixels)!="8de6f73eedc266e05e42c2e175219a93fbea590d8f82daa140ad9aa868cac825"
            ||sha256(mk.pixels)!="99253d629bdb7b0aa4ba1f863a12be3aa884473810391216ceb0333c891578bd"
            ||little(hva,16,4)!=1||little(hva,20,4)!=1)return fail("independent original body/buildup/HVA");
        int rawBaseWhite=0,mkAboveBody=0;
        std::vector<unsigned char> bodyCanvas(98*68),mkCanvas(98*68);
        for(int y=0;y<body.height;++y)for(int x=0;x<body.width;++x)bodyCanvas[(body.y+y)*98+body.x+x]=body.pixels[y*body.width+x];
        for(int y=0;y<mk.height;++y)for(int x=0;x<mk.width;++x)mkCanvas[(mk.y+y)*98+mk.x+x]=mk.pixels[y*mk.width+x];
        for(int y=0;y<68;++y)for(int x=0;x<98;++x){const int index=bodyCanvas[y*98+x];
            if(index&&!(index>=16&&index<=31)){const int r=palette[index*3]*4,g=palette[index*3+1]*4,b=palette[index*3+2]*4;
                rawBaseWhite+=r+g+b>=600&&std::max({r,g,b})-std::min({r,g,b})<=40;}
            mkAboveBody+=mkCanvas[y*98+x]&&!bodyCanvas[y*98+x]&&y<body.y;}
        if(!rawBaseWhite||!mkAboveBody)return fail("original white pedestal / complete MK head landmarks absent");
        int clearTile=-1;const auto metadata=runtimeTerrainMetadataContext(*assets);const int clearSet=assets->theater.getGeneralTileSetIndex("ClearTile");
        for(int tile=0;tile<int(assets->theater.getTotalTileSlots());++tile){TerrainCell c;c.tileIndex=tile;c.subTileIndex=0;
            if(assets->theater.resolveTileIndex(tile).tileSetIndex==clearSet
                &&resolveRuntimeTerrainSubTileLandType(c,metadata,assets->renderCache)==RuntimeTerrainLandType::Clear
                &&resolveRuntimeTerrainSubTileRampType(c,metadata,assets->renderCache)==0){clearTile=tile;break;}}
        if(clearTile<0)return fail("original clear flat TMP");
        std::ofstream sources(root/"sources.txt"),rows(root/"static.csv"),states(root/"states.csv"),shots(root/"shots.csv"),checks(root/"cases.csv"),saves(root/"checkpoints.csv");
        if(!sources||!rows||!states||!shots||!checks||!saves)return fail("evidence streams");
        sources<<"Original rules="<<sha256(rawRules)<<"\nART="<<sha256(rawArt)<<"\nSAMVXL="<<sha256(vxl)<<"\nSAMHVA="<<sha256(hva)
            <<"\nNGSAM0="<<sha256(bodyBytes)<<"\nNTSAMMK7="<<sha256(mkBytes)<<"\nunittem="<<sha256(palette)
            <<"\nOriginal independently decoded body0=1003 opaque, complete MK main7=1669 opaque. Original white pedestal="<<rawBaseWhite<<" MK head above body="<<mkAboveBody
            <<"\nSeat expectation: 1x1 foundation upper vertex centerY-tileHeight/2, project geometry/reference alignment, not a newly established retail PE constant."
            <<"\nSAM alpha is a SHARED production VXL raster pinned to original VXL/HVA; the test independently places that shape and compares actual foreground outside independently decoded SHP body mask."
            <<"\nThis is NOT independent VXL decoding or a full scene-Z/color compositor. Original palette white pedestal pixels are additional witnesses."
            <<"\nRuntime facing inputs0..224 and nearest authored bucket((facing-32+4) modulo256)/8 are the existing project model direction convention; no ROT/facing/scale/ZAdjust changes."
            <<"\n16 static cases=8facings*2zoom, 8 true AA command/fullSimulation cases with original NASAM900/DISK600/RedEye2."
            <<"\nRestored whole DIB comparisons isolate the NASAM building and do not prove projectile screen-pixel equality. Four projectile presentation fields are excluded by the formal authority contract; all physical fields remain exact. Actual original FLH worldXYZ are independently checked. Corrected screen offsets are recorded without assuming they equal the seat correction, because original fitted model-endpoint corrections also apply."
            <<"\nTwo unique physical files: full ordinary14 and canonical extension, three reopen actions. Canonical raw authority exact; ordinary unused inactive cache XYZ only is normalized in private copies."
            <<"\nAA input is an original map/script DISK airborne pose; normal full simulation establishes its original750lepton hover. No artificial HP, damage, speed or firing clock."
            <<"\nClearTile="<<clearTile<<" native MapFacts cellIndex=y*512+x; runtime navigation uses1000.\n";sources.flush();
        rows<<"case,zoom,facing,bucket,offset_x,offset_y,expected_seat_y,raster_w,raster_h,fit,outside_expected,outside_actual,mask_mismatch,old_mask_wrong,white_candidates,white_visible,repeat_equal,hdc_equal,world_equal,passed\n";
        states<<std::setprecision(17)<<"case,replica,tick,source_hp,source_facing,turret_facing,turret_clock,target_hp,target_x,target_y,target_z,target_airborne,behavior,accepted,rejected,status\n";
        shots<<std::setprecision(17)<<"case,tick,actor,source,target,weapon,warhead,damage,spawn_tick,from_x,from_y,from_z,source_screen_x,source_screen_y,target_distance,turret_facing\n";
        checks<<"case,kind,zoom,facing,real_command,first_shot_tick,Stop_held,passed\n";
        saves<<"case,format,file,tick,world_raw_equal,world_contract_equal,special_equal,projectile_equal,behavior_equal,pixels_equal,passed\n";
        constexpr int sourceId=101,powerId=102,targetId=103,width=640,height=480;
        const RECT viewport{0,0,width,height};
        struct Surface {HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
            Surface(){BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;
                bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;dc=CreateCompatibleDC(nullptr);void* raw=nullptr;
                if(dc)bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&raw,nullptr,0);pixels=static_cast<std::uint32_t*>(raw);
                if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
            ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
        } surface;
        if(!surface.dc||!surface.pixels)return fail("DIB allocation");
        const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
        const auto make=[&](){
            auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;seedRuntimeLoadScratch(s,*assets,false);
            s.config.fixedTickHz=30;s.config.playerCountry="Americans";s.config.gameMode="Battle";s.config.playerSlots.clear();
            s.config.gameModeRulesOverride.clear();s.config.noCooldown=s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.shortGame=s.config.superweapons=false;
            s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessTerrainCells().clear();s.world.accessSmudges().clear();
            s.mapFacts={};s.mapFactsLoaded=true;s.terrainByCell.clear();s.terrainInfoByCell.clear();s.structureFootprints.clear();
            s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();s.impactEffects.clear();s.jumpjetFlights.clear();s.aircraftFlights.clear();
            s.economy={};s.economy.owner="Americans";s.economy.credits=10000;s.economy.skirmishStartApplied=true;
            s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.humanHouseAuthorities.clear();s.triggers={};s.triggers.bootstrapComplete=true;
            s.initialOreFieldsSeeded=true;s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
            s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
            s.ownerRemapColors.clear();s.ownerRemapColors["americans"]=RGB(235,210,45);
            RuntimeHumanHouseAuthority owner;owner.owner=owner.country="Americans";owner.canonicalOwner="americans";owner.localFacade=true;s.humanHouseAuthorities.push_back(owner);
            for(int y=48;y<=80;++y)for(int x=48;x<=80;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=clearTile;c.subTileIndex=0;
                s.world.accessTerrainCells().push_back(c);MapCellFacts f;f.x=x;f.y=y;f.cellIndex=c.cellIndex512;f.hasTerrain=true;
                f.terrain.x=x;f.terrain.y=y;f.terrain.tileIndex=clearTile;f.terrain.subTileIndex=0;f.terrainClass="clear";f.passabilityHint="passable";
                s.mapFacts.cellIndexLookup.emplace(f.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(f);}
            const auto add=[&](int id,const char* type,int x,int y){WorldObjectInstance o;o.instanceId=id;o.type=type;o.kind=WorldObjectKind::Structure;
                o.owner="Americans";o.mapId="patriot-"+std::to_string(id);o.x=x;o.y=y;o.cell=runtimeCellKey(x,y);o.alive=o.poweredOperational=true;
                o.health=runtimeMaxHealthForObject(s,o);o.currentAmmo=-1;o.mission="Sleep";clearRuntimeRenderPosition(o);s.world.accessObjects().push_back(o);};
            add(sourceId,"NASAM",64,64);add(powerId,"GAPOWR",50,50);
            s.terrainByCell=buildRuntimeTerrainLookup(s.world);s.structureFootprints=buildRuntimeStructureFootprints(s);initializeRuntimeNavigation(s);
            initializeRuntimeLocalProductionLanes(s.economy);rebuildRuntimeEconomyPower(s);ensureRuntimeSubcellsInitialized(s);
            ensureRuntimeSpecialMechanismsInitialized(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);loadRuntimeSoundDatabase(s);
            initializeRuntimeOfflineGameplayRouting(s);step(s);
            if(!findObjectById(s,sourceId)->poweredOperational||findObjectById(s,sourceId)->health!=900)throw std::runtime_error("original operational900HP NASAM input");
            return box;};
        struct Draw {std::vector<std::uint32_t> pixels;RuntimeMapRenderStats stats;};
        const auto paint=[&](RuntimeWindowState& s,int zoom,bool blank,bool hdc=false){
            RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=zoom;camera.tileHeight=zoom/2;
            camera.worldOffsetX=-width/2;camera.worldOffsetY=128*zoom/4-height/2;
            RuntimeMapRenderContext view;view.rules=&s.rules;view.art=&s.art;view.theater=&s.theater;view.mapFacts=&s.mapFacts;
            view.assetRootPath=runtimeAssetRootPath(s.config);view.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
            view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);view.palettePath=runtimePalettePath(s.config,s.report.map);
            view.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);view.ownerRemapColors=&s.ownerRemapColors;
            view.animationTick=s.tick;view.fixedTickHz=30;view.useTileTextures=view.shadowsEnabled=false;view.seeHiddenObjects=false;
            if(!hdc){view.surfacePixels=surface.pixels;view.surfaceWidth=width;view.surfaceHeight=height;}
            GameWorld world=s.world;world.accessObjects().clear();if(!blank)world.accessObjects().push_back(*findObjectById(s,sourceId));
            Draw result;RuntimeSnapshotActiveStateScope active(s);result.stats=drawRuntimeMap(surface.dc,viewport,world,view,camera,s.renderCache);
            if(!result.stats.rendered)throw std::runtime_error("actual original render");GdiFlush();
            result.pixels.assign(surface.pixels,surface.pixels+width*height);return result;};
        int statics=0,aa=0,stops=0,ordinaryLoads=0,canonicalLoads=0,reopens=0,paired=0,bmps=0;
        std::uint64_t outsideWitness=0,whiteWitness=0,oldWitness=0;
        const auto samePixels=[](const auto& a,const auto& b){if(a.size()!=b.size())return false;for(std::size_t p=0;p<a.size();++p)
            if((a[p]&0xffffffU)!=(b[p]&0xffffffU))return false;return true;};
        const auto anchor=[&](RuntimeWindowState& s,int zoom,bool saveImages){
            const auto* object=findObjectById(s,sourceId);if(!object)return fail("Patriot absent");
            const int facing=object->turretFacing>=0?object->turretFacing:object->facing,bucket=((facing+224+4)%256)/8;
            const auto hash=s.world.deterministicSimulationStateHash();const auto blank=paint(s,zoom,true),actual=paint(s,zoom,false);
            const auto repeat=paint(s,zoom,false),hdc=paint(s,zoom,false,true);
            const RuntimeVoxelCacheEntry* raster=nullptr;const RuntimeVoxelSourceCacheEntry* bytes=nullptr;
            for(const auto& [key,v]:s.renderCache.voxelObjectsByType){if(v.loaded&&v.hasSprite&&v.facingBucket==bucket
                &&toLowerAsciiLocal(v.metadata.imageId)=="sam"&&toLowerAsciiLocal(key).find("nasam|turretanim")!=std::string::npos){
                    if(raster&&raster->image.pixels!=v.image.pixels)return fail("ambiguous shared SAM raster");raster=&v;}}
            if(!raster)return fail("real SAM raster missing");
            for(const auto& [key,v]:s.renderCache.voxelSourcesByImage)if(v.loaded&&v.metadata.vxlPath==raster->metadata.vxlPath)bytes=&v;
            if(!bytes||bytes->vxlBytes!=vxl||bytes->hvaBytes!=hva)return fail("rendered SAM original bytes differ");
            bool rawBody=false;for(const auto& [key,v]:s.renderCache.objectShpBytesByImage)if(v.loaded&&v.bytes==bodyBytes)rawBody=true;
            if(!rawBody)return fail("actual resolved original NGSAM body not pinned");
            const auto& image=raster->image;if(image.width<=0||image.height<=0||image.pixels.size()!=std::size_t(image.width)*image.height)return fail("shared SAM shape malformed");
            std::vector<unsigned char> bm(width*height),expected(width*height),old(width*height);
            const int cx=width/2,cy=height/2,bx=cx-body.canvasWidth/2+body.x,by=cy-body.canvasHeight/2+body.y;
            for(int y=0;y<body.height;++y)for(int x=0;x<body.width;++x)if(body.pixels[y*body.width+x])bm[(by+y)*width+bx+x]=1;
            // Generic voxel foot anchoring adds half a tile; upper-vertex seating cancels it.
            // No production turret-offset helper or draw statistic supplies this expectation.
            const int tx=cx-image.globalWidth/2+image.offsetX,ty=cy-image.globalHeight+image.offsetY;
            for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)if(image.pixels[y*image.width+x]){
                const int sx=tx+x,sy=ty+y;if(sx<0||sx>=width||sy<0||sy+zoom/4>=height)return fail("SAM fixture crop");
                expected[sy*width+sx]=1;old[(sy+zoom/4)*width+sx]=1;}
            int exp=0,act=0,mismatch=0,oldWrong=0,candidates=0,white=0;
            for(int p=0;p<width*height;++p)if(!bm[p]){const bool visible=(actual.pixels[p]&0xffffffU)!=(blank.pixels[p]&0xffffffU);
                exp+=expected[p]!=0;act+=visible;mismatch+=visible!=(expected[p]!=0);oldWrong+=(old[p]!=expected[p]);}
            for(int y=0;y<body.height;++y)for(int x=0;x<body.width;++x){const int index=body.pixels[y*body.width+x],p=(by+y)*width+bx+x;
                if(!index||(index>=16&&index<=31)||expected[p]||!old[p])continue;
                const int r=palette[index*3]*4,g=palette[index*3+1]*4,b=palette[index*3+2]*4;
                if(r+g+b<600||std::max({r,g,b})-std::min({r,g,b})>40)continue;++candidates;
                const std::uint32_t rgb=(r<<16)|(g<<8)|b;white+=(actual.pixels[p]&0xffffffU)==rgb;}
            const bool repeated=samePixels(actual.pixels,repeat.pixels),hdcEqual=samePixels(actual.pixels,hdc.pixels);
            const bool worldEqual=hash==s.world.deterministicSimulationStateHash();
            const bool okay=!mismatch&&exp>0&&oldWrong>0&&repeated&&hdcEqual&&worldEqual
                &&actual.stats.structureTurretAnimDrawn==1&&actual.stats.structureTurretAnimVoxel==1;
            rows<<label<<','<<zoom<<','<<facing<<','<<bucket<<','<<actual.stats.firstStructureTurretScreenOffsetX<<','
                <<actual.stats.firstStructureTurretScreenOffsetY<<','<<-zoom/4<<','<<image.width<<','<<image.height<<','<<raster->rasterFitScale<<','
                <<exp<<','<<act<<','<<mismatch<<','<<oldWrong<<','<<candidates<<','<<white<<','<<repeated<<','<<hdcEqual<<','<<worldEqual<<','<<okay<<'\n';rows.flush();
            outsideWitness+=exp;whiteWitness+=white;oldWitness+=oldWrong;
            if(saveImages||!okay){std::vector<std::uint32_t> mask(width*height,0);for(int p=0;p<width*height;++p)
                    mask[p]=bm[p]?0xe0e0e0U:expected[p]?0x40ff40U:old[p]?0xff4040U:0x181818U;
                if(!writeRuntimeSmokeBmp((root/(label+"-actual.bmp")).string(),actual.pixels.data(),width,height,error)
                    ||!writeRuntimeSmokeBmp((root/(label+"-anchor-masks.bmp")).string(),mask.data(),width,height,error))return fail("actual evidence BMP "+error);bmps+=2;}
            if(!okay)return fail("actual SAM anchor outside original body: expected="+std::to_string(exp)+" actual="+std::to_string(act)
                +" mismatches="+std::to_string(mismatch)+" offsetY="+std::to_string(actual.stats.firstStructureTurretScreenOffsetY)
                +" expectedY="+std::to_string(-zoom/4));return true;};
        const auto worldContract=[](const GameWorld& world){GameWorld copy=world;
            for(auto& o:copy.accessObjects())if(!o.hasRenderPosition&&!o.renderMoveActive){o.renderX=double(o.x);o.renderY=double(o.y);o.renderLevel=double(o.level);}
            return copy.deterministicSimulationStateHash();};
        const auto projectileAuthority=[](const RuntimeWindowState& s){std::vector<std::string> result;for(auto p:s.projectiles){
            // The formal projectile authority excludes these four presentation scalars only.
            p.sourceScreenOffsetX=p.sourceScreenOffsetY=p.sourceNativeFixedX=p.sourceNativeFixedY=0;result.push_back(serializeRuntimeProjectileEffect(p));}return result;};
        const auto persistentBehavior=[](const RuntimeObjectBehavior& v){return std::make_tuple(v.kind,v.targetObjectId,v.moveTarget.x,v.moveTarget.y,
            v.weaponCooldownTicks,v.moveCooldownTicks,v.attackBridge,v.attackBridgeCell.x,v.attackBridgeCell.y,v.pendingAllowedOccupiedObjectId,
            v.moveIntervalRemainderQ16,v.forceFire,v.attackGround,v.moveSegmentFrom.x,v.moveSegmentFrom.y,v.factoryExitClearanceHostId);};
        const auto same=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,bool canonical){
            if(a.tick!=b.tick||(canonical?a.world.deterministicSimulationStateHash()!=b.world.deterministicSimulationStateHash():worldContract(a.world)!=worldContract(b.world))
                ||a.specialMechanisms.stateHash()!=b.specialMechanisms.stateHash()||a.economy.credits!=b.economy.credits
                ||a.playerCommands.serialize()!=b.playerCommands.serialize()||projectileAuthority(a)!=projectileAuthority(b)
                ||serializeRuntimeJumpjetFlights(a.jumpjetFlights)!=serializeRuntimeJumpjetFlights(b.jumpjetFlights)||a.behaviors.size()!=b.behaviors.size())return false;
            for(const auto& [id,v]:a.behaviors){const auto q=b.behaviors.find(id);if(q==b.behaviors.end()||persistentBehavior(v)!=persistentBehavior(q->second)
                ||(canonical&&runtimeMultiplayerSnapshotBehaviorRecord(id,v)!=runtimeMultiplayerSnapshotBehaviorRecord(id,q->second)))return false;}
            for(int id:{sourceId,targetId}){const auto* x=findRuntimeObjectByIdIncludingInactive(a,id);const auto* y=findRuntimeObjectByIdIncludingInactive(b,id);
                if(!x||!y||x->health!=y->health||x->facing!=y->facing||x->turretFacing!=y->turretFacing||x->mission!=y->mission
                    ||runtimeObjectRenderX(*x)!=runtimeObjectRenderX(*y)||runtimeObjectRenderY(*x)!=runtimeObjectRenderY(*y)
                    ||runtimeObjectRenderLevel(*x)!=runtimeObjectRenderLevel(*y)||x->weaponCooldownTicksRemaining!=y->weaponCooldownTicksRemaining)return false;}
            return true;};
        const auto sample=[&](RuntimeWindowState& s,const char* replica){const auto* o=findObjectById(s,sourceId);const auto* t=findObjectById(s,targetId);
            states<<label<<','<<replica<<','<<s.tick<<','<<(o?o->health:-1)<<','<<(o?o->facing:-1)<<','<<(o?o->turretFacing:-1)<<','
                <<(o?o->turretFacingClock.active():false)<<','<<(t?t->health:-1)<<','<<(t?runtimeObjectRenderX(*t):-1)<<','
                <<(t?runtimeObjectRenderY(*t):-1)<<','<<(t?runtimeObjectRenderLevel(*t):-1)<<','<<(t?t->airborne:false)<<','
                <<(s.behaviors.contains(sourceId)?int(s.behaviors.at(sourceId).kind):-1)<<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount
                <<','<<std::quoted(s.simulationStatus)<<'\n';states.flush();};
        const auto savePair=[&](RuntimeWindowState& s,std::unique_ptr<RuntimeWindowState>& ordinary,std::unique_ptr<RuntimeWindowState>& canonical){
            const auto basePath=(root/(label+"-firstshot-ordinary14.ini")).string(),exactPath=(root/(label+"-firstshot-canonical.ini")).string();
            std::ostringstream text,extended,normalized;IniFile exact;
            if(!writeRuntimeStateToStream(s,text,error))return fail("full save writer "+error);
            {std::ofstream file(basePath,std::ios::binary);file<<text.str();if(!file)return fail("closed ordinary physical write");}
            extended<<text.str();if(!writeRuntimeMultiplayerSnapshotExtension(s,extended,error)||!exact.loadFromString(extended.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error))return fail("canonical writer "+error);
            {std::ofstream file(exactPath,std::ios::binary);file<<normalized.str();if(!file)return fail("closed canonical physical write");}
            for(bool isCanonical:{false,true}){IniFile base,reopened;
                if(!base.loadFromFile(basePath))return fail("full ordinary base physical reopen");++reopens;
                if(isCanonical){if(!reopened.loadFromFile(exactPath))return fail("physical canonical reopen");++reopens;}
                auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);
                {RuntimeSnapshotActiveStateScope active(*copy);if(!materializeRuntimeLoadStateFromIni(*copy,base,error)
                    ||(isCanonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,reopened,error)))return fail("full physical restore "+error);}
                copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;
                initializeRuntimeOfflineGameplayRouting(*copy);
                const bool equal=same(s,*copy,isCanonical),pixels=samePixels(paint(s,60,false).pixels,paint(*copy,60,false).pixels);
                const bool raw=s.world.deterministicSimulationStateHash()==copy->world.deterministicSimulationStateHash();
                saves<<label<<','<<(isCanonical?"canonical":"ordinary14")<<','<<(isCanonical?exactPath:basePath)<<','<<s.tick<<','<<raw<<','
                    <<(worldContract(s.world)==worldContract(copy->world))<<','<<(s.specialMechanisms.stateHash()==copy->specialMechanisms.stateHash())<<','
                    <<(projectileAuthority(s)==projectileAuthority(*copy))<<','<<equal<<','<<pixels<<','<<(equal&&pixels)<<'\n';saves.flush();
                if(!equal||!pixels){std::ostringstream liveText,peerText;writeRuntimeStateToStream(s,liveText,error);writeRuntimeStateToStream(*copy,peerText,error);
                    {std::ofstream f(root/(label+"-"+(isCanonical?"canonical":"ordinary14")+"-source.txt"));f<<liveText.str();}
                    {std::ofstream f(root/(label+"-"+(isCanonical?"canonical":"ordinary14")+"-peer.txt"));f<<peerText.str();}
                    return fail("initial full physical authority/Patriot pixels "+std::string(isCanonical?"canonical":"ordinary14"));}
                if(isCanonical){canonical=std::move(copy);++canonicalLoads;}else{ordinary=std::move(copy);++ordinaryLoads;}}
            return true;};
        for(int zoom:{48,60})for(int facing:{0,32,64,96,128,160,192,224}){
            label="NASAM-static-z"+std::to_string(zoom)+"-f"+std::to_string(facing);auto scene=make();
            auto* source=findMutableObjectById(*scene,sourceId);source->facing=source->turretFacing=facing;
            if(!anchor(*scene,zoom,facing==0))return false;
            checks<<label<<",static,"<<zoom<<','<<facing<<",0,-1,0,1\n";checks.flush();++statics;
        }
        // Actual selected-player Primary, not a seeded Attack phase or artificial projectile.
        // Original stationary airborne map/script DISK input; native full simulation owns its hover.
        std::ofstream commands(root/"commands.csv");commands<<"case,tick,dispatched,valid,verb,accepted,rejected,target,expected_facing,actual_first_facing\n";
        const std::array<std::array<int,2>,8> directions{{{0,-5},{5,-5},{5,0},{5,5},{0,5},{-5,5},{-5,0},{-5,-5}}};
        for(int direction=0;direction<8;++direction){label="NASAM-real-AA-d"+std::to_string(direction);auto scene=make();auto& s=*scene;
            RuntimeSnapshotActiveStateScope active(s);const int dx=directions[direction][0],dy=directions[direction][1];
            const double theta=std::atan2(double(dx-dy),-double(dx+dy));
            const int expectedFacing=(int(std::lround(theta*256/6.2831853071795864769))+256)%256;
            auto* source=findMutableObjectById(s,sourceId);source->facing=source->turretFacing=(expectedFacing+128)%256;
            WorldObjectInstance target;target.instanceId=targetId;target.type="DISK";target.owner="Russians";target.kind=WorldObjectKind::Unit;
            target.mapId="original-airborne-DISK";target.x=64+dx;target.y=64+dy;target.cell=runtimeCellKey(target.x,target.y);
            target.alive=target.poweredOperational=target.airborne=true;target.health=runtimeMaxHealthForObject(s,target);target.currentAmmo=-1;
            target.mission="Sleep";target.level=target.flightLevel=3;clearRuntimeRenderPosition(target);s.world.accessObjects().push_back(target);
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            s.camera.initialized=true;s.camera.tileWidth=60;s.camera.tileHeight=30;s.camera.worldOffsetX=-width/2;s.camera.worldOffsetY=128*15-height/2;
            if(target.health!=600||findObjectById(s,sourceId)->health!=900||!runtimeObjectVisibleToOwner(s,"Americans",*findObjectById(s,targetId)))
                return fail("original600HP visible airborne target/900HP source input");
            if(!anchor(s,60,direction==0))return false;
            s.selectedObjectIds={sourceId};const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*findObjectById(s,targetId)),findObjectById(s,targetId));
            const int accepted=s.lastCommand.acceptedCount,rejected=s.lastCommand.rejectedCount;
            if(!dispatched||!s.lastCommand.valid||accepted!=1||rejected!=0||!s.behaviors.contains(sourceId)
                ||s.behaviors.at(sourceId).kind!=RuntimeBehaviorKind::Attack||s.behaviors.at(sourceId).targetObjectId!=targetId)
                return fail("actual Primary rejected: "+s.lastCommand.verb+" accepted="+std::to_string(accepted)+" rejected="+std::to_string(rejected));
            int first=-1,firstFacing=-1;RuntimeProjectileEffect shot;
            for(int tick=0;tick<30*20&&first<0;++tick){step(s);sample(s,"live");
                for(const auto& p:s.projectiles)if(p.attackerObjectId==sourceId&&p.weaponId=="RedEye2"){shot=p;first=s.tick;firstFacing=findObjectById(s,sourceId)->turretFacing;break;}
                if(first<0)for(const auto& p:s.recentExpiredProjectiles)if(p.attackerObjectId==sourceId&&p.weaponId=="RedEye2"){shot=p;first=s.tick;firstFacing=findObjectById(s,sourceId)->turretFacing;break;}}
            commands<<label<<','<<s.tick<<','<<dispatched<<','<<s.lastCommand.valid<<','<<s.lastCommand.verb<<','<<accepted<<','<<rejected<<','
                <<targetId<<','<<expectedFacing<<','<<firstFacing<<'\n';commands.flush();
            if(first<0)return fail("real original AA first shot timeout");
            const int angleDifference=std::min((firstFacing-expectedFacing+256)%256,(expectedFacing-firstFacing+256)%256);
            if(shot.targetObjectId!=targetId||shot.damage!=75||shot.warheadId!="SAMWH"||shot.projectileId!="AAHeatSeeker"
                ||!shot.hasSourcePosition||shot.spawnTick!=first||angleDifference>8||findObjectById(s,sourceId)->health!=900)
                return fail("original AA first projectile/target/ROT8 direction identity");
            // Original PrimaryFireFLH90,50,100 and independent project world
            // direction basis. Sprite seating changes presentation, not this
            // physical FLH or the original firing facing/height.
            const double yaw=(shot.attackerFacing-96)*6.2831853071795864769/256.0;
            const double forwardX=std::cos(yaw),forwardY=std::sin(yaw);
            const double originalX=64+(forwardX*90-forwardY*50)/256.0;
            const double originalY=64+(forwardY*90+forwardX*50)/256.0;
            const double originalZ=100*2.4494897427831780982/256.0;
            if(std::abs(shot.sourceX-originalX)>1e-6||std::abs(shot.sourceY-originalY)>1e-6||std::abs(shot.sourceZ-originalZ)>1e-6)
                return fail("original independent PrimaryFireFLH world source changed: actual="+std::to_string(shot.sourceX)+","+std::to_string(shot.sourceY)+","+std::to_string(shot.sourceZ)
                    +" expected="+std::to_string(originalX)+","+std::to_string(originalY)+","+std::to_string(originalZ));
            shots<<label<<','<<first<<','<<shot.actorId<<','<<shot.attackerObjectId<<','<<shot.targetObjectId<<','<<shot.weaponId<<','<<shot.warheadId<<','
                <<shot.damage<<','<<shot.spawnTick<<','<<shot.sourceX<<','<<shot.sourceY<<','<<shot.sourceZ<<','<<shot.sourceScreenOffsetX<<','
                <<shot.sourceScreenOffsetY<<','<<std::hypot(double(dx),double(dy))<<','<<firstFacing<<'\n';shots.flush();
            if(!anchor(s,60,direction==0))return false;
            std::unique_ptr<RuntimeWindowState> ordinary,canonical;
            if(direction==0&&!savePair(s,ordinary,canonical))return false;
            for(int n=0;n<16;++n){step(s);sample(s,"live");
                for(const auto& peer:{std::pair<const char*,RuntimeWindowState*>{"ordinary14",ordinary.get()},{"canonical",canonical.get()}})if(peer.second){
                    step(*peer.second);sample(*peer.second,peer.first);if(!same(s,*peer.second,peer.first==std::string("canonical"))
                        ||!samePixels(paint(s,60,false).pixels,paint(*peer.second,60,false).pixels))return fail("future actual flight/AA clock/Patriot pixels "+std::string(peer.first));++paired;}}
            const int held=findObjectById(s,sourceId)->turretFacing;
            for(RuntimeWindowState* q:{&s,ordinary.get(),canonical.get()})if(q){RuntimeSnapshotActiveStateScope selected(*q);q->selectedObjectIds={sourceId};issueRuntimeStopCommand(*q);
                if(findObjectById(*q,sourceId)->turretFacing!=held)return fail("actual Stop immediately reset turret");}
            for(int n=0;n<30;++n){step(s);sample(s,"live");const auto* o=findObjectById(s,sourceId);
                if(!o||o->health!=900||o->turretFacing!=held||o->turretFacingClock.active())return fail("actual Stop/idle did not hold final facing");
                for(const auto& peer:{std::pair<const char*,RuntimeWindowState*>{"ordinary14",ordinary.get()},{"canonical",canonical.get()}})if(peer.second){
                    step(*peer.second);sample(*peer.second,peer.first);if(!same(s,*peer.second,peer.first==std::string("canonical"))
                        ||!samePixels(paint(s,60,false).pixels,paint(*peer.second,60,false).pixels))return fail("Stop paired authority/pixels "+std::string(peer.first));++paired;}}
            if(!anchor(s,60,direction==0))return false;++aa;++stops;
            checks<<label<<",real-AA,60,"<<firstFacing<<",1,"<<first<<",1,1\n";checks.flush();
        }
        if(statics!=16||aa!=8||stops!=8||ordinaryLoads!=1||canonicalLoads!=1||reopens!=3||paired!=92
            ||outsideWitness==0||oldWitness==0||whiteWitness==0)return fail("acceptance census/independent pedestal witnesses incomplete");
        std::ofstream result(root/"result.txt");result<<"PASS static="<<statics<<" realAA="<<aa<<" Stop="<<stops<<" physicalOrdinary="<<ordinaryLoads
            <<" physicalCanonical="<<canonicalLoads<<" uniqueFiles=2 reopenActions="<<reopens<<" pairedTicks="<<paired<<" BMP="<<bmps
            <<" outsideWitness="<<outsideWitness<<" originalWhitePedestal="<<whiteWitness<<" lowerAnchorWrong="<<oldWitness<<"\n";
        std::cout<<"Issue25 Patriot passed static="<<statics<<" realAA="<<aa<<" Stop="<<stops<<" physicalLoads=2 reopenActions="<<reopens
            <<" pairedTicks="<<paired<<" BMP="<<bmps<<'\n';return true;
    } catch(const std::exception& exception){return fail(exception.what());}
}
#pragma optimize("", on)