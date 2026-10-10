#include "render/effects/diagnostics/RuntimeTemporalFrozenPixelsSmoke.inl"

bool runRuntimeTemporalFrozenUiSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail=[&](const std::string& why){error="Temporal frozen UI: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_TEMPORAL_FROZEN_OUTPUT");
    const std::string root=output?output:"";
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("original resources");
    int cases=0,inputs=0,saves=0,hovers=0,releases=0;
    for(const char* type:{"E1","MTNK","GAWEAP","ATESLA"})for(int hz:{15,30,60}) {
        scene=std::string(type)+" hz="+std::to_string(hz);
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        RuntimeSnapshotActiveStateScope active(s);
        s.config=config;s.config.fixedTickHz=hz;s.config.playerCountry="Americans";
        s.config.computerAiEnabled=s.config.shortGame=s.config.crates=s.config.noCooldown=false;
        s.config.multiplayerActive=false;s.config.startingUnits=0;s.config.playerSlots.clear();
        s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;s.world=assets->world;
        s.world.accessObjects().clear();s.report=assets->report;s.mapFacts=assets->mapFacts;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeSkirmishStart(s);
        s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        const auto kind=std::string(type)=="E1"?RuntimeBuildKind::Infantry:std::string(type)=="MTNK"?RuntimeBuildKind::Unit:RuntimeBuildKind::Building;
        auto* p=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"CLEG",{110,215});
        if(!p)return fail("CLEG creation");const int attacker=p->instanceId;
        p=addRuntimeObjectInstance(s,kind,type,{114,215});
        if(!p)return fail("target creation");const int target=p->instanceId;
        p->buildAnimTicksRemaining=p->buildAnimTicksTotal=0;p->mission="Sleep";p->poweredOperational=true;
        const int health=p->health;
        applySelection(s,{attacker},false,false);issueRuntimeAttackCommand(s,*p,true);
        if(s.lastCommand.acceptedCount!=1)return fail("friendly force attack");
        applySelection(s,{target},false,false);
        if(!selectionContains(s,target))return fail("unfrozen target not selectable");
        for(int n=0;n<hz*10&&!s.specialMechanisms.isTemporalFrozen(target);++n){++s.tick;advanceRuntimeSimulation(s,false);}
        if(!s.specialMechanisms.isTemporalFrozen(target))return fail("real shot never froze target");
        if(selectionContains(s,target)) {
            if(!root.empty())drawRuntimeStateOffscreenAtCell(s,{114,215},800,600,error,root+"/before-selected-frozen.bmp",false,true,target);
            return fail("frozen target remained selected after simulation tick");
        }
        if(runtimeObjectAcceptsPlayerSelection(s,*findObjectById(s,target))||runtimePlayerLocalSelectable(s,target))
            return fail("selection eligibility accepts frozen target");
        s.selectedObjectIds={target};cleanRuntimeSelection(s);
        if(selectionContains(s,target))return fail("obsolete selection survived cleanup");
        applySelection(s,{target},false,false);if(selectionContains(s,target))return fail("direct selection accepted");
        const RECT client{0,0,1000,700};const RECT viewport=runtimeMapViewport(client);
        centerRuntimeCameraOnCell(s,client,{114,215});
        const auto projected=buildProjectedObjects(s,viewport);
        const auto view=std::find_if(projected.begin(),projected.end(),[&](const auto& v){return v.object&&v.object->instanceId==target;});
        if(view==projected.end())return fail("frozen target lost interaction geometry");
        const RECT bounds=view->bounds;std::optional<POINT> hit;
        for(int y=std::max(viewport.top,bounds.top);y<std::min(viewport.bottom,bounds.bottom)&&!hit;++y)
            for(int x=std::max(viewport.left,bounds.left);x<std::min(viewport.right,bounds.right);++x){
                const auto* v=findProjectedObjectAtPoint(projected,{x,y});
                if(v&&v->object&&v->object->instanceId==target){hit=POINT{x,y};break;}}
        if(!hit)return fail("frozen body cannot be hovered");
        HWND window=CreateWindowExW(0,L"STATIC",L"Temporal frozen input",WS_POPUP,0,0,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)return fail("hidden input window");
        struct Cleanup{HWND w;~Cleanup(){DestroyWindow(w);}} cleanup{window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        for(WPARAM modifiers:{WPARAM{0},WPARAM{MK_SHIFT},WPARAM{MK_CONTROL}}) {
            s.selectedObjectIds.clear();
            runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON|modifiers,MAKELPARAM(hit->x,hit->y));
            runtimeWindowProc(window,WM_LBUTTONUP,modifiers,MAKELPARAM(hit->x,hit->y));
            if(selectionContains(s,target))return fail("mouse selected frozen target");++inputs;
        }
        s.selectedObjectIds.clear();
        runtimeWindowProc(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(bounds.left,bounds.top));
        runtimeWindowProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(bounds.right,bounds.bottom));
        runtimeWindowProc(window,WM_LBUTTONUP,0,MAKELPARAM(bounds.right,bounds.bottom));
        if(selectionContains(s,target))return fail("box selected frozen target");++inputs;
        s.config.keyboardReferenceLayout=true;s.config.keyBindings.clear();ensureRuntimeMenuKeyboardLoaded(s);
        s.playerLocalCommands.teamCreate(0,{target},[](int){return true;});
        for(int key:std::array<int,7>{'T','P','Y','U','1','1'|runtimePlayerShiftKey,'1'|runtimePlayerAltKey}) {
            s.selectedObjectIds.clear();handleRuntimePlayerCommandCode(s,client,key,false,1000+inputs*700);
            if(selectionContains(s,target))return fail("shortcut selected frozen target");++inputs;
        }
        s.selectedObjectIds.clear();runtimeWindowProc(window,WM_MOUSEMOVE,0,MAKELPARAM(hit->x,hit->y));
        const auto* hovered=findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s,viewport),*hit);
        if(!hovered||!hovered->object||hovered->object->instanceId!=target)return fail("mouse hover suppressed");
        const auto image=[&](RuntimeWindowState& world,const std::string& phase,std::vector<std::uint32_t>& pixels,bool capture){
            return drawRuntimeStateOffscreenAtCell(world,{114,215},800,600,error,
                capture&&!root.empty()?root+"/"+type+"-"+phase+".bmp":"",false,true,target,&pixels).has_value();};
        std::vector<std::uint32_t> frozen,repeat;
        if(!image(s,"frozen-hover",frozen,hz==30)||s.lastSelectedHealthBars!=0
            ||s.lastHoveredUnitHealthBars+s.lastHoveredStructureHealthBars!=1
            ||s.lastHealthPipResourceDraws<=0||s.lastHealthPipFallbackDraws!=0)return fail("native hover health missing: "+error);
        ++hovers;
        if(!image(s,"repeat",repeat,false)||repeat!=frozen)return fail("paused repaint changed frozen frame");
        for(bool exact:{false,true}) {
            std::ostringstream saved;IniFile ini;
            if(!writeRuntimeStateToStream(s,saved,error)||(exact&&!writeRuntimeMultiplayerSnapshotExtension(s,saved,error))
                ||!ini.loadFromString(saved.str()))return fail("frozen write: "+error);
            auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
            if(!materializeRuntimeLoadStateFromIni(*loaded,ini,error,exact)
                ||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*loaded,ini,error)))return fail("frozen reload: "+error);
            if(!loaded->specialMechanisms.isTemporalFrozen(target))return fail("reload lost freeze");
            applySelection(*loaded,{target},false,false);
            if(selectionContains(*loaded,target))return fail("reload selectable");
            std::vector<std::uint32_t> restored;
            if(!image(*loaded,"restored",restored,false)||restored!=frozen)return fail("reload changed frozen body or hover pixels");
            ++saves;
        }
        applySelection(s,{attacker},false,false);issueRuntimeStopCommand(s);
        if(s.specialMechanisms.isTemporalFrozen(target)||findObjectById(s,target)->health!=health)return fail("release/damage changed");
        applySelection(s,{target},false,false);
        if(!selectionContains(s,target))return fail("released target not selectable");
        s.selectedObjectIds.clear();std::vector<std::uint32_t> released;
        if(hz==30&&!image(s,"released-hover",released,true))return fail(error);
        ++releases;++cases;
        std::cout<<"Temporal frozen UI: "<<scene<<" realFreeze/input/hover/nativePips/repaint/ordinary+snapshot/release=ok\n";
    }
    if(!runRuntimeTemporalFrozenPixelsSmoke(*assets,root,error))return false;
    std::cout<<"Temporal frozen UI: cases="<<cases<<" inputChecks="<<inputs<<" hoverChecks="<<hovers
        <<" saves="<<saves<<" releases="<<releases<<" PASS\n";
    return true;
}
