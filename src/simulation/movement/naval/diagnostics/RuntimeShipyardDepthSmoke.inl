// Stock shipyard/ship reproduction with a raw-resource, test-only row oracle.
// The solo renderer supplies colors, never the expected scene Z or ownership.
bool runRuntimeShipyardDepthSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const char* requested = std::getenv("CONCORD_SHIPYARD_DEPTH_OUTPUT");
    const std::filesystem::path root = requested && *requested ? requested
        : "build/smoke/feature-set-19-complete/05-shipyard-depth/checks";
    const bool diagnostic = std::getenv("CONCORD_SHIPYARD_DEPTH_DIAGNOSTIC") != nullptr;
    const bool minimal = std::getenv("CONCORD_SHIPYARD_DEPTH_MINIMAL") != nullptr;
    std::filesystem::create_directories(root);
    std::string label;
    const auto fail = [&](const std::string& why) {
        error = "shipyard depth " + label + ": " + why;
        std::cout << error << '\n'; return false;
    };
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config; assets->config.playerSlots.clear();
    assets->config.gameModeRulesOverride.clear();
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world,
        &assets->theater, &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("stock resources unavailable");
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    if(std::getenv("CONCORD_SHIPYARD_LIFECYCLE_ONLY"))
        return runRuntimeShipyardLifecycleSmoke(*assets,error);
    int waterTile = -1, landTile = -1;
    const int waterSet=assets->theater.getGeneralTileSetIndex("WaterSet");
    const auto terrainContext=runtimeTerrainMetadataContext(*assets);
    const auto* waterRules=assets->rules.findLandType("Water");
    if(waterSet<0||!waterRules||waterRules->floatSpeed!=100.0)
        return fail("stock WaterSet / Float=100% unavailable");
    for (int tile = 0; tile < static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell probe; probe.tileIndex = tile; probe.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(*assets, probe);
        if (!info.passable || info.shore || info.cliff || info.bridge) continue;
        // Cliff/Water names also satisfy the old broad gameplay water flag.
        // Require the authored open-water set and actual TMP movement type.
        if (info.water && waterTile < 0
            && assets->theater.resolveTileIndex(tile).tileSetIndex==waterSet
            && resolveRuntimeTerrainSubTileLandType(probe,terrainContext,assets->renderCache)==RuntimeTerrainLandType::Water
            && resolveRuntimeTerrainSubTileRampType(probe,terrainContext,assets->renderCache)==0)waterTile=tile;
        if (!info.water && landTile < 0) landTile = tile;
        if (waterTile >= 0 && landTile >= 0) break;
    }
    if (waterTile < 0 || landTile < 0) return fail("native open-water/land tiles unavailable");
    const auto waterReference=assets->theater.resolveTileIndex(waterTile);
    std::cout<<"Shipyard open water: tile="<<waterTile<<" set="<<waterReference.tileSetIndex
        <<" file="<<waterReference.expectedFileName<<" Float=100% TMP=Water ramp=0\n";
    using namespace RuntimeBuildingDepthOracle;
    const auto zShape=decodeFrameZero(retailTemplate(std::filesystem::current_path()));
    const auto decodeOriginalFrame=[](const Bytes& bytes,int frameIndex) {
        if(frameIndex<0||static_cast<unsigned>(frameIndex)>=little(bytes,6,2))
            throw std::runtime_error("independent frame index outside original SHP");
        Bytes selected=bytes;const std::size_t at=8+static_cast<std::size_t>(frameIndex)*24;
        if(at+24>selected.size())throw std::runtime_error("independent SHP header range");
        std::copy_n(bytes.begin()+at,24,selected.begin()+8);
        return decodeFrameZero(selected);
    };
    // Preserve the current raster's documented projection. Original retail
    // bounds/gradient rules are checked here; this is not a retail rasterizer.
    for(const char* setting:{"CONCORD_VXL_PROJ_GROUND_Y","CONCORD_VXL_PROJ_HEIGHT_Y",
            "CONCORD_VXL_SCREEN_Y_SCALE_MUL","CONCORD_VXL_HEIGHT_Y_SCALE_MUL"})
        if(std::getenv(setting))return fail(std::string("oracle requires default projection: ")+setting);
    const auto voxelBottom=[&](const RuntimeVoxelSourceCacheEntry& source,int facing,double fit) {
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
        const double c=std::cos(angle),sine=std::sin(angle),px=(xmin+xmax)/2,py=(ymin+ymax)/2;
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
        anchor/=model.size();
        // Retail 754510 truncates the complete eight-corner box and gives
        // four pixels of padding per side. No opaque crop or shadow extent.
        const int rows=static_cast<int>((high-low)*fit)+8;
        const int center=static_cast<int>(((low+high)/2-anchor)*fit);
        return center-rows/2+rows;
    };

    constexpr int yardId = 101, shipId = 102, powerId = 103;
    constexpr int width = 720, height = 540;
    const RECT viewport{0,0,width,height};
    struct Surface {
        HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ previous = nullptr;
        std::uint32_t* pixels = nullptr;
        Surface() {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            dc = CreateCompatibleDC(nullptr); void* raw = nullptr;
            if (dc) bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
            pixels = static_cast<std::uint32_t*>(raw);
            if (bitmap && pixels) previous = SelectObject(dc,bitmap);
        }
        ~Surface() {
            if (previous && dc) SelectObject(dc,previous);
            if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc);
        }
    } surface;
    if (!surface.dc || !surface.pixels) return fail("DIB allocation");
    const auto difference = [](const auto& a, const auto& b) {
        if (a.size() != b.size()) return std::numeric_limits<int>::max();
        int result = 0;
        for (std::size_t p = 0; p < a.size(); ++p) result += (a[p]&0xffffffU) != (b[p]&0xffffffU);
        return result;
    };
    const auto position = [](const RuntimeWindowState& s) {
        const auto* ship = findObjectById(s,shipId);
        return std::array<double,2>{runtimeObjectRenderX(*ship),runtimeObjectRenderY(*ship)};
    };
    const auto distance = [](const auto& a, const auto& b) { return std::hypot(a[0]-b[0],a[1]-b[1]); };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s,false);
    };
    int routes = 0, saves = 0, pairedTicks = 0, renderCases = 0, hdcChecks = 0;
    std::uint64_t overlapPixels = 0, visibleShipPixels = 0, visibleYardPixels = 0;
    std::uint64_t frontWrongPixels=0,backWrongPixels=0;
    std::array<std::uint64_t,2> alphaWitnesses{};int alphaCases=0;
    std::ofstream lifecycle(root/"repair-lifecycle.csv"), pixelsReport(root/"shipyard-overlap.csv");
    std::ofstream sourcesReport(root/"oracle-resources.csv");
    std::ofstream alphaReport(root/"shipyard-frozen-blends.csv");
    alphaReport<<"mode,zoom,facing,shared_pixel_witnesses,pixel_mismatch,hdc_mismatch,repeat_mismatch,restored_mismatch\n";
    sourcesReport<<"yard,ship,pose,zoom,facing,vxl_sha256,hva_sha256,ship_shp_sha256,yard_shp_sha256,native_bottom\n";
    lifecycle << "yard,ship,tick,phase,x,y,renderX,renderY,health,credits,docked,step\n";
    pixelsReport << "yard,ship,pose,zoom,facing,pan,overlap_pixels,ship_color_pixels,yard_color_pixels,"
        "other_color_pixels,pick_consistency_errors,insertion_mismatch,id_mismatch,hdc_mismatch,"
        "repeat_mismatch,oracle_mismatch,whole_ship_front_wrong,whole_ship_back_wrong,native_bottom,coverage\n";
    const auto saveLoad = [&](RuntimeWindowState& s, const std::string& suffix,
        std::unique_ptr<RuntimeWindowState>& clone) {
        const auto path = root/(label+"-"+suffix+".ini");
        if (!saveRuntimeState(s,pathString(path),error)) return fail("full save " + suffix + ": " + error);
        clone = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*clone,s,false);
        clone->runtimeSoundsEnabled = clone->runtimePreferencePersistenceEnabled = false;
        clone->runtimeSuppressPlayerAudioFeedback = true;
        RuntimeSnapshotActiveStateScope active(*clone);
        if (!loadRuntimeState(*clone,pathString(path),error)) return fail("full load " + suffix + ": " + error);
        const auto* original = findObjectById(s,shipId); const auto* restored = findObjectById(*clone,shipId);
        if (!restored || distance(position(s),position(*clone)) > .000001
            || original->health != restored->health || original->dockedToObjectId != restored->dockedToObjectId
            || s.repairDepots.stateHash() != clone->repairDepots.stateHash()
            || s.economy.credits != clone->economy.credits)
            return fail("full load changed repair/continuous pose " + suffix);
        ++saves; return true;
    };
    const auto compare = [&](const RuntimeWindowState& s,const RuntimeWindowState& clone) {
        const auto* a = findObjectById(s,shipId); const auto* b = findObjectById(clone,shipId);
        if (!a || !b || distance(position(s),position(clone)) > .000001
            || a->health != b->health || a->dockedToObjectId != b->dockedToObjectId
            || a->dockIndex != b->dockIndex || a->x != b->x || a->y != b->y
            || s.economy.credits != clone.economy.credits)
            return fail("continued repair diverged at tick " + std::to_string(s.tick));
        ++pairedTicks; return true;
    };
    const auto capture = [&](RuntimeWindowState& s,const std::string& suffix) {
        const auto selected = s.selectedObjectIds; s.selectedObjectIds.clear();
        const auto stats = drawRuntimeStateOffscreenAtCell(s,{22,22},width,height,error,
            pathString(root/(label+"-"+suffix+".bmp")),false);
        s.selectedObjectIds = selected;
        if (stats && stats->rendered && stats->visibleVoxelBodies >= 1) return true;
        return fail("original ship rendering " + suffix + ": " + error
            + " worldLoaded=" + std::to_string(s.world.getSummary().loaded)
            + " terrain=" + std::to_string(s.world.getTerrainCells().size())
            + " objects=" + std::to_string(s.world.getObjects().size())
            + " stats=" + std::to_string(stats.has_value())
            + " rendered=" + std::to_string(stats && stats->rendered)
            + " voxelBodies=" + std::to_string(stats ? stats->visibleVoxelBodies : -1));
    };

    const std::array<std::array<const char*,4>,3> specimens{{
        {{"GAYARD","CARRIER","Americans","GAPOWR"}},
        {{"NAYARD","DRED","Russians","NAPOWR"}},
        {{"YAYARD","AEGIS","YuriCountry","YAPOWR"}}
    }};
    for (std::size_t specimen = 0; specimen < (minimal ? 1U : specimens.size()); ++specimen) {
        const auto& type = specimens[specimen]; label = std::string(type[0])+"-"+type[1];
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = assets->config; s.config.fixedTickHz = 30; s.config.noCooldown = false;
        s.config.playerCountry = type[2]; s.config.computerAiEnabled = s.config.multiplayerActive = false;
        s.config.crates = s.config.shortGame = false;
        s.rules = assets->rules; s.art = assets->art; s.theater = assets->theater; s.report = assets->report;
        // GameWorld's loaded summary is established by the original map load;
        // populating vectors on a default GameWorld does not establish it.
        s.world=assets->world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.economy.owner = type[2]; s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        initializeRuntimeLocalProductionLanes(s.economy);
        RuntimeHumanHouseAuthority authority; authority.owner = type[2];
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(type[2]); authority.localFacade = true;
        s.humanHouseAuthorities.push_back(authority); s.ownerRemapColors[type[2]] = RGB(235,210,45);
        for (int y = 10; y <= 40; ++y) for (int x = 10; x <= 40; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex = x>=34 && y>=34 ? landTile : waterTile; cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        const auto add = [&](int id,const char* name,WorldObjectKind kind,int x,int y) {
            WorldObjectInstance object; object.instanceId=id; object.type=name; object.kind=kind;
            object.mapId=label+"-"+std::to_string(id); object.owner=type[2];
            object.x=x; object.y=y; object.cell=runtimeCellKey(x,y); object.alive=object.poweredOperational=true;
            object.health=runtimeMaxHealthForObject(s,object); object.mission="Guard"; object.currentAmmo=-1;
            clearRuntimeRenderPosition(object); s.world.accessObjects().push_back(object);
        };
        add(yardId,type[0],WorldObjectKind::Structure,20,20);
        add(shipId,type[1],WorldObjectKind::Unit,27,21);
        add(powerId,type[3],WorldObjectKind::Structure,35,35);
        findMutableObjectById(s,shipId)->health-=128;
        initializeRuntimeNavigation(s); s.structureFootprints=buildRuntimeStructureFootprints(s);
        rebuildRuntimeEconomyPower(s); ensureRuntimeRepairDepotsInitialized(s);
        RuntimeSnapshotActiveStateScope active(s);
        const auto* yardRule=s.rules.findBuildingType(type[0]); const auto* yardArt=s.art.findArtType(type[0]);
        if (!yardRule || !yardArt || !yardRule->unitRepair || !yardRule->naval || !yardRule->waterBound
            || yardRule->numberOfDocks!=1 || yardArt->foundation!="4x4") return fail("stock yard contract");
        const auto authored = runtimeAircraftDockSpawn(s,*findObjectById(s,yardId),0);
        if (authored.cell!=RuntimeCellPoint{23,21} || authored.renderX!=23.5 || authored.renderY!=21.5)
            return fail("original DockingOffset no longer maps to independent (23.5,21.5)");
        if (!capture(s,"before-order")) return false;
        s.selectedObjectIds={shipId};
        const auto beforeCommand=position(s);
        if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {20,20},findObjectById(s,yardId))
            || s.lastCommand.verb!="unitRepair" || s.lastCommand.acceptedCount!=1
            || distance(position(s),beforeCommand)>.000001) return fail("real repair order / position jump");
        const int initialHealth=findObjectById(s,shipId)->health, initialCredits=s.economy.credits;
        const double maximumStep=1.5/std::max(1.0,std::floor(runtimeMovementBaseIntervalTicks(s,*findObjectById(s,shipId))))+.000001;
        std::unique_ptr<RuntimeWindowState> clone;
        bool docked=false,approachSaved=false;
        const auto log=[&](const char* phase,double displacement) {
            const auto* ship=findObjectById(s,shipId); const auto p=position(s);
            lifecycle<<type[0]<<','<<type[1]<<','<<s.tick<<','<<phase<<','<<ship->x<<','<<ship->y
                <<','<<p[0]<<','<<p[1]<<','<<ship->health<<','<<s.economy.credits
                <<','<<ship->dockedToObjectId<<','<<displacement<<'\n';
        };
        for (int n=0;n<2700;++n) {
            const auto previous=position(s); step(s); const double displacement=distance(previous,position(s));
            if (clone) {step(*clone); if(!compare(s,*clone))return false;}
            log("approach",displacement);
            if(displacement>maximumStep)return fail("real approach jumped: "+std::to_string(displacement)
                +" limit="+std::to_string(maximumStep));
            const auto* ship=findObjectById(s,shipId); const auto behavior=s.behaviors.find(shipId);
            if(!approachSaved && ship->x==23 && ship->y==21 && ship->renderMoveActive
                && behavior!=s.behaviors.end() && behavior->second.renderMoveTicksRemaining>1
                && behavior->second.renderMoveTicksRemaining<behavior->second.renderMoveTicksTotal) {
                if(!saveLoad(s,"approach",clone)||!capture(s,"approach"))return false;
                approachSaved=true;
            }
            if(ship->dockedToObjectId==yardId){docked=true;break;}
        }
        if(!docked||!approachSaved||distance(position(s),std::array<double,2>{23.5,21.5})>.000001)
            return fail("real route did not dock at authored half-cell or lacked mid-approach save");
        const auto dockedYard=*findObjectById(s,yardId), dockedShip=*findObjectById(s,shipId);
        const int dockedTick=s.tick;
        if(!capture(s,"docked")||!saveLoad(s,"docked",clone))return false;
        bool healed=false;
        for(int n=0;n<600;++n) {
            step(s);step(*clone);if(!compare(s,*clone))return false;log("repair",0.0);
            if(findObjectById(s,shipId)->health>initialHealth&&s.economy.credits<initialCredits){healed=true;break;}
        }
        if(!healed||!capture(s,"paid-repair"))return fail("actual paid repair missing");
        const auto departure=position(s); s.selectedObjectIds={shipId};clone->selectedObjectIds={shipId};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{27,25}))
            return fail("real departure order rejected");
        {RuntimeSnapshotActiveStateScope other(*clone);
            if(!dispatchRuntimeTargetOrderFromUi(*clone,RuntimeMultiplayerGameplayTargetOrderKind::Move,{27,25}))
                return fail("restored departure order rejected");}
        if(distance(departure,position(s))>.000001||distance(departure,position(*clone))>.000001)
            return fail("release lost half-cell pose before departure");
        bool left=false;
        for(int n=0;n<2700;++n) {
            const auto previous=position(s);step(s);step(*clone);if(!compare(s,*clone))return false;
            const double displacement=distance(previous,position(s));log("departure",displacement);
            if(displacement>maximumStep)return fail("departure jumped: "+std::to_string(displacement));
            if(n==0&&!capture(s,"departure-first-step"))return false;
            const auto* ship=findObjectById(s,shipId);
            if(ship->x==27&&ship->y==25&&!ship->renderMoveActive&&!s.behaviors.contains(shipId)){left=true;break;}
        }
        if(!left||findObjectById(s,shipId)->dockedToObjectId>=0
            ||findRuntimeRepairDepotRelationForUnit(s.repairDepots,shipId)||!capture(s,"outside"))
            return fail("departure did not release/finish");
        ++routes;

        // Static real-resource matrix is separate from the actual route. Both
        // the stock sprites and the genuine docked coordinates are retained.
        auto renderBox=std::make_unique<RuntimeWindowState>();auto& r=*renderBox;
        seedRuntimeLoadScratch(r,s,false);r.tick=dockedTick;r.selectedObjectIds.clear();
        r.runtimeSoundsEnabled=r.runtimePreferencePersistenceEnabled=false;r.runtimeSuppressPlayerAudioFeedback=true;
        RuntimeMapRenderContext view;view.rules=&r.rules;view.art=&r.art;view.theater=&r.theater;
        view.assetRootPath=runtimeAssetRootPath(r.config);view.structureRootPath=runtimeStructureRootPath(r.config,r.report.map);
        view.palettePath=runtimePalettePath(r.config,r.report.map);view.structurePalettePath=runtimeStructurePalettePath(r.config,r.report.map);
        view.ownerRemapColors=&r.ownerRemapColors;view.animationTick=dockedTick;view.fixedTickHz=30;
        view.useTileTextures=view.shadowsEnabled=false;view.surfacePixels=surface.pixels;
        view.seeHiddenObjects=false; // Numerical ship/body oracle; Behind is checked separately below.
        view.surfaceWidth=width;view.surfaceHeight=height;
        auto cache=std::make_unique<RuntimeMapRenderCache>();GameWorld paintWorld=r.world;
        RuntimeSnapshotActiveStateScope renderActive(r);
        std::vector<std::string> renderTypes{type[1]};
        if(!minimal&&specimen==1)renderTypes.push_back("DLPH");
        if(!minimal&&specimen==2)renderTypes.push_back("SQD");
        for(const auto& renderType:renderTypes)for(int pose=0;pose<(minimal?1:3);++pose)
        for(int zoom:{48,60})for(int facing:{0,32,64,96,128,160,192,224})for(bool pan:{false,true}) {
            auto yard=dockedYard,ship=dockedShip;ship.facing=ship.turretFacing=facing;
            ship.type=renderType;ship.health=runtimeMaxHealthForObject(r,ship);ship.mission="Guard";
            ship.submerged=ship.cloaked=false;ship.spriteFiringStartTick=-1;
            if(pose) {
                ship.x=pose==1?19:25;ship.y=pose==1?19:21;ship.cell=runtimeCellKey(ship.x,ship.y);
                clearRuntimeRenderPosition(ship);ship.dockedToObjectId=ship.dockIndex=-1;
            }
            const auto* shipArt=r.art.findArtType(renderType);
            const auto* shipRule=r.rules.findUnitType("vehicle",renderType);
            if(!shipArt||!shipRule||shipRule->turret)return fail("stock naval body/visible SHP fixture contract");
            const bool shpShip=!shipArt->voxel;
            const std::string poseName=pose==0?"dock":pose==1?"rear":"front";
            const std::string matrixLabel=std::string(type[0])+"-"+renderType+"-"+poseName;
            r.world.accessObjects()={yard,ship};r.camera.initialized=true;r.camera.tileWidth=zoom;r.camera.tileHeight=zoom/2;
            r.camera.worldOffsetX=-width/2+(pan?31:0);
            r.camera.worldOffsetY=22*zoom/2-height/2+(pan?-19:0);
            invalidateRuntimeObjectProjectionCache(r);(void)runtimeProjectedObjectsForViewport(r,viewport);
            RuntimeMapRenderStats lastPaintStats;
            const auto paint=[&](const std::vector<WorldObjectInstance>& objects,bool hdc=false,bool markers=false) {
                paintWorld.accessObjects()=objects;auto camera=r.camera;auto context=view;context.seeHiddenObjects=markers;
                if(hdc){context.surfacePixels=nullptr;context.surfaceWidth=context.surfaceHeight=0;}
                lastPaintStats=drawRuntimeMap(surface.dc,viewport,paintWorld,context,camera,*cache);
                if(!lastPaintStats.rendered)throw std::runtime_error("shipyard matrix draw failed");
                GdiFlush();return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);
            };
            const auto blank=paint({}),yardOnly=paint({yard}),shipOnly=paint({ship});
            // These deliberately separated event streams avoid presuming a
            // whole-object order: rear unit -> body -> animations, or
            // body -> animations -> dock/front unit. Unit draws never write Z.
            const double shipSort=(runtimeObjectRenderX(ship)+runtimeObjectRenderY(ship)+1)*256;
            const double yardSort=(yard.x+yard.y)*256;
            if(yardArt->normalZAdjust||yardArt->zShapePointMove!=std::array<int,2>{}
                ||!yardArt->bibShape.empty())return fail("dock oracle requires original unshifted body");
            for(const auto* animations:{&yardArt->activeAnimations,&yardArt->specialAnimations,&yardArt->auxiliaryAnimations})
                for(const auto& animation:*animations)
                    if(animation.offsetX||animation.offsetY||animation.ySort<0
                        ||(pose==1?shipSort>=yardSort:shipSort<=yardSort+animation.ySort))
                        return fail("oracle event order lacks strict original separation");
            const RuntimeAuxiliaryShpBytesCacheEntry* bodyBytes=nullptr;
            for(const auto& [key,value]:cache->objectShpBytesByImage)
                if(value.loaded&&toLowerAsciiLocal(key).find("|"+toLowerAsciiLocal(type[0])+"|")!=std::string::npos) {
                    if(bodyBytes&&bodyBytes->bytes!=value.bytes)return fail("ambiguous independent yard source");
                    bodyBytes=&value;
                }
            const RuntimeVoxelCacheEntry* raster=nullptr;
            for(const auto& [key,value]:cache->voxelObjectsByType)
                if(value.loaded&&value.hasSprite&&toLowerAsciiLocal(value.metadata.imageId)==toLowerAsciiLocal(renderType)
                    &&value.facingBucket==((facing+224)%256)/8&&value.animationFrameIndex==0) {
                    if(raster&&raster->image.pixels!=value.image.pixels)return fail("ambiguous independent ship raster");
                    raster=&value;
                }
            const RuntimeVoxelSourceCacheEntry* source=nullptr;
            if(raster)for(const auto& [key,value]:cache->voxelSourcesByImage)
                if(value.loaded&&value.metadata.vxlPath==raster->metadata.vxlPath)source=&value;
            const RuntimeAuxiliaryShpBytesCacheEntry* shipBytes=nullptr;
            if(shpShip)for(const auto& [key,value]:cache->objectShpBytesByImage)
                if(value.loaded&&toLowerAsciiLocal(key).find("|"+toLowerAsciiLocal(renderType)+"|")!=std::string::npos)
                    shipBytes=&value;
            if(!bodyBytes||(shpShip?!shipBytes:(!raster||!source)))return fail("oracle original sources absent: body="
                +std::to_string(bodyBytes!=nullptr)+" raster="+std::to_string(raster!=nullptr)
                +" source="+std::to_string(source!=nullptr)+" shp="+std::to_string(shipBytes!=nullptr));
            const auto body=decodeFrameZero(bodyBytes->bytes);
            const int yardCenterX=static_cast<int>(std::lround((yard.x-yard.y)*zoom/2.0))-r.camera.worldOffsetX;
            const int yardCenterY=static_cast<int>(std::lround((yard.x+yard.y)*zoom/4.0))-r.camera.worldOffsetY;
            const int shipCenterX=static_cast<int>(std::lround((runtimeObjectRenderX(ship)-runtimeObjectRenderY(ship))*zoom/2.0))-r.camera.worldOffsetX;
            const int shipCenterY=static_cast<int>(std::lround((runtimeObjectRenderX(ship)+runtimeObjectRenderY(ship))*zoom/4.0))-r.camera.worldOffsetY;
            const int nativeShipY=static_cast<int>(std::lround((runtimeObjectRenderX(ship)+runtimeObjectRenderY(ship))*15.0));
            OriginalFrame shipFrame;
            const Bytes* shipPixels=nullptr;int shipLeft=0,shipTop=0,shipWidth=0,shipHeight=0,nativeBottom=0;
            if(shpShip) {
                if(shipArt->walkFrames<=0||shipRule->idleRate<=0||!shipRule->underwater
                    ||shipArt->standingFrames>0||shipArt->startStandFrame>=0)
                    return fail("independent stock visible SHP idle contract");
                const int frame=shipArt->walkFrames*(facing/32)
                    +(static_cast<std::int64_t>(dockedTick)*15/(30*shipRule->idleRate))%shipArt->walkFrames;
                shipFrame=decodeOriginalFrame(shipBytes->bytes,frame);shipPixels=&shipFrame.pixels;
                shipWidth=shipFrame.width;shipHeight=shipFrame.height;
                shipLeft=shipCenterX-shipFrame.canvasWidth/2+shipFrame.x;
                // Existing underwater canvas alignment belongs to both the
                // revealed and submerged art; depth uses its actual drawn rect.
                shipTop=shipCenterY+zoom/4-shipFrame.canvasHeight+shipFrame.y
                    +std::max(0,shipFrame.canvasHeight/2-zoom/2-10);
                nativeBottom=nativeShipY+shipTop-shipCenterY+shipHeight;
            } else {
                const auto& image=raster->image;shipPixels=&image.pixels;
                shipWidth=image.width;shipHeight=image.height;
                shipLeft=shipCenterX-image.globalWidth/2+image.offsetX;
                shipTop=shipCenterY+image.offsetY-image.globalHeight;
                nativeBottom=nativeShipY+voxelBottom(*source,facing,raster->rasterFitScale);
            }
            const int yardLeft=yardCenterX-body.canvasWidth/2+body.x;
            const int yardTop=yardCenterY-body.canvasHeight/2+body.y;
            const int nativeYardTop=(yard.x+yard.y)*15-body.canvasHeight/2+body.y;
            const int templateX=body.x-body.canvasWidth/2+198;
            const int templateY=body.y-body.canvasHeight/2+356; // 446 - 15*(4+4-2)
            const int bodyRows=std::min(body.height,zShape.height-templateY)-std::max(0,-templateY);
            std::vector<int> yardZ(width*height,std::numeric_limits<int>::max());
            for(int y=0;y<body.height;++y)for(int x=0;x<body.width;++x) {
                if(!body.pixels[static_cast<std::size_t>(y)*body.width+x])continue;
                const int sx=yardLeft+x,sy=yardTop+y;
                if(sx<0||sy<0||sx>=width||sy>=height)continue;
                const int u=templateX+x,v=templateY+y;int z=0;
                if(u>=0&&v>=0&&u<zShape.width&&v<zShape.height) {
                    const int raw=zShape.pixels[static_cast<std::size_t>(v)*zShape.width+u];
                    int signedValue=raw?raw-65:0;if(signedValue>127)signedValue-=256;if(signedValue< -128)signedValue+=256;
                    z=32768-nativeYardTop-bodyRows-1-signedValue;
                } else {
                    // Item 3's explicit compatibility fallback for authored
                    // body pixels outside the finite original template.
                    z=((32768-nativeYardTop-body.height-1)/3)*3;
                    for(int row=body.height-1;row>y;--row)if((body.height-1-row)%3==2)--z;
                }
                yardZ[static_cast<std::size_t>(sy)*width+sx]=z;
            }
            auto expected=yardOnly,wholeFront=yardOnly,wholeBack=shipOnly;
            std::vector<unsigned char> shipVisibleMask(width*height,0);
            for(std::size_t p=0;p<blank.size();++p)
                if((yardOnly[p]&0xffffffU)!=(blank[p]&0xffffffU))wholeBack[p]=yardOnly[p];
            for(int y=0;y<shipHeight;++y)for(int x=0;x<shipWidth;++x) {
                if(!(*shipPixels)[static_cast<std::size_t>(y)*shipWidth+x])continue;
                const int sx=shipLeft+x,sy=shipTop+y;
                if(sx<0||sy<0||sx>=width||sy>=height)continue;
                const auto p=static_cast<std::size_t>(sy)*width+sx;wholeFront[p]=shipOnly[p];
                const int nativeRow=nativeShipY+sy-shipCenterY;
                // Execute the retail gradient's backward row accumulation;
                // do not call the production depth or ownership helpers.
                int z=((32768-nativeBottom-(shpShip?2:0))/3)*3;
                for(int row=nativeBottom-1,phase=0;row>nativeRow;--row)
                    if(++phase==3){--z;phase=0;}
                if(z<yardZ[p]){expected[p]=shipOnly[p];shipVisibleMask[p]=1;}
            }
            if(pose==1) {
                expected=wholeBack;
                for(std::size_t p=0;p<blank.size();++p)shipVisibleMask[p]=
                    (shipOnly[p]&0xffffffU)!=(blank[p]&0xffffffU)&&(yardOnly[p]&0xffffffU)==(blank[p]&0xffffffU);
            }
            sourcesReport<<type[0]<<','<<renderType<<','<<poseName<<','<<zoom<<','<<facing<<','
                <<(source?sha256(source->vxlBytes):"")<<','<<(source?sha256(source->hvaBytes):"")<<','
                <<(shipBytes?sha256(shipBytes->bytes):"")<<','<<sha256(bodyBytes->bytes)<<','<<nativeBottom<<'\n';
            const auto projected=runtimeProjectedObjectsForViewport(r,viewport);
            std::vector<std::pair<std::size_t,int>> beforePicks;
            for(std::size_t p=0;p<blank.size();++p) {
                if((yardOnly[p]&0xffffffU)==(blank[p]&0xffffffU)
                    ||(shipOnly[p]&0xffffffU)==(blank[p]&0xffffffU)
                    ||(shipOnly[p]&0xffffffU)==(yardOnly[p]&0xffffffU))continue;
                const auto* picked=findProjectedObjectAtPoint(projected,
                    POINT{static_cast<LONG>(p%width),static_cast<LONG>(p/width)});
                beforePicks.emplace_back(p,picked&&picked->object?picked->object->instanceId:-1);
            }
            const auto actual=paint({yard,ship}),repeat=paint({yard,ship});
            const int oracleMismatch=difference(actual,expected);
            const int wholeFrontWrong=difference(expected,wholeFront),wholeBackWrong=difference(expected,wholeBack);
            frontWrongPixels+=wholeFrontWrong;backWrongPixels+=wholeBackWrong;
            const int insertionMismatch=difference(actual,paint({ship,yard}));
            auto idYard=yard,idShip=ship;idYard.instanceId=shipId;idShip.instanceId=yardId;
            if(idShip.dockedToObjectId==yardId)idShip.dockedToObjectId=shipId;
            const int idMismatch=difference(actual,paint({idShip,idYard}));
            const int repeatMismatch=difference(actual,repeat);
            int hdcMismatch=0;if(!pan){hdcMismatch=difference(actual,paint({yard,ship},true));++hdcChecks;}
            int shipVisible=0,yardVisible=0,other=0,pickErrors=0;
            for(const auto& [p,picked]:beforePicks) {
                const auto color=actual[p]&0xffffffU;
                if(color==(shipOnly[p]&0xffffffU))++shipVisible;
                else if(color==(yardOnly[p]&0xffffffU))++yardVisible;
                else ++other;
                const int expectedOwner=shipVisibleMask[p]?shipId:yardId;
                if(picked!=expectedOwner)++pickErrors;
            }
            overlapPixels+=beforePicks.size();visibleShipPixels+=shipVisible;visibleYardPixels+=yardVisible;
            pixelsReport<<type[0]<<','<<renderType<<','<<poseName<<','<<zoom<<','<<facing<<','<<pan<<','<<beforePicks.size()
                <<','<<shipVisible<<','<<yardVisible<<','<<other<<','<<pickErrors<<','<<insertionMismatch
                <<','<<idMismatch<<','<<hdcMismatch<<','<<repeatMismatch<<','<<oracleMismatch
                <<','<<wholeFrontWrong<<','<<wholeBackWrong<<','<<nativeBottom
                <<','<<(beforePicks.empty()?"nonoverlap-control":"independent-native-rows")<<'\n';
            if(!pan&&(minimal||facing==0||facing==64)) {
                std::vector<std::uint32_t> sheet(width*4*height);
                for(int column=0;column<4;++column) {
                    const auto& frame=column==0?actual:column==1?expected:column==2?wholeFront:wholeBack;
                    for(int y=0;y<height;++y)std::copy_n(frame.begin()+y*width,width,sheet.begin()+y*width*4+column*width);
                }
                if(!writeRuntimeSmokeBmp(pathString(root/(matrixLabel+"-z"+std::to_string(zoom)+"-f"
                    +std::to_string(facing)+"-actual-expected-front-back.bmp")),sheet.data(),width*4,height,error))return false;
            }
            if(specimen==0&&renderType=="CARRIER"&&pose==1&&zoom==48&&facing==0&&!pan){
                // Independently pinned BEHIND frame 4 at the original real-route
                // dock tick 303: Rate200 gives delay8; LoopStart1+(303/8)%17=4.
                const auto offStats=lastPaintStats;const auto marked=paint({yard,ship},false,true);const auto onStats=lastPaintStats;
                const RuntimeAuxiliaryShpBytesCacheEntry* markerBytes=nullptr;
                for(const auto& [key,value]:cache->auxiliaryShpBytesByImage)
                    if(value.loaded&&toLowerAsciiLocal(key).find("|behind|")!=std::string::npos)markerBytes=&value;
                if(!markerBytes||sha256(markerBytes->bytes)!="ef26a68a4579fc630b1743ec0febe42b4f09182d80b0ab708f012c716f33f4e3"
                    ||dockedTick!=303||offStats.behindMarkerSprites!=0||onStats.behindMarkerSprites!=1||onStats.behindMarkerFrame!=4)
                    return fail("native naval Behind on/off resource/frame/count contract");
                const auto marker=decodeOriginalFrame(markerBytes->bytes,4);std::vector<unsigned char> mask(width*height);
                int nonzero=0,outside=0,pickChanges=0;
                for(int y=0;y<marker.height;++y)for(int x=0;x<marker.width;++x){
                    if(!marker.pixels[static_cast<std::size_t>(y)*marker.width+x])continue;++nonzero;
                    const int px=shipCenterX-marker.canvasWidth/2+marker.x+x,py=shipCenterY-8-marker.canvasHeight/2+marker.y+y;
                    if(px>=0&&py>=0&&px<width&&py<height)mask[static_cast<std::size_t>(py)*width+px]=1;}
                const int changed=difference(marked,actual);
                for(std::size_t p=0;p<actual.size();++p)if(!mask[p]&&(marked[p]&0xffffffU)!=(actual[p]&0xffffffU))++outside;
                for(const auto& [p,owner]:beforePicks){const auto* hit=findProjectedObjectAtPoint(projected,
                    POINT{static_cast<LONG>(p%width),static_cast<LONG>(p/width)});
                    if((hit&&hit->object?hit->object->instanceId:-1)!=owner)++pickChanges;}
                std::cout<<"Shipyard Behind control: tick="<<dockedTick<<" frame="<<onStats.behindMarkerFrame
                    <<" alpha="<<nonzero<<" changed="<<changed<<" outside_original_mask="<<outside<<" pickChanges="<<pickChanges<<'\n';
                std::vector<std::uint32_t> sheet(width*3*height);for(int column=0;column<3;++column){
                    const auto& pixels=column==0?actual:column==1?expected:marked;
                    for(int y=0;y<height;++y)std::copy_n(pixels.begin()+y*width,width,sheet.begin()+y*width*3+column*width);}
                if(!writeRuntimeSmokeBmp(pathString(root/"naval-behind-on-off-control.bmp"),sheet.data(),width*3,height,error))return false;
                if(nonzero!=215||changed!=215||outside||pickChanges)return fail("native naval Behind raw mask/picking contract");
            }
            if(insertionMismatch||idMismatch||hdcMismatch||repeatMismatch)
                return fail("matrix unstable under order/ID/HDC/pause");
            if(!diagnostic&&(oracleMismatch||pickErrors))return fail(matrixLabel+" independent row oracle z="
                +std::to_string(zoom)+" facing="+std::to_string(facing)+" pan="+std::to_string(pan)
                +" pixels="+std::to_string(oracleMismatch)+" picks="+std::to_string(pickErrors));
            if(!minimal&&specimen==0&&pose==0&&!pan) {
                const auto mix=[](std::uint32_t background,std::uint32_t foreground,unsigned opacity) {
                    std::uint32_t result=0;
                    for(unsigned shift:{0U,8U,16U})result|=((((foreground>>shift)&255U)*opacity
                        +((background>>shift)&255U)*(255U-opacity)+127U)/255U)<<shift;
                    return result;
                };
                // This assertion covers the independently proven event path
                // background -> frozen yard -> visible foreground ship. It
                // does not invent an oracle for a late ship behind frozen Z.
                for(int mode=0;mode<2;++mode) {
                    auto translucentShip=ship;translucentShip.cloaked=mode==0;
                    SpecialMechanismRenderSnapshot freeze;freeze.kind="temporal";
                    freeze.targetObjectId=yard.instanceId;freeze.requiredProgress=10000;freeze.progress=100;
                    std::vector<SpecialMechanismRenderSnapshot> snapshots{freeze};
                    if(mode==1){freeze.targetObjectId=ship.instanceId;snapshots.push_back(freeze);}
                    view.specialMechanismSnapshots=&snapshots;
                    const auto blended=paint({yard,translucentShip});
                    const int blendHdc=difference(blended,paint({yard,translucentShip},true));
                    const int blendRepeat=difference(blended,paint({yard,translucentShip}));
                    int witnesses=0,blendErrors=0;
                    std::vector<std::uint32_t> witnessImage(width*height,0);
                    for(const auto& [p,picked]:beforePicks) {
                        (void)picked;if(!shipVisibleMask[p])continue;
                        const auto expectedBlend=mix(mix(blank[p],yardOnly[p],128),shipOnly[p],mode==0?144:128);
                        // Demand that ignoring the rear freeze would change
                        // this pixel, not merely that both masks touch it.
                        if(expectedBlend==mix(yardOnly[p],shipOnly[p],mode==0?144:128))continue;
                        ++witnesses;witnessImage[p]=expectedBlend;
                        blendErrors+=(blended[p]&0xffffffU)!=(expectedBlend&0xffffffU);
                    }
                    view.specialMechanismSnapshots=nullptr;
                    const int restoredMismatch=difference(actual,paint({yard,ship}));
                    alphaWitnesses[mode]+=witnesses;++alphaCases;
                    alphaReport<<(mode==0?"yard-frozen-ship-cloaked":"both-frozen")<<','<<zoom<<','<<facing
                        <<','<<witnesses<<','<<blendErrors<<','<<blendHdc<<','<<blendRepeat<<','<<restoredMismatch<<'\n';
                    if(witnesses&&facing==0) {
                        if(!writeRuntimeSmokeBmp(pathString(root/(matrixLabel+"-z"+std::to_string(zoom)
                            +"-alpha"+std::to_string(mode)+"-actual.bmp")),blended.data(),width,height,error)
                            ||!writeRuntimeSmokeBmp(pathString(root/(matrixLabel+"-z"+std::to_string(zoom)
                            +"-alpha"+std::to_string(mode)+"-expected-witness-only.bmp")),witnessImage.data(),width,height,error))return false;
                    }
                    if(blendHdc||blendRepeat||restoredMismatch||(!diagnostic&&blendErrors))
                        return fail("independent frozen chain mode="+std::to_string(mode)+" pixels="
                            +std::to_string(blendErrors)+" HDC="+std::to_string(blendHdc)
                            +" repeat="+std::to_string(blendRepeat)+" restored="+std::to_string(restoredMismatch));
                }
            }
            ++renderCases;
        }
    }
    lifecycle.close();pixelsReport.close();sourcesReport.close();alphaReport.close();
    if(!lifecycle.good()||!pixelsReport.good()||!sourcesReport.good()||!alphaReport.good())return fail("evidence CSV write");
    if(!overlapPixels)return fail("no real ship/yard pixel overlap in matrix");
    // The original GAYARD/CARRIER dock is a one-sided overlap: every shared
    // pixel can correctly belong to the carrier. The full placement/yard
    // matrix must additionally prove the opposite ordering, not invent it
    // for a reduced diagnostic subset.
    if((!minimal&&(!frontWrongPixels||!backWrongPixels))
        ||(minimal&&!frontWrongPixels&&!backWrongPixels))
        return fail("independent oracle lacks required whole-order counterexamples");
    if(!minimal&&(!alphaWitnesses[0]||!alphaWitnesses[1]))return fail("frozen chain lacks genuine shared-pixel witnesses");
    std::cout<<"Shipyard reproduction: "<<routes<<" actual repair routes, "<<saves<<" full saves/loads, "
        <<pairedTicks<<" paired ticks, "<<renderCases<<" original render cases, "<<hdcChecks
        <<" HDC comparisons; overlap="<<overlapPixels<<" shipVisible="<<visibleShipPixels
        <<" yardVisible="<<visibleYardPixels<<"; independent whole-front wrong="<<frontWrongPixels
        <<" whole-back wrong="<<backWrongPixels<<" alphaCases="<<alphaCases<<" alphaWitness="
        <<alphaWitnesses[0]<<'/'<<alphaWitnesses[1]<<(diagnostic?" diagnostic only":" strict oracle passed")<<'\n';
    return true;
}
