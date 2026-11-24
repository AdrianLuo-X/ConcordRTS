#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
namespace chrono_state_facade_detail {
bool trace=false;
int checks=0,worldProbes=0,checkpoints=0;std::uint64_t digest=1469598103934665603ULL;
void require(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_same_v<T,float>)return "f"+std::to_string(std::bit_cast<std::uint32_t>(v))+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
    else if constexpr (std::is_same_v<T,ChronoMoveState>) { return encode(v.objectId)+encode(v.phase)+encode(v.source)+encode(v.destination)+encode(v.sourceLevel)+encode(v.destinationLevel)+encode(v.startTick)+encode(v.completeTick)+encode(v.cooldownUntilTick); }
    else if constexpr (std::is_same_v<T,SuperWeaponObjectLifecycleEvent>) {return encode(v.kind)+encode(v.removalDisposition)+encode(v.tick)+encode(v.objectId)+encode(v.healthBefore)+encode(v.healthAfter)+encode(v.superWeaponInstanceId)+encode(v.sourceOwner)+encode(v.superWeaponId)+encode(v.warheadId)+encode(v.reason)+encode(v.oldCell)+encode(v.newCell)+encode(v.oldOwner)+encode(v.newOwner);}
    else if constexpr (requires { v.voxelDebris;v.parentActorId;v.deathWeaponRequestId; }) { return encode(v.voxelDebris)+encode(v.homingInitialized)+encode(v.homingYaw)+encode(v.homingPitch)+encode(v.actorId)+encode(v.parentActorId)+encode(v.generation)+encode(v.attackerObjectId)+encode(v.targetObjectId)+encode(v.sourceAnchorObjectId)+encode(v.owner)+encode(v.fromCell)+encode(v.toCell)+encode(v.currentCell)+encode(v.hasSourcePosition)+encode(v.sourceX)+encode(v.sourceY)+encode(v.sourceZ)+encode(v.sourceScreenOffsetX)+encode(v.sourceScreenOffsetY)+encode(v.sourceNativeFixedX)+encode(v.sourceNativeFixedY)+encode(v.hasTargetPosition)+encode(v.targetX)+encode(v.targetY)+encode(v.targetZ)+encode(v.fromLevel)+encode(v.toLevel)+encode(v.currentLevel)+encode(v.attackerFacing)+encode(v.targetFacing)+encode(v.spawnTick)+encode(v.impactTick)+encode(v.elapsedTicks)+encode(v.damage)+encode(v.veteranDamageMultiplier)+encode(v.spawnedMissileDamage)+encode(v.spawnedMissileBaseDamage)+encode(v.totalTicks)+encode(v.remainingTicks)+encode(v.lifecycle)+encode(v.lifecycleState)+encode(v.renderIntent)+encode(v.weaponId)+encode(v.weaponSlot)+encode(v.projectileId)+encode(v.projectileImageId)+encode(v.warheadId)+encode(v.weaponAnimId)+encode(v.impactAnimId)+encode(v.trailerAnimId)+encode(v.trailerIntentId)+encode(v.warheadImpactIntentId)+encode(v.reportSoundId)+encode(v.arcing)+encode(v.vertical)+encode(v.acceleration)+encode(v.high)+encode(v.veryHigh)+encode(v.shadow)+encode(v.animates)+encode(v.aa)+encode(v.ag)+encode(v.cluster)+encode(v.bright)+encode(v.laser)+encode(v.houseColorLaser)+encode(v.electricBolt)+encode(v.alternateElectricColor)+encode(v.electricColor)+encode(v.shrapnel)+encode(v.shrapnelReleased)+encode(v.laserDuration)+encode(v.laserThickness)+encode(v.cellSpread)+encode(v.percentAtMax)+encode(v.affectsAllies)+encode(v.affectsEnemies)+encode(v.particleId)+encode(v.progress)+encode(v.currentX)+encode(v.currentY)+encode(v.currentZ)+encode(v.speedCellsPerTick)+encode(v.rot)+encode(v.deathWeaponCascadeId)+encode(v.deathWeaponRequestId)+encode(v.deathWeaponCascadeDepth)+encode(v.launchSourceKind)+encode(v.launchSourceType)+encode(v.launchSourceAuthoredAircraft)+encode(v.launchSourceValidated)+encode(v.airstrikeStrikeId)+encode(v.airstrikePlaneToken)+encode(v.particleDamagePulse)+encode(v.suppressWarheadParticleSpawn);}
    else if constexpr (requires { v.framesLeft;v.orientation;v.spin; }) {return encode(v.enabled)+encode(v.framesLeft)+encode(v.frameRemainder)+encode(v.frameCount)+encode(v.bounces)+encode(v.elasticity)+encode(v.position)+encode(v.velocity)+encode(v.orientation)+encode(v.spin);}
    else if constexpr (requires { v.cell;v.x;v.y;v.level; } && !requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.cell)+encode(v.x)+encode(v.y)+encode(v.level);}
    else if constexpr (requires {v.has_value();v.value();})return v ? "some"+encode(*v):"none";
    else if constexpr (requires { v.kind;v.moveTarget; }) {return encode(v.kind)+encode(v.state)+encode(v.moveTarget)+encode(v.targetObjectId)+encode(v.autoAcquired)+encode(v.forceFire)+encode(v.attackGround)+encode(v.attackBridge)+encode(v.attackBridgeCell)+encode(v.path)+encode(v.pathRequestPending)+encode(v.pendingPathTowardRange)+encode(v.pendingPathStart)+encode(v.pendingPathGoal)+encode(v.pendingHasSharedGoal)+encode(v.pendingSharedGoal)+encode(v.pendingAllowedOccupiedObjectId)+encode(v.pendingTargetObjectId)+encode(v.pendingRangeCells)+encode(v.pendingAllowNearbyTarget)+encode(v.pendingIgnoredObjectIds)+encode(v.repackMoveGroup)+encode(v.moveCooldownTicks)+encode(v.moveSegmentFrom)+encode(v.moveIntervalRemainderQ16)+encode(v.moveStartupDistanceQ16)+encode(v.renderStartupAccelerationDistance)+encode(v.factoryExitClearanceHostId)+encode(v.transportTransferHostId)+encode(v.transportTransferKind)+encode(v.weaponCooldownTicks)+encode(v.blockedTicks)+encode(v.pathReplans)+encode(v.nextRepathTick)+encode(v.returnToDeployed)+encode(v.approachSlotIndex)+encode(v.renderFrom)+encode(v.renderTo)+encode(v.renderFromOffsetX)+encode(v.renderFromOffsetY)+encode(v.renderToOffsetX)+encode(v.renderToOffsetY)+encode(v.renderFromLevel)+encode(v.renderToLevel)+encode(v.renderFromOnBridge)+encode(v.renderToOnBridge)+encode(v.renderMoveTicksTotal)+encode(v.renderMoveTicksRemaining)+encode(v.renderArrivalDistanceAfter);}
    else if constexpr (requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.instanceId)+encode(v.kind)+encode(v.mapId)+encode(v.owner)+encode(v.type)+encode(v.health)+encode(v.x)+encode(v.y)+encode(v.cell)+encode(v.level)+encode(v.facing)+encode(v.turretFacing)+encode(v.bodyFacingClock)+encode(v.turretFacingClock)+encode(v.subCell)+encode(v.mission)+encode(v.tag)+encode(v.veterancy)+encode(v.veterancyExperience)+encode(v.crateArmorMultiplier)+encode(v.crateFirepowerMultiplier)+encode(v.crateSpeedMultiplier)+encode(v.crateCloakable)+encode(v.harvestAutoSearchSuspended)+encode(v.hasBeenCaptured)+encode(v.onBridge)+encode(v.authoredAircraft)+encode(v.alive)+encode(v.rubble)+encode(v.submerged)+encode(v.cloaked)+encode(v.cloakCooldownTicks)+encode(v.submergeCooldownTicks)+encode(v.deployed)+encode(v.deployStateTicksRemaining)+encode(v.prone)+encode(v.proneTicksRemaining)+encode(v.weaponCooldownTicksRemaining)+encode(v.spriteFiringStartTick)+encode(v.radiationEruptionCooldownTicksRemaining)+encode(v.hasRenderPosition)+encode(v.renderX)+encode(v.renderY)+encode(v.renderMoveActive)+encode(v.renderLevel)+encode(v.renderOnBridge)+encode(v.movementAnimationDistance)+encode(v.buildAnimTicksTotal)+encode(v.buildAnimTicksRemaining)+encode(v.structureRecoilStartTick)+encode(v.chargeTurretStartTick)+encode(v.workAnimationActive)+encode(v.poweredOperational)+encode(v.powerAnimationPausedAtTick)+encode(v.powerAnimationPausedTicks)+encode(v.factoryExitAnimationStartTick)+encode(v.factoryExitAnimationEndTick)+encode(v.factoryExitAirborne)+encode(v.factoryExitHostId)+encode(v.specialCashNextTick)+encode(v.specialCloneDeliveryObjectId)+encode(v.transportPassengerIds)+encode(v.bioReactorAnimationStartTick)+encode(v.bioReactorAnimationPoweredStartTick)+encode(v.activeGunnerMode)+encode(v.activeGunnerWeaponIndex)+encode(v.activeGunnerTurretIndex)+encode(v.tankBunkerHostId)+encode(v.tankBunkerOccupantId)+encode(v.tankBunkerAnimationStartTick)+encode(v.tankBunkerLowering)+encode(v.dockedToObjectId)+encode(v.dockIndex)+encode(v.airborne)+encode(v.flightLevel)+encode(v.aircraftDockFlightProgress)+encode(v.aircraftBuildingClearanceLevel)+encode(v.wallConnectionMask)+encode(v.garrisonHostObjectId)+encode(v.garrisonOccupantIds)+encode(v.garrisonFireCursor)+encode(v.currentAmmo)+encode(v.pendingAircraftAuxSounds)+encode(v.turretSoundActivityTick)+encode(v.activeTurretSoundEvent)+encode(v.pendingTurretSounds)+encode(v.modExtensions);}
    else if constexpr (requires { v.elapsedNumerator;v.lastFacing; }) {return encode(v.initial)+encode(v.target)+encode(v.rate)+encode(v.elapsedNumerator)+encode(v.lastTick)+encode(v.lastFacing);}
    else if constexpr (requires { v.technoType;v.shieldType;v.effects; }) {return encode(v.initialized)+encode(v.technoType)+encode(v.shieldType)+encode(v.shieldCapacity)+encode(v.shieldHealth)+encode(v.shieldTimer)+encode(v.shieldOnline)+encode(v.innateDelayTicks)+encode(v.effects);}
    else if constexpr (requires { v.sourceType;v.fromWarhead; }) {return encode(v.sourceType)+encode(v.fromWarhead)+encode(v.remainingTicks)+encode(v.elapsedTicks);}
    else if constexpr (requires { v.event;v.takingOff; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.takingOff);}
    else if constexpr (requires { v.event;v.rotating; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.rotating);}
    else if constexpr (requires { v.center;v.members; }) {return encode(v.center)+encode(v.members);}
    else if constexpr (requires { v.x;v.y;v.level; }) {return encode(v.x)+encode(v.y)+encode(v.level);}
    else if constexpr (requires { v.x;v.y; }) {return encode(v.x)+encode(v.y);}
    else if constexpr (requires {v.begin();v.end();}) {
        std::vector<std::string> parts;for(const auto& item:v)parts.push_back(encode(item));
        if constexpr (requires {typename T::key_type;})std::sort(parts.begin(),parts.end());
        std::string out="n"+std::to_string(parts.size())+":";for(const auto& part:parts)out+=std::to_string(part.size())+":"+part;return out;
    } else static_assert(sizeof(T)==0,"unencoded complete field");
}
template<class T> void mutateEncoderField(T& v) {
    if constexpr(std::is_same_v<T,bool>) v=!v;
    else if constexpr(std::is_enum_v<T>) v=static_cast<T>(static_cast<int>(v)==0?1:0);
    else if constexpr(std::is_integral_v<T>) v=v==0?1:0;
    else if constexpr(std::is_floating_point_v<T>) {
        using U=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;
        v=std::bit_cast<T>(std::bit_cast<U>(v)^U{1});
    } else if constexpr(std::is_same_v<T,std::string>) v += "encoder-probe";
    else if constexpr(requires {v.elapsedNumerator;v.lastFacing;}) v.lastFacing=v.lastFacing==0?1:0;
    else if constexpr(requires {v.initialized;v.technoType;v.shieldType;}) v.initialized=!v.initialized;
    else if constexpr(requires {v.empty();v.clear();v.emplace_back();}) {if(v.empty())v.emplace_back();else v.clear();}
    else if constexpr(requires {v.x;v.y;}) v.x=v.x==0?1:0;
    else static_assert(sizeof(T)==0,"unprobed canonical WorldObject field");
}


