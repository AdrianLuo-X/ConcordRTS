bool runRuntimeMissileHoverSmoke(RuntimeWindowState& live,std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why){error="missile hover: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_MISSILE_HOVER_OUTPUT");const std::string root=output?output:"";
    if(!root.empty())std::filesystem::create_directories(root);
    int scenes=0,frames=0,clicks=0;
    for(const char* parentType:{"V3","DRED","BSUB"})for(bool enemy:{false,true}){
        scene=std::string(parentType)+(enemy?" enemy":" own");
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=live.config;s.config.fixedTickHz=30;s.config.computerAiEnabled=false;s.config.playerSlots.clear();
        s.config.multiplayerActive=false;s.report=live.report;s.art=live.art;s.theater=live.theater;
        IniFile rules;if(!rules.loadFromFile(live.report.rulesPath)||!s.rules.loadFromIni(rules))return fail("native rules");
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        for(int y=44;y<=88;++y)for(int x=44;x<=92;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance parent;parent.instanceId=1;parent.type=parentType;parent.kind=WorldObjectKind::Unit;
        parent.owner=enemy?"Russians":"Americans";parent.alive=true;parent.currentAmmo=-1;
        parent.health=runtimeMaxHealthForObject(s,parent);parent.x=60;parent.y=64;parent.cell=runtimeCellKey(60,64);
        parent.mission="Attack";parent.facing=parent.turretFacing=runtimeFacingFromDelta(1,0);
        auto target=parent;target.instanceId=2;target.type="CAEAST01";target.kind=WorldObjectKind::Structure;
        target.owner=enemy?"Americans":"Russians";target.health=100000;target.x=78;target.cell=runtimeCellKey(78,64);target.mission="Sleep";
        s.world.accessObjects()={parent,target};initializeRuntimeNavigation(s);
        RuntimeSnapshotActiveStateScope active(s);
        RuntimeObjectBehavior attack;attack.kind=RuntimeBehaviorKind::Attack;attack.state=RuntimeBehaviorState::Queued;attack.targetObjectId=2;
        s.behaviors[1]=attack;advanceRuntimeSpecialMechanisms(s);
        const char* weaponId=std::string(parentType)=="V3"?"V3Launcher":std::string(parentType)=="DRED"?"DredLauncher":"CruiseLauncher";
        const auto* weapon=s.rules.findWeaponType(weaponId);
        if(!weapon||!launchRuntimeSpawnedMissileVolley(s,*findObjectById(s,1),*findObjectById(s,2),*weapon,1.0))return fail("native launch");
        const int id=s.specialMechanisms.spawnedMissiles().begin()->first;
        bool flying=false;
        for(int tick=0;tick<30*30;++tick){
            ++s.tick;advanceRuntimeEffects(s);
            const auto it=s.specialMechanisms.spawnedMissiles().find(id);
            if(it==s.specialMechanisms.spawnedMissiles().end())return fail("missile vanished before hover");
            if(it->second.released&&std::hypot(it->second.x-it->second.originX,it->second.y-it->second.originY)>=3.0){flying=true;break;}
        }
        if(!flying)return fail("flight release");
        const RECT client{0,0,1000,700};const auto viewport=runtimeMapViewport(client);
        const auto* missile=findObjectById(s,id);const RuntimeCellPoint center{missile->x,missile->y};
        centerRuntimeCameraOnCell(s,client,center);ensureRuntimeCameraForViewport(s,viewport);
        HWND window=CreateWindowExW(0,L"STATIC",L"Missile hover check",WS_POPUP,0,0,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)return fail("input window");
        struct Cleanup{HWND h;~Cleanup(){SetWindowLongPtrW(h,GWLP_USERDATA,0);DestroyWindow(h);}}cleanup{window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        POINT point{};RECT bounds{};bool found=false;
        for(const auto& projected:runtimeProjectedObjectsForViewport(s,viewport))if(projected.object&&projected.object->instanceId==id){
            bounds=projected.bounds;
            const auto body=projected.spriteBounds;
            point={(body.left+body.right)/2,(body.top+body.bottom)/2};
            if(!runtimeRectEquals(projected.selectionBounds,body))return fail("health frame detached from missile body");
            const auto* hit=runtimeCursorObjectAtPoint(s,viewport,point);
            found=hit&&hit->instanceId==id&&pointInRectInclusive(viewport,point);break;
        }
        if(!found)return fail("visible missile hit point");
        if(runtimePlayerLocalSelectable(s,id)||runtimePlayerMobileSelectable(s,*findObjectById(s,id)))return fail("hotkey/team selection eligible");
        for(int percent:{100,50,10}){
            auto* mutableMissile=findMutableObjectById(s,id);mutableMissile->health=std::max(1,runtimeMaxHealthForObject(s,*mutableMissile)*percent/100);
            const auto hash=s.world.deterministicSimulationStateHash();s.selectedObjectIds.clear();
            const auto stats=drawRuntimeStateOffscreenAtCell(s,center,1000,700,error,
                root.empty()?"":root+"/"+parentType+(enemy?"-enemy-":"-own-")+std::to_string(percent)+".bmp",
                false,true,id,nullptr,true,&s.camera);
            if(!stats||!stats->rendered||s.lastHoveredUnitHealthBars!=1||s.lastSelectedHealthBars!=0||s.lastUnitSelectionFrames!=0)
                return fail("hover health without selection frame: "+error);
            if(s.world.deterministicSimulationStateHash()!=hash)return fail("hover mutated missile flight");
            ++frames;
        }
        s.selectedObjectIds.clear();
        runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        if(selectionContains(s,id))return fail("mouse selected missile");++clicks;
        s.selectedObjectIds.clear();s.leftPointer.active=s.leftPointer.moved=true;
        s.leftPointer.start={bounds.left-2,bounds.top-2};s.leftPointer.current={bounds.right+2,bounds.bottom+2};
        runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(s.leftPointer.current.x,s.leftPointer.current.y));
        if(selectionContains(s,id))return fail("box selected missile");++clicks;
        for(const auto modifiers:{std::pair{false,false},std::pair{true,false},std::pair{false,true}}){
            s.selectedObjectIds={1};applySelection(s,{id},modifiers.first,modifiers.second);
            if(selectionContains(s,id))return fail("append/toggle selection accepted missile");
        }
        s.selectedObjectIds={id};cleanRuntimeSelection(s);
        if(!s.selectedObjectIds.empty())return fail("stale selected missile survived cleanup");
        s.selectedObjectIds.clear();applySelection(s,{1},false,false);
        if(s.selectedObjectIds!=std::vector<int>{1})return fail("launcher can no longer be selected");
        if(enemy){
            auto aa=parent;aa.instanceId=100;aa.type="FV";aa.owner="Americans";aa.x=missile->x-2;aa.y=missile->y;
            aa.cell=runtimeCellKey(aa.x,aa.y);aa.health=200;aa.mission="Guard";s.world.accessObjects().push_back(aa);
            s.selectedObjectIds={100};const auto intent=runtimeTargetCursorIntent(s,*findObjectById(s,id));
            if(intent!=RuntimeTargetCursorIntent::Attack&&intent!=RuntimeTargetCursorIntent::AttackOutOfRange)return fail("enemy missile no longer targetable");
            issueRuntimeAttackCommand(s,*findObjectById(s,id));
            const auto order=s.behaviors.find(100);
            if(order==s.behaviors.end()||order->second.kind!=RuntimeBehaviorKind::Attack||order->second.targetObjectId!=id)return fail("anti-air attack order rejected");
        }
        ++scenes;
    }
    std::cout<<"Missile hover passed: "<<scenes<<" native V3/DRED/BSUB flights, "<<frames<<" health frames, "<<clicks
        <<" mouse/box checks, append/toggle/team exclusion, launcher selection and enemy anti-air targeting\n";
    return true;
}
