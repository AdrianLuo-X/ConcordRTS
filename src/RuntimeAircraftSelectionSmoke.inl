bool runRuntimeAircraftSelectionSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;std::string scene,phase;
    const auto fail=[&](const std::string& why){error="Aircraft selection: "+scene+" "+phase+": "+why;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("original resources");
    const char* output=std::getenv("CONCORD_AIRCRAFT_SELECTION_OUTPUT");
    int scenes=0,sorties=0,selectionTicks=0,reloads=0,cancellations=0,switches=0,deaths=0,frames=0;
    for(const auto* type:{"BEAG","ORCA"})for(int hz:{15,30,60})for(int count:{1,4})for(bool instant:{false,true}) {
        scene=std::string(type)+" hz="+std::to_string(hz)+" count="+std::to_string(count)+" noCooldown="+std::to_string(instant);
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=config;s.config.fixedTickHz=hz;s.config.noCooldown=instant;s.config.computerAiEnabled=false;
        s.config.shortGame=false;s.config.startingUnits=0;s.config.playerCountry="Americans";s.config.playerSlots.clear();
        RuntimeConfig::PlayerSlot player;player.enabled=player.local=player.human=true;player.country="Americans";
        player.startPosition=1;player.color="Gold";s.config.playerSlots.push_back(player);
        player.local=player.human=false;player.country="Russians";player.startPosition=2;player.color="Red";s.config.playerSlots.push_back(player);
        s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;s.world=assets->world;
        s.world.accessObjects().clear();s.report=assets->report;s.mapFacts=assets->mapFacts;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeSkirmishStart(s);
        ensureRuntimeRepairDepotsInitialized(s);s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=instant;
        const auto building=[&](const char* name,RuntimeCellPoint cell){auto* o=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,name,cell);
            if(o)o->buildAnimTicksRemaining=0;return o?o->instanceId:-1;};
        if(building("GAPOWR",{40,72})<0||building("GAAIRC",{44,76})<0)return fail("airport setup");
        std::vector<int> planes;
        for(int n=0;n<count;++n){int oldMax=-1;for(const auto& object:s.world.getObjects())oldMax=std::max(oldMax,object.instanceId);
            RuntimeBuildCatalogItem item;item.kind=RuntimeBuildKind::Unit;item.typeId=type;item.requiredFactory="AircraftType";
            if(!completeRuntimeProductionOrder(s,item))return fail("real production");
            for(const auto& object:s.world.getObjects())if(object.instanceId>oldMax&&object.type==type)planes.push_back(object.instanceId);}
        if(planes.size()!=static_cast<std::size_t>(count))return fail("produced plane count");
        auto* tank=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{50,70});if(!tank)return fail("selection alternative");const int tankId=tank->instanceId;
        initializeRuntimeOfflineGameplayRouting(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);
        applySelection(s,planes,false,false);std::vector<int> expected=planes;
        const auto check=[&](){++selectionTicks;
            if(s.selectedObjectIds==expected)return true;
            std::cout<<"Selection lost tick="<<s.tick<<" actual="<<s.selectedObjectIds.size()<<" expected="<<expected.size();
            for(int id:planes){const auto* p=findObjectById(s,id);if(p)std::cout<<" #"<<id<<':'<<p->mission<<':'<<p->airborne;}
            std::cout<<'\n';return false;};
        const auto step=[&](){++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);return check();};
        const bool pictured=output&&std::string(type)=="BEAG"&&hz==30&&count==4&&!instant;
        const auto capture=[&](const std::string& name){if(!pictured)return true;const auto* p=findObjectById(s,planes.front());
            if(!p)return false;s.mouseClientPointValid=false;const auto path=std::filesystem::path(output)/(name+".bmp");
            const auto result=drawRuntimeStateOffscreenAtCell(s,{p->x,p->y},800,600,error,path.string(),false,true,-1,nullptr,true);
            if(!result||!check())return false;++frames;return true;};
        phase="initial selection";if(!check()||!capture("selected-at-airport"))return fail("initial frame/selection");
        for(int sortie=0;sortie<3;++sortie){
            phase=sortie==0?"move and return":sortie==1?"attack and reload":"user changes selection";
            const WorldObjectInstance* target=nullptr;int targetId=-1;
            if(sortie==1){auto* t=addRuntimeObjectInstanceForOwner(s,runtimeSkirmishOwnerForSlot(1),RuntimeBuildKind::Building,"NACNST",{90,92},std::nullopt,false);
                if(!t)return fail("attack target");t->buildAnimTicksRemaining=0;targetId=t->instanceId;target=t;}
            if(!dispatchRuntimeTargetOrderFromUi(s,sortie==1?RuntimeMultiplayerGameplayTargetOrderKind::Primary:RuntimeMultiplayerGameplayTargetOrderKind::Move,
                target?RuntimeCellPoint{target->x,target->y}:RuntimeCellPoint{105,93},target)||s.lastCommand.acceptedCount!=count||!check())return fail("outbound command or selection");
            bool departed=false,landed=false,returnFrame=false;
            for(int n=0;n<hz*50;++n){if(!step())return fail("automatic flight transition changed selection");
                bool allGround=true;for(int id:planes){const auto* p=findObjectById(s,id);if(!p)return fail("plane lost");
                    departed=departed||p->airborne;allGround=allGround&&!p->airborne&&p->mission=="Dock";}
                if(n==hz/2){if(!capture(sortie==0?"selected-outbound":sortie==1?"selected-attacking":"before-user-change"))return fail("outbound frame");
                    if(sortie==2){if(count==1){cancelRuntimeInteraction(s);expected.clear();++cancellations;}
                        else{applySelection(s,{tankId},false,false);expected={tankId};++switches;}
                        if(!check())return fail("explicit user selection not applied");}}
                if(sortie<2&&!returnFrame){const auto b=s.behaviors.find(planes.front());
                    if(b!=s.behaviors.end()&&b->second.pendingAllowedOccupiedObjectId>=0){
                        if(!capture(sortie==0?"selected-returning":"selected-empty-return"))return fail("return frame");returnFrame=true;}}
                if(departed&&allGround){landed=true;break;}}
            if(!landed)return fail("no completed sortie");++sorties;
            if(sortie==1){bool ready=false;
                const int reloadBudget=static_cast<int>(std::ceil(s.rules.getSpecialMechanismRules().reloadRateMinutes*60*hz))*count+hz*2;
                for(int n=0;n<reloadBudget;++n){
                    if(!step())return fail("reload changed selection");ready=true;
                    for(int id:planes)ready=ready&&findObjectById(s,id)->currentAmmo==1;
                    if(ready)break;}
                if(!ready)return fail("magazine did not refill");++reloads;}
            if(!capture(sortie==0?"selected-landed":sortie==1?"selected-reloaded":"user-choice-after-return"))return fail("landing frame");
            if(sortie==1&&targetId>=0){RuntimeObjectExitRequest exit;exit.objectId=targetId;exit.kind=RuntimeObjectExitKind::ScriptRemoved;
                queueRuntimeObjectExit(s,exit);drainRuntimeObjectExitQueue(s);if(!check())return fail("target exit changed aircraft selection");}
        }
        // Explicit user cancellation/selection must remain in force. A real
        // plane death must still remove only that actor from the selection.
        applySelection(s,planes,false,false);expected=planes;
        RuntimeObjectExitRequest exit;exit.objectId=planes.front();exit.kind=RuntimeObjectExitKind::Destroyed;exit.suppressCrew=true;
        if(queueRuntimeObjectExit(s,exit)!=RuntimeObjectExitQueueResult::Queued||!drainRuntimeObjectExitQueue(s))return fail("death setup");
        expected.erase(expected.begin());if(!check())return fail("dead aircraft stayed selected or erased surviving selections");++deaths;
        ++scenes;std::cout<<"Aircraft selection: "<<scene<<" movement/attack/return/reload/user-choice/death=ok\n";
    }
    std::cout<<"Aircraft selection: passed scenes="<<scenes<<" sorties="<<sorties<<" selectionTicks="<<selectionTicks
        <<" reloads="<<reloads<<" cancellations="<<cancellations<<" switches="<<switches<<" deaths="<<deaths<<" nativeFrames="<<frames<<'\n';return true;
}
