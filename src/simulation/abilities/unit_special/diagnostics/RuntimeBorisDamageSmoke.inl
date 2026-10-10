#include "simulation/abilities/unit_special/diagnostics/RuntimeBorisBuildingDamageSmoke.inl"

bool runRuntimeBorisDamageSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout<<std::unitbuf;
    const bool baseline=std::getenv("CONCORD_BORIS_DAMAGE_BASELINE")!=nullptr;
    const char* output=std::getenv("CONCORD_BORIS_DAMAGE_OUTPUT");
    const std::string root=output?output:"";
    if(!root.empty())std::filesystem::create_directories(root);
    const auto fail=[&](const std::string& why){error="Boris damage: "+why;return false;};
    int cases=0,saves=0,continuations=0,rejected=0;
    const auto step=[](RuntimeWindowState& s,int callerId){
        ++s.tick;processRuntimePathRequests(s);advanceRuntimeWeaponCooldowns(s);
        auto* b=findMutableObjectById(s,callerId);const auto behavior=s.behaviors.find(callerId);
        if(b&&behavior!=s.behaviors.end()&&behavior->second.kind==RuntimeBehaviorKind::Attack
            &&behavior->second.state!=RuntimeBehaviorState::Complete)advanceRuntimeAttackBehavior(s,*b,behavior->second);
        advanceRuntimeEffects(s);
    };
    const auto salvoState=[](const RuntimeWindowState& s){
        std::ostringstream out;out<<s.airstrikes.stateHash()<<'|'<<s.nextProjectileActorId;
        for(const auto& [id,a]:s.airstrikeRootAuthoritiesByActor){
            out<<'|'<<id<<','<<a.terminalTick<<','<<a.nextFireTick<<','<<a.roundsRemaining;
            for(const auto& r:a.rounds)out<<';'<<r.actorId<<','<<r.launchTick<<','<<r.terminalTick
                <<','<<r.sourceCell.x<<','<<r.sourceCell.y<<','<<r.damage;
        }
        for(const auto& p:s.projectiles)if(p.airstrikeStrikeId>0)
            out<<"/p"<<p.actorId<<','<<p.spawnTick<<','<<p.damage<<','<<p.parentActorId<<','<<p.generation;
        return out.str();
    };
    const auto clone=[&](RuntimeWindowState& s)->std::unique_ptr<RuntimeWindowState>{
        std::ostringstream out;if(!writeRuntimeStateToStream(s,out,error))return nullptr;
        IniFile saved;if(!saved.loadFromString(out.str()))return nullptr;
        auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,false);
        if(!materializeRuntimeLoadStateFromIni(*loaded,saved,error)||salvoState(s)!=salvoState(*loaded))return nullptr;
        ++saves;return loaded;
    };
    for(int hz:{15,30,60})for(int rank:{0,2})for(bool durable:{false,true}) {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;seedRuntimeLoadScratch(s,live,false);
        if(durable){
            IniFile rules,overlay;
            if(!rules.loadFromFile(live.report.rulesPath)||!overlay.loadFromString("[NACNST]\nStrength=100000\n"))return fail("durable fixture rules");
            rules.appendFrom(overlay);if(!s.rules.loadFromIni(rules))return fail("durable fixture load");
        }
        s.tick=100;s.config.fixedTickHz=hz;s.config.noCooldown=false;
        s.config.multiplayerActive=false;s.config.computerAiEnabled=false;s.config.gameMode="Battle";s.campaignMode=false;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.visionEnabled=false;s.economy.owner="Russians";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.behaviors.clear();s.projectiles.clear();s.impactEffects.clear();s.airstrikes.reset();s.deathWeapons.reset();
        s.airstrikePlanesByToken.clear();s.airstrikeRootAuthoritiesByActor.clear();
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear();s.terrainInfoByCell.clear();s.terrainByCell.clear();s.mapFacts={};s.mapFactsLoaded=false;
        for(int y=45;y<=85;++y)for(int x=45;x<=85;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=0;s.world.accessTerrainCells().push_back(c);}
        initializeRuntimeNavigation(s);
        auto* caller=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"BORIS",{60,65});if(!caller)return fail("caller missing");
        caller->owner="Russians";caller->veterancy=rank;caller->mission="Guard";const int callerId=caller->instanceId;
        auto* target=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"NACNST",{66,65});if(!target)return fail("target missing");
        target->owner="Americans";target->mission="Guard";target->buildAnimTicksRemaining=target->buildAnimTicksTotal=0;
        const int id=target->instanceId,initial=target->health;
        advanceRuntimeSpecialMechanisms(s);applySelection(s,{callerId},false,false);issueRuntimeAttackCommand(s,*target);
        if(s.lastCommand.acceptedCount!=1)return fail("actual attack command rejected");
        std::set<int> missiles,planes;int firstLaunch=-1,lastImpact=-1,lastHealth=initial;bool cleared=false;
        std::vector<std::unique_ptr<RuntimeWindowState>> restored;
        bool savedGap=false,savedOverlap=false,savedImpact=false;
        const auto capture=[&](const std::string& stage){
            if(root.empty()||hz!=30||rank!=0||durable)return true;
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{64,65},960,640,error,root+"/boris-"+stage+".bmp",false,false);
            return stats&&stats->rendered;
        };
        if(!capture("before"))return fail(error);
        for(int frame=0;frame<hz*40;++frame){
            step(s,callerId);
            for(auto& loaded:restored){
                step(*loaded,callerId);
                const auto* a=findRuntimeObjectByIdIncludingInactive(s,id);
                const auto* b=findRuntimeObjectByIdIncludingInactive(*loaded,id);
                if(!a||!b||a->health!=b->health||salvoState(s)!=salvoState(*loaded))return fail("save continuation changed at "+std::to_string(s.tick));
                ++continuations;
            }
            for(const auto& [token,binding]:s.airstrikePlanesByToken)planes.insert(token);
            for(const auto& p:s.projectiles)if(p.airstrikeStrikeId>0&&p.generation==0&&missiles.insert(p.actorId).second){
                if(firstLaunch<0)firstLaunch=s.tick;
                std::cout<<"Boris missile hz="<<hz<<" rank="<<rank<<" durable="<<durable<<" tick="<<s.tick
                    <<" plane="<<p.airstrikePlaneToken<<" actor="<<p.actorId<<" damage="<<p.damage<<" weapon="<<p.weaponId<<'\n';
            }
            const auto* t=findRuntimeObjectByIdIncludingInactive(s,id);if(!t)return fail("target identity lost");
            if(t->health!=lastHealth){lastHealth=t->health;lastImpact=s.tick;}
            if(durable&&!baseline){
                bool gap=false,overlap=false,partialImpact=false;
                for(const auto& [actor,a]:s.airstrikeRootAuthoritiesByActor){
                    int pending=0,terminal=0;for(const auto& r:a.rounds){if(r.terminalTick<0)++pending;else ++terminal;}
                    gap=gap||a.roundsRemaining>0;overlap=overlap||pending>1;
                    partialImpact=partialImpact||(terminal>0&&pending>0);
                }
                const bool saveNow=(!savedGap&&gap)||(!savedOverlap&&overlap)||(!savedImpact&&partialImpact);
                if(saveNow){
                    auto loaded=clone(s);if(!loaded)return fail("full salvo restore: "+error);restored.push_back(std::move(loaded));
                    savedGap=savedGap||gap;savedOverlap=savedOverlap||overlap;savedImpact=savedImpact||partialImpact;
                }
                if(hz==30&&rank==0&&overlap&&rejected==0){
                    std::ostringstream text;if(!writeRuntimeStateToStream(s,text,error))return fail("malformed fixture save");
                    IniFile saved;if(!saved.loadFromString(text.str()))return fail("malformed fixture parse");
                    const auto original=splitRuntimeSaveFields(saved.getValue("RuntimeAirstrikeHost","Salvo0",""));
                    if(original.size()<15)return fail("overlap fixture has fewer than two rounds");
                    const auto before=salvoState(s);const auto hash=runtimePersistentTransientStateHash(s);
                    for(int bad=0;bad<7;++bad){
                        auto fields=original;
                        if(bad==0)fields[0]="257";
                        if(bad==1)fields[9]=fields[3];
                        if(bad==2)fields[10]=std::to_string(s.tick+1);
                        if(bad==3)fields[11]=std::to_string(s.tick+1);
                        if(bad==4)fields[12]=std::to_string(std::stoi(fields[12])+1);
                        if(bad==5)fields[14]=std::to_string(std::stoi(fields[14])+1);
                        if(bad==6)fields.pop_back();
                        std::ostringstream record;for(std::size_t i=0;i<fields.size();++i){if(i)record<<'|';record<<fields[i];}
                        const std::string line="Salvo0="+saved.getValue("RuntimeAirstrikeHost","Salvo0","");
                        auto malformed=text.str();const auto offset=malformed.find(line);if(offset==std::string::npos)return fail("salvo record location");
                        malformed.replace(offset,line.size(),"Salvo0="+record.str());
                        const auto file=std::filesystem::path(root.empty()?"build/smoke/feature-set-17/11-boris-damage":root)/("invalid-salvo-"+std::to_string(bad)+".ini");
                        {std::ofstream stream(file,std::ios::binary);stream<<malformed;}
                        std::string reason;
                        if(loadRuntimeState(s,pathString(file),reason)||reason.empty()||salvoState(s)!=before
                            ||runtimePersistentTransientStateHash(s)!=hash)return fail("malformed salvo accepted or mutated live state: "+std::to_string(bad));
                        ++rejected;
                    }
                    auto& a=s.airstrikeRootAuthoritiesByActor.begin()->second;
                    std::string hashError;const auto authoritative=runtimeStandaloneSimulationStateHash(s,hashError);
                    if(authoritative==0)return fail("offline authority fixture: "+hashError);
                    ++a.nextFireTick;
                    if(runtimePersistentTransientStateHash(s)==hash
                        ||runtimeStandaloneSimulationStateHash(s,hashError)==authoritative)return fail("salvo clock omitted from deterministic hashes");
                    --a.nextFireTick;
                }
            }
            if(firstLaunch==s.tick&&!capture("missiles"))return fail(error);
            if(firstLaunch>=0&&s.airstrikes.strikes().empty()&&s.airstrikePlanesByToken.empty()){cleared=true;break;}
        }
        const auto* t=findRuntimeObjectByIdIncludingInactive(s,id);
        std::cout<<"Boris damage hz="<<hz<<" rank="<<rank<<" durable="<<durable<<" initial="<<initial
            <<" remaining="<<t->health<<" planes="<<planes.size()<<" missiles="<<missiles.size()
            <<" lastImpact="<<lastImpact<<" cleared="<<cleared<<'\n';
        if(!cleared||lastImpact<firstLaunch)return fail("sortie did not complete and hit");
        if(!baseline&&durable&&missiles.size()!=(rank==2?8U:4U))return fail("stock Burst=2 is missing rounds");
        if(!baseline&&durable&&(initial-t->health!=(rank==2?2400:1500)||!savedGap||!savedOverlap||!savedImpact))
            return fail("complete salvo damage/save phases missing");
        if(!baseline&&!durable&&t->alive)return fail("stock construction yard survived the completed sortie");
        if(!capture("after"))return fail(error);
        ++cases;
    }
    std::cout<<"Boris damage "<<(baseline?"baseline recorded":"passed")<<": cases="<<cases
        <<" saves="<<saves<<" continuationChecks="<<continuations<<" rejected="<<rejected<<'\n';
    return baseline||runRuntimeBorisBuildingDamageSmoke(live,error);
}
