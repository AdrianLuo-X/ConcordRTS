bool runRuntimeMirageDisguiseSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const bool diagnose=std::getenv("CONCORD_MIRAGE_DIAGNOSE")!=nullptr;
    const auto fail=[&](const std::string& why){error="Mirage disguise: "+why;return false;};
    auto s=std::make_unique<RuntimeWindowState>();
    s->config=config;s->config.fixedTickHz=30;s->config.computerAiEnabled=false;s->config.shortGame=false;
    s->report=buildRuntimeStartupReport(config,&s->world,&s->theater,&s->art,&s->rules,&s->mapFacts);
    if(!s->report.success)return fail("original resources unavailable");
    s->rulesLoaded=s->artLoaded=s->theaterLoaded=true;s->mapFactsLoaded=false;
    s->economy.owner="Americans";s->visionEnabled=false;s->runtimeSoundsEnabled=false;
    s->runtimeSuppressPlayerAudioFeedback=true;s->runtimePreferencePersistenceEnabled=false;
    s->world.accessObjects().clear();s->world.accessTerrainCells().clear();
    s->world.accessTerrainObjects().clear();s->world.accessSmudges().clear();s->mapFacts=MapFacts{};
    s->report.map.size.clear();s->report.map.localSize.clear();s->report.map.width=-1;
    for(int y=1;y<50;++y)for(int x=1;x<50;++x){
        TerrainCell cell;cell.x=x;cell.y=y;cell.tileIndex=0;cell.cellIndex512=y*512+x;
        s->world.accessTerrainCells().push_back(cell);
        s->terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});
    }
    WorldObjectInstance tank;tank.instanceId=1;tank.kind=WorldObjectKind::Unit;
    tank.type="MGTK";tank.owner="Americans";tank.health=200;tank.mission="Guard";
    tank.x=tank.y=25;tank.cell=runtimeCellKey(25,25);
    s->world.accessObjects()={tank};initializeRuntimeNavigation(*s);
    const auto step=[&](){++s->tick;advanceRuntimeSimulation(*s);};
    for(int tick=0;tick<40;++tick)step();
    if(!s->specialMechanisms.isCamouflaged(1))return fail("full idle simulation never disguises");
    constexpr int width=800,height=600;
    const RECT client{0,0,width,height};const RECT viewport=runtimeMapViewport(client);
    centerRuntimeCameraOnCell(*s,client,{25,25});
    RuntimeMapRenderContext context=runtimeTerrainMetadataContext(*s);
    context.rules=&s->rules;context.art=&s->art;context.fixedTickHz=30;
    context.palettePath=runtimePalettePath(config,s->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,s->report.map);
    context.structureRootPath=runtimeStructureRootPath(config,s->report.map);
    context.visionObserverOwner="Americans";
    context.specialMechanismSnapshots=&s->specialMechanismRenderSnapshots;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* raw=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    if(!dc||!bitmap||!raw){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return fail("surface");}
    struct SurfaceCleanup{HDC dc;HBITMAP bitmap;HGDIOBJ old;
        ~SurfaceCleanup(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}}
        cleanup{dc,bitmap,SelectObject(dc,bitmap)};
    auto* pixels=static_cast<std::uint32_t*>(raw);
    context.surfacePixels=pixels;context.surfaceWidth=width;context.surfaceHeight=height;
    RuntimeMapRenderStats stats;
    const auto draw=[&](){
        refreshRuntimeSpecialMechanismRenderSnapshots(*s);context.animationTick=s->tick;
        std::fill(pixels,pixels+width*height,0x00242c30);
        stats=drawRuntimeMap(dc,viewport,s->world,context,s->camera,s->renderCache);GdiFlush();
        return std::vector<std::uint32_t>(pixels,pixels+width*height);
    };
    const auto image=[&](const std::string& name){
        if(const char* directory=std::getenv("CONCORD_MIRAGE_DIR")){
            BITMAPFILEHEADER header{};header.bfType=0x4d42;
            header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
            header.bfSize=header.bfOffBits+width*height*4;
            std::ofstream out(std::filesystem::path(directory)/(name+".bmp"),std::ios::binary);
            out.write(reinterpret_cast<const char*>(&header),sizeof(header));
            out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));
            out.write(reinterpret_cast<const char*>(pixels),width*height*4);
        }
    };
    const auto idle=draw();image("mirage-owner-idle");
    std::cout<<"Mirage owner idle: logical="<<s->specialMechanisms.isCamouflaged(1)
        <<" treeActors="<<stats.specialMechanismRenderActors<<" tankBodies="<<stats.visibleVoxelBodies<<'\n';
    if(!diagnose && (stats.specialMechanismRenderActors!=1||stats.visibleVoxelBodies!=0))
        return fail("owner still sees only tank");
    const auto projection=runtimeProjectedObjectsForViewport(*s,viewport);
    if(projection.size()!=1)return fail("owner selection projection");
    if(!diagnose){
        auto snapshots=s->specialMechanismRenderSnapshots;
        auto camouflage=std::find_if(snapshots.begin(),snapshots.end(),[](const auto& v){return v.kind=="camouflage";});
        if(camouflage==snapshots.end())return fail("render snapshot");
        const auto paint=[&](const GameWorld& world){
            std::fill(pixels,pixels+width*height,0x00242c30);
            stats=drawRuntimeMap(dc,viewport,world,context,s->camera,s->renderCache);GdiFlush();
            return std::vector<std::uint32_t>(pixels,pixels+width*height);
        };
        GameWorld reference=s->world;reference.accessObjects().clear();
        context.shadowsEnabled=false;
        const auto treeBody=paint(s->world);
        context.specialMechanismSnapshots=nullptr;
        const auto ground=paint(reference);
        bool pickedCanopy=false;
        for(int y=viewport.top;y<viewport.bottom;++y)for(int x=viewport.left;x<viewport.right;++x){
            if(treeBody[y*width+x]==ground[y*width+x])continue;
            if(!pointInRectInclusive(projection.front().bounds,{x,y}))return fail("tree pixels outside click bounds");
            if(y<projection.front().selectionBounds.top
                &&findProjectedObjectAtPoint(projection,{x,y})!=nullptr)pickedCanopy=true;
        }
        if(!pickedCanopy)return fail("tall tree canopy cannot select its tank");
        context.shadowsEnabled=true;
        int cases=0;
        auto trees=s->rules.getSpecialMechanismRules().defaultMirageDisguises;
        for(const std::string tree:{"TREE01","TREE02","TREE03","TREE04"})
            if(std::find(trees.begin(),trees.end(),tree)==trees.end())trees.push_back(tree);
        for(const auto& tree:trees){
            if(s->rules.findTerrainType(tree)==nullptr)return fail("invalid stock tree");
            WorldTerrainObject terrain;terrain.type=tree;terrain.x=terrain.y=25;
            terrain.cell=runtimeCellKey(25,25);reference.accessTerrainObjects()={terrain};
            context.specialMechanismSnapshots=nullptr;context.animationTick=40;context.fixedTickHz=30;
            const auto expected=paint(reference);
            image("reference-"+tree);
            if(stats.terrainObjectSprites!=1)return fail("stock tree did not decode: "+tree);
            camouflage->resourceId=tree;context.specialMechanismSnapshots=&snapshots;
            for(const std::string owner:{"Americans","Ally","Russians",""}){
                context.visionObserverOwner=owner;
                const auto actual=paint(s->world);image("disguise-"+tree);
                if(actual!=expected||stats.specialMechanismRenderActors!=1||stats.visibleVoxelBodies!=0)
                    return fail("tree resource/palette/anchor mismatch: "+tree+" observer="+owner);
                ++cases;
            }
        }
        for(int hz:{15,30,60})for(int frame:{0,7,8,20,55,56,255,256,264}){
            context.fixedTickHz=hz;context.animationTick=camouflage->progress+frame*hz/15;
            for(const std::string owner:{"Americans","Ally","Russians"}){
                context.visionObserverOwner=owner;paint(s->world);
                const int phase=(frame+64)%256;
                const bool tree=owner!="Americans"||(phase>=72&&phase<=119);
                if((stats.specialMechanismRenderActors==1)!=tree||(stats.visibleVoxelBodies==0)!=tree)
                    return fail("owner blink/frame-rate mismatch");
                ++cases;
            }
        }
        RuntimeVisionSystem vision;
        vision.update(s->world,&s->rules,{{"Russians",{"Russians"}}},s->tick);
        context.vision=&vision;context.visionObserverOwner="Russians";paint(s->world);
        if(stats.specialMechanismRenderActors!=0||stats.visibleVoxelBodies!=0)return fail("shroud leaks camouflage");
        vision.revealMap("Russians");paint(s->world);
        if(stats.specialMechanismRenderActors!=1)return fail("visible enemy tree missing");
        context.vision=nullptr;context.visionObserverOwner="Americans";context.fixedTickHz=30;
        context.specialMechanismSnapshots=&s->specialMechanismRenderSnapshots;
        std::cout<<"Mirage render: "<<cases<<" resource/observer/frame-rate cases, canopy picking, shroud passed\n";
    }
    const auto objectHash=s->world.deterministicSimulationStateHash();
    s->selectedObjectIds={1};
    if(draw()!=idle || s->world.deterministicSimulationStateHash()!=objectHash)
        return fail("selection changes camouflage body/state");
    drawRuntimeInteractionOverlay(dc,client,*s,context);GdiFlush();image("mirage-selected");
    std::ostringstream saved;
    if(!writeRuntimeStateToStream(*s,saved,error))return fail("save: "+error);
    IniFile ini;ini.loadFromString(saved.str());auto restored=std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*restored,*s,false);
    if(!materializeRuntimeLoadStateFromIni(*restored,ini,error)
        || restored->specialMechanisms.stateHash()!=s->specialMechanisms.stateHash())
        return fail("save lost disguise: "+error);
    refreshRuntimeSpecialMechanismRenderSnapshots(*restored);
    context.specialMechanismSnapshots=&restored->specialMechanismRenderSnapshots;
    std::fill(pixels,pixels+width*height,0x00242c30);
    drawRuntimeMap(dc,viewport,restored->world,context,s->camera,s->renderCache);GdiFlush();
    if(!std::equal(idle.begin(),idle.end(),pixels))return fail("save changed tree pixels/owner blink phase");
    context.specialMechanismSnapshots=&s->specialMechanismRenderSnapshots;
    auto target=tank;target.instanceId=2;target.kind=WorldObjectKind::Structure;
    target.type="GACNST";target.owner="Russians";
    target.x=28;target.cell=runtimeCellKey(28,25);target.health=1000;
    s->world.accessObjects().push_back(target);invalidateRuntimeObjectProjectionCache(*s);
    issueRuntimeAttackCommand(*s,*findObjectById(*s,2));
    if(s->lastCommand.acceptedCount!=1)return fail("attack command rejected");
    int disguised=0,blinking=0,shots=0;int previousHealth=1000;
    int previousBlink=s->specialMechanisms.camouflageStates().at(1).fakeBlinkUntilTick;
    for(int tick=0;tick<180;++tick){step();
        const auto* victim=findObjectById(*s,2);if(!victim)return fail("attack target vanished: tick="
            +std::to_string(s->tick)+" previousHealth="+std::to_string(previousHealth)+" shots="+std::to_string(shots));
        if(victim->health<previousHealth)++shots;previousHealth=victim->health;
        if(s->specialMechanisms.isCamouflaged(1))++disguised;
        if(s->specialMechanisms.isCamouflageBlinking(1,s->tick))++blinking;
        const int until=s->specialMechanisms.camouflageStates().at(1).fakeBlinkUntilTick;
        if(!diagnose&&until>previousBlink&&until-s->tick!=30)return fail("15 original blink frames not scaled to 30 Hz");
        previousBlink=until;
    }
    std::cout<<"Mirage stationary attack: shots="<<shots<<" disguisedTicks="<<disguised
        <<" blinkTicks="<<blinking<<" mission="<<findObjectById(*s,1)->mission<<'\n';
    if(!diagnose && (shots<2||disguised<175||blinking==0))return fail("stationary firing destroys camouflage");
    // Use the same single-player UI path as the attack/move above. This
    // isolated fixture has no authenticated multiplayer house registry.
    issueRuntimeStopCommand(*s);
    if(s->lastCommand.acceptedCount!=1)return fail("stop command");
    findMutableObjectById(*s,2)->alive=false;
    issueRuntimeMoveCommand(*s,{22,25});
    if(s->lastCommand.acceptedCount!=1||s->specialMechanisms.isCamouflaged(1))return fail("move reveal");
    bool arrived=false;
    for(int tick=0;tick<600;++tick){step();const auto* actor=findObjectById(*s,1);
        if(actor&&actor->x==22&&actor->y==25&&!actor->hasRenderPosition
            &&s->specialMechanisms.isCamouflaged(1)){arrived=true;break;}}
    if(!arrived)return fail("movement never returns to stationary disguise");
    std::cout<<(diagnose?"Mirage diagnosis completed":"Mirage disguise: passed")<<'\n';return true;
}
