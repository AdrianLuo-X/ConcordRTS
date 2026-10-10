bool runRuntimeAircraftAmmoSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& detail){error="Aircraft ammo: "+detail;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("original resources");
    const char* output=std::getenv("CONCORD_AIRCRAFT_AMMO_OUTPUT");
    int scenes=0,sorties=0,shots=0,returns=0,reloads=0,rejections=0,saves=0,paired=0,pips=0,frames=0;
    for(const auto* type:{"BEAG","ORCA"})for(int rank:{0,2})for(int hz:{15,30,60})for(bool instant:{false,true}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=config;s.config.fixedTickHz=hz;s.config.noCooldown=instant;
        s.config.computerAiEnabled=false;s.config.shortGame=false;s.config.startingUnits=0;
        s.config.playerCountry=std::string(type)=="BEAG"?"Alliance":"Americans";
        s.config.playerSlots.clear();RuntimeConfig::PlayerSlot player;
        player.enabled=player.local=player.human=true;player.country=s.config.playerCountry;
        player.startPosition=1;player.color="Gold";s.config.playerSlots.push_back(player);
        player.local=player.human=false;player.country="Russians";player.startPosition=2;
        player.color="Red";s.config.playerSlots.push_back(player);
        s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;s.world=assets->world;
        s.world.accessObjects().clear();s.report=assets->report;s.mapFacts=assets->mapFacts;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeSkirmishStart(s);
        ensureRuntimeRepairDepotsInitialized(s);s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        auto* power=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"GAPOWR",{40,72});
        if(!power)return fail("power creation");power->buildAnimTicksRemaining=0;
        auto* airport=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"GAAIRC",{44,76});
        if(!airport)return fail("airport creation");const int hostId=airport->instanceId;airport->buildAnimTicksRemaining=0;
        RuntimeBuildCatalogItem item;item.kind=RuntimeBuildKind::Unit;item.typeId=type;item.requiredFactory="AircraftType";
        if(!completeRuntimeProductionOrder(s,item))return fail("real aircraft production");
        int id=-1;for(const auto& object:s.world.getObjects())if(object.type==type)id=object.instanceId;
        auto* plane=findMutableObjectById(s,id);if(!plane)return fail("produced aircraft missing");plane->veterancy=rank;
        const auto* rules=runtimeAmmoRulesForObject(s.rules,*plane);
        if(!rules||rules->ammo!=1||!rules->manualReload||!rules->airportBound||plane->currentAmmo!=1)
            return fail("stock magazine/production rules");
        const int interval=static_cast<int>(std::ceil(s.rules.getSpecialMechanismRules().reloadRateMinutes*60*hz-1e-9));
        std::ofstream trace;
        if(output){trace.open(std::filesystem::path(output)/(std::string(type)+"-"+std::to_string(rank)+"-"+std::to_string(hz)+"-"+std::to_string(instant)+".csv"));
            trace<<"tick,event,ammo,airborne,mission,reloadRemaining\n";}
        const auto record=[&](const char* event){const auto* p=findObjectById(s,id);
            if(trace)trace<<s.tick<<','<<event<<','<<p->currentAmmo<<','<<p->airborne<<','<<p->mission<<','<<s.aircraftServices.remainingReloadTicks(hostId)<<'\n';};
        const auto step=[&](RuntimeWindowState& state){++state.tick;advanceRuntimeSimulation(state,false);completeRuntimeWorldRefresh(state);};
        const auto checkPips=[&](int expected){s.selectedObjectIds={id};
            const auto& ui=materializeRuntimeUiStateSnapshot(s);const auto display=runtimeCargoPipDisplayForObject(s,*findObjectById(s,id));
            if(ui.selectedAmmoCapacity!=1||ui.selectedAmmoCurrent!=expected||ui.selectedPipTotal!=1||ui.selectedPipFilled!=expected
                ||!display.available||display.totalUnits!=1||display.filledUnits!=expected)return false;
            ++pips;return true;};
        const auto roundtrip=[&](){std::ostringstream text;IniFile ini;
            if(!writeRuntimeStateToStream(s,text,error)||!ini.loadFromString(text.str()))return false;
            auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
            if(!materializeRuntimeLoadStateFromIni(*restored,ini,error))return false;
            initializeRuntimeNavigation(*restored);completeRuntimeWorldRefresh(*restored);++saves;
            for(int n=0;n<12;++n){const auto* a=findObjectById(s,id);const auto* b=findObjectById(*restored,id);
                if(a->currentAmmo!=b->currentAmmo||a->airborne!=b->airborne||a->mission!=b->mission
                    ||s.aircraftServices.stateHash()!=restored->aircraftServices.stateHash())return false;
                step(s);step(*restored);++paired;}
            return true;};
        const auto capture=[&](const char* phase){
            if(!output||std::string(type)!="BEAG"||rank!=0||hz!=30)return true;
            s.selectedObjectIds={id};s.mouseClientPointValid=false;
            const auto* p=findObjectById(s,id);
            const auto path=std::filesystem::path(output)/(std::string("BEAG-")+std::to_string(instant)+"-"+phase+".bmp");
            if(!drawRuntimeStateOffscreenAtCell(s,{p->x,p->y},800,600,error,path.string(),false,true,-1,nullptr,true))return false;
            ++frames;return true;
        };
        initializeRuntimeOfflineGameplayRouting(s);step(s);record("production");
        if(!checkPips(1))return fail("loaded ammunition indicator");
        if(!capture("loaded"))return fail("loaded frame: "+error);
        for(int sortie=0;sortie<2;++sortie) {
            auto* target=addRuntimeObjectInstanceForOwner(s,runtimeSkirmishOwnerForSlot(1),RuntimeBuildKind::Building,
                "NACNST",{90+sortie*7,92},std::nullopt,false);
            if(!target)return fail("target creation");const int targetId=target->instanceId;const int beforeHealth=target->health;
            target->buildAnimTicksRemaining=0;s.selectedObjectIds={id};
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{target->x,target->y},target))
                return fail("ready aircraft attack rejected");
            bool fired=false;int firingTicks=0;
            for(int n=0;n<hz*30;++n){step(s);firingTicks+=s.actionSummary.attacksThisTick;plane=findMutableObjectById(s,id);
                if(plane->currentAmmo==0){fired=true;break;}}
            if(!fired||firingTicks!=1||!plane->airborne)return fail("one real discharge must spend the stored round");
            ++sorties;++shots;record("fired");if(!checkPips(0))return fail("empty ammunition indicator");
            if(sortie==0&&!capture("empty-airborne"))return fail("empty airborne frame: "+error);
            const auto* weapon=s.rules.findWeaponType(rank==2?(std::string(type)=="BEAG"?"Maverick2E":"MaverickE")
                :(std::string(type)=="BEAG"?"Maverick2":"Maverick"));
            if(!weapon||weapon->burst!=1)return fail("stock one-missile weapon");
            if(sortie==0&&!roundtrip())return fail("empty airborne full save: "+error);
            target=findMutableObjectById(s,targetId);s.selectedObjectIds={id};
            if(target&&target->alive&&dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{target->x,target->y},target))
                return fail("empty aircraft accepted another attack");
            ++rejections;
            bool landed=false;int landingTick=-1;
            for(int n=0;n<hz*40;++n){plane=findMutableObjectById(s,id);
                if(!plane||!plane->alive)return fail("aircraft lost during empty return");
                if(plane->currentAmmo!=0)return fail("ammunition refilled before physical docking");
                if(!plane->airborne&&plane->mission=="Dock"){landed=true;landingTick=s.tick;break;}
                step(s);if(s.actionSummary.attacksThisTick!=0)return fail("empty aircraft fired again");}
            if(!landed||plane->dockedToObjectId!=hostId){
                std::cout<<"Aircraft ammo return timeout: type="<<type<<" rank="<<rank<<" hz="<<hz
                    <<" noCooldown="<<instant<<" sortie="<<sortie<<" tick="<<s.tick
                    <<" matchFinished="<<s.skirmishMatchFinished<<" result="<<s.skirmishResult
                    <<" expectedHost="<<hostId<<'\n';
                const auto* p=findRuntimeObjectByIdIncludingInactive(s,id);
                if(p)std::cout<<"  aircraft="<<id<<" alive="<<p->alive<<" health="<<p->health
                    <<" airborne="<<p->airborne<<" mission="<<p->mission<<" ammo="<<p->currentAmmo
                    <<" cell="<<p->x<<','<<p->y<<" level="<<p->level<<" flightLevel="<<p->flightLevel
                    <<" render="<<p->renderX<<','<<p->renderY<<','<<p->renderLevel
                    <<" hasRender="<<p->hasRenderPosition<<" renderActive="<<p->renderMoveActive
                    <<" facing="<<p->facing<<" dock="<<p->dockedToObjectId<<':'<<p->dockIndex
                    <<" dockProgress="<<p->aircraftDockFlightProgress<<'\n';
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
                std::set<int> diagnosticHosts{hostId};if(p)diagnosticHosts.insert(p->dockedToObjectId);
                for(int diagnosticHost:diagnosticHosts){const auto* h=findRuntimeObjectByIdIncludingInactive(s,diagnosticHost);
                    if(h)std::cout<<"  host="<<diagnosticHost<<" type="<<h->type<<" owner="<<h->owner
                        <<" alive="<<h->alive<<" health="<<h->health<<" cell="<<h->x<<','<<h->y
                        <<" level="<<h->level<<" mission="<<h->mission<<'\n';
                    else std::cout<<"  host="<<diagnosticHost<<" missing\n";}
                return fail("empty aircraft did not return to airport");
            }
            ++returns;record("landed");
            if(sortie==0&&!capture("empty-docked"))return fail("empty dock frame: "+error);
            target=findMutableObjectById(s,targetId);
            if(target&&target->health>=beforeHealth)return fail("stored missile caused no real damage");
            bool ready=false;int reloadTick=-1;
            for(int n=0;n<interval+hz*2;++n){step(s);plane=findMutableObjectById(s,id);
                if(plane->airborne)return fail("empty parked aircraft departed without an order");
                if(plane->currentAmmo>0){ready=true;reloadTick=s.tick;break;}
                if(sortie==0&&n==interval/2&&!roundtrip())return fail("mid-reload full save: "+error);}
            if(!ready||reloadTick-landingTick<interval-1||reloadTick-landingTick>interval+1)
                return fail("docked reload does not follow the original interval: "+std::to_string(reloadTick-landingTick)+" expected "+std::to_string(interval));
            ++reloads;record("reloaded");if(!checkPips(1))return fail("reloaded ammunition indicator");
            if(sortie==0&&!capture("reloaded"))return fail("reloaded frame: "+error);
            for(int n=0;n<12;++n){step(s);if(findObjectById(s,id)->currentAmmo!=1)return fail("stored ammo did not remain capped");}
        }
        ++scenes;std::cout<<"Aircraft ammo: "<<type<<" rank="<<rank<<" hz="<<hz<<" noCooldown="<<instant<<" two real sorties/reload/pips/save=ok\n";
    }
    std::cout<<"Aircraft ammo: passed scenes="<<scenes<<" sorties="<<sorties<<" shots="<<shots<<" returns="<<returns
        <<" reloads="<<reloads<<" emptyRejections="<<rejections<<" saves="<<saves<<" pairedTicks="<<paired<<" pips="<<pips<<" nativeFrames="<<frames<<'\n';
    return true;
}
