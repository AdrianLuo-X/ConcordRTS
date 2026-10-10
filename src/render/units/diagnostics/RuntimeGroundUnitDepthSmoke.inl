// Independent flat-ground mobile/building integration. Include after the
// existing RuntimeBuildingDepthOracle header and offscreen smoke utilities.
// Fixtures use untouched stock rules/art and raw original model/SHP bytes.
// Drive/Track (MTNK, HTNK, HARV) and Drive/Wheel (FV) use a genuine flat Clear
// TMP field with matching MapFacts. Every tested fractional coordinate has
// at least a two-cell metadata/terrain margin. Qualification is asserted
// separately from the independent expected pixel/depth computation.
//
// Expected events are explicitly separated by original lepton sort:
// unit behind the complete building, or body+authored animations before unit.
// Body/turret/barrel parts use one native bottom, with retail +1 only on an
// extending later part. Raw per-pixel VXL model depths are never scene Z.
namespace RuntimeGroundDepthOracle {
using namespace RuntimeBuildingDepthOracle;
inline const auto nativeVoxelBottom=[](const RuntimeVoxelSourceCacheEntry& source,int facing,double fit,bool originPivot=false) {
        const auto& v=source.vxlBytes;const auto& h=source.hvaBytes;
        if(v.size()<802||std::memcmp(v.data(),"Voxel Animation",15))
            throw std::runtime_error("independent VXL header");
        const unsigned sections=little(v,20,4);
        if(!sections||sections>1024||little(v,24,4)!=sections||fit<=0)
            throw std::runtime_error("independent VXL section/scale");
        const std::size_t footer=802+sections*28U+little(v,28,4);
        if(footer+sections*92U>v.size()||h.size()<24||!little(h,16,4)||little(h,20,4)!=sections)
            throw std::runtime_error("independent VXL/HVA range");
        const auto real=[](const Bytes& bytes,std::size_t at) {
            const std::uint32_t bits=little(bytes,at,4);float value=0;
            std::memcpy(&value,&bits,4);
            if(!std::isfinite(value))throw std::runtime_error("independent nonfinite model value");
            return static_cast<double>(value);
        };
        struct Section {std::array<double,3> low{},high{};std::array<double,12> matrix{};};
        std::vector<Section> model;
        double xmin=1e30,xmax=-1e30,ymin=1e30,ymax=-1e30;
        const auto transform=[](const Section& section,const std::array<double,3>& p) {
            std::array<double,3> value{};
            for(int row=0;row<3;++row) {
                value[row]=section.matrix[row*4+3];
                for(int column=0;column<3;++column)value[row]+=section.matrix[row*4+column]*p[column];
            }
            return value;
        };
        for(unsigned n=0;n<sections;++n) {
            Section section;const std::size_t at=footer+n*92U;
            for(int axis=0;axis<3;++axis) {
                section.low[axis]=real(v,at+64+axis*4);section.high[axis]=real(v,at+76+axis*4);
                if(!v[at+88+axis]||section.high[axis]<=section.low[axis])
                    throw std::runtime_error("independent model box");
            }
            for(int i=0;i<12;++i)section.matrix[i]=real(h,24+sections*16U+n*48U+i*4);
            double multiplier=real(v,at+12);if(multiplier==0)multiplier=1;
            for(int axis=0;axis<3;++axis)section.matrix[axis*4+3]*=multiplier
                *(section.high[axis]-section.low[axis])/v[at+88+axis];
            for(int corner=0;corner<8;++corner) {
                std::array<double,3> p{};for(int axis=0;axis<3;++axis)
                    p[axis]=(corner&(1<<axis))?section.high[axis]:section.low[axis];
                const auto q=transform(section,p);xmin=std::min(xmin,q[0]);xmax=std::max(xmax,q[0]);
                ymin=std::min(ymin,q[1]);ymax=std::max(ymax,q[1]);
            }
            model.push_back(section);
        }
        const int authoredBucket=((facing+224)%256)/8;
        const double angle=(authoredBucket-8)*6.2831853071795864769/32.0;
        const double c=std::cos(angle),sine=std::sin(angle),px=originPivot?0:(xmin+xmax)/2,py=originPivot?0:(ymin+ymax)/2;
        const auto projectedY=[&](const std::array<double,3>& p) {
            return ((p[0]-px)*(c+sine)+(p[1]-py)*(c-sine))*.72-p[2]*1.55;
        };
        double low=1e30,high=-1e30,anchor=0;
        for(const auto& section:model) {
            for(int corner=0;corner<8;++corner) {
                std::array<double,3> p{};for(int axis=0;axis<3;++axis)
                    p[axis]=(corner&(1<<axis))?section.high[axis]:section.low[axis];
                const double y=projectedY(transform(section,p));low=std::min(low,y);high=std::max(high,y);
            }
            anchor+=projectedY(transform(section,{(section.low[0]+section.high[0])/2,
                (section.low[1]+section.high[1])/2,section.low[2]}));
        }
        anchor=originPivot?0:anchor/model.size();
        // Retail 754510 truncates the complete eight-corner box and gives
        // four pixels of padding per side. No opaque crop or shadow extent.
        const int rows=static_cast<int>((high-low)*fit)+8;
        const int center=static_cast<int>(((low+high)/2-anchor)*fit);
        return center-rows/2+rows;
    };
}

