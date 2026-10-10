bool runRuntimeSidebarCancelSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail = [&](const std::string& why) { error = "sidebar cancel " + scene + ": " + why; return false; };
    IniFile native; RulesDatabase rules;
    if (!native.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(native)) return fail("native rules");
    const char* output = std::getenv("CONCORD_SIDEBAR_CANCEL_OUTPUT");
    const std::string root = output ? output : "build/smoke/feature-set-14/38-sidebar-cancel/checks";
    std::filesystem::create_directories(root);
    const std::array<std::array<const char*,4>,3> types{{
        {{"GAWEAP","MTNK","CMIN","Americans"}},
        {{"NAWEAP","HTNK","HARV","Russians"}},
        {{"YAWEAP","LTNK","SMIN","YuriCountry"}}
    }};
    int scenes=0, clicks=0, canceled=0, produced=0, saves=0, renders=0, routedCommands=0;
    for (const auto& typesForSide : types) for (int hz : {15,30,60}) for (bool routed : {false,true}) {
        scene=std::string(typesForSide[0])+"-"+std::to_string(hz)+(routed?"-routed":"-direct");
        auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
        s.config=live.config; s.report=live.report; s.world=live.world; s.art=live.art; s.theater=live.theater;
        s.rules=rules; s.rulesLoaded=s.artLoaded=s.theaterLoaded=true; s.visionEnabled=false;
        s.config.fixedTickHz=hz; s.config.noCooldown=false; s.config.computerAiEnabled=false;
        s.config.multiplayerActive=false; s.config.playerSlots.clear(); s.config.playerCountry=typesForSide[3];
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true; s.runtimePreferencePersistenceEnabled=false;
        s.economy.owner=typesForSide[3]; s.economy.credits=100000; s.economy.techLevel=10;
        s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        s.cameoPalette=live.cameoPalette;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.terrainInfoByCell.clear();
        for (int y=45;y<=85;++y) for (int x=45;x<=85;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y); cell.tileIndex=cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance factory; factory.instanceId=1; factory.kind=WorldObjectKind::Structure;
        factory.type=typesForSide[0]; factory.owner=s.economy.owner; factory.x=factory.y=64;
        factory.cell=runtimeCellKey(64,64); factory.alive=true; factory.health=1000; factory.poweredOperational=true; factory.mission="Guard";
        auto power=factory; power.instanceId=2; power.type="NANRCT"; power.x=54; power.cell=runtimeCellKey(54,64);
        auto blocker=factory; blocker.instanceId=10; blocker.kind=WorldObjectKind::Unit; blocker.type=typesForSide[1];
        blocker.x=69; blocker.y=65; blocker.cell=runtimeCellKey(69,65);
        s.world.accessObjects()={factory,power,blocker}; initializeRuntimeNavigation(s);
        s.structureFootprints=buildRuntimeStructureFootprints(s); rebuildRuntimeEconomyPower(s);
        for (const char* id : {typesForSide[1],typesForSide[2]}) {
            addRuntimeBuildCatalogItem(s,RuntimeBuildKind::Unit,id);
            // Isolate queue UI from technology unlocks; keep native type, price, factory and cameo.
            s.economy.catalog.back().prerequisite.clear();
        }
        if(s.economy.catalog.size()!=2) return fail("native catalog");
        const auto first=s.economy.catalog[0], second=s.economy.catalog[1];
        s.activeBuildTab=RuntimeBuildKind::Unit;
        const RECT client{0,0,1024,768}, sidebar=runtimeSidebarRect(client);
        centerRuntimeCameraOnCell(s,client,{66,65}); ensureRuntimeCameraForViewport(s,runtimeMapViewport(client));
        RuntimeSnapshotActiveStateScope activeScope(s);
        HWND window=CreateWindowExW(0,L"STATIC",L"Sidebar cancellation check",WS_POPUP,0,0,1024,768,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window) return fail("input window");
        struct Cleanup { HWND h; ~Cleanup(){SetWindowLongPtrW(h,GWLP_USERDATA,0);DestroyWindow(h);} } cleanup{window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        if(routed) {
            initializeRuntimeOfflineGameplayRouting(s);
            s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command,std::string&) {
                ++routedCommands; return applyRuntimeMultiplayerGameplayCommand(s,s.economy.owner,command);
            });
        }
        const auto click=[&](int index,bool right) {
            const auto indices=runtimeVisibleBuildCatalogIndices(s,sidebar);
            const auto it=std::find(indices.begin(),indices.end(),index);
            if(it==indices.end()) return false;
            const auto rect=runtimeBuildButtonRect(sidebar,static_cast<int>(it-indices.begin()));
            const POINT p{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};
            runtimeWindowProc(window,right?WM_RBUTTONDOWN:WM_LBUTTONDOWN,right?MK_RBUTTON:MK_LBUTTON,MAKELPARAM(p.x,p.y));
            runtimeWindowProc(window,right?WM_RBUTTONUP:WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y)); ++clicks; return true;
        };
        const auto badge=[&](const std::string& filename)->std::vector<std::uint32_t> {
            HDC dc=CreateCompatibleDC(nullptr); BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=1024; info.bmiHeader.biHeight=-768; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
            void* bits=nullptr; HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
            auto old=SelectObject(dc,bitmap); std::memset(bits,0,1024*768*4); drawRuntimeSidebar(dc,client,s,1000); GdiFlush();
            std::vector<std::uint32_t> result; const auto rect=runtimeBuildButtonRect(sidebar,0);
            const auto* pixels=static_cast<const std::uint32_t*>(bits);
            for(int y=rect.top+3;y<rect.top+19;++y) for(int x=rect.right-25;x<rect.right-3;++x) result.push_back(pixels[y*1024+x]&0xffffffU);
            if(!filename.empty()&&!writeRuntimeSmokeBmp(root+"/"+filename,bits,1024,768,error)) result.clear();
            SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ++renders; return result;
        };
        const auto count=[&] { return runtimeQueuedProductionCountForItem(s,first)+(runtimeActiveProductionForItem(s,first)?1:0); };
        const auto step=[&] { ++s.tick; advanceRuntimeProductionEconomy(s); };
        initializeRuntimeLocalProductionLanes(s.economy);
        if(!runtimeBuildAvailability(s,first).canQueue||!runtimeBuildAvailability(s,second).canQueue) return fail("unlocked native pair");
        // A queued duplicate is removed first; a partially paid active order still holds, then cancels.
        click(0,false); step(); click(0,false);
        auto& lane=s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Vehicles)];
        if(count()!=2||!lane.active) return fail("two-order setup");
        const auto partialSerial=lane.active->serial; click(0,true);
        if(count()!=1||!lane.active||lane.active->serial!=partialSerial||lane.onHold) return fail("queued tail cancellation");
        click(0,true); if(!lane.onHold||count()!=1) return fail("partial order hold");
        const int partialRefund=lane.active->creditsSpent, partialCredits=s.economy.credits;
        click(0,true);
        if(count()!=0||lane.onHold||s.economy.credits!=partialCredits+partialRefund) return fail("partial order cancellation");
        canceled+=2;
        for(bool freeOrder:{false,true}) {
            s.economy.catalog[0].cost=freeOrder?0:first.cost;
            initializeRuntimeLocalProductionLanes(s.economy); s.factoryDeliveries.reset(); s.behaviors.clear();
            s.world.accessObjects()={factory,power,blocker}; initializeRuntimeNavigation(s);
            s.structureFootprints=buildRuntimeStructureFootprints(s); rebuildRuntimeEconomyPower(s);
            const auto rect=runtimeBuildButtonRect(sidebar,0);
            s.mouseClientPointValid=true; s.mouseClientPoint={(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};
            const auto empty=badge(""); if(empty.empty()) return fail("empty badge render");
            const int initialCredits=s.economy.credits;
            s.config.noCooldown=true; click(0,false); step(); s.config.noCooldown=false;
            if(!lane.active||!runtimePeekCompletedProduction(s.economy.localProductionLanes,RuntimeProductionLane::Vehicles)
                ||count()!=1||s.world.getObjects().size()!=3) return fail("fully paid order waiting for factory exit");
            const std::string phase=scene+(freeOrder?"-free":"-paid");
            const bool picture=hz==30&&!routed&&!freeOrder;
            const auto waiting=badge(picture?phase+"-before.bmp":"");
            if(waiting==empty) return fail("waiting order badge absent");
            click(0,true);
            if(count()!=0||lane.active||lane.onHold||s.economy.credits!=initialCredits) return fail("completed last order was held instead of canceled");
            const auto cleared=badge(picture?phase+"-after.bmp":"");
            if(cleared!=empty) return fail("canceled last order left badge pixels");
            ++canceled;
            if(hz==30&&!freeOrder) {
                std::ostringstream saved; IniFile ini; auto restored=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored,s,false);
                if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())||!materializeRuntimeLoadStateFromIni(*restored,ini,error)) return fail("cancel reload: "+error);
                if(runtimeProductionLaneOccupancy(restored->economy.localProductionLanes,RuntimeProductionLane::Vehicles)!=0
                    ||restored->economy.credits!=s.economy.credits) return fail("cancel resurrected after reload");
                ++saves;
            }
            click(1,false); for(int t=0;t<hz*2;++t) step();
            if(!lane.active||lane.active->itemKey!=second.typeId||lane.onHold||lane.active->creditsSpent<=0) return fail("another native unit did not start spending");
            // Remove the fixture's obstruction, then finish the replacement through actual production.
            auto* obstacle=findMutableObjectById(s,10); obstacle->x=obstacle->y=50; obstacle->cell=runtimeCellKey(50,50);
            eraseRuntimeBehavior(s,10); initializeRuntimeNavigation(s); s.config.noCooldown=true; step(); s.config.noCooldown=false;
            if(lane.active||!lane.queued.empty()||s.world.getObjects().size()!=4||s.world.getObjects().back().type!=second.typeId) return fail("replacement failed to leave production lane");
            ++produced;
        }
        ++scenes;
    }
    std::cout<<"Sidebar cancellation passed: "<<scenes<<" native/direct/routed scenes, "<<clicks<<" Win32 clicks, "<<canceled
        <<" cancellations, "<<produced<<" replacement units, "<<saves<<" full reloads, "<<renders<<" actual sidebar renders, "<<routedCommands<<" captured commands; paid/free exit waits and partial hold controls\n";
    return true;
}
