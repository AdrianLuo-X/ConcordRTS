// Test-only original full Temporal handlers on the actual independent production Root.
namespace h02402_private_qualification {
using namespace concord::runtime::session_detail;
using h023_private_qualification::encode;
int checks=0,states=0,positiveProgress=0,completions=0,interruptions=0,zeroFrame=0,unitLostClock=0;
std::string where;
void require(bool v,const std::string& why){++checks;if(!v)throw std::runtime_error(where+why);}
namespace original {
bool advanceRuntimeTemporalAttacks(RuntimeProductionHostState& state) {
    if (state.specialMechanisms.temporalTargets().empty()) return false;
    // Work from IDs: completion and interruption mutate the relationship map.
    const auto targets = state.specialMechanisms.temporalTargets();
    const auto sink = runtimeSpecialEventSink(state);
    bool changed = false;
    bool playerObjectLost = false;
    for (const auto& [targetId, temporal] : targets) {
        const int previousTick = temporal.lastAdvancedTick < 0
            ? std::max(0, state.tick - 1) : temporal.lastAdvancedTick;
        if (!state.specialMechanisms.beginTemporalTick(targetId, state.tick)) continue;
        const auto frames = runtimeRuleFrameAtTick(state.tick, state.config.fixedTickHz)
            - runtimeRuleFrameAtTick(previousTick, state.config.fixedTickHz);
        for (const auto& [attackerId, unusedProgress] : temporal.progressByAttacker) {
            const auto* target = findObjectById(state, targetId);
            const auto* source = findRuntimeObjectByIdIncludingInactive(state, attackerId);
            const WorldObjectInstance* host = nullptr;
            const auto explicitOrder = state.behaviors.find(attackerId);
            const bool forceFire = explicitOrder != state.behaviors.end()
                && explicitOrder->second.kind == RuntimeBehaviorKind::Attack
                && explicitOrder->second.targetObjectId == targetId && explicitOrder->second.forceFire;
            bool valid = source && target && target->health > 0 && runtimeCombatCanAct(state, *source)
                && !runtimeCarrierAircraftHasDeckProtection(state, *target)
                && (forceFire || !runtimeOwnersAreAllied(state, source->owner, target->owner));
            if (valid && !source->alive) {
                const auto cursor = state.combatDetails.fireCursors.find(attackerId);
                if (cursor != state.combatDetails.fireCursors.end()
                    && cursor->second.lastTargetObjectId == targetId)
                    host = findObjectById(state, cursor->second.sourceAnchorObjectId);
                valid = host && runtimeCombatValidPassenger(*source, *host) && runtimeCombatCanAct(state, *host);
            } else if (valid) {
                const auto order = state.behaviors.find(attackerId);
                const auto cursor = state.combatDetails.fireCursors.find(attackerId);
                const auto* unit = findRuntimeUnitType(state.rules, *source);
                const bool independentMoveFire = unit && unit->opportunityFire && !unit->noMovingFire
                    && order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Move
                    && cursor != state.combatDetails.fireCursors.end()
                    && cursor->second.lastTargetObjectId == targetId;
                valid = order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack
                    && order->second.targetObjectId == targetId;
                valid = valid || independentMoveFire;
            }
            const auto hostOrder = host == nullptr ? state.behaviors.end()
                : state.behaviors.find(host->instanceId);
            const bool profileForceFire = forceFire
                || (hostOrder != state.behaviors.end()
                    && hostOrder->second.kind == RuntimeBehaviorKind::Attack
                    && hostOrder->second.targetObjectId == targetId
                    && hostOrder->second.forceFire);
            const auto profile = !valid ? RuntimeWeaponProfile{}
                : host ? runtimeCombatPassengerWeaponProfile(state, *source, target, profileForceFire)
                : runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
            valid = valid && profile.weapon && profile.warhead && profile.warhead->temporal;
            if (valid) {
                const auto pose = runtimeCombatFiringPose(*source, host);
                const double range = host ? state.rules.getCombatDetailRules().openToppedWarpDistance
                    : runtimeEffectiveWeaponRange(state, pose, *target, *profile.weapon);
                const double distance = host
                    ? runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(*host), *target)
                    : runtimeWeaponDistanceToTarget(state, pose, *target, *profile.weapon);
                valid = distance <= range && distance + 0.001 >= profile.weapon->minimumRange
                    && runtimeObjectVisibleToOwner(state, source->owner, *target)
                    && runtimeWeaponHasLineOfSight(state, pose, *target, *profile.weapon);
            }
            if (!valid) {
                state.specialMechanisms.clearTemporalAttacker(attackerId, targetId, state.tick,
                    sink, "temporal source or target no longer available");
                changed = true;
                continue;
            }
            if (frames <= 0) continue;
            // Temporal ignores armor verses, difficulty health and ordinary
            // firepower bonuses. ElitePrimary supplies its own authored Damage.
            const int progress = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), std::max(0, profile.weapon->damage) * frames));
            bool completed = false;
            std::string reason;
            changed = state.specialMechanisms.applyTemporalHit(state.world.accessObjects(), state.rules,
                attackerId, targetId, progress, temporal.requiredProgress, state.tick,
                [&state](const std::string& a, const std::string& b) { return runtimeOwnersAreAllied(state, a, b); },
                sink, completed, reason, forceFire) || changed;
            if (completed) {
                playerObjectLost = playerObjectLost || (target->kind != WorldObjectKind::Structure && target->owner == state.economy.owner);
                RuntimeObjectExitRequest exit;
                exit.objectId = targetId;
                exit.kind = RuntimeObjectExitKind::TemporalDestroyed;
                exit.tick = state.tick;
                exit.mission = "TemporalRemoved";
                exit.reason = "temporal continuous erasure complete";
                exit.sourceObjectId = attackerId;
                exit.sourceOwner = source->owner;
                exit.suppressDeathWeapon = true;
                queueRuntimeObjectExit(state, std::move(exit));
                break;
            }
        }
    }
    changed = drainRuntimeObjectExitQueue(state) || changed;
    if (playerObjectLost && state.tick - state.runtimeLastUnitLostSoundTick
            >= std::max(1, state.config.fixedTickHz * 2)) {
        emitRuntimeSound(state, runtimeSoundIntentUnitLost);
        state.runtimeLastUnitLostSoundTick = state.tick;
    }
    return changed;
}