bool runRuntimeGroundUnitDepthSmoke(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeBuildingDepthOracle;
    std::cout<<std::unitbuf;
    const char* output=std::getenv("CONCORD_GROUND_UNIT_DEPTH_OUTPUT");
    const std::filesystem::path root=output&&*output?output:
        "build/smoke/feature-set-19-complete/05-shipyard-depth/ground";
    std::filesystem::create_directories(root);
    std::string label;
    const auto fail=[&](const std::string& why){error="Ground unit depth "+label+": "+why;return false;};
    try {
        for(const char* setting:{"CONCORD_VXL_PROJ_SCREEN_X","CONCORD_VXL_PROJ_GROUND_Y","CONCORD_VXL_PROJ_HEIGHT_Y",
            "CONCORD_VXL_SCREEN_X_SCALE_MUL","CONCORD_VXL_SCREEN_Y_SCALE_MUL","CONCORD_VXL_HEIGHT_Y_SCALE_MUL",
            "CONCORD_VXL_TURRET_OFFSET_SCALE","CONCORD_VXL_TURRET_PIXEL_OFFSET_X","CONCORD_VXL_TURRET_PIXEL_OFFSET_Y",
            "CONCORD_VXL_BARREL_PIXEL_OFFSET_X","CONCORD_VXL_BARREL_PIXEL_OFFSET_Y"})
            if(std::getenv(setting))return fail(std::string("oracle requires original default projection/offset: ")+setting);
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=config;s.config.playerSlots.clear();s.config.gameModeRulesOverride.clear();
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        if(!s.report.success)return fail("original startup");
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.config.playerCountry="Americans";s.economy.owner="Americans";s.economy.skirmishStartApplied=true;
        RuntimeHumanHouseAuthority local;local.owner="Americans";local.canonicalOwner="americans";local.localFacade=true;
        s.humanHouseAuthorities={local};s.ownerRemapColors["Americans"]=RGB(235,210,45);
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        const auto metadata=runtimeTerrainMetadataContext(s);
        const int clearSet=s.theater.getGeneralTileSetIndex("ClearTile");int clearTile=-1;
        for(int tile=0;tile<static_cast<int>(s.theater.getTotalTileSlots());++tile) {
            TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;
            if(s.theater.resolveTileIndex(tile).tileSetIndex==clearSet
                &&resolveRuntimeTerrainSubTileLandType(cell,metadata,s.renderCache)==RuntimeTerrainLandType::Clear
                &&resolveRuntimeTerrainSubTileRampType(cell,metadata,s.renderCache)==0){clearTile=tile;break;}
        }
        if(clearTile<0)return fail("authored Clear TMP unavailable");
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();s.mapFacts={};
        for(int y=10;y<=36;++y)for(int x=10;x<=36;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=clearTile;
            cell.subTileIndex=0;s.world.accessTerrainCells().push_back(cell);
            MapCellFacts fact;fact.x=x;fact.y=y;fact.cellIndex=cell.cellIndex512;fact.hasTerrain=true;
            fact.terrain.x=x;fact.terrain.y=y;fact.terrain.tileIndex=clearTile;fact.terrain.subTileIndex=0;
            fact.terrain.level=fact.heightLevel=0;fact.terrainClass="clear";
            s.mapFacts.cellIndexLookup.emplace(fact.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(fact);
        }
        s.mapFacts.terrainCellCount=s.mapFacts.cells.size();s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();
        s.mapFacts.contentHash=0x19050001;s.mapFactsLoaded=true;
        initializeRuntimeNavigation(s);s.tick=17;
        const auto zShape=decodeFrameZero(retailTemplate(std::filesystem::current_path()));
        const auto* buildingArt=s.art.findArtType("GAPOWR");
        if(!buildingArt||buildingArt->foundation!="2x2"||buildingArt->normalZAdjust
            ||buildingArt->zShapePointMove!=std::array<int,2>{}||!buildingArt->bibShape.empty())
            return fail("original GAPOWR body contract");
        constexpr int width=640,height=480,buildingId=1501,unitId=1502;
        const RECT viewport{0,0,width,height};
        struct Surface {
            HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
            Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
                dc=CreateCompatibleDC(nullptr);void* raw=nullptr;if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
                pixels=static_cast<std::uint32_t*>(raw);if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
            ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
        } surface;
        if(!surface.dc||!surface.pixels)return fail("DIB allocation");
        RuntimeMapRenderContext view;view.rules=&s.rules;view.art=&s.art;view.theater=&s.theater;view.mapFacts=&s.mapFacts;
        view.assetRootPath=runtimeAssetRootPath(s.config);view.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
        view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);view.palettePath=runtimePalettePath(s.config,s.report.map);
        view.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);view.ownerRemapColors=&s.ownerRemapColors;
        view.animationTick=17;view.fixedTickHz=30;view.useTileTextures=view.shadowsEnabled=false;
        view.seeHiddenObjects=false; // Body-depth oracle; Behind has a separate stock-SHP contract.
        view.surfacePixels=surface.pixels;view.surfaceWidth=width;view.surfaceHeight=height;
        auto cache=std::make_unique<RuntimeMapRenderCache>();GameWorld renderWorld=s.world;
        const auto difference=[](const auto& a,const auto& b){int count=0;for(std::size_t p=0;p<a.size();++p)
            count+=(a[p]&0xffffffU)!=(b[p]&0xffffffU);return count;};
        std::ofstream rows(root/"ground-depth.csv"),partsRows(root/"ground-parts.csv");
        rows<<"unit,pose,zoom,facing,pan,parts,overlap,unit_visible,building_visible,pixels,picks,order,id,repeat,hdc,mixed,shared_phase_owner_witness\n";
        partsRows<<"unit,pose,zoom,facing,role,image,vxl_sha,hva_sha,individual_bottom,union_bottom\n";
        int cases=0,hdcChecks=0,negativeChecks=0,mixedFrames=0;
        std::uint64_t overlapTotal=0,unitVisibleTotal=0,buildingVisibleTotal=0,multipartPixels=0,sharedPhaseTotal=0;
        RuntimeSnapshotActiveStateScope active(s);
        for(const std::string type:{"MTNK","HTNK","FV","HARV"})for(int pose:{0,1,2})
            for(int zoom:{48,60})for(int facing:{0,32,64,96,128,160,192,224})for(bool pan:{false,true}) {
            // Offline original-resource selection found this single legal
            // side segment sufficient. Do not multiply the old matrix to
            // search at runtime or accept an unproven no-overlap control.
            if(pose==2&&(type!="MTNK"||(facing!=0&&facing!=64)))continue;
            const bool front=pose!=0;
            label=type+(pose==2?"-side":front?"-front":"-rear")+"-z"+std::to_string(zoom)+"-f"+std::to_string(facing);
            WorldObjectInstance building;building.kind=WorldObjectKind::Structure;building.type="GAPOWR";
            building.instanceId=buildingId;building.owner="Americans";building.x=building.y=20;building.cell=runtimeCellKey(20,20);
            building.alive=building.poweredOperational=true;building.health=runtimeMaxHealthForObject(s,building);building.mission="Guard";
            WorldObjectInstance unit;unit.kind=WorldObjectKind::Unit;unit.type=type;unit.instanceId=unitId;unit.owner="Americans";
            unit.x=front?21:19;unit.y=front?22:19;unit.cell=runtimeCellKey(unit.x,unit.y);unit.hasRenderPosition=true;
            unit.renderX=unit.x+.25;unit.renderY=unit.y+.25;unit.renderLevel=0;unit.alive=unit.poweredOperational=true;
            if(pose==2){unit.x=20;unit.y=22;unit.cell=runtimeCellKey(20,22);unit.renderX=20.25;unit.renderY=22.0;}
            unit.health=runtimeMaxHealthForObject(s,unit);unit.mission="Guard";unit.facing=facing;unit.turretFacing=(facing+64)%256;
            const auto* rule=s.rules.findUnitType("vehicle",type);
            if(!rule)return fail("original vehicle rules missing");
            // Stock MTNK is Grizzly Image=GTNK; ART [MTNK] belongs to the
            // Apocalypse model and must not supply this vehicle's part data.
            const std::string visualId=rule->image.empty()?type:rule->image;
            const auto* art=s.art.findArtType(visualId);
            if(!art||!rule||!art->voxel||!rule->turret)return fail("expected original hull+turret vehicle");
            const double sort=(unit.renderX+unit.renderY+1)*256;
            for(const auto* animations:{&buildingArt->activeAnimations,&buildingArt->specialAnimations,&buildingArt->auxiliaryAnimations})
                for(const auto& animation:*animations)if(animation.offsetX||animation.offsetY||animation.ySort<0
                    ||(front?sort<=40*256+animation.ySort:sort>=40*256))return fail("independent event separation");
            s.world.accessObjects()={building,unit};s.structureFootprints=buildRuntimeStructureFootprints(s);
            if(pose==2){
                // Fixed quarter-step input on (20,22)->(21,22), wholly
                // outside GAPOWR's [20,22)x[20,22) occupied foundation.
                // This verifies legal endpoints, not a simulated move.
                const auto occupied=buildRuntimeOccupancyLookup(s,unit.instanceId,nullptr,nullptr);
                const RuntimeCellPoint start{20,22},end{21,22};
                if(!runtimeCellIsPassable(s,s.terrainByCell,occupied,start,unit)
                    ||!runtimeCellIsPassable(s,s.terrainByCell,occupied,end,unit)
                    ||!runtimeCellTransitionIsPassable(s,s.terrainByCell,occupied,start,end,unit,-1,nullptr,false))
                    return fail("side segment endpoints/transition are not actually passable");
            }
            s.camera.initialized=true;s.camera.tileWidth=zoom;s.camera.tileHeight=zoom/2;
            s.camera.worldOffsetX=-width/2+(pan?29:0);s.camera.worldOffsetY=21*zoom/2-height/2+(pan?-17:0);
            invalidateRuntimeObjectProjectionCache(s);const auto projected=runtimeProjectedObjectsForViewport(s,viewport);
            if(!runtimeGroundVehicleUsesBuildingDepth(unit,s.world,view,*cache))return fail("genuine flat Clear qualification rejected");
            if(!front&&zoom==48&&facing==0&&!pan) {
                for(int kind=0;kind<7;++kind) {
                    auto excluded=unit;auto excludeView=view;MapFacts changedFacts=s.mapFacts;
                    switch(kind){case 0:excluded.onBridge=true;break;case 1:excluded.airborne=true;break;
                    case 2:excluded.dockedToObjectId=buildingId;break;case 3:excluded.factoryExitHostId=buildingId;break;
                    case 4:excluded.tankBunkerHostId=buildingId;break;
                    case 5:excludeView.mapFacts=nullptr;break;
                    case 6:{auto& fact=changedFacts.cells.at(changedFacts.cellIndexLookup.at(runtimeCellKey(19,19)));
                        fact.isRamp=true;++changedFacts.contentHash;excludeView.mapFacts=&changedFacts;break;}}
                    auto excludedCache=std::make_unique<RuntimeMapRenderCache>();
                    if(runtimeGroundVehicleUsesBuildingDepth(excluded,s.world,excludeView,*excludedCache))return fail("exceptional qualification accepted");
                    ++negativeChecks;
                }
            }
            const auto paint=[&](const std::vector<WorldObjectInstance>& objects,bool hdc=false){
                renderWorld.accessObjects()=objects;auto camera=s.camera;auto context=view;
                if(hdc){context.surfacePixels=nullptr;context.surfaceWidth=context.surfaceHeight=0;}
                if(!drawRuntimeMap(surface.dc,viewport,renderWorld,context,camera,*cache).rendered)
                    throw std::runtime_error("ground original production draw");
                GdiFlush();return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);};
            const auto blank=paint({}),buildingOnly=paint({building}),unitOnly=paint({unit});
            const RuntimeAuxiliaryShpBytesCacheEntry* buildingBytes=nullptr;
            for(const auto& [key,value]:cache->objectShpBytesByImage)
                if(value.loaded&&toLowerAsciiLocal(key).find("|gapowr|")!=std::string::npos)buildingBytes=&value;
            if(!buildingBytes)return fail("original building raw SHP missing");
            const auto body=decodeFrameZero(buildingBytes->bytes);
            const int bx=-s.camera.worldOffsetX,by=20*zoom/2-s.camera.worldOffsetY;
            const int ux=static_cast<int>(std::lround((unit.renderX-unit.renderY)*zoom/2.0))-s.camera.worldOffsetX;
            const int uy=static_cast<int>(std::lround((unit.renderX+unit.renderY)*zoom/4.0))-s.camera.worldOffsetY;
            const int nativeY=static_cast<int>(std::lround((unit.renderX+unit.renderY)*15));
            const int nativeTop=600-body.canvasHeight/2+body.y,templateX=body.x-body.canvasWidth/2+198;
            const int templateY=body.y-body.canvasHeight/2+416;
            const int bodyRows=std::min(body.height,zShape.height-templateY)-std::max(0,-templateY);
            std::vector<int> buildingZ(width*height,std::numeric_limits<int>::max());
            for(int y=0;y<body.height;++y)for(int x=0;x<body.width;++x) {
                if(!body.pixels[static_cast<std::size_t>(y)*body.width+x])continue;
                const int sx=bx-body.canvasWidth/2+body.x+x,sy=by-body.canvasHeight/2+body.y+y;
                if(sx<0||sy<0||sx>=width||sy>=height)continue;
                const int tx=templateX+x,ty=templateY+y;if(tx<0||ty<0||tx>=zShape.width||ty>=zShape.height)
                    return fail("GAPOWR source unexpectedly outside original template");
                const int raw=zShape.pixels[static_cast<std::size_t>(ty)*zShape.width+tx];
                int value=raw?raw-65:0;if(value>127)value-=256;if(value< -128)value+=256;
                buildingZ[static_cast<std::size_t>(sy)*width+sx]=32768-nativeTop-bodyRows-1-value;
            }
            struct Part {const RuntimeVoxelCacheEntry* raster=nullptr;const RuntimeVoxelSourceCacheEntry* source=nullptr;
                int x=0,y=0,bottom=0;};std::array<Part,3> parts;
            const std::string lower=toLowerAsciiLocal(type);
            for(const auto& [key,value]:cache->voxelObjectsByType) {
                if(!value.loaded||!value.hasSprite||value.animationFrameIndex!=0||!key.starts_with(lower+"|"))continue;
                const int role=key.starts_with(lower+"|turret|")?1:key.starts_with(lower+"|barrel|")?2:0;
                if(role==0&&toLowerAsciiLocal(value.metadata.imageId)
                    !=toLowerAsciiLocal(art->image.empty()?visualId:art->image))continue;
                const int partFacing=role?unit.turretFacing:unit.facing;
                if(value.facingBucket!=((partFacing+224)%256)/8)continue;
                if(parts[role].raster&&parts[role].raster->image.pixels!=value.image.pixels)return fail("ambiguous original model part");
                parts[role].raster=&value;
            }
            if(!parts[0].raster||!parts[1].raster)return fail("real stock hull and turret were not rendered");
            const double angle=(((facing+224)%256)/8-8)*6.2831853071795864769/32;
            const double mount=art->turretOffset.empty()?0:std::stod(art->turretOffset)*.18;
            const int mx=static_cast<int>(std::lround(mount*(std::cos(angle)-std::sin(angle))*1.38*parts[0].raster->rasterFitScale));
            const int my=static_cast<int>(std::lround(mount*(std::cos(angle)+std::sin(angle))*.72*parts[0].raster->rasterFitScale));
            int unionBottom=0,partCount=0;
            for(int role=0;role<3;++role)if(parts[role].raster) {
                auto& part=parts[role];const auto& raster=*part.raster;
                for(const auto& [key,source]:cache->voxelSourcesByImage)
                    if(source.loaded&&source.metadata.vxlPath==raster.metadata.vxlPath)part.source=&source;
                if(!part.source)return fail("part's original VXL/HVA bytes missing");
                part.x=ux+(role?mx:0)-raster.image.globalWidth/2+raster.image.offsetX;
                part.y=uy+(role?my:0)-raster.image.globalHeight+raster.image.offsetY;
                part.bottom=nativeY+(role?my:0)+RuntimeGroundDepthOracle::nativeVoxelBottom(*part.source,
                    role?unit.turretFacing:unit.facing,raster.rasterFitScale,role!=0);
                if(partCount==0)unionBottom=part.bottom;else if(part.bottom>unionBottom)unionBottom=part.bottom+1;
                ++partCount;
            }
            auto expected=buildingOnly,wholeFront=buildingOnly,wholeBack=unitOnly;
            std::vector<unsigned char> unitMask(width*height,0),visibleMask(width*height,0),independentPartMask(width*height,0);
            for(std::size_t p=0;p<blank.size();++p)
                if((buildingOnly[p]&0xffffffU)!=(blank[p]&0xffffffU))wholeBack[p]=buildingOnly[p];
            for(int role=0;role<3;++role)if(parts[role].raster) {
                const auto& part=parts[role];const auto& image=part.raster->image;
                partsRows<<type<<','<<pose<<','<<zoom<<','<<facing<<','<<role<<','<<part.raster->metadata.imageId
                    <<','<<sha256(part.source->vxlBytes)<<','<<sha256(part.source->hvaBytes)<<','<<part.bottom<<','<<unionBottom<<'\n';
                for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
                    if(!image.pixels[static_cast<std::size_t>(y)*image.width+x])continue;
                    const int sx=part.x+x,sy=part.y+y;if(sx<0||sy<0||sx>=width||sy>=height)continue;
                    const auto p=static_cast<std::size_t>(sy)*width+sx;unitMask[p]=1;wholeFront[p]=unitOnly[p];
                    int z=((32768-unionBottom)/3)*3;
                    for(int row=unionBottom-1,phase=0;row>nativeY+sy-uy;--row)if(++phase==3){--z;phase=0;}
                    if(z<buildingZ[p]){expected[p]=unitOnly[p];visibleMask[p]=1;}
                    // Negative model only: reset the phase for each part.
                    // Count owner changes on real overlap, so merely having
                    // different numeric bottoms cannot pass as a witness.
                    int wrongZ=((32768-part.bottom)/3)*3;
                    for(int row=part.bottom-1,phase=0;row>nativeY+sy-uy;--row)if(++phase==3){--wrongZ;phase=0;}
                    if(wrongZ<buildingZ[p])independentPartMask[p]=1;
                    if(role&&part.bottom!=unionBottom)++multipartPixels;
                }
            }
            if(!front){expected=wholeBack;for(std::size_t p=0;p<blank.size();++p)
                visibleMask[p]=unitMask[p]&&(buildingOnly[p]&0xffffffU)==(blank[p]&0xffffffU);}
            int overlap=0,unitVisible=0,buildingVisible=0,pickErrors=0,sharedPhaseWitness=0;
            for(std::size_t p=0;p<blank.size();++p) {
                if(!unitMask[p]||(buildingOnly[p]&0xffffffU)==(blank[p]&0xffffffU)
                    ||(unitOnly[p]&0xffffffU)==(buildingOnly[p]&0xffffffU))continue;
                ++overlap;if(visibleMask[p])++unitVisible;else ++buildingVisible;
                if(front&&visibleMask[p]!=independentPartMask[p])++sharedPhaseWitness;
                const auto* hit=findProjectedObjectAtPoint(projected,POINT{static_cast<LONG>(p%width),static_cast<LONG>(p/width)});
                if(!hit||!hit->object||hit->object->instanceId!=(visibleMask[p]?unitId:buildingId))++pickErrors;
            }
            const auto actual=paint({building,unit});const int mismatch=difference(actual,expected);
            const int orderMismatch=difference(actual,paint({unit,building})),repeatMismatch=difference(actual,paint({building,unit}));
            auto idBuilding=building,idUnit=unit;idBuilding.instanceId=unitId;idUnit.instanceId=buildingId;
            const int idMismatch=difference(actual,paint({idUnit,idBuilding}));
            int hdcMismatch=0;if(!pan){hdcMismatch=difference(actual,paint({building,unit},true));++hdcChecks;}
            const bool mixed=unitVisible>0&&buildingVisible>0;
            rows<<type<<','<<pose<<','<<zoom<<','<<facing<<','<<pan<<','<<partCount<<','<<overlap<<','<<unitVisible<<','<<buildingVisible
                <<','<<mismatch<<','<<pickErrors<<','<<orderMismatch<<','<<idMismatch<<','<<repeatMismatch<<','<<hdcMismatch
                <<','<<mixed<<','<<sharedPhaseWitness<<'\n';
            if(!pan&&(facing==0||facing==64)) {
                std::vector<std::uint32_t> sheet(width*4*height);
                for(int column=0;column<4;++column){const auto& frame=column==0?actual:column==1?expected:column==2?wholeFront:wholeBack;
                    for(int y=0;y<height;++y)std::copy_n(frame.begin()+y*width,width,sheet.begin()+y*width*4+column*width);}
                if(!writeRuntimeSmokeBmp(pathString(root/(label+"-actual-expected-front-back.bmp")),sheet.data(),width*4,height,error))return false;
            }
            if(mismatch||pickErrors||orderMismatch||idMismatch||repeatMismatch||hdcMismatch)
                return fail("pixels="+std::to_string(mismatch)+" picks="+std::to_string(pickErrors)+" order="+std::to_string(orderMismatch)
                    +" IDs="+std::to_string(idMismatch)+" repeat="+std::to_string(repeatMismatch)+" HDC="+std::to_string(hdcMismatch));
            if(pose==2&&zoom==48&&!mixed)return fail("side fixture lacks same-frame bidirectional overlap: unit="
                +std::to_string(unitVisible)+" building="+std::to_string(buildingVisible));
            mixedFrames+=mixed;sharedPhaseTotal+=sharedPhaseWitness;
            overlapTotal+=overlap;unitVisibleTotal+=unitVisible;buildingVisibleTotal+=buildingVisible;++cases;
        }
        rows.close();partsRows.close();if(!rows.good()||!partsRows.good())return fail("evidence write");
        if(!overlapTotal||!unitVisibleTotal||!buildingVisibleTotal||!multipartPixels||!mixedFrames)
            return fail("missing true same-frame bidirectional/multipart coverage");
        std::cout<<"Ground unit depth: "<<cases<<" cases, "<<hdcChecks<<" HDC, "<<negativeChecks<<" excluded-path checks; overlap="
            <<overlapTotal<<" unitVisible="<<unitVisibleTotal<<" buildingVisible="<<buildingVisibleTotal
            <<" differingPartBottomPixels="<<multipartPixels<<" mixedFrames="<<mixedFrames<<" sharedPhaseOwnerWitness="<<sharedPhaseTotal<<'\n';
        return true;
    }catch(const std::exception& detail){return fail(detail.what());}
}
