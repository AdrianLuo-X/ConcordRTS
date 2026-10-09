// Issue 25.3: independent source-pixel and scene-Z composition for neutral
// map occluders. Include after RuntimeGroundOcclusionIssue25Smoke.inl.
// The photographed material is selected only after original-byte identification;
// overlay SHP walls and TMP extra images must not share an assumed depth rule.
namespace RuntimeIssue25NeutralOracle {
using namespace RuntimeBuildingDepthOracle;
constexpr int Width=640,Height=480,NoDepth=std::numeric_limits<int>::max();
struct Surface {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;
    std::uint32_t* pixels=nullptr;
    Surface(){
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        dc=CreateCompatibleDC(nullptr);void* raw=nullptr;
        if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
        pixels=static_cast<std::uint32_t*>(raw);
        if(bitmap&&pixels)previous=SelectObject(dc,bitmap);
    }
    ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    std::vector<std::uint32_t> copy() const {
        GdiFlush();return {pixels,pixels+static_cast<std::size_t>(Width)*Height};
    }
};
struct Layer {
    std::string name;
    int owner=-1;
    double authoredSort=0;
    bool terrainStage=false,writeZ=true;
    std::vector<unsigned char> mask;
    std::vector<int> depth;
    std::vector<std::uint32_t> color;
    Layer():mask(static_cast<std::size_t>(Width)*Height),
        depth(static_cast<std::size_t>(Width)*Height,NoDepth){}
};
struct Expected {
    std::vector<std::uint32_t> colors;
    std::vector<int> owners,sceneZ;
};
inline int difference(const std::vector<std::uint32_t>& a,
    const std::vector<std::uint32_t>& b){
    if(a.size()!=b.size())throw std::runtime_error("neutral-wall image dimensions differ");
    int result=0;for(std::size_t p=0;p<a.size();++p)result+=(a[p]&0xffffffU)!=(b[p]&0xffffffU);
    return result;
}
inline int retailGradient(int bottom,int row,int adjustment){
    const int base=32768-bottom-adjustment;
    return (base/3)*3-(bottom-1-row)/3;
}
inline void shpMask(Layer& layer,const OriginalFrame& frame,int left,int top,
    const std::function<int(int,int)>& depth){
    for(int y=0;y<frame.height;++y)for(int x=0;x<frame.width;++x){
        if(!frame.pixels[static_cast<std::size_t>(y)*frame.width+x])continue;
        const int px=left+x,py=top+y;
        if(px<0||py<0||px>=Width||py>=Height)continue;
        const auto p=static_cast<std::size_t>(py)*Width+px;
        layer.mask[p]=1;layer.depth[p]=depth(x,y);
    }
}
inline std::vector<std::uint32_t> shpColors(const OriginalFrame& frame,int left,int top,
    const std::vector<RuntimePaletteColor>& palette){
    if(palette.size()!=256)throw std::runtime_error("neutral raw color palette size");
    std::vector<std::uint32_t> colors(static_cast<std::size_t>(Width)*Height);
    for(int y=0;y<frame.height;++y)for(int x=0;x<frame.width;++x){
        const auto index=frame.pixels[static_cast<std::size_t>(y)*frame.width+x];
        const int px=left+x,py=top+y;if(!index||px<0||py<0||px>=Width||py>=Height)continue;
        const auto& color=palette[index];colors[static_cast<std::size_t>(py)*Width+px]=
            (std::uint32_t(color.r)<<16)|(std::uint32_t(color.g)<<8)|color.b;
    }
    return colors;
}
inline Layer overlayWall(const OriginalFrame& frame,int centerX,int centerY,
    int nativeCenterY,int cellX,int cellY){
    Layer layer;layer.name="original-overlay-wall";layer.terrainStage=true;
    layer.authoredSort=double(cellX+cellY)*256;
    const int left=centerX-frame.canvasWidth/2+frame.x;
    const int top=centerY-frame.canvasHeight/2+frame.y;
    const int nativeTop=nativeCenterY-frame.canvasHeight/2+frame.y;
    shpMask(layer,frame,left,top,[&](int,int y){
        // Cell DrawOverlay Wall flags0x4e00 center, gradient2, scene-Z write.
        return retailGradient(nativeTop+frame.height,nativeTop+y,1);
    });
    return layer;
}
inline Layer buildingBody(const OriginalFrame& frame,const OriginalFrame& shape,
    int centerX,int centerY,int nativeCenterY,int foundationWidth,int foundationHeight,
    int moveX,int moveY,int normalZAdjust,int owner,double authoredSort){
    Layer layer;layer.name="original-building-body";layer.owner=owner;layer.authoredSort=authoredSort;
    const int left=centerX-frame.canvasWidth/2+frame.x;
    const int top=centerY-frame.canvasHeight/2+frame.y;
    const int nativeTop=nativeCenterY-frame.canvasHeight/2+frame.y;
    const int u0=frame.x-frame.canvasWidth/2+198-30*(foundationWidth-foundationHeight)+moveX;
    const int v0=frame.y-frame.canvasHeight/2+446-15*(foundationWidth+foundationHeight-2)+moveY;
    const int clippedRows=std::min(frame.height,shape.height-v0)-std::max(0,-v0);
    shpMask(layer,frame,left,top,[&](int x,int y){
        const int u=u0+x,v=v0+y;
        if(u<0||v<0||u>=shape.width||v>=shape.height)
            return retailGradient(nativeTop+frame.height,nativeTop+y,1-normalZAdjust);
        const int raw=shape.pixels[static_cast<std::size_t>(v)*shape.width+u];
        int transformed=raw?raw-65:0;
        if(transformed>127)transformed-=256;if(transformed< -128)transformed+=256;
        return 32768-nativeTop-clippedRows+normalZAdjust-1-transformed;
    });
    return layer;
}
inline Layer buildingAnimation(const OriginalFrame& frame,int centerX,int centerY,
    int nativeCenterY,int offsetX,int offsetY,int zAdjust,int yDrawOffset,
    int owner,double authoredSort){
    Layer layer;layer.name="original-building-animation";layer.owner=owner;layer.authoredSort=authoredSort;layer.writeZ=false;
    const int left=centerX+offsetX-frame.canvasWidth/2+frame.x;
    const int top=centerY+offsetY+yDrawOffset-frame.canvasHeight/2+frame.y;
    const int nativeTop=nativeCenterY+offsetY+yDrawOffset-frame.canvasHeight/2+frame.y;
    shpMask(layer,frame,left,top,[&](int,int y){
        return retailGradient(nativeTop+frame.height,nativeTop+y,1-zAdjust-yDrawOffset);
    });
    return layer;
}
inline Expected compose(const std::vector<std::uint32_t>& blank,std::vector<Layer> layers){
    Expected result;result.colors=blank;result.owners.assign(blank.size(),-1);
    result.sceneZ.assign(blank.size(),NoDepth);
    std::stable_sort(layers.begin(),layers.end(),[](const Layer& a,const Layer& b){
        if(a.terrainStage!=b.terrainStage)return a.terrainStage;
        return a.authoredSort<b.authoredSort;
    });
    for(const auto& layer:layers){
        if(layer.mask.size()!=blank.size()||layer.depth.size()!=blank.size()||layer.color.size()!=blank.size())
            throw std::runtime_error("neutral scene raw-layer/color dimensions differ");
        for(std::size_t p=0;p<blank.size();++p){
            if(!layer.mask[p]||layer.depth[p]>=result.sceneZ[p])continue;
            result.colors[p]=layer.color[p];result.owners[p]=layer.owner;
            if(layer.writeZ)result.sceneZ[p]=layer.depth[p];
        }
    }
    return result;
}
inline Bytes pinnedSlice(const std::filesystem::path& archive,const char* archiveHash,
    std::size_t offset,std::size_t length,const char* sourceHash){
    const auto source=readPinned(archive,archiveHash);
    if(offset>source.size()||length>source.size()-offset)
        throw std::runtime_error("neutral original MIX slice exceeds audited archive");
    Bytes result(source.begin()+offset,source.begin()+offset+length);
    if(sha256(result)!=sourceHash)throw std::runtime_error("neutral original SHP slice fingerprint differs");
    return result;
}
inline std::pair<int,int> originalOverlayIdentity(const IniFile& rules,const std::string& name){
    const auto* entries=rules.getSection("OverlayTypes");
    if(!entries)throw std::runtime_error("original OverlayTypes unavailable");
    int ordinal=0;
    for(const auto& [key,id]:*entries){
        if(key.empty()||!std::all_of(key.begin(),key.end(),[](unsigned char c){return std::isdigit(c);}))continue;
        if(toLowerAsciiLocal(trimAsciiLocal(id))==toLowerAsciiLocal(name))return {ordinal,std::stoi(key)};
        ++ordinal;
    }
    throw std::runtime_error("original overlay ID absent: "+name);
}
}



