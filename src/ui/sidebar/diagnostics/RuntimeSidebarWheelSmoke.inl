bool runRuntimeSidebarWheelSmoke(RuntimeWindowState& live,std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why){error="sidebar wheel: "+scene+": "+why;return false;};
    int cases=0,events=0;
    for(const auto size:{POINT{800,600},POINT{1280,720},POINT{1920,1080}}){
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=live.config;s.report=live.report;s.world=live.world;s.rules=live.rules;s.rulesLoaded=true;
        s.config.computerAiEnabled=false;s.config.multiplayerActive=false;s.config.playerSlots.clear();
        s.economy.owner="Russians";s.economy.techLevel=10;s.economy.credits=100000;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.visionEnabled=false;
        const RECT client{0,0,size.x,size.y};const auto sidebar=runtimeSidebarRect(client);
        const int perPage=runtimeBuildButtonsPerPage(sidebar);
        for(int tab=0;tab<4;++tab)for(int i=0;i<perPage*3+1;++i){
            RuntimeBuildCatalogItem item;item.kind=runtimeBuildKindForTabIndex(tab);
            item.typeId="WHEEL_"+std::to_string(tab)+"_"+std::to_string(i);item.label=item.typeId;
            s.economy.catalog.push_back(item);
        }
        centerRuntimeCameraOnCell(s,client,{64,64});ensureRuntimeCameraForViewport(s,runtimeMapViewport(client));
        const auto camera=s.camera;const auto hash=s.world.deterministicSimulationStateHash();
        HWND window=CreateWindowExW(0,L"STATIC",L"Sidebar wheel check",WS_POPUP,0,0,size.x,size.y,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)return fail("input window");
        struct Cleanup{HWND h;~Cleanup(){SetWindowLongPtrW(h,GWLP_USERDATA,0);DestroyWindow(h);}}cleanup{window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        const auto wheel=[&](int delta,bool overSidebar){
            POINT p{overSidebar?sidebar.left+35:200,overSidebar?sidebar.top+runtimeSidebarListOffset+20:200};
            ClientToScreen(window,&p);
            runtimeWindowProc(window,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(delta)),MAKELPARAM(p.x,p.y));++events;
        };
        for(int tab=0;tab<4;++tab){
            scene=std::to_string(size.x)+"x"+std::to_string(size.y)+" tab="+std::to_string(tab);
            s.activeBuildTab=runtimeBuildKindForTabIndex(tab);
            if(runtimeBuildPageCount(s,sidebar)!=4)return fail("four-page unlocked catalog");
            const auto before=runtimeVisibleBuildCatalogIndices(s,sidebar);
            wheel(-WHEEL_DELTA,false);
            if(runtimeActiveBuildPage(s)!=1||runtimeVisibleBuildCatalogIndices(s,sidebar)==before)return fail("map-hover wheel did not scroll sidebar");
            wheel(-WHEEL_DELTA,true);if(runtimeActiveBuildPage(s)!=2)return fail("sidebar-hover wheel");
            wheel(WHEEL_DELTA,true);if(runtimeActiveBuildPage(s)!=1)return fail("wheel up");
            wheel(-WHEEL_DELTA*3,false);if(runtimeActiveBuildPage(s)!=3)return fail("multi-notch lower bound");
            const auto visible=runtimeVisibleBuildCatalogIndices(s,sidebar);
            if(visible.size()!=1)return fail("last partial page");
            const auto button=runtimeBuildButtonRect(sidebar,0);
            const auto hit=runtimeBuildButtonAtPoint(s,sidebar,{(button.left+button.right)/2,(button.top+button.bottom)/2});
            if(!hit||*hit!=visible.front())return fail("scrolled cameo click mapping");
            wheel(-WHEEL_DELTA,false);if(runtimeActiveBuildPage(s)!=3)return fail("lower bound moved");
            wheel(WHEEL_DELTA*6,false);if(runtimeActiveBuildPage(s)!=0)return fail("upper bound");
            for(int i=0;i<3;++i){wheel(-30,false);if(runtimeActiveBuildPage(s)!=0)return fail("high-resolution partial notch jumped");}
            wheel(-30,false);if(runtimeActiveBuildPage(s)!=1)return fail("high-resolution accumulated notch lost");
            wheel(60,false);if(runtimeActiveBuildPage(s)!=1)return fail("positive partial notch jumped");
            wheel(60,false);if(runtimeActiveBuildPage(s)!=0)return fail("positive accumulated notch lost");
            wheel(0,false);if(runtimeActiveBuildPage(s)!=0)return fail("zero delta changed page");
            s.runtimeMenuOpen=true;wheel(-WHEEL_DELTA,false);s.runtimeMenuOpen=false;
            if(runtimeActiveBuildPage(s)!=0)return fail("menu wheel reached sidebar");
            runtimeSetActiveBuildPage(s,2);++cases;
        }
        for(int tab=0;tab<4;++tab)if(s.buildPageByTab[tab]!=2)return fail("tabs shared page positions");
        if(s.camera.worldOffsetX!=camera.worldOffsetX||s.camera.worldOffsetY!=camera.worldOffsetY
            ||s.world.deterministicSimulationStateHash()!=hash)return fail("wheel moved camera or changed gameplay");
        s.economy.catalog.resize(1);s.activeBuildTab=RuntimeBuildKind::Building;
        wheel(-WHEEL_DELTA,false);wheel(WHEEL_DELTA,false);
        if(runtimeActiveBuildPage(s)!=0||s.camera.worldOffsetY!=camera.worldOffsetY)return fail("short catalog moved camera or retained invalid page");
        s.economy.catalog.clear();wheel(-WHEEL_DELTA,false);
        if(runtimeActiveBuildPage(s)!=0)return fail("empty catalog changed page");
    }
    std::cout<<"Sidebar wheel passed: "<<cases<<" size/category cases, "<<events
        <<" Win32 wheel events, map/sidebar hover, partial/multiple notches, bounds, short/empty catalogs, cameo hit mapping, camera and world unchanged\n";
    return true;
}
