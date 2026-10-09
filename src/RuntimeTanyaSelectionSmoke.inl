bool runRuntimeTanyaSelectionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& why){error="Tanya selection: "+why;return false;};
    auto s=std::make_unique<RuntimeWindowState>();
    s->config=config; s->config.fixedTickHz=30; s->config.computerAiEnabled=false;
    s->config.shortGame=false; s->runtimePreferencePersistenceEnabled=false;
    s->report=buildRuntimeStartupReport(config,&s->world,&s->theater,&s->art,&s->rules,&s->mapFacts);
    if(!s->report.success) return fail("original resources unavailable");
    s->theaterLoaded=s->artLoaded=s->rulesLoaded=s->mapFactsLoaded=true;
    s->economy.owner="Americans"; s->visionEnabled=false; s->runtimeSoundsEnabled=false;
    if(!loadRuntimeSoundDatabase(*s)) return fail("selection voice catalog");
    auto context=runtimeTerrainMetadataContext(*s);
    context.rules=&s->rules;context.art=&s->art;
    context.palettePath=runtimePalettePath(config,s->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,s->report.map);
    context.structureRootPath=runtimeStructureRootPath(config,s->report.map);
    if(!runRuntimeInfantrySwimRenderContractCheck(context,error))return false;
    TerrainCell water;bool found=false;
    for(const auto& set:s->theater.getTileSets()) {
        if(toLowerAsciiLocal(set.fileName+" "+set.setName).find("water")==std::string::npos)continue;
        for(int tile=set.startTileIndex;tile<set.startTileIndex+set.tilesInSet && !found;++tile)
            for(int sub=0;sub<16 && !found;++sub) {
                water.tileIndex=tile;water.subTileIndex=sub;
                found=resolveRuntimeTerrainSubTileLandType(water,context,s->renderCache)==RuntimeTerrainLandType::Water;
            }
        if(found)break;
    }
    if(!found)return fail("water TMP unavailable");
    s->world.accessObjects().clear();s->world.accessTerrainCells().clear();
    s->world.accessTerrainObjects().clear();s->world.accessSmudges().clear();
    s->mapFacts=MapFacts{};
    // The diagnostic terrain is a new rectangle. The original map's LocalSize
    // must not clamp the camera to an unrelated part of that rectangle.
    s->report.map.size.clear();s->report.map.localSize.clear();s->report.map.width=-1;
    s->radarProjectionCacheValid=false;
    for(int y=1;y<40;++y)for(int x=1;x<40;++x) {
        auto cell=water;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;
        s->world.accessTerrainCells().push_back(cell);
        MapCellFacts fact;fact.x=x;fact.y=y;fact.cellIndex=cell.cellIndex512;
        fact.hasTerrain=true;fact.isWater=true;
        fact.terrain.x=x;fact.terrain.y=y;fact.terrain.tileIndex=cell.tileIndex;
        fact.terrain.subTileIndex=cell.subTileIndex;
        s->mapFacts.cellIndexLookup.emplace(fact.cellIndex,s->mapFacts.cells.size());
        s->mapFacts.cells.push_back(fact);
    }
    WorldObjectInstance tanya;tanya.instanceId=1;tanya.kind=WorldObjectKind::Infantry;
    tanya.type="TANY";tanya.owner="Americans";tanya.health=200;tanya.mission="Guard";
    tanya.x=tanya.y=20;tanya.cell=runtimeCellKey(20,20);
    s->world.accessObjects()={tanya};initializeRuntimeNavigation(*s);
    if(!MapQuery(s->mapFacts).getCell(20,20))return fail("water cell lookup");
    constexpr int width=800,height=600;
    const RECT client{0,0,width,height};const RECT viewport=runtimeMapViewport(client);
    centerRuntimeCameraOnCell(*s,client,{20,20});
    HWND window=CreateWindowExW(0,L"STATIC",L"Tanya selection check",WS_POPUP,0,0,width,height,
        nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return fail("selection window");
    struct WindowCleanup{HWND h;~WindowCleanup(){DestroyWindow(h);}} windowCleanup{window};
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s.get()));
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* raw=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    if(!dc||!bitmap||!raw){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return fail("surface");}
    struct SurfaceCleanup{HDC dc;HBITMAP bitmap;HGDIOBJ old;
        ~SurfaceCleanup(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}}
        surfaceCleanup{dc,bitmap,SelectObject(dc,bitmap)};
    auto* pixels=static_cast<std::uint32_t*>(raw);
    context.surfacePixels=pixels;context.surfaceWidth=width;context.surfaceHeight=height;
    context.mapFacts=&s->mapFacts;context.selectedObjectIds=&s->selectedObjectIds;
    context.playerCheerUntilTick=&s->playerCommunication.cheerUntilTick;context.fixedTickHz=30;
    bool visible=true;
    const auto draw=[&](){
        std::fill(pixels,pixels+width*height,0x00242c30);
        const auto stats=drawRuntimeMap(dc,viewport,s->world,context,s->camera,s->renderCache);GdiFlush();
        visible=visible && stats.visibleObjectShpSprites==1;
        return std::vector<std::uint32_t>(pixels,pixels+width*height);
    };
    int cases=0,routedCommands=0;
    for(bool routed:{false,true}) {
    if(routed) {
        initializeRuntimeOfflineGameplayRouting(*s);
        s->multiplayerIntentRouter.setCaptureCallback([&](const auto&,std::string&){++routedCommands;return false;});
    }
    for(bool moving:{false,true})for(int facing=0;facing<8;++facing) {
        auto& object=s->world.accessObjects().front();object=tanya;object.facing=facing*32;
        if(moving){object.mission="Move";object.hasRenderPosition=object.renderMoveActive=true;
            object.renderX=20.25;object.renderY=20;object.movementAnimationDistance=0.35;}
        s->tick=17;context.animationTick=s->tick;s->selectedObjectIds.clear();
        s->playerCommands={};
        RuntimeMultiplayerGameplayTargetOrderCommand queued;
        queued.order=RuntimeMultiplayerGameplayTargetOrderKind::Move;queued.objectIds={1};
        queued.target=RuntimeMultiplayerGameplayCellTarget{{22,20}};
        if(!s->playerCommands.append("Americans",queued))return fail("queued movement fixture");
        const auto beforeOrders=s->playerCommands.stateHash();
        invalidateRuntimeObjectProjectionCache(*s);
        const auto beforeHash=s->world.deterministicSimulationStateHash();
        const auto before=draw();
        const auto projected=runtimeProjectedObjectsForViewport(*s,viewport);
        if(projected.size()!=1 || !projected.front().object)return fail("projection");
        const RECT bounds=projected.front().bounds;
        const RECT body=projected.front().spriteBounds;
        const POINT point{(body.left+body.right)/2,(body.top+body.bottom)/2};
        if(!findProjectedObjectAtPoint(projected,point)||!pointInRectInclusive(viewport,point)||!visible)
            return fail("visible pick: point="+std::to_string(point.x)+","+std::to_string(point.y));
        const RECT selection=projected.front().selectionBounds;
        const POINT corners[]={{selection.left+1,selection.top+1},
            {selection.right-1,selection.top+1},{selection.left+1,selection.bottom-1},
            {selection.right-1,selection.bottom-1}};
        for(int input=0;input<8;++input) {
            const bool box=input==1;
            const POINT clickPoint=input>=2&&input<=5?corners[input-2]:point;
            const POINT start=box?POINT{bounds.left-2,bounds.top-2}:clickPoint;
            const POINT end=box?POINT{bounds.right+2,bounds.bottom+2}:clickPoint;
            runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
            if(box)runtimeWindowProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
            if(draw()!=before)return fail("mouse down changed body");
            runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
            if(s->selectedObjectIds!=std::vector<int>{1})return fail("actual click/box selection");
            drawRuntimeInteractionOverlay(dc,client,*s,context);GdiFlush();
            if(input==0 && facing==2)if(const char* dir=std::getenv("CONCORD_TANYA_SELECTION_DIR");dir&&*dir)
                if(!writeRuntimeSmokeBmp(std::string(dir)+(moving?"/tanya-moving-selected.bmp":"/tanya-idle-selected.bmp"),pixels,width,height,error))return false;
            const auto after=draw();
            if(before!=after || beforeHash!=s->world.deterministicSimulationStateHash()
                || beforeOrders!=s->playerCommands.stateHash() || routedCommands!=0)
                return fail(std::string(moving?"Swim":"Tread")+" changed at the same tick after input="+std::to_string(input));
            ++cases;
        }
    }
    }
    s->offlineGameplayIntentRouting=false;s->multiplayerIntentRouter.setCaptureCallback({});s->playerCommands={};
    if(!visible)return fail("visible Tanya body missing");
    if(s->runtimeSoundPlaybackHistory.empty())return fail("selection voice route missing");
    // Reproduce the autonomous timer, independent of mouse clicks. Previously
    // it selected Idle1/2 every six seconds and the swimmer played wet stunts.
    int wetIdleTicks=0,landIdleChanges=0,oldWetIdleTicks=0;
    for(int hz:{15,30,60}) for(const char* type:{"TANY","GHOST"}) {
        s->config.fixedTickHz=hz; context.fixedTickHz=hz;
        auto& object=s->world.accessObjects().front();object=tanya;object.type=type;
        for(int tick=1;tick<=hz*24;++tick) {
            s->tick=tick;context.animationTick=tick;
            if(!runtimeIdleVariantMissionForObject(*s,object).empty())++oldWetIdleTicks;
            advanceRuntimeIdleVariants(*s);
            const auto mission=toLowerAsciiLocal(object.mission);
            if(mission=="idle1"||mission=="idle2") return fail("autonomous wet idle stunt at Hz="
                +std::to_string(hz)+" tick="+std::to_string(tick));
            ++wetIdleTicks;
        }
        // A saved old Idle1 pose settles back to tread on the next idle update.
        object.mission="Idle1";
        for(int tick=1;tick<=hz;++tick){s->tick=tick;advanceRuntimeIdleVariants(*s);}
        if(toLowerAsciiLocal(object.mission)=="idle1")return fail("old wet idle never settled");
        // On a bridge the same infantry still uses its authored land idles.
        object.onBridge=true;
        for(int tick=1;tick<=hz*12;++tick) {
            s->tick=tick;advanceRuntimeIdleVariants(*s);
            const auto mission=toLowerAsciiLocal(object.mission);
            if(mission=="idle1"||mission=="idle2")++landIdleChanges;
        }
    }
    if(landIdleChanges==0||oldWetIdleTicks==0)return fail("idle timer was not exercised");
    std::cout<<"Tanya autonomous water idle: ticks="<<wetIdleTicks
        <<" TANY/GHOST 15/30/60 Hz wet stunts=0 former wet idle ticks="<<oldWetIdleTicks
        <<" land idle ticks="<<landIdleChanges<<" ok\n";
    s->config.fixedTickHz=30;context.fixedTickHz=30;
    // Exercise the stationary pose over time and the sound path of a real game.
    s->world.accessObjects().front()=tanya;s->world.accessObjects().front().facing=64;
    s->selectedObjectIds={1};s->runtimeSoundVolume=1;
    if(std::getenv("CONCORD_TANYA_SELECTION_AUDIO")) {
    s->runtimeSoundsEnabled=true;
    for(int attempt=0;attempt<8;++attempt) {
        s->tick=20+attempt*7;context.animationTick=s->tick;
        const auto before=draw();
        const auto& projection=runtimeProjectedObjectsForViewport(*s,viewport);
        const RECT body=projection.front().spriteBounds;
        const POINT point{(body.left+body.right)/2,(body.top+body.bottom)/2};
        const auto started=std::chrono::steady_clock::now();
        runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        const double milliseconds=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-started).count();
        const auto& voice=s->runtimeSoundPlaybackHistory.back();
        std::cout<<"Tanya live selection: tick="<<s->tick<<" elapsedMs="<<milliseconds
            <<" audio="<<voice.status<<"\n";
        if(before!=draw())return fail("audible selection changed stationary pose");
    }
    stopRuntimeSfxPlayback(*s);s->runtimeSoundsEnabled=false;
    }
    for(int tick=1;tick<=360;++tick) {
        s->tick=tick;context.animationTick=tick;
        advanceRuntimeIdleVariants(*s);
        const auto before=draw();
        const auto& projection=runtimeProjectedObjectsForViewport(*s,viewport);
        const RECT body=projection.front().spriteBounds;
        const POINT point{(body.left+body.right)/2,(body.top+body.bottom)/2};
        if(tick%3==0) {
            runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
            runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
            if(before!=draw()||s->world.getObjects().front().hasRenderPosition)
                return fail("idle sequence changed on select tick="+std::to_string(tick));
        }
    }
    s->world.accessObjects().front()=tanya;s->selectedObjectIds={1};
    s->tick=400;context.animationTick=s->tick;invalidateRuntimeObjectProjectionCache(*s);
    const auto frameBefore=draw();
    const auto projection=runtimeProjectedObjectsForViewport(*s,viewport).front();
    const RECT selection=projection.selectionBounds;
    const POINT framePoint{selection.left+1,selection.bottom-1};
    std::cout<<"Tanya selection frame: hit="<<pointInRectInclusive(projection.bounds,framePoint)<<'\n';
    const auto frameHash=s->world.deterministicSimulationStateHash();
    runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(framePoint.x,framePoint.y));
    runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(framePoint.x,framePoint.y));
    if(s->lastCommand.acceptedCount>0) {
        ++s->tick;advanceRuntimeSimulation(*s);context.animationTick=s->tick;
    }
    const auto frameAfter=draw();
    if(const char* dir=std::getenv("CONCORD_TANYA_SELECTION_DIR");dir&&*dir) {
        if(!writeRuntimeSmokeBmp(std::string(dir)+"/tanya-frame-before.bmp",frameBefore.data(),width,height,error)
            ||!writeRuntimeSmokeBmp(std::string(dir)+"/tanya-frame-after.bmp",frameAfter.data(),width,height,error))return false;
    }
    std::cout<<"Tanya frame click: mission="<<s->world.getObjects().front().mission
        <<" accepted="<<s->lastCommand.acceptedCount<<" changedPixels="<<(frameBefore!=frameAfter)<<'\n';
    if(s->lastCommand.acceptedCount>0||frameBefore!=frameAfter||frameHash!=s->world.deterministicSimulationStateHash())
        return fail("click inside visible selection frame issued movement");
    std::cout << "Tanya selection: passed; actual click/box/repeat=" << cases
        << " same-tick map pixels=yes unchanged world/orders=yes selection voice=yes"
        << " routed ability commands="<<routedCommands<<" idle ticks=360 frame corners=ok\n";
    return true;
}
