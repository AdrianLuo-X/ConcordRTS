bool runRuntimeYuriCursorSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail=[&](const std::string& why){error="Yuri cursor: "+why;return false;};
    const char* output=std::getenv("CONCORD_YURI_CURSOR_OUTPUT");const std::string root=output?output:"";
    if(!root.empty())std::filesystem::create_directories(root);
    constexpr int width=960,height=640;constexpr std::uint32_t background=0x00283840U;
    const RECT client{0,0,width,height},viewport=runtimeMapViewport(client);
    int cases=0,frames=0,pixelsCompared=0,mismatches=0,exclusions=0,commands=0;
    for(const std::string type:{"YURI","YURIPR","MIND","YAPSYT"})
        for(const std::string targetType:{"E2","MTNK","GAPOWR"}) {
        if(targetType=="GAPOWR"&&type!="YURIPR")continue;
        const std::string label=type+"-"+targetType;
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);s.config.computerAiEnabled=false;s.config.multiplayerActive=false;
        s.config.fixedTickHz=30;s.config.noCooldown=false;s.config.playerSlots.clear();s.config.playerCountry="YuriCountry";
        s.economy.owner="YuriCountry";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimeSoundsEnabled=false;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear();s.world.accessTerrainCells().clear();
        s.terrainByCell.clear();s.terrainInfoByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;s.visionEnabled=false;
        for(int y=44;y<=88;++y)for(int x=44;x<=88;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=0;cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        const auto sourceKind=type=="YAPSYT"?RuntimeBuildKind::Building:type=="MIND"?RuntimeBuildKind::Unit:RuntimeBuildKind::Infantry;
        const auto targetKind=targetType=="GAPOWR"?RuntimeBuildKind::Building:targetType=="E2"?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit;
        auto* source=addRuntimeObjectInstance(s,sourceKind,type,{60,64});
        if(!source)return fail(label+" missing controller");const int sourceId=source->instanceId;
        auto* target=addRuntimeObjectInstance(s,targetKind,targetType,{64,64});
        if(!target)return fail(label+" missing target");const int targetId=target->instanceId;
        target->owner="Americans";target->mission="Sleep";target->weaponCooldownTicksRemaining=100000;
        const int targetHealth=target->health;
        if(!addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"NANRCT",{54,58}))return fail("power fixture");
        for(auto& object:s.world.accessObjects())object.buildAnimTicksRemaining=object.buildAnimTicksTotal=0;
        initializeRuntimeNavigation(s);rebuildRuntimeEconomyPower(s);RuntimeSnapshotActiveStateScope active(s);
        ensureRuntimeSpecialMechanismsInitialized(s);
        s.selectedObjectIds={sourceId};s.mouseClientPointValid=true;
        centerRuntimeCameraOnCell(s,client,{64,64});invalidateRuntimeObjectProjectionCache(s);
        const auto& projections=runtimeProjectedObjectsForViewport(s,viewport);
        std::optional<POINT> point;
        for(const auto& p:projections)if(p.object&&p.object->instanceId==targetId) {
            const POINT middle{(p.bounds.left+p.bounds.right)/2,(p.bounds.top+p.bounds.bottom)/2};
            const auto hit=[&](POINT candidate){const auto* found=findProjectedObjectAtPoint(projections,candidate);
                return found&&found->object&&found->object->instanceId==targetId;};
            if(hit(middle))point=middle;
            for(int y=p.bounds.top;y<p.bounds.bottom&&!point;y+=2)
                for(int x=p.bounds.left;x<p.bounds.right&&!point;x+=2)if(hit({x,y}))point=POINT{x,y};
        }
        if(!point||runtimeTargetCursorIntent(s,*findObjectById(s,targetId))!=RuntimeTargetCursorIntent::MindControl)
            return fail(label+" projected psychic target fixture");
        s.mouseClientPoint=*point;
        struct Surface {RuntimeWindowState& state;HDC screen=GetDC(nullptr);
            ~Surface(){releaseRuntimeBackBuffer(state.backBuffer);if(screen)ReleaseDC(nullptr,screen);}} surface{s};
        if(!surface.screen||!ensureRuntimeBackBuffer(s.backBuffer,surface.screen,width,height))return fail("surface");
        const auto clear=[&]{GdiFlush();std::fill_n(static_cast<std::uint32_t*>(s.backBuffer.pixels),width*height,background);};
        const auto capture=[&]{GdiFlush();const auto* p=static_cast<std::uint32_t*>(s.backBuffer.pixels);
            std::vector<std::uint32_t> result(p,p+width*height);for(auto& pixel:result)pixel&=0xffffffU;return result;};
        std::vector<RuntimePaletteColor> palette;std::string path;
        const auto cacheMix=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/"ra2"/"cache.mix");
        const auto mouseMix=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/"ra2"/"conquer.mix");
        if(!loadRuntimePaletteFromMixArchive(cacheMix,s.renderCache,"mousepal",palette,path,error))return fail(error);
        for(int phase=0;phase<5;++phase) {
            s.tick=phase*4;const auto cursor=runtimeMouseCursorFrameForPoint(s,client,*point);
            const bool correct=cursor&&cursor->atlasId=="mouse"&&cursor->frameIndex==53+phase&&cursor->offsetX==27&&cursor->offsetY==21;
            if(!correct)++mismatches;
            clear();drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc,client,s);const auto actual=capture();
            RuntimeShpFrameImage frame;
            if(!loadRuntimeShpFrameFromMixArchive(mouseMix,s.renderCache,"mouse",53+phase,frame,path,error))return fail(error);
            std::vector<std::uint32_t> expected(width*height,background);
            for(int y=0;y<frame.height;++y)for(int x=0;x<frame.width;++x) {
                const auto index=frame.pixels[y*frame.width+x];const int dx=point->x-27+frame.offsetX+x,dy=point->y-21+frame.offsetY+y;
                if(index&&dx>=0&&dx<width&&dy>=0&&dy<height) {const auto color=palette[index];
                    expected[dy*width+dx]=(color.r<<16)|(color.g<<8)|color.b;}
            }
            if(correct&&actual!=expected)return fail(label+" original attack cursor pixels");
            if(correct)++pixelsCompared;++frames;
            if(!root.empty()) {
                std::vector<std::uint32_t> crop(55*42,background);
                for(int y=0;y<42;++y)for(int x=0;x<55;++x) {
                    const int sx=point->x-27+x,sy=point->y-21+y;
                    if(sx>=0&&sx<width&&sy>=0&&sy<height)crop[y*55+x]=actual[sy*width+sx];
                }
                if(!writeRuntimeSmokeBmp(root+"/"+label+"-cursor-"+std::to_string(phase)+".bmp",crop.data(),55,42,error))return fail(error);
            }
        }
        if(!root.empty()) {
            std::vector<std::uint32_t> scene;
            if(!drawRuntimeStateOffscreenAtCell(s,{64,64},width,height,error,"",false,true,-1,&scene,true,&s.camera))return fail(error);
            GdiFlush();std::memcpy(s.backBuffer.pixels,scene.data(),scene.size()*sizeof(std::uint32_t));
            drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc,client,s);GdiFlush();
            if(!writeRuntimeSmokeBmp(root+"/"+label+"-scene.bmp",s.backBuffer.pixels,width,height,error))return fail(error);
        }
        const auto normalAttack=[&]{const auto c=runtimeMouseCursorFrameForPoint(s,client,*point);
            return c&&c->frameIndex>=53&&c->frameIndex<=57;};
        s.selectedObjectIds.clear();if(normalAttack())return fail("empty selection");++exclusions;
        s.selectedObjectIds={sourceId};target=findMutableObjectById(s,targetId);target->owner=s.economy.owner;
        if(normalAttack())return fail("allied target");target->owner="Americans";++exclusions;
        s.activeSidebarTool=RuntimeSidebarTool::Repair;
        if(runtimeMouseCursorFrameForPoint(s,client,*point)->frameIndex!=190)return fail("repair priority");++exclusions;
        s.activeSidebarTool=RuntimeSidebarTool::Sell;
        if(runtimeMouseCursorFrameForPoint(s,client,*point)->frameIndex!=149)return fail("sell priority");++exclusions;
        s.activeSidebarTool=RuntimeSidebarTool::None;s.rightPointer.active=true;
        if(runtimeMouseCursorFrameForPoint(s,client,*point)->frameIndex!=0)return fail("pan priority");++exclusions;
        s.rightPointer.active=false;s.forceAttackHeld=true;
        if(!normalAttack())return fail("force-fire priority");s.forceAttackHeld=false;++exclusions;
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{64,64},findObjectById(s,targetId))
            ||s.lastCommand.acceptedCount!=1)return fail(label+" Primary attack rejected: "+s.interactionStatus);
        bool controlled=false;
        for(int tick=0;tick<300&&!controlled;++tick) {
            ++s.tick;advanceRuntimeSimulation(s);
            const auto* unit=findObjectById(s,targetId);
            controlled=unit&&unit->owner==s.economy.owner;
        }
        target=findMutableObjectById(s,targetId);
        if(!controlled||!target||target->health!=targetHealth||s.specialMechanisms.renderSnapshots(s.tick,30).empty())
            return fail(label+" click did not establish undamaged mind control: "+s.simulationStatus);
        if(normalAttack())return fail("controlled ally still advertises an attack");++exclusions;
        ++commands;++cases;
        std::cout<<"Yuri cursor: "<<label<<" native frames, Primary order and real control passed\n";
    }
    std::cout<<"Yuri cursor: "<<cases<<" cases, "<<frames<<" frames, "<<pixelsCompared
        <<" native pixel comparisons, "<<exclusions<<" exclusions, "<<commands<<" real controls; mismatches="<<mismatches<<'\n';
    return mismatches==0?true:fail("brain cursor instead of attack: "+std::to_string(mismatches));
}
