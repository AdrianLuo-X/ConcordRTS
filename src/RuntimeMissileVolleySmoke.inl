bool runRuntimeMissileVolleySmoke(RuntimeWindowState& live, std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& why){error="Missile volley: "+why;return false;};
    const char* rawOutput=std::getenv("CONCORD_MISSILE_VOLLEY_OUTPUT");
    const std::string root=rawOutput&&*rawOutput?rawOutput:"build/missile-volley-smoke";
    std::filesystem::create_directories(root);
    const auto fixture=[&](const std::string& type,int hz,bool noCooldown) {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        seedRuntimeLoadScratch(s,live,false);
        s.config.fixedTickHz=hz;s.config.noCooldown=noCooldown;s.config.computerAiEnabled=false;
        s.config.multiplayerActive=false;s.campaignMode=false;s.visionEnabled=false;
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.economy.owner="Russians";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();s.behaviors.clear();
        s.terrainInfoByCell.clear();s.terrainByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        for(int y=1;y<=32;++y)for(int x=1;x<=42;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance parent;
        parent.instanceId=1;parent.kind=WorldObjectKind::Unit;parent.type=type;parent.owner="Russians";
        parent.health=s.rules.findUnitType("UnitType",type)->strength;parent.alive=true;
        parent.x=8;parent.y=10;parent.cell=runtimeCellKey(8,10);parent.mission="Attack";
        parent.facing=parent.turretFacing=runtimeFacingFromDelta(1,0);
        auto target=parent;target.instanceId=2;target.kind=WorldObjectKind::Structure;target.type="CAEAST01";
        target.owner="Americans";target.health=3000;target.x=24;target.cell=runtimeCellKey(24,10);target.mission="Guard";
        s.world.accessObjects()={parent,target};initializeRuntimeNavigation(s);
        RuntimeObjectBehavior attack;attack.kind=RuntimeBehaviorKind::Attack;attack.state=RuntimeBehaviorState::Queued;
        attack.targetObjectId=2;s.behaviors[1]=attack;
        advanceRuntimeSpecialMechanisms(s);
        rebuildRuntimeEconomyPower(s);
        return box;
    };
    const auto active=[](const RuntimeWindowState& s,int parent=1){
        std::set<int> ids;
        for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles())if(m.parentObjectId==parent)ids.insert(id);
        return ids;
    };
    // Ordinary saves reconstruct interpolation from logical cells. Missile
    // flight coordinates are compared exactly through specialMechanisms;
    // compare the remaining world without transient interpolation fields.
    const auto logicalWorldHash=[](const RuntimeWindowState& s){
        auto world=s.world;
        for(auto& object:world.accessObjects())clearRuntimeRenderPosition(object);
        return world.deterministicSimulationStateHash();
    };
    const auto step=[](RuntimeWindowState& s){
        ++s.tick;advanceRuntimeWeaponCooldowns(s);advanceRuntimeCombatBursts(s);
        auto* parent=findMutableObjectById(s,1);auto it=s.behaviors.find(1);
        if(parent&&it!=s.behaviors.end()&&it->second.kind==RuntimeBehaviorKind::Attack)
            (void)advanceRuntimeAttackBehavior(s,*parent,it->second);
        advanceRuntimeEffects(s);
    };
    const auto roundtrip=[&](RuntimeWindowState& s,const std::string& name){
        std::ostringstream output;
        if(!writeRuntimeStateToStream(s,output,error))return std::unique_ptr<RuntimeWindowState>{};
        IniFile ini;if(!ini.loadFromString(output.str())){error="save parse";return std::unique_ptr<RuntimeWindowState>{};}
        auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
        if(!materializeRuntimeLoadStateFromIni(*restored,ini,error))return std::unique_ptr<RuntimeWindowState>{};
        if(restored->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash()){
            error="save changed missile authority";return std::unique_ptr<RuntimeWindowState>{};
        }
        std::ofstream file(root+"/"+name+".ini",std::ios::binary);file<<output.str();
        return restored;
    };
    int cases=0,saves=0,comparisons=0,readyWhileFlying=0,interruptions=0;
    for(const std::string type:{"DRED","V3","BSUB"})for(int hz:{15,30,60})for(bool noCooldown:{false,true}) {
        auto storage=fixture(type,hz,noCooldown);auto& s=*storage;
        const std::string name=type+"-"+std::to_string(hz)+(noCooldown?"-no-cooldown":"-normal");
        const int capacity=s.rules.findUnitType("UnitType",type)->spawnsNumber;
        std::ofstream timeline(root+"/"+name+".csv");timeline<<"tick,event,active,targetHealth\n";
        int rounds=0,completed=0,lastEnd=-1,nextStart=-1;bool savedPreparing=false,savedFlying=false,savedReady=false;
        std::unique_ptr<RuntimeWindowState> restored;
        for(int tick=0;tick<hz*180&&completed<2;++tick){
            const auto before=active(s);
            ++s.tick;advanceRuntimeWeaponCooldowns(s);advanceRuntimeCombatBursts(s);
            auto* parent=findMutableObjectById(s,1);auto& behavior=s.behaviors.at(1);
            const bool fired=advanceRuntimeAttackBehavior(s,*parent,behavior);
            const auto committed=active(s);
            bool newMissile=false;for(int id:committed)if(!before.contains(id))newMissile=true;
            if(newMissile){
                if(!before.empty())return fail(name+" began another volley before the previous missiles ended at tick="
                    +std::to_string(s.tick)+" previous="+std::to_string(before.size())+" now="+std::to_string(committed.size()));
                if(committed.size()!=static_cast<std::size_t>(capacity))return fail(name+" split a native volley");
                ++rounds;nextStart=s.tick;
                if(rounds>1&&nextStart<=lastEnd)return fail(name+" next volley predates impact");
                timeline<<s.tick<<",launch,"<<committed.size()<<','<<findObjectById(s,2)->health<<'\n';
            }
            (void)fired;
            advanceRuntimeEffects(s);
            const auto after=active(s);
            if(!committed.empty()&&after.empty()){
                ++completed;lastEnd=s.tick;
                timeline<<s.tick<<",finished,0,"<<findObjectById(s,2)->health<<'\n';
            }
            if(restored){
                step(*restored);
                if(restored->tick!=s.tick||restored->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash()
                    ||logicalWorldHash(*restored)!=logicalWorldHash(s)) {
                    std::ofstream original(root+"/diverged-original.ini"),loaded(root+"/diverged-restored.ini");
                    std::string why;
                    writeRuntimeStateToStream(s,original,why);writeRuntimeStateToStream(*restored,loaded,why);
                    return fail(name+" restored continuation diverged at "+std::to_string(s.tick)
                        +" missileHash="+std::to_string(s.specialMechanisms.stateHash())+"/"
                        +std::to_string(restored->specialMechanisms.stateHash())+" worldHash="
                        +std::to_string(logicalWorldHash(s))+"/"+std::to_string(logicalWorldHash(*restored)));
                }
                ++comparisons;
            }
            bool flying=false,preparing=false,hasReady=false;
            for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles())if(m.parentObjectId==1){
                flying|=m.released;preparing|=!m.released;
            }
            const auto& slots=s.specialMechanisms.carrierParents().at(1);
            for(const auto& [slot,id]:slots.childBySlot)
                if(id>0&&s.specialMechanisms.isCarrierChildDocked(id)&&!after.contains(id))hasReady=true;
            if(flying&&hasReady)++readyWhileFlying;
            const bool shouldSave=rounds==1&&((preparing&&!savedPreparing)||(flying&&!savedFlying)||(flying&&hasReady&&!savedReady));
            if(shouldSave){
                restored=roundtrip(s,name+"-save-"+std::to_string(saves));
                if(!restored)return fail(name+" save/load "+error);
                ++saves;savedPreparing|=preparing;savedFlying|=flying;savedReady|=flying&&hasReady;
            }
        }
        if(rounds!=2||completed!=2||!savedPreparing||!savedFlying||findObjectById(s,2)->health>=3000)
            return fail(name+" two complete impact-driven volleys did not finish");
        ++cases;
        std::cout<<"Missile volley cycle: "<<name<<" rounds="<<rounds<<" completed="<<completed
            <<" finish="<<lastEnd<<" health="<<findObjectById(s,2)->health<<" PASS\n";
    }
    if(readyWhileFlying==0)return fail("no reloaded slot was exercised while the previous volley was flying");

    for(const std::string type:{"DRED","V3","BSUB"}){
        auto storage=fixture(type,30,true);auto& s=*storage;
        for(int i=0;i<180&&active(s).empty();++i)step(s);
        const auto initial=active(s);if(initial.empty())return fail(type+" interruption fixture did not launch");
        s.behaviors.erase(1);++s.tick;advanceRuntimeEffects(s);
        if(!active(s).empty())return fail(type+" Stop did not cancel preparation");
        RuntimeObjectBehavior attack;attack.kind=RuntimeBehaviorKind::Attack;attack.state=RuntimeBehaviorState::Queued;attack.targetObjectId=2;
        s.behaviors[1]=attack;
        for(int i=0;i<180&&active(s).empty();++i)step(s);
        for(int i=0;i<300;++i){
            const bool released=!active(s).empty()&&std::all_of(s.specialMechanisms.spawnedMissiles().begin(),
                s.specialMechanisms.spawnedMissiles().end(),[](const auto& e){return e.second.released;});
            if(released)break;step(s);
        }
        const auto flying=active(s);if(flying.size()!=initial.size())return fail(type+" cancelled volley did not restart");
        for(int id:flying)if(!s.specialMechanisms.spawnedMissiles().at(id).released)return fail(type+" release timeout");
        s.behaviors.erase(1);for(int i=0;i<3;++i){++s.tick;advanceRuntimeEffects(s);}
        if(active(s)!=flying)return fail(type+" Stop erased independent flying missiles");
        auto other=*findObjectById(s,2);other.instanceId=nextRuntimeObjectInstanceId(s);other.x=23;other.y=12;other.cell=runtimeCellKey(23,12);
        const int otherId=other.instanceId;s.world.accessObjects().push_back(other);attack.targetObjectId=otherId;s.behaviors[1]=attack;
        const auto first=*flying.begin();
        RuntimeObjectExitRequest exit;exit.objectId=first;exit.kind=RuntimeObjectExitKind::Destroyed;
        exit.tick=s.tick;exit.reason="missile volley interception probe";exit.mission="Destroyed";
        queueRuntimeObjectExit(s,std::move(exit));drainRuntimeObjectExitQueue(s);
        if(active(s).contains(first))return fail(type+" interception retained missile authority");
        const auto survivors=active(s);
        if(!survivors.empty()){
            auto snapshot=roundtrip(s,type+"-partial-interception");if(!snapshot)return fail(error);++saves;
            for(int i=0;i<120&&!active(s).empty();++i){
                const auto before=active(s);step(s);
                for(int id:active(s))if(!before.contains(id))return fail(type+" retarget bypassed surviving missile");
            }
        }
        for(int id:active(s)){
            RuntimeObjectExitRequest last;last.objectId=id;last.kind=RuntimeObjectExitKind::Destroyed;last.tick=s.tick;
            last.reason="last missile interception probe";last.mission="Destroyed";
            queueRuntimeObjectExit(s,std::move(last));drainRuntimeObjectExitQueue(s);
        }
        const int oldTargetHealth=findObjectById(s,2)->health;
        for(int i=0;i<1200&&active(s).empty();++i)step(s);
        if(active(s).empty())return fail(type+" did not recover after the last interception");
        for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles())if(m.parentObjectId==1&&m.targetObjectId!=otherId)
            return fail(type+" next volley ignored replacement target");
        if(findObjectById(s,2)->health!=oldTargetHealth)return fail(type+" intercepted missile caused ghost damage");
        ++interruptions;
    }
    int independentParents=0,parentDeaths=0,ownerChanges=0;
    const auto remove=[&](RuntimeWindowState& s,int id){
        RuntimeObjectExitRequest request;request.objectId=id;request.kind=RuntimeObjectExitKind::Destroyed;
        request.tick=s.tick;request.reason="missile volley lifecycle probe";request.mission="Destroyed";
        queueRuntimeObjectExit(s,std::move(request));drainRuntimeObjectExitQueue(s);
    };
    const auto waitReleased=[&](RuntimeWindowState& s){
        for(int i=0;i<400;++i){
            step(s);
            const auto ids=active(s);
            if(!ids.empty()&&std::all_of(ids.begin(),ids.end(),[&](int id){
                return s.specialMechanisms.spawnedMissiles().at(id).released;}))return true;
        }
        return false;
    };
    for(const std::string type:{"DRED","V3","BSUB"}){
        const auto* weapon=live.rules.findWeaponType(type=="DRED"?"DredLauncher":type=="V3"?"V3Launcher":"CruiseLauncher");
        if(!weapon)return fail(type+" native launcher missing");
        // A second parent must be able to start its own complete volley while
        // the first parent's shots are still flying; the gate is not global.
        auto storage=fixture(type,30,true);auto& s=*storage;
        if(!waitReleased(s))return fail(type+" independent-parent release timeout");
        const auto old=active(s);
        auto second=*findObjectById(s,1);second.instanceId=nextRuntimeObjectInstanceId(s);
        second.y=14;second.cell=runtimeCellKey(second.x,second.y);clearRuntimeRenderPosition(second);
        const int secondId=second.instanceId;s.world.accessObjects().push_back(second);
        s.behaviors[secondId]=s.behaviors.at(1);advanceRuntimeSpecialMechanisms(s);
        if(!launchRuntimeSpawnedMissileVolley(s,*findObjectById(s,secondId),*findObjectById(s,2),*weapon,1.0)
            ||active(s,secondId).size()!=old.size()||active(s)!=old)
            return fail(type+" one parent blocked another");
        ++independentParents;

        // Ownership may change while a shot is in flight. It does not make
        // that parent's existing volley disappear or change shot attribution.
        auto owned=fixture(type,30,true);auto& o=*owned;
        if(!waitReleased(o))return fail(type+" owner-change release timeout");
        const auto committed=active(o);const std::string originalOwner=findObjectById(o,1)->owner;
        findMutableObjectById(o,1)->owner="French";
        o.specialMechanisms.onOwnerChanged(o.world.accessObjects(),1,"French",o.tick,SpecialEventSink{});
        for(int id:committed)if(o.specialMechanisms.spawnedMissiles().at(id).owner!=originalOwner)
            return fail(type+" capture reassigned an airborne missile");
        for(int i=0;i<120&&!active(o).empty();++i){
            if(launchRuntimeSpawnedMissileVolley(o,*findObjectById(o,1),*findObjectById(o,2),*weapon,1.0))
                return fail(type+" ownership change bypassed the previous volley");
            ++o.tick;advanceRuntimeEffects(o);
        }
        ++ownerChanges;

        for(bool released:{false,true}){
            auto dying=fixture(type,30,true);auto& d=*dying;
            if(released){if(!waitReleased(d))return fail(type+" parent-death release timeout");}
            else step(d);
            const auto prior=active(d);if(prior.empty())return fail(type+" parent-death volley missing");
            remove(d,1);d.behaviors.erase(1);
            if(released){
                if(active(d)!=prior)return fail(type+" parent death removed an independent flight");
                auto restored=roundtrip(d,type+"-parent-dead");if(!restored)return fail(error);++saves;
                for(int i=0;i<30*90&&!active(d).empty();++i){
                    ++d.tick;advanceRuntimeEffects(d);++restored->tick;advanceRuntimeEffects(*restored);
                    if(d.specialMechanisms.stateHash()!=restored->specialMechanisms.stateHash()
                        ||findObjectById(d,2)->health!=findObjectById(*restored,2)->health)
                        return fail(type+" parent-dead save continuation diverged");
                    ++comparisons;
                }
                if(!active(d).empty()||findObjectById(d,2)->health>=3000)
                    return fail(type+" independent flight failed to hit after parent death");
            }else if(!active(d).empty())return fail(type+" parent death retained preparing missiles");
            ++parentDeaths;
        }
    }
    std::cout<<"Missile volleys passed: cases="<<cases<<" saves="<<saves<<" continuations="<<comparisons
        <<" readyWhileFlying="<<readyWhileFlying<<" stopRetargetInterception="<<interruptions
        <<" independentParents="<<independentParents<<" parentDeaths="<<parentDeaths<<" ownerChanges="<<ownerChanges<<'\n';
    return true;
}
