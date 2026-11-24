#include "simulation/abilities/chrono/RuntimeChronoCellOccupancy.h"
#include "simulation/abilities/chrono/RuntimeChronoTerrainView.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
namespace chrono_spatial_model_detail {
namespace current=concord::simulation::abilities;
using Lookup=current::RuntimeChronoTerrainLookup;
int checks=0,states=0,worldProbes=0,terrainProbes=0,faults=0,aliases=0,terrainCases=0;
std::set<std::string> faultPorts;
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


std::array<TerrainCell,8> cells;
std::string terrainRecord(const TerrainCell& c){return encode(c.x)+encode(c.y)+encode(c.cellIndex512)+encode(c.tileIndex)+encode(c.subTileIndex)+encode(c.level)+encode(c.iceGrowth)+encode(c.hasOverlay)+encode(c.overlayIndex)+encode(c.overlayFrame)+encode(c.overlayType)+encode(c.overlayImage)+encode(c.overlayIsOre)+encode(c.overlayIsGem);}
std::string lookupRecord(const Lookup& values){
 std::vector<std::pair<int,int>> normalized;
 for(const auto& [key,value]:values){int index=-1;if(value){index=-2;for(std::size_t i=0;i<cells.size();++i)if(value==&cells[i])index=static_cast<int>(i);}normalized.emplace_back(key,index);}
 std::sort(normalized.begin(),normalized.end());std::string out;
 for(const auto& [key,index]:normalized)out+=encode(key)+encode(index);
 return out;
}
struct QueryFault {};
struct Model {
 mutable std::vector<WorldObjectInstance> objects;
 mutable std::unordered_map<int,int> occupied;
 mutable Lookup cached,generated;
 mutable std::vector<int> slots;
 mutable std::vector<std::string> trace;
 mutable std::vector<std::string> ports;
 mutable int calls=0,traceDepth=0;
 int faultCall=-1,faultPhase=-1,mutationCall=-1,mutationPhase=0,mutationMode=0;
 bool referenceDefault=false;
 std::string snapshot() const {
  std::vector<std::pair<int,int>> occupancy(occupied.begin(),occupied.end());std::sort(occupancy.begin(),occupancy.end());std::string out=encode(objects);
  for(const auto& [key,id]:occupancy)out+=encode(key)+encode(id);
  return out+lookupRecord(cached)+"/"+lookupRecord(generated)+encode(slots)+encode(calls)+encode(traceDepth);
 }
 void mutate() const {
  if(mutationMode==1&&!objects.empty()){objects.front().kind=WorldObjectKind::Infantry;objects.front().airborne=!objects.front().airborne;objects.front().mission+="|live";}
  if(mutationMode==2){occupied[-1]=43;occupied[3001]=71;occupied[0]=11;}
  if(mutationMode==3){slots.empty()?slots.push_back(4):slots.clear();}
  if(mutationMode==4){cached.clear();cached.emplace(99,&cells[7]);}
  if(mutationMode==5){generated.clear();generated.emplace(-1,nullptr);}
  if(mutationMode==6&&!objects.empty()){objects.front().instanceId+=1;objects.front().health=-17;objects.front().tag+="|post";}
 }
 void phase(const std::string& port,int stage) const {
  trace.push_back(port+":"+std::to_string(stage)+":"+snapshot());
  if(calls==faultCall&&stage==faultPhase)throw QueryFault{};
  if(calls==mutationCall&&stage==mutationPhase)mutate();
 }
 void begin(const std::string& port,const std::string& args) const {++calls;ports.push_back(port);trace.push_back("args:"+port+":"+args);phase(port,0);}
 void end(const std::string& port,const std::string& result) const {phase(port,1);trace.push_back("result:"+port+":"+result);}
 const WorldObjectInstance* find(int id) const {
  begin("find",encode(id));const WorldObjectInstance* result=nullptr;
  for(const auto& object:objects)if(object.instanceId==id){result=&object;break;}
  end("find",result?encode(*result):"missing");return result;
 }
 bool ground(const WorldObjectInstance& object) const {
  begin("ground",encode(object));const bool result=object.kind==WorldObjectKind::Infantry&&!object.airborne;
  end("ground",encode(result));return result;
 }
 std::vector<int> available(const WorldObjectInstance& object,const RuntimeCellPoint& destination) const {
  begin("slots",encode(object)+encode(destination));auto result=slots;end("slots",encode(result));return result;
 }
 int key(int x,int y) const {
  begin("key",encode(x)+encode(y));const int result=x<0||y<0?-1:y*1000+x;
  end("key",encode(result));return result;
 }
 Lookup build() const {
  begin("build","");auto result=generated;end("build",lookupRecord(result));return result;
 }
};
thread_local const Model* activeModel=nullptr;
struct Active {const Model* prior;explicit Active(const Model& m):prior(activeModel){activeModel=&m;}~Active(){activeModel=prior;}};
struct RuntimeGameSessionState {const Model& data;const Lookup& terrainByCell;const Model& world;int tick=37;};
struct RuntimePerfTraceScope {
 explicit RuntimePerfTraceScope(const char* name,int tick){require(std::string(name)=="moveCommand.chronoTerrain"&&tick==37,"original trace args");++activeModel->traceDepth;}
 ~RuntimePerfTraceScope(){--activeModel->traceDepth;}
};
namespace original {
using ::chrono_spatial_model_detail::RuntimeGameSessionState;
using ::chrono_spatial_model_detail::RuntimePerfTraceScope;
const WorldObjectInstance* findObjectById(const RuntimeGameSessionState& state,int id){return state.data.find(id);}
bool runtimeInfantryUsesGroundSubcells(const WorldObjectInstance& object){return activeModel->ground(object);}
std::vector<int> runtimeAvailableInfantrySubcells(const RuntimeGameSessionState& state,const WorldObjectInstance& object,const RuntimeCellPoint& destination){return state.data.available(object,destination);}
int runtimeCellKey(int x,int y){return activeModel->key(x,y);}
bool runtimeChronoMoveTerrainReference(){return activeModel->referenceDefault;}
Lookup buildRuntimeTerrainLookup(const Model& world){return world.build();}
#include "RuntimeChronoSpatialOriginal.inl"
}
class OccupancyQueries final:public current::RuntimeChronoOccupancyQueries {
 const Model& model_;
public:
 explicit OccupancyQueries(const Model& m):model_(m){}
 const WorldObjectInstance* findObject(int id)const override{return model_.find(id);}
 bool usesGroundSubcells(const WorldObjectInstance& object)const override{return model_.ground(object);}
 std::vector<int> availableSubcells(const WorldObjectInstance& object,const RuntimeCellPoint& destination)const override{return model_.available(object,destination);}
 int cellKey(int x,int y)const override{return model_.key(x,y);}
};
class TerrainSource final:public current::RuntimeChronoTerrainSource {
 const Model& model_;
public:
 explicit TerrainSource(const Model& m):model_(m){}
 Lookup buildLookup()const override{return model_.build();}
};
struct Result {bool value=false,thrown=false;std::string state;std::vector<std::string> trace;std::vector<std::string> ports;bool operator==(const Result&)const=default;};
Result occupancy(Model model,bool candidate,int x,int y,int id){
 Active active(model);RuntimeGameSessionState state{model,model.cached,model};Result out;
 try{if(candidate){const OccupancyQueries query(model);out.value=current::runtimeChronoCellIsOccupied(model.occupied,x,y,id,query);}else out.value=original::runtimeChronoCellIsOccupied(state,model.occupied,x,y,id);}
 catch(const QueryFault&){out.thrown=true;}
 out.state=model.snapshot();out.trace=model.trace;out.ports=model.ports;return out;
}
void compareOccupancy(const Model& model,int x,int y,int id){
 const auto old=occupancy(model,false,x,y,id),now=occupancy(model,true,x,y,id);require(old==now,"occupancy complete state/query/result/exception order");++states;
 if(old.thrown){++faults;faultPorts.insert(old.ports.back());}
}
Model occupancyFixture(int kind,int air,int presence,int occupied,int slot){
 Model m;WorldObjectInstance object;object.instanceId=11;object.kind=static_cast<WorldObjectKind>(kind);object.airborne=air!=0;object.alive=(kind+air)%2==0;object.health=kind==0?-9:air==0?0:93;object.owner="Owner\0tail";object.mission="Guard";object.tag="sentinel";object.hasRenderPosition=true;object.renderX=-1.25;object.renderY=23.5;object.transportPassengerIds={8,9};object.modExtensions.technoType="full-field";
 if(presence!=0)m.objects.push_back(object);
 if(presence==2){object.kind=object.kind==WorldObjectKind::Infantry?WorldObjectKind::Unit:WorldObjectKind::Infantry;object.airborne=!object.airborne;object.tag="duplicate-second";m.objects.push_back(object);}
 if(presence==3){object.instanceId=44;m.objects.clear();m.objects.push_back(object);}
 for(int key:std::array<int,5>{-1,0,3001,511999,512000})if(occupied!=0)m.occupied.emplace(key,occupied==1?11:occupied==2?71:occupied==3?-1:0);
 for(int n=0;n<slot;++n)m.slots.push_back(n%2?4:2);
 return m;
}
void verifyOccupancy(){
 const std::array<RuntimeCellPoint,8> coords{{{-1,0},{0,-1},{-1,-1},{0,0},{1,3},{1001,2},{999,511},{0,512}}};
 for(int kind=0;kind<4;++kind)for(int air=0;air<2;++air)for(int presence=0;presence<4;++presence)for(int occupied=0;occupied<5;++occupied)for(int slot=0;slot<4;++slot)for(auto point:coords)compareOccupancy(occupancyFixture(kind,air,presence,occupied,slot),point.x,point.y,11);
 for(int kind=0;kind<4;++kind)for(int presence=0;presence<3;++presence){auto m=occupancyFixture(kind,0,presence,2,0);auto normal=occupancy(m,false,1,3,11);require(!normal.thrown,"baseline no fault");
  for(int call=1;call<=static_cast<int>(normal.ports.size());++call)for(int phase=0;phase<2;++phase){auto fault=m;fault.faultCall=call;fault.faultPhase=phase;compareOccupancy(fault,1,3,11);}
  for(int mode=1;mode<=6;++mode)for(int call=1;call<=3;++call)for(int phase=0;phase<2;++phase){auto live=m;live.mutationCall=call;live.mutationPhase=phase;live.mutationMode=mode;compareOccupancy(live,1,3,11);++aliases;}
 }
 auto duplicate=occupancyFixture(static_cast<int>(WorldObjectKind::Infantry),0,2,2,1);auto result=occupancy(duplicate,true,1,3,11);require(!result.value&&result.ports==std::vector<std::string>({"find","ground","slots"}),"first duplicate includes dead/zero-health ground infantry and ignores cell representative");
 auto missing=occupancyFixture(0,0,0,1,0);result=occupancy(missing,true,-1,0,11);require(!result.value&&result.ports==std::vector<std::string>({"find","key"}),"missing actor still excludes self at negative key");
 auto collision=occupancyFixture(0,1,0,2,0);require(occupancy(collision,true,1,3,11).value==occupancy(collision,true,1001,2,11).value,"original thousand-key collision preserved");
}
struct ViewResult {bool thrown=false;std::string value,state;std::vector<std::string> trace;std::vector<std::string> ports;bool operator==(const ViewResult&)const=default;};
template<class V>void inspectTerrain(const V& view,const Model& model,std::string& out){
 const auto* address=&view.get();out+=encode(view.borrowed())+encode(view.generated())+encode(view.reference())+lookupRecord(view.get());
 require((address==&model.cached)==view.borrowed(),"terrain owned/borrowed identity");
 auto before=lookupRecord(view.get());model.cached[177]=&cells[6];model.generated[188]=nullptr;
 const auto after=lookupRecord(view.get());require(&view.get()==address,"view identity remains stable after source lifetime");
 require(view.borrowed()?after!=before:after==before,"borrow observes live cached map; owned map remains isolated");out+="/after:"+after;
 require(!view.generated()||!view.borrowed(),"generated map is owned");
}
ViewResult terrain(Model model,bool candidate,bool reference){
 Active active(model);RuntimeGameSessionState state{model,model.cached,model};ViewResult out;
 try{
  if(candidate){std::unique_ptr<current::RuntimeChronoTerrainView> view;
   {RuntimePerfTraceScope trace("moveCommand.chronoTerrain",state.tick);const TerrainSource source(model);view=std::make_unique<current::RuntimeChronoTerrainView>(model.cached,reference,source);}
   inspectTerrain(*view,model,out.value);
  }else{original::RuntimeChronoMoveTerrainView view(state,reference);inspectTerrain(view,model,out.value);}
 }catch(const QueryFault&){out.thrown=true;}
 require(model.traceDepth==0,"original constructor trace exits on success/throw");out.state=model.snapshot();out.trace=model.trace;out.ports=model.ports;return out;
}
void compareTerrain(const Model& model,bool reference){auto old=terrain(model,false,reference),now=terrain(model,true,reference);require(old==now,"terrain complete lookup/flags/state/orderedsource/exception exact");++states;++terrainCases;if(old.thrown){++faults;faultPorts.insert("build");}}
Model terrainFixture(int cached,int generated){Model m;
 if(cached==1)m.cached.emplace(3001,&cells[0]);
 if(cached==2)m.cached.emplace(-1,nullptr);
 if(cached==3){m.cached.emplace(-1,&cells[2]);m.cached.emplace(3001,&cells[0]);m.cached.emplace(512000,nullptr);m.cached.emplace(999999,&cells[0]);}
 if(generated==1)m.generated.emplace(-1,&cells[4]);
 if(generated==2)m.generated.emplace(0,nullptr);
 if(generated==3){m.generated.emplace(3,&cells[3]);m.generated.emplace(1001,&cells[1]);m.generated.emplace(-1,nullptr);}
 return m;
}
void verifyTerrain(){
 static_assert(!std::is_copy_constructible_v<current::RuntimeChronoTerrainView> && !std::is_move_constructible_v<current::RuntimeChronoTerrainView>);
 static_assert(!std::is_copy_assignable_v<current::RuntimeChronoTerrainView> && !std::is_move_assignable_v<current::RuntimeChronoTerrainView>);
 static_assert(sizeof(current::RuntimeChronoTerrainView)==sizeof(original::RuntimeChronoMoveTerrainView));
 static_assert(alignof(current::RuntimeChronoTerrainView)==alignof(original::RuntimeChronoMoveTerrainView));
 for(int cached=0;cached<4;++cached)for(int generated=0;generated<4;++generated)for(bool reference:{false,true}){
  auto model=terrainFixture(cached,generated);compareTerrain(model,reference);
  if(cached==0){for(int phase=0;phase<2;++phase){auto fault=model;fault.faultCall=1;fault.faultPhase=phase;compareTerrain(fault,reference);}
   for(int mode=1;mode<=6;++mode)for(int phase=0;phase<2;++phase){auto live=model;live.mutationCall=1;live.mutationPhase=phase;live.mutationMode=mode;compareTerrain(live,reference);++aliases;}
  }
 }
 auto empty=terrainFixture(0,0);auto r=terrain(empty,true,true);require(!r.thrown&&r.ports==std::vector<std::string>{"build"},"empty cache always falls back even reference and empty generated result");
 for(int cached=1;cached<4;++cached)for(bool reference:{false,true}){auto m=terrainFixture(cached,3);m.faultCall=1;m.faultPhase=0;require(!terrain(m,true,reference).thrown&&terrain(m,true,reference).ports.empty(),"nonempty cache never calls fallback, including null-only");}
}
void verifyEncoders(){WorldObjectInstance base;const auto before=encode(base);
{ WorldObjectInstance changed=base;mutateEncoderField(changed.instanceId);require(encode(changed)!=before,"World encoder field instanceId");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.kind);require(encode(changed)!=before,"World encoder field kind");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.mapId);require(encode(changed)!=before,"World encoder field mapId");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.owner);require(encode(changed)!=before,"World encoder field owner");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.type);require(encode(changed)!=before,"World encoder field type");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.health);require(encode(changed)!=before,"World encoder field health");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.x);require(encode(changed)!=before,"World encoder field x");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.y);require(encode(changed)!=before,"World encoder field y");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.cell);require(encode(changed)!=before,"World encoder field cell");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.level);require(encode(changed)!=before,"World encoder field level");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.facing);require(encode(changed)!=before,"World encoder field facing");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.turretFacing);require(encode(changed)!=before,"World encoder field turretFacing");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.bodyFacingClock);require(encode(changed)!=before,"World encoder field bodyFacingClock");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.turretFacingClock);require(encode(changed)!=before,"World encoder field turretFacingClock");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.subCell);require(encode(changed)!=before,"World encoder field subCell");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.mission);require(encode(changed)!=before,"World encoder field mission");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.tag);require(encode(changed)!=before,"World encoder field tag");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.veterancy);require(encode(changed)!=before,"World encoder field veterancy");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.veterancyExperience);require(encode(changed)!=before,"World encoder field veterancyExperience");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.crateArmorMultiplier);require(encode(changed)!=before,"World encoder field crateArmorMultiplier");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.crateFirepowerMultiplier);require(encode(changed)!=before,"World encoder field crateFirepowerMultiplier");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.crateSpeedMultiplier);require(encode(changed)!=before,"World encoder field crateSpeedMultiplier");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.crateCloakable);require(encode(changed)!=before,"World encoder field crateCloakable");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.harvestAutoSearchSuspended);require(encode(changed)!=before,"World encoder field harvestAutoSearchSuspended");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.hasBeenCaptured);require(encode(changed)!=before,"World encoder field hasBeenCaptured");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.onBridge);require(encode(changed)!=before,"World encoder field onBridge");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.authoredAircraft);require(encode(changed)!=before,"World encoder field authoredAircraft");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.alive);require(encode(changed)!=before,"World encoder field alive");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.rubble);require(encode(changed)!=before,"World encoder field rubble");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.submerged);require(encode(changed)!=before,"World encoder field submerged");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.cloaked);require(encode(changed)!=before,"World encoder field cloaked");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.cloakCooldownTicks);require(encode(changed)!=before,"World encoder field cloakCooldownTicks");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.submergeCooldownTicks);require(encode(changed)!=before,"World encoder field submergeCooldownTicks");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.deployed);require(encode(changed)!=before,"World encoder field deployed");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.deployStateTicksRemaining);require(encode(changed)!=before,"World encoder field deployStateTicksRemaining");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.prone);require(encode(changed)!=before,"World encoder field prone");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.proneTicksRemaining);require(encode(changed)!=before,"World encoder field proneTicksRemaining");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.weaponCooldownTicksRemaining);require(encode(changed)!=before,"World encoder field weaponCooldownTicksRemaining");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.spriteFiringStartTick);require(encode(changed)!=before,"World encoder field spriteFiringStartTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.radiationEruptionCooldownTicksRemaining);require(encode(changed)!=before,"World encoder field radiationEruptionCooldownTicksRemaining");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.hasRenderPosition);require(encode(changed)!=before,"World encoder field hasRenderPosition");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.renderX);require(encode(changed)!=before,"World encoder field renderX");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.renderY);require(encode(changed)!=before,"World encoder field renderY");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.renderMoveActive);require(encode(changed)!=before,"World encoder field renderMoveActive");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.renderLevel);require(encode(changed)!=before,"World encoder field renderLevel");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.renderOnBridge);require(encode(changed)!=before,"World encoder field renderOnBridge");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.movementAnimationDistance);require(encode(changed)!=before,"World encoder field movementAnimationDistance");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.buildAnimTicksTotal);require(encode(changed)!=before,"World encoder field buildAnimTicksTotal");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.buildAnimTicksRemaining);require(encode(changed)!=before,"World encoder field buildAnimTicksRemaining");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.structureRecoilStartTick);require(encode(changed)!=before,"World encoder field structureRecoilStartTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.chargeTurretStartTick);require(encode(changed)!=before,"World encoder field chargeTurretStartTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.workAnimationActive);require(encode(changed)!=before,"World encoder field workAnimationActive");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.poweredOperational);require(encode(changed)!=before,"World encoder field poweredOperational");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.powerAnimationPausedAtTick);require(encode(changed)!=before,"World encoder field powerAnimationPausedAtTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.powerAnimationPausedTicks);require(encode(changed)!=before,"World encoder field powerAnimationPausedTicks");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.factoryExitAnimationStartTick);require(encode(changed)!=before,"World encoder field factoryExitAnimationStartTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.factoryExitAnimationEndTick);require(encode(changed)!=before,"World encoder field factoryExitAnimationEndTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.factoryExitAirborne);require(encode(changed)!=before,"World encoder field factoryExitAirborne");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.factoryExitHostId);require(encode(changed)!=before,"World encoder field factoryExitHostId");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.specialCashNextTick);require(encode(changed)!=before,"World encoder field specialCashNextTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.specialCloneDeliveryObjectId);require(encode(changed)!=before,"World encoder field specialCloneDeliveryObjectId");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.transportPassengerIds);require(encode(changed)!=before,"World encoder field transportPassengerIds");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.bioReactorAnimationStartTick);require(encode(changed)!=before,"World encoder field bioReactorAnimationStartTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.bioReactorAnimationPoweredStartTick);require(encode(changed)!=before,"World encoder field bioReactorAnimationPoweredStartTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.activeGunnerMode);require(encode(changed)!=before,"World encoder field activeGunnerMode");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.activeGunnerWeaponIndex);require(encode(changed)!=before,"World encoder field activeGunnerWeaponIndex");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.activeGunnerTurretIndex);require(encode(changed)!=before,"World encoder field activeGunnerTurretIndex");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.tankBunkerHostId);require(encode(changed)!=before,"World encoder field tankBunkerHostId");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.tankBunkerOccupantId);require(encode(changed)!=before,"World encoder field tankBunkerOccupantId");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.tankBunkerAnimationStartTick);require(encode(changed)!=before,"World encoder field tankBunkerAnimationStartTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.tankBunkerLowering);require(encode(changed)!=before,"World encoder field tankBunkerLowering");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.dockedToObjectId);require(encode(changed)!=before,"World encoder field dockedToObjectId");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.dockIndex);require(encode(changed)!=before,"World encoder field dockIndex");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.airborne);require(encode(changed)!=before,"World encoder field airborne");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.flightLevel);require(encode(changed)!=before,"World encoder field flightLevel");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.aircraftDockFlightProgress);require(encode(changed)!=before,"World encoder field aircraftDockFlightProgress");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.aircraftBuildingClearanceLevel);require(encode(changed)!=before,"World encoder field aircraftBuildingClearanceLevel");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.wallConnectionMask);require(encode(changed)!=before,"World encoder field wallConnectionMask");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.garrisonHostObjectId);require(encode(changed)!=before,"World encoder field garrisonHostObjectId");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.garrisonOccupantIds);require(encode(changed)!=before,"World encoder field garrisonOccupantIds");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.garrisonFireCursor);require(encode(changed)!=before,"World encoder field garrisonFireCursor");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.currentAmmo);require(encode(changed)!=before,"World encoder field currentAmmo");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.pendingAircraftAuxSounds);require(encode(changed)!=before,"World encoder field pendingAircraftAuxSounds");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.turretSoundActivityTick);require(encode(changed)!=before,"World encoder field turretSoundActivityTick");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.activeTurretSoundEvent);require(encode(changed)!=before,"World encoder field activeTurretSoundEvent");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.pendingTurretSounds);require(encode(changed)!=before,"World encoder field pendingTurretSounds");++worldProbes; }
{ WorldObjectInstance changed=base;mutateEncoderField(changed.modExtensions);require(encode(changed)!=before,"World encoder field modExtensions");++worldProbes; }
 TerrainCell terrain;const auto initial=terrainRecord(terrain);
{auto changed=terrain;mutateEncoderField(changed.x);require(terrainRecord(changed)!=initial,"Terrain encoder x");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.y);require(terrainRecord(changed)!=initial,"Terrain encoder y");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.cellIndex512);require(terrainRecord(changed)!=initial,"Terrain encoder cellIndex512");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.tileIndex);require(terrainRecord(changed)!=initial,"Terrain encoder tileIndex");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.subTileIndex);require(terrainRecord(changed)!=initial,"Terrain encoder subTileIndex");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.level);require(terrainRecord(changed)!=initial,"Terrain encoder level");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.iceGrowth);require(terrainRecord(changed)!=initial,"Terrain encoder iceGrowth");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.hasOverlay);require(terrainRecord(changed)!=initial,"Terrain encoder hasOverlay");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.overlayIndex);require(terrainRecord(changed)!=initial,"Terrain encoder overlayIndex");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.overlayFrame);require(terrainRecord(changed)!=initial,"Terrain encoder overlayFrame");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.overlayType);require(terrainRecord(changed)!=initial,"Terrain encoder overlayType");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.overlayImage);require(terrainRecord(changed)!=initial,"Terrain encoder overlayImage");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.overlayIsOre);require(terrainRecord(changed)!=initial,"Terrain encoder overlayIsOre");++terrainProbes;}
{auto changed=terrain;mutateEncoderField(changed.overlayIsGem);require(terrainRecord(changed)!=initial,"Terrain encoder overlayIsGem");++terrainProbes;}
}
bool runContract(){
 try{for(std::size_t i=0;i<cells.size();++i){auto& c=cells[i];c.x=static_cast<int>(i);c.y=static_cast<int>(i+17);c.cellIndex512=static_cast<int>(i+511);c.tileIndex=static_cast<int>(i*7);c.overlayType="overlay-"+std::to_string(i);c.overlayImage="image-"+std::to_string(i);}
 verifyEncoders();verifyOccupancy();verifyTerrain();require(worldProbes==87&&terrainProbes==14,"all owned sourcefields represented");require(faultPorts==std::set<std::string>({"find","ground","slots","key","build"}),"every reachable typedquery fault independently covered");
 std::cout<<"chrono-spatial checks="<<checks<<" states="<<states<<" world-probes="<<worldProbes<<" terrain-probes="<<terrainProbes<<" terrain-cases="<<terrainCases<<" faults="<<faults<<" fault-ports="<<faultPorts.size()<<" live-aliases="<<aliases<<" view-bytes="<<sizeof(current::RuntimeChronoTerrainView)<<" complete-query-state-and-original-bodies=exact failures=0\n";return true;
 }catch(const std::exception& e){std::cerr<<"chrono-spatial failure: "<<e.what()<<'\n';return false;}
}
}
