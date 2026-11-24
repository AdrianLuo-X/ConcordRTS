namespace h015_private_qualification {
using namespace concord::runtime::session_detail;
void originalCompletion(RuntimeProductionHostState& state,
    const std::vector<SuperWeaponObjectLifecycleEvent>& objects) {
#include "RuntimeDominatorControlCompletionOriginal.inl"
}
std::string exercise(bool reference,const std::string& mapPath,int& checks){
    std::string where;
    const auto require=[&](bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(where+why);};
    std::ostringstream image;
    for(int hz:{15,30,60})for(int scenario=0;scenario<8;++scenario){
        where="hz="+std::to_string(hz)+" scenario="+std::to_string(scenario)+" ";
        auto box=std::make_unique<IndependentRecoveryHost>();auto& s=*box;std::string error;
        require(configureRuntimeDetailSnapshotSmokeWorld(s,false,error),"scene "+error);
        RuntimeSnapshotActiveStateScope scope(s);
        s.config.defaultMapPath=mapPath;s.config.fixedTickHz=hz;s.config.gameMode="Battle";
        s.config.multiplayerActive=false;s.config.computerAiEnabled=false;s.tick=100;
        s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
        s.economy.owner="Alpha";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.skirmishModeRulesLoaded=true;s.skirmishModeAlliesAllowed=true;s.skirmishTeamByOwner={{"alpha","1"},{"beta","2"}};
        IniFile ini;
        require(ini.loadFromString(R"H15([VehicleTypes]
0=H015_TARGET
[H015_TARGET]
Strength=200
Armor=heavy
Speed=6
SpeedType=Track
MovementZone=Normal
)H15")&&s.rules.loadFromIni(ini),"typed actual rules");
        s.rulesLoaded=true;s.specialMechanismsInitialized=false;ensureRuntimeSpecialMechanismsInitialized(s);
        s.particles.reset(hz);require(s.particles.alignEmptyStateTick(s.tick),"particle clock");s.particlesInitialized=true;
        s.world.accessObjects().clear();s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();
        s.impactEffects.clear();s.pendingObjectExits.clear();s.deathWeapons.reset();
        for(auto& terrain:s.world.accessTerrainCells()){terrain.tileIndex=0;terrain.subTileIndex=0;}
        rebuildRuntimeTerrainInfoCache(s);initializeRuntimeNavigation(s);
        const auto add=[&](RuntimeCellPoint cell){
            auto* p=addRuntimeObjectInstanceForOwner(s,"Beta",RuntimeBuildKind::Unit,"H015_TARGET",cell);
            require(p!=nullptr,"actual target producer");return p->instanceId;
        };
        const int first=add({5,5}),second=add({7,5});
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        const std::vector<RuntimeOwnerTransitionRoot> roots{{first,"Beta","Alpha"},{second,"Beta","Alpha"}};
        const auto committed=applyRuntimeOwnerTransitionBatch(s,roots,
            RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits,"psychicDominatorOwnerChanged");
        require(committed.applied,"actual atomic owner transaction "+committed.error);
        require(findObjectById(s,first)->owner=="Alpha"&&findObjectById(s,second)->owner=="Alpha",
            "real owners committed before completion");
        const auto beforeHash=s.specialMechanisms.stateHash();
        std::vector<SuperWeaponObjectLifecycleEvent> events;
        SuperWeaponObjectLifecycleEvent e;e.kind=SuperWeaponObjectLifecycleKind::OwnerChanged;
        e.objectId=first;e.oldOwner="Beta";e.newOwner="Alpha";e.tick=73;
        e.sourceOwner="Alpha";e.superWeaponId="PsychicDominatorSpecial";e.reason="psychic dominator control";
        e.healthBefore=200;e.healthAfter=200;e.oldCell={5,5};e.newCell=e.oldCell;
        if(scenario!=0)events.push_back(e);
        if(scenario==2)events[0].kind=SuperWeaponObjectLifecycleKind::Damaged;
        if(scenario==3)events[0].objectId=999999;
        if(scenario==4)events[0].newOwner="alpha";
        if(scenario==5){
            auto next=e;next.oldOwner="Alpha";next.tick=74;
            events.push_back(next);
        }
        if(scenario==6){
            auto next=e;next.objectId=second;next.tick=75;next.oldCell={7,5};next.newCell=next.oldCell;
            events.insert(events.begin(),SuperWeaponObjectLifecycleEvent{});
            events.push_back(next);
        }
        if(scenario==7){
            auto* p=findMutableObjectById(s,first);p->alive=false;p->health=0;
        }
        if(reference)originalCompletion(s,events);else completeRuntimeDominatorOwnerChanges(s,events);
        const bool accepted=scenario==1||scenario==5||scenario==6;
        const auto& links=s.specialMechanisms.mindControlLinks();
        require(links.size()==(accepted?(scenario==6?2U:1U):0U),"actual canonical link count");
        const auto* target=findRuntimeObjectByIdIncludingInactive(s,first);
        require(target&&target->owner=="Alpha","completion does not rewrite committed owner");
        if(accepted){
            const auto& link=links.at(first);
            require(link.controllerObjectId==-1&&link.targetObjectId==first&&link.controllerOwner=="Alpha"
                &&link.originalOwner==(scenario==5?"Alpha":"Beta")&&link.permanent
                &&link.startedTick==(scenario==5?74:73),"all six actual permanent link fields/last duplicate");
            require(target->mission=="MindControlledPermanent","mission after original canonical link");
            require(s.specialMechanisms.stateHash()!=beforeHash,"actual canonical state hash");
            if(scenario==6){
                const auto& other=links.at(second);
                require(other.controllerObjectId==-1&&other.targetObjectId==second&&other.controllerOwner=="Alpha"
                    &&other.originalOwner=="Beta"&&other.permanent&&other.startedTick==75,"second live target/event fields");
                require(findObjectById(s,second)->mission=="MindControlledPermanent","all accepted targets completed");
            }
        }else{
            require(target->mission!="MindControlledPermanent","skipped completion keeps mission");
            require(s.specialMechanisms.stateHash()==beforeHash,"skipped completion preserves canonical state");
        }
        std::ostringstream save;require(writeRuntimeStateToStream(s,save,error),"complete production save "+error);
        std::ostringstream linkState;s.specialMechanisms.save(linkState);
        RuntimeSpecialMechanismState restored;restored.reset(s.rules.getSpecialMechanismRules());IniFile saved;
        require(saved.loadFromString(linkState.str())&&restored.load(saved,error),"canonical control save/load "+error);
        std::ostringstream loaded;restored.save(loaded);
        require(loaded.str()==linkState.str()&&restored.stateHash()==s.specialMechanisms.stateHash(),"full standard mechanism bytes/hash restored");
        image<<"hz="<<hz<<" scenario="<<scenario<<"\n"<<linkState.str()<<save.str();
    }
    return image.str();
}
int run(const std::string& mapPath){
    try{int checks=0;const auto old=exercise(true,mapPath,checks),now=exercise(false,mapPath,checks);
        ++checks;if(old!=now)throw std::runtime_error("actual original/staged Dom completion full saves differ");
        std::cout<<"private-dominator-completion checks="<<checks
            <<" actual-root-states=48 real-owner-transaction=yes three-rates=yes permanent-link6-mission-save-hash=exact failures=0\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
}
