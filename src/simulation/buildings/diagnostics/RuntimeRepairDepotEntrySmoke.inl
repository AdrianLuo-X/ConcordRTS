bool runRuntimeRepairDepotEntrySmoke(RuntimeWindowState& assets, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& why){error="Repair entry: "+why;return false;};
    const char* output=std::getenv("CONCORD_REPAIR_ENTRY_OUTPUT");
    const bool diagnostic=std::getenv("CONCORD_REPAIR_ENTRY_DIAGNOSTIC")!=nullptr;
    if(output) std::filesystem::create_directories(output);
    int cases=0,saves=0,paired=0;
    for(const std::string depot:{"GADEPT","NADEPT"}) for(int hz:{15,30,60})
    for(bool moving:{false,true}) {
        const std::string label=depot+"-"+std::to_string(hz)+(moving?"-moving":"-idle");
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,assets,false);s.config.fixedTickHz=hz;s.config.noCooldown=false;
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.shortGame=false;
        s.config.playerSlots.clear();s.config.playerCountry=depot=="GADEPT"?"Americans":"Russians";
        s.economy.owner=s.config.playerCountry;s.economy.credits=10000;s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        s.runtimePreferencePersistenceEnabled=s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear();s.terrainInfoByCell.clear();s.terrainByCell.clear();
        s.mapFacts={};s.mapFactsLoaded=false;
        for(int y=48;y<=84;++y) for(int x=48;x<=84;++x) {
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        auto* host=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,depot,{64,64});
        if(!host)return fail(label+" host missing");const int hostId=host->instanceId;
        host->buildAnimTicksTotal=host->buildAnimTicksRemaining=0;
        auto* power=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,depot=="GADEPT"?"GAPOWR":"NAPOWR",{79,79});
        if(!power)return fail(label+" power missing");power->buildAnimTicksTotal=power->buildAnimTicksRemaining=0;
        auto* tank=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{60,66});
        if(!tank)return fail(label+" tank missing");const int id=tank->instanceId;
        tank->health=std::max(1,runtimeMaxHealthForObject(s,*tank)-64);tank->facing=64;
        const int initialHealth=tank->health;
        initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);rebuildRuntimeEconomyPower(s);
        ensureRuntimeRepairDepotsInitialized(s);RuntimeSnapshotActiveStateScope scope(s);s.selectedObjectIds={id};
        const auto position=[&](const RuntimeWindowState& state){
            const auto* p=findObjectById(state,id);
            return std::array<double,2>{p->hasRenderPosition?p->renderX:p->x,p->hasRenderPosition?p->renderY:p->y};
        };
        const auto step=[](RuntimeWindowState& state){
            RuntimeSnapshotActiveStateScope active(state);++state.tick;advanceRuntimeSimulation(state,false);
        };
        if(moving) {
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{70,68}))
                return fail(label+" initial move rejected");
            for(int i=0;i<hz*10;++i) {
                step(s);tank=findMutableObjectById(s,id);
                const auto it=s.behaviors.find(id);
                if(tank->renderMoveActive&&it!=s.behaviors.end()
                    &&it->second.renderMoveTicksRemaining>1
                    &&it->second.renderMoveTicksRemaining<it->second.renderMoveTicksTotal)break;
            }
            if(!findObjectById(s,id)->renderMoveActive)return fail(label+" initial move never started");
        }
        const auto beforeCommand=position(s);
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            {64,64},findObjectById(s,hostId))||s.lastCommand.verb!="unitRepair"||s.lastCommand.acceptedCount!=1)
            return fail(label+" repair command rejected");
        const auto afterCommand=position(s);
        const double commandJump=std::hypot(afterCommand[0]-beforeCommand[0],afterCommand[1]-beforeCommand[1]);
        // Short segments round their fractional timing to whole ticks. Allow
        // that existing quantization, then check the docking boundary itself.
        const double maxAllowed=1.25/std::max(1.0,std::floor(runtimeMovementBaseIntervalTicks(s,*findObjectById(s,id))))+.001;
        double largestStep=0,previousStep=0,dockStep=0,preDockStep=0;
        int largestTick=-1;bool docked=false,captured=false;
        std::ofstream csv;if(output){csv.open(std::filesystem::path(output)/(label+".csv"));csv<<"tick,x,y,step,cellX,cellY,docked\n";}
        auto clone=std::unique_ptr<RuntimeWindowState>{};
        for(int n=0;n<hz*20;++n) {
            const auto before=position(s);step(s);const auto after=position(s);tank=findMutableObjectById(s,id);
            const double delta=std::hypot(after[0]-before[0],after[1]-before[1]);
            if(delta>largestStep){largestStep=delta;largestTick=s.tick;}
            if(csv)csv<<s.tick<<','<<after[0]<<','<<after[1]<<','<<delta<<','<<tank->x<<','<<tank->y<<','<<tank->dockedToObjectId<<'\n';
            if(clone) {
                step(*clone);++paired;
                const auto restored=position(*clone);const auto* other=findObjectById(*clone,id);
                if(std::hypot(restored[0]-after[0],restored[1]-after[1])>.000001
                    ||tank->dockedToObjectId!=other->dockedToObjectId||tank->health!=other->health)
                    return fail(label+" saved approach diverged at "+std::to_string(s.tick));
            }
            const auto* relation=findRuntimeRepairDepotRelationForUnit(s.repairDepots,id);
            if(!clone&&relation&&relation->phase==RuntimeRepairDepotRelationPhase::Reserved
                &&tank->renderMoveActive&&tank->x==relation->dockCell.x&&tank->y==relation->dockCell.y
                &&s.behaviors.at(id).renderMoveTicksRemaining<s.behaviors.at(id).renderMoveTicksTotal) {
                std::ostringstream saved;IniFile ini;clone=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*clone,s,false);
                if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())
                    ||!materializeRuntimeLoadStateFromIni(*clone,ini,error))return fail(label+" approach save: "+error);
                ++saves;
            }
            if(output&&hz==30&&!moving&&((!captured&&relation&&tank->x==relation->dockCell.x)||tank->dockedToObjectId==hostId)) {
                const auto stats=drawRuntimeStateOffscreenAtCell(s,{65,65},800,600,error,
                    std::string(output)+"/"+label+(tank->dockedToObjectId==hostId?"-docked.bmp":"-entry.bmp"),false);
                if(!stats||!stats->rendered||stats->visibleVoxelBodies<1)return fail(label+" real vehicle frame missing");
                captured=true;
            }
            if(tank->dockedToObjectId==hostId){docked=true;dockStep=delta;preDockStep=previousStep;break;}
            previousStep=delta;
        }
        std::cout<<"Repair entry "<<label<<" commandJump="<<commandJump<<" maxStep="<<largestStep
            <<" limit="<<maxAllowed<<" tick="<<largestTick<<" docked="<<docked
            <<" dockStep="<<dockStep<<" previous="<<preDockStep<<'\n';
        if(!docked||!clone)return fail(label+" approach did not finish/save");
        if(!diagnostic&&(commandJump>.000001||largestStep>maxAllowed||dockStep>preDockStep+.000001))
            return fail(label+" visible body jumped or accelerated at attachment");
        for(int n=0;n<hz*6&&findObjectById(s,id)->health==initialHealth;++n)step(s);
        if(findObjectById(s,id)->health<=initialHealth)return fail(label+" repair never started");
        ++cases;
    }
    std::cout<<"Repair entry "<<(diagnostic?"diagnostic":"passed")<<": "<<cases<<" real idle/moving orders, "<<saves<<" approach saves, "
        <<paired<<" continued ticks; continuous position and actual repair=ok\n";
    return true;
}
