bool runRuntimePrimaryFactoryClickSmoke(RuntimeWindowState& live, std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why){error="primary factory click: "+scene+": "+why;return false;};
    const std::array<std::array<const char*,3>,6> types{{
        {{"GAPILE","E1","Americans"}},{{"NAHAND","E2","Russians"}},{{"YABRCK","INIT","YuriCountry"}},
        {{"GAWEAP","MTNK","Americans"}},{{"NAWEAP","HTNK","Russians"}},{{"YAWEAP","LTNK","YuriCountry"}}
    }};
    int cases=0,saves=0,produced=0;
    for(const auto& type:types) for(bool routed:{false,true}) {
        scene=std::string(type[0])+(routed?" routed":" direct");
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=live.config;s.config.computerAiEnabled=false;s.config.multiplayerActive=false;s.config.playerSlots.clear();
        s.report=live.report;s.art=live.art;s.theater=live.theater;
        IniFile stock;if(!stock.loadFromFile(live.report.rulesPath)||!s.rules.loadFromIni(stock))return fail("native rules");
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=false;s.runtimePreferencePersistenceEnabled=false;
        s.economy.owner=type[2];s.economy.credits=100000;s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        initializeRuntimeLocalProductionLanes(s.economy);s.economy.localProductionLanes.nextSerial=2;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        for(int y=40;y<=88;++y)for(int x=40;x<=88;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);s.terrainInfoByCell.emplace(cell.cellIndex512,RuntimeTerrainInfo{});
        }
        for(int id=1;id<=2;++id){
            WorldObjectInstance factory;factory.instanceId=id;factory.kind=WorldObjectKind::Structure;factory.type=type[0];
            factory.owner=type[2];factory.alive=true;factory.mission="Guard";factory.poweredOperational=true;
            factory.x=54+id*10;factory.y=64;factory.cell=runtimeCellKey(factory.x,factory.y);
            factory.health=runtimeMaxHealthForObject(s,factory);s.world.accessObjects().push_back(factory);
        }
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);
        s.structureFootprints=buildRuntimeStructureFootprints(s);
        RuntimeSnapshotActiveStateScope active(s);
        if(!loadRuntimeSoundDatabase(s))return fail("native audio");
        const RECT client{0,0,1280,720};const RECT viewport=runtimeMapViewport(client);
        centerRuntimeCameraOnCell(s,client,{69,65});ensureRuntimeCameraForViewport(s,viewport);
        HWND window=CreateWindowExW(0,L"STATIC",L"Primary factory click check",WS_POPUP,0,0,1280,720,
            nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)return fail("input window");
        struct Cleanup{HWND h;~Cleanup(){SetWindowLongPtrW(h,GWLP_USERDATA,0);DestroyWindow(h);}}cleanup{window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        const auto pointFor=[&](int id)->std::optional<POINT>{
            for(const auto& object:runtimeProjectedObjectsForViewport(s,viewport))if(object.object&&object.object->instanceId==id){
                for(LONG y=std::max(viewport.top,object.bounds.top);y<std::min(viewport.bottom,object.bounds.bottom);y+=3)
                    for(LONG x=std::max(viewport.left,object.bounds.left);x<std::min(viewport.right,object.bounds.right);x+=3){
                        const POINT point{x,y};const auto* hit=runtimeCursorObjectAtPoint(s,viewport,point);
                        if(hit&&hit->instanceId==id)return point;
                    }
            }return std::nullopt;
        };
        const auto first=pointFor(1),second=pointFor(2);if(!first||!second)return fail("factory hit points");
        const auto click=[&](POINT point){
            runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
            runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        };
        const auto primary=[&](const RuntimeWindowState& state){
            const auto key=std::pair{runtimeCanonicalHumanHouseOwner(type[2]),runtimePlayerFactoryCategory(state,*findObjectById(state,2))};
            const auto it=state.playerCommands.primaryFactories.find(key);return it==state.playerCommands.primaryFactories.end()?-1:it->second;
        };
        int captures=0;
        if(routed){
            initializeRuntimeOfflineGameplayRouting(s);
            s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command,std::string&){
                const auto* body=std::get_if<RuntimeMultiplayerGameplaySetPrimaryFactoryCommand>(&command.body);
                if(!body)return false;++captures;
                return applyRuntimePlayerPrimaryFactory(s,s.economy.owner,body->objectId);
            });
        }
        click(*second);
        if(s.selectedObjectIds!=std::vector<int>{2}||primary(s)!=-1)return fail("first click must only select");
        auto cursor=runtimeMouseCursorFrameForPoint(s,client,*second);
        if(!cursor||cursor->frameIndex<110||cursor->frameIndex>118)return fail("selected factory lacks native deploy cursor");
        s.forceAttackHeld=true;cursor=runtimeMouseCursorFrameForPoint(s,client,*second);s.forceAttackHeld=false;
        if(!cursor||cursor->frameIndex<53||cursor->frameIndex>57)return fail("primary cursor overrode force attack");
        click(*second);if(primary(s)!=2||s.selectedObjectIds!=std::vector<int>{2})return fail("second click did not choose primary");
        click(*first);if(primary(s)!=2)return fail("selection alone changed primary");
        click(*first);if(primary(s)!=1)return fail("primary could not switch");
        click(*second);click(*second);if(primary(s)!=2||(routed&&captures!=3))return fail("last selection/routing");
        if(std::count(s.runtimeSoundIntentHistory.begin(),s.runtimeSoundIntentHistory.end(),"EVA_PrimaryBuildingSelected")!=3)
            return fail("one confirmation per primary command");
        s.offlineGameplayIntentRouting=false;
        auto* chosen=findMutableObjectById(s,2);
        chosen->owner="Neutral";
        if(runtimeSelectedFactoryCanBePrimary(s,*chosen))return fail("enemy/neutral primary allowed");chosen->owner=type[2];
        chosen->buildAnimTicksRemaining=chosen->buildAnimTicksTotal=10;
        if(runtimeSelectedFactoryCanBePrimary(s,*chosen))return fail("unfinished primary allowed");chosen->buildAnimTicksRemaining=chosen->buildAnimTicksTotal=0;
        s.selectedObjectIds={1,2};if(runtimeSelectedFactoryCanBePrimary(s,*chosen))return fail("ambiguous group primary");s.selectedObjectIds={2};
        RuntimeBuildCatalogItem item;item.typeId=type[1];
        const auto* building=s.rules.findBuildingType(type[0]);item.requiredFactory=building->factory;
        item.kind=toUpperAsciiLocal(building->factory)=="INFANTRYTYPE"?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit;
        const auto spawn=findRuntimeProductionSpawnForOwner(s,item,type[2],false);
        if(!spawn||!spawn->anchor||spawn->anchor->instanceId!=2)return fail("chosen factory did not win production");
        std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
        if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())||!materializeRuntimeLoadStateFromIni(*restored,ini,error))
            return fail("full reload: "+error);
        if(primary(*restored)!=2)return fail("reload lost chosen factory");++saves;
        for(auto* state:{&s,restored.get()}){
            RuntimeSnapshotActiveStateScope producing(*state);
            const auto location=findRuntimeProductionSpawnForOwner(*state,item,type[2],false);
            if(!location||!location->anchor||location->anchor->instanceId!=2)return fail("reload chose another factory");
            const auto expected=location->cell;
            if(!completeRuntimeProductionOrder(*state,item,false,
                    item.kind==RuntimeBuildKind::Infantry?RuntimeProductionLane::Infantry:RuntimeProductionLane::Vehicles,1)
                ||state->world.getObjects().size()!=3)return fail("real unit production");
            const auto& unit=state->world.getObjects().back();
            if(runtimeCellPointForObject(unit)!=expected)return fail("unit appeared at another factory");++produced;
        }
        ++cases;
    }
    std::cout<<"Primary factory click passed: "<<cases<<" native/direct/routed UI scenes, "<<saves
        <<" full reloads, "<<produced<<" real unit deliveries, native deploy cursor and confirmation, selection-only/ownership/build guards\n";
    return true;
}
