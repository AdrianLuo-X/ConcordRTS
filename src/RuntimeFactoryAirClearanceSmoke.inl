bool runRuntimeFactoryAirClearanceSmoke(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;std::string scene;
    const auto fail=[&](const std::string& why){error="factory air clearance "+scene+": "+why;return false;};
    IniFile native;RulesDatabase rules;
    if(!native.loadFromFile(live.report.rulesPath)||!rules.loadFromIni(native))return fail("native rules");
    const char* output=std::getenv("CONCORD_FACTORY_AIR_CLEARANCE_OUTPUT");
    const std::string root=output?output:"build/smoke/feature-set-14/37-factory-air-clearance/checks";
    std::filesystem::create_directories(root);
    int scenes=0,productions=0,saves=0,animationFrames=0,crowds=0;
    const auto step=[&](RuntimeWindowState& s) {
        ++s.tick;s.actionSummary.repathsThisTick=0;s.actionSummary.localAvoidanceAttemptsThisTick=0;
        s.actionSummary.localAvoidanceMovesThisTick=0;
        processRuntimePathRequests(s);prepareRuntimeInfantryStepReservations(s);advanceRuntimeJumpjetFlights(s);
        advanceRuntimeFactoryExitAnimations(s);
        std::vector<int> ids;for(const auto& [id,move]:s.behaviors)if(move.kind==RuntimeBehaviorKind::Move)ids.push_back(id);
        std::sort(ids.begin(),ids.end());
        for(int id:ids){
            auto* object=findMutableObjectById(s,id);auto move=s.behaviors.find(id);
            if(!object||move==s.behaviors.end())continue;
            const auto before=runtimeCellPointForObject(*object);
            advanceRuntimeMoveBehavior(s,*object,move->second);
            if(std::abs(object->x-before.x)>1||std::abs(object->y-before.y)>1)return false;
            if(object->renderMoveActive)++animationFrames;
        }
        advanceRuntimeFactoryExitAnimations(s);advanceRuntimeFactoryDeliveries(s);return true;
    };
    const auto moving=[&](const RuntimeWindowState& s,int id){
        const auto* object=findObjectById(s,id);const auto move=s.behaviors.find(id);
        return object&&(object->renderMoveActive||object->factoryExitHostId>=0
            ||(move!=s.behaviors.end()&&move->second.state!=RuntimeBehaviorState::Complete));
    };
    for(const std::string factory:{"GAWEAP","NAWEAP","YAWEAP"})for(int hz:{15,30,60})
        for(int producer=0;producer<3;++producer)for(bool airProduct:{false,true}){
        scene=factory+"-"+std::to_string(hz)+"-"+std::to_string(producer)+"-"+std::to_string(airProduct);
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=live.config;s.report=live.report;s.world=live.world;s.art=live.art;s.theater=live.theater;
        s.config.fixedTickHz=hz;s.config.computerAiEnabled=false;s.config.multiplayerActive=false;s.config.playerSlots.clear();
        s.rules=rules;s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.economy.owner="ProbeA";s.economy.credits=100000;s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        initializeRuntimeLocalProductionLanes(s.economy);s.economy.localProductionLanes.nextSerial=3;
        s.factoryDeliveries.reset();s.behaviors.clear();s.jumpjetFlights.clear();
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear();s.terrainInfoByCell.clear();
        for(int y=45;y<=85;++y)for(int x=45;x<=85;++x){
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=c.subTileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        WorldObjectInstance building;building.instanceId=1;building.kind=WorldObjectKind::Structure;building.type=factory;
        building.owner=s.economy.owner;building.x=building.y=64;building.cell=runtimeCellKey(64,64);
        building.alive=true;building.health=1000;building.poweredOperational=true;building.mission="Guard";
        auto power=building;power.instanceId=2;power.type="NANRCT";power.x=54;power.cell=runtimeCellKey(54,64);
        auto blocker=building;blocker.instanceId=10;blocker.kind=WorldObjectKind::Unit;blocker.type=factory=="YAWEAP"?"DISK":"ZEP";
        blocker.x=airProduct?70:69;blocker.y=65;blocker.cell=runtimeCellKey(blocker.x,blocker.y);
        blocker.airborne=true;blocker.flightLevel=blocker.level=15;blocker.currentAmmo=-1;
        s.world.accessObjects()={building,power,blocker};initializeRuntimeNavigation(s);
        s.structureFootprints=buildRuntimeStructureFootprints(s);rebuildRuntimeEconomyPower(s);
        RuntimeSnapshotActiveStateScope active(s);
        RuntimeBuildCatalogItem item;item.kind=RuntimeBuildKind::Unit;item.requiredFactory="UnitType";
        item.typeId=airProduct?blocker.type:factory=="GAWEAP"?"MTNK":factory=="NAWEAP"?"HTNK":"LTNK";
        RuntimeComputerAiState ai;ai.owner=s.economy.owner;
        RuntimeCampaignHouseProductionState house;house.owner=s.economy.owner;RuntimeCampaignTeamBuild team;
        const auto produce=[&](int serial){
            return producer==0?completeRuntimeProductionOrder(s,item,false,RuntimeProductionLane::Vehicles,serial)
                :producer==1?runtimeComputerAiBuildUnit(s,ai,item):completeRuntimeCampaignAiUnit(s,house,item,team).has_value();
        };
        const auto original=runtimeCellPointForObject(*findObjectById(s,10));
        if(produce(1)||s.world.getObjects().size()!=3||!s.behaviors.contains(10)||s.economy.credits!=100000)
            return fail("completed order did not wait for air blocker");
        const auto firstGoal=s.behaviors.at(10).moveTarget;
        if(firstGoal==original)return fail("no real yielding destination");
        if(hz==30&&producer==0&&airProduct){
            const auto frame=drawRuntimeStateOffscreenAtCell(s,{66,65},800,600,error,root+"/"+factory+"-before.bmp",false,false);
            if(!frame||!frame->rendered)return fail("before image: "+error);
        }
        std::unique_ptr<RuntimeWindowState> restored;bool saved=false;
        for(int serial=1;serial<=2;++serial){
            bool built=false;int produced=-1;
            for(int tick=0;tick<hz*120;++tick){
                if(!step(s))return fail("yield or exit teleported");
                if(restored&&moving(*restored,10)){
                    RuntimeSnapshotActiveStateScope restoreScope(*restored);
                    if(!step(*restored))return fail("restored yield teleported");
                }
                if(!saved&&producer==0&&airProduct&&findObjectById(s,10)->renderMoveActive){
                    std::ostringstream text;IniFile ini;restored=std::make_unique<RuntimeWindowState>();
                    seedRuntimeLoadScratch(*restored,s,false);
                    if(!writeRuntimeStateToStream(s,text,error)||!ini.loadFromString(text.str())
                        ||!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("mid-yield reload: "+error);
                    const auto* copy=findObjectById(*restored,10);
                    if(!copy||std::abs(runtimeObjectRenderX(*copy)-runtimeObjectRenderX(*findObjectById(s,10)))>1e-6
                        ||restored->behaviors.at(10).moveTarget!=firstGoal)return fail("saved flight/goal changed");
                    if(!saveRuntimeState(s,root+"/yield-"+scene+".ini",error))return fail("save artifact");
                    saved=true;++saves;
                }
                if(!built&&produce(serial)){
                    built=true;produced=s.world.getObjects().back().instanceId;++productions;
                    if(moving(s,10))return fail("new unit appeared before yielding flight finished");
                }
                if(built&&!moving(s,produced)&&s.factoryDeliveries.leases().empty())break;
            }
            if(!built||moving(s,produced)||!s.factoryDeliveries.leases().empty()){
                std::ostringstream detail;detail<<"production or egress did not resume: serial="<<serial<<" built="<<built
                    <<" produced="<<produced<<" leases="<<s.factoryDeliveries.leases().size()<<" status="<<s.economy.status<<'/'<<s.simulationStatus;
                for(const auto& object:s.world.getObjects())if(object.kind==WorldObjectKind::Unit){
                    detail<<" unit="<<object.instanceId<<'@'<<object.x<<','<<object.y<<" air="<<object.airborne
                        <<" render="<<object.renderMoveActive<<" host="<<object.factoryExitHostId;
                    const auto move=s.behaviors.find(object.instanceId);
                    if(move!=s.behaviors.end())detail<<" goal="<<move->second.moveTarget.x<<','<<move->second.moveTarget.y
                        <<" state="<<static_cast<int>(move->second.state)<<" path="<<move->second.path.size()
                        <<" pending="<<move->second.pathRequestPending<<" blocked="<<move->second.blockedTicks;
                }
                return fail(detail.str());
            }
            if(s.world.getObjects().size()!=static_cast<std::size_t>(3+serial)||s.economy.credits!=100000)
                return fail("duplicate unit/payment");
            if(airProduct){
                std::set<int> parked;
                for(const auto& object:s.world.getObjects())if(object.alive&&object.kind==WorldObjectKind::Unit)
                    if(!parked.insert(runtimeCellKey(object.x,object.y)).second)return fail("aircraft stacked after delivery");
                for (const auto& a : s.world.getObjects()) for (const auto& b : s.world.getObjects())
                    if (a.instanceId < b.instanceId && runtimeObjectOccupiesAirNavigation(a)
                        && runtimeObjectOccupiesAirNavigation(b)
                        && std::abs(a.x-b.x) < 3 && std::abs(a.y-b.y) < 3)
                        return fail("large aircraft overlap in adjacent delivery slots");
            }
        }
        if(restored){
            RuntimeSnapshotActiveStateScope restoreScope(*restored);
            for(int tick=0;tick<hz*60&&moving(*restored,10);++tick)if(!step(*restored))return fail("restored completion");
            if(moving(*restored,10)||runtimeCellPointForObject(*findObjectById(*restored,10))!=firstGoal)
                return fail("restored flight did not reach reserved space");
        }
        if(hz==30&&producer==0&&airProduct){
            const auto frame=drawRuntimeStateOffscreenAtCell(s,{66,65},800,600,error,root+"/"+factory+"-after.bmp",false,false);
            if(!frame||!frame->rendered)return fail("after image: "+error);
        }
        // Hostile aircraft, docked aircraft and missiles never accept the order.
        auto* excluded=findMutableObjectById(s,10);excluded->x=70;excluded->y=65;excluded->cell=runtimeCellKey(70,65);
        eraseRuntimeBehavior(s,10);excluded->owner="ProbeEnemy";clearRuntimeRenderPosition(*excluded);
        (void)requestRuntimeFactoryExitClearance(s,s.economy.owner,{{70,65}},-1,true);
        if(s.behaviors.contains(10))return fail("enemy aircraft was ordered to yield");
        excluded->owner=s.economy.owner;excluded->dockedToObjectId=1;
        (void)requestRuntimeFactoryExitClearance(s,s.economy.owner,{{70,65}},-1,true);
        if(s.behaviors.contains(10))return fail("docked aircraft was ordered to yield");
        excluded->dockedToObjectId=-1;excluded->type="V3ROCKET";
        (void)requestRuntimeFactoryExitClearance(s,s.economy.owner,{{70,65}},-1,true);
        if(s.behaviors.contains(10))return fail("spawned missile was ordered to yield");
        if(hz==30&&producer==0&&airProduct){
            s.behaviors.clear();s.jumpjetFlights.clear();
            for(auto& object:s.world.accessObjects())if(object.kind==WorldObjectKind::Unit){
                object.type=blocker.type;object.owner=s.economy.owner;object.dockedToObjectId=-1;
                object.x=object.instanceId==10?70:50+object.instanceId%10;object.y=object.instanceId==10?65:55;
                object.cell=runtimeCellKey(object.x,object.y);clearRuntimeRenderPosition(object);
            }
            int id=100;for(const auto& cell:runtimeNeighborsOf({70,65})){
                auto neighbor=blocker;neighbor.instanceId=id++;neighbor.x=cell.x;neighbor.y=cell.y;
                neighbor.cell=runtimeCellKey(cell.x,cell.y);s.world.accessObjects().push_back(neighbor);
            }
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            bool clear=false;
            for(int tick=0;tick<hz*60;++tick){
                const bool waiting=requestRuntimeFactoryExitClearance(s,s.economy.owner,{{70,65}},-1,true);
                if(!waiting&&!moving(s,10)&&runtimeCellPointForObject(*findObjectById(s,10))!=RuntimeCellPoint{70,65}){clear=true;break;}
                if(!step(s))return fail("crowded air exit teleported");
            }
            if(!clear)return fail("surrounding flyers never made room");
            ++crowds;
        }
        ++scenes;std::cout<<"Factory air clearance "<<scene<<" PASS\n";
    }
    if(saves!=9||animationFrames==0||crowds!=3)return fail("missing real flight/reload/crowd coverage");
    std::cout<<"Factory air clearance passed: "<<scenes<<" scenes, "<<productions<<" consecutive productions, "
        <<saves<<" full mid-flight reloads, "<<animationFrames<<" moving frames, "<<crowds
        <<" surrounded air exits; three factions, three producers, 15/30/60 Hz\n";
    return true;
}
