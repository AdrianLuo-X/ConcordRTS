bool runRuntimeAirportLossSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    std::string scene;
    const auto fail=[&](const std::string& why){error="Airport loss: "+scene+": "+why;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("original resources");
    const char* output=std::getenv("CONCORD_AIRPORT_LOSS_OUTPUT");
    int cases=0,groundDestroyed=0,airSurvivors=0,otherSurvivors=0,saves=0,paired=0,snapshots=0,duplicates=0,landings=0,frames=0,boundaries=0,formatChecks=0;
    for(const auto* type:{"GAAIRC","AMRADR"})for(int hz:{15,30,60})for(int flying:{0,1,4})for(bool alternate:{false,true}) {
        scene=std::string(type)+" hz="+std::to_string(hz)+" flying="+std::to_string(flying)+" alternate="+std::to_string(alternate);
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=config;s.config.fixedTickHz=hz;s.config.noCooldown=flying==1;
        s.config.computerAiEnabled=false;s.config.shortGame=false;s.config.startingUnits=0;
        s.config.playerCountry="Americans";s.config.playerSlots.clear();RuntimeConfig::PlayerSlot player;
        player.enabled=player.local=player.human=true;player.country="Americans";player.startPosition=1;player.color="Gold";
        s.config.playerSlots.push_back(player);player.local=player.human=false;player.country="Russians";player.startPosition=2;player.color="Red";
        s.config.playerSlots.push_back(player);
        s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;s.world=assets->world;
        s.world.accessObjects().clear();s.report=assets->report;s.mapFacts=assets->mapFacts;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeSkirmishStart(s);
        ensureRuntimeRepairDepotsInitialized(s);s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        const auto addHost=[&](const char* name,RuntimeCellPoint cell){auto* o=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,name,cell);
            if(o)o->buildAnimTicksRemaining=0;return o?o->instanceId:-1;};
        if(addHost("GAPOWR",{40,72})<0)return fail("power creation");
        const int hostId=addHost(type,{44,76});if(hostId<0)return fail("airport creation");
        std::vector<int> planes;
        const auto produce=[&](const char* planeType){int oldMax=-1;
            for(const auto& o:s.world.getObjects())oldMax=std::max(oldMax,o.instanceId);
            RuntimeBuildCatalogItem item;item.kind=RuntimeBuildKind::Unit;item.typeId=planeType;item.requiredFactory="AircraftType";
            if(!completeRuntimeProductionOrder(s,item))return -1;
            for(const auto& o:s.world.getObjects())if(o.instanceId>oldMax&&o.type==planeType)return o.instanceId;
            return -1;};
        for(int slot=0;slot<4;++slot){const int id=produce(slot%2?"BEAG":"ORCA");if(id<0)return fail("four real aircraft production");planes.push_back(id);}
        int alternateId=-1,otherPlane=-1;
        if(alternate){alternateId=addHost(type,{44,100});if(alternateId<0)return fail("alternate airport");
            // Keep a different airport's parked aircraft as an exclusion case.
            if(flying!=4){otherPlane=produce("BEAG");if(otherPlane<0||findObjectById(s,otherPlane)->dockedToObjectId!=alternateId)
                return fail("other airport aircraft");}}
        auto* nearby=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{50,70});if(!nearby)return fail("nearby tank");const int nearbyId=nearby->instanceId;
        auto* source=addRuntimeObjectInstanceForOwner(s,runtimeSkirmishOwnerForSlot(1),RuntimeBuildKind::Unit,"MTNK",{92,120},std::nullopt,false);
        if(!source)return fail("enemy source");const int sourceId=source->instanceId;
        const auto step=[&](RuntimeWindowState& state){++state.tick;advanceRuntimeSimulation(state,false);completeRuntimeWorldRefresh(state);};
        initializeRuntimeOfflineGameplayRouting(s);step(s);
        if(flying){s.selectedObjectIds.assign(planes.begin(),planes.begin()+flying);
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{105,93})||s.lastCommand.acceptedCount!=flying)
                return fail("real takeoff command");
            for(int n=0;n<hz/2;++n)step(s);}
        for(int slot=0;slot<4;++slot){const auto* p=findObjectById(s,planes[slot]);
            if(!p||p->airborne!=(slot<flying)||p->dockedToObjectId!=hostId)return fail("pre-death support relationship");}
        const auto clone=[&](bool exact){std::ostringstream out;IniFile ini;
            if(!writeRuntimeStateToStream(s,out,error)||(exact&&!writeRuntimeMultiplayerSnapshotExtension(s,out,error))||!ini.loadFromString(out.str()))
                return std::unique_ptr<RuntimeWindowState>();
            auto result=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*result,s,false);
            if(!materializeRuntimeLoadStateFromIni(*result,ini,error))return std::unique_ptr<RuntimeWindowState>();
            initializeRuntimeNavigation(*result);completeRuntimeWorldRefresh(*result);
            if(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*result,ini,error))return std::unique_ptr<RuntimeWindowState>();
            ++saves;if(exact)++snapshots;return result;};
        const auto kill=[&](RuntimeWindowState& state){const auto* host=findObjectById(state,hostId);if(!host)return false;
            const auto center=runtimeProjectileTargetPosition(state,*host);
            RuntimeProjectileEffect hit;hit.actorId=nextRuntimeProjectileActorId(state);hit.attackerObjectId=sourceId;
            hit.targetObjectId=hostId;hit.owner=runtimeSkirmishOwnerForSlot(1);hit.toCell=center.cell;
            hit.toLevel=center.level;hit.currentCell=center.cell;hit.damage=20000;hit.warheadId="AP";
            hit.impactAnimId="PIFF";hit.ag=true;hit.affectsEnemies=hit.affectsAllies=true;
            return applyRuntimeProjectileImpact(state,hit);};
        const bool pictured=output&&std::string(type)=="GAAIRC"&&hz==30&&flying==1&&alternate;
        const auto capture=[&](const std::string& phase,bool air=false){if(!pictured)return true;
            s.selectedObjectIds=planes;const auto* p=findObjectById(s,planes[0]);
            const RuntimeCellPoint camera=air&&p?RuntimeCellPoint{p->x,p->y}:RuntimeCellPoint{46,78};
            const auto path=std::filesystem::path(output)/(phase+".bmp");s.mouseClientPointValid=false;
            if(!drawRuntimeStateOffscreenAtCell(s,camera,800,600,error,path.string(),false,true,-1,nullptr,true))return false;
            ++frames;return true;};
        if(!capture("airport-before")||!capture("aircraft-before",true))return fail("before frame: "+error);
        if(hz==30&&flying==1&&alternate)for(const auto kind:{RuntimeObjectExitKind::TemporalDestroyed,
                RuntimeObjectExitKind::Sold,RuntimeObjectExitKind::ScriptRemoved}){
            auto boundary=clone(false);if(!boundary)return fail("boundary save: "+error);
            RuntimeObjectExitRequest exit;exit.objectId=hostId;exit.kind=kind;exit.tick=s.tick;
            exit.sourceObjectId=sourceId;exit.sourceOwner=runtimeSkirmishOwnerForSlot(1);exit.suppressCrew=true;
            if(queueRuntimeObjectExit(*boundary,exit)!=RuntimeObjectExitQueueResult::Queued
                ||!drainRuntimeObjectExitQueue(*boundary))return fail("boundary exit rejected");
            for(int slot=0;slot<4;++slot)if(bool(findObjectById(*boundary,planes[slot]))
                !=(slot==0||kind!=RuntimeObjectExitKind::TemporalDestroyed))return fail("temporal/sold/script exit confused");
            if(!findObjectById(*boundary,otherPlane))return fail("boundary affected another airport");
            ++boundaries;
        }
        auto before=clone(false);if(!before)return fail("pre-death save: "+error);
        if(!kill(s))return fail("real lethal impact rejected");
        int survivingGround=0,survivingAir=0;
        for(int slot=0;slot<4;++slot){const auto* p=findObjectById(s,planes[slot]);if(p)(slot<flying?survivingAir:survivingGround)++;}
        std::cout<<"Airport loss observed: "<<scene<<" groundAlive="<<survivingGround<<" airAlive="<<survivingAir<<'\n';
        if(findObjectById(s,hostId)||survivingGround!=0||survivingAir!=flying)return fail("destroyed airport must remove its grounded aircraft and preserve its airborne aircraft");
        if(!findObjectById(s,nearbyId)||(otherPlane>=0&&!findObjectById(s,otherPlane)))return fail("unrelated unit was destroyed");
        groundDestroyed+=4-flying;airSurvivors+=flying;otherSurvivors+=otherPlane>=0;
        for(int slot=flying;slot<4;++slot){const auto* p=findRuntimeObjectByIdIncludingInactive(s,planes[slot]);
            if(!p||p->health!=0||p->dockedToObjectId!=-1||p->dockIndex!=-1||!s.deathWeapons.hasTriggered(p->instanceId)
                ||s.behaviors.contains(p->instanceId)||s.aircraftFlights.contains(p->instanceId))return fail("ground aircraft exit cleanup");}
        if(!kill(*before))return fail("pre-death restored impact");
        auto after=clone(true);if(!after)return fail("post-death snapshot: "+error);
        if(std::string(type)=="GAAIRC"&&hz==15&&flying==0&&!alternate){
            std::ostringstream wire;if(!writeRuntimeMultiplayerSnapshotExtension(s,wire,error))return fail("snapshot format fixture");
            const auto exact=wire.str();const std::string key="ImpactPresentation0=";
            const auto begin=exact.find(key),end=exact.find('\n',begin);
            if(begin==std::string::npos||end==std::string::npos)return fail("missing impact presentation");
            const auto fields=splitRuntimeSaveFields(exact.substr(begin+key.size(),end-begin-key.size()));
            if(fields.size()!=7)return fail("impact presentation width");
            for(int part=0;part<7;++part){auto bad=fields;bad[part]=part==2?"-2":part==3?"256":part==4?"-1":"!";
                std::string line=key;for(std::size_t i=0;i<bad.size();++i){if(i)line+='|';line+=bad[i];}
                auto changed=exact;changed.replace(begin,end-begin,line);IniFile ini;ini.loadFromString(changed);std::string rejected;
                if(materializeRuntimeMultiplayerSnapshotExtension(*after,ini,rejected)||rejected.empty())return fail("invalid impact presentation accepted");
                ++formatChecks;}
            std::istringstream lines(exact);std::string line,legacy;while(std::getline(lines,line))
                if(line.rfind("ImpactPresentation",0)!=0)legacy+=line+'\n';
            IniFile old;old.loadFromString(legacy);
            if(!materializeRuntimeMultiplayerSnapshotExtension(*after,old,error))return fail("legacy snapshot rejected: "+error);
            ++formatChecks;IniFile original;original.loadFromString(exact);
            if(!materializeRuntimeMultiplayerSnapshotExtension(*after,original,error))return fail("exact impact restore after format checks");
        }
        const auto compare=[&](const RuntimeWindowState& other){
            for(int id:planes){const auto* a=findRuntimeObjectByIdIncludingInactive(s,id);const auto* b=findRuntimeObjectByIdIncludingInactive(other,id);
                if(!a||!b||a->alive!=b->alive||a->health!=b->health||a->airborne!=b->airborne
                    ||a->dockedToObjectId!=b->dockedToObjectId||a->dockIndex!=b->dockIndex||a->currentAmmo!=b->currentAmmo)return false;}
            return true;};
        if(!compare(*before)||!compare(*after))return fail("saved destruction outcome diverged");
        if(!capture("airport-destroyed")||!capture("aircraft-survived",true))return fail("destruction frame: "+error);
        for(int n=0;n<12;++n){step(s);step(*before);step(*after);paired+=2;
            if(!compare(*before)||!compare(*after))return fail("continued saved destruction diverged");
            std::vector<std::pair<std::string,std::uint64_t>> left,right;
            const auto a=runtimeStandaloneSimulationStateHash(s,error,&left),b=runtimeStandaloneSimulationStateHash(*after,error,&right);
            if(!a||a!=b){
                for(std::size_t i=0;i<std::min(left.size(),right.size());++i)if(left[i]!=right[i]){
                    std::cout<<"Airport snapshot first mismatch "<<left[i].first<<" tick="<<s.tick<<'\n';break;}
                if(output){std::ofstream first(std::filesystem::path(output)/"snapshot-source.ini"),second(std::filesystem::path(output)/"snapshot-restored.ini");
                    std::string diagnostic;writeRuntimeStateToStream(s,first,diagnostic);writeRuntimeMultiplayerSnapshotExtension(s,first,diagnostic);
                    writeRuntimeStateToStream(*after,second,diagnostic);writeRuntimeMultiplayerSnapshotExtension(*after,second,diagnostic);}
                return fail("exact post-death snapshot diverged: "+error);}}
        if(!capture("airport-after"))return fail("after frame: "+error);
        RuntimeObjectExitRequest repeat;repeat.objectId=hostId;repeat.kind=RuntimeObjectExitKind::Destroyed;
        const auto tombstones=s.deathWeapons.triggeredObjects().size();
        if(queueRuntimeObjectExit(s,repeat)!=RuntimeObjectExitQueueResult::AlreadyFinalized||s.deathWeapons.triggeredObjects().size()!=tombstones)
            return fail("duplicate airport destruction repeated");++duplicates;
        if(flying){
            // Without an alternate, planes must remain alive while waiting;
            // providing a new airport later must restore a usable return route.
            if(!alternate){for(int n=0;n<hz*10;++n)step(s);
                for(int slot=0;slot<flying;++slot){const auto* p=findObjectById(s,planes[slot]);
                    if(!p||!p->airborne)return fail("airborne survivor lost while no airport exists");}
                alternateId=addHost(type,{44,100});if(alternateId<0)return fail("replacement airport");}
            // Original Speed/ROT clocks and descent replaced the former fast
            // movement clock. A survivor finishes its pending Move before
            // returning: budget both remaining legs from its current pose.
            const auto* returnHost=findObjectById(s,alternateId);
            const auto* returnBuilding=returnHost
                ?findRuntimeBuildingTypeById(s.rules,returnHost->type):nullptr;
            if(!returnHost||!returnBuilding||returnBuilding->numberOfDocks<=0)
                return fail("return budget has no replacement airport docks");
            long long returnBudgetTicks=50LL*hz;
            for(int slot=0;slot<flying;++slot){
                const auto* p=findObjectById(s,planes[slot]);
                if(!p)return fail("return budget aircraft disappeared");
                if(!p->airborne&&p->mission=="Dock"&&p->dockedToObjectId==alternateId)continue;
                const auto* unit=findRuntimeUnitType(s.rules,*p);
                const double ticksPerCell=runtimeMovementBaseIntervalTicks(s,*p);
                if(!unit||!std::isfinite(ticksPerCell)||ticksPerCell<=0.0)
                    return fail("return budget has invalid effective flight speed");
                const auto flight=s.aircraftFlights.find(p->instanceId);
                const double currentX=flight!=s.aircraftFlights.end()
                    ?double(flight->second.x)/runtimeAircraftCellQ16:runtimeObjectRenderX(*p);
                const double currentY=flight!=s.aircraftFlights.end()
                    ?double(flight->second.y)/runtimeAircraftCellQ16:runtimeObjectRenderY(*p);
                const double currentZ=flight!=s.aircraftFlights.end()&&flight->second.z>=0
                    ?double(flight->second.z)/runtimeAircraftCellQ16:runtimeObjectRenderLevel(*p);
                const auto order=s.behaviors.find(p->instanceId);
                const bool hasMove=order!=s.behaviors.end()&&order->second.kind==RuntimeBehaviorKind::Move;
                const bool waitingForDock=hasMove&&order->second.state==RuntimeBehaviorState::Blocked
                    &&order->second.pendingAllowedOccupiedObjectId<0
                    &&order->second.moveTarget==runtimeCellPointForObject(*p);
                // Use its assigned slot when known. A newly created airport
                // can be assigned on the next normal world/service pass; in
                // that case take the largest budget across its concrete pads.
                const bool assigned=p->dockedToObjectId==alternateId
                    &&p->dockIndex>=0&&p->dockIndex<returnBuilding->numberOfDocks;
                const int firstDock=assigned?p->dockIndex:0;
                const int endDock=assigned?p->dockIndex+1:returnBuilding->numberOfDocks;
                long long planeBudgetTicks=0;
                for(int dockIndex=firstDock;dockIndex<endDock;++dockIndex){
                    const auto dock=runtimeAircraftDockSpawn(s,*returnHost,dockIndex);
                    const bool alreadyReturning=hasMove
                        &&order->second.pendingAllowedOccupiedObjectId==alternateId
                        &&order->second.moveTarget==dock.cell;
                    const bool viaPendingMove=hasMove&&!waitingForDock&&!alreadyReturning;
                    const auto goal=viaPendingMove?runtimeFixedWingMoveGoal(s,*p,order->second)
                        :std::pair<double,double>{dock.renderX,dock.renderY};
                    const double routeCells=std::hypot(currentX-goal.first,currentY-goal.second)
                        +(viaPendingMove?std::hypot(goal.first-dock.renderX,goal.second-dock.renderY):0.0);
                    const long long travelTicks=static_cast<long long>(std::ceil(routeCells*ticksPerCell));
                    // A full native ROT revolution for each flown leg allows
                    // bounded-heading arcs/close off-axis approach. The final
                    // PoseDir can require another half revolution at pad XY.
                    const int turnStep=std::max(1,static_cast<int>(std::llround(
                        std::clamp(unit->rot,1,127)*256.0*15/hz)));
                    const long long fullTurnTicks=(65536LL+turnStep-1)/turnStep;
                    const long long halfTurnTicks=(32768LL+turnStep-1)/turnStep;
                    const long long turnTicks=(viaPendingMove?2:1)*fullTurnTicks+halfTurnTicks;
                    // A pending outbound leg climbs back to its cruise level.
                    // Evaluate the retail non-dropship 20..50 lepton descent
                    // independently using the current height/cruise envelope.
                    const double descentStart=std::max(currentZ,double(p->flightLevel));
                    std::int64_t remainingZ=std::llround(std::max(0.0,descentStart-dock.level)
                        *runtimeAircraftCellQ16);
                    long long descentTicks=0;
                    while(remainingZ>0){
                        const auto heightLeptons=remainingZ*256/runtimeAircraftCellQ16;
                        const auto stepLeptons=std::clamp<std::int64_t>(heightLeptons/20,20,50);
                        const auto stepZ=std::min(remainingZ,std::max<std::int64_t>(1,std::llround(
                            double(stepLeptons)*runtimeAircraftCellQ16*15/(256.0*hz))));
                        remainingZ-=stepZ;++descentTicks;
                    }
                    // Retain a pending native cooldown, at least the one-second
                    // missing-host retry, plus four fixed-tick route/arrival
                    // boundaries. This is a finite bound set once per scene.
                    const long long repathTicks=hasMove
                        ?std::max<long long>(hz,static_cast<long long>(order->second.nextRepathTick)-s.tick):hz;
                    planeBudgetTicks=std::max(planeBudgetTicks,
                        travelTicks+turnTicks+descentTicks+repathTicks+4);
                }
                returnBudgetTicks=std::max(returnBudgetTicks,planeBudgetTicks);
                std::cout<<"Airport loss native return budget: "<<scene<<" aircraft="<<p->instanceId
                    <<" tick="<<s.tick<<" current="<<currentX<<','<<currentY<<','<<currentZ
                    <<" assignedDock="<<(assigned?p->dockIndex:-1)<<" ticks="<<planeBudgetTicks
                    <<" seconds="<<double(planeBudgetTicks)/hz<<'\n';
            }
            if(returnBudgetTicks>std::numeric_limits<int>::max())
                return fail("native return budget exceeds fixed-tick test range");
            bool allLanded=false;
            for(int n=0;n<returnBudgetTicks;++n){step(s);allLanded=true;std::set<int> slots;
                for(int slot=0;slot<flying;++slot){const auto* p=findObjectById(s,planes[slot]);
                    if(!p)return fail("airborne survivor disappeared");
                    allLanded=allLanded&&!p->airborne&&p->mission=="Dock"&&p->dockedToObjectId==alternateId;slots.insert(p->dockIndex);}
                if(allLanded&&slots.size()==static_cast<std::size_t>(flying))break;allLanded=false;}
            if(!allLanded){
                std::cout<<"Airport loss return timeout: "<<scene<<" tick="<<s.tick
                    <<" matchFinished="<<s.skirmishMatchFinished<<" result="<<s.skirmishResult
                    <<" originalHost="<<hostId<<" expectedHost="<<alternateId<<'\n';
                std::set<int> diagnosticHosts{hostId,alternateId};
                for(int slot=0;slot<flying;++slot){const int id=planes[slot];
                    const auto* p=findRuntimeObjectByIdIncludingInactive(s,id);
                    if(p){diagnosticHosts.insert(p->dockedToObjectId);std::cout
                        <<"  aircraft="<<id<<" type="<<p->type<<" alive="<<p->alive<<" health="<<p->health
                        <<" airborne="<<p->airborne<<" mission="<<p->mission<<" ammo="<<p->currentAmmo
                        <<" cell="<<p->x<<','<<p->y<<" level="<<p->level<<" flightLevel="<<p->flightLevel
                        <<" render="<<p->renderX<<','<<p->renderY<<','<<p->renderLevel
                        <<" hasRender="<<p->hasRenderPosition<<" renderActive="<<p->renderMoveActive
                        <<" facing="<<p->facing<<" dock="<<p->dockedToObjectId<<':'<<p->dockIndex
                        <<" dockProgress="<<p->aircraftDockFlightProgress<<'\n';}
                    else std::cout<<"  aircraft="<<id<<" missing\n";
                    if(const auto b=s.behaviors.find(id);b!=s.behaviors.end())std::cout
                        <<"  behavior kind="<<static_cast<int>(b->second.kind)<<" state="<<static_cast<int>(b->second.state)
                        <<" target="<<b->second.targetObjectId<<" moveTarget="<<b->second.moveTarget.x<<','<<b->second.moveTarget.y
                        <<" allowedHost="<<b->second.pendingAllowedOccupiedObjectId<<" pathSize="<<b->second.path.size()
                        <<" pathPending="<<b->second.pathRequestPending<<" nextRepath="<<b->second.nextRepathTick
                        <<" moveCooldown="<<b->second.moveCooldownTicks<<'\n';
                    else std::cout<<"  behavior missing\n";
                    if(const auto f=s.aircraftFlights.find(id);f!=s.aircraftFlights.end())std::cout
                        <<"  flightQ16="<<f->second.x<<','<<f->second.y<<','<<f->second.z
                        <<" heading="<<f->second.heading<<" lastTick="<<f->second.lastTick<<" moving="<<f->second.moving
                        <<" landing="<<f->second.landingHostId<<':'<<f->second.landingDockIndex
                        <<" envelope="<<f->second.dockFlightStartZ<<','<<f->second.dockFlightTargetZ
                        <<" soundPlayed="<<f->second.landingSoundPlayed<<'\n';
                    else std::cout<<"  flight missing\n";
                }
                for(int diagnosticHost:diagnosticHosts){const auto* h=findRuntimeObjectByIdIncludingInactive(s,diagnosticHost);
                    if(h)std::cout<<"  host="<<diagnosticHost<<" type="<<h->type<<" owner="<<h->owner
                        <<" alive="<<h->alive<<" health="<<h->health<<" cell="<<h->x<<','<<h->y
                        <<" level="<<h->level<<" mission="<<h->mission<<'\n';
                    else std::cout<<"  host="<<diagnosticHost<<" missing\n";}
                return fail("surviving aircraft did not land at the remaining/new airport");
            }
            landings+=flying;if(!capture("aircraft-returned",true))return fail("return frame: "+error);
        }
        ++cases;
    }
    std::cout<<"Airport loss: passed cases="<<cases<<" groundDestroyed="<<groundDestroyed<<" airSurvivors="<<airSurvivors
        <<" otherAirportSurvivors="<<otherSurvivors<<" saves="<<saves<<" snapshots="<<snapshots<<" pairedTicks="<<paired
        <<" duplicateDeaths="<<duplicates<<" survivorLandings="<<landings<<" nativeFrames="<<frames<<" exitBoundaries="<<boundaries
        <<" snapshotFormatChecks="<<formatChecks<<'\n';
    return true;
}
