// Issue 25.1: raw-resource occlusion oracle for stock ground mobiles by a refinery.
// Include after RuntimeGroundUnitDepthSmoke.inl (native VXL/HVA box oracle).
// Colors come from isolated original sprites; expected owners come solely from
// raw BUILDNGZ, raw body/bib SHPs and the independently audited retail gradient.
// Infantry DrawIt 518F90 / 5195AF reads signed -10 at 825500; gradient 2,
// 4DAFC0 gives flat -heightPixel-1, and 706430 subtracts 2: base has -12. DRON normal Unit SHP caller supplies zero, giving -2.
namespace RuntimeIssue25GroundOracle {
using namespace RuntimeBuildingDepthOracle;
inline OriginalFrame frame(const Bytes& bytes,int index) {
    if(index<0||static_cast<unsigned>(index)>=little(bytes,6,2))
        throw std::runtime_error("issue25 original SHP frame outside resource");
    Bytes selected=bytes;const std::size_t at=8+static_cast<std::size_t>(index)*24;
    if(at+24>bytes.size())throw std::runtime_error("issue25 original SHP table range");
    std::copy_n(bytes.begin()+at,24,selected.begin()+8);return decodeFrameZero(selected);
}
inline void retailGroundParameters(const std::filesystem::path& root) {
    const auto exe=readPinned(root/"RedAlert2/GameMD/gamemd.exe",
        "7b8a068535d6af06845edf95ae829b113d00c02909330e16f197426cd7db94b6");
    const std::size_t nt=little(exe,0x3c,4);
    if(little(exe,nt,4)!=0x00004550||little(exe,nt+24,2)!=0x10b)
        throw std::runtime_error("issue25 original PE header");
    const std::uint32_t base=little(exe,nt+24+28,4);
    const std::size_t first=nt+24+little(exe,nt+20,2);const auto count=little(exe,nt+6,2);
    const auto at=[&](std::uint32_t va,std::size_t bytes){
        if(va<base)throw std::runtime_error("issue25 original VA below image");const auto rva=va-base;
        for(unsigned section=0;section<count;++section){const auto offset=first+section*40U;
            const auto address=little(exe,offset+12,4),size=little(exe,offset+16,4),raw=little(exe,offset+20,4);
            if(rva>=address&&static_cast<std::uint64_t>(rva-address)+bytes<=size)return static_cast<std::size_t>(raw+rva-address);}
        throw std::runtime_error("issue25 original VA outside raw PE sections");};
    if(little(exe,at(0x825500,4),4)!=0xfffffff6U
        ||little(exe,at(0x7eb058+0xb8,4),4)!=0x5f6bd0
        ||little(exe,at(0x7f69f8+0x38,4),4)!=0x55aba0
        ||little(exe,at(0x7f69f8+0x3c,4),4)!=0x55abb0
        ||little(exe,at(0x55aba0,3),3)!=0xc2c033
        ||little(exe,at(0x55abb0,5),1)!=0xb8||little(exe,at(0x55abb0+1,4),4)!=2
        ||little(exe,at(0x81ac58,5),4)!=0x6c6c6157||exe.at(at(0x81ac58+4,1))!=0)
        throw std::runtime_error("issue25 independent infantry/Walk/wall retail constants differ");
    const auto matches=[&](std::uint32_t va,std::initializer_list<unsigned char> expected){
        const auto offset=at(va,expected.size());return std::equal(expected.begin(),expected.end(),exe.begin()+offset);};
    if(little(exe,at(0x7f5c70+0x2ec,4),4)!=0x4dafc0
        ||little(exe,at(0x7f5c70+0x2f0,4),4)!=0x4db0a0
        ||little(exe,at(0x7f5c70+0x50c,4),4)!=0x41c090
        ||little(exe,at(0x7e7eb0+0x38,4),4)!=0x4b4870
        ||little(exe,at(0x7e7eb0+0x3c,4),4)!=0x4b4880
        ||!matches(0x4b4870,{0x33,0xc0,0xc2,0x04,0x00})
        ||!matches(0x4b4880,{0x8b,0x44,0x24,0x04,0x50,0xe8,0x26,0x63,0x0a,0x00,0xc2,0x04,0x00})
        ||!matches(0x73ce71,{0x33,0xc9,0x33,0xc0,0x8b,0x75,0x00})
        ||!matches(0x73ce86,{0x50,0x8b,0xcd,0xff,0x96,0xf0,0x02,0x00,0x00,0x50,0x6a,0x00})
        ||!matches(0x73cead,{0xff,0x96,0x0c,0x05,0x00,0x00})
        ||!matches(0x7476d3,{0x8b,0x87,0x7c,0x06,0x00,0x00})
        ||!matches(0x7476de,{0x8a,0x97,0x28,0x0d,0x00,0x00,0xf6,0xda,0x1b,0xd2,0x83,0xc2,0x02}))
        throw std::runtime_error("issue25 independent Unit SHP/Drive/default Wheel retail constants differ");
}
inline int gradient(int nativeBottom,int nativeRow,int adjustment) {
    int z=((32768-nativeBottom-adjustment)/3)*3;
    for(int row=nativeBottom-1,phase=0;row>nativeRow;--row)if(++phase==3){--z;phase=0;}
    return z;
}
}

