bool runRuntimeBuildingRepairSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Building repair: " + why; return false; };
    const char* output = std::getenv("CONCORD_BUILDING_REPAIR_OUTPUT");
    const std::string root = output && *output ? output : "build/building-repair-smoke";
    std::filesystem::create_directories(root);
    constexpr int width = 960, height = 640;
    constexpr std::uint32_t background = 0x002e4037;
    const RECT client{0, 0, width, height}, viewport = runtimeMapViewport(client);
    const auto liveHash = live.world.deterministicSimulationStateHash();
    int cases = 0, iconComparisons = 0, cursorComparisons = 0, saves = 0, clocks = 0;
    const std::pair<const char*, const char*> factions[] = {
        {"Americans", "GAPOWR"}, {"Russians", "NAPOWR"}, {"YuriCountry", "YAPOWR"}};
    for (int hz : {15, 30, 60}) for (const auto& [owner, type] : factions) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false);
        s.config.fixedTickHz = hz; s.config.gameSpeed = 6; s.config.computerAiEnabled = false;
        s.config.multiplayerActive = false; s.config.gameMode = "Battle";
        s.campaignMode = false; s.visionEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.economy.owner = owner; s.economy.credits = 10000;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear(); s.terrainInfoByCell.clear(); s.terrainByCell.clear();
        s.mapFacts = {}; s.mapFactsLoaded = false; s.behaviors.clear(); s.repairingStructureTicks.clear();
        for (int y=45;y<=85;++y) for (int x=45;x<=85;++x) {
            TerrainCell c; c.x=x; c.y=y; c.cellIndex512=runtimeCellKey(x,y); c.tileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        auto* building = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, type, {60,60});
        if (!building) return fail("stock building missing");
        const int buildingId = building->instanceId;
        building->owner = owner; building->mission = "Guard";
        building->buildAnimTicksRemaining = building->buildAnimTicksTotal = 0;
        building->health /= 2;
        int engineerTargetId=buildingId;
        if (std::string(type)=="YAPOWR") {
            // A reactor offers absorption before engineer repair. Use a Yuri
            // barracks for the ordinary engineer relationship in this case.
            auto* target=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"YABRCK",{64,56});
            if(!target)return fail("engineer target fixture");
            engineerTargetId=target->instanceId;target->owner=owner;target->health/=2;
            target->buildAnimTicksRemaining=target->buildAnimTicksTotal=0;
        }
        const char* engineerType=std::string(owner)=="Americans"?"ENGINEER":std::string(owner)=="Russians"?"SENGINEER":"YENGINEER";
        auto* engineer = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, engineerType, {55,60});
        if (!engineer) return fail("engineer fixture");
        const int engineerId = engineer->instanceId; engineer->owner = owner;
        auto* ifv = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "FV", {55,65});
        if (!ifv) return fail("IFV fixture");
        const int ifvId = ifv->instanceId; ifv->owner = owner;
        ifv->activeGunnerMode = 1; ifv->activeGunnerWeaponIndex = 1; ifv->activeGunnerTurretIndex = 1;
        auto* tank = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "MTNK", {60,65});
        if (!tank) return fail("tank fixture");
        const int tankId = tank->instanceId; tank->owner = owner; tank->health /= 2;
        s.camera = {}; centerRuntimeCameraOnCell(s, client, {60,60});
        struct Surface {
            RuntimeWindowState& state; HDC screen = GetDC(nullptr);
            ~Surface() { releaseRuntimeBackBuffer(state.backBuffer); if (screen) ReleaseDC(nullptr, screen); }
        } surface{s};
        if (!surface.screen || !ensureRuntimeBackBuffer(s.backBuffer, surface.screen, width, height))
            return fail("comparison surface");
        const auto clear = [&]() {
            GdiFlush(); std::fill_n(static_cast<std::uint32_t*>(s.backBuffer.pixels), width*height, background);
        };
        const auto pixels = [&]() {
            GdiFlush(); const auto* data = static_cast<std::uint32_t*>(s.backBuffer.pixels);
            std::vector<std::uint32_t> result(data, data+width*height);
            for (auto& pixel : result) pixel &= 0x00ffffffU;
            return result;
        };
        const auto blank = std::vector<std::uint32_t>(width*height, background);
        const auto projection = [&]() {
            invalidateRuntimeObjectProjectionCache(s);
            return runtimeProjectedObjectsForViewport(s, viewport);
        };
        const auto hitPoint = [&](int id) -> std::optional<POINT> {
            const auto all = projection();
            for (const auto& p : all) if (p.object && p.object->instanceId==id) {
                for (int y=std::max<LONG>(viewport.top,p.bounds.top);y<std::min(viewport.bottom,p.bounds.bottom);y+=2)
                    for (int x=std::max<LONG>(viewport.left,p.bounds.left);x<std::min(viewport.right,p.bounds.right);x+=2) {
                        const auto* hit=runtimeCursorObjectAtPoint(s,viewport,{x,y});
                        if (hit && hit->instanceId==id) return POINT{x,y};
                    }
            }
            return std::nullopt;
        };
        const auto buildingPoint=hitPoint(buildingId), tankPoint=hitPoint(tankId), engineerPoint=hitPoint(engineerTargetId);
        if (!buildingPoint || !tankPoint || !engineerPoint) return fail("actual target hit testing");
        std::vector<RuntimePaletteColor> palette; std::string source;
        const auto cacheMix=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/"ra2"/"cache.mix");
        const auto mouseMix=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/"ra2"/"conquer.mix");
        if (!loadRuntimePaletteFromMixArchive(cacheMix,s.renderCache,"mousepal",palette,source,error)) return fail(error);
        const auto reference = [&](const std::string& mix,const std::string& name,int frame,POINT anchor,const RECT& clip) {
            std::vector<std::uint32_t> result=blank; RuntimeShpFrameImage image; std::string path;
            if (!loadRuntimeShpFrameFromMixArchive(mix,s.renderCache,name,frame,image,path,error)) return std::vector<std::uint32_t>{};
            const int left=anchor.x-image.globalWidth/2+image.offsetX;
            const int top=anchor.y-image.globalHeight/2+image.offsetY;
            for (int y=0;y<image.height;++y) for (int x=0;x<image.width;++x) {
                const int dx=left+x,dy=top+y; const auto index=image.pixels[y*image.width+x];
                if (!index || dx<clip.left || dx>=clip.right || dy<clip.top || dy>=clip.bottom) continue;
                const auto c=palette[index]; result[dy*width+dx]=(c.r<<16)|(c.g<<8)|c.b;
            }
            return result;
        };
        // Frame boundaries are sampled against the independently decoded retail
        // timing table, including the wrap, all seven speed settings and rates.
        constexpr int periods[]={4,4,5,7,9,14,28};
        for (int speed=0;speed<=6;++speed) {
            s.config.gameSpeed=speed;
            for (int frame=0;frame<periods[speed]*2;++frame) {
                s.tick=frame*hz/15;
                const int expected=(frame%periods[speed])*6/(periods[speed]-1);
                if (runtimeBuildingRepairIconFrame(s)!=expected) return fail("retail icon clock");
                ++clocks;
            }
        }
        s.config.gameSpeed=6; s.tick=0;
        s.selectedObjectIds.clear(); s.activeSidebarTool=RuntimeSidebarTool::Repair;
        s.mouseClientPointValid=true; s.mouseClientPoint=*buildingPoint;
        if (!dispatchRuntimeStructureToolFromUi(s,RuntimeSidebarTool::Repair,buildingId)) return fail("repair UI start");
        std::set<std::vector<std::uint32_t>> uniqueIcons;
        for (int n=0;n<7;++n) {
            const int nativeFrame=(n*27+5)/6;
            s.tick=nativeFrame*hz/15;
            const auto all=projection(); const auto beforeHash=s.world.deterministicSimulationStateHash();
            const auto timers=s.repairingStructureTicks; const int credits=s.economy.credits;
            clear(); if(drawRuntimeRepairOverlays(s.backBuffer.memoryDc,viewport,s,all)!=1) return fail("native wrench did not draw");
            const auto actual=pixels(); const POINT anchor=runtimeBuildingRepairIconAnchor(s,*findObjectById(s,buildingId),viewport);
            if(actual!=reference(cacheMix,"wrench",n,anchor,viewport) || actual==blank) return fail("native wrench pixels/frame/palette "+std::to_string(n));
            uniqueIcons.insert(actual); ++iconComparisons;
            // Repeated repaint while paused is inert and does not consume money.
            clear(); drawRuntimeRepairOverlays(s.backBuffer.memoryDc,viewport,s,all);
            if(pixels()!=actual || beforeHash!=s.world.deterministicSimulationStateHash()
                || timers!=s.repairingStructureTicks || credits!=s.economy.credits) return fail("paused rendering changed simulation");
            auto altered=all;
            for(auto& p:altered) {p.bounds.left-=61;p.bounds.top-=49;p.bounds.right+=27;p.bounds.bottom+=19;}
            clear();drawRuntimeRepairOverlays(s.backBuffer.memoryDc,viewport,s,altered);
            if(pixels()!=actual)return fail("idle animation bounds moved the wrench");
        }
        // Native frames 0/3 and 4/6 repeat during the rocking motion.
        if(uniqueIcons.size()<2)return fail("wrench is static");
        for(int context=0;context<3;++context) for(int n=0;n<20;++n) {
            s.tick=n*4*hz/15; s.activeSidebarTool=context==0?RuntimeSidebarTool::Repair:RuntimeSidebarTool::None;
            s.selectedObjectIds=context==0?std::vector<int>{}:std::vector<int>{context==1?engineerId:ifvId};
            s.mouseClientPoint=context==2?*tankPoint:context==1?*engineerPoint:*buildingPoint;
            const auto cursor=runtimeMouseCursorFrameForPoint(s,client,s.mouseClientPoint);
            const int frame=(context==0?170:150)+n;
            if(!cursor || cursor->frameIndex!=frame || cursor->offsetX!=27 || cursor->offsetY!=21)
                return fail("actual cursor mapping context="+std::to_string(context)+" frame="+std::to_string(n));
            clear();drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc,client,s);
            if(pixels()!=reference(mouseMix,"mouse",frame,s.mouseClientPoint,client))return fail("native cursor pixels");
            ++cursorComparisons;
        }
        s.selectedObjectIds.clear();s.activeSidebarTool=RuntimeSidebarTool::Repair;
        const POINT empty{viewport.left+5,viewport.top+5};
        const auto blocked=runtimeMouseCursorFrameForPoint(s,client,empty);
        if(!blocked || blocked->frameIndex!=190)return fail("blocked repair cursor");
        s.rightDragActive=true;
        if(runtimeMouseCursorFrameForPoint(s,client,*buildingPoint)->frameIndex!=385)return fail("repair panning cursor");
        s.rightDragActive=false;s.mouseClientPointValid=false;
        if(runtimeMouseCursorFrameForPoint(s,client,*buildingPoint))return fail("mouse outside capture");
        // Test crop at all four map edges on both the CPU and GDI sprite paths.
        s.tick=0;const auto edgeObjects=projection();const auto originalCamera=s.camera;
        const auto originalAnchor=runtimeBuildingRepairIconAnchor(s,*findObjectById(s,buildingId),viewport);
        const POINT edges[]={{viewport.left,originalAnchor.y},{viewport.right-1,originalAnchor.y},
            {originalAnchor.x,viewport.top},{originalAnchor.x,viewport.bottom-1}};
        for(const auto& edge:edges) {
            s.camera=originalCamera;s.camera.worldOffsetX+=originalAnchor.x-edge.x;s.camera.worldOffsetY+=originalAnchor.y-edge.y;
            // Keep the visible host list while placing its icon at the exact
            // edge; the camera controller would otherwise clamp this fixture.
            const auto& all=edgeObjects;clear();drawRuntimeRepairOverlays(s.backBuffer.memoryDc,viewport,s,all);
            const auto expected=reference(cacheMix,"wrench",0,edge,viewport);
            if(pixels()!=expected || expected==blank)return fail("viewport clipping");
            const auto dc=s.backBuffer.memoryDc;clear();s.backBuffer.memoryDc=nullptr;
            drawRuntimeRepairOverlays(dc,viewport,s,all);s.backBuffer.memoryDc=dc;
            if(pixels()!=expected)return fail("GDI/CPU clipping disagree");
        }
        s.camera=originalCamera;s.tick=0;invalidateRuntimeObjectProjectionCache(s);
        const auto all=projection();s.economy.credits=0;
        for(int n=0;n<hz*2;++n)advanceRuntimeStructureRepairs(s);
        if(!s.repairingStructureTicks.contains(buildingId))return fail("cash shortage discarded repair");
        clear();if(drawRuntimeRepairOverlays(s.backBuffer.memoryDc,viewport,s,all)!=1)return fail("cash shortage hid active wrench");
        s.economy.credits=10000;
        std::ostringstream save;
        if(!writeRuntimeStateToStream(s,save,error))return fail("save "+error);
        IniFile saved;if(!saved.loadFromString(save.str()))return fail("save parse");
        auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
        if(!materializeRuntimeLoadStateFromIni(*loaded,saved,error)
            ||loaded->repairingStructureTicks!=s.repairingStructureTicks
            ||runtimeBuildingRepairIconFrame(*loaded)!=runtimeBuildingRepairIconFrame(s))return fail("restored repair "+error);
        ++saves;
        if(!dispatchRuntimeStructureToolFromUi(s,RuntimeSidebarTool::Repair,buildingId))return fail("cancel UI");
        clear();if(drawRuntimeRepairOverlays(s.backBuffer.memoryDc,viewport,s,all)!=0 || pixels()!=blank)return fail("cancel left wrench");
        if(!dispatchRuntimeStructureToolFromUi(s,RuntimeSidebarTool::Repair,buildingId))return fail("restart UI");
        for(int n=0;n<hz*120 && s.repairingStructureTicks.contains(buildingId);++n)advanceRuntimeStructureRepairs(s);
        if(s.repairingStructureTicks.contains(buildingId) || findObjectById(s,buildingId)->health!=runtimeMaxHealthForObject(s,*findObjectById(s,buildingId)))return fail("repair did not complete");
        clear();drawRuntimeRepairOverlays(s.backBuffer.memoryDc,viewport,s,all);
        if(pixels()!=blank || runtimeMouseCursorFrameForPoint(s,client,*buildingPoint))return fail("finished wrench/cursor state");
        s.mouseClientPointValid=true;
        if(runtimeMouseCursorFrameForPoint(s,client,*buildingPoint)->frameIndex!=190)return fail("full building still repairable");
        // Stale projection cannot draw an icon for a destroyed building.
        auto* deathProbe=findMutableObjectById(s,buildingId);
        if(!deathProbe)return fail("death fixture disappeared");
        s.repairingStructureTicks[buildingId]=0;deathProbe->alive=false;
        clear();if(drawRuntimeRepairOverlays(s.backBuffer.memoryDc,viewport,s,all)!=0 || pixels()!=blank)return fail("dead building icon");
        deathProbe->alive=true;deathProbe->health/=2;
        s.mouseClientPointValid=false;s.activeSidebarTool=RuntimeSidebarTool::None;
        if(hz==30) for(int frame:{0,2,4,6}) {
            s.tick=((frame*27+5)/6)*hz/15;
            const std::string file=root+"/"+type+"-wrench-"+std::to_string(frame)+".bmp";
            if(!drawRuntimeStateOffscreenAtCell(s,{60,60},width,height,error,file,false,true,-1,nullptr,true))return fail("scene "+error);
        }
        ++cases;std::cout<<"Repair "<<type<<" hz="<<hz<<" PASS\n";
    }
    if(live.world.deterministicSimulationStateHash()!=liveHash)return fail("changed live world");
    std::cout<<"Building repair passed: cases="<<cases<<" iconPixels="<<iconComparisons
        <<" cursorPixels="<<cursorComparisons<<" clocks="<<clocks<<" saves="<<saves
        <<" pause/anchor/edges/cash/cancel/completion/death passed\n";
    return true;
}
