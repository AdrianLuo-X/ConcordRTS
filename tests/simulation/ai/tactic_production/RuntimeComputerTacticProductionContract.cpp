#include "simulation/ai/RuntimeComputerTacticProduction.h"
#include "world/model/GameWorld.h"
#include "simulation/buildings/RuntimeGrindingSystem.h"
#include "simulation/triggers/RuntimeTriggerState.h"
#include "data/rules/RulesDatabase.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
namespace domain=concord::simulation::ai;
namespace {
using domain::RuntimeProductionSpawn;
using domain::RuntimeAircraftProductionMatchInputBuild;
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
std::string catalogImage(const RuntimeBuildCatalogItem&);
int checks=0,models=0,faults=0;
void require(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
struct Trace {std::vector<std::string> calls;int failAt=0;bool after=false;
 void enter(const std::string& x){calls.push_back(x);if(!after&&static_cast<int>(calls.size())==failAt)throw std::runtime_error("fault:"+x);}
 void leave(){if(after&&static_cast<int>(calls.size())==failAt)throw std::runtime_error("fault:"+calls.back());}
};
struct Model {
 int teamId=7;bool useDefaultTeam=false;
 int tick=91,nextId=100,effectSerial=0,storedOre=70,outId=827,factoryId=10,groundId=75;
 std::uint64_t rng=0xA2827371ULL;
 RuntimeComputerAiState ai;
 RuntimeBuildCatalogItem item;
 std::map<std::string,RuntimeBuildCatalogItem> catalog;
 std::map<std::string,RuntimeComputerTacticsState> owners;
 RuntimeComputerTacticsState detached;
 bool tacticsOwned=true,itemInCatalog=false,allowed=true,groundResult=true,groundMutatesOnFailure=false,addFails=false,addAliasesHost=false,hideHostAfterAdd=false,spawnNullAnchor=false,mutateCostOnSpend=false,mutateTypeOnSpend=false,mutateTickOnRecord=false,mutateTeamOnRecord=false,mutatePowerOnLookup=false;
 bool worldReallocated=false;
 std::vector<WorldObjectInstance> objects;
 std::map<std::string,BuildingType> buildings;
 std::set<int> nullObjects;
 RuntimeAircraftProductionMatchInputBuild inputBuild;
 RuntimeAircraftProductionMatchResult match;
 RuntimeProductionSpawn dock;
 std::optional<RuntimeAircraftProductionOrder> receivedOrder;
 std::string receivedOwner;
 std::vector<std::string> actions;
 Trace trace;
};
Model* active=nullptr;
RuntimeComputerTacticsState& tactics(Model& m){return m.tacticsOwned?m.owners.at("alpha"):m.detached;}
RuntimeBuildCatalogItem& item(Model& m){return m.itemInCatalog?m.catalog.at("TANK"):m.item;}
WorldObjectInstance* lookup(Model& m,int id){auto x=std::find_if(m.objects.begin(),m.objects.end(),[=](const auto& o){return o.instanceId==id;});return x==m.objects.end()?nullptr:&*x;}
struct Rules {Model& m;};
struct TriggerView {std::map<std::string,RuntimeComputerTacticsState>& computerTacticsByOwner;};
struct Root {Model& m;int& tick;Rules rules;TriggerView triggers;explicit Root(Model& x):m(x),tick(x.tick),rules{x},triggers{x.owners}{}};
RuntimeAircraftProductionMatchInputBuild buildRuntimeAircraftProductionMatchInputForOwner(const Root& r,const std::string& owner,std::optional<RuntimeAircraftProductionOrder> order,const void* repair,bool existing){
 auto& m=r.m;m.trace.enter("input:"+owner+":"+std::to_string(order?order->serial:0)+":"+(order?order->typeId:"none")+":"+std::to_string(order?static_cast<int>(order->phase):-1));
 require(!repair&&!existing&&order.has_value()&&order->serial==1&&order->phase==RuntimeAircraftProductionOrderPhase::Prospective,"original prospective identity/args");
 m.receivedOrder=std::move(order);m.receivedOwner=owner;m.trace.leave();return m.inputBuild;
}
RuntimeAircraftProductionMatchResult modelMatchAircraftProductionDocks(const RuntimeAircraftProductionMatchInput& input){auto& m=*active;m.trace.enter("match:"+input.canonicalOwner);require(input==m.inputBuild.input,"complete matcher input exact");m.trace.leave();return m.match;}
const WorldObjectInstance* findObjectById(const Root& r,int id){auto& m=r.m;m.trace.enter("object:"+std::to_string(id));auto* p=m.nullObjects.contains(id)?nullptr:lookup(m,id);m.trace.leave();return p;}
const BuildingType* findRuntimeBuildingTypeById(const Rules& rules,const std::string& id){auto& m=rules.m;m.trace.enter("building:"+id);auto x=m.buildings.find(toUpperAsciiLocal(id));if(m.mutatePowerOnLookup){if(auto* host=lookup(m,10))host->poweredOperational=false;}m.trace.leave();return x==m.buildings.end()?nullptr:&x->second;}
RuntimeProductionSpawn runtimeAircraftDockSpawn(const Root& r,const WorldObjectInstance& host,int index){auto& m=r.m;m.trace.enter("dock:"+std::to_string(host.instanceId)+":"+std::to_string(index));auto result=m.dock;result.anchor=m.spawnNullAnchor?nullptr:&host;m.trace.leave();return result;}
bool runtimeComputerAiItemAllowed(const Root& r,const RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& value){auto& m=r.m;require(&ai==&m.ai,"same AI node borrow");m.trace.enter("allowed:"+ai.owner+":"+catalogImage(value));m.trace.leave();return m.allowed;}
std::optional<RuntimeBuildCatalogItem> runtimeComputerAiBuildItem(const Root& r,const std::string& id,const std::string& owner){auto& m=r.m;m.trace.enter("catalog:"+id+":"+owner);auto i=m.catalog.find(toUpperAsciiLocal(id));m.trace.leave();return i==m.catalog.end()?std::nullopt:std::optional<RuntimeBuildCatalogItem>{i->second};}
const WorldObjectInstance* runtimeComputerAiFactoryAnchor(const Root& r,const RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& value){auto& m=r.m;require(&ai==&m.ai,"same AI node borrow");m.trace.enter("factory:"+ai.owner+":"+catalogImage(value));auto* p=lookup(m,m.factoryId);m.trace.leave();return p;}
void spendRuntimeComputerStoredOre(Root& r,const std::string& owner,int amount){auto& m=r.m;m.trace.enter("ore:"+owner+":"+std::to_string(amount));m.storedOre-=amount;++m.effectSerial;m.actions.push_back("ore:"+std::to_string(amount));if(m.mutateCostOnSpend)item(m).cost=9;if(m.mutateTypeOnSpend)item(m).typeId="AFTER";m.trace.leave();}
bool runtimeComputerAiBuildUnit(Root& r,RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& value,int* id){auto& m=r.m;require(&ai==&m.ai,"same AI node borrow");m.trace.enter("ground:"+ai.owner+":"+catalogImage(value)+":"+std::to_string(id==&m.outId));*id=m.groundId;if(m.groundResult||m.groundMutatesOnFailure){++ai.unitsBuilt;ai.lastBuildType=value.typeId;++m.effectSerial;m.actions.push_back("ground");}m.trace.leave();return m.groundResult;}
void refreshRuntimeAircraftDockAssignments(Root& r){auto& m=r.m;m.trace.enter("refresh-docks");++m.effectSerial;m.actions.push_back("refresh-docks");m.trace.leave();}
WorldObjectInstance* addRuntimeObjectInstanceForOwner(Root& r,const std::string& owner,RuntimeBuildKind kind,const std::string& id,const RuntimeCellPoint& cell){auto& m=r.m;m.trace.enter("add:"+owner+":"+std::to_string(static_cast<int>(kind))+":"+id+":"+std::to_string(cell.x)+","+std::to_string(cell.y));if(m.addFails){m.trace.leave();return nullptr;}if(m.addAliasesHost){m.trace.leave();return lookup(m,10);}const auto before=m.objects.empty()?0:reinterpret_cast<std::uintptr_t>(m.objects.data());WorldObjectInstance o;o.instanceId=m.nextId++;o.owner=owner;o.type=id;o.x=cell.x;o.y=cell.y;o.alive=true;o.health=100;m.objects.push_back(o);m.worldReallocated=before!=reinterpret_cast<std::uintptr_t>(m.objects.data());if(m.hideHostAfterAdd)m.nullObjects.insert(10);++m.effectSerial;m.actions.push_back("add:"+std::to_string(o.instanceId));m.trace.leave();return &m.objects.back();}
void applyRuntimeProducedObjectVeterancy(Root& r,WorldObjectInstance& o){auto& m=r.m;m.trace.enter("veterancy:"+std::to_string(o.instanceId));++o.veterancy;o.veterancyExperience+=17;++m.effectSerial;m.actions.push_back("veterancy");m.trace.leave();}
void scaleRuntimeComputerAiObjectHealth(Root& r,const RuntimeComputerAiState& ai,WorldObjectInstance& o){auto& m=r.m;require(&ai==&m.ai,"same AI node borrow");m.trace.enter("health:"+ai.owner+":"+std::to_string(o.instanceId));o.health=137;++m.effectSerial;m.actions.push_back("health");m.trace.leave();}
void applyRuntimeAircraftDockPose(Root& r,WorldObjectInstance& o,const WorldObjectInstance& host,int index){auto& m=r.m;m.trace.enter("pose:"+std::to_string(o.instanceId)+":"+std::to_string(host.instanceId)+":"+std::to_string(index));o.x=host.x+1;o.y=host.y-1;o.dockIndex=index;o.dockedToObjectId=host.instanceId;++m.effectSerial;m.actions.push_back("pose");m.trace.leave();}
void recordRuntimeCompletedBuild(Root& r,const std::string& owner,const RuntimeBuildCatalogItem& value){auto& m=r.m;m.trace.enter("record:"+owner+":"+value.typeId);++m.effectSerial;m.actions.push_back("record");if(m.mutateTickOnRecord)m.tick=271;if(m.mutateTeamOnRecord){for(auto& order:tactics(m).production)if(!order.typeId.empty())order.teamInstanceId=9;}m.trace.leave();}
void refreshRuntimeObjectOccupancyAfterLocalChange(Root& r){auto& m=r.m;m.trace.enter("occupancy");++m.effectSerial;m.actions.push_back("occupancy");m.trace.leave();}
namespace original {
using RuntimeGameSessionState=Root;
using RuntimeProductionHostState=Root;
#define runtimeMatchAircraftProductionDocks modelMatchAircraftProductionDocks
#include "RuntimeComputerTacticProductionOriginal.inl"
#undef runtimeMatchAircraftProductionDocks
}
class Queries final:public domain::RuntimeComputerTacticProductionQueries {
 Root& r;
public:explicit Queries(Root& x):r(x){}
 RuntimeAircraftProductionMatchInputBuild buildAircraftInput(const std::string& owner,std::optional<RuntimeAircraftProductionOrder> order)const override{return buildRuntimeAircraftProductionMatchInputForOwner(r,owner,std::move(order),nullptr,false);}
 RuntimeAircraftProductionMatchResult matchAircraftDocks(const RuntimeAircraftProductionMatchInput& in)const override{return modelMatchAircraftProductionDocks(in);}
 const WorldObjectInstance* object(int id)const override{return findObjectById(r,id);}
 const BuildingType* buildingType(const std::string& id)const override{return findRuntimeBuildingTypeById(r.rules,id);}
 RuntimeProductionSpawn dockSpawn(const WorldObjectInstance& o,int i)const override{return runtimeAircraftDockSpawn(r,o,i);}
 bool itemAllowed(const RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& value)const override{return runtimeComputerAiItemAllowed(r,ai,value);}
 std::optional<RuntimeBuildCatalogItem> buildItem(const std::string& id,const std::string& owner)const override{return runtimeComputerAiBuildItem(r,id,owner);}
 const WorldObjectInstance* factoryAnchor(const RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& value)const override{return runtimeComputerAiFactoryAnchor(r,ai,value);}
};
class QueueEffects final:public domain::RuntimeComputerTacticQueueEffects {Root& r;public:explicit QueueEffects(Root& x):r(x){}void spendStoredOre(const std::string& owner,int amount)override{spendRuntimeComputerStoredOre(r,owner,amount);}};
class CompleteEffects final:public domain::RuntimeComputerTacticCompletionEffects {Root& r;public:explicit CompleteEffects(Root& x):r(x){}
 bool buildUnit(RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& value,int* id)override{return runtimeComputerAiBuildUnit(r,ai,value,id);}
 void refreshDocks()override{refreshRuntimeAircraftDockAssignments(r);}
 WorldObjectInstance* addObject(const std::string& owner,RuntimeBuildKind kind,const std::string& id,const RuntimeCellPoint& cell)override{return addRuntimeObjectInstanceForOwner(r,owner,kind,id,cell);}
 void applyVeterancy(WorldObjectInstance& o)override{applyRuntimeProducedObjectVeterancy(r,o);}
 void scaleAiHealth(const RuntimeComputerAiState& ai,WorldObjectInstance& o)override{scaleRuntimeComputerAiObjectHealth(r,ai,o);}
 void applyDockPose(WorldObjectInstance& o,const WorldObjectInstance& host,int i)override{applyRuntimeAircraftDockPose(r,o,host,i);}
 void recordCompletedBuild(const std::string& owner,const RuntimeBuildCatalogItem& value)override{recordRuntimeCompletedBuild(r,owner,value);}
 void refreshOccupancy()override{refreshRuntimeObjectOccupancyAfterLocalChange(r);}
};
std::string image;
void save(const std::string& value) { image+=std::to_string(value.size())+":"+value+";"; }
void save(int value) { save(std::to_string(value)); }
void save(bool value) { save(value?1:0); }
void save(double value) { save(std::to_string(std::bit_cast<std::uint64_t>(value))); }
void save(RuntimeCellPoint value) { save(value.x);save(value.y); }
void save(const RuntimeComputerTacticHistory& value) { save(value.weight);save(value.executions);save(value.successes);save(value.failures); }
void save(const RuntimeComputerTacticProduction& value) { save(value.teamInstanceId);save(value.typeId);save(value.remainingTicks);save(value.paidCost);save(value.totalTicks); }
void save(const RuntimeComputerTacticsState::CaptureDecision& value) { save(value.controllerId);save(value.startedTick);save(value.decision); }

template<class T> void sequence(const T& values) { save(static_cast<int>(values.size()));for(const auto& value:values)save(value); }
void save(const RuntimeComputerTacticTeam& value) {
    save(value.instanceId);save(value.triggerId);save(value.teamTypeId);save(value.targetOwner);
    sequence(value.objectIds);sequence(value.joiningObjectIds);sequence(value.missingMembers);
    save(value.createdTick);save(value.lastProgressTick);save(value.missionInstanceId);
    save(value.secondary);save(value.baseDefense);save(value.launched);save(value.outcomeReported);
    save(value.missionFinished);save(value.achievedGreatSuccess);
}
void save(const RuntimeComputerTacticsState&);



template<class T> void mapping(const T& values) { save(static_cast<int>(values.size()));for(const auto& [key,value]:values){save(key);save(value);} }
void save(const RuntimeComputerTacticsState& value) {
    save(value.nextTriggerTick);save(value.selectionSerial);save(value.nextTeamInstanceId);
    sequence(value.production);mapping(value.teams);mapping(value.history);mapping(value.captureDecisions);
}
void save(const RuntimeComputerAiState& value) {
    save(value.initialized);
    save(value.owner);
    save(value.country);
    save(value.difficulty);
    save(value.baseCenter);
    save(value.credits);
    save(value.nextThinkTick);
    save(value.nextIncomeTick);
    save(value.nextBuildTick);
    save(value.nextAttackTick);
    save(value.nextRepairTick);
    save(value.productionSerial);
    save(value.incomeEarned);
    save(value.creditsSpent);
    save(value.structuresBuilt);
    save(value.infantryBuilt);
    save(value.unitsBuilt);
    save(value.freeUnitsReceived);
    save(value.attackWaves);
    save(value.attacksOrdered);
    save(value.defenseOrders);
    save(value.tacticalOrders);
    save(value.repairsPerformed);
    save(value.stalledBuilds);
    save(value.lastTargetObjectId);
    save(value.lastBuildType);
    save(value.status);
}









template<class T> std::string encode(const T& v) {
    if constexpr (std::is_enum_v<T>)return "e"+std::to_string(static_cast<long long>(v))+";";
    else if constexpr (std::is_integral_v<T>)return "i"+std::to_string(v)+";";
    else if constexpr (std::is_floating_point_v<T>)return "d"+std::to_string(std::bit_cast<std::uint64_t>(v))+";";
    else if constexpr (std::is_same_v<T,std::string>)return "s"+std::to_string(v.size())+":"+v;
    else if constexpr (requires {v.has_value();v.value();})return v ? "some"+encode(*v):"none";
    else if constexpr (requires { v.kind;v.moveTarget; }) {return encode(v.kind)+encode(v.state)+encode(v.moveTarget)+encode(v.targetObjectId)+encode(v.autoAcquired)+encode(v.forceFire)+encode(v.attackGround)+encode(v.attackBridge)+encode(v.attackBridgeCell)+encode(v.path)+encode(v.pathRequestPending)+encode(v.pendingPathTowardRange)+encode(v.pendingPathStart)+encode(v.pendingPathGoal)+encode(v.pendingHasSharedGoal)+encode(v.pendingSharedGoal)+encode(v.pendingAllowedOccupiedObjectId)+encode(v.pendingTargetObjectId)+encode(v.pendingRangeCells)+encode(v.pendingAllowNearbyTarget)+encode(v.pendingIgnoredObjectIds)+encode(v.repackMoveGroup)+encode(v.moveCooldownTicks)+encode(v.moveSegmentFrom)+encode(v.moveIntervalRemainderQ16)+encode(v.moveStartupDistanceQ16)+encode(v.renderStartupAccelerationDistance)+encode(v.factoryExitClearanceHostId)+encode(v.transportTransferHostId)+encode(v.transportTransferKind)+encode(v.weaponCooldownTicks)+encode(v.blockedTicks)+encode(v.pathReplans)+encode(v.nextRepathTick)+encode(v.returnToDeployed)+encode(v.approachSlotIndex)+encode(v.renderFrom)+encode(v.renderTo)+encode(v.renderFromOffsetX)+encode(v.renderFromOffsetY)+encode(v.renderToOffsetX)+encode(v.renderToOffsetY)+encode(v.renderFromLevel)+encode(v.renderToLevel)+encode(v.renderFromOnBridge)+encode(v.renderToOnBridge)+encode(v.renderMoveTicksTotal)+encode(v.renderMoveTicksRemaining)+encode(v.renderArrivalDistanceAfter);}
    else if constexpr (requires { v.instanceId;v.mapId;v.renderX; }) {return encode(v.instanceId)+encode(v.kind)+encode(v.mapId)+encode(v.owner)+encode(v.type)+encode(v.health)+encode(v.x)+encode(v.y)+encode(v.cell)+encode(v.level)+encode(v.facing)+encode(v.turretFacing)+encode(v.bodyFacingClock)+encode(v.turretFacingClock)+encode(v.subCell)+encode(v.mission)+encode(v.tag)+encode(v.veterancy)+encode(v.veterancyExperience)+encode(v.crateArmorMultiplier)+encode(v.crateFirepowerMultiplier)+encode(v.crateSpeedMultiplier)+encode(v.crateCloakable)+encode(v.harvestAutoSearchSuspended)+encode(v.hasBeenCaptured)+encode(v.onBridge)+encode(v.authoredAircraft)+encode(v.alive)+encode(v.rubble)+encode(v.submerged)+encode(v.cloaked)+encode(v.cloakCooldownTicks)+encode(v.submergeCooldownTicks)+encode(v.deployed)+encode(v.deployStateTicksRemaining)+encode(v.prone)+encode(v.proneTicksRemaining)+encode(v.weaponCooldownTicksRemaining)+encode(v.spriteFiringStartTick)+encode(v.radiationEruptionCooldownTicksRemaining)+encode(v.hasRenderPosition)+encode(v.renderX)+encode(v.renderY)+encode(v.renderMoveActive)+encode(v.renderLevel)+encode(v.renderOnBridge)+encode(v.movementAnimationDistance)+encode(v.buildAnimTicksTotal)+encode(v.buildAnimTicksRemaining)+encode(v.structureRecoilStartTick)+encode(v.chargeTurretStartTick)+encode(v.workAnimationActive)+encode(v.poweredOperational)+encode(v.powerAnimationPausedAtTick)+encode(v.powerAnimationPausedTicks)+encode(v.factoryExitAnimationStartTick)+encode(v.factoryExitAnimationEndTick)+encode(v.factoryExitAirborne)+encode(v.factoryExitHostId)+encode(v.specialCashNextTick)+encode(v.specialCloneDeliveryObjectId)+encode(v.transportPassengerIds)+encode(v.bioReactorAnimationStartTick)+encode(v.bioReactorAnimationPoweredStartTick)+encode(v.activeGunnerMode)+encode(v.activeGunnerWeaponIndex)+encode(v.activeGunnerTurretIndex)+encode(v.tankBunkerHostId)+encode(v.tankBunkerOccupantId)+encode(v.tankBunkerAnimationStartTick)+encode(v.tankBunkerLowering)+encode(v.dockedToObjectId)+encode(v.dockIndex)+encode(v.airborne)+encode(v.flightLevel)+encode(v.aircraftDockFlightProgress)+encode(v.aircraftBuildingClearanceLevel)+encode(v.wallConnectionMask)+encode(v.garrisonHostObjectId)+encode(v.garrisonOccupantIds)+encode(v.garrisonFireCursor)+encode(v.currentAmmo)+encode(v.pendingAircraftAuxSounds)+encode(v.turretSoundActivityTick)+encode(v.activeTurretSoundEvent)+encode(v.pendingTurretSounds)+encode(v.modExtensions);}
    else if constexpr (requires { v.elapsedNumerator;v.lastFacing; }) {return encode(v.initial)+encode(v.target)+encode(v.rate)+encode(v.elapsedNumerator)+encode(v.lastTick)+encode(v.lastFacing);}
    else if constexpr (requires { v.technoType;v.shieldType;v.effects; }) {return encode(v.initialized)+encode(v.technoType)+encode(v.shieldType)+encode(v.shieldCapacity)+encode(v.shieldHealth)+encode(v.shieldTimer)+encode(v.shieldOnline)+encode(v.innateDelayTicks)+encode(v.effects);}
    else if constexpr (requires { v.sourceType;v.fromWarhead; }) {return encode(v.sourceType)+encode(v.fromWarhead)+encode(v.remainingTicks)+encode(v.elapsedTicks);}
    else if constexpr (requires { v.event;v.takingOff; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.takingOff);}
    else if constexpr (requires { v.event;v.rotating; }) {return encode(v.event)+encode(v.tick)+encode(v.x)+encode(v.y)+encode(v.level)+encode(v.rotating);}
    else if constexpr (requires { v.center;v.members; }) {return encode(v.center)+encode(v.members);}
    else if constexpr (requires { v.x;v.y; }) {return encode(v.x)+encode(v.y);}
    else if constexpr (requires {v.begin();v.end();}) {
        std::vector<std::string> parts;for(const auto& item:v)parts.push_back(encode(item));
        if constexpr (requires {typename T::key_type;})std::sort(parts.begin(),parts.end());
        std::string out="n"+std::to_string(parts.size())+":";for(const auto& part:parts)out+=std::to_string(part.size())+":"+part;return out;
    } else static_assert(sizeof(T)==0,"unencoded complete field");
}

std::string catalogImage(const RuntimeBuildCatalogItem& v){return encode(v.kind)+encode(v.typeId)+encode(v.label)+encode(v.owner)+encode(v.requiredHouses)+encode(v.forbiddenHouses)+encode(v.prerequisite)+encode(v.prerequisiteOverride)+encode(v.requiredFactory)+encode(v.cost)+encode(v.buildTicks)+encode(v.powerDelta)+encode(v.techLevel)+encode(v.buildLimit)+encode(v.waterBound)+encode(v.naval)+encode(v.wall)+encode(v.techTreeAllowed)+encode(v.secretLabInjected)+encode(v.techTreeKind)+encode(v.techTreeBlockedReason);}

std::string snapshot(const Model& m){image.clear();save(m.teamId);save(m.useDefaultTeam);save(m.tick);save(m.nextId);save(m.effectSerial);save(m.storedOre);save(m.outId);save(m.factoryId);save(m.groundId);save(std::to_string(m.rng));save(m.ai);save(catalogImage(m.item));for(const auto& [key,value]:m.catalog){save(key);save(catalogImage(value));}mapping(m.owners);save(m.detached);save(encode(m.objects));for(const auto& [key,value]:m.buildings){save(key);save(value.powered);}sequence(m.nullObjects);save(m.dock.cell);save(m.dock.dockIndex);save(m.dock.aircraftDock);save(m.dock.renderX);save(m.dock.renderY);save(m.dock.level);save(m.receivedOwner);sequence(m.actions);save(m.tacticsOwned);save(m.itemInCatalog);save(m.allowed);save(m.groundResult);save(m.groundMutatesOnFailure);save(m.addFails);save(m.addAliasesHost);save(m.hideHostAfterAdd);save(m.spawnNullAnchor);save(m.mutateCostOnSpend);save(m.mutateTypeOnSpend);save(m.mutateTickOnRecord);save(m.mutateTeamOnRecord);save(m.mutatePowerOnLookup);save(m.worldReallocated); return image;}
bool sameState(const Model& a,const Model& b){return snapshot(a)==snapshot(b)&&a.inputBuild.input==b.inputBuild.input&&a.inputBuild.error==b.inputBuild.error&&a.match==b.match&&a.receivedOrder==b.receivedOrder;}
enum class Operation{Lane,Aircraft,Complete,Queue,Advance};
struct Outcome {int value=-999;std::string payload,error;bool operator==(const Outcome&)const=default;};
std::string spawnImage(const Model& m,const RuntimeProductionSpawn& s){require(!s.anchor||s.anchor==lookup(const_cast<Model&>(m),s.anchor->instanceId),"spawn original world-node borrow");return encode(s.anchor?s.anchor->instanceId:-1)+encode(s.cell)+encode(s.dockIndex)+encode(s.aircraftDock)+encode(s.renderX)+encode(s.renderY)+encode(s.level);}
Outcome invoke(Model& m,Operation op,bool old){Root r(m);Queries q(r);QueueEffects qe(r);CompleteEffects ce(r);active=&m;Outcome out;try{
 if(op==Operation::Lane)out.value=old?original::runtimeComputerTacticLane(item(m)):domain::runtimeComputerTacticLane(item(m));
 else if(op==Operation::Aircraft){auto s=old?original::runtimeComputerTacticAircraftSpawn(r,m.ai.owner,item(m)):domain::runtimeComputerTacticAircraftSpawn(q,m.ai.owner,item(m));out.value=s?1:0;if(s)out.payload=spawnImage(m,*s);}
 else if(op==Operation::Complete)out.value=old?original::completeRuntimeComputerTacticUnit(r,m.ai,item(m),m.outId):domain::completeRuntimeComputerTacticUnit(q,ce,m.ai,item(m),m.outId);
 else if(op==Operation::Queue){if(m.useDefaultTeam)out.value=old?original::queueRuntimeComputerTacticProduction(r,m.ai,item(m)):domain::queueRuntimeComputerTacticProduction(m.owners,q,qe,m.ai,item(m));else out.value=old?original::queueRuntimeComputerTacticProduction(r,m.ai,item(m),m.teamId):domain::queueRuntimeComputerTacticProduction(m.owners,q,qe,m.ai,item(m),m.teamId);}
 else out.value=old?original::advanceRuntimeComputerTacticProduction(r,m.ai,tactics(m)):domain::advanceRuntimeComputerTacticProduction(m.tick,q,ce,m.ai,tactics(m));
 }catch(const std::runtime_error& e){out.error=e.what();}active=nullptr;return out;}
void compare(const std::string& name,const Model& seed,Operation op,int expected,const std::function<void(const Model&)>& verify={}){
 Model probe=seed;probe.trace={};auto normal=invoke(probe,op,true);require(normal.error.empty()&&normal.value==expected,name+" authored outcome:"+normal.error+" got="+std::to_string(normal.value));if(verify)verify(probe);const int calls=static_cast<int>(probe.trace.calls.size());
 for(bool after:{false,true})for(int fail=0;fail<=calls+1;++fail){Model a=seed,b=seed;a.trace={};b.trace={};a.trace.failAt=b.trace.failAt=fail;a.trace.after=b.trace.after=after;auto x=invoke(a,op,true),y=invoke(b,op,false);++models;const bool reached=fail>0&&fail<=calls;if(reached)++faults;require(x==y,name+" outcome "+std::to_string(fail));require(a.trace.calls==b.trace.calls,name+" ordered typed dependencies "+std::to_string(fail));require(sameState(a,b),name+" complete controlled state/partial commit "+std::to_string(fail));require(reached?x.error.starts_with("fault:"):(x.error.empty()&&x.value==expected),name+" fault reachability");}
}
Model seed(bool aircraft=false){Model m;m.ai.owner=" Alpha ";m.ai.country="Americans";m.ai.difficulty="Hard";m.ai.credits=100;m.ai.productionSerial=31;m.ai.lastBuildType="old";m.ai.status="sentinel";m.item.kind=RuntimeBuildKind::Unit;m.item.typeId="TANK";m.item.requiredFactory=aircraft?"AircraftType":"WeaponType";m.item.cost=20;m.item.buildTicks=3;m.catalog["TANK"]=m.item;
 auto& t=m.owners["alpha"].teams[7];t.instanceId=7;t.launched=true;t.objectIds={11};t.joiningObjectIds={};t.missingMembers={"tank","TANK","unrelated"};t.lastProgressTick=3;m.owners["alpha"].nextTeamInstanceId=44;m.owners["beta"].selectionSerial=51;m.detached=m.owners["alpha"];
 WorldObjectInstance host;host.instanceId=10;host.owner=m.ai.owner;host.type="FACTORY";host.poweredOperational=true;host.alive=true;host.health=500;host.x=8;host.y=10;m.objects.push_back(host);m.buildings["FACTORY"].powered=true;m.inputBuild.input.canonicalOwner="alpha";m.match.inputValid=m.match.feasible=true;RuntimeAircraftProductionAssignment a;a.canonicalOwner="alpha";a.orderSerial=1;a.typeId="TANK";a.phase=RuntimeAircraftProductionOrderPhase::Prospective;a.hostId=10;a.dockIndex=2;m.match.assignments.push_back(a);m.dock.cell={17,19};m.dock.dockIndex=2;m.dock.aircraftDock=true;m.dock.renderX=17.25;m.dock.renderY=-19.125;m.dock.level=4;return m;}
void order(Model& m,int lane=1,int ticks=1){auto& o=tactics(m).production[lane];o.teamInstanceId=7;o.typeId="TANK";o.remainingTicks=ticks;o.paidCost=20;o.totalTicks=3;}

void laneCases(){for(auto kind:{RuntimeBuildKind::Building,RuntimeBuildKind::Defense,RuntimeBuildKind::Infantry,RuntimeBuildKind::Unit})for(const std::string& factory:std::vector<std::string>{"","AircraftType","aircrafttype"," AIRCRAFTTYPE","WeaponType"}){auto m=seed();m.item.kind=kind;m.item.requiredFactory=factory;const int expected=kind==RuntimeBuildKind::Infantry?0:toUpperAsciiLocal(factory)=="AIRCRAFTTYPE"?2:1;compare("lane",m,Operation::Lane,expected);}}
void aircraftCases(){
 auto c=[](const std::string& name,const std::function<void(Model&)>& edit,int expected){auto m=seed(true);edit(m);compare(name,m,Operation::Aircraft,expected);};
 c("air-default",[](Model&){},1);c("air-input-error",[](Model& m){m.inputBuild.error=RuntimeAircraftProductionMatchResult{};},0);c("air-no-feasible",[](Model& m){m.match.feasible=false;},0);c("air-invalid-feasible-preserved",[](Model& m){m.match.inputValid=false;},1);c("air-empty-assignment",[](Model& m){m.match.assignments.clear();},0);c("air-wrong-serial",[](Model& m){m.match.assignments[0].orderSerial=2;},0);c("air-missing-host",[](Model& m){m.match.assignments[0].hostId=87;},0);c("air-missing-rule",[](Model& m){m.buildings.clear();},0);c("air-power-paused",[](Model& m){m.objects[0].poweredOperational=false;},0);c("air-unpowered-rule-allows",[](Model& m){m.objects[0].poweredOperational=false;m.buildings["FACTORY"].powered=false;},1);c("air-live-object-after-rule-query",[](Model& m){m.mutatePowerOnLookup=true;},0);c("air-null-anchor-still-optional",[](Model& m){m.spawnNullAnchor=true;},1);c("air-prior-wrong-serial",[](Model& m){auto a=m.match.assignments[0];a.orderSerial=9;m.match.assignments.insert(m.match.assignments.begin(),a);},1);c("air-prior-missing-host",[](Model& m){auto a=m.match.assignments[0];a.hostId=-4;m.match.assignments.insert(m.match.assignments.begin(),a);},1);c("air-exact-spawn-bits",[](Model& m){m.dock.renderX=std::bit_cast<double>(std::uint64_t{0x7ff80000000000AFULL});m.dock.renderY=-0.0;m.dock.cell={-7,29};m.dock.level=-3;},1);
}
void completeCases(){
 compare("ground-complete",seed(),Operation::Complete,1,[](const Model& m){require(m.outId==75&&m.ai.unitsBuilt==1&&m.actions==std::vector<std::string>{"ground"},"ground helper result/onlycommand");});
 auto m=seed();m.groundResult=false;compare("ground-false-output-preserved",m,Operation::Complete,0,[](const Model& x){require(x.outId==75&&x.ai.unitsBuilt==0,"failed helper output retained");});m.groundMutatesOnFailure=true;compare("ground-false-partial-preserved",m,Operation::Complete,0,[](const Model& x){require(x.outId==75&&x.ai.unitsBuilt==1,"failed helper partial mutation retained");});
 compare("air-complete",seed(true),Operation::Complete,1,[](const Model& x){require(x.outId==100&&x.ai.unitsBuilt==1&&x.ai.lastBuildType=="TANK"&&x.worldReallocated,"air created/root-node reallocation");require(x.objects.back().health==137&&x.objects.back().veterancy==1&&x.objects.back().dockedToObjectId==10,"air complete effects");require(x.actions==std::vector<std::string>{"refresh-docks","add:100","veterancy","health","pose","record","occupancy"},"exact air command order");});
 m=seed(true);m.inputBuild.error=RuntimeAircraftProductionMatchResult{};compare("air-error-reset",m,Operation::Complete,0,[](const Model& x){require(x.outId==-1&&x.actions==std::vector<std::string>{"refresh-docks"},"output reset before failed input");});
 m=seed(true);m.spawnNullAnchor=true;compare("air-null-anchor-reject-completion",m,Operation::Complete,0,[](const Model& x){require(x.outId==-1&&x.objects.size()==1,"nullanchor not create");});
 m=seed(true);m.addFails=true;compare("air-create-fails",m,Operation::Complete,0,[](const Model& x){require(x.outId==-1&&x.ai.unitsBuilt==0,"failed creation out/AI");});
 m=seed(true);m.hideHostAfterAdd=true;compare("air-host-disappears-after-create",m,Operation::Complete,1,[](const Model& x){require(x.objects.back().x==17&&x.objects.back().y==19&&x.objects.back().dockIndex==-1,"losthost skips pose only");});
 m=seed(true);m.addAliasesHost=true;compare("air-node-alias",m,Operation::Complete,1,[](const Model& x){require(x.outId==10&&x.objects.size()==1&&x.objects[0].x==9&&x.objects[0].y==9,"same aircraft/host alias");});
}
void queueCases(){for(int lane:{0,1,2})for(int credits:{-10,0,19,20,50})for(int cost:{-7,0,20})for(int ticks:{-3,0,1,5}){auto m=seed(lane==2);m.item.kind=lane==0?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit;m.ai.credits=credits;m.item.cost=cost;m.item.buildTicks=ticks;const bool expected=credits>=cost;compare("queue-matrix",m,Operation::Queue,expected,[=](const Model& x){const auto& o=x.owners.at("alpha").production[lane];if(expected)require(o.typeId=="TANK"&&o.teamInstanceId==7&&o.remainingTicks==std::max(1,ticks)&&o.totalTicks==std::max(1,ticks)&&o.paidCost==cost&&x.ai.credits==credits-cost&&x.ai.creditsSpent==cost&&x.ai.productionSerial==32,"queue quota/debit/serial/clamp");else require(o.typeId.empty()&&x.ai.productionSerial==31,"rejected queue unchanged");});}
 auto m=seed();m.owners.erase("alpha");m.allowed=false;compare("queue-rejected-owner-insert",m,Operation::Queue,0,[](const Model& x){require(x.owners.contains("alpha")&&x.owners.at("alpha").production[1].typeId.empty(),"owner insertion before validation");});
 m=seed();m.owners["alpha"].production[1].typeId="EXISTING";compare("queue-occupied-shortcircuit",m,Operation::Queue,0,[](const Model& x){require(x.trace.calls.empty(),"occupied skips eligibility");});
 m=seed(true);m.match.feasible=false;compare("queue-air-no-pad",m,Operation::Queue,0);
 m=seed(true);m.spawnNullAnchor=true;compare("queue-air-optional-no-anchor",m,Operation::Queue,1);
 m=seed();m.itemInCatalog=true;m.mutateCostOnSpend=m.mutateTypeOnSpend=true;compare("queue-live-item-alias",m,Operation::Queue,1,[](const Model& x){const auto& o=x.owners.at("alpha").production[1];require(o.paidCost==20&&o.typeId=="TANK"&&x.ai.credits==91&&x.ai.creditsSpent==9&&x.ai.status=="building AFTER","live item reads after spend");});
 m=seed();m.useDefaultTeam=true;compare("queue-default-team",m,Operation::Queue,1,[](const Model& x){require(x.owners.at("alpha").production[1].teamInstanceId==-1,"original default -1");});
}
void advanceCases(){
 compare("advance-empty",seed(),Operation::Advance,0);
 for(int ticks:{-2,0,1,3})for(bool ok:{false,true})for(bool launched:{false,true}){auto m=seed();m.groundResult=ok;order(m,1,ticks);tactics(m).teams[7].launched=launched;const bool complete=ticks<=1&&ok;const bool changed=ticks>0||complete;compare("advance-progress-matrix",m,Operation::Advance,changed,[=](const Model& x){const auto& t=x.owners.at("alpha");const auto& o=t.production[1];if(complete){require(o.typeId.empty()&&t.teams.at(7).objectIds==std::vector<int>{11,75}&&t.teams.at(7).joiningObjectIds==(launched?std::vector<int>{75}:std::vector<int>{})&&t.teams.at(7).missingMembers==std::vector<std::string>{"TANK","unrelated"}&&t.teams.at(7).lastProgressTick==91,"advance membership/first-missing/clear");}else require(o.typeId=="TANK"&&o.remainingTicks==(ticks>0?ticks-1:ticks),"partial countdown and paid order retained");});}
 auto m=seed();order(m);m.catalog.clear();compare("advance-missing-catalog",m,Operation::Advance,0);
 m=seed();order(m);m.allowed=false;compare("advance-ineligible-paused",m,Operation::Advance,0);
 m=seed();order(m);m.factoryId=-8;compare("advance-no-factory",m,Operation::Advance,0);
 m=seed();order(m);m.buildings.clear();compare("advance-no-building-rule",m,Operation::Advance,0);
 m=seed();order(m);m.objects[0].poweredOperational=false;compare("advance-no-power",m,Operation::Advance,0);
 m.buildings["FACTORY"].powered=false;compare("advance-unpowered-rule",m,Operation::Advance,1);
 m=seed(true);order(m);m.match.feasible=false;compare("advance-air-no-pad-paused",m,Operation::Advance,0);
 m=seed();order(m);tactics(m).teams.clear();compare("advance-no-team-completes",m,Operation::Advance,1,[](const Model& x){require(x.owners.at("alpha").production[1].typeId.empty()&&x.owners.at("alpha").teams.empty(),"no implicit team creation");});
 m=seed(true);order(m);m.mutateTickOnRecord=m.mutateTeamOnRecord=true;tactics(m).teams[9].instanceId=9;tactics(m).teams[9].missingMembers={"TANK"};compare("advance-live-team-and-tick",m,Operation::Advance,1,[](const Model& x){const auto& t=x.owners.at("alpha");require(t.teams.at(9).objectIds==std::vector<int>{100}&&t.teams.at(9).lastProgressTick==271&&t.teams.at(9).missingMembers.empty()&&t.teams.at(7).objectIds==std::vector<int>{11},"team and tick read after completion");});
 m=seed();m.tacticsOwned=false;order(m);compare("advance-detached-state-borrow",m,Operation::Advance,1,[](const Model& x){require(x.detached.production[1].typeId.empty()&&x.owners.at("alpha").teams.at(7).objectIds==std::vector<int>{11},"uses passed detached tactics");});
 m=seed();for(int lane=0;lane<3;++lane)order(m,lane);compare("advance-three-ordered-lanes",m,Operation::Advance,1,[](const Model& x){const auto& t=x.owners.at("alpha");require(t.teams.at(7).objectIds==std::vector<int>{11,75,75,75}&&x.ai.unitsBuilt==3,"all3ordered lanes no new dedup");});
}
}
int main(){try{static_assert(sizeof(RuntimeProductionSpawn)==48&&alignof(RuntimeProductionSpawn)==8);static_assert(offsetof(RuntimeProductionSpawn,anchor)==0&&offsetof(RuntimeProductionSpawn,cell)==8&&offsetof(RuntimeProductionSpawn,dockIndex)==16&&offsetof(RuntimeProductionSpawn,aircraftDock)==20&&offsetof(RuntimeProductionSpawn,renderX)==24&&offsetof(RuntimeProductionSpawn,renderY)==32&&offsetof(RuntimeProductionSpawn,level)==40);laneCases();aircraftCases();completeCases();queueCases();advanceCases();std::cout<<"tactic-production checks="<<checks<<" model-states="<<models<<" injected-faults="<<faults<<" before-after-commands=exact full-controlled-state=exact failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<" checks="<<checks<<"\n";return 1;}}
