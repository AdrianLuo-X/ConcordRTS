bool runRuntimeMcvOpeningSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail=[&](const std::string& why){error="MCV opening: "+why;return false;};
    const char* output=std::getenv("CONCORD_PRISM_PRESENTATION_OUTPUT");
    const std::string root=output?output:"";
    std::cout << std::unitbuf;
    int cases=0, saves=0, cursorChecks=0, selectedReloads=0, blockedStarts=0;
    const auto checkOpeningUi=[&](RuntimeWindowState& s,const WorldObjectInstance& base) {
        if(!s.selectedObjectIds.empty())return fail("opening auto-selected MCV #"+std::to_string(base.instanceId));
        const RECT client{0,0,960,640};
        centerRuntimeCameraOnCell(s,client,runtimeCellPointForObject(base));
        for(const auto cell:{runtimeCellPointForObject(base),RuntimeCellPoint{base.x+5,base.y}}) {
            const auto projected=runtimeProjectCellXY(cell.x,cell.y,base.level,s.camera);
            s.mouseClientPoint=runtimeScreenPoint(projected,runtimeMapViewport(client),s.camera);s.mouseClientPointValid=true;
            const auto cursor=runtimeMouseCursorFrameForPoint(s,client,s.mouseClientPoint);
            if(!cursor||cursor->frameIndex!=0||cursor->offsetX!=0||cursor->offsetY!=0)
                return fail("fresh game cursor is not the original default arrow");
            ++cursorChecks;
        }
        return true;
    };
    for(const char* country:{"Americans","Russians","YuriCountry"})
        for(bool computer:{false,true})for(bool repacks:{false,true}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        RuntimeSnapshotActiveStateScope active(s);
        s.config=config;s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.multiplayerActive=false;s.config.computerAiEnabled=true;s.config.fixedTickHz=30;
        s.config.mcvRepacks=repacks;s.config.shortGame=true;s.config.crates=false;
        s.config.startingUnits=repacks?6:0;s.config.startingCredits=10000;
        s.config.playerCountry=country;s.config.playerSlots.assign(2,{});
        for(int i=0;i<2;++i) {
            auto& slot=s.config.playerSlots[i];slot.enabled=true;slot.human=i==0||!computer;
            slot.local=i==0;slot.country=country;slot.team="-";slot.startPosition=i+1;
        }
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        s.rulesLoaded=s.report.rulesLoaded;s.artLoaded=s.report.artLoaded;s.theaterLoaded=s.report.theaterLoaded;
        if(!s.report.success)return fail("original map/rules");
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);
        if(!initializeRuntimeSkirmishStart(s)||!initializeRuntimeTriggers(s))return fail(s.economy.skirmishStatus);
        s.visionEnabled=true;updateRuntimeVisionState(s);
        const std::string yard=runtimeConstructionYardForCountry(country);
        const auto* building=s.rules.findBuildingType(yard);
        const int id=s.economy.skirmishStartObjectId;
        const auto* base=findObjectById(s,id);
        if(!building||!base||base->kind!=WorldObjectKind::Unit||base->type!=building->undeploysInto
            ||base->deployed||base->mission!="Guard"
            ||runtimeOwnerHasConstructionYard(s,s.economy.owner)||!runtimeObjectCanMove(s,*base))
            return fail(std::string(country)+" did not start with a movable MCV");
        if(!checkOpeningUi(s,*base))return false;
        const auto start=runtimeCellPointForObject(*base);
        const int credits=s.economy.credits;
        const char* power=std::string(country)=="Americans"?"GAPOWR":std::string(country)=="Russians"?"NAPOWR":"YAPOWR";
        const auto* item=findRuntimeGameplayCatalogItem(s,s.economy.owner,power);
        if(!item||evaluateRuntimeTechTreeDecision(s,*item).canQueue)return fail("building unlocked before deployment");
        const std::string opponent=runtimeSkirmishOwnerForSlot(1);
        int otherMcv=-1;
        for(const auto& object:s.world.getObjects())if(object.alive&&object.owner==opponent&&object.type==building->undeploysInto)
            otherMcv=object.instanceId;
        if(otherMcv<0||runtimeOwnerHasConstructionYard(s,opponent))return fail("opponent did not start mobile");
        for(int tick=0;tick<600;++tick){++s.tick;advanceRuntimeSimulation(s,false);}
        base=findObjectById(s,id);
        if(!base||base->type!=building->undeploysInto||base->mission!="Guard"
            ||runtimeCellPointForObject(*base)!=start||runtimeOwnerHasConstructionYard(s,s.economy.owner)
            ||s.economy.credits!=credits||!s.selectedObjectIds.empty()||s.skirmishMatchFinished)
            return fail(std::string(country)+" human MCV moved/deployed or lost after 20 seconds");
        if(computer) {
            if(!runtimeOwnerHasConstructionYard(s,opponent))return fail("computer did not deploy its own MCV");
        } else if(!findObjectById(s,otherMcv)||runtimeOwnerHasConstructionYard(s,opponent))
            return fail("remote human was controlled by AI");
        std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored,s,false);
        if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())
            ||!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("mobile save/load: "+error);
        const auto* resumed=findObjectById(*restored,id);
        if(!resumed||resumed->type!=base->type||runtimeCellPointForObject(*resumed)!=start
            ||runtimeOwnerHasConstructionYard(*restored,restored->economy.owner)
            ||!restored->selectedObjectIds.empty())return fail("load auto-deployed or selected MCV");
        ++saves;
        if(computer&&!repacks&&!root.empty()) {
            const auto stats=drawRuntimeStateOffscreenAtCell(s,start,800,600,error,root+"/"+country+"-waiting.bmp",false);
            if(!stats)return fail("opening render: "+error);
        }
        if(dispatchRuntimeSecondaryActionForSelectionFromUi(s))return fail("D deployed without a selection");
        applySelection(s,{id},false,false);
        if(s.selectedObjectIds!=std::vector<int>{id})return fail("manual selection rejected");
        if(!computer&&!repacks) {
            std::ostringstream selectedSave;IniFile selectedIni;
            auto selectedRestore=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*selectedRestore,s,false);
            if(!writeRuntimeStateToStream(s,selectedSave,error)||!selectedIni.loadFromString(selectedSave.str())
                ||!materializeRuntimeLoadStateFromIni(*selectedRestore,selectedIni,error)
                ||!selectedRestore->selectedObjectIds.empty())return fail("ordinary reload unexpectedly retained selection: "+error);
            ++selectedReloads;
        }
        const auto deploy=[&](){return computer?dispatchRuntimeSecondaryActionForSelectionFromUi(s)
            :dispatchRuntimeSecondaryActionFromUi(s,id);};
        const auto yardItem=runtimeBuildItemForBuilding(s,yard);
        if(!yardItem)return fail("missing opening yard catalog entry");
        auto deploymentPosition=start;
        const bool currentFootprintFree=runtimeCellCanPlaceBuilding(s,*yardItem,
            runtimePlacementTopLeftForAnchor(start,runtimeStructureFootprintForBuildItem(s,*yardItem)),
            id,nullptr,s.economy.owner);
        bool accepted=deploy();
        if(!currentFootprintFree) {
            // Starting troops can occupy the yard footprint. A player must move
            // the MCV explicitly; deployment must not silently choose a site.
            base=findObjectById(s,id);
            if(!accepted||!base||runtimeCellPointForObject(*base)!=start
                ||runtimeOwnerHasConstructionYard(s,s.economy.owner)
                ||s.selectedObjectIds!=std::vector<int>{id}||s.economy.credits!=credits)
                return fail("blocked opening footprint did not retain the selected MCV");
            const auto open=findRuntimeMcvDeployCell(s,*base,*yardItem);
            if(!open||*open==start)return fail("no nearby open ground for player move");
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,*open)
                ||s.lastCommand.acceptedCount!=1)return fail("opening player move rejected");
            bool arrived=false;
            for(int n=0;n<900;++n){++s.tick;advanceRuntimeSimulation(s,false);base=findObjectById(s,id);
                if(!base)return fail("opening move automatically deployed the MCV");
                if(runtimeCellPointForObject(*base)==*open&&!base->renderMoveActive){arrived=true;break;}}
            if(!arrived)return fail("opening MCV did not reach player-selected open ground");
            deploymentPosition=*open;accepted=deploy();++blockedStarts;
            std::cout<<"MCV opening "<<country<<" troops="<<s.config.startingUnits
                <<" blockedStart="<<start.x<<","<<start.y<<" manualDestination="<<open->x<<","<<open->y<<"\n";
        }
        if(accepted)for(int tick=0;tick<s.config.fixedTickHz*6&&findObjectById(s,id);++tick){
            RuntimeSnapshotActiveStateScope scope(s);++s.tick;advanceRuntimeSimulation(s,false);
        }
        if(!accepted||findObjectById(s,id)||s.selectedObjectIds.size()!=1)return fail("player deploy command rejected");
        const auto* deployed=findObjectById(s,s.selectedObjectIds.front());
        if(!deployed||deployed->kind!=WorldObjectKind::Structure||deployed->type!=yard
            ||deployed->owner!=s.economy.owner||runtimePlacementAnchorCellForFootprint(runtimeCellPointForObject(*deployed),
                runtimeStructureFootprintForObject(s,*deployed))!=deploymentPosition
            ||s.economy.credits!=credits)return fail("wrong deployed yard or charged credits");
        for(int tick=0;tick<240;++tick){++s.tick;advanceRuntimeBuildAnimations(s);}
        item=findRuntimeGameplayCatalogItem(s,s.economy.owner,power);
        if(!item||!evaluateRuntimeTechTreeDecision(s,*item).canQueue)return fail("manual deployment did not unlock construction");
        std::cout<<"MCV opening "<<country<<" opponent="<<(computer?"computer":"human")
            <<" repacks="<<repacks<<" wait/save/click-or-D/deploy/technology=ok\n";
        ++cases;
    }
    // The launch path without lobby slots must use the same mobile opening.
    for(const char* country:{"Americans","Russians","YuriCountry"}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=config;s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();s.config.playerSlots.clear();
        s.config.playerCountry=country;s.config.startingUnits=0;s.config.computerAiEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        s.rulesLoaded=s.report.rulesLoaded;s.artLoaded=s.report.artLoaded;s.theaterLoaded=s.report.theaterLoaded;
        if(!s.report.success)return fail("legacy launch resources");
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);
        if(!initializeRuntimeSkirmishStart(s))return fail("legacy launch");
        const auto* base=findObjectById(s,s.economy.skirmishStartObjectId);
        const auto* unit=base?findRuntimeUnitType(s.rules,*base):nullptr;
        if(!base||base->kind!=WorldObjectKind::Unit||!unit||!runtimeUnitTypeIsMcv(s,*unit)
            ||runtimeOwnerHasConstructionYard(s,s.economy.owner))return fail("legacy launch auto-deployed");
        if(!checkOpeningUi(s,*base))return false;
        ++cases;
    }
    std::cout<<"MCV opening: "<<cases<<" three-faction/AI/remote-human/repacks/no-lobby cases; "
        <<saves<<" undeployed reloads, 20-second waits and manual deployment passed\n";
    std::cout<<"Unselected opening: defaultCursorChecks="<<cursorChecks<<" selectedSaveReloads="
        <<selectedReloads<<" empty-selection D rejection and manual deployment PASS\n";
    std::cout<<"Opening blocked footprints: explicitPlayerMoves="<<blockedStarts<<" PASS\n";
    return true;
}