std::string renderImage(const std::vector<SpecialMechanismRenderSnapshot>& snapshots){std::string out;for(const auto& v:snapshots){out+=encode(v.kind)+encode(v.instanceId)+encode(v.objectId)+encode(v.targetObjectId)+encode(v.superWeaponId)+encode(v.sourceCell)+encode(v.targetCell)+encode(v.cell)+encode(v.cell2)+encode(v.sourceX)+encode(v.sourceY)+encode(v.targetX)+encode(v.targetY)+encode(v.sourceLevel)+encode(v.targetLevel)+encode(v.sourceScreenOffsetX)+encode(v.sourceScreenOffsetY)+encode(v.sourceNativeFixedX)+encode(v.sourceNativeFixedY)+encode(v.hasBeamEndpoints)+encode(v.missilePitch)+encode(v.missileFacing)+encode(v.radius)+encode(v.intensity)+encode(v.maximumIntensity)+encode(v.progress)+encode(v.requiredProgress)+encode(v.expireTick)+encode(v.owner)+encode(v.color)+encode(v.phase)+encode(v.resourceId); }return out;}
std::string save(const RuntimeSpecialMechanismState& state){std::ostringstream out;state.save(out);return out.str();}
void checkpoint(const std::string& label,const RuntimeSpecialMechanismState& state,const std::vector<WorldObjectInstance>& objects,int tick){
 std::string out=encode(label)+encode(save(state))+encode(state.stateHash())+encode(state.hasActiveChronoTemporalStates())+encode(objects)+renderImage(state.renderSnapshots(tick,30));
 for(int id:{-1,41,52,63,99}){const auto* p=state.chronoMoveState(id);out+=encode(id)+encode(p!=nullptr)+(p?encode(*p):"")+encode(state.isChronoInTransit(id))+encode(state.isChronoMovementLocked(id,tick));}
 out+='\n';for(unsigned char c:out){digest^=c;digest*=1099511628211ULL;}++checkpoints;
 if(trace)std::cout<<out;
}
bool runContract(){try{
 IniFile rulesIni;require(rulesIni.loadFromString("[InfantryTypes]\n0=TELE\n[VehicleTypes]\n0=TELE\n[TELE]\nTeleporter=yes\nStrength=100\n"),"rules text");RulesDatabase rules;require(rules.loadFromIni(rulesIni),"rules parse");
 for(int scenario=0;scenario<32;++scenario){SpecialMechanismRules cfg;cfg.chronoDelay=scenario%5;cfg.chronoReinforcementDelay=scenario%7;cfg.chronoMinimumDelay=scenario%3;cfg.chronoTrigger=(scenario&1)==0;cfg.chronoDistanceFactor=16+scenario;cfg.chronoRangeMinimum=48;
 RuntimeSpecialMechanismState state;state.reset(cfg);std::vector<WorldObjectInstance> objects;
 for(int id:{41,63,52}){WorldObjectInstance obj;obj.instanceId=id;obj.kind=WorldObjectKind::Unit;obj.type="TELE";obj.owner="home";obj.alive=true;obj.health=100;obj.x=id%11;obj.y=id%7;obj.cell=obj.y*512+obj.x;obj.level=2;obj.mission="Move";obj.hasRenderPosition=true;obj.renderMoveActive=true;obj.renderX=3.5;obj.renderY=-0.0;obj.renderLevel=-1.25;objects.push_back(obj);}
 checkpoint("reset:"+std::to_string(scenario),state,objects,100);
 state.beginChronosphereRecovery(objects[0],100,scenario%2?15:30);state.beginChronoReinforcementRecovery(objects[1],100,scenario%2?30:60);checkpoint("arrivals",state,objects,100);
 std::string reason="preserved";SpecialEventSink emit=[&](const SpecialMechanismEvent& e){const auto event=encode(e.kind)+encode(e.tick)+encode(e.objectId)+encode(e.targetObjectId)+encode(e.value)+encode(e.cell)+encode(e.reason);for(unsigned char c:event){digest^=c;digest*=1099511628211ULL;}};
 auto legal=[&](int,int,int){return (scenario&2)==0;};auto occupied=[&](int,int,int){return (scenario&4)!=0;};
 (void)state.requestChronoMove(objects,rules,52,{18,-7},4,100,legal,occupied,emit,reason,30);checkpoint("request:"+reason,state,objects,100);
 auto copied=state;const auto* borrow=state.chronoMoveState(41);if(borrow){require(copied.chronoMoveState(41)!=borrow,"copy distinct owned map");const auto bytes=encode(*borrow);state.beginChronoReinforcementRecovery(objects[2],100,30);require(state.chronoMoveState(41)==borrow&&encode(*borrow)==bytes,"other insertion preserves borrow");}
 checkpoint("copy-original",state,objects,100);checkpoint("copy-owned",copied,objects,100);
 RuntimeSpecialMechanismState swapped;swapped.reset(cfg);swapped.swap(state);require(!state.hasActiveChronoTemporalStates(),"swap empties old aggregate");if(borrow)require(swapped.chronoMoveState(41)==borrow,"aggregate swap retains borrow");state.swap(swapped);checkpoint("swap-return",state,objects,100);
 auto raw=save(state);IniFile ini;require(ini.loadFromString(raw),"aggregate save parse");RuntimeSpecialMechanismState loaded;loaded.reset(cfg);std::string error="old error";require(loaded.load(ini,error),"whole aggregate load");require(save(loaded)==raw&&loaded.stateHash()==state.stateHash(),"whole save/hash readback exact");checkpoint("loaded",loaded,objects,100);
 for(int bad=0;bad<4;++bad){std::string text=bad==0?"[SpecialMechanisms]\nVersion=99\n":raw+(bad==1?"\n[SpecialChrono]\nCount=1\n0=truncated\n":bad==2?"\n[SpecialTemporal]\nCount=1\n0=truncated\n":"\n[SpecialElectricDisable]\nCount=1\n0=truncated\n");IniFile invalid;require(invalid.loadFromString(text),"invalid semantic fixture parses");const auto before=save(loaded);const auto oldHash=loaded.stateHash();error="untouched";require(!loaded.load(invalid,error),"malformed whole load rejects");require(save(loaded)==before&&loaded.stateHash()==oldHash,"failed load restores all original state");checkpoint("rollback:"+error,loaded,objects,100);}
 for(int tick:{100,101,102,104,110,140}){if((scenario&8)&&tick==101){objects[0].alive=false;objects[0].mission="InsideTransport";objects[2].transportPassengerIds={41};}if((scenario&16)&&tick==102)objects[2].health=0;
 (void)state.tickChronoMoves(objects,tick,legal,occupied,emit);checkpoint("tick:"+std::to_string(tick),state,objects,tick);}
 (void)state.cancelChronoMoveInTransit(52);state.onObjectRemoved(objects,41,141,emit,"chrono facade contract");checkpoint("remove",state,objects,141);state.reset(cfg);checkpoint("reset-end",state,objects,141);
 }
 require(checkpoints==608&&digest==2039598514301886609ULL,"frozen original full save/hash/render/rollback checkpoints");
 std::cout<<"chrono-state-facade checkpoints="<<checkpoints<<" render-fields=32 digest="<<digest<<" checks="<<checks<<" failures=0\n";return true;
 }catch(const std::exception& e){std::cerr<<"chrono-state-facade: "<<e.what()<<'\n';return false;}}
}