void clearRuntimeTemporalAttack(
    RuntimeProductionHostState& state,
    int attackerObjectId,
    const RuntimeObjectBehavior& behavior,
    const std::string& reason) {
    if (!state.specialMechanismsInitialized
        || behavior.kind != RuntimeBehaviorKind::Attack
        || behavior.targetObjectId < 0) {
        return;
    }
    state.specialMechanisms.clearTemporalAttacker(
        attackerObjectId,
        behavior.targetObjectId,
        state.tick,
        runtimeSpecialEventSink(state),
        reason
    );
}


}
std::string temporalImage(RuntimeProductionHostState& s){
 require(s.pendingObjectExits.empty(),"all original queued exits drained");
 return h02305_private_qualification::warpImage(s)+encode(s.runtimeLastUnitLostSoundTick)+encode(s.actionSummary.completedCommands);
}
std::unique_ptr<h023_private_qualification::ChronoIndependentRecoveryHost> scene(const std::string& mapPath,int hz,int scenario,int count){
 auto box=std::make_unique<h023_private_qualification::ChronoIndependentRecoveryHost>();auto& s=*box;std::string error;
 require(configureRuntimeDetailSnapshotSmokeWorld(s,false,error),"original actual Root scene: "+error);
 s.config.defaultMapPath=mapPath;s.report.map.path=mapPath;s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerCountry="Americans";s.config.multiplayerActive=false;s.config.computerAiEnabled=false;s.config.noCooldown=false;
 s.tick=100;s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
 const std::string text=R"H24([InfantryTypes]
0=H24_SOURCE
[VehicleTypes]
0=H24_TARGET
1=H24_HOST
[BuildingTypes]
0=H24_BUILD
[H24_SOURCE]
Strength=100
Speed=6
Primary=H24_BEAM
OpportunityFire=yes
NoMovingFire=no
[H24_TARGET]
Strength=1000
Armor=none
Speed=6
[H24_HOST]
Strength=200
Speed=6
Passengers=3
SizeLimit=6
OpenTopped=yes
[H24_BUILD]
Strength=1000
Armor=wood
Foundation=1x1
[H24_BEAM]
Damage=7
ROF=1
Range=6
Projectile=H24_PROJECTILE
Warhead=H24_WARHEAD
FireInTransport=yes
RadBeam=yes
[H24_PROJECTILE]
Invisible=yes
AG=yes
AA=yes
[H24_WARHEAD]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
Temporal=yes
)H24";
 IniFile ini;require(ini.loadFromString(text)&&s.rules.loadFromIni(ini),"real typed Temporal weapon rules");s.rulesLoaded=true;
 auto& tuning=const_cast<SpecialMechanismRules&>(s.rules.getSpecialMechanismRules());tuning.warpAwayAnim.clear();
 s.world.accessObjects().clear();s.behaviors.clear();s.pendingTransportTargets.clear();s.projectiles.clear();s.impactEffects.clear();s.pendingObjectExits.clear();s.wrecks.clear();s.buildingCollapses.clear();s.deathWeapons.reset();s.combatDetails=RuntimeCombatDetailsState{};s.economy.owner="Americans";
 auto add=[&](int id,WorldObjectKind kind,const std::string& type,const std::string& owner,int x,int y){WorldObjectInstance o;o.instanceId=id;o.kind=kind;o.type=type;o.owner=owner;o.health=kind==WorldObjectKind::Infantry?100:1000;o.alive=true;o.x=x;o.y=y;o.cell=runtimeCellKey(x,y);o.level=0;o.mission="Attack";s.world.accessObjects().push_back(std::move(o));};
 for(int n=0;n<count;++n)add(41+n,WorldObjectKind::Infantry,"H24_SOURCE","Americans",5,6+n);
 add(52,WorldObjectKind::Unit,"H24_TARGET","Russians",6,6);add(60,WorldObjectKind::Unit,"H24_HOST","Americans",5,6);add(70,WorldObjectKind::Unit,"H24_TARGET","Russians",7,6);
 for(int n=0;n<count;++n){RuntimeObjectBehavior b;b.kind=RuntimeBehaviorKind::Attack;b.state=RuntimeBehaviorState::Attacking;b.targetObjectId=52;b.forceFire=false;s.behaviors.emplace(41+n,b);s.combatDetails.fireCursors[41+n].lastTargetObjectId=52;s.combatDetails.fireCursors[41+n].sourceAnchorObjectId=60;}
 RuntimeObjectBehavior hb;hb.kind=RuntimeBehaviorKind::Attack;hb.targetObjectId=52;s.behaviors.emplace(60,hb);
 s.specialMechanisms.reset(s.rules.getSpecialMechanismRules());s.specialMechanismsInitialized=true;
 for(int n=0;n<count;++n){bool completed=false;std::string why;require(s.specialMechanisms.applyTemporalHit(s.world.accessObjects(),s.rules,41+n,52,1,200,s.tick-1,[](const std::string&a,const std::string&b){return a==b;},SpecialEventSink{},completed,why),"seed actual canonical Temporal "+why);require(!completed,"initial target not erased");}
 auto& record=const_cast<std::map<int,TemporalTargetState>&>(s.specialMechanisms.temporalTargets()).at(52);record.lastAdvancedTick=s.tick-1;
 auto* target=findMutableObjectById(s,52);auto* source=findMutableObjectById(s,41);require(target&&source,"real source andtarget identities");
 switch(scenario){
 case 1:record.requiredProgress=1;break;
 case 2:target->owner="Americans";break;
 case 3:target->owner="Americans";for(auto&[id,b]:s.behaviors)if(id<60)b.forceFire=true;break;
 case 4:target->health=0;break;
 case 5:source->health=0;break;
 case 6:s.behaviors.at(41).kind=RuntimeBehaviorKind::Move;break;
 case 7:s.behaviors.at(41).kind=RuntimeBehaviorKind::Move;const_cast<UnitType*>(s.rules.findUnitType("InfantryType","H24_SOURCE"))->noMovingFire=true;break;
 case 8:case 9:source->alive=false;source->garrisonHostObjectId=60;source->mission="InsideTransport";findMutableObjectById(s,60)->transportPassengerIds={41};if(scenario==9)s.combatDetails.fireCursors[41].lastTargetObjectId=99;break;
 case 10:const_cast<WarheadType*>(s.rules.findWarheadType("H24_WARHEAD"))->temporal=false;break;
 case 11:source->type="UNKNOWN_SOURCE_TYPE";break;
 case 12:s.tick=101;s.config.fixedTickHz=1000;record.lastAdvancedTick=100;break;
 case 13:record.lastAdvancedTick=s.tick;break;
 case 14:{bool complete=false;std::string why;require(s.specialMechanisms.applyTemporalHit(s.world.accessObjects(),s.rules,41,70,1,200,s.tick-1,[](const std::string&a,const std::string&b){return a==b;},SpecialEventSink{},complete,why),"second original canonical target");break;}
 case 15:target->kind=WorldObjectKind::Structure;target->type="H24_BUILD";record.requiredProgress=1;break;
 case 16:target->owner="Americans";record.requiredProgress=1;for(auto&[id,b]:s.behaviors)if(id<60)b.forceFire=true;s.runtimeLastUnitLostSoundTick=-10000;break;
 case 17:target->owner="Americans";record.requiredProgress=1;for(auto&[id,b]:s.behaviors)if(id<60)b.forceFire=true;s.runtimeLastUnitLostSoundTick=s.tick;break;
 }
 s.particles.reset(s.config.fixedTickHz);require(s.particles.alignEmptyStateTick(s.tick),"actual particleclock");s.particlesInitialized=true;
 s.superWeapons.reset(std::vector<SuperWeaponRule>{});require(s.superWeapons.alignEmptyStateTick(s.tick),"actual superweaponclock");s.superWeaponsInitialized=true;
 return box;
}
std::string exercise(bool reference,const std::string& mapPath,int hz,int scenario,int count){
 where="reference="+std::to_string(reference)+" hz="+std::to_string(hz)+" scenario="+std::to_string(scenario)+" attackers="+std::to_string(count)+" ";auto box=scene(mapPath,hz,scenario,count);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
 const auto before=temporalImage(s);std::string all=before;const auto beforeTemporal=s.specialMechanisms.temporalTargets();
 const bool changed=reference?original::advanceRuntimeTemporalAttacks(s):advanceRuntimeTemporalAttacks(s);all+=encode(changed)+temporalImage(s);
 if(scenario==0){require(s.specialMechanisms.hasTemporalAttacker(41,52),"actual ordinary Temporal authority retained");const auto& t=s.specialMechanisms.temporalTargets().at(52);require(t.progressByAttacker.at(41)>beforeTemporal.at(52).progressByAttacker.at(41),"actual authored beam advanced");++positiveProgress;}
 if(scenario==1||scenario==15||scenario==16||scenario==17){require(findObjectById(s,52)==nullptr,"real completion commits objectexit");++completions;}
 if(scenario==2){require(!s.specialMechanisms.hasTemporalAttacker(41,52),"real allied ordinary source interrupted");++interruptions;}
 if(scenario==12){require(!changed&&s.specialMechanisms.temporalTargets().at(52).progressByAttacker==beforeTemporal.at(52).progressByAttacker,"zero-frame keeps original validation butno progress");++zeroFrame;}
 if(scenario==16){require(s.runtimeLastUnitLostSoundTick==s.tick,"actual unitlost sharedclock committed");++unitLostClock;}
 for(int step=0;step<3;++step){++s.tick;require(s.particles.alignEmptyStateTick(s.tick)&&s.superWeapons.alignEmptyStateTick(s.tick),"empty clocks continuation");all+=encode(reference?original::advanceRuntimeTemporalAttacks(s):advanceRuntimeTemporalAttacks(s))+temporalImage(s);}
 for(int source:{41,42,43}){RuntimeObjectBehavior behavior;behavior.kind=RuntimeBehaviorKind::Attack;behavior.targetObjectId=52;const auto old=temporalImage(s);if(reference)original::clearRuntimeTemporalAttack(s,source,behavior,s.economy.owner);else clearRuntimeTemporalAttack(s,source,behavior,s.economy.owner);all+=old+temporalImage(s);}
 ++states;return all;
}
int run(const std::string& mapPath){try{
 for(int hz:{15,30,60})for(int scenario=0;scenario<18;++scenario)for(int count:{1,3}){const auto before=exercise(true,mapPath,hz,scenario,count),now=exercise(false,mapPath,hz,scenario,count);require(before==now,"full actual Root save/world87/behavior49/effects/sounds/clock/canonicalcontinuation differs");}
 require(states==216&&positiveProgress==12&&completions==48&&interruptions==12&&zeroFrame==12&&unitLostClock==12,"all mandatory actualRoot states andpositive branches");
 std::cout<<"private-temporal-attack checks="<<checks<<" actual-root-states="<<states<<" positive-progress="<<positiveProgress<<" completed="<<completions<<" interrupted="<<interruptions<<" zero-frame="<<zeroFrame<<" unitlost-clock="<<unitLostClock<<" fullsave-world87-behavior49-canonical-effect-sound-continuation=exact failures=0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"private-temporal-attack FAIL "<<e.what()<<'\n';return 1;}}
}
