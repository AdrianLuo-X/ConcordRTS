bool runRuntimeEnemySelectionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail=[&](const std::string& why){error="Enemy selection: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_ENEMY_SELECTION_OUTPUT");
    const std::string root=output?output:"";
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("native resources");
    int cases=0,inputs=0,hovers=0,saves=0,attacks=0,authority=0;
    for(const char* type:{"E1","MTNK","GAWEAP"})for(const char* owner:{"Russians","British","Neutral"}) {
        scene=std::string(type)+" "+owner;
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
        s.config=config;s.config.fixedTickHz=30;s.config.playerCountry="Americans";
        s.config.computerAiEnabled=s.config.shortGame=s.config.crates=s.config.noCooldown=false;
        s.config.multiplayerActive=false;s.config.startingUnits=0;s.config.playerSlots.clear();
        s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.world=assets->world;s.world.accessObjects().clear();s.report=assets->report;s.mapFacts=assets->mapFacts;
        s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeSkirmishStart(s);
        s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        s.playerCommands.alliances[{"americans","british"}]=true;s.playerCommands.alliances[{"british","americans"}]=true;
        auto* p=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{110,215});
        if(!p)return fail("local creation");const int local=p->instanceId;p->mission="Sleep";
        const auto kind=std::string(type)=="E1"?RuntimeBuildKind::Infantry:std::string(type)=="MTNK"?RuntimeBuildKind::Unit:RuntimeBuildKind::Building;
        p=addRuntimeObjectInstance(s,kind,type,{114,215});if(!p)return fail("target creation");
        const int target=p->instanceId;p->owner=owner;p->health=std::max(1,p->health/2);
        p->mission="Sleep";p->buildAnimTicksRemaining=p->buildAnimTicksTotal=0;p->poweredOperational=true;
        ++s.tick;advanceRuntimeSimulation(s,false);
        const RECT client{0,0,1000,700};const RECT viewport=runtimeMapViewport(client);
        centerRuntimeCameraOnCell(s,client,{112,215});
        const auto projected=buildProjectedObjects(s,viewport);
        const auto view=std::find_if(projected.begin(),projected.end(),[&](const auto& v){return v.object&&v.object->instanceId==target;});
        if(view==projected.end())return fail("target projection");const RECT bounds=view->bounds;
        std::optional<POINT> hit;
        for(int y=std::max(viewport.top,bounds.top);y<std::min(viewport.bottom,bounds.bottom)&&!hit;++y)
            for(int x=std::max(viewport.left,bounds.left);x<std::min(viewport.right,bounds.right);++x){
                const auto* v=findProjectedObjectAtPoint(projected,{x,y});
                if(v&&v->object&&v->object->instanceId==target){hit=POINT{x,y};break;}}
        if(!hit)return fail("target hover pixels");
        HWND window=CreateWindowExW(0,L"STATIC",L"Enemy selection input",WS_POPUP,0,0,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)return fail("hidden input window");
        struct Cleanup{HWND w;~Cleanup(){DestroyWindow(w);}} cleanup{window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        const auto click=[&](WPARAM modifiers){
            runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON|modifiers,MAKELPARAM(hit->x,hit->y));
            runtimeWindowProc(window,WM_LBUTTONUP,modifiers,MAKELPARAM(hit->x,hit->y));};
        const auto box=[&](RECT r,WPARAM modifiers){
            runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON|modifiers,MAKELPARAM(r.left,r.top));
            runtimeWindowProc(window,WM_MOUSEMOVE,MK_LBUTTON|modifiers,MAKELPARAM(r.right,r.bottom));
            runtimeWindowProc(window,WM_LBUTTONUP,modifiers,MAKELPARAM(r.right,r.bottom));};
        const auto worldBefore=s.world.deterministicSimulationStateHash();
        for(WPARAM modifiers:{WPARAM{0},WPARAM{MK_SHIFT},WPARAM{MK_CONTROL}}) {
            s.selectedObjectIds.clear();click(modifiers);
            if(selectionContains(s,target)) {
                if(!root.empty())drawRuntimeStateOffscreenAtCell(s,{112,215},800,600,error,root+"/before-selected-enemy.bmp",false,true,target);
                return fail("mouse selected foreign target");
            }
            ++inputs;s.selectedObjectIds.clear();box(bounds,modifiers);
            if(selectionContains(s,target))return fail("box selected foreign target");++inputs;
        }
        for(WPARAM modifiers:{WPARAM{0},WPARAM{MK_SHIFT},WPARAM{MK_CONTROL}}) {
            s.selectedObjectIds.clear();box({viewport.left+5,viewport.top+5,viewport.right-5,viewport.bottom-5},modifiers);
            if(s.selectedObjectIds!=std::vector<int>{local})return fail("mixed box did not keep only local actor");++inputs;
        }
        s.selectedObjectIds={local};click(MK_SHIFT);
        if(s.selectedObjectIds!=std::vector<int>{local})return fail("shift click appended foreign target");++inputs;
        s.config.keyboardReferenceLayout=true;s.config.keyBindings.clear();ensureRuntimeMenuKeyboardLoaded(s);
        s.playerLocalCommands.teamCreate(0,{target,local},[](int){return true;});
        for(int key:std::array<int,9>{'T','P','Y','U','M','N','1','1'|runtimePlayerShiftKey,'1'|runtimePlayerAltKey}) {
            s.selectedObjectIds={target};handleRuntimePlayerCommandCode(s,client,key,false,1000+inputs*700);
            if(selectionContains(s,target))return fail("shortcut retained foreign target");++inputs;
        }
        centerRuntimeCameraOnCell(s,client,{112,215});
        s.selectedObjectIds.clear();runtimeWindowProc(window,WM_MOUSEMOVE,0,MAKELPARAM(hit->x,hit->y));
        const auto* hovered=findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s,viewport),*hit);
        if(!hovered||!hovered->object||hovered->object->instanceId!=target)return fail("enemy hover lost");
        const auto capture=[&](RuntimeWindowState& state,const std::string& name,std::vector<std::uint32_t>& pixels){
            return drawRuntimeStateOffscreenAtCell(state,{112,215},800,600,error,name,false,true,target,&pixels).has_value();};
        std::vector<std::uint32_t> pixels,repeat;
        if(!capture(s,!root.empty()&&std::string(owner)=="Russians"?root+"/"+type+"-enemy-hover.bmp":"",pixels)
            ||s.lastSelectedHealthBars!=0||s.lastHoveredUnitHealthBars+s.lastHoveredStructureHealthBars!=1
            ||s.lastHealthPipResourceDraws<=0||s.lastHealthPipFallbackDraws!=0)return fail("native hover health missing");
        if(!capture(s,"",repeat)||pixels!=repeat)return fail("paused hover frame unstable");++hovers;
        if(s.world.deterministicSimulationStateHash()!=worldBefore)return fail("selection changed simulation state");
        for(bool exact:{false,true}) {
            std::ostringstream saved;IniFile ini;
            if(!writeRuntimeStateToStream(s,saved,error)||(exact&&!writeRuntimeMultiplayerSnapshotExtension(s,saved,error))
                ||!ini.loadFromString(saved.str()))return fail("save: "+error);
            auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
            if(!materializeRuntimeLoadStateFromIni(*loaded,ini,error,exact)
                ||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*loaded,ini,error)))return fail("reload: "+error);
            if(!loaded->selectedObjectIds.empty()||runtimePlayerLocalSelectable(*loaded,target))return fail("reload selects foreign actor");
            if(!capture(*loaded,"",repeat)||repeat!=pixels)return fail("reload hover pixels changed");++saves;
        }
        // Acquired ownership is selectable immediately; losing ownership must
        // remove the old selection at the next real simulation boundary.
        findMutableObjectById(s,target)->owner=s.economy.owner;s.selectedObjectIds.clear();click(0);
        if(s.selectedObjectIds!=std::vector<int>{target})return fail("owned target click rejected");
        findMutableObjectById(s,target)->owner=owner;++s.tick;advanceRuntimeSimulation(s,false);
        if(selectionContains(s,target))return fail("selection survived ownership loss");
        if(std::string(owner)=="Russians") {
            s.selectedObjectIds={local};click(0);
            if(s.selectedObjectIds!=std::vector<int>{local}||s.lastCommand.acceptedCount!=1
                ||!s.behaviors.contains(local)||s.behaviors.at(local).targetObjectId!=target)return fail("normal enemy attack click rejected");
            issueRuntimeStopCommand(s);++attacks;
            if(kind!=RuntimeBuildKind::Building) {
                if(issueRuntimeTeamMove(s,{target},{118,215})!=1||s.selectedObjectIds!=std::vector<int>{local})
                    return fail("autonomous enemy team lost command or local selection");++authority;
                RuntimeHumanHouseAuthority remote;remote.owner=owner;remote.canonicalOwner=runtimeCanonicalHumanHouseOwner(owner);
                remote.remoteEconomy=s.economy;remote.remoteEconomy->owner=owner;
                remote.remoteTechTree=s.techTree;remote.remoteTechTreeLoaded=s.techTreeLoaded;s.humanHouseAuthorities.push_back(std::move(remote));
                const bool accepted=applyRuntimeGameplaySelectionCommand(s,owner,{target},[&](){
                    issueRuntimeMoveCommand(s,{119,215});return s.lastCommand.acceptedCount==1;});
                if(!accepted||s.selectedObjectIds!=std::vector<int>{local}||s.economy.owner!="Americans")return fail("remote house command or facade restoration");++authority;
            }
        }
        ++cases;std::cout<<"Enemy selection: "<<scene<<" click/box/keys/hover/save/ownership=ok\n";
    }
    std::cout<<"Enemy selection: cases="<<cases<<" inputChecks="<<inputs<<" hoverChecks="<<hovers
        <<" fullSaves="<<saves<<" enemyAttacks="<<attacks<<" authorityChecks="<<authority<<" PASS\n";
    return true;
}
