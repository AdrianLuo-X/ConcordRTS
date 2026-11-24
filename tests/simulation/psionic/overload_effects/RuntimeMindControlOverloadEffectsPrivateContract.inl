namespace h021_private_qualification {
namespace original {
using Root=RuntimeProductionHostState;
#include "RuntimeMindControlOverloadEffectsOriginal.inl"
}
std::string exercise(bool reference,const std::string& mapPath,int& checks) {
    std::string where;
    const auto require=[&](bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(where+why);};
    std::ostringstream image;
    for(int hz:{15,30,60}) for(int scenario=0;scenario<8;++scenario) {
        where="hz="+std::to_string(hz)+" scenario="+std::to_string(scenario)+" ";
        auto box=std::make_unique<IndependentRecoveryHost>();auto& s=*box;std::string error;
        require(configureRuntimeDetailSnapshotSmokeWorld(s,false,error),"overload scene "+error);
        RuntimeSnapshotActiveStateScope scope(s);
        s.config.defaultMapPath=mapPath;s.config.multiplayerActive=false;s.config.computerAiEnabled=false;
        s.config.gameMode="Battle";s.config.fixedTickHz=hz;s.tick=100;
        s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
        s.economy.owner="Alpha";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.skirmishModeRulesLoaded=true;s.skirmishModeAlliesAllowed=true;s.skirmishTeamByOwner={{"alpha","1"},{"beta","2"}};
        std::string ruleText=R"H21([VehicleTypes]
0=H021_BRAIN
1=H021_TARGET
2=H021_IMMUNE
[H021_BRAIN]
Primary=H021_MIND
Strength=500
Armor=heavy
Speed=6
SpeedType=Track
MovementZone=Normal
[H021_IMMUNE]
Primary=H021_MIND
Strength=500
Armor=heavy
Speed=6
SpeedType=Track
MovementZone=Normal
Immune=yes
[H021_TARGET]
Strength=200
Armor=heavy
Speed=6
SpeedType=Track
MovementZone=Normal
[H021_MIND]
Damage=3
ROF=10
Range=8
InfiniteMindControl=yes
Projectile=H021_PROJECTILE
Warhead=H021_CONTROL
[H021_PROJECTILE]
AG=yes
AA=no
[H021_CONTROL]
MindControl=yes
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
[H021_C4]
Verses=H021_VERSES
[CombatDamage]
C4Warhead=H021_C4
OverloadCount=3,6,10,50
OverloadDamage=0,50,100,500
OverloadFrames=30,60,60,60
[AudioVisual]
MasterMindOverloadDeathSound=H021_OVERLOAD
[SuperWeaponTypes]
0=H021_SHIELD
[H021_SHIELD]
Type=IronCurtain
Action=IronCurtain
TargetMode=Area
IsPowered=no
Radius=1
Duration=500
)H21";
        const std::string verses=scenario==6?"0%,0%,0%,0%,0%,0%,0%,0%,0%,0%,0%":"100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%";
        ruleText.replace(ruleText.find("H021_VERSES"),11,verses);
        IniFile ini;require(ini.loadFromString(ruleText)&&s.rules.loadFromIni(ini),"actual overloaded rules");
        s.rulesLoaded=true;s.specialMechanismsInitialized=false;ensureRuntimeSpecialMechanismsInitialized(s);
        s.particles.reset(hz);require(s.particles.alignEmptyStateTick(s.tick),"particle clock");s.particlesInitialized=true;
        require(s.rules.getSpecialMechanismRules().c4Warhead=="H021_C4"
            &&s.rules.getSpecialMechanismRules().masterMindOverloadDeathSound=="H021_OVERLOAD","actual live rule fields");
        s.world.accessObjects().clear();s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();
        s.impactEffects.clear();s.pendingObjectExits.clear();s.deathWeapons.reset();
        for(auto& terrain:s.world.accessTerrainCells()){terrain.tileIndex=0;terrain.subTileIndex=0;}
        rebuildRuntimeTerrainInfoCache(s);initializeRuntimeNavigation(s);
        const auto add=[&](const std::string& type,const std::string& owner,RuntimeCellPoint cell){
            auto* object=addRuntimeObjectInstanceForOwner(s,owner,RuntimeBuildKind::Unit,type,cell);
            require(object!=nullptr,"actual overload object "+type);return object->instanceId;
        };
        const int brain=add(scenario==2?"H021_IMMUNE":"H021_BRAIN","Alpha",{5,5});
        const int spare=add("H021_TARGET","Beta",{12,12});
        std::vector<int> targets;
        for(int k=0;k<4;++k)targets.push_back(add("H021_TARGET","Beta",{7+k,5}));
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        require(runtimeHasInfiniteMindControl(s.rules,*findObjectById(s,brain)),"real canonical pulse eligibility");
        if(scenario!=0&&scenario!=7)for(int target:targets){
            std::string reason;require(s.specialMechanisms.tryMindControl(s.world.accessObjects(),s.rules,brain,target,3,false,true,false,s.tick,nullptr,nullptr,reason),"actual control link "+reason);
        }
        if(scenario==4)findMutableObjectById(s,brain)->health=40;
        s.superWeapons.reset(s.rules);s.superWeaponsInitialized=scenario==3;
        if(scenario==3){
            SuperWeaponWorldContext context;context.tick=s.tick-1;context.fixedTickHz=hz;
            context.mapWidth=context.mapHeight=16;context.objects=&s.world.accessObjects();context.rules=&s.rules;
            context.cellLegal=[](int,int,SuperWeaponTargetMode){return true;};
            context.ownerPowered=[](const std::string&){return true;};
            require(s.superWeapons.forceReady("Alpha","H021_SHIELD",&context)
                &&s.superWeapons.beginTargetSelection("Alpha","H021_SHIELD",context)
                &&s.superWeapons.submitTarget("Alpha","H021_SHIELD",{{5,5},brain},context),"real shield activation");
            context.tick=s.tick;s.superWeapons.tick(context);
            require(s.superWeapons.isObjectProtected(brain,s.tick),"actual protection—not a stub");
        }
        if(scenario==7){
            RuntimeObjectExitRequest exit;exit.objectId=spare;exit.kind=RuntimeObjectExitKind::Destroyed;
            exit.tick=s.tick;exit.mission="Destroyed";exit.reason="queued-before-empty-overload";
            (void)queueRuntimeObjectExit(s,std::move(exit));
        }
        std::vector<SpecialMechanismEvent> events;
        SpecialEventSink sink=[&](const SpecialMechanismEvent& event){
            events.push_back(event);
            if(scenario==5)findMutableObjectById(s,brain)->health=0;
        };
        const bool changed=reference?original::advanceRuntimeMindControlOverload(s,sink):advanceRuntimeMindControlOverload(s,sink);
        const bool pulse=scenario!=0&&scenario!=7;
        require(events.size()==(pulse?1U:0U),"actual pulse event count");
        const bool fatal=scenario==4||scenario==5;
        const auto* controller=findRuntimeObjectByIdIncludingInactive(s,brain);
        require(controller!=nullptr,"retained actual controller identity");
        if(fatal){
            require(!controller->alive&&controller->health==0,"actual lifecycle finalizes controller");
            for(int target:targets)require(findObjectById(s,target)->owner=="Beta","death releases all nonpermanent links");
            require(s.specialMechanisms.mindControlLinks().empty(),"actual link owner drained");
        }else{
            const int health=scenario==1?450:500;
            require(controller->alive&&controller->health==health,"actual immunity/protection/warhead damage");
        }
        if(pulse){
            const int amount=(scenario==2||scenario==3||scenario==6)?0:50;
            require(events[0].kind=="MindControlOverload"&&events[0].objectId==brain&&events[0].targetObjectId==-1
                &&events[0].tick==s.tick&&events[0].value==amount&&events[0].cell.x==5&&events[0].cell.y==5
                &&events[0].reason=="controlled=4","all seven actual event fields");
            require(s.runtimeSoundEvents>=1&&std::find(s.runtimeSoundIntentHistory.begin(),s.runtimeSoundIntentHistory.end(),"sound:H021_OVERLOAD")!=s.runtimeSoundIntentHistory.end(),"actual intent even damage blocked");
        }
        if(scenario==7)require(!findRuntimeObjectByIdIncludingInactive(s,spare)->alive,"empty pulse still drains prior exit");
        require(s.pendingObjectExits.empty(),"single tail drain leaves no queued exits");
        require(changed==(scenario==1||scenario==4||scenario==5||scenario==7),"original changed result with kernel-only mutation");
        std::ostringstream save;require(writeRuntimeStateToStream(s,save,error),"complete overload save "+error);
        image<<"hz="<<hz<<" scenario="<<scenario<<" changed="<<changed<<" sounds="<<s.runtimeSoundEvents<<'\n';
        for(const auto& event:events)image<<event.kind<<'|'<<event.tick<<'|'<<event.objectId<<'|'<<event.targetObjectId<<'|'<<event.value<<'|'<<event.cell.x<<','<<event.cell.y<<'|'<<event.reason<<'\n';
        for(const auto& sound:s.runtimeSoundIntentHistory)image<<sound<<'\n';
        image<<save.str();
    }
    return image.str();
}
int run(const std::string& mapPath){
    try{int checks=0;const auto before=exercise(true,mapPath,checks),after=exercise(false,mapPath,checks);
        ++checks;if(before!=after)throw std::runtime_error("actual overload original/staged full-state/events/sound differ");
        std::cout<<"private-mind-overload checks="<<checks<<" actual-root-states=48 three-rates=yes kernel-and-protection=real death-release-and-empty-drain=yes full-save-events-sound=exact failures=0\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
}
