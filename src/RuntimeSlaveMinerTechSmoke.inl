bool runRuntimeSlaveMinerTechSmoke(RuntimeWindowState& live,std::string& error) {
    const auto fail=[&](const std::string& why){error="slave miner technology: "+why;return false;};
    int cases=0, phases=0, saves=0;
    for(int hz:{15,30,60})for(bool repacks:{false,true}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        RuntimeSnapshotActiveStateScope active(s);
        s.config=live.config;s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=false;s.config.multiplayerActive=false;s.config.fixedTickHz=hz;
        s.config.mcvRepacks=repacks;s.config.startingCredits=100000;s.config.startingUnits=0;
        s.config.noCooldown=false;s.config.crates=false;s.config.playerCountry="YuriCountry";
        s.config.playerSlots.assign(2,{});
        for(int i=0;i<2;++i){auto& slot=s.config.playerSlots[i];slot.enabled=true;slot.human=slot.local=i==0;
            slot.country=i==0?"YuriCountry":"Russians";slot.team="-";slot.startPosition=i+1;}
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        s.rulesLoaded=s.report.rulesLoaded;s.artLoaded=s.report.artLoaded;s.theaterLoaded=s.report.theaterLoaded;
        if(!s.report.success)return fail("native resources");
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);
        if(!initializeRuntimeSkirmishStart(s)||!initializeRuntimeTriggers(s)
            ||!dispatchRuntimeSecondaryActionFromUi(s,s.economy.skirmishStartObjectId))return fail("Yuri opening");
        const std::string owner=s.economy.owner, enemy=runtimeSkirmishOwnerForSlot(1);
        const auto finishAnimations=[&](){for(int i=0;i<hz*15;++i){++s.tick;advanceRuntimeBuildAnimations(s);}};
        finishAnimations();
        const auto completeAndPlace=[&](const RuntimeBuildCatalogItem& item)->int {
            if(!completeRuntimeActiveProductionForProbe(s,item))return -1;
            ++s.tick;advanceRuntimeProductionEconomy(s);
            const auto ready=runtimeOldestReadyPlacement(s);
            if(!ready||!ready->item||ready->item->typeId!=item.typeId||!beginRuntimePlacementFromUi(s,*ready,true))return -1;
            const auto cell=findRuntimePlacementCellNearBase(s,item);
            if(!cell||!placeRuntimeReadyBuilding(s,*cell))return -1;
            finishAnimations();int id=-1;
            for(const auto& object:s.world.getObjects())if(object.alive&&object.owner==owner&&object.type==item.typeId)
                id=std::max(id,object.instanceId);
            return id;
        };
        const auto buy=[&](const char* id)->int {
            const auto* found=findRuntimeGameplayCatalogItem(s,owner,id);
            if(!found)return -1;const auto item=*found;const int before=s.economy.credits;
            if(!dispatchRuntimeQueueProductionFromUi(s,item))return -1;
            ++s.tick;advanceRuntimeProductionEconomy(s);
            const int result=completeAndPlace(item);
            return before-s.economy.credits==item.cost?result:-1;
        };
        if(buy("YAPOWR")<0||buy("YABRCK")<0)return fail("paid opening prerequisites");
        const auto check=[&](bool expected,const std::string& phase){
            if(runtimePrerequisiteTokenMet(s,"PROC")!=expected
                ||runtimeComputerAiPrerequisiteTokenMet(s,owner,"PROC")!=expected)
                return fail(phase+" player/computer PROC mismatch");
            for(const char* id:{"YAWEAP","NAPSIS"}){
                const auto* item=findRuntimeGameplayCatalogItem(s,owner,id);
                if(!item||evaluateRuntimeTechTreeDecision(s,*item).canQueue!=expected
                    ||runtimeBuildItemUnlockedForSidebar(s,*item)!=expected)
                    return fail(phase+" sidebar/production mismatch: "+id);
            }
            ++phases;return true;
        };
        const auto roundTrip=[&](bool expected){
            std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*restored,s,false);
            if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())
                ||!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("save/load: "+error);
            if(runtimePrerequisiteTokenMet(*restored,"PROC")!=expected)return fail("save changed alternate refinery eligibility");
            ++saves;return true;
        };
        const auto transform=[&](int id,const char* expected)->int {
            s.selectedObjectIds={id};
            if(!dispatchRuntimeSecondaryActionFromUi(s,id)||s.selectedObjectIds.size()!=1)return -1;
            const auto* object=findObjectById(s,s.selectedObjectIds.front());
            if(!object||object->type!=expected)return -1;
            const int next=object->instanceId;finishAnimations();return next;
        };
        const auto destroy=[&](int id){
            RuntimeObjectExitRequest request;request.objectId=id;request.kind=RuntimeObjectExitKind::Destroyed;
            request.tick=++s.tick;request.suppressCrew=true;request.suppressDeathWeapon=true;
            return finalizeRuntimeObjectExit(s,request).accepted;
        };
        if(!check(false,"before purchase"))return false;
        int refinery=buy("YAREFN");
        if(refinery<0||!check(true,"bought deployed refinery"))return fail("paid native refinery purchase: "+error);
        const auto* radar=findRuntimeGameplayCatalogItem(s,owner,"NAPSIS");
        if(!radar)return fail("native radar");const auto radarItem=*radar;
        if(!dispatchRuntimeQueueProductionFromUi(s,radarItem))return fail("radar order");
        ++s.tick;advanceRuntimeProductionEconomy(s);
        int mobile=transform(refinery,"SMIN");
        if(mobile<0||!check(true,"last refinery packed with active production")
            ||runtimePrerequisiteTokenMet(s,"YAREFN"))return fail("mobile form: "+error);
        if(completeAndPlace(radarItem)<0||!roundTrip(true))return fail("production/load while packed: "+error);
        const auto* miner=findObjectById(s,mobile);
        const auto move=findRuntimePassableCellNear(s,*miner,{miner->x+4,miner->y+2},8);
        if(!move)return fail("move destination");
        s.selectedObjectIds={mobile};issueRuntimeMoveCommand(s,*move);
        if(s.lastCommand.acceptedCount!=1||!check(true,"mobile move order"))return fail("moving technology: "+error);
        if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop))return fail("stop miner");
        refinery=transform(mobile,"YAREFN");
        if(refinery<0||!check(true,"redeployed refinery"))return fail("redeploy: "+error);
        mobile=transform(refinery,"SMIN");
        if(mobile<0||!check(true,"repacked refinery")||!roundTrip(true))return fail("repack: "+error);
        const int second=buy("YAREFN");
        if(second<0||!check(true,"one mobile plus one building")||!destroy(second)
            ||!check(true,"one of two destroyed"))return fail("multiple refinery ownership: "+error);
        findMutableObjectById(s,mobile)->owner=enemy;
        if(!check(false,"last mobile belongs to enemy")||!runtimeComputerAiPrerequisiteTokenMet(s,enemy,"PROC"))return fail("ownership transfer: "+error);
        findMutableObjectById(s,mobile)->owner=owner;
        if(!check(true,"mobile returned")||!destroy(mobile)||!check(false,"all refineries destroyed")||!roundTrip(false))return fail("last refinery loss: "+error);
        ++cases;
        std::cout<<"Slave refinery technology hz="<<hz<<" repacks="<<repacks
            <<" paid purchase/pack/move/redeploy/active-production/multiple/ownership/loss/load=ok\n";
    }
    // Read the authored alternate type rather than hard-coding SMIN or
    // treating every harvester as an economic prerequisite.
    auto custom=std::make_unique<RuntimeWindowState>();custom->economy.owner="Player";
    WorldObjectInstance object;object.instanceId=1;object.kind=WorldObjectKind::Unit;
    object.owner="Player";object.alive=true;object.health=100;
    for(const char* alternate:{"CMIN","<none>"}) {
        IniFile rules,overlay;
        if(!rules.loadFromFile(live.report.rulesPath)||!overlay.loadFromString(std::string("[General]\nPrerequisiteProcAlternate=")+alternate+"\n"))
            return fail("alternate override input");
        rules.appendFrom(overlay);if(!custom->rules.loadFromIni(rules))return fail("alternate override rules");
        for(const char* type:{"CMIN","SMIN"}){
            object.type=type;object.alive=true;object.health=100;object.owner="Player";
            object.dockedToObjectId=999;object.cloaked=true;custom->world.accessObjects()={object};
            const bool expected=std::string(alternate)==type;
            if(runtimePrerequisiteTokenMet(*custom,"PROC")!=expected)return fail("authored alternate or transported/cloaked owner");
            custom->world.accessObjects()[0].health=0;
            if(runtimePrerequisiteTokenMet(*custom,"PROC"))return fail("zero-health alternate counted");
        }
    }
    std::cout<<"Slave miner technology: "<<cases<<" stock scenarios, "<<phases<<" sidebar/queue/AI phases, "
        <<saves<<" full reloads and authored alternate overrides passed\n";
    return true;
}