bool runRuntimeNeutralWallIssue25Smoke(const RuntimeConfig& config,std::string& error){
    using namespace RuntimeIssue25NeutralOracle;
    using RuntimeIssue25GroundOracle::frame;
    std::cout<<std::unitbuf;
    const char* requested=std::getenv("CONCORD_ISSUE25_NEUTRAL_WALL_OUTPUT");
    const std::filesystem::path output=requested&&*requested?requested:"build/smoke/feature-set-25/03-neutral-wall";
    std::filesystem::create_directories(output);std::string label;
    const auto fail=[&](const std::string& why){error="Issue25 neutral wall "+label+": "+why;std::cout<<error<<'\n';return false;};
    try{
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=config;s.config.playerSlots.clear();s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.shortGame=false;
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        if(!s.report.success)return fail("original startup");
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;
        RuntimeHumanHouseAuthority authority;authority.owner="Americans";authority.canonicalOwner="americans";authority.localFacade=true;
        s.humanHouseAuthorities={authority};s.ownerRemapColors["americans"]=RGB(235,210,45);
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        const auto cwd=std::filesystem::current_path();
        RuntimeIssue25GroundOracle::retailGroundParameters(cwd);
        const auto originalRules=readPinned(cwd/"local_data/resources/mix_exports/ra2md/localmd/rulesmd.ini",
            "06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05");
        const auto originalArt=readPinned(cwd/"local_data/resources/mix_exports/ra2md/localmd/artmd.ini",
            "e1f0378394313c04ebbd5073f47785ee3e46f1b3c62d65724e8f3c310ee7ba31");
        IniFile ri,ai;
        if(!ri.loadFromString(std::string(originalRules.begin(),originalRules.end()))
            ||!ai.loadFromString(std::string(originalArt.begin(),originalArt.end())))return fail("original INIs");
        if(originalOverlayIdentity(ri,"NAWALL")!=std::pair<int,int>{26,27}
            ||originalOverlayIdentity(ri,"CAKRMW")!=std::pair<int,int>{240,244}
            ||originalOverlayIdentity(ri,"LUNRK1")!=std::pair<int,int>{244,248}
            ||toLowerAsciiLocal(ri.getValue("NAWALL","Wall"))!="yes"
            ||ai.getValue("NAWALL","ToOverlay")!="NAWALL")return fail("independent original overlay identity");
        const auto wallBytes=pinnedSlice(cwd/"local_data/resources/mix_exports/ra2/temperat.mix",
            "e8cab1997e9aeba5a44570e38a7de00640a8d9124f458bfed6980c63861d0c78",2661750,66488,
            "31e694ae51865eb378dcea863a7671afed4e5406617ada5598a59afa56d23c17");
        const auto bodyBytes=pinnedSlice(cwd/"local_data/resources/mix_exports/ra2/generic.mix",
            "dbe8d76687c1787e4f17396cdf6c88fe507b916031261492fa6375fe944f2b70",3995822,9184,
            "5035c5cce5e7a0036b64d451cbe175e012083e3b2ed608e4e70e40a243f707e4");
        const auto activeBytes=pinnedSlice(cwd/"local_data/resources/mix_exports/ra2/generic.mix",
            "dbe8d76687c1787e4f17396cdf6c88fe507b916031261492fa6375fe944f2b70",4005006,52400,
            "d5e7e48d8dd14e2a3e6f872cbaf7796c55ebdd435f256492571010811f495478");
        const auto paletteBytes=readPinned(cwd/"local_data/resources/mix_exports/extracted_palettes/cache_unittem.pal",
            "ed785e62eed291480f3198dd44f6b656ebe3a9b75e9f641944d710abc6bde3e3");
        if(paletteBytes.size()!=768||*std::max_element(paletteBytes.begin(),paletteBytes.end())>63)
            return fail("original 6-bit unittem palette");
        std::vector<RuntimePaletteColor> originalPalette;
        for(std::size_t index=0;index<256;++index)originalPalette.push_back({
            static_cast<unsigned char>(paletteBytes[index*3]*4),
            static_cast<unsigned char>(paletteBytes[index*3+1]*4),
            static_cast<unsigned char>(paletteBytes[index*3+2]*4)});
        // This conversion supplies color only. SHP masks, per-layer depth,
        // scene-Z writes and ownership remain the independent retail oracle.

        const auto buildingPalette=runtimeOwnerRemappedUiPalette(originalPalette,RGB(235,210,45));
        const auto shape=decodeFrameZero(retailTemplate(cwd)),body=decodeFrameZero(bodyBytes),animation=frame(activeBytes,4);
        if(little(wallBytes,6,2)!=96||frame(wallBytes,5).canvasWidth!=80||frame(wallBytes,5).canvasHeight!=64
            ||std::array<int,4>{body.x,body.y,body.width,body.height}!=std::array<int,4>{61,4,37,85}
            ||std::array<int,4>{animation.x,animation.y,animation.width,animation.height}!=std::array<int,4>{55,4,44,85})
            return fail("independent original wall/body/active frame headers");
        if(ai.getValue("NATSLA","Foundation")!="1x1"||ai.getValue("NATSLA","Height")!="5"
            ||ai.getValue("NATSLA","ActiveAnim")!="NATSLA_A"||ai.getValue("NATSLA","ActiveAnimZAdjust")!="-30"
            ||ai.getValue("NATSLA_A","Rate")!="450"||ai.getValue("NATSLA_A","LoopStart")!="0"
            ||ai.getValue("NATSLA_A","LoopEnd")!="9"||!ai.getValue("NATSLA_A","YDrawOffset").empty()
            ||!ai.getValue("NATSLA","ActiveAnimYSort").empty()
            ||!ai.getValue("NATSLA","ActiveAnimX").empty()||!ai.getValue("NATSLA","ActiveAnimY").empty())
            return fail("independent NATSLA body/active slot/tick17 frame4 contract");
        if(ri.getValue("TESLA","Image")!="NATSLA"||ri.getValue("TESLA","BuildCat")!="Combat"
            ||ri.getValue("TESLA","Strength")!="600"||ri.getValue("TESLA","Power")!="-75"
            ||toLowerAsciiLocal(ri.getValue("TESLA","Powered"))!="yes"
            ||ai.getValue("NATSLA","Remapable")!="no"
            ||!ai.getValue("NATSLA","TerrainPalette").empty()||!ai.getValue("NATSLA","AnimPalette").empty()
            ||!ai.getValue("NATSLA_A","TerrainPalette").empty()||!ai.getValue("NATSLA_A","AnimPalette").empty())
            return fail("independent stock TESLA rules / NATSLA ordinary palette identity");
        const auto* teslaRule=s.rules.findBuildingType("TESLA");
        if(!teslaRule||teslaRule->image!="NATSLA"||teslaRule->buildCat!="Combat"||teslaRule->strength!=600
            ||teslaRule->power!= -75||!teslaRule->powered)
            return fail("loaded stock TESLA rule identity");
        const auto* natslaArt=s.art.findArtType("NATSLA");
        if(!natslaArt||natslaArt->foundation!="1x1"||natslaArt->normalZAdjust
            ||natslaArt->zShapePointMove!=std::array<int,2>{}||!natslaArt->bibShape.empty())
            return fail("stock NATSLA body template contract");
        const auto metadata=runtimeTerrainMetadataContext(s);int clearTile=-1;
        const int clearSet=s.theater.getGeneralTileSetIndex("ClearTile");
        for(int tile=0;tile<static_cast<int>(s.theater.getTotalTileSlots());++tile){
            TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;
            if(s.theater.resolveTileIndex(tile).tileSetIndex==clearSet
                &&resolveRuntimeTerrainSubTileLandType(cell,metadata,s.renderCache)==RuntimeTerrainLandType::Clear
                &&resolveRuntimeTerrainSubTileRampType(cell,metadata,s.renderCache)==0){clearTile=tile;break;}
        }
        if(clearTile<0)return fail("original flat Clear TMP");
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.mapFacts={};
        for(int y=10;y<=36;++y)for(int x=10;x<=36;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=clearTile;cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
            MapCellFacts fact;fact.x=x;fact.y=y;fact.cellIndex=cell.cellIndex512;fact.hasTerrain=true;
            fact.terrain.x=x;fact.terrain.y=y;fact.terrain.tileIndex=clearTile;fact.terrain.subTileIndex=0;fact.terrainClass="clear";
            s.mapFacts.cellIndexLookup.emplace(fact.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(fact);
        }
        s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();s.mapFacts.contentHash=0x25030001;s.mapFactsLoaded=true;
        const auto plainTerrain=s.world.getTerrainCells();const auto plainFacts=s.mapFacts;
        Surface surface;const RECT viewport{0,0,Width,Height};if(!surface.dc||!surface.pixels)return fail("DIB");
        RuntimeMapRenderContext view;view.rules=&s.rules;view.art=&s.art;view.theater=&s.theater;view.mapFacts=&s.mapFacts;
        view.assetRootPath=runtimeAssetRootPath(s.config);view.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
        view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);view.palettePath=runtimePalettePath(s.config,s.report.map);
        view.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);view.ownerRemapColors=&s.ownerRemapColors;
        view.animationTick=17;view.fixedTickHz=30;view.useTileTextures=view.shadowsEnabled=false;view.seeHiddenObjects=false;
        view.surfacePixels=surface.pixels;view.surfaceWidth=Width;view.surfaceHeight=Height;
        if(!runRuntimeNeutralWallIdentityIssue25Contract(view,error))return false;
        auto cache=std::make_unique<RuntimeMapRenderCache>(),reverseCache=std::make_unique<RuntimeMapRenderCache>();
        std::ofstream rows(output/"neutral-wall.csv"),sources(output/"sources.csv"),coverageLog(output/"coverage.csv"),clickErrors(output/"click-errors.csv");
        if(!rows||!sources||!coverageLog||!clickErrors)return fail("evidence files");
        rows<<"type,axis,damage,pose,zoom,pan,wall_color_overlap,object_overlap,visible,hidden,pixels,order,ids,repeat,hdc,before_picks,after_picks,reversed_picks\n";
        sources<<"role,source,sha256,frame\n";
        clickErrors<<"case,x,y,expected_owner,before_owner,after_owner,reverse_owner,wall0_mask,wall0_depth,wall1_mask,wall1_depth,body_mask,body_depth,active_mask,active_depth,mobile_mask,mobile_depth,scene_z,actual_rgb,expected_rgb\n";
        sources<<"map-wall,ra2/temperat.mix!NTWALL.SHP,"<<sha256(wallBytes)<<",5/10/15/0/21/26\n";
        sources<<"palette,original-unittem.pal,"<<sha256(paletteBytes)<<",6-bit-DAC-shift2\n";
        sources<<"body,ra2/generic.mix!NGTSLA.SHP,"<<sha256(bodyBytes)<<",0\n";
        sources<<"active,ra2/generic.mix!NGTSLA_A.SHP,"<<sha256(activeBytes)<<",4\n";
        sources<<"registry,original-rulesmd.ini,"<<sha256(originalRules)<<",NAWALL-ordinal26-key27\n";
        sources<<"building-rule,original-rulesmd.ini[TESLA],"<<sha256(originalRules)<<",Image=NATSLA/Combat/Strength600/Power-75\n";
        sources<<"map_identity,unconfirmed,not-inferred-from-current-neutral-free-default,0\n";sources.flush();
        struct Wall {int x,y,frame;};
        struct Counts {std::uint64_t overlap=0,visible=0,hidden=0,mixed=0,wallColors=0;};
        std::map<std::string,Counts> coverage;int cases=0,hdcChecks=0;std::uint64_t beforeClicks=0,afterClicks=0;
        RuntimeSnapshotActiveStateScope scope(s);
        struct Scene {int kind,axis,damage,pose,zoom;bool pan;};std::vector<Scene> scenes;
        for(int kind=0;kind<4;++kind)for(int axis:{0,1})for(int damage:{0,1})for(int pose:{0,1})
            for(int zoom:{48,60})for(bool pan:{false,true})scenes.push_back({kind,axis,damage,pose,zoom,pan});
        for(int axis:{0,1})for(int pose:{0,1})for(int zoom:{48,60})for(bool pan:{false,true})
            scenes.push_back({4,axis,0,pose,zoom,pan});
        for(const auto& scene:scenes){
            const bool hasMobile=scene.kind!=3,hasBuilding=scene.kind>=3,infantry=scene.kind==2;
            const std::string type=scene.kind==0||scene.kind==4?"FV":scene.kind==1?"MTNK":scene.kind==2?"E1":"NATSLA";
            const std::string group=scene.kind==4?"FV+NATSLA":type;
            label=group+"-axis"+std::to_string(scene.axis)+"-damage"+std::to_string(scene.damage)
                +"-pose"+std::to_string(scene.pose)+"-z"+std::to_string(scene.zoom)+"-pan"+std::to_string(scene.pan);
            const int zoom=scene.zoom;const int connection=scene.axis?10:5;
            const std::array<Wall,2> walls{{{20,20,scene.damage?0:connection},
                {20+(scene.axis?1:0),20+(scene.axis?0:1),scene.damage?16+connection:15}}};
            WorldObjectInstance building;building.instanceId=25301;building.kind=WorldObjectKind::Structure;
            building.type="TESLA";building.owner="Americans";building.alive=building.poweredOperational=true;
            building.x=scene.pose?21:(scene.axis?20:19);building.y=scene.pose?21:(scene.axis?19:20);
            // The triple control shares the independently audited TESLA rear/front poses.
            building.cell=runtimeCellKey(building.x,building.y);building.health=runtimeMaxHealthForObject(s,building);
            building.mission="Guard";building.weaponCooldownTicksRemaining=100000;
            WorldObjectInstance mobile;mobile.instanceId=25302;mobile.kind=infantry?WorldObjectKind::Infantry:WorldObjectKind::Unit;
            mobile.type=hasMobile?type:"FV";mobile.owner="Americans";mobile.alive=mobile.poweredOperational=true;
            mobile.renderX=scene.pose?(scene.axis?21.75:21.375):(scene.axis?20.25:19.75);
            mobile.renderY=scene.pose?(scene.axis?21.375:21.75):(scene.axis?19.75:20.25);
            if(infantry&&!scene.pose){
                // Original GI/NTWALL masks: the previous rear pose had no
                // overlap. These adjacent clear cells preserve frame8/sub2
                // and yield hidden rows, including a damaged-wall mixed case.
                mobile.renderX=scene.axis?20.75:21.0;
                mobile.renderY=scene.axis?21.25:20.75;
            }
            if(scene.kind==4){mobile.renderX=scene.axis?21.125:21.375;mobile.renderY=scene.axis?21.375:21.125;}
            mobile.x=static_cast<int>(std::floor(mobile.renderX));mobile.y=static_cast<int>(std::floor(mobile.renderY));
            mobile.cell=runtimeCellKey(mobile.x,mobile.y);mobile.hasRenderPosition=true;mobile.renderMoveActive=infantry;
            mobile.subCell=infantry?(scene.pose?4:2):-1;mobile.facing=scene.pose?64:0;mobile.turretFacing=(mobile.facing+64)%256;
            mobile.health=runtimeMaxHealthForObject(s,mobile);mobile.mission="Guard";mobile.movementAnimationDistance=0;
            const double rx=mobile.renderX+(infantry?.25:0),ry=mobile.renderY+(infantry?(scene.pose?.25:-.25):0);
            std::vector<WorldObjectInstance> objects;if(hasBuilding)objects.push_back(building);if(hasMobile)objects.push_back(mobile);
            const auto prepare=[&](bool reversed){
                s.world.accessObjects()=objects;s.world.accessTerrainCells()=plainTerrain;s.mapFacts=plainFacts;
                for(const auto& w:walls){
                    auto& cell=s.world.accessTerrainCells().at(static_cast<std::size_t>(w.y-10)*27+w.x-10);
                    cell.hasOverlay=true;cell.overlayIndex=26;cell.overlayType=cell.overlayImage="NAWALL";cell.overlayFrame=w.frame;
                    auto& fact=s.mapFacts.cells.at(s.mapFacts.cellIndexLookup.at(cell.cellIndex512));fact.hasOverlay=true;
                    fact.overlay.cellIndex=cell.cellIndex512;fact.overlay.x=w.x;fact.overlay.y=w.y;fact.overlay.overlayIndex=26;
                    fact.overlay.type=fact.overlay.image="NAWALL";fact.overlay.frameIndex=fact.overlay.frame=w.frame;
                    s.mapFacts.contentHash+=static_cast<std::uint64_t>(cell.cellIndex512)*128+w.frame;
                }
                if(reversed){std::reverse(s.world.accessTerrainCells().begin(),s.world.accessTerrainCells().end());
                    std::reverse(s.world.accessObjects().begin(),s.world.accessObjects().end());}
                initializeRuntimeNavigation(s);s.world.accessObjects()=objects;
                s.ownerRemapColors["americans"]=RGB(235,210,45);
                if(reversed)std::reverse(s.world.accessObjects().begin(),s.world.accessObjects().end());
                s.structureFootprints=buildRuntimeStructureFootprints(s);s.tick=17;
                s.camera.initialized=true;s.camera.tileWidth=zoom;s.camera.tileHeight=zoom/2;
                s.camera.worldOffsetX=-Width/2+(scene.pan?29:0);s.camera.worldOffsetY=21*zoom/2-Height/2+(scene.pan?-17:0);
                invalidateRuntimeObjectProjectionCache(s);
            };
            prepare(false);
            const auto beforeProjected=runtimeProjectedObjectsForViewport(s,viewport);
            std::vector<int> before(static_cast<std::size_t>(Width)*Height,-2);
            for(const auto& item:beforeProjected){RECT clip{};if(!IntersectRect(&clip,&item.spriteBounds,&viewport))continue;
                for(int y=clip.top;y<clip.bottom;++y)for(int x=clip.left;x<clip.right;++x){
                    const auto p=static_cast<std::size_t>(y)*Width+x;if(before[p]!=-2)continue;
                    const auto* hit=findProjectedObjectAtPoint(beforeProjected,POINT{x,y});
                    before[p]=hit&&hit->object?hit->object->instanceId:-1;++beforeClicks;
                }}
            GameWorld drawing=s.world;MapFacts drawFacts=plainFacts;
            const auto paint=[&](const std::vector<Wall>& chosen,const std::vector<WorldObjectInstance>& drawObjects,bool reversed=false,bool hdc=false){
                drawing.accessObjects()=drawObjects;drawing.accessTerrainCells()=plainTerrain;drawFacts=plainFacts;
                for(const auto& w:chosen){
                    auto& cell=drawing.accessTerrainCells().at(static_cast<std::size_t>(w.y-10)*27+w.x-10);
                    cell.hasOverlay=true;cell.overlayIndex=26;cell.overlayType=cell.overlayImage="NAWALL";cell.overlayFrame=w.frame;
                    auto& fact=drawFacts.cells.at(drawFacts.cellIndexLookup.at(cell.cellIndex512));fact.hasOverlay=true;
                    fact.overlay.cellIndex=cell.cellIndex512;fact.overlay.x=w.x;fact.overlay.y=w.y;fact.overlay.overlayIndex=26;
                    fact.overlay.type=fact.overlay.image="NAWALL";fact.overlay.frameIndex=fact.overlay.frame=w.frame;
                    drawFacts.contentHash+=static_cast<std::uint64_t>(cell.cellIndex512)*128+w.frame;
                }
                if(reversed){std::reverse(drawing.accessTerrainCells().begin(),drawing.accessTerrainCells().end());
                    std::reverse(drawing.accessObjects().begin(),drawing.accessObjects().end());}
                auto context=view;context.mapFacts=&drawFacts;auto camera=s.camera;
                if(hdc){context.surfacePixels=nullptr;context.surfaceWidth=context.surfaceHeight=0;}
                if(!drawRuntimeMap(surface.dc,viewport,drawing,context,camera,reversed?*reverseCache:*cache).rendered)
                    throw std::runtime_error("original full map paint");
                return surface.copy();
            };
            const std::vector<Wall> pair(walls.begin(),walls.end());
            const auto blank=paint({},{}),first=paint({walls[0]},{}),second=paint({walls[1]},{});
            const auto buildingOnly=hasBuilding?paint({},{building}):blank;
            const auto mobileOnly=hasMobile?paint({},{mobile}):blank;
            const auto loadedSource=[&](const auto& entries,const std::string& hash){
                for(const auto& [key,value]:entries)if(value.loaded&&sha256(value.bytes)==hash)return true;
                return false;
            };
            if(!loadedSource(cache->auxiliaryShpBytesByImage,sha256(wallBytes)))return fail("actual neutral overlay selected different original NTWALL bytes");
            if(hasBuilding&&(!loadedSource(cache->objectShpBytesByImage,sha256(bodyBytes))
                ||!loadedSource(cache->auxiliaryShpBytesByImage,sha256(activeBytes))))
                return fail("actual NATSLA body/active selected different original bytes");
            const auto center=[&](double x,double y){return POINT{
                static_cast<LONG>(std::lround((x-y)*zoom/2.0))-s.camera.worldOffsetX,
                static_cast<LONG>(std::lround((x+y)*zoom/4.0))-s.camera.worldOffsetY};};
            std::vector<Layer> layers;int wallColorWitness=0;
            for(int n=0;n<2;++n){
                const auto f=frame(wallBytes,walls[n].frame);const auto c=center(walls[n].x,walls[n].y);
                auto layer=overlayWall(f,c.x,c.y,(walls[n].x+walls[n].y)*15,walls[n].x,walls[n].y);
                layer.name="NAWALL-"+std::to_string(walls[n].frame);layer.color=n?second:first;
                bool frameInput=false;
                for(const auto& [key,value]:cache->auxiliaryShpSpritesByImage)if(value.loaded&&value.frameIndex==walls[n].frame
                    &&value.image.width==f.width&&value.image.height==f.height&&value.image.offsetX==f.x
                    &&value.image.offsetY==f.y&&value.image.pixels==f.pixels)frameInput=true;
                if(!frameInput)return fail("actual wall pose differs from independent original frame");
                layers.push_back(std::move(layer));
            }
            for(std::size_t p=0;p<blank.size();++p)if(layers[0].mask[p]&&layers[1].mask[p]
                &&(first[p]&0xffffffU)!=(second[p]&0xffffffU))++wallColorWitness;
            if(hasBuilding){
                const auto c=center(building.x,building.y);const int native=(building.x+building.y)*15;
                auto b=buildingBody(body,shape,c.x,c.y,native,1,1,0,0,0,building.instanceId,double(building.x+building.y)*256);
                auto a=buildingAnimation(animation,c.x,c.y,native,0,0,-30,0,building.instanceId,double(building.x+building.y)*256);
                b.color=shpColors(body,c.x-body.canvasWidth/2+body.x,c.y-body.canvasHeight/2+body.y,buildingPalette);
                a.color=shpColors(animation,c.x-animation.canvasWidth/2+animation.x,c.y-animation.canvasHeight/2+animation.y,buildingPalette);
                sources<<label<<",authored-fixture-color,235/210/45,TESLA-player-body-and-active\n";
                bool activeInput=false;
                for(const auto& [key,value]:cache->auxiliaryShpSpritesByImage)if(value.loaded&&value.frameIndex==4
                    &&value.image.width==animation.width&&value.image.height==animation.height
                    &&value.image.offsetX==animation.x&&value.image.offsetY==animation.y&&value.image.pixels==animation.pixels)activeInput=true;
                if(!activeInput)return fail("tick17 NATSLA active pose differs from independent rate450/frame4");
                for(std::size_t p=0;p<blank.size();++p)if((buildingOnly[p]&0xffffffU)!=(blank[p]&0xffffffU)&&!b.mask[p]&&!a.mask[p])
                    return fail("unmodeled original NATSLA body/active layer");
                const auto soloExpected=compose(blank,{b,a});
                const int soloPixels=difference(buildingOnly,soloExpected.colors);
                if(soloPixels){
                    std::vector<std::uint32_t> soloSheet(static_cast<std::size_t>(Width)*2*Height);
                    for(int y=0;y<Height;++y){
                        std::copy_n(buildingOnly.begin()+y*Width,Width,soloSheet.begin()+static_cast<std::size_t>(y)*Width*2);
                        std::copy_n(soloExpected.colors.begin()+y*Width,Width,soloSheet.begin()+static_cast<std::size_t>(y)*Width*2+Width);
                    }
                    writeRuntimeSmokeBmp(pathString(output/(label+"-solo-actual-expected.bmp")),soloSheet.data(),Width*2,Height,error);
                    return fail("isolated NATSLA original-layer color/depth differs pixels="+std::to_string(soloPixels));
                }
                if(scene.kind==3&&scene.axis==0&&scene.damage==0&&scene.pose==0&&zoom==48&&!scene.pan){
                    // Preserve the legitimate blank-footprint fallback: these
                    // two physical foundation points contain no original SHP
                    // pixel and have no wall in the controlled scene.
                    const auto savedTerrain=s.world.getTerrainCells();const auto savedFacts=s.mapFacts;
                    s.world.accessTerrainCells()=plainTerrain;s.mapFacts=plainFacts;
                    invalidateRuntimeObjectProjectionCache(s);
                    const auto blankBefore=runtimeProjectedObjectsForViewport(s,viewport);
                    const std::array<POINT,2> points{{{c.x+zoom/2,c.y},{c.x+zoom/2-1,c.y}}};
                    std::array<int,2> beforeIds{};
                    for(std::size_t n=0;n<points.size();++n){
                        const auto point=points[n];const auto p=static_cast<std::size_t>(point.y)*Width+point.x;
                        if(double(std::abs(point.x-c.x))/(zoom/2)+double(std::abs(point.y-c.y))/(zoom/4)>1.0
                            ||b.mask[p]||a.mask[p])return fail("blank foundation control lacks independent physical/raw-zero proof");
                        const auto* hit=findProjectedObjectAtPoint(blankBefore,point);
                        beforeIds[n]=hit&&hit->object?hit->object->instanceId:-1;
                    }
                    (void)paint({},{building});
                    const auto blankAfter=runtimeProjectedObjectsForViewport(s,viewport);
                    std::ofstream blankRows(output/"blank-foundation-controls.csv");
                    blankRows<<"x,y,raw_body_mask,raw_active_mask,expected,before,after\n";
                    for(std::size_t n=0;n<points.size();++n){
                        const auto* hit=findProjectedObjectAtPoint(blankAfter,points[n]);
                        const int afterId=hit&&hit->object?hit->object->instanceId:-1;
                        blankRows<<points[n].x<<','<<points[n].y<<",0,0,"<<building.instanceId<<','<<beforeIds[n]<<','<<afterId<<'\n';
                        blankRows.flush();
                        if(beforeIds[n]!=building.instanceId||afterId!=building.instanceId)return fail("legitimate blank foundation click lost");
                    }
                    blankRows.close();if(!blankRows.good())return fail("blank foundation evidence");
                    s.world.accessTerrainCells()=savedTerrain;s.mapFacts=savedFacts;invalidateRuntimeObjectProjectionCache(s);
                }
                layers.push_back(std::move(b));layers.push_back(std::move(a));
            }
            if(hasMobile){
                Layer m;m.name=type;m.owner=mobile.instanceId;m.authoredSort=(rx+ry+1)*256;m.writeZ=false;m.color=mobileOnly;
                const auto c=center(rx,ry);const int native=static_cast<int>(std::lround((rx+ry)*15));int bottom=0;
                if(infantry){
                    const RuntimeAuxiliaryShpBytesCacheEntry* bytes=nullptr;
                    for(const auto& [key,value]:cache->objectShpBytesByImage)if(value.loaded&&toLowerAsciiLocal(key).find("|gi|")!=std::string::npos)bytes=&value;
                    if(!bytes)return fail("original GI bytes");
                    const int bucket=((((256-mobile.facing)%256)*8)+128)/256%8;
                    const auto f=frame(bytes->bytes,8+bucket*6);
                    const int left=c.x-f.canvasWidth/2+f.x,top=c.y+zoom/4-f.canvasHeight+f.y;
                    bottom=native+top-c.y+f.height;
                    shpMask(m,f,left,top,[&](int,int y){return retailGradient(bottom,native+top-c.y+y,12);});
                    bool input=false;
                    for(const auto& [key,value]:cache->objectShpSpritesByType)if(value.loaded&&value.frameIndex==8+bucket*6&&value.sequenceName=="Walk"
                        &&value.image.width==f.width&&value.image.height==f.height&&value.image.offsetX==f.x&&value.image.offsetY==f.y
                        &&value.image.pixels==f.pixels)input=true;
                    if(!input)return fail("actual GI original Walk strip input");
                    sources<<label<<",GI,"<<sha256(bytes->bytes)<<','<<8+bucket*6<<'\n';
                }else{
                    std::array<const RuntimeVoxelCacheEntry*,3> rasters{};const auto token=toLowerAsciiLocal(type)+"|";
                    for(const auto& [key,value]:cache->voxelObjectsByType){
                        if(!value.loaded||!value.hasSprite||value.animationFrameIndex!=0||!key.starts_with(token))continue;
                        const int role=key.starts_with(token+"turret|")?1:key.starts_with(token+"barrel|")?2:0;
                        if(value.facingBucket!=(((role?mobile.turretFacing:mobile.facing)+224)%256)/8)continue;
                        rasters[role]=&value;
                    }
                    const auto* rule=s.rules.findUnitType("vehicle",type);
                    if(!rule||!rasters[0]||(rule->turret&&!rasters[1]))return fail("original VXL body/turret input");
                    const auto* art=s.art.findArtType(rule->image.empty()?type:rule->image);
                    if(!art)return fail("original vehicle ART");
                    const double angle=(((mobile.facing+224)%256)/8-8)*6.2831853071795864769/32;
                    const double mount=art->turretOffset.empty()?0:std::stod(art->turretOffset)*.18;
                    const int tx=static_cast<int>(std::lround(mount*(std::cos(angle)-std::sin(angle))*1.38*rasters[0]->rasterFitScale));
                    const int ty=static_cast<int>(std::lround(mount*(std::cos(angle)+std::sin(angle))*.72*rasters[0]->rasterFitScale));
                    for(int role=0;role<3;++role)if(rasters[role]){
                        const auto& raster=*rasters[role];const RuntimeVoxelSourceCacheEntry* source=nullptr;
                        for(const auto& [key,value]:cache->voxelSourcesByImage)if(value.loaded&&value.metadata.vxlPath==raster.metadata.vxlPath)source=&value;
                        if(!source)return fail("original VXL/HVA source");
                        const int partBottom=native+(role?ty:0)+RuntimeGroundDepthOracle::nativeVoxelBottom(*source,
                            role?mobile.turretFacing:mobile.facing,raster.rasterFitScale,role!=0);
                        if(!role)bottom=partBottom;else if(partBottom>bottom)bottom=partBottom+1;
                        const auto& f=raster.image;const int left=c.x+(role?tx:0)-f.globalWidth/2+f.offsetX;
                        const int top=c.y+(role?ty:0)-f.globalHeight+f.offsetY;
                        for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x)if(f.pixels[static_cast<std::size_t>(y)*f.width+x]){
                            const int px=left+x,py=top+y;if(px>=0&&py>=0&&px<Width&&py<Height)m.mask[static_cast<std::size_t>(py)*Width+px]=1;
                        }
                        sources<<label<<",VXL-role"<<role<<','<<sha256(source->vxlBytes)<<'/'<<sha256(source->hvaBytes)<<",0\n";
                    }
                    for(std::size_t p=0;p<m.mask.size();++p)if(m.mask[p])
                        m.depth[p]=retailGradient(bottom,native+static_cast<int>(p/Width)-c.y,0);
                }
                layers.push_back(std::move(m));
            }
            const auto expected=compose(blank,layers);int objectOverlap=0,visible=0,hidden=0;
            std::vector<unsigned char> check(blank.size());
            for(std::size_t p=0;p<blank.size();++p){
                const bool wall=layers[0].mask[p]||layers[1].mask[p];if(!wall)continue;
                bool object=false;for(std::size_t n=2;n<layers.size();++n)object=object||layers[n].mask[p];
                if(!object)continue;check[p]=1;++objectOverlap;
                expected.owners[p]<0?++hidden:++visible;
            }
            const auto actual=paint(pair,objects),reversed=paint(pair,objects,true);
            auto changedIds=objects;for(auto& object:changedIds)object.instanceId+=1000;
            const int pixels=difference(actual,expected.colors),order=difference(actual,reversed),
                ids=difference(actual,paint(pair,changedIds)),repeat=difference(actual,paint(pair,objects));
            const int hdc=difference(actual,paint(pair,objects,false,true));++hdcChecks;
            int beforeBad=0,afterBad=0,reverseBad=0;
            std::vector<int> afterOwners(blank.size(),-2),reverseOwners(blank.size(),-2);
            const auto afterProjected=runtimeProjectedObjectsForViewport(s,viewport);
            for(std::size_t p=0;p<check.size();++p)if(check[p]){
                if(before[p]!=expected.owners[p])++beforeBad;
                const POINT point{static_cast<LONG>(p%Width),static_cast<LONG>(p/Width)};
                const auto* hit=findProjectedObjectAtPoint(afterProjected,point);
                afterOwners[p]=hit&&hit->object?hit->object->instanceId:-1;
                if(afterOwners[p]!=expected.owners[p])++afterBad;++afterClicks;
            }
            prepare(true);const auto reverseProjected=runtimeProjectedObjectsForViewport(s,viewport);
            for(std::size_t p=0;p<check.size();++p)if(check[p]){
                const auto* hit=findProjectedObjectAtPoint(reverseProjected,POINT{static_cast<LONG>(p%Width),static_cast<LONG>(p/Width)});
                reverseOwners[p]=hit&&hit->object?hit->object->instanceId:-1;
                if(reverseOwners[p]!=expected.owners[p])++reverseBad;
            }
            if(beforeBad||afterBad||reverseBad){
                for(std::size_t p=0;p<check.size();++p)if(check[p]
                    &&(before[p]!=expected.owners[p]||afterOwners[p]!=expected.owners[p]||reverseOwners[p]!=expected.owners[p])){
                    clickErrors<<label<<','<<p%Width<<','<<p/Width<<','<<expected.owners[p]<<','<<before[p]<<','<<afterOwners[p]<<','<<reverseOwners[p];
                    for(std::size_t n=0;n<2;++n)clickErrors<<','<<int(layers[n].mask[p])<<','<<layers[n].depth[p];
                    for(const std::string& name:std::array<std::string,3>{"original-building-body","original-building-animation",hasMobile?type:"absent-mobile"}){
                        const Layer* layer=nullptr;for(const auto& candidate:layers)if(candidate.name==name)layer=&candidate;
                        clickErrors<<','<<(layer?int(layer->mask[p]):0)<<','<<(layer?layer->depth[p]:NoDepth);
                    }
                    clickErrors<<','<<expected.sceneZ[p]<<','<<(actual[p]&0xffffffU)<<','<<(expected.colors[p]&0xffffffU)<<'\n';
                }
                clickErrors.flush();
            }
            rows<<group<<','<<scene.axis<<','<<scene.damage<<','<<scene.pose<<','<<zoom<<','<<scene.pan<<','<<wallColorWitness
                <<','<<objectOverlap<<','<<visible<<','<<hidden<<','<<pixels<<','<<order<<','<<ids<<','<<repeat<<','<<hdc
                <<','<<beforeBad<<','<<afterBad<<','<<reverseBad<<'\n';rows.flush();sources.flush();
            if(pixels||order||ids||repeat||hdc||beforeBad||afterBad||reverseBad||(!scene.pan&&scene.damage==0)){
                std::vector<std::uint32_t> sheet(static_cast<std::size_t>(Width)*3*Height);
                for(int column=0;column<3;++column){const auto& image=column==0?actual:column==1?expected.colors:reversed;
                    for(int y=0;y<Height;++y)std::copy_n(image.begin()+y*Width,Width,sheet.begin()+static_cast<std::size_t>(y)*Width*3+column*Width);}
                if(!writeRuntimeSmokeBmp(pathString(output/(label+"-actual-expected-reversed.bmp")),sheet.data(),Width*3,Height,error))return false;
            }
            if(pixels||order||ids||repeat||hdc||beforeBad||afterBad||reverseBad)return fail("pixels="+std::to_string(pixels)
                +" order="+std::to_string(order)+" IDs="+std::to_string(ids)+" repeat="+std::to_string(repeat)+" HDC="+std::to_string(hdc)
                +" before="+std::to_string(beforeBad)+" after="+std::to_string(afterBad)+" reversePicks="+std::to_string(reverseBad));
            auto& count=coverage[group];count.overlap+=objectOverlap;count.visible+=visible;count.hidden+=hidden;
            count.mixed+=visible>0&&hidden>0;count.wallColors+=wallColorWitness;++cases;
        }
        coverageLog<<"type,overlap,visible,hidden,mixed,wall_color_witness\n";
        for(const auto& [type,count]:coverage){
            coverageLog<<type<<','<<count.overlap<<','<<count.visible<<','<<count.hidden<<','<<count.mixed<<','<<count.wallColors<<'\n';
            if(!count.overlap||!count.visible||!count.hidden||!count.wallColors)return fail(type+" lacks independent original-color visible/hidden witnesses");
        }
        rows.close();sources.close();coverageLog.close();clickErrors.close();
        if(!rows.good()||!sources.good()||!coverageLog.good()||!clickErrors.good()||cases!=144)return fail("complete 128+16 evidence matrix missing");
        std::cout<<"Issue25 neutral default wall: NAWALL=NTWALL original asset confirmed, cases="<<cases<<" HDC="<<hdcChecks
            <<" beforePaintClicks="<<beforeClicks<<" overlapClicks="<<afterClicks<<" originalMap=unconfirmed\n";
        error.clear();return true;
    }catch(const std::exception& detail){return fail(detail.what());}
}
