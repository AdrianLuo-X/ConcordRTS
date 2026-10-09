bool runRuntimeBorisBuildingDamageSmoke(RuntimeWindowState& live, std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why){error="Boris building damage "+scene+": "+why;return false;};
    IniFile stockIni;RulesDatabase stock;
    if(!stockIni.loadFromFile(live.report.rulesPath)||!stock.loadFromIni(stockIni))return fail("stock rules");
    struct Fixture {std::unique_ptr<RuntimeWindowState> state;int caller=-1,target=-1;};
    const auto make=[&](const std::string& type,bool durable,bool slowMissiles=false){
        Fixture f;f.state=std::make_unique<RuntimeWindowState>();auto& s=*f.state;seedRuntimeLoadScratch(s,live,false);
        s.rules=stock;s.rulesLoaded=true;
        if(durable||slowMissiles){IniFile rules=stockIni,overlay;
            overlay.loadFromString(std::string(durable?"[NACNST]\nStrength=100000\n":"")
                +(slowMissiles?"[Maverick3]\nSpeed=2\n":""));rules.appendFrom(overlay);s.rules.loadFromIni(rules);}
        s.tick=100;s.config.fixedTickHz=30;s.config.noCooldown=false;s.config.multiplayerActive=false;
        s.config.computerAiEnabled=false;s.config.gameMode="Battle";s.campaignMode=false;s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
        s.economy.owner="Russians";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.behaviors.clear();s.projectiles.clear();s.impactEffects.clear();s.airstrikes.reset();s.deathWeapons.reset();
        s.airstrikePlanesByToken.clear();s.airstrikeRootAuthoritiesByActor.clear();
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear();s.terrainInfoByCell.clear();s.terrainByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        for(int y=45;y<=85;++y)for(int x=45;x<=85;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);s.world.accessTerrainCells().push_back(c);}
        initializeRuntimeNavigation(s);
        auto* caller=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"BORIS",{60,65});if(!caller)return f;
        f.caller=caller->instanceId;caller->owner="Russians";caller->mission="Guard";
        auto* target=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,type,{66,65});if(!target)return f;
        f.target=target->instanceId;target->owner="Americans";target->mission="Guard";
        target->buildAnimTicksRemaining=target->buildAnimTicksTotal=0;
        advanceRuntimeSpecialMechanisms(s);applySelection(s,{f.caller},false,false);issueRuntimeAttackCommand(s,*target);
        return f;
    };
    const auto step=[](RuntimeWindowState& s,int callerId){
        ++s.tick;processRuntimePathRequests(s);advanceRuntimeWeaponCooldowns(s);
        auto* b=findMutableObjectById(s,callerId);const auto behavior=s.behaviors.find(callerId);
        if(b&&behavior!=s.behaviors.end()&&behavior->second.kind==RuntimeBehaviorKind::Attack
            &&behavior->second.state!=RuntimeBehaviorState::Complete)advanceRuntimeAttackBehavior(s,*b,behavior->second);
        advanceRuntimeEffects(s);
    };
    const auto health=[](const RuntimeWindowState& s,int id){const auto* t=findRuntimeObjectByIdIncludingInactive(s,id);return t?t->health:-1;};
    const auto save=[&](RuntimeWindowState& s,bool legacy)->std::unique_ptr<RuntimeWindowState>{
        std::ostringstream out;if(!writeRuntimeStateToStream(s,out,error))return nullptr;
        auto text=out.str();
        if(legacy){const std::string from="[RuntimeAirstrikeHost]\nSchema=2";const auto at=text.find(from);
            if(at==std::string::npos)return nullptr;text.replace(at,from.size(),"[RuntimeAirstrikeHost]\nSchema=1");}
        IniFile ini;if(!ini.loadFromString(text))return nullptr;
        auto result=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*result,s,false);
        if(!materializeRuntimeLoadStateFromIni(*result,ini,error))return nullptr;return result;
    };
    int buildings=0,airstrikeBuildings=0,rifleBuildings=0,interruptions=0,saves=0;
    for(const auto& type:stock.getBuildingTypes()){
        if(type.techLevel<0||type.strength<=0||type.wall||type.gate||type.immune||type.owner.empty())continue;
        scene=type.id;auto f=make(type.id,false);auto& s=*f.state;
        if(f.caller<0||f.target<0||s.lastCommand.acceptedCount!=1)return fail("stock building attack rejected");
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,f.caller),findObjectById(s,f.target));
        const bool airstrike=profile.warhead&&profile.warhead->airstrike;
        // Retail excludes a building that is both ResourceGatherer and
        // ResourceDestination from the secondary airstrike (0x6F348F).
        // YAREFN also has medium armor, with AirstrikeFlare Verses=0%.
        if(!airstrike&&(type.id!="YAREFN"||!profile.weapon||profile.weapon->id!="AKM"
            ||!type.resourceGatherer||!type.resourceDestination))return fail("unexpected stock weapon selection");
        bool launched=false,finished=false;
        for(int i=0;i<30*(airstrike?40:150);++i){step(s,f.caller);launched=launched||!s.airstrikeRootAuthoritiesByActor.empty();
            if((airstrike&&launched&&s.airstrikes.strikes().empty()&&s.airstrikePlanesByToken.empty())
                ||(!airstrike&&health(s,f.target)==0)){finished=true;break;}}
        if(!finished||health(s,f.target)!=0||(!airstrike&&launched))return fail("native attack left "+std::to_string(health(s,f.target))+" health");
        std::cout<<"Boris stock building="<<type.id<<" strength="<<type.strength<<" destroyed=1 weapon="<<(airstrike?"Flare":"AKM")<<'\n';
        ++buildings;if(airstrike)++airstrikeBuildings;else ++rifleBuildings;
    }
    if(buildings<40)return fail("stock building coverage unexpectedly small");
    scene="immune CABHUT";
    {
        auto f=make("CABHUT",false);auto& s=*f.state;const int initial=health(s,f.target);
        if(initial!=2000)return fail("native immune structure fixture");
        for(int i=0;i<30*20;++i)step(s,f.caller);
        if(health(s,f.target)!=initial)return fail("native Immune building took damage");
    }
    for(int mode=0;mode<5;++mode){
        scene="interruption="+std::to_string(mode);auto f=make("NACNST",true,mode==3);auto& s=*f.state;
        bool changed=false,finished=false;int firstPlane=-1;std::map<int,std::set<int>> shots;
        std::unique_ptr<RuntimeWindowState> loaded;
        for(int i=0;i<30*50;++i){
            // The slow-missile fixture uses the normal flight simulation to
            // exercise receipts that outlive their source aircraft.
            step(s,f.caller);
            if(loaded){step(*loaded,f.caller);
                if(mode!=2&&(health(s,f.target)!=health(*loaded,f.target)
                    ||s.airstrikes.stateHash()!=loaded->airstrikes.stateHash()))return fail("interrupted save diverged");}
            for(const auto& p:s.projectiles)if(p.airstrikeStrikeId>0&&p.generation==0){
                if(firstPlane<0)firstPlane=p.airstrikePlaneToken;shots[p.airstrikePlaneToken].insert(p.actorId);}
            const bool ready=!s.airstrikeRootAuthoritiesByActor.empty();
            if(!changed&&ready){
                if(mode==2&&(s.airstrikeRootAuthoritiesByActor.size()!=2
                    ||std::any_of(s.airstrikeRootAuthoritiesByActor.begin(),s.airstrikeRootAuthoritiesByActor.end(),
                        [](const auto& entry){return entry.second.rounds.size()!=1;})))continue;
                if(mode<2||mode==4){
                    const int victim=mode==0?s.airstrikeRootAuthoritiesByActor.begin()->second.planeObjectId:mode==4?f.caller:f.target;
                    RuntimeObjectExitRequest exit;exit.objectId=victim;exit.tick=s.tick;exit.kind=RuntimeObjectExitKind::ScriptRemoved;
                    exit.suppressDeathWeapon=true;exit.suppressCrew=true;exit.reason="salvo interruption smoke";
                    if(queueRuntimeObjectExit(s,exit)!=RuntimeObjectExitQueueResult::Queued)return fail("exit rejected");
                    drainRuntimeObjectExitQueue(s);step(s,f.caller);
                }
                changed=true;
                if(mode!=3){loaded=save(s,mode==2);if(!loaded)return fail("interrupted save: "+error);++saves;}
                else {applySelection(s,{f.caller},false,false);issueRuntimeStopCommand(s);}
            }
            if(mode==3&&changed&&s.airstrikePlanesByToken.empty()&&!loaded){
                if(s.airstrikeRootAuthoritiesByActor.size()!=2||s.projectiles.size()!=4)return fail("long salvo lost roots after aircraft exit");
                loaded=save(s,false);if(!loaded)return fail("long salvo save: "+error);++saves;
            }
            if(changed&&loaded&&s.airstrikes.strikes().empty()&&s.airstrikePlanesByToken.empty()&&s.airstrikeRootAuthoritiesByActor.empty()
                &&loaded->airstrikeRootAuthoritiesByActor.empty()){finished=true;break;}
        }
        if(!changed||!finished||!loaded)return fail("interruption did not finish");
        if(mode==0&&(shots[firstPlane].size()!=1||health(s,f.target)!=100000-1125))return fail("destroyed plane fired again or lost its in-flight missile");
        if(mode==1&&(shots.size()!=1||shots[firstPlane].size()!=1||health(s,f.target)!=100000
            ||findRuntimeObjectByIdIncludingInactive(s,f.target)->alive))return fail("removed target kept the burst active");
        if(mode==2&&(health(s,f.target)!=100000-1500||health(*loaded,f.target)!=100000-750))return fail("legacy save invented additional shots");
        if(mode==3&&health(s,f.target)!=100000-1500)return fail("long salvo failed to apply every root");
        if(mode==4&&(shots.size()!=1||shots[firstPlane].size()!=1||health(s,f.target)!=100000-375))return fail("cancelled strike fired remaining rounds");
        std::cout<<"Boris interruption="<<mode<<" damage="<<100000-health(s,f.target)<<" savedDamage="<<100000-health(*loaded,f.target)<<" PASS\n";
        ++interruptions;
    }
    std::cout<<"Boris building coverage passed: stockBuildings="<<buildings<<" airstrike="<<airstrikeBuildings
        <<" rifle="<<rifleBuildings<<" immune=1 interruptions="<<interruptions<<" saves="<<saves<<'\n';
    return true;
}