bool runRuntimeGroundOcclusionIssue25Smoke(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeBuildingDepthOracle;
    using RuntimeIssue25GroundOracle::gradient;
    std::cout<<std::unitbuf;
    const char* requested=std::getenv("CONCORD_ISSUE25_GROUND_OCCLUSION_OUTPUT");
    const std::filesystem::path root=requested&&*requested?requested:"build/smoke/feature-set-25/01-ground-occlusion";
    std::filesystem::create_directories(root);std::string label;
    const auto fail=[&](const std::string& why){error="Issue25 ground occlusion "+label+": "+why;
        std::cout<<error<<'\n';return false;};
    try {
        for(const char* setting:{"CONCORD_VXL_PROJ_SCREEN_X","CONCORD_VXL_PROJ_GROUND_Y","CONCORD_VXL_PROJ_HEIGHT_Y",
            "CONCORD_VXL_SCREEN_X_SCALE_MUL","CONCORD_VXL_SCREEN_Y_SCALE_MUL","CONCORD_VXL_HEIGHT_Y_SCALE_MUL",
            "CONCORD_VXL_TURRET_OFFSET_SCALE","CONCORD_VXL_TURRET_PIXEL_OFFSET_X","CONCORD_VXL_TURRET_PIXEL_OFFSET_Y",
            "CONCORD_VXL_BARREL_PIXEL_OFFSET_X","CONCORD_VXL_BARREL_PIXEL_OFFSET_Y"})
            if(std::getenv(setting))return fail(std::string("original projection required: ")+setting);
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=config;s.config.playerSlots.clear();s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.shortGame=false;
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        if(!s.report.success)return fail("stock startup");
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;
        RuntimeHumanHouseAuthority authority;authority.owner="Americans";authority.canonicalOwner="americans";authority.localFacade=true;
        s.humanHouseAuthorities={authority};s.ownerRemapColors["Americans"]=RGB(235,210,45);
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        const auto metadata=runtimeTerrainMetadataContext(s);int clearTile=-1;
        const int clearSet=s.theater.getGeneralTileSetIndex("ClearTile");
        for(int tile=0;tile<static_cast<int>(s.theater.getTotalTileSlots());++tile){TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;
            if(s.theater.resolveTileIndex(tile).tileSetIndex==clearSet
                &&resolveRuntimeTerrainSubTileLandType(cell,metadata,s.renderCache)==RuntimeTerrainLandType::Clear
                &&resolveRuntimeTerrainSubTileRampType(cell,metadata,s.renderCache)==0){clearTile=tile;break;}}
        if(clearTile<0)return fail("stock flat Clear TMP unavailable");
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.mapFacts={};
        for(int y=10;y<=36;++y)for(int x=10;x<=36;++x){TerrainCell cell;
            cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=clearTile;cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);MapCellFacts fact;fact.x=x;fact.y=y;fact.cellIndex=cell.cellIndex512;fact.hasTerrain=true;
            fact.terrain.x=x;fact.terrain.y=y;fact.terrain.tileIndex=clearTile;fact.terrain.subTileIndex=0;
            fact.terrain.level=fact.heightLevel=0;fact.terrainClass="clear";
            s.mapFacts.cellIndexLookup.emplace(fact.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(fact);}
        s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();s.mapFacts.contentHash=0x25010001;s.mapFactsLoaded=true;
        const auto plainTerrain=s.world.getTerrainCells();const auto plainFacts=s.mapFacts;
        int waterTile=-1;
        for(int tile=0;tile<static_cast<int>(s.theater.getTotalTileSlots());++tile){TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;
            if(resolveRuntimeTerrainSubTileLandType(cell,metadata,s.renderCache)==RuntimeTerrainLandType::Water
                &&resolveRuntimeTerrainSubTileRampType(cell,metadata,s.renderCache)==0){waterTile=tile;break;}}
        if(waterTile<0)return fail("stock flat Water TMP unavailable");
        auto waterTerrain=plainTerrain;auto waterFacts=plainFacts;waterFacts.contentHash=0x25010002;
        // A genuine flat shore keeps the refinery foundation on Clear and the hover craft on original Water TMP.
        for(auto& cell:waterTerrain){if(cell.x>=20&&cell.x<24&&cell.y>=20&&cell.y<23)continue;
            cell.tileIndex=waterTile;auto& fact=waterFacts.cells.at(waterFacts.cellIndexLookup.at(cell.cellIndex512));
            fact.terrain.tileIndex=waterTile;fact.terrainClass="water";}
        RuntimeIssue25GroundOracle::retailGroundParameters(std::filesystem::current_path());
        const auto originalArtBytes=readPinned(std::filesystem::current_path()/"local_data/resources/mix_exports/ra2md/localmd/artmd.ini",
            "e1f0378394313c04ebbd5073f47785ee3e46f1b3c62d65724e8f3c310ee7ba31");
        IniFile originalArt;if(!originalArt.loadFromString(std::string(originalArtBytes.begin(),originalArtBytes.end())))
            return fail("pinned original infantry ART unavailable");
        for(const char* image:{"GI","ENGINEER","CONS","TANY"}){
            const std::string sequence=originalArt.getValue(image,"Sequence");
            if(sequence.empty()||originalArt.getValue(sequence,"Walk")!="8,6,6")return fail(std::string("original Walk strip contract: ")+image);
        }
        if(toLowerAsciiLocal(originalArt.getValue("DRON","Voxel"))!="no"
            ||originalArt.getValue("DRON","WalkFrames")!="6"||originalArt.getValue("DRON","FiringFrames")!="4"
            ||!originalArt.getValue("DRON","Sequence").empty()||!originalArt.getValue("DRON","Facings").empty()
            ||!originalArt.getValue("DRON","StandingFrames").empty()||!originalArt.getValue("DRON","StartStandFrame").empty())
            return fail("original DRON implicit clockwise Walk/Stand strip contract");
        if(originalArt.getValue("CLEG","Sequence")!="ClegSequence"||originalArt.getValue("ClegSequence","Walk")!="117,6,6")
            return fail("original CLEG Walk strip contract");
        const auto shape=decodeFrameZero(retailTemplate(std::filesystem::current_path()));
        const auto* refineryArt=s.art.findArtType("GAREFN");
        if(!refineryArt||refineryArt->foundation!="4x3"||refineryArt->zShapePointMove!=std::array<int,2>{30,15}
            ||refineryArt->normalZAdjust||toLowerAsciiLocal(refineryArt->bibShape)!="garefnbb")return fail("stock refinery art contract");
        const OverlayType* wallRule=nullptr;
        for(const auto& overlay:s.rules.getOverlayTypes())if(toLowerAsciiLocal(overlay.id)=="gafwll")wallRule=&overlay;
        if(!wallRule||wallRule->listIndex<0)return fail("stock GAFWLL overlay rule unavailable");
        constexpr int width=640,height=480,buildingId=25101,mobileId=25102;const RECT viewport{0,0,width,height};
        struct Surface {HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
            Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
                info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;dc=CreateCompatibleDC(nullptr);void* raw=nullptr;
                if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);pixels=static_cast<std::uint32_t*>(raw);
                if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
            ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}} surface;
        if(!surface.dc||!surface.pixels)return fail("DIB allocation");
        RuntimeMapRenderContext view;view.rules=&s.rules;view.art=&s.art;view.theater=&s.theater;view.mapFacts=&s.mapFacts;
        view.assetRootPath=runtimeAssetRootPath(s.config);view.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
        view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);view.palettePath=runtimePalettePath(s.config,s.report.map);
        view.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);view.ownerRemapColors=&s.ownerRemapColors;
        view.animationTick=17;view.fixedTickHz=30;view.useTileTextures=view.shadowsEnabled=false;view.seeHiddenObjects=false;
        view.surfacePixels=surface.pixels;view.surfaceWidth=width;view.surfaceHeight=height;
        const auto difference=[](const auto& a,const auto& b){int count=0;for(std::size_t p=0;p<a.size();++p)
            count+=(a[p]&0xffffffU)!=(b[p]&0xffffffU);return count;};
        struct Specimen {const char* type;bool infantry;bool shpVehicle=false;bool water=false;int walkStart=8;};
        const std::array<Specimen,13> specimens{{{"MTNK",false},{"HTNK",false},{"FV",false},{"HARV",false},
            {"E1",true},{"ENGINEER",true},{"E2",true},{"TANY",true},{"DRON",false,true},
            {"ROBO",false},{"CMIN",false},{"CLEG",true,false,false,117},{"LCRF",false,false,true}}};
        struct Coverage {std::uint64_t overlap=0,visible=0,hidden=0,mixed=0,wall=0;};std::map<std::string,Coverage> coverage;
        std::ofstream rows(root/"issue25-ground-depth.csv"),sources(root/"issue25-original-sources.csv"),pickDiagnostics(root/"issue25-pick-diagnostics.csv");
        pickDiagnostics<<"case,x,y,expected,before,after,projected_id,render_x,render_y,bounds_left,bounds_top,bounds_right,bounds_bottom,sprite_left,sprite_top,sprite_right,sprite_bottom,in_bounds,in_sprite,has_final_visibility,in_final_visible_span,span_count\n";
        rows<<"mobile,pose,subcell,zoom,facing,pan,wall,parts,overlap,mobile_visible,building_visible,mixed,pixels,before_picks,after_picks,order,ids,repeat,hdc,whole_front_wrong,whole_back_wrong\n";
        sources<<"mobile,role,image,sha256,vxl_sha256,hva_sha256,frame,native_bottom\n";
        int cases=0,hdcChecks=0,qualificationChecks=0;std::uint64_t beforeClickCount=0,afterClickCount=0;
        auto cache=std::make_unique<RuntimeMapRenderCache>();
        RuntimeSnapshotActiveStateScope active(s);
        for(int target:{0,1,2})for(const auto& specimen:specimens)for(bool moving:specimen.shpVehicle?std::vector<bool>{true,false}:std::vector<bool>{true})for(int hoverPhase:specimen.water?std::vector<int>{0,1}:std::vector<int>{0})for(int pose:{0,1,2})for(int subCell:specimen.infantry?std::vector<int>{2,3,4}:std::vector<int>{-1})
            for(int zoom:{48,60})for(int facing:{0,64})for(bool pan:{false,true})for(bool nearbyWall:{false,true}) {
            if(target>0&&(nearbyWall||(std::string(specimen.type)!="MTNK"&&std::string(specimen.type)!="FV"
                &&std::string(specimen.type)!="E1"&&std::string(specimen.type)!="E2"&&!specimen.shpVehicle)))continue;
            const std::string targetName=target==0?"refinery":target==1?"player-wall":"map-wall";
            const std::string specimenName=std::string(specimen.type)+(specimen.shpVehicle?(moving?"-moving":"-stand"):"")+(specimen.water?"-water-phase"+std::to_string(hoverPhase):"");
            label=targetName+"-"+specimenName+"-p"+std::to_string(pose)+"-s"+std::to_string(subCell)+"-z"+std::to_string(zoom)
                +"-f"+std::to_string(facing)+"-pan"+std::to_string(pan)+"-wall"+std::to_string(nearbyWall);
            const auto& scenePlainTerrain=specimen.water?waterTerrain:plainTerrain;const auto& scenePlainFacts=specimen.water?waterFacts:plainFacts;
            s.world.accessTerrainCells()=scenePlainTerrain;s.mapFacts=scenePlainFacts;
            const int sceneTick=specimen.water?(hoverPhase?45:15):17;
            const int hoverLift=specimen.water?(zoom==48?(hoverPhase?5:7):(hoverPhase?6:9)):0;
            view.animationTick=sceneTick;
            WorldObjectInstance building;building.kind=WorldObjectKind::Structure;building.type=target?"GAFWLL":"GAREFN";building.instanceId=buildingId;
            building.owner="Americans";building.x=building.y=20;building.cell=runtimeCellKey(20,20);building.alive=building.poweredOperational=true;
            building.health=runtimeMaxHealthForObject(s,building);building.mission="Guard";
            WorldObjectInstance mobile;mobile.kind=specimen.infantry?WorldObjectKind::Infantry:WorldObjectKind::Unit;
            mobile.type=specimen.type;mobile.instanceId=mobileId;mobile.owner="Americans";mobile.alive=mobile.poweredOperational=true;
            mobile.x=pose==0?19:pose==1?(target?20:21):20;mobile.y=pose==0?19:(target?21:23);mobile.cell=runtimeCellKey(mobile.x,mobile.y);
            mobile.hasRenderPosition=true;mobile.renderX=mobile.x+(pose==0?0:.25);mobile.renderY=mobile.y+(pose==0||pose==2?0:.25);mobile.renderLevel=0;
            if(!target&&pose==1&&(specimen.infantry||specimen.shpVehicle)){
                // These fixed near-edge poses were selected from original SHP/BUILDNGZ/BIB pixels, before production comparison.
                const std::string type=specimen.type;
                if(type=="ENGINEER"||type=="CLEG"){mobile.renderX=22.25;mobile.renderY=23.25;}
                else if(type=="E2"){mobile.renderX=22.75;mobile.renderY=23.25;}
                else if(type=="TANY"){mobile.renderX=22.75;mobile.renderY=24;}
                else if(specimen.shpVehicle){mobile.renderX=22.75;mobile.renderY=23.75;}
                mobile.x=static_cast<int>(std::floor(mobile.renderX));mobile.y=static_cast<int>(std::floor(mobile.renderY));mobile.cell=runtimeCellKey(mobile.x,mobile.y);
            }
            if(target&&pose>0&&(specimen.infantry||specimen.shpVehicle)){
                // Original SHP masks and native row depths put these poses across the wall's near edge.
                if(specimen.shpVehicle){mobile.renderX=pose==1?21.5:21.4375;mobile.renderY=21.625;}
                else if(std::string(specimen.type)=="E1"){mobile.renderX=pose==1?21:20.5;mobile.renderY=pose==1?21.5:21.625;}
                else if(std::string(specimen.type)=="E2"){mobile.renderX=pose==1?21.75:21.5;mobile.renderY=pose==1?21.75:21.5625;}
                mobile.x=static_cast<int>(std::floor(mobile.renderX));mobile.y=static_cast<int>(std::floor(mobile.renderY));mobile.cell=runtimeCellKey(mobile.x,mobile.y);
            }
            mobile.health=runtimeMaxHealthForObject(s,mobile);mobile.mission="Guard";mobile.facing=facing;mobile.turretFacing=(facing+64)%256;
            mobile.subCell=subCell;if(specimen.infantry||specimen.shpVehicle){mobile.renderMoveActive=moving;mobile.movementAnimationDistance=0;}
            // Explicit canonical input offsets; no production geometry helper computes the oracle.
            const double sx=subCell==2||subCell==4?.25:subCell==3?-.25:0;
            const double sy=subCell==3||subCell==4?.25:subCell==2?-.25:0;
            const double rx=mobile.renderX+sx,ry=mobile.renderY+sy;
            const double mobileSort=(rx+ry+1)*256;
            if(!target)for(const auto* slots:{&refineryArt->activeAnimations,&refineryArt->specialAnimations,&refineryArt->auxiliaryAnimations})
                for(const auto& slot:*slots)if(slot.offsetX||slot.offsetY||slot.ySort<0
                    ||(pose==0?mobileSort>=40*256:mobileSort<=40*256+slot.ySort))return fail("independent authored event separation");
            if(nearbyWall||target==2){const int wx=target==2?20:mobile.x-2,wy=target==2?20:mobile.y+1;
                auto& cell=s.world.accessTerrainCells().at(static_cast<std::size_t>(wy-10)*27+wx-10);
                if(specimen.water)cell.tileIndex=clearTile;
                cell.hasOverlay=true;cell.overlayIndex=wallRule->listIndex;cell.overlayType="GAFWLL";
                cell.overlayImage=wallRule->image.empty()?wallRule->id:wallRule->image;cell.overlayFrame=0;
                auto& fact=s.mapFacts.cells.at(s.mapFacts.cellIndexLookup.at(cell.cellIndex512));fact.hasOverlay=true;
                if(specimen.water){fact.terrain.tileIndex=clearTile;fact.terrainClass="clear";}
                fact.overlay.cellIndex=cell.cellIndex512;fact.overlay.x=wx;fact.overlay.y=wy;
                fact.overlay.overlayIndex=cell.overlayIndex;fact.overlay.type=cell.overlayType;fact.overlay.image=cell.overlayImage;
                s.mapFacts.contentHash+=static_cast<std::uint64_t>(runtimeCellKey(wx,wy))+1;}
            s.world.accessObjects()=target==2?std::vector<WorldObjectInstance>{mobile}:std::vector<WorldObjectInstance>{building,mobile};initializeRuntimeNavigation(s);
            // The fixed original Walk/standing fixture must be identical for projection, paint and the raw oracle.
            // Navigation clears continuous motion without a live behavior segment; restore only this diagnostic mobile pose.
            for(auto& object:s.world.accessObjects())if(object.instanceId==mobileId)object=mobile;
            s.structureFootprints=buildRuntimeStructureFootprints(s);s.tick=sceneTick;
            s.camera.initialized=true;s.camera.tileWidth=zoom;s.camera.tileHeight=zoom/2;
            s.camera.worldOffsetX=-width/2+(pan?29:0);s.camera.worldOffsetY=22*zoom/2-height/2+(pan?-17:0);
            invalidateRuntimeObjectProjectionCache(s);
            // Resolve picks before any draw of this scene, after navigation and camera settle.
            const auto projected=runtimeProjectedObjectsForViewport(s,viewport);
            std::vector<int> beforeOwners(width*height,-2);
            for(const auto& item:projected){RECT clip{};
                if(!IntersectRect(&clip,&item.spriteBounds,&viewport))continue;
                for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x){auto& saved=beforeOwners[static_cast<std::size_t>(y)*width+x];
                    if(saved!=-2)continue;const auto* hit=findProjectedObjectAtPoint(projected,POINT{x,y});saved=hit&&hit->object?hit->object->instanceId:-1;++beforeClickCount;}}
            GameWorld renderWorld=s.world;
            if(!(specimen.infantry?runtimeGroundInfantryUsesBuildingDepth(mobile,s.world,view,*cache)
                :runtimeGroundVehicleUsesBuildingDepth(mobile,s.world,view,*cache)))return fail("stock flat ground mobile qualification rejected");++qualificationChecks;
            RuntimeMapRenderStats lastPaintStats;
            const auto paint=[&](const std::vector<WorldObjectInstance>& objects,bool hdc=false,bool removeMapWall=false,bool markers=false){renderWorld.accessObjects()=objects;
                renderWorld.accessTerrainCells()=removeMapWall?scenePlainTerrain:s.world.getTerrainCells();auto camera=s.camera;auto context=view;
                if(removeMapWall)context.mapFacts=&scenePlainFacts;context.seeHiddenObjects=markers;
                if(hdc){context.surfacePixels=nullptr;context.surfaceWidth=context.surfaceHeight=0;}
                lastPaintStats=drawRuntimeMap(surface.dc,viewport,renderWorld,context,camera,*cache);
                if(!lastPaintStats.rendered)throw std::runtime_error("original production draw");
                GdiFlush();return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);};
            const auto blank=paint({},false,target==2),buildingOnly=paint(target==2?std::vector<WorldObjectInstance>{}:std::vector<WorldObjectInstance>{building}),
                mobileOnly=paint({mobile},false,target==2);
            const RuntimeAuxiliaryShpBytesCacheEntry* bodyBytes=nullptr;const RuntimeAuxiliaryShpBytesCacheEntry* bibBytes=nullptr;
            if(!target)for(const auto& [key,value]:cache->objectShpBytesByImage)if(value.loaded&&toLowerAsciiLocal(key).find("|garefn|")!=std::string::npos)bodyBytes=&value;
            if(target)for(const auto& [key,value]:cache->auxiliaryShpBytesByImage)if(value.loaded&&toLowerAsciiLocal(key).find("|gafwll|")!=std::string::npos)bodyBytes=&value;
            for(const auto& [key,value]:cache->auxiliaryShpBytesByImage)if(value.loaded&&toLowerAsciiLocal(key).find("|garefnbb|")!=std::string::npos)bibBytes=&value;
            if(!bodyBytes||(!target&&!bibBytes))return fail("original target raw SHPs absent");
            const auto body=decodeFrameZero(bodyBytes->bytes);const auto bib=target?OriginalFrame{}:decodeFrameZero(bibBytes->bytes);
            const int bx=-s.camera.worldOffsetX,by=static_cast<int>(std::lround(40*zoom/4.0))-s.camera.worldOffsetY;
            std::vector<int> buildingZ(width*height,std::numeric_limits<int>::max());
            const auto addBuilding=[&](const OriginalFrame& f,bool isBib){
                const int left=bx-f.canvasWidth/2+f.x,top=by-f.canvasHeight/2+f.y,nativeTop=600-f.canvasHeight/2+f.y;
                const int u0=f.x-f.canvasWidth/2+198-30*(4-3)+30,v0=f.y-f.canvasHeight/2+446-15*(4+3-2)+15;
                const int templateRows=std::min(f.height,shape.height-v0)-std::max(0,-v0);
                for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x){if(!f.pixels[static_cast<std::size_t>(y)*f.width+x])continue;
                    const int px=left+x,py=top+y;if(px<0||py<0||px>=width||py>=height)continue;int z=0;
                    if(target)z=gradient(nativeTop+f.height,nativeTop+y,1);
                    else if(isBib)z=32768-nativeTop-y-3;
                    else {const int u=u0+x,v=v0+y;
                        if(u>=0&&v>=0&&u<shape.width&&v<shape.height){const int raw=shape.pixels[static_cast<std::size_t>(v)*shape.width+u];
                            int signedValue=raw?raw-65:0;if(signedValue>127)signedValue-=256;if(signedValue< -128)signedValue+=256;
                            z=32768-nativeTop-templateRows-1-signedValue;}
                        else z=gradient(nativeTop+f.height,nativeTop+y,1);}
                    auto& dest=buildingZ[static_cast<std::size_t>(py)*width+px];dest=std::min(dest,z);}};
            if(!target)addBuilding(bib,true);addBuilding(body,false);
            sources<<targetName+"-"+specimen.type<<",body,"<<building.type<<','<<sha256(bodyBytes->bytes)<<",,,0,0\n";
            if(!target)sources<<targetName+"-"+specimen.type<<",bib,GAREFNBB,"<<sha256(bibBytes->bytes)<<",,,0,0\n";
            const int mx=static_cast<int>(std::lround((rx-ry)*zoom/2.0))-s.camera.worldOffsetX;
            const int my=static_cast<int>(std::lround((rx+ry)*zoom/4.0))-s.camera.worldOffsetY;
            const int nativeY=static_cast<int>(std::lround((rx+ry)*15));
            struct Part {const RuntimeShpFrameImage* image=nullptr;OriginalFrame original;int left=0,top=0,bottom=0;};std::vector<Part> parts;
            int unionBottom=0;
            const auto* mobileRule=s.rules.findUnitType(specimen.infantry?"infantry":"vehicle",specimen.type);
            if(!mobileRule)return fail("stock mobile rule absent");
            const std::string visualId=mobileRule->image.empty()?specimen.type:mobileRule->image;const auto* mobileArt=s.art.findArtType(visualId);
            if(!mobileArt)return fail("stock mobile art absent");
            if(specimen.infantry||specimen.shpVehicle){
                const RuntimeAuxiliaryShpBytesCacheEntry* bytes=nullptr;const std::string keyToken="|"+toLowerAsciiLocal(mobileArt->image.empty()?visualId:mobileArt->image)+"|";
                for(const auto& [key,value]:cache->objectShpBytesByImage)if(value.loaded&&toLowerAsciiLocal(key).find(keyToken)!=std::string::npos)bytes=&value;
                if(!bytes)return fail("original mobile raw SHP absent: "+keyToken);
                if(specimen.shpVehicle&&(sha256(bytes->bytes)!="c5d2a3a0b58c664bd5d8cbf033f865588224e6888552c83ec4b44f2b32dd2e94"
                    ||little(bytes->bytes,6,2)!=176))return fail("original DRON stock SHP source contract");
                const int bucket=specimen.shpVehicle?((facing*8+128)/256)%8:((((256-facing)%256)*8)+128)/256%8;
                // Original infantry strips are N,NW,W,SW,S,SE,E,NE; DRON implicit Unit strips are N,NE,E,SE,S,SW,W,NW.
                const int frameIndex=specimen.shpVehicle?(moving?bucket*6:48+bucket):specimen.walkStart+bucket*6;Part part;part.original=RuntimeIssue25GroundOracle::frame(bytes->bytes,frameIndex);
                const auto& f=part.original;
                if(specimen.shpVehicle){const std::array<int,4> expectedCrop=moving?(facing==0?std::array<int,4>{29,23,17,20}:std::array<int,4>{29,26,22,17})
                        :(facing==0?std::array<int,4>{29,26,19,18}:std::array<int,4>{29,26,18,17});
                    if(f.canvasWidth!=77||f.canvasHeight!=77||std::array<int,4>{f.x,f.y,f.width,f.height}!=expectedCrop)
                        return fail("independent DRON original frame header");}
                part.left=mx-f.canvasWidth/2+f.x;part.top=my+zoom/4-f.canvasHeight+f.y;
                bool poseInput=false;
                for(const auto& [key,value]:cache->objectShpSpritesByType)
                    if(value.loaded&&toLowerAsciiLocal(value.resolvedImageId)==toLowerAsciiLocal(visualId)
                        &&value.frameIndex==frameIndex&&value.sequenceName==(specimen.shpVehicle?(moving?"MoveFallback":"GuardFallback"):"Walk")&&value.image.width==f.width&&value.image.height==f.height
                        &&value.image.offsetX==f.x&&value.image.offsetY==f.y&&value.image.pixels==f.pixels)poseInput=true;
                if(!poseInput)return fail("rendered SHP pose differs from independent original frame input");
                part.bottom=nativeY+part.top-my+f.height;unionBottom=part.bottom;parts.push_back(std::move(part));
                sources<<specimenName<<(specimen.shpVehicle?",shp-vehicle,":",infantry,")<<visualId<<','<<sha256(bytes->bytes)<<",,,"<<frameIndex<<','<<unionBottom<<'\n';
            } else {
                std::array<const RuntimeVoxelCacheEntry*,3> rasters{};const std::string token=toLowerAsciiLocal(specimen.type)+"|";
                for(const auto& [key,value]:cache->voxelObjectsByType){if(!value.loaded||!value.hasSprite||value.animationFrameIndex!=0||!key.starts_with(token))continue;
                    const int role=key.starts_with(token+"turret|")?1:key.starts_with(token+"barrel|")?2:0;
                    if(value.facingBucket!=(((role?mobile.turretFacing:facing)+224)%256)/8)continue;
                    if(role==0&&toLowerAsciiLocal(value.metadata.imageId)!=toLowerAsciiLocal(mobileArt->image.empty()?visualId:mobileArt->image))continue;
                    rasters[role]=&value;}
                if(!rasters[0]||(mobileRule->turret&&!rasters[1]))return fail("stock vehicle authored body/turret rasters absent");
                if(!mobileRule->turret&&(rasters[1]||rasters[2]))return fail("stock non-turret vehicle has unexpected turret/barrel raster");
                if((std::string(specimen.type)=="ROBO"&&!mobileRule->turret)
                    ||((std::string(specimen.type)=="LCRF"||std::string(specimen.type)=="CMIN")&&mobileRule->turret))return fail("original Hover/Teleport Turret contract");
                const double angle=(((facing+224)%256)/8-8)*6.2831853071795864769/32;
                const double mount=mobileArt->turretOffset.empty()?0:std::stod(mobileArt->turretOffset)*.18;
                const int tx=static_cast<int>(std::lround(mount*(std::cos(angle)-std::sin(angle))*1.38*rasters[0]->rasterFitScale));
                const int ty=static_cast<int>(std::lround(mount*(std::cos(angle)+std::sin(angle))*.72*rasters[0]->rasterFitScale));
                for(int role=0;role<3;++role)if(rasters[role]){const auto& raster=*rasters[role];const RuntimeVoxelSourceCacheEntry* source=nullptr;
                    for(const auto& [key,value]:cache->voxelSourcesByImage)if(value.loaded&&value.metadata.vxlPath==raster.metadata.vxlPath)source=&value;
                    if(!source)return fail("original VXL/HVA raw source absent");Part part;part.image=&raster.image;
                    part.left=mx+(role?tx:0)-raster.image.globalWidth/2+raster.image.offsetX;
                    part.top=my-hoverLift+(role?ty:0)-raster.image.globalHeight+raster.image.offsetY;
                    part.bottom=nativeY+(role?ty:0)+RuntimeGroundDepthOracle::nativeVoxelBottom(*source,role?mobile.turretFacing:facing,raster.rasterFitScale,role!=0);
                    if(parts.empty())unionBottom=part.bottom;else if(part.bottom>unionBottom)unionBottom=part.bottom+1;parts.push_back(std::move(part));
                    sources<<specimen.type<<','<<role<<','<<raster.metadata.imageId<<",,"<<sha256(source->vxlBytes)<<','<<sha256(source->hvaBytes)<<",0,"<<parts.back().bottom<<'\n';}
            }
            auto expected=buildingOnly,wholeFront=buildingOnly,wholeBack=mobileOnly;
            std::vector<unsigned char> mobileMask(width*height),visible(width*height);
            for(std::size_t p=0;p<blank.size();++p)if((buildingOnly[p]&0xffffffU)!=(blank[p]&0xffffffU))wholeBack[p]=buildingOnly[p];
            for(const auto& part:parts){const int pw=part.image?part.image->width:part.original.width,ph=part.image?part.image->height:part.original.height;
                const auto& raw=part.image?part.image->pixels:part.original.pixels;
                for(int y=0;y<ph;++y)for(int x=0;x<pw;++x){if(!raw[static_cast<std::size_t>(y)*pw+x])continue;
                    const int px=part.left+x,py=part.top+y;if(px<0||py<0||px>=width||py>=height)continue;const auto p=static_cast<std::size_t>(py)*width+px;
                    mobileMask[p]=1;wholeFront[p]=mobileOnly[p];const int z=gradient(unionBottom,nativeY+py-my+hoverLift,specimen.infantry?12:specimen.shpVehicle?2:0);
                    if(z<buildingZ[p]){visible[p]=1;expected[p]=mobileOnly[p];}}}
            if(pose==0&&!target){expected=wholeBack;for(std::size_t p=0;p<blank.size();++p)visible[p]=mobileMask[p]
                &&(buildingOnly[p]&0xffffffU)==(blank[p]&0xffffffU);}
            int overlap=0,mobileVisible=0,buildingVisible=0,beforeErrors=0,afterErrors=0;
            const auto actual=paint(target==2?std::vector<WorldObjectInstance>{mobile}:std::vector<WorldObjectInstance>{building,mobile});
            const auto afterProjected=runtimeProjectedObjectsForViewport(s,viewport);
            for(std::size_t p=0;p<blank.size();++p){if(!mobileMask[p]||(buildingOnly[p]&0xffffffU)==(blank[p]&0xffffffU)
                ||(mobileOnly[p]&0xffffffU)==(buildingOnly[p]&0xffffffU))continue;
                ++overlap;visible[p]?++mobileVisible:++buildingVisible;const int owner=visible[p]?mobileId:target==2?-1:buildingId;
                const POINT point{static_cast<LONG>(p%width),static_cast<LONG>(p/width)};
                const auto* diagnosticHit=findProjectedObjectAtPoint(afterProjected,point);
                const int diagnosticAfter=diagnosticHit&&diagnosticHit->object?diagnosticHit->object->instanceId:-1;
                const auto projectedMobile=std::find_if(projected.begin(),projected.end(),[&](const auto& item){return item.object&&item.object->instanceId==mobileId;});
                const bool mobileOutsideBounds=visible[p]&&(projectedMobile==projected.end()||!pointInRectInclusive(projectedMobile->bounds,point));
                if(beforeOwners[p]!=owner||diagnosticAfter!=owner||mobileOutsideBounds){
                    for(const auto& item:projected){if(!item.object)continue;
                        pickDiagnostics<<label<<','<<point.x<<','<<point.y<<','<<owner<<','<<beforeOwners[p]<<','<<diagnosticAfter<<','<<item.object->instanceId
                            <<','<<item.renderCenter.x<<','<<item.renderCenter.y<<','<<item.bounds.left<<','<<item.bounds.top<<','<<item.bounds.right<<','<<item.bounds.bottom
                            <<','<<item.spriteBounds.left<<','<<item.spriteBounds.top<<','<<item.spriteBounds.right<<','<<item.spriteBounds.bottom
                            <<','<<pointInRectInclusive(item.bounds,point)<<','<<PtInRect(&item.spriteBounds,point)<<','<<item.hasBuildingDepthVisibility
                            <<','<<runtimeSpriteHitSpansContain(item.visibleBuildingHitSpans,point)<<','<<item.visibleBuildingHitSpans.size()<<'\n';}
                }
                if(beforeOwners[p]!=owner)++beforeErrors;const auto* hit=findProjectedObjectAtPoint(afterProjected,POINT{static_cast<LONG>(p%width),static_cast<LONG>(p/width)});
                if((hit&&hit->object?hit->object->instanceId:-1)!=owner)++afterErrors;++afterClickCount;}
            const int mismatch=difference(actual,expected),order=difference(actual,paint(target==2?std::vector<WorldObjectInstance>{mobile}:std::vector<WorldObjectInstance>{mobile,building})),
                repeat=difference(actual,paint(target==2?std::vector<WorldObjectInstance>{mobile}:std::vector<WorldObjectInstance>{building,mobile}));
            auto idBuilding=building,idMobile=mobile;idBuilding.instanceId=mobileId;idMobile.instanceId=buildingId;
            const int ids=difference(actual,paint(target==2?std::vector<WorldObjectInstance>{idMobile}:std::vector<WorldObjectInstance>{idMobile,idBuilding}));int hdc=0;
            if(!pan){hdc=difference(actual,paint(target==2?std::vector<WorldObjectInstance>{mobile}:std::vector<WorldObjectInstance>{building,mobile},true));++hdcChecks;}
            const bool mixed=mobileVisible>0&&buildingVisible>0;
            const int frontWrong=difference(expected,wholeFront),backWrong=difference(expected,wholeBack);
            rows<<targetName+"-"+specimenName<<','<<pose<<','<<subCell<<','<<zoom<<','<<facing<<','<<pan<<','<<nearbyWall<<','<<parts.size()<<','<<overlap
                <<','<<mobileVisible<<','<<buildingVisible<<','<<mixed<<','<<mismatch<<','<<beforeErrors<<','<<afterErrors<<','<<order
                <<','<<ids<<','<<repeat<<','<<hdc<<','<<frontWrong<<','<<backWrong<<'\n';rows.flush();
            if((mismatch||beforeErrors||afterErrors||order||ids||repeat||hdc)
                ||(pose==2&&zoom==60&&facing==0&&!pan&&(nearbyWall||target)&&(!specimen.infantry||subCell==4))){
                std::vector<std::uint32_t> sheet(width*4*height);for(int column=0;column<4;++column){const auto& f=column==0?actual:column==1?expected:column==2?wholeFront:wholeBack;
                    for(int y=0;y<height;++y)std::copy_n(f.begin()+y*width,width,sheet.begin()+y*width*4+column*width);}
                if(!writeRuntimeSmokeBmp(pathString(root/(label+"-actual-expected-front-back.bmp")),sheet.data(),width*4,height,error))return false;}
            pickDiagnostics.flush();
            if(beforeErrors||afterErrors){
                std::ofstream cacheDiagnostic(root/(label+"-pick-cache-geometry.txt"));
                const auto item=std::find_if(projected.begin(),projected.end(),[&](const auto& value){return value.object&&value.object->instanceId==mobileId;});
                if(item!=projected.end()){RECT queried{};const bool resolved=resolveRuntimeMobileObjectSpriteBounds(mobile,item->renderCenter,view,s.camera,s.renderCache,queried);
                    cacheDiagnostic<<"before center="<<item->renderCenter.x<<','<<item->renderCenter.y<<" bounds="<<item->bounds.left<<','<<item->bounds.top<<','<<item->bounds.right<<','<<item->bounds.bottom
                        <<" sprite="<<item->spriteBounds.left<<','<<item->spriteBounds.top<<','<<item->spriteBounds.right<<','<<item->spriteBounds.bottom<<" finalSpans="<<item->visibleBuildingHitSpans.size()
                        <<" directResolve="<<resolved<<" directBounds="<<queried.left<<','<<queried.top<<','<<queried.right<<','<<queried.bottom<<'\n';}
                for(const auto& part:parts){cacheDiagnostic<<"independent raw rectangle="<<part.left<<','<<part.top<<','
                    <<part.left+(part.image?part.image->width:part.original.width)<<','<<part.top+(part.image?part.image->height:part.original.height)<<'\n';}
                for(const auto& [key,value]:s.renderCache.objectShpSpritesByType)if(value.loaded&&toLowerAsciiLocal(value.resolvedImageId)==toLowerAsciiLocal(visualId))
                    cacheDiagnostic<<"query cache key="<<key<<" image="<<value.resolvedImageId<<" frame="<<value.frameIndex<<" sequence="<<value.sequenceName
                        <<" crop="<<value.image.offsetX<<','<<value.image.offsetY<<','<<value.image.width<<','<<value.image.height<<'\n';
                cacheDiagnostic.close();if(!cacheDiagnostic.good())return fail("pick cache geometry diagnostics write");
            }
            if(mismatch||beforeErrors||afterErrors||order||ids||repeat||hdc)return fail("pixels="+std::to_string(mismatch)
                +" beforePicks="+std::to_string(beforeErrors)+" afterPicks="+std::to_string(afterErrors)+" order="+std::to_string(order)
                +" IDs="+std::to_string(ids)+" repeat="+std::to_string(repeat)+" HDC="+std::to_string(hdc));
                        // This separate display-layer control documents why a body-only
            // oracle must omit Behind. It does not alter expected body owners.
            if(!target&&std::string(specimen.type)=="MTNK"&&pose==0&&zoom==48&&facing==0&&!pan&&!nearbyWall){
                const auto offStats=lastPaintStats;const auto marked=paint({building,mobile},false,false,true);const auto markerStats=lastPaintStats;
                const RuntimeAuxiliaryShpBytesCacheEntry* markerBytes=nullptr;
                for(const auto& [key,value]:cache->auxiliaryShpBytesByImage)
                    if(value.loaded&&toLowerAsciiLocal(key).find("|behind|")!=std::string::npos)markerBytes=&value;
                if(!markerBytes||sha256(markerBytes->bytes)!="ef26a68a4579fc630b1743ec0febe42b4f09182d80b0ab708f012c716f33f4e3"
                    ||offStats.behindMarkerSprites!=0||markerStats.behindMarkerSprites!=1||markerStats.behindMarkerFrame!=3)
                    return fail("rear Behind display-layer control missing original marker");
                const auto marker=RuntimeIssue25GroundOracle::frame(markerBytes->bytes,markerStats.behindMarkerFrame);
                std::vector<unsigned char> markerMask(width*height);int nonzero=0,outside=0,pickChanges=0;
                for(int y=0;y<marker.height;++y)for(int x=0;x<marker.width;++x){
                    if(!marker.pixels[static_cast<std::size_t>(y)*marker.width+x])continue;++nonzero;
                    const int px=mx-marker.canvasWidth/2+marker.x+x,py=my-8-marker.canvasHeight/2+marker.y+y;
                    if(px>=0&&py>=0&&px<width&&py<height)markerMask[static_cast<std::size_t>(py)*width+px]=1;}
                const int changed=difference(marked,actual);
                for(std::size_t p=0;p<actual.size();++p){
                    if(!markerMask[p]&&(marked[p]&0xffffffU)!=(actual[p]&0xffffffU))++outside;
                    if(!mobileMask[p]||(buildingOnly[p]&0xffffffU)==(blank[p]&0xffffffU))continue;
                    const auto* hit=findProjectedObjectAtPoint(projected,POINT{static_cast<LONG>(p%width),static_cast<LONG>(p/width)});
                    if((hit&&hit->object?hit->object->instanceId:-1)!=beforeOwners[p])++pickChanges;
                }
                std::cout<<"Issue25 Behind control: frame="<<markerStats.behindMarkerFrame<<" alpha="<<nonzero
                    <<" changed="<<changed<<" outside_original_mask="<<outside<<" pickChanges="<<pickChanges<<'\n';
                std::vector<std::uint32_t> sheet(width*3*height);for(int column=0;column<3;++column){
                    const auto& f=column==0?actual:column==1?expected:marked;
                    for(int y=0;y<height;++y)std::copy_n(f.begin()+y*width,width,sheet.begin()+y*width*3+column*width);}
                if(!writeRuntimeSmokeBmp(pathString(root/"behind-on-off-control.bmp"),sheet.data(),width*3,height,error))return false;
                if(nonzero!=214||changed!=214||outside||pickChanges)return fail("Behind raw SHP mask/picking control differs");
            }
            auto& c=coverage[targetName+"-"+specimenName];c.overlap+=overlap;c.visible+=mobileVisible;c.hidden+=buildingVisible;c.mixed+=mixed;
            if(nearbyWall||target)c.wall+=overlap;++cases;
        }
        // Native connected map walls use distinct SHP connection frames. The
        // terrain color pass must agree with the later depth owner regardless
        // of source terrain-vector order, including scenes with no Structure.
        {
            struct Wall {int x=0,y=0,frame=0;};
            const RuntimeAuxiliaryShpBytesCacheEntry* wallBytes=nullptr;const RuntimeAuxiliaryShpBytesCacheEntry* giBytes=nullptr;
            for(const auto& [key,value]:cache->auxiliaryShpBytesByImage)
                if(value.loaded&&toLowerAsciiLocal(key).find("|gafwll|")!=std::string::npos)wallBytes=&value;
            for(const auto& [key,value]:cache->objectShpBytesByImage)
                if(value.loaded&&toLowerAsciiLocal(key).find("|gi|")!=std::string::npos)giBytes=&value;
            if(!wallBytes||!giBytes)return fail("connected-wall original SHP sources absent");
            auto ordinaryCache=std::make_unique<RuntimeMapRenderCache>(),reverseCache=std::make_unique<RuntimeMapRenderCache>();
            std::ofstream multiRows(root/"issue25-multi-wall-order.csv");
            multiRows<<"axis,quarter,subcell,zoom,facing,pan,wall_overlap,wall_color_witness,mobile_overlap,mobile_visible,mobile_hidden,mixed,pixels,terrain_order,hdc,before_picks,after_picks,reverse_picks\n";
            std::uint64_t wallWitnessTotal=0,mobileOverlapTotal=0,mixedTotal=0;int multiCases=0;
            for(int axis:{0,1})for(int quarter:{0,1,2})for(int sub:{2,4})for(int zoom:{48,60})for(int facing:{0,64})for(bool pan:{false,true}){
                label="multi-map-wall-a"+std::to_string(axis)+"-q"+std::to_string(quarter)+"-s"+std::to_string(sub)
                    +"-z"+std::to_string(zoom)+"-f"+std::to_string(facing)+"-pan"+std::to_string(pan);
                const std::array<Wall,2> walls=axis==0?std::array<Wall,2>{{{20,20,4},{20,21,1}}}
                    :std::array<Wall,2>{{{20,20,2},{21,20,8}}};
                WorldObjectInstance mobile;mobile.kind=WorldObjectKind::Infantry;mobile.type="E1";mobile.instanceId=mobileId;
                mobile.owner="Americans";const double edgeOffset=quarter==0?.625:quarter==1?1.125:1.375;
                mobile.renderX=axis==0?20+edgeOffset:22;mobile.renderY=axis==0?22:20+edgeOffset;
                mobile.x=static_cast<int>(std::floor(mobile.renderX));mobile.y=static_cast<int>(std::floor(mobile.renderY));mobile.cell=runtimeCellKey(mobile.x,mobile.y);
                mobile.hasRenderPosition=mobile.renderMoveActive=true;
                mobile.subCell=sub;mobile.facing=facing;mobile.movementAnimationDistance=0;
                mobile.alive=mobile.poweredOperational=true;mobile.health=runtimeMaxHealthForObject(s,mobile);mobile.mission="Guard";
                const double rx=mobile.renderX+.25,ry=mobile.renderY+(sub==2?-.25:.25);
                s.camera.initialized=true;s.camera.tileWidth=zoom;s.camera.tileHeight=zoom/2;
                s.camera.worldOffsetX=-width/2+(pan?29:0);s.camera.worldOffsetY=21*zoom/2-height/2+(pan?-17:0);
                const auto prepare=[&](const std::vector<Wall>& selected,bool withMobile){
                    s.world.accessTerrainCells()=plainTerrain;s.mapFacts=plainFacts;view.animationTick=17;
                    s.world.accessObjects()=withMobile?std::vector<WorldObjectInstance>{mobile}:std::vector<WorldObjectInstance>{};
                    for(const auto& wall:selected){auto& cell=s.world.accessTerrainCells().at(static_cast<std::size_t>(wall.y-10)*27+wall.x-10);
                        cell.hasOverlay=true;cell.overlayIndex=wallRule->listIndex;cell.overlayType="GAFWLL";cell.overlayImage="GAFWLL";cell.overlayFrame=wall.frame;
                        auto& fact=s.mapFacts.cells.at(s.mapFacts.cellIndexLookup.at(cell.cellIndex512));fact.hasOverlay=true;
                        fact.overlay.cellIndex=cell.cellIndex512;fact.overlay.x=wall.x;fact.overlay.y=wall.y;fact.overlay.overlayIndex=cell.overlayIndex;
                        fact.overlay.type=cell.overlayType;fact.overlay.image=cell.overlayImage;fact.overlay.frameIndex=fact.overlay.frame=wall.frame;
                        s.mapFacts.contentHash+=static_cast<std::uint64_t>(cell.cellIndex512)*64+wall.frame+1;}
                    initializeRuntimeNavigation(s);
                    // Pin the authored fixture pose after navigation normalizes live movement without an active segment.
                    if(withMobile)s.world.accessObjects().front()=mobile;
                    s.structureFootprints=buildRuntimeStructureFootprints(s);s.tick=17;invalidateRuntimeObjectProjectionCache(s);
                };
                const std::vector<Wall> pair(walls.begin(),walls.end());prepare(pair,true);
                const auto projected=runtimeProjectedObjectsForViewport(s,viewport);
                std::vector<int> before(width*height,-2);
                for(const auto& item:projected){RECT clip{};if(!IntersectRect(&clip,&item.spriteBounds,&viewport))continue;
                    for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x){const auto p=static_cast<std::size_t>(y)*width+x;
                        const auto* hit=findProjectedObjectAtPoint(projected,POINT{x,y});before[p]=hit&&hit->object?hit->object->instanceId:-1;}}
                const auto paint=[&](const std::vector<Wall>& selected,bool withMobile,bool reversed=false,bool hdc=false){
                    GameWorld drawing=s.world;drawing.accessTerrainCells()=plainTerrain;MapFacts facts=plainFacts;
                    drawing.accessObjects()=withMobile?std::vector<WorldObjectInstance>{mobile}:std::vector<WorldObjectInstance>{};
                    for(const auto& wall:selected){auto& cell=drawing.accessTerrainCells().at(static_cast<std::size_t>(wall.y-10)*27+wall.x-10);
                        cell.hasOverlay=true;cell.overlayIndex=wallRule->listIndex;cell.overlayType=cell.overlayImage="GAFWLL";cell.overlayFrame=wall.frame;
                        auto& fact=facts.cells.at(facts.cellIndexLookup.at(cell.cellIndex512));fact.hasOverlay=true;fact.overlay.cellIndex=cell.cellIndex512;
                        fact.overlay.x=wall.x;fact.overlay.y=wall.y;fact.overlay.overlayIndex=cell.overlayIndex;fact.overlay.type=fact.overlay.image="GAFWLL";
                        fact.overlay.frameIndex=fact.overlay.frame=wall.frame;facts.contentHash+=static_cast<std::uint64_t>(cell.cellIndex512)*64+wall.frame+1;}
                    if(reversed)std::reverse(drawing.accessTerrainCells().begin(),drawing.accessTerrainCells().end());
                    auto context=view;context.mapFacts=&facts;context.seeHiddenObjects=false;
                    if(hdc){context.surfacePixels=nullptr;context.surfaceWidth=context.surfaceHeight=0;}
                    auto camera=s.camera;auto& drawingCache=reversed?*reverseCache:*ordinaryCache;
                    if(!drawRuntimeMap(surface.dc,viewport,drawing,context,camera,drawingCache).rendered)throw std::runtime_error("connected-wall original draw");
                    GdiFlush();return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);
                };
                const auto blank=paint({},false),first=paint({walls[0]},false),second=paint({walls[1]},false),mobileOnly=paint({},true);
                std::array<std::vector<int>,2> depths;for(auto& z:depths)z.assign(width*height,std::numeric_limits<int>::max());
                for(int n=0;n<2;++n){const auto& wall=walls[n];const auto f=RuntimeIssue25GroundOracle::frame(wallBytes->bytes,wall.frame);
                    const int cx=static_cast<int>(std::lround((wall.x-wall.y)*zoom/2.0))-s.camera.worldOffsetX;
                    const int cy=static_cast<int>(std::lround((wall.x+wall.y)*zoom/4.0))-s.camera.worldOffsetY;
                    const int left=cx-f.canvasWidth/2+f.x,top=cy-f.canvasHeight/2+f.y,nativeTop=(wall.x+wall.y)*15-f.canvasHeight/2+f.y;
                    for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x){if(!f.pixels[static_cast<std::size_t>(y)*f.width+x])continue;
                        const int px=left+x,py=top+y;if(px<0||py<0||px>=width||py>=height)continue;
                        depths[n][static_cast<std::size_t>(py)*width+px]=gradient(nativeTop+f.height,nativeTop+y,1);}}
                auto expected=blank;std::vector<int> sceneZ(width*height,std::numeric_limits<int>::max());
                int wallOverlap=0,wallWitness=0;
                for(std::size_t p=0;p<expected.size();++p){for(int n=0;n<2;++n)if(depths[n][p]<sceneZ[p]){
                        sceneZ[p]=depths[n][p];expected[p]=n?second[p]:first[p];}
                    if(depths[0][p]!=std::numeric_limits<int>::max()&&depths[1][p]!=std::numeric_limits<int>::max()){
                        ++wallOverlap;if((first[p]&0xffffffU)!=(second[p]&0xffffffU))++wallWitness;}}
                const int bucket=((((256-facing)%256)*8)+128)/256%8;const auto f=RuntimeIssue25GroundOracle::frame(giBytes->bytes,8+bucket*6);
                bool poseInput=false;
                for(const auto& [key,value]:ordinaryCache->objectShpSpritesByType)
                    if(value.loaded&&toLowerAsciiLocal(value.resolvedImageId)=="gi"&&value.frameIndex==8+bucket*6
                        &&value.sequenceName=="Walk"&&value.image.width==f.width&&value.image.height==f.height
                        &&value.image.offsetX==f.x&&value.image.offsetY==f.y&&value.image.pixels==f.pixels)poseInput=true;
                if(!poseInput)return fail("connected-wall Walk pose differs from independent original input");
                const int cx=static_cast<int>(std::lround((rx-ry)*zoom/2.0))-s.camera.worldOffsetX;
                const int cy=static_cast<int>(std::lround((rx+ry)*zoom/4.0))-s.camera.worldOffsetY,nativeY=static_cast<int>(std::lround((rx+ry)*15));
                const int left=cx-f.canvasWidth/2+f.x,top=cy+zoom/4-f.canvasHeight+f.y,bottom=nativeY+top-cy+f.height;
                std::vector<unsigned char> mask(width*height),visible(width*height);int mobileOverlap=0,mobileVisible=0,mobileHidden=0;
                for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x){if(!f.pixels[static_cast<std::size_t>(y)*f.width+x])continue;
                    const int px=left+x,py=top+y;if(px<0||py<0||px>=width||py>=height)continue;const auto p=static_cast<std::size_t>(py)*width+px;mask[p]=1;
                    const auto wallColor=expected[p]&0xffffffU;
                    visible[p]=gradient(bottom,nativeY+py-cy,12)<sceneZ[p];if(visible[p])expected[p]=mobileOnly[p];
                    if(sceneZ[p]!=std::numeric_limits<int>::max()&&(mobileOnly[p]&0xffffffU)!=wallColor){
                        ++mobileOverlap;visible[p]?++mobileVisible:++mobileHidden;}}
                const auto actual=paint(pair,true),reversed=paint(pair,true,true);int hdc=0;
                if(!pan){hdc=difference(actual,paint(pair,true,false,true));++hdcChecks;}
                int beforeErrors=0,afterErrors=0,reverseErrors=0;
                for(std::size_t p=0;p<mask.size();++p){if(!mask[p]||sceneZ[p]==std::numeric_limits<int>::max())continue;
                    const int owner=visible[p]?mobileId:-1;if(before[p]!=owner)++beforeErrors;
                    const auto* hit=findProjectedObjectAtPoint(projected,POINT{static_cast<LONG>(p%width),static_cast<LONG>(p/width)});
                    if((hit&&hit->object?hit->object->instanceId:-1)!=owner)++afterErrors;}
                std::reverse(s.world.accessTerrainCells().begin(),s.world.accessTerrainCells().end());initializeRuntimeNavigation(s);
                s.world.accessObjects().front()=mobile;invalidateRuntimeObjectProjectionCache(s);
                const auto reverseProjected=runtimeProjectedObjectsForViewport(s,viewport);
                for(std::size_t p=0;p<mask.size();++p){if(!mask[p]||sceneZ[p]==std::numeric_limits<int>::max())continue;
                    const auto* hit=findProjectedObjectAtPoint(reverseProjected,POINT{static_cast<LONG>(p%width),static_cast<LONG>(p/width)});
                    if((hit&&hit->object?hit->object->instanceId:-1)!=(visible[p]?mobileId:-1))++reverseErrors;}
                const int mismatch=difference(actual,expected),order=difference(actual,reversed);const bool mixed=mobileVisible>0&&mobileHidden>0;
                multiRows<<axis<<','<<quarter<<','<<sub<<','<<zoom<<','<<facing<<','<<pan<<','<<wallOverlap<<','<<wallWitness<<','<<mobileOverlap
                    <<','<<mobileVisible<<','<<mobileHidden<<','<<mixed<<','<<mismatch<<','<<order<<','<<hdc<<','<<beforeErrors<<','<<afterErrors<<','<<reverseErrors<<'\n';multiRows.flush();
                if((!pan&&quarter==1&&sub==4&&facing==0)||(mismatch||order||hdc||beforeErrors||afterErrors||reverseErrors)){
                    std::vector<std::uint32_t> sheet(width*3*height);for(int column=0;column<3;++column){const auto& pixels=column==0?actual:column==1?expected:reversed;
                        for(int y=0;y<height;++y)std::copy_n(pixels.begin()+y*width,width,sheet.begin()+y*width*3+column*width);}
                    if(!writeRuntimeSmokeBmp(pathString(root/(label+"-actual-expected-reversed.bmp")),sheet.data(),width*3,height,error))return false;}
                if(mismatch||order||hdc||beforeErrors||afterErrors||reverseErrors)return fail("connected-wall pixels="+std::to_string(mismatch)
                    +" terrainOrder="+std::to_string(order)+" HDC="+std::to_string(hdc)+" beforePicks="+std::to_string(beforeErrors)
                    +" afterPicks="+std::to_string(afterErrors)+" reversedPicks="+std::to_string(reverseErrors));
                wallWitnessTotal+=wallWitness;mobileOverlapTotal+=mobileOverlap;mixedTotal+=mixed;++multiCases;
            }
            multiRows.close();if(!multiRows.good())return fail("connected-wall evidence write");
            if(!wallWitnessTotal||!mobileOverlapTotal||!mixedTotal)return fail("connected-wall lacks original color/occlusion witnesses");
            std::cout<<"Issue25 connected map walls: cases="<<multiCases<<" differentWallColorOverlap="<<wallWitnessTotal
                <<" mobileOverlap="<<mobileOverlapTotal<<" mixedFrames="<<mixedTotal<<'\n';
            cases+=multiCases;
        }
        rows.close();sources.close();pickDiagnostics.close();if(!rows.good()||!sources.good()||!pickDiagnostics.good())return fail("evidence write");
        std::ofstream complete(root/"issue25-coverage.csv");complete<<"mobile,overlap,mobile_visible,building_visible,mixed_frames,wall_overlap\n";
        for(const auto& [name,c]:coverage){complete<<name<<','<<c.overlap<<','<<c.visible<<','<<c.hidden<<','<<c.mixed<<','<<c.wall<<'\n';
            if(!c.overlap||!c.visible||!c.hidden||!c.wall)return fail(name+" missing stock visible/hidden/near-wall witnesses");}
        complete.close();if(!complete.good())return fail("coverage write");
        const auto mixed=std::accumulate(coverage.begin(),coverage.end(),std::uint64_t{0},[](auto total,const auto& item){return total+item.second.mixed;});
        std::uint64_t playerWallMixed=0,mapWallMixed=0;
        for(const auto& [name,c]:coverage){if(name.starts_with("player-wall-"))playerWallMixed+=c.mixed;
            if(name.starts_with("map-wall-"))mapWallMixed+=c.mixed;}
        if(!mixed||!playerWallMixed||!mapWallMixed)return fail("missing genuine same-frame bidirectional wall/mobile occlusion");
        std::cout<<"Issue25 ground occlusion: cases="<<cases<<" HDC="<<hdcChecks<<" qualifiedDrive="<<qualificationChecks
            <<" beforePaintClicks="<<beforeClickCount<<" afterPaintOverlapClicks="<<afterClickCount<<" mixedFrames="<<mixed<<'\n';
        error.clear();return true;
    }catch(const std::exception& detail){return fail(detail.what());}
}
