#include "simulation/abilities/chrono/RuntimeChronoWarp.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/effects/RuntimeParticleSystem.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "world/model/GameWorld.h"
#include <array>
#include <bit>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
namespace chrono_warp_model_detail {
namespace current=concord::simulation::abilities;
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
int checks=0,states=0,worldProbes=0,effectProbes=0,nestedProbes=0,faultCases=0,aliasConfigs=0,aliasFired=0;
void require(bool v,const std::string& why){++checks;if(!v)throw std::runtime_error(why);}
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
    else if constexpr (requires {v.first;v.second;}) {return encode(v.first)+encode(v.second);}
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
struct PlainImpactEffect {
    int projectileActorId = -1;
    int attackerObjectId = -1;
    int targetObjectId = -1;
    bool targetIsStructure = false;
    std::string owner;
    RuntimeCellPoint cell;
    int level = 0;
    bool hasWorldPosition = false;
    double worldX = 0.0;
    double worldY = 0.0;
    double worldLevel = 0.0;
    int screenOffsetX = 0;
    int screenOffsetY = 0;
    int nativeFixedX = 0;
    int nativeFixedY = 0;
    bool bodyCenterAnchored = false;
    RuntimeParticleAirbornePresentation bodyCenterPresentation{};
    int bodyCenterSpriteOffsetY = 0;
    int totalTicks = 8;
    int remainingTicks = 8;
    int radius = 10;
    int frameIndex = -1;
    int facing = -1;
    // Last resolved owner position for a moving passenger's muzzle. Together
    // with worldX/Y/Level it preserves the authored offset across snapshots.
    double muzzleAnchorX = 0.0;
    double muzzleAnchorY = 0.0;
    double muzzleAnchorLevel = 0.0;
    int muzzleFacing = -1;
    int animationFrameCount = 0;
    int animationFrameDelayTicks = 1;
    int animationStartFrame = 0;
    int animationLoopStartFrame = 0;
    int animationLoopEndFrame = 0;
    int animationLoopCount = 1;
    int animationEndFrame = 0;
    int animationSequenceSteps = 0;
    int animationMidpointWidth = 0;
    int animationMidpointHeight = 0;
    int lastSmudgeMidpointStep = -1;
    // Source ownership is independent of the victim's corpse remap color.
    std::string deferredParticleAnimId;
    std::string deferredParticleOwner;
    int deferredParticleTicks = -1;
    bool death = false;
    std::string lifecycleState = "Impact";
    std::string renderIntent = "impact";
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string animId;
    std::string warheadId;
    std::string warheadImpactIntentId;
    std::string trailerIntentId;
    std::string particleId;
    int damage = 0;
    double cellSpread = 0.0;
    bool bright = false;
};
struct RuntimeImpactEffect {
    int projectileActorId = -1;
    int attackerObjectId = -1;
    int targetObjectId = -1;
    bool targetIsStructure = false;
    std::string owner;
    RuntimeCellPoint cell;
    int level = 0;
    bool hasWorldPosition = false;
    double worldX = 0.0;
    double worldY = 0.0;
    double worldLevel = 0.0;
    int screenOffsetX = 0;
    int screenOffsetY = 0;
    int nativeFixedX = 0;
    int nativeFixedY = 0;
    bool bodyCenterAnchored = false;
    RuntimeParticleAirbornePresentation bodyCenterPresentation{};
    int bodyCenterSpriteOffsetY = 0;
    int totalTicks = 8;
    int remainingTicks = 8;
    int radius = 10;
    int frameIndex = -1;
    int facing = -1;
    // Last resolved owner position for a moving passenger's muzzle. Together
    // with worldX/Y/Level it preserves the authored offset across snapshots.
    double muzzleAnchorX = 0.0;
    double muzzleAnchorY = 0.0;
    double muzzleAnchorLevel = 0.0;
    int muzzleFacing = -1;
    int animationFrameCount = 0;
    int animationFrameDelayTicks = 1;
    int animationStartFrame = 0;
    int animationLoopStartFrame = 0;
    int animationLoopEndFrame = 0;
    int animationLoopCount = 1;
    int animationEndFrame = 0;
    int animationSequenceSteps = 0;
    int animationMidpointWidth = 0;
    int animationMidpointHeight = 0;
    int lastSmudgeMidpointStep = -1;
    // Source ownership is independent of the victim's corpse remap color.
    std::string deferredParticleAnimId;
    std::string deferredParticleOwner;
    int deferredParticleTicks = -1;
    bool death = false;
    std::string lifecycleState = "Impact";
    std::string renderIntent = "impact";
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string animId;
    std::string warheadId;
    std::string warheadImpactIntentId;
    std::string trailerIntentId;
    std::string particleId;
    int damage = 0;
    double cellSpread = 0.0;
    bool bright = false;
    RuntimeImpactEffect() noexcept;
    RuntimeImpactEffect(const RuntimeImpactEffect&) = default;
    RuntimeImpactEffect(RuntimeImpactEffect&&) noexcept = default;
    RuntimeImpactEffect& operator=(const RuntimeImpactEffect&) = default;
    RuntimeImpactEffect& operator=(RuntimeImpactEffect&&) noexcept = default;
    ~RuntimeImpactEffect() noexcept;
};
static_assert(sizeof(RuntimeImpactEffect)==sizeof(PlainImpactEffect));
static_assert(alignof(RuntimeImpactEffect)==alignof(PlainImpactEffect));
static_assert(std::is_nothrow_default_constructible_v<RuntimeImpactEffect>);
static_assert(std::is_nothrow_move_constructible_v<RuntimeImpactEffect>);
// A complete presentation value is one field of the original effect.
inline void mutateEncoderField(RuntimeParticleAirbornePresentation& v){v.enabled=!v.enabled;}
std::string roofImage(const RuntimeParticleClearanceRoof& v){return encode(v.cellSum)+encode(v.level)+encode(v.authoredHeight)+encode(v.nativeSpriteHeight)+encode(v.rubble);}
std::string airborneImage(const RuntimeParticleAirbornePresentation& v){std::string out=encode(v.enabled)+encode(v.sourceCellSum)+encode(v.sourceLevel)+encode(v.factoryLiftCancellation)+encode(v.clearanceFraction)+encode(v.resolvedClearanceLevel)+encode(v.roofs.size());for(const auto& r:v.roofs)out+=roofImage(r);return out;}
std::string impactImage(const RuntimeImpactEffect& v){return encode(v.projectileActorId)+encode(v.attackerObjectId)+encode(v.targetObjectId)+encode(v.targetIsStructure)+encode(v.owner)+encode(v.cell)+encode(v.level)+encode(v.hasWorldPosition)+encode(v.worldX)+encode(v.worldY)+encode(v.worldLevel)+encode(v.screenOffsetX)+encode(v.screenOffsetY)+encode(v.nativeFixedX)+encode(v.nativeFixedY)+encode(v.bodyCenterAnchored)+airborneImage(v.bodyCenterPresentation)+encode(v.bodyCenterSpriteOffsetY)+encode(v.totalTicks)+encode(v.remainingTicks)+encode(v.radius)+encode(v.frameIndex)+encode(v.facing)+encode(v.muzzleAnchorX)+encode(v.muzzleAnchorY)+encode(v.muzzleAnchorLevel)+encode(v.muzzleFacing)+encode(v.animationFrameCount)+encode(v.animationFrameDelayTicks)+encode(v.animationStartFrame)+encode(v.animationLoopStartFrame)+encode(v.animationLoopEndFrame)+encode(v.animationLoopCount)+encode(v.animationEndFrame)+encode(v.animationSequenceSteps)+encode(v.animationMidpointWidth)+encode(v.animationMidpointHeight)+encode(v.lastSmudgeMidpointStep)+encode(v.deferredParticleAnimId)+encode(v.deferredParticleOwner)+encode(v.deferredParticleTicks)+encode(v.death)+encode(v.lifecycleState)+encode(v.renderIntent)+encode(v.weaponId)+encode(v.weaponSlot)+encode(v.projectileId)+encode(v.projectileImageId)+encode(v.animId)+encode(v.warheadId)+encode(v.warheadImpactIntentId)+encode(v.trailerIntentId)+encode(v.particleId)+encode(v.damage)+encode(v.cellSpread)+encode(v.bright);}

enum class Step { Mutable=1,Admission,Pending,Object,Cell,Bridge,Ground,Release,Assign,Clear,Refresh,Invalidate,Rules,Hull,Offset,Timeline,TimelineReady,Append,Appended,Unit,Spatial,Sound,Sounded,Construct,Destroy,Caught };
struct Record {Step step;int a=0,b=0,c=0;};
struct InjectedFault {};
struct Config {
 std::string kind="ChronoWarpIn";bool playSound=true,present=true,alive=true,airborne=false,render=true,unitAvailable=true,bridge=false,selfHost=false,hostAvailable=true;
 WorldObjectKind objectKind=WorldObjectKind::Infantry;int admission=1,pending=2,anim=4,sound=2,x=5,y=6,subCell=3,initialEffects=1;
 int failAt=0,aliasStep=0,aliasMode=0,worldField=0;bool scribbleTimeline=false;
};
struct Model;
Model* activeModel=nullptr;
struct PendingTargets {
 Model* owner=nullptr;std::unordered_map<int,int> values;
 auto find(int id) const->std::unordered_map<int,int>::const_iterator;
 auto end() const {return values.end();}
};
struct Effects {
 Model* owner=nullptr;std::vector<RuntimeImpactEffect> values;
 void push_back(RuntimeImpactEffect&& effect);
};
struct ModelRules {
 Model* owner=nullptr;SpecialMechanismRules special;
 const SpecialMechanismRules& getSpecialMechanismRules() const;
};
struct Sound {std::string intent;RuntimeSpatialAudioPosition position;};
struct Model {
 Config config;std::vector<WorldObjectInstance> world;ModelRules rules;PendingTargets pendingTransportTargets;Effects impactEffects;UnitType unit;
 SpecialMechanismEvent event;std::vector<Record> trace;std::vector<Sound> sounds;std::unordered_map<int,int> reservations,occupancy;
 int calls=0,released=0,assigned=0,cleared=0,refreshed=0,invalidated=0,timelines=0,constructs=0,destroys=0,depth=0;bool projectionValid=true,caught=false,aliasDone=false,unitAvailable=true;
 std::array<const void*,8> localEffects{};int rngSentry=74331,tick=100;
 explicit Model(Config c):config(std::move(c)){trace.reserve(256);rules.owner=this;pendingTransportTargets.owner=this;impactEffects.owner=this;}
 int index(const WorldObjectInstance* p) const {for(std::size_t i=0;i<world.size();++i)if(std::addressof(world[i])==p)return static_cast<int>(i);return -1;}
 WorldObjectInstance* lookup(int id){for(auto& o:world)if(o.instanceId==id)return &o;return nullptr;}
 void note(Step s,int a=0,int b=0,int c=0) noexcept {if(trace.size()==trace.capacity())std::terminate();trace.push_back({s,a,b,c});}
 void constructed(const void* p) noexcept {if(depth==static_cast<int>(localEffects.size()))std::terminate();localEffects[depth++]=p;++constructs;note(Step::Construct,depth);}
 void destroyed(const void* p) noexcept {if(depth&&localEffects[depth-1]==p){note(Step::Destroy,depth);--depth;++destroys;}}
 void mutateWorld(){switch(config.worldField){case 0: mutateEncoderField(world[0].instanceId); break;
case 1: mutateEncoderField(world[0].kind); break;
case 2: mutateEncoderField(world[0].mapId); break;
case 3: mutateEncoderField(world[0].owner); break;
case 4: mutateEncoderField(world[0].type); break;
case 5: mutateEncoderField(world[0].health); break;
case 6: mutateEncoderField(world[0].x); break;
case 7: mutateEncoderField(world[0].y); break;
case 8: mutateEncoderField(world[0].cell); break;
case 9: mutateEncoderField(world[0].level); break;
case 10: mutateEncoderField(world[0].facing); break;
case 11: mutateEncoderField(world[0].turretFacing); break;
case 12: mutateEncoderField(world[0].bodyFacingClock); break;
case 13: mutateEncoderField(world[0].turretFacingClock); break;
case 14: mutateEncoderField(world[0].subCell); break;
case 15: mutateEncoderField(world[0].mission); break;
case 16: mutateEncoderField(world[0].tag); break;
case 17: mutateEncoderField(world[0].veterancy); break;
case 18: mutateEncoderField(world[0].veterancyExperience); break;
case 19: mutateEncoderField(world[0].crateArmorMultiplier); break;
case 20: mutateEncoderField(world[0].crateFirepowerMultiplier); break;
case 21: mutateEncoderField(world[0].crateSpeedMultiplier); break;
case 22: mutateEncoderField(world[0].crateCloakable); break;
case 23: mutateEncoderField(world[0].harvestAutoSearchSuspended); break;
case 24: mutateEncoderField(world[0].hasBeenCaptured); break;
case 25: mutateEncoderField(world[0].onBridge); break;
case 26: mutateEncoderField(world[0].authoredAircraft); break;
case 27: mutateEncoderField(world[0].alive); break;
case 28: mutateEncoderField(world[0].rubble); break;
case 29: mutateEncoderField(world[0].submerged); break;
case 30: mutateEncoderField(world[0].cloaked); break;
case 31: mutateEncoderField(world[0].cloakCooldownTicks); break;
case 32: mutateEncoderField(world[0].submergeCooldownTicks); break;
case 33: mutateEncoderField(world[0].deployed); break;
case 34: mutateEncoderField(world[0].deployStateTicksRemaining); break;
case 35: mutateEncoderField(world[0].prone); break;
case 36: mutateEncoderField(world[0].proneTicksRemaining); break;
case 37: mutateEncoderField(world[0].weaponCooldownTicksRemaining); break;
case 38: mutateEncoderField(world[0].spriteFiringStartTick); break;
case 39: mutateEncoderField(world[0].radiationEruptionCooldownTicksRemaining); break;
case 40: mutateEncoderField(world[0].hasRenderPosition); break;
case 41: mutateEncoderField(world[0].renderX); break;
case 42: mutateEncoderField(world[0].renderY); break;
case 43: mutateEncoderField(world[0].renderMoveActive); break;
case 44: mutateEncoderField(world[0].renderLevel); break;
case 45: mutateEncoderField(world[0].renderOnBridge); break;
case 46: mutateEncoderField(world[0].movementAnimationDistance); break;
case 47: mutateEncoderField(world[0].buildAnimTicksTotal); break;
case 48: mutateEncoderField(world[0].buildAnimTicksRemaining); break;
case 49: mutateEncoderField(world[0].structureRecoilStartTick); break;
case 50: mutateEncoderField(world[0].chargeTurretStartTick); break;
case 51: mutateEncoderField(world[0].workAnimationActive); break;
case 52: mutateEncoderField(world[0].poweredOperational); break;
case 53: mutateEncoderField(world[0].powerAnimationPausedAtTick); break;
case 54: mutateEncoderField(world[0].powerAnimationPausedTicks); break;
case 55: mutateEncoderField(world[0].factoryExitAnimationStartTick); break;
case 56: mutateEncoderField(world[0].factoryExitAnimationEndTick); break;
case 57: mutateEncoderField(world[0].factoryExitAirborne); break;
case 58: mutateEncoderField(world[0].factoryExitHostId); break;
case 59: mutateEncoderField(world[0].specialCashNextTick); break;
case 60: mutateEncoderField(world[0].specialCloneDeliveryObjectId); break;
case 61: mutateEncoderField(world[0].transportPassengerIds); break;
case 62: mutateEncoderField(world[0].bioReactorAnimationStartTick); break;
case 63: mutateEncoderField(world[0].bioReactorAnimationPoweredStartTick); break;
case 64: mutateEncoderField(world[0].activeGunnerMode); break;
case 65: mutateEncoderField(world[0].activeGunnerWeaponIndex); break;
case 66: mutateEncoderField(world[0].activeGunnerTurretIndex); break;
case 67: mutateEncoderField(world[0].tankBunkerHostId); break;
case 68: mutateEncoderField(world[0].tankBunkerOccupantId); break;
case 69: mutateEncoderField(world[0].tankBunkerAnimationStartTick); break;
case 70: mutateEncoderField(world[0].tankBunkerLowering); break;
case 71: mutateEncoderField(world[0].dockedToObjectId); break;
case 72: mutateEncoderField(world[0].dockIndex); break;
case 73: mutateEncoderField(world[0].airborne); break;
case 74: mutateEncoderField(world[0].flightLevel); break;
case 75: mutateEncoderField(world[0].aircraftDockFlightProgress); break;
case 76: mutateEncoderField(world[0].aircraftBuildingClearanceLevel); break;
case 77: mutateEncoderField(world[0].wallConnectionMask); break;
case 78: mutateEncoderField(world[0].garrisonHostObjectId); break;
case 79: mutateEncoderField(world[0].garrisonOccupantIds); break;
case 80: mutateEncoderField(world[0].garrisonFireCursor); break;
case 81: mutateEncoderField(world[0].currentAmmo); break;
case 82: mutateEncoderField(world[0].pendingAircraftAuxSounds); break;
case 83: mutateEncoderField(world[0].turretSoundActivityTick); break;
case 84: mutateEncoderField(world[0].activeTurretSoundEvent); break;
case 85: mutateEncoderField(world[0].pendingTurretSounds); break;
case 86: mutateEncoderField(world[0].modExtensions); break;
default:throw std::runtime_error("unknown world field");}}
 void mutate(){aliasDone=true;switch(config.aliasMode){
  case 1:rules.special.warpInAnim="late-rule-animation-in";rules.special.warpOutAnim="late-rule-animation-out";rules.special.chronoInSound="late-rule-sound-in";rules.special.chronoOutSound="late-rule-sound-out";break;
  case 2:unit.chronoInSound=" late-unit-in ";unit.chronoOutSound=" late-unit-out ";break;
  case 3:mutateWorld();break;
  case 4:pendingTransportTargets.values[world[0].instanceId]=world[0].instanceId;break;
  case 5:world[1].renderX+=13.25;world[1].renderY-=4.5;world[1].renderLevel+=2.75;world[0].facing=3;break;
  case 6:event.kind="ChronoReinforcementIn";event.objectId=999;break;
  case 7:unitAvailable=!unitAvailable;break;
  default:throw std::runtime_error("unknown alias");}}
 void touch(Step s,int a=0,int b=0,int c=0){++calls;note(s,a,b,c);if(config.aliasMode&&!aliasDone&&config.aliasStep==static_cast<int>(s))mutate();if(config.failAt==calls)throw InjectedFault{};}
};
RuntimeImpactEffect::RuntimeImpactEffect() noexcept {if(activeModel)activeModel->constructed(this);}
RuntimeImpactEffect::~RuntimeImpactEffect() noexcept {if(activeModel)activeModel->destroyed(this);}
auto PendingTargets::find(int id) const->std::unordered_map<int,int>::const_iterator {owner->touch(Step::Pending,id);return values.find(id);}
const SpecialMechanismRules& ModelRules::getSpecialMechanismRules() const {owner->touch(Step::Rules);return special;}
void Effects::push_back(RuntimeImpactEffect&& e){owner->touch(Step::Append,e.attackerObjectId);values.push_back(std::move(e));owner->touch(Step::Appended,static_cast<int>(values.size()));}
using RuntimeProductionHostState=Model;
WorldObjectInstance* findMutableObjectById(Model& m,int id){m.touch(Step::Mutable,id);return m.lookup(id);}
const WorldObjectInstance* findObjectById(const Model& m,int id){auto& s=const_cast<Model&>(m);s.touch(Step::Object,id);return s.config.hostAvailable?s.lookup(id):nullptr;}
RuntimeCellPoint runtimeCellPointForObject(const WorldObjectInstance& o){return {o.x,o.y};}
std::optional<bool> runtimeChronoTransportDestinationAdmission(const Model& m,const WorldObjectInstance& o,const RuntimeCellPoint& p){auto& s=const_cast<Model&>(m);s.touch(Step::Admission,o.instanceId,p.x,p.y);if(s.config.admission<0)return {};return s.config.admission!=0;}
int runtimeCellKey(int x,int y){activeModel->touch(Step::Cell,x,y);return x<0||y<0?-1:y*1000+x;}
bool runtimeCellIsBridge(const Model& m,int x,int y){auto& s=const_cast<Model&>(m);s.touch(Step::Bridge,x,y);return s.config.bridge;}
bool runtimeInfantryUsesGroundSubcells(const WorldObjectInstance& o){activeModel->touch(Step::Ground,o.instanceId,activeModel->index(&o));return o.kind==WorldObjectKind::Infantry&&!o.airborne;}
void releaseRuntimeInfantrySubcellReservation(Model& m,int id){m.touch(Step::Release,id);++m.released;m.reservations.erase(id);}
void assignRuntimeInfantrySubcellAtCurrentLocation(Model& m,WorldObjectInstance& o){m.touch(Step::Assign,o.instanceId,m.index(&o));++m.assigned;o.subCell=(o.x+o.y+7)%5;m.reservations[o.instanceId]=o.subCell;}
void clearRuntimeRenderPosition(WorldObjectInstance& o){auto& m=*activeModel;m.touch(Step::Clear,o.instanceId,m.index(&o));++m.cleared;o.hasRenderPosition=false;o.renderX=o.x;o.renderY=o.y;o.renderMoveActive=false;o.renderLevel=o.level;o.renderOnBridge=o.onBridge;}
void refreshRuntimeObjectOccupancyAfterLocalChange(Model& m){m.touch(Step::Refresh);++m.refreshed;m.occupancy.clear();for(const auto& o:m.world)m.occupancy[o.y*1000+o.x]=o.instanceId;}
void invalidateRuntimeObjectProjectionCache(Model& m){m.touch(Step::Invalidate);++m.invalidated;m.projectionValid=false;}
struct RuntimeTransportMotionBody {double x=0,y=0,level=0;bool onBridge=false;};
RuntimeTransportMotionBody runtimeTransportMotionBody(const WorldObjectInstance& o){auto& m=*activeModel;m.touch(Step::Hull,o.instanceId,m.index(&o));return {o.hasRenderPosition?o.renderX:o.x,o.hasRenderPosition?o.renderY:o.y,o.hasRenderPosition?o.renderLevel:o.level,o.onBridge};}
const UnitType* findRuntimeUnitType(const ModelRules& rules,const WorldObjectInstance& o){auto& m=*rules.owner;m.touch(Step::Unit,o.instanceId,m.index(&o));return m.unitAvailable?&m.unit:nullptr;}
// Adversarial test-double geometry; the actual World helper stays in the realRoot adapter.
WorldObjectCellOffset modelOffsetValue(const WorldObjectInstance& o) noexcept {return {o.subCell*0.125+o.x*0.001,-o.subCell*0.25+o.y*0.002};}
WorldObjectCellOffset modelSubcellOffset(const WorldObjectInstance& o){auto& m=*activeModel;m.touch(Step::Offset,o.instanceId,m.index(&o),o.subCell);return modelOffsetValue(o);}
RuntimeSpatialAudioPosition runtimeSpatialAudioPositionForObject(const WorldObjectInstance& o){auto& m=*activeModel;m.touch(Step::Spatial,o.instanceId,m.index(&o));const auto offset=modelOffsetValue(o);return {(o.hasRenderPosition?o.renderX:o.x)+offset.x,(o.hasRenderPosition?o.renderY:o.y)+offset.y,o.hasRenderPosition?o.renderLevel:o.level};}
void initializeRuntimeImpactAnimationTimeline(Model& m,RuntimeImpactEffect& e,bool force){m.touch(Step::Timeline,e.attackerObjectId,force);++m.timelines;e.animationFrameCount=5;e.animationFrameDelayTicks=3;e.totalTicks=29;e.remainingTicks=29;e.frameIndex=2;e.animationStartFrame=2;e.animationEndFrame=4;e.animationLoopStartFrame=2;e.animationLoopEndFrame=3;e.animationLoopCount=2;e.animationSequenceSteps=7;e.animationMidpointWidth=13;e.animationMidpointHeight=17;
 if(m.config.scribbleTimeline){mutateEncoderField(e.projectileActorId);
mutateEncoderField(e.attackerObjectId);
mutateEncoderField(e.targetObjectId);
mutateEncoderField(e.targetIsStructure);
mutateEncoderField(e.owner);
mutateEncoderField(e.cell);
mutateEncoderField(e.level);
mutateEncoderField(e.hasWorldPosition);
mutateEncoderField(e.worldX);
mutateEncoderField(e.worldY);
mutateEncoderField(e.worldLevel);
mutateEncoderField(e.screenOffsetX);
mutateEncoderField(e.screenOffsetY);
mutateEncoderField(e.nativeFixedX);
mutateEncoderField(e.nativeFixedY);
mutateEncoderField(e.bodyCenterAnchored);
mutateEncoderField(e.bodyCenterPresentation);
mutateEncoderField(e.bodyCenterSpriteOffsetY);
mutateEncoderField(e.totalTicks);
mutateEncoderField(e.remainingTicks);
mutateEncoderField(e.radius);
mutateEncoderField(e.frameIndex);
mutateEncoderField(e.facing);
mutateEncoderField(e.muzzleAnchorX);
mutateEncoderField(e.muzzleAnchorY);
mutateEncoderField(e.muzzleAnchorLevel);
mutateEncoderField(e.muzzleFacing);
mutateEncoderField(e.animationFrameCount);
mutateEncoderField(e.animationFrameDelayTicks);
mutateEncoderField(e.animationStartFrame);
mutateEncoderField(e.animationLoopStartFrame);
mutateEncoderField(e.animationLoopEndFrame);
mutateEncoderField(e.animationLoopCount);
mutateEncoderField(e.animationEndFrame);
mutateEncoderField(e.animationSequenceSteps);
mutateEncoderField(e.animationMidpointWidth);
mutateEncoderField(e.animationMidpointHeight);
mutateEncoderField(e.lastSmudgeMidpointStep);
mutateEncoderField(e.deferredParticleAnimId);
mutateEncoderField(e.deferredParticleOwner);
mutateEncoderField(e.deferredParticleTicks);
mutateEncoderField(e.death);
mutateEncoderField(e.lifecycleState);
mutateEncoderField(e.renderIntent);
mutateEncoderField(e.weaponId);
mutateEncoderField(e.weaponSlot);
mutateEncoderField(e.projectileId);
mutateEncoderField(e.projectileImageId);
mutateEncoderField(e.animId);
mutateEncoderField(e.warheadId);
mutateEncoderField(e.warheadImpactIntentId);
mutateEncoderField(e.trailerIntentId);
mutateEncoderField(e.particleId);
mutateEncoderField(e.damage);
mutateEncoderField(e.cellSpread);
mutateEncoderField(e.bright);}
 m.touch(Step::TimelineReady,e.attackerObjectId,e.totalTicks);}
void emitRuntimeSound(Model& m,const std::string& intent,RuntimeSpatialAudioPosition p){m.touch(Step::Sound,static_cast<int>(intent.size()));m.sounds.push_back({intent,p});m.touch(Step::Sounded,static_cast<int>(m.sounds.size()));}
namespace original {
#define worldObjectSubCellOffset chrono_warp_model_detail::modelSubcellOffset
#include "RuntimeChronoWarpOriginal.inl"
#undef worldObjectSubCellOffset
}
class Queries final:public current::RuntimeChronoWarpQueries {
 Model& m;
public:explicit Queries(Model& s):m(s){}
 WorldObjectInstance* mutableObject(int id) override {return findMutableObjectById(m,id);}
 std::optional<bool> destinationAdmission(const WorldObjectInstance& o,const RuntimeCellPoint& p)const override{return runtimeChronoTransportDestinationAdmission(m,o,p);}
 const int* pendingTarget(int id)const override{const auto i=m.pendingTransportTargets.find(id);return i==m.pendingTransportTargets.end()?nullptr:std::addressof(i->second);}
 const WorldObjectInstance* object(int id)const override{return findObjectById(m,id);}
 int canonicalCell(int x,int y)const override{return runtimeCellKey(x,y);}
 bool bridge(int x,int y)const override{return runtimeCellIsBridge(m,x,y);}
 bool groundSubcells(const WorldObjectInstance& o)const override{return runtimeInfantryUsesGroundSubcells(o);}
 const SpecialMechanismRules& specialRules()const override{return m.rules.getSpecialMechanismRules();}
 WorldObjectCellOffset subcellOffset(const WorldObjectInstance& o)const override{return modelSubcellOffset(o);}
 current::RuntimeChronoBoardingPose boardingPose(const WorldObjectInstance& o)const override{const auto p=runtimeTransportMotionBody(o);return {p.x,p.y,p.level};}
 const UnitType* unitType(const WorldObjectInstance& o)const override{return findRuntimeUnitType(m.rules,o);}
 RuntimeSpatialAudioPosition spatialPosition(const WorldObjectInstance& o)const override{return runtimeSpatialAudioPositionForObject(o);}
};
class Arrival final:public current::RuntimeChronoWarpArrivalActions {
 Model& m;
public:explicit Arrival(Model& s):m(s){}
 void releaseSubcellReservation(int id)override{releaseRuntimeInfantrySubcellReservation(m,id);}
 void assignSubcell(WorldObjectInstance& o)override{assignRuntimeInfantrySubcellAtCurrentLocation(m,o);}
 void clearRenderPosition(WorldObjectInstance& o)override{clearRuntimeRenderPosition(o);}
 void refreshOccupancy()override{refreshRuntimeObjectOccupancyAfterLocalChange(m);}
 void invalidateProjection()override{invalidateRuntimeObjectProjectionCache(m);}
};
class Feedback final:public current::RuntimeChronoWarpFeedback {
 Model& m;std::optional<RuntimeImpactEffect> effect;
public:explicit Feedback(Model& s):m(s){}
 void beginEffect(const WorldObjectInstance& o)override{effect.emplace();effect->attackerObjectId=o.instanceId;effect->targetObjectId=o.instanceId;effect->owner=o.owner;effect->cell={o.x,o.y};effect->level=o.level;effect->hasWorldPosition=true;}
 void worldPosition(double x,double y,double level)noexcept override{effect->worldX=x;effect->worldY=y;effect->worldLevel=level;}
 void facingAndLifecycle(int facing,const char* lifecycle)override{effect->facing=facing;effect->lifecycleState=lifecycle;effect->renderIntent="chronoWarp";}
 void animation(const std::string& id)override{effect->animId=id;}
 const std::string& animation()const noexcept override{return effect->animId;}
 void initializeAndAppend()override{initializeRuntimeImpactAnimationTimeline(m,*effect,true);m.impactEffects.push_back(std::move(*effect));}
 void sound(const std::string& intent,RuntimeSpatialAudioPosition p)override{emitRuntimeSound(m,intent,p);}
};
void currentWarp(Model& m,const SpecialMechanismEvent& e,bool playSound){Queries q(m);Arrival a(m);Feedback f(m);current::applyRuntimeChronoWarpEvent(e,playSound,q,a,f);}
std::unique_ptr<Model> scene(Config cfg){auto m=std::make_unique<Model>(cfg);m->unitAvailable=cfg.unitAvailable;m->world.reserve(2);WorldObjectInstance p,h;p.instanceId=11;p.mapId="primary-map";p.kind=cfg.objectKind;p.owner="primary-owner-with-long-string";p.type="primary-type";p.alive=cfg.alive;p.health=123;p.x=cfg.x;p.y=cfg.y;p.level=2;p.cell=3077;p.subCell=cfg.subCell;p.facing=7;p.hasRenderPosition=cfg.render;p.renderMoveActive=cfg.render;p.renderX=5.25;p.renderY=6.75;p.renderLevel=2.5;p.airborne=cfg.airborne;
 h.instanceId=22;h.kind=WorldObjectKind::Unit;h.owner="host-owner";h.type="host-type";h.alive=true;h.x=9;h.y=10;h.level=3;h.cell=10009;h.hasRenderPosition=true;h.renderX=9.375;h.renderY=10.625;h.renderLevel=3.75;m->world.push_back(p);m->world.push_back(h);
 m->event.kind=cfg.kind;m->event.tick=100;m->event.objectId=cfg.present?11:999;m->event.targetObjectId=22;m->event.value=7;m->event.cell={3,4};m->event.reason="original-reason";
 if(cfg.pending)m->pendingTransportTargets.values.emplace(11,cfg.pending==1?999:cfg.selfHost?11:22);m->reservations.emplace(11,4);m->occupancy.emplace(6005,55);
 const std::array<std::string,6> anims{"","none","NoNe"," none ","long-authored-animation-out","LONG-AUTHORED-ANIMATION-IN"};const std::array<std::string,6> sounds{""," \t "," authored-unit-sound "," none "," MiXeD-Sound ","long-authored-sound-name-123"};m->rules.special.warpOutAnim=anims.at(cfg.anim);m->rules.special.warpInAnim=anims.at((cfg.anim+1)%6);m->rules.special.chronoInSound=" rule-in-fallback ";m->rules.special.chronoOutSound=cfg.sound==0?" \t ":" rule-out-fallback ";m->unit.chronoInSound=sounds.at(cfg.sound);m->unit.chronoOutSound=sounds.at((cfg.sound+1)%6);
 for(int i=0;i<cfg.initialEffects;++i){RuntimeImpactEffect e;e.owner="preexisting-sentry";e.remainingTicks=1234+i;e.animId="sentry";m->impactEffects.values.push_back(std::move(e));}return m;}
std::string snapshot(const Model& m){std::string out=encode(m.world)+encode(m.pendingTransportTargets.values)+encode(m.reservations)+encode(m.occupancy)+encode(m.released)+encode(m.assigned)+encode(m.cleared)+encode(m.refreshed)+encode(m.invalidated)+encode(m.timelines)+encode(m.projectionValid)+encode(m.rngSentry)+encode(m.tick)+encode(m.unitAvailable)+encode(m.rules.special.warpInAnim)+encode(m.rules.special.warpOutAnim)+encode(m.rules.special.chronoInSound)+encode(m.rules.special.chronoOutSound)+encode(m.unit.chronoInSound)+encode(m.unit.chronoOutSound)+encode(m.event.kind)+encode(m.event.tick)+encode(m.event.objectId)+encode(m.event.targetObjectId)+encode(m.event.value)+encode(m.event.cell)+encode(m.event.reason)+encode(m.caught)+encode(m.aliasDone)+encode(m.calls)+encode(m.constructs)+encode(m.destroys)+encode(m.depth);
 out+=encode(m.impactEffects.values.size());for(const auto& e:m.impactEffects.values)out+=impactImage(e);out+=encode(m.sounds.size());for(const auto& s:m.sounds)out+=encode(s.intent)+encode(s.position.x)+encode(s.position.y)+encode(s.position.level);for(const auto& t:m.trace)out+=encode(t.step)+encode(t.a)+encode(t.b)+encode(t.c);return out;}
struct Outcome {std::string bytes;std::vector<Record> trace;int calls=0;bool caught=false,aliased=false;};
Outcome execute(const Config& cfg,bool useCurrent){auto m=scene(cfg);require(activeModel==nullptr,"no leaked active test context");activeModel=m.get();try{if(useCurrent)currentWarp(*m,m->event,cfg.playSound);else original::spawnRuntimeChronoWarpEffect(*m,m->event,cfg.playSound);}catch(const InjectedFault&){m->caught=true;m->note(Step::Caught);}catch(...){activeModel=nullptr;throw;}activeModel=nullptr;require(m->depth==0&&m->constructs==m->destroys,"effect destroyed at original caller boundary");++states;return {snapshot(*m),m->trace,m->calls,m->caught,m->aliasDone};}
Outcome compare(const Config& cfg,const std::string& where){auto old=execute(cfg,false),now=execute(cfg,true);require(old.bytes==now.bytes,where+" full original state/trace/lifetime differs");return old;}
void probeEncoders(){
 {WorldObjectInstance zero;const auto before=encode(zero);{auto v=zero;mutateEncoderField(v.instanceId);require(encode(v)!=before,"world instanceId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.kind);require(encode(v)!=before,"world kind omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.mapId);require(encode(v)!=before,"world mapId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.owner);require(encode(v)!=before,"world owner omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.type);require(encode(v)!=before,"world type omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.health);require(encode(v)!=before,"world health omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.x);require(encode(v)!=before,"world x omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.y);require(encode(v)!=before,"world y omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.cell);require(encode(v)!=before,"world cell omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.level);require(encode(v)!=before,"world level omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.facing);require(encode(v)!=before,"world facing omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.turretFacing);require(encode(v)!=before,"world turretFacing omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.bodyFacingClock);require(encode(v)!=before,"world bodyFacingClock omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.turretFacingClock);require(encode(v)!=before,"world turretFacingClock omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.subCell);require(encode(v)!=before,"world subCell omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.mission);require(encode(v)!=before,"world mission omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tag);require(encode(v)!=before,"world tag omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.veterancy);require(encode(v)!=before,"world veterancy omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.veterancyExperience);require(encode(v)!=before,"world veterancyExperience omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.crateArmorMultiplier);require(encode(v)!=before,"world crateArmorMultiplier omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.crateFirepowerMultiplier);require(encode(v)!=before,"world crateFirepowerMultiplier omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.crateSpeedMultiplier);require(encode(v)!=before,"world crateSpeedMultiplier omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.crateCloakable);require(encode(v)!=before,"world crateCloakable omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.harvestAutoSearchSuspended);require(encode(v)!=before,"world harvestAutoSearchSuspended omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.hasBeenCaptured);require(encode(v)!=before,"world hasBeenCaptured omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.onBridge);require(encode(v)!=before,"world onBridge omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.authoredAircraft);require(encode(v)!=before,"world authoredAircraft omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.alive);require(encode(v)!=before,"world alive omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.rubble);require(encode(v)!=before,"world rubble omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.submerged);require(encode(v)!=before,"world submerged omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.cloaked);require(encode(v)!=before,"world cloaked omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.cloakCooldownTicks);require(encode(v)!=before,"world cloakCooldownTicks omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.submergeCooldownTicks);require(encode(v)!=before,"world submergeCooldownTicks omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.deployed);require(encode(v)!=before,"world deployed omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.deployStateTicksRemaining);require(encode(v)!=before,"world deployStateTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.prone);require(encode(v)!=before,"world prone omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.proneTicksRemaining);require(encode(v)!=before,"world proneTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.weaponCooldownTicksRemaining);require(encode(v)!=before,"world weaponCooldownTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.spriteFiringStartTick);require(encode(v)!=before,"world spriteFiringStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.radiationEruptionCooldownTicksRemaining);require(encode(v)!=before,"world radiationEruptionCooldownTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.hasRenderPosition);require(encode(v)!=before,"world hasRenderPosition omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderX);require(encode(v)!=before,"world renderX omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderY);require(encode(v)!=before,"world renderY omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderMoveActive);require(encode(v)!=before,"world renderMoveActive omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderLevel);require(encode(v)!=before,"world renderLevel omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.renderOnBridge);require(encode(v)!=before,"world renderOnBridge omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.movementAnimationDistance);require(encode(v)!=before,"world movementAnimationDistance omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.buildAnimTicksTotal);require(encode(v)!=before,"world buildAnimTicksTotal omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.buildAnimTicksRemaining);require(encode(v)!=before,"world buildAnimTicksRemaining omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.structureRecoilStartTick);require(encode(v)!=before,"world structureRecoilStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.chargeTurretStartTick);require(encode(v)!=before,"world chargeTurretStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.workAnimationActive);require(encode(v)!=before,"world workAnimationActive omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.poweredOperational);require(encode(v)!=before,"world poweredOperational omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.powerAnimationPausedAtTick);require(encode(v)!=before,"world powerAnimationPausedAtTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.powerAnimationPausedTicks);require(encode(v)!=before,"world powerAnimationPausedTicks omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.factoryExitAnimationStartTick);require(encode(v)!=before,"world factoryExitAnimationStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.factoryExitAnimationEndTick);require(encode(v)!=before,"world factoryExitAnimationEndTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.factoryExitAirborne);require(encode(v)!=before,"world factoryExitAirborne omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.factoryExitHostId);require(encode(v)!=before,"world factoryExitHostId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.specialCashNextTick);require(encode(v)!=before,"world specialCashNextTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.specialCloneDeliveryObjectId);require(encode(v)!=before,"world specialCloneDeliveryObjectId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.transportPassengerIds);require(encode(v)!=before,"world transportPassengerIds omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.bioReactorAnimationStartTick);require(encode(v)!=before,"world bioReactorAnimationStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.bioReactorAnimationPoweredStartTick);require(encode(v)!=before,"world bioReactorAnimationPoweredStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.activeGunnerMode);require(encode(v)!=before,"world activeGunnerMode omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.activeGunnerWeaponIndex);require(encode(v)!=before,"world activeGunnerWeaponIndex omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.activeGunnerTurretIndex);require(encode(v)!=before,"world activeGunnerTurretIndex omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tankBunkerHostId);require(encode(v)!=before,"world tankBunkerHostId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tankBunkerOccupantId);require(encode(v)!=before,"world tankBunkerOccupantId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tankBunkerAnimationStartTick);require(encode(v)!=before,"world tankBunkerAnimationStartTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.tankBunkerLowering);require(encode(v)!=before,"world tankBunkerLowering omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.dockedToObjectId);require(encode(v)!=before,"world dockedToObjectId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.dockIndex);require(encode(v)!=before,"world dockIndex omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.airborne);require(encode(v)!=before,"world airborne omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.flightLevel);require(encode(v)!=before,"world flightLevel omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.aircraftDockFlightProgress);require(encode(v)!=before,"world aircraftDockFlightProgress omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.aircraftBuildingClearanceLevel);require(encode(v)!=before,"world aircraftBuildingClearanceLevel omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.wallConnectionMask);require(encode(v)!=before,"world wallConnectionMask omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.garrisonHostObjectId);require(encode(v)!=before,"world garrisonHostObjectId omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.garrisonOccupantIds);require(encode(v)!=before,"world garrisonOccupantIds omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.garrisonFireCursor);require(encode(v)!=before,"world garrisonFireCursor omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.currentAmmo);require(encode(v)!=before,"world currentAmmo omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.pendingAircraftAuxSounds);require(encode(v)!=before,"world pendingAircraftAuxSounds omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.turretSoundActivityTick);require(encode(v)!=before,"world turretSoundActivityTick omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.activeTurretSoundEvent);require(encode(v)!=before,"world activeTurretSoundEvent omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.pendingTurretSounds);require(encode(v)!=before,"world pendingTurretSounds omitted");++worldProbes;}
{auto v=zero;mutateEncoderField(v.modExtensions);require(encode(v)!=before,"world modExtensions omitted");++worldProbes;}}
 {RuntimeImpactEffect zero;const auto before=impactImage(zero);{auto v=zero;mutateEncoderField(v.projectileActorId);require(impactImage(v)!=before,"effect projectileActorId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.attackerObjectId);require(impactImage(v)!=before,"effect attackerObjectId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.targetObjectId);require(impactImage(v)!=before,"effect targetObjectId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.targetIsStructure);require(impactImage(v)!=before,"effect targetIsStructure omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.owner);require(impactImage(v)!=before,"effect owner omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.cell);require(impactImage(v)!=before,"effect cell omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.level);require(impactImage(v)!=before,"effect level omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.hasWorldPosition);require(impactImage(v)!=before,"effect hasWorldPosition omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.worldX);require(impactImage(v)!=before,"effect worldX omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.worldY);require(impactImage(v)!=before,"effect worldY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.worldLevel);require(impactImage(v)!=before,"effect worldLevel omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.screenOffsetX);require(impactImage(v)!=before,"effect screenOffsetX omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.screenOffsetY);require(impactImage(v)!=before,"effect screenOffsetY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.nativeFixedX);require(impactImage(v)!=before,"effect nativeFixedX omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.nativeFixedY);require(impactImage(v)!=before,"effect nativeFixedY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.bodyCenterAnchored);require(impactImage(v)!=before,"effect bodyCenterAnchored omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.bodyCenterPresentation);require(impactImage(v)!=before,"effect bodyCenterPresentation omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.bodyCenterSpriteOffsetY);require(impactImage(v)!=before,"effect bodyCenterSpriteOffsetY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.totalTicks);require(impactImage(v)!=before,"effect totalTicks omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.remainingTicks);require(impactImage(v)!=before,"effect remainingTicks omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.radius);require(impactImage(v)!=before,"effect radius omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.frameIndex);require(impactImage(v)!=before,"effect frameIndex omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.facing);require(impactImage(v)!=before,"effect facing omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.muzzleAnchorX);require(impactImage(v)!=before,"effect muzzleAnchorX omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.muzzleAnchorY);require(impactImage(v)!=before,"effect muzzleAnchorY omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.muzzleAnchorLevel);require(impactImage(v)!=before,"effect muzzleAnchorLevel omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.muzzleFacing);require(impactImage(v)!=before,"effect muzzleFacing omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationFrameCount);require(impactImage(v)!=before,"effect animationFrameCount omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationFrameDelayTicks);require(impactImage(v)!=before,"effect animationFrameDelayTicks omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationStartFrame);require(impactImage(v)!=before,"effect animationStartFrame omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationLoopStartFrame);require(impactImage(v)!=before,"effect animationLoopStartFrame omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationLoopEndFrame);require(impactImage(v)!=before,"effect animationLoopEndFrame omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationLoopCount);require(impactImage(v)!=before,"effect animationLoopCount omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationEndFrame);require(impactImage(v)!=before,"effect animationEndFrame omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationSequenceSteps);require(impactImage(v)!=before,"effect animationSequenceSteps omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationMidpointWidth);require(impactImage(v)!=before,"effect animationMidpointWidth omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animationMidpointHeight);require(impactImage(v)!=before,"effect animationMidpointHeight omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.lastSmudgeMidpointStep);require(impactImage(v)!=before,"effect lastSmudgeMidpointStep omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.deferredParticleAnimId);require(impactImage(v)!=before,"effect deferredParticleAnimId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.deferredParticleOwner);require(impactImage(v)!=before,"effect deferredParticleOwner omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.deferredParticleTicks);require(impactImage(v)!=before,"effect deferredParticleTicks omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.death);require(impactImage(v)!=before,"effect death omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.lifecycleState);require(impactImage(v)!=before,"effect lifecycleState omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.renderIntent);require(impactImage(v)!=before,"effect renderIntent omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.weaponId);require(impactImage(v)!=before,"effect weaponId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.weaponSlot);require(impactImage(v)!=before,"effect weaponSlot omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.projectileId);require(impactImage(v)!=before,"effect projectileId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.projectileImageId);require(impactImage(v)!=before,"effect projectileImageId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.animId);require(impactImage(v)!=before,"effect animId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.warheadId);require(impactImage(v)!=before,"effect warheadId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.warheadImpactIntentId);require(impactImage(v)!=before,"effect warheadImpactIntentId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.trailerIntentId);require(impactImage(v)!=before,"effect trailerIntentId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.particleId);require(impactImage(v)!=before,"effect particleId omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.damage);require(impactImage(v)!=before,"effect damage omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.cellSpread);require(impactImage(v)!=before,"effect cellSpread omitted");++effectProbes;}
{auto v=zero;mutateEncoderField(v.bright);require(impactImage(v)!=before,"effect bright omitted");++effectProbes;}}
 {RuntimeParticleAirbornePresentation zero;const auto before=airborneImage(zero);{auto v=zero;mutateEncoderField(v.enabled);require(airborneImage(v)!=before,"airborne enabled omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.sourceCellSum);require(airborneImage(v)!=before,"airborne sourceCellSum omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.sourceLevel);require(airborneImage(v)!=before,"airborne sourceLevel omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.factoryLiftCancellation);require(airborneImage(v)!=before,"airborne factoryLiftCancellation omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.clearanceFraction);require(airborneImage(v)!=before,"airborne clearanceFraction omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.resolvedClearanceLevel);require(airborneImage(v)!=before,"airborne resolvedClearanceLevel omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.roofs);require(airborneImage(v)!=before,"airborne roofs omitted");++nestedProbes;}}
 {RuntimeParticleClearanceRoof zero;const auto before=roofImage(zero);{auto v=zero;mutateEncoderField(v.cellSum);require(roofImage(v)!=before,"roof cellSum omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.level);require(roofImage(v)!=before,"roof level omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.authoredHeight);require(roofImage(v)!=before,"roof authoredHeight omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.nativeSpriteHeight);require(roofImage(v)!=before,"roof nativeSpriteHeight omitted");++nestedProbes;}
{auto v=zero;mutateEncoderField(v.rubble);require(roofImage(v)!=before,"roof rubble omitted");++nestedProbes;}}
 require(worldProbes==87&&effectProbes==56&&nestedProbes==12,"all original complete encoders");}
bool runContract(){try{probeEncoders();std::set<Step> faultPorts;
 const std::array<std::string,6> kinds{"ChronoWarpOut","ChronoWarpIn","ChronoReinforcementIn","OtherCompatibilityEvent","","chronowarpin"};
 for(const auto& kind:kinds)for(bool sound:{false,true})for(int i=0;i<192;++i){Config c;c.kind=kind;c.playSound=sound;c.present=i%17!=0;c.alive=i%19!=0;c.objectKind=static_cast<WorldObjectKind>(i%3);c.airborne=(i/3)%2;c.render=(i/6)%2;c.admission=(i/12)%3-1;c.pending=(i/36)%3;c.anim=(i/9)%6;c.sound=(i/8)%6;c.unitAvailable=(i/4)%5!=0;c.selfHost=i%11==0;c.hostAvailable=i%13!=0;c.subCell=i%7-1;c.initialEffects=i%3;const std::array<int,5> cells{-1,0,5,511,512};c.x=cells[i%5];c.y=cells[(i/5)%5];c.bridge=i%2;c.scribbleTimeline=i%23==0;compare(c,"matrix "+kind+"/"+std::to_string(sound)+"/"+std::to_string(i));}
 for(const auto& kind:kinds)for(int variant=0;variant<4;++variant){Config base;base.kind=kind;base.objectKind=WorldObjectKind::Infantry;base.anim=variant%2?0:4;base.sound=variant/2?0:2;base.pending=variant==3?0:2;base.scribbleTimeline=variant==2;const auto original=compare(base,"fault spine original");for(int n=1;n<=original.calls;++n){auto c=base;c.failAt=n;const auto old=compare(c,"fault "+kind+"/"+std::to_string(variant)+"/"+std::to_string(n));require(old.caught,"original reached configured fault");int ordinal=0;for(const auto& t:old.trace)if(t.step<=Step::Sounded&&++ordinal==n)faultPorts.insert(t.step);++faultCases;}}
 for(int mode:{1,2,4,5,6,7})for(int stage=1;stage<=static_cast<int>(Step::Sounded);++stage)for(const auto& kind:std::array<std::string,3>{"ChronoWarpOut","ChronoWarpIn","ChronoReinforcementIn"}){Config c;c.kind=kind;c.aliasMode=mode;c.aliasStep=stage;c.sound=1;auto result=compare(c,"alias mode="+std::to_string(mode)+" stage="+std::to_string(stage)+" kind="+kind);++aliasConfigs;if(result.aliased)++aliasFired;}
 for(int field=0;field<87;++field)for(Step stage:{Step::Rules,Step::Hull,Step::Offset,Step::Appended,Step::Unit,Step::Spatial}){Config c;c.aliasMode=3;c.aliasStep=static_cast<int>(stage);c.worldField=field;c.sound=1;auto result=compare(c,"world alias "+std::to_string(field)+"/"+std::to_string(static_cast<int>(stage)));++aliasConfigs;if(result.aliased)++aliasFired;}
 require(faultPorts.size()==23&&faultCases>0&&aliasConfigs==936&&aliasFired>0,"all typed fault ports and complete configured live aliases");
 std::cout<<"chrono-warp checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" effect-probes="<<effectProbes<<" nested-probes="<<nestedProbes<<" faults="<<faultCases<<" fault-ports="<<faultPorts.size()<<" alias-configs="<<aliasConfigs<<" aliases-fired="<<aliasFired<<" original-state-trace-and-lifetime=exact failures=0\n";return true;
 }catch(const std::exception& e){activeModel=nullptr;std::cerr<<"chrono-warp FAIL "<<e.what()<<'\n';return false;}}
} // namespace chrono_warp_model_detail
