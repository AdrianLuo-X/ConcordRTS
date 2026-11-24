#include "simulation/ai/RuntimeComputerTacticRecruitment.h"
#include "world/model/GameWorld.h"
#include "simulation/buildings/RuntimeGrindingSystem.h"
#include "data/rules/RulesDatabase.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
namespace domain=concord::simulation::ai;
namespace {
using runtime_save_text::toLowerAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
int checks=0,models=0,faults=0;
void require(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
struct Trace {std::vector<std::string> calls;int failAt=0;void hit(const std::string& call){calls.push_back(call);if(static_cast<int>(calls.size())==failAt)throw std::runtime_error("fault:"+call);}};
struct Model {
    int tick=91,effectSerial=0;
    std::uint64_t rng=0x827391ULL;int nextMissionId=19;
    RuntimeComputerAiState ai;
    RuntimeComputerTacticsState detachedTactics;RuntimeComputerTacticTeam detachedTeam;
    bool tacticsOwned=true,teamOwned=true;
    std::map<std::string,RuntimeComputerTacticsState> owners;
    std::map<std::string,RuntimeTeamType> teams;
    std::map<std::string,RuntimeTaskForce> forces;
    std::map<std::string,RuntimeScriptType> scripts;
    std::map<int,RuntimeTeamMissionState> missions;
    RuntimeTeamType desiredType;
    std::map<int,int> engineer,transport,occupy;
    std::vector<RuntimeGrindingRelation> grinding;
    std::map<std::string,UnitType> units;
    std::map<int,bool> ammo;
    std::vector<WorldObjectInstance> objects;
    std::set<int> disabled,nullMutable,nullConst;
    std::map<int,int> behaviors;
    std::map<int,RuntimeCellPoint> objectCells;
    std::map<std::string,double> distances;
    RuntimeCellPoint center{8,9},afterAttackCell{21,-3};
    int attackResult=1,moveResult=1,mutateMission=9;
    bool attackMutates=false;
    std::vector<std::string> actions;
    Trace trace;
};
Model* active=nullptr;
RuntimeComputerTacticsState& tactic(Model& m){return m.tacticsOwned?m.owners.at("yuri"):m.detachedTactics;}
RuntimeComputerTacticTeam& target(Model& m){return m.teamOwned?m.owners.at("yuri").teams.at(7):m.detachedTeam;}
std::string pointKey(const RuntimeCellPoint& a,const RuntimeCellPoint& b){return std::to_string(a.x)+","+std::to_string(a.y)+":"+std::to_string(b.x)+","+std::to_string(b.y);}
WorldObjectInstance* lookup(Model& m,int id){auto i=std::find_if(m.objects.begin(),m.objects.end(),[=](const auto& o){return o.instanceId==id;});return i==m.objects.end()?nullptr:&*i;}
struct Pending {Model& m;const std::map<int,int>& data;const char* name;bool contains(int id)const{m.trace.hit(std::string(name)+":"+std::to_string(id));return data.contains(id);}};
template<class T> struct DefinitionMap {Model& m;const std::map<std::string,T>& data;const char* name;auto find(const std::string& id)const{m.trace.hit(std::string(name)+":"+id);return data.find(id);}auto end()const{return data.end();}};
struct OwnerMap {Model& m;const std::map<std::string,RuntimeComputerTacticsState>& data;auto begin()const{m.trace.hit("owners");return data.begin();}auto end()const{return data.end();}};
struct Grinding {Model& m;const std::vector<RuntimeGrindingRelation>& data;const auto& relations()const{m.trace.hit("grinding");return data;}};
struct World {Model& m;const auto& getObjects()const{m.trace.hit("objects");return m.objects;}};
struct Rules {Model& m;};
struct TriggerView {DefinitionMap<RuntimeTaskForce> taskForces;DefinitionMap<RuntimeTeamType> teams;OwnerMap computerTacticsByOwner;std::map<int,RuntimeTeamMissionState>& teamMissions;const std::map<std::string,RuntimeScriptType>& scripts;};
struct Root {
    Model& model;int& tick;Rules rules;World world;Grinding grinding;TriggerView triggers;Pending pendingEngineerCaptures,pendingTransportTargets,pendingOccupyTargets;
    explicit Root(Model& m):model(m),tick(m.tick),rules{m},world{m},grinding{m,m.grinding},triggers{{m,m.forces,"force"},{m,m.teams,"type"},{m,m.owners},m.missions,m.scripts},pendingEngineerCaptures{m,m.engineer,"engineer"},pendingTransportTargets{m,m.transport,"transport"},pendingOccupyTargets{m,m.occupy,"occupy"}{}
};
bool runtimeComputerAiOwnerMatches(const WorldObjectInstance& object,const std::string& owner){active->trace.hit("owner:"+std::to_string(object.instanceId)+":"+owner);return toLowerAsciiLocal(object.owner)==toLowerAsciiLocal(owner);}
bool runtimeObjectDisabledByParasite(const Root& root,int id){root.model.trace.hit("disabled:"+std::to_string(id));return root.model.disabled.contains(id);}
const UnitType* findRuntimeUnitType(const Rules& rules,const WorldObjectInstance& object){rules.m.trace.hit("unit:"+std::to_string(object.instanceId)+":"+object.type);auto it=rules.m.units.find(toUpperAsciiLocal(object.type));return it==rules.m.units.end()?nullptr:&it->second;}
bool runtimeObjectHasAttackAmmo(const Rules& rules,const WorldObjectInstance& object){rules.m.trace.hit("ammo:"+std::to_string(object.instanceId));auto it=rules.m.ammo.find(object.instanceId);return it==rules.m.ammo.end() || it->second;}
RuntimeCellPoint runtimeCellPointForObject(const WorldObjectInstance& object){active->trace.hit("point:"+std::to_string(object.instanceId));auto it=active->objectCells.find(object.instanceId);return it==active->objectCells.end()?RuntimeCellPoint{object.x,object.y}:it->second;}
double runtimeCellDistance(const RuntimeCellPoint& a,const RuntimeCellPoint& b){active->trace.hit("distance:"+pointKey(a,b));auto it=active->distances.find(pointKey(a,b));return it==active->distances.end()?std::hypot(static_cast<double>(a.x)-b.x,static_cast<double>(a.y)-b.y):it->second;}
void detachRuntimeObjectFromTeamMembership(Root& root,int id){auto& m=root.model;m.trace.hit("detach:"+std::to_string(id));for(auto& [house,state]:m.owners)for(auto& [key,team]:state.teams){std::erase(team.objectIds,id);std::erase(team.joiningObjectIds,id);}++m.effectSerial;m.actions.push_back("detach:"+std::to_string(id));}
void eraseRuntimeBehavior(Root& root,int id){auto& m=root.model;m.trace.hit("erase:"+std::to_string(id));m.behaviors.erase(id);++m.effectSerial;m.actions.push_back("erase:"+std::to_string(id));}
WorldObjectInstance* findMutableObjectById(Root& root,int id){root.model.trace.hit("mutable:"+std::to_string(id));return root.model.nullMutable.contains(id)?nullptr:lookup(root.model,id);}
const WorldObjectInstance* findObjectById(const Root& root,int id){root.model.trace.hit("object:"+std::to_string(id));return root.model.nullConst.contains(id)?nullptr:lookup(root.model,id);}
RuntimeCellPoint runtimeTeamMissionCenter(const Root& root,const RuntimeTeamMissionState& mission){std::string call="center";for(int id:mission.objectIds)call+=":"+std::to_string(id);root.model.trace.hit(call);return root.model.center;}
int issueRuntimeTeamAttack(Root& root,const std::vector<int>& ids,const WorldObjectInstance& object){auto& m=root.model;std::string call="attack:"+std::to_string(object.instanceId);for(int id:ids)call+=":"+std::to_string(id);m.trace.hit(call);m.actions.push_back(call);++m.effectSerial;if(m.attackMutates)m.missions.at(m.mutateMission).targetCell=m.afterAttackCell;return m.attackResult;}
int issueRuntimeTeamMove(Root& root,const std::vector<int>& ids,const RuntimeCellPoint& cell){auto& m=root.model;std::string call="move:"+std::to_string(cell.x)+","+std::to_string(cell.y);for(int id:ids)call+=":"+std::to_string(id);m.trace.hit(call);m.actions.push_back(call);++m.effectSerial;return m.moveResult;}
namespace original {
using RuntimeGameSessionState=Root;using RuntimeProductionHostState=Root;
#include "RuntimeComputerTacticRecruitmentOriginal.inl"
}

class Queries final:public domain::RuntimeComputerTacticRecruitmentQueries {
    Root& root;
public:
    explicit Queries(Root& r):root(r){}
    bool pendingTransportTarget(int id)const override{return root.pendingTransportTargets.contains(id);}
    bool pendingEngineerCapture(int id)const override{return root.pendingEngineerCaptures.contains(id);}
    bool pendingOccupyTarget(int id)const override{return root.pendingOccupyTargets.contains(id);}
    const std::vector<RuntimeGrindingRelation>& grindingRelations()const override{return root.grinding.relations();}
    const std::map<std::string,RuntimeComputerTacticsState>& ownerTactics()const override{root.model.trace.hit("owners");return root.model.owners;}
    bool ownerMatches(const WorldObjectInstance& o,const std::string& s)const override{return runtimeComputerAiOwnerMatches(o,s);}
    bool disabledByParasite(int id)const override{return runtimeObjectDisabledByParasite(root,id);}
    const UnitType* unitType(const WorldObjectInstance& o)const override{return findRuntimeUnitType(root.rules,o);}
    bool hasAttackAmmo(const WorldObjectInstance& o)const override{return runtimeObjectHasAttackAmmo(root.rules,o);}
    const RuntimeTaskForce* taskForce(const std::string& id)const override{auto it=root.triggers.taskForces.find(id);return it==root.triggers.taskForces.end()?nullptr:&it->second;}
    const RuntimeTeamType* teamType(const std::string& id)const override{auto it=root.triggers.teams.find(id);return it==root.triggers.teams.end()?nullptr:&it->second;}
    const std::vector<WorldObjectInstance>& objects()const override{return root.world.getObjects();}
    RuntimeCellPoint objectCell(const WorldObjectInstance& o)const override{return runtimeCellPointForObject(o);}
    double cellDistance(const RuntimeCellPoint& a,const RuntimeCellPoint& b)const override{return runtimeCellDistance(a,b);}
    const WorldObjectInstance* object(int id)const override{return findObjectById(root,id);}
    RuntimeCellPoint missionCenter(const RuntimeTeamMissionState& m)const override{return runtimeTeamMissionCenter(root,m);}
};
class Effects final:public domain::RuntimeComputerTacticRecruitmentEffects {
    Root& root;
public:
    explicit Effects(Root& r):root(r){}
    void detachMember(int id)override{detachRuntimeObjectFromTeamMembership(root,id);}
    void eraseBehavior(int id)override{eraseRuntimeBehavior(root,id);}
    WorldObjectInstance* mutableObject(int id)override{return findMutableObjectById(root,id);}
    int issueTeamAttack(const std::vector<int>& ids,const WorldObjectInstance& o)override{return issueRuntimeTeamAttack(root,ids,o);}
    int issueTeamMove(const std::vector<int>& ids,const RuntimeCellPoint& p)override{return issueRuntimeTeamMove(root,ids,p);}
};
std::string image;
void save(const std::string& value) { image+=std::to_string(value.size())+":"+value+";"; }
void save(int value) { save(std::to_string(value)); }
void save(std::size_t value) { save(std::to_string(value)); }
void save(bool value) { save(value?1:0); }
void save(double value) { save(std::to_string(std::bit_cast<std::uint64_t>(value))); }
void save(RuntimeCellPoint value) { save(value.x);save(value.y); }
void save(const RuntimeComputerTacticHistory& value) { save(value.weight);save(value.executions);save(value.successes);save(value.failures); }
void save(const RuntimeComputerTacticProduction& value) { save(value.teamInstanceId);save(value.typeId);save(value.remainingTicks);save(value.paidCost);save(value.totalTicks); }
void save(const RuntimeComputerTacticsState::CaptureDecision& value) { save(value.controllerId);save(value.startedTick);save(value.decision); }
void save(const RuntimeTaskForce&);void save(const RuntimeTaskForceMember&);void save(const RuntimeScriptStep&);void save(const RuntimeGrindingRelation&);void save(const UnitType&);
template<class T> void sequence(const T& values) { save(static_cast<int>(values.size()));for(const auto& value:values)save(value); }
void save(const RuntimeComputerTacticTeam& value) {
    save(value.instanceId);save(value.triggerId);save(value.teamTypeId);save(value.targetOwner);
    sequence(value.objectIds);sequence(value.joiningObjectIds);sequence(value.missingMembers);
    save(value.createdTick);save(value.lastProgressTick);save(value.missionInstanceId);
    save(value.secondary);save(value.baseDefense);save(value.launched);save(value.outcomeReported);
    save(value.missionFinished);save(value.achievedGreatSuccess);
}
void save(const RuntimeComputerTacticsState&);
void save(const RuntimeTeamMissionState&);
void save(const RuntimeTeamType&);
void save(const RuntimeScriptType&);
template<class T> void mapping(const T& values) { save(static_cast<int>(values.size()));for(const auto& [key,value]:values){save(key);save(value);} }
void save(const RuntimeComputerTacticsState& value) {
    save(value.nextTriggerTick);save(value.selectionSerial);save(value.nextTeamInstanceId);
    sequence(value.production);mapping(value.teams);mapping(value.history);mapping(value.captureDecisions);
}
void save(const RuntimeTeamMissionState& v){save(v.instanceId);save(v.teamTypeId);save(v.scriptId);sequence(v.objectIds);save(v.stepIndex);save(v.stepStartedTick);save(v.stepIssued);save(v.targetObjectId);save(v.targetCell);save(v.completed);save(v.sourceAiTriggerId);save(v.sourceAiBuildId);save(v.aiOutcomeReported);save(v.achievedGreatSuccess);save(v.targetInteractionCompleted);mapping(v.transportReturnCells);save(v.regrouping);save(v.regroupCell);save(v.regroupStartedTick);mapping(v.regroupDestinations);save(v.status);}void save(const RuntimeComputerAiState& value) {
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

void save(const RuntimeTeamType& v){save(v.id);save(v.name);save(v.house);save(v.waypointId);save(v.scriptId);save(v.taskForceId);save(v.priority);save(v.max);save(v.group);save(v.autocreate);save(v.prebuild);save(v.reinforce);save(v.full);save(v.aggressive);save(v.isBaseDefense);save(v.recruitable);save(v.recruiter);save(v.looseRecruit);save(v.avoidThreats);save(v.suicide);save(v.guardSlower);save(v.onlyTargetHouseEnemy);save(v.transportsReturnOnUnload);save(v.veteranLevel);save(v.mindControlDecision);}

void save(const RuntimeTaskForceMember& v){save(v.count);save(v.unitId);}
void save(const RuntimeTaskForce& v){save(v.id);save(v.name);save(v.group);sequence(v.members);}
void save(const RuntimeScriptStep& v){save(v.action);save(v.argument);}
void save(const RuntimeScriptType& v){save(v.id);save(v.name);sequence(v.steps);}
void save(const RuntimeGrindingRelation& v){save(v.hostId);save(v.sourceId);save(static_cast<int>(v.phase));save(v.hostOwner);save(v.sourceOwner);save(v.hostType);save(v.sourceType);save(v.queuedTick);save(v.enteringTick);sequence(v.passengerIds);}
void save(const UnitType& v){save(v.harvester);save(v.resourceGatherer);save(v.airportBound);}
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

std::string snapshot(const Model& m){
    image.clear();save(m.tick);save(m.effectSerial);save(std::to_string(m.rng));save(m.nextMissionId);save(m.ai);
    save(m.detachedTactics);save(m.detachedTeam);save(m.tacticsOwned);save(m.teamOwned);mapping(m.owners);mapping(m.teams);mapping(m.forces);mapping(m.scripts);mapping(m.missions);save(m.desiredType);
    mapping(m.engineer);mapping(m.transport);mapping(m.occupy);sequence(m.grinding);mapping(m.units);mapping(m.ammo);save(encode(m.objects));sequence(m.disabled);sequence(m.nullMutable);sequence(m.nullConst);mapping(m.behaviors);mapping(m.objectCells);mapping(m.distances);save(m.center);save(m.afterAttackCell);save(m.attackResult);save(m.moveResult);save(m.mutateMission);save(m.attackMutates);sequence(m.actions);return image;
}
enum class Operation{Group,May,Recruit,Join};
struct Outcome{int value=-999;std::string error;bool operator==(const Outcome&)const=default;};
Outcome invoke(Model& m,Operation op,bool old){
    Root r(m);Queries q(r);Effects e(r);domain::RuntimeComputerTacticRecruitmentState state{m.tick,m.missions,m.scripts};active=&m;Outcome out;
    try{
        if(op==Operation::Group)out.value=old?original::runtimeComputerTacticGroup(r,m.desiredType):domain::runtimeComputerTacticGroup(q,m.desiredType);
        else if(op==Operation::May)out.value=old?original::runtimeComputerTacticMayRecruit(r,m.ai,target(m),m.desiredType,m.objects.front()):domain::runtimeComputerTacticMayRecruit(q,m.ai,target(m),m.desiredType,m.objects.front());
        else if(op==Operation::Recruit)out.value=old?original::recruitRuntimeComputerTacticMembers(r,m.ai,tactic(m),target(m),m.desiredType):domain::recruitRuntimeComputerTacticMembers(state,q,e,m.ai,tactic(m),target(m),m.desiredType);
        else out.value=old?original::joinRuntimeComputerTacticReinforcements(r,target(m),m.desiredType):domain::joinRuntimeComputerTacticReinforcements(state,q,e,target(m),m.desiredType);
    }catch(const std::runtime_error& error){out.error=error.what();}active=nullptr;return out;
}
void compare(const std::string& name,const Model& seed,Operation op,int expected,const std::function<void(const Model&)>& verify={}){
    Model probe=seed;probe.trace={};const auto normal=invoke(probe,op,true);require(normal.error.empty()&&normal.value==expected,name+" authored outcome "+normal.error+" got="+std::to_string(normal.value));if(verify)verify(probe);
    const int calls=static_cast<int>(probe.trace.calls.size());
    for(int fail=0;fail<=calls+1;++fail){Model a=seed,b=seed;a.trace={};b.trace={};a.trace.failAt=fail;b.trace.failAt=fail;const auto x=invoke(a,op,true),y=invoke(b,op,false);++models;if(fail>0&&fail<=calls)++faults;
        require(x==y,name+" outcome at "+std::to_string(fail));require(a.trace.calls==b.trace.calls,name+" dependency order at "+std::to_string(fail));require(snapshot(a)==snapshot(b),name+" complete owned state/partial mutation at "+std::to_string(fail));
        require((fail>0&&fail<=calls)?!x.error.empty():(x.error.empty()&&x.value==expected),name+" fault reachability at "+std::to_string(fail));
    }
}
Model seed(){Model m;m.ai.owner="Yuri";m.ai.initialized=true;m.ai.baseCenter={2,3};m.ai.credits=523;m.ai.productionSerial=29;m.ai.lastBuildType="sentinel";m.ai.status="borrow";
    m.desiredType.id="wanted";m.desiredType.group=-1;m.desiredType.priority=5;m.desiredType.taskForceId="force";m.desiredType.veteranLevel=3;
    RuntimeTaskForce force;force.id="force";force.group=-1;force.members={{2,"TANK"}};m.forces[force.id]=force;
    auto& t=m.owners["yuri"].teams[7];t.instanceId=7;t.teamTypeId="wanted";t.missingMembers={"TANK"};t.missionInstanceId=9;t.lastProgressTick=17;t.launched=true;
    m.owners["yuri"].nextTeamInstanceId=51;m.detachedTactics=m.owners["yuri"];m.detachedTeam=t;
    WorldObjectInstance o;o.instanceId=41;o.type="Tank";o.owner="YURI";o.alive=true;o.health=200;o.x=3;o.y=3;o.mission="Guard";m.objects.push_back(o);m.behaviors[41]=7;
    m.units["TANK"]=UnitType{};return m;}

void eligibilityCases(){
    const auto may=[](const std::string& name,const std::function<void(Model&)>& edit,int expected){auto m=seed();edit(m);compare(name,m,Operation::May,expected);};
    compare("default unassigned -1",seed(),Operation::May,1);
    for(int g:{-2,-1,0,1,7})for(bool recruiter:{false,true})may("unassigned group "+std::to_string(g)+" recruiter "+std::to_string(recruiter),[=](auto& m){m.desiredType.group=g;m.desiredType.recruiter=recruiter;},recruiter||g==-1||g==-2);
    may("dead short circuit",[](auto& m){m.objects[0].alive=false;m.objects[0].health=0;},0);
    for(int hp:{0,-1})may("health "+std::to_string(hp),[=](auto& m){m.objects[0].health=hp;},0);
    may("wrong owner",[](auto& m){m.objects[0].owner="other";},0);
    may("owner no whitespace normalization",[](auto& m){m.objects[0].owner=" YURI ";},0);
    may("factory exit active",[](auto& m){m.objects[0].factoryExitHostId=0;},0);
    may("garrison active",[](auto& m){m.objects[0].garrisonHostObjectId=0;},0);
    may("bunker active",[](auto& m){m.objects[0].tankBunkerHostId=0;},0);
    may("parasite",[](auto& m){m.disabled.insert(41);},0);
    for(const std::string mission:{"guard","AreaGuard","aReA gUaRd","Move","Attack","SecondaryFire","Dock","Stop","Patrol","Hunt","FactoryExit","Harvest","Unload"," guard","guard ",""}){
        const auto lower=toLowerAsciiLocal(mission);const bool allowed=lower=="guard"||lower=="areaguard"||lower=="area guard"||lower=="move"||lower=="attack"||lower=="secondaryfire"||lower=="dock"||lower=="stop"||lower=="patrol"||lower=="hunt";
        may("mission spelling ["+mission+"]",[&](auto& m){m.objects[0].mission=mission;},allowed);
    }
    for(int mask=1;mask<8;++mask)may("pending short circuit "+std::to_string(mask),[=](auto& m){if(mask&1)m.engineer[41]=1;if(mask&2)m.transport[41]=1;if(mask&4)m.occupy[41]=1;},0);
    may("grinding root denies",[](auto& m){RuntimeGrindingRelation r;r.sourceId=41;m.grinding.push_back(r);},0);
    may("grinding passenger not root",[](auto& m){RuntimeGrindingRelation r;r.sourceId=92;r.hostId=41;r.passengerIds={41};m.grinding.push_back(r);},1);
    may("missing unit rule preserves original",[](auto& m){m.units.clear();m.objects[0].airborne=true;},1);
    may("harvester",[](auto& m){m.units.at("TANK").harvester=true;},0);
    may("resource gatherer",[](auto& m){m.units.at("TANK").resourceGatherer=true;},0);
    for(bool airborne:{false,true})for(bool ammo:{false,true})may("airport ammo "+std::to_string(airborne)+std::to_string(ammo),[=](auto& m){m.units.at("TANK").airportBound=true;m.objects[0].airborne=airborne;m.ammo[41]=ammo;},!airborne&&ammo);
    const auto peer=[](Model& m){RuntimeTeamType old;old.id="old";old.group=-1;old.priority=4;old.recruitable=true;m.teams[old.id]=old;auto& p=m.owners["yuri"].teams[2];p.instanceId=2;p.teamTypeId=old.id;p.objectIds={41};};
    may("lower priority peer allowed",[&](auto& m){peer(m);},1);
    for(int priority:{4,5,6})may("peer strict priority "+std::to_string(priority),[&](auto& m){peer(m);m.teams.at("old").priority=priority;},priority<5);
    may("unrecruitable peer",[&](auto& m){peer(m);m.teams.at("old").recruitable=false;},0);
    may("peer missing authored type",[&](auto& m){peer(m);m.teams.clear();},0);
    may("same teamtype rejects despite detached address",[&](auto& m){peer(m);m.owners.at("yuri").teams.at(2).teamTypeId="wanted";m.teamOwned=false;},0);
    may("same node address rejects despite different type id",[](auto& m){target(m).objectIds={41};target(m).teamTypeId="other";},0);
    may("detached node with different type eligible",[](auto& m){auto& t=target(m);t.objectIds={41};t.teamTypeId="old";m.detachedTeam=t;m.detachedTeam.teamTypeId="wanted";m.teamOwned=false;RuntimeTeamType old;old.id="old";old.priority=0;old.recruitable=true;m.teams["old"]=old;},1);
    for(int group:{-2,-1,4,5})for(bool recruiter:{false,true})may("peer group "+std::to_string(group)+" recruit "+std::to_string(recruiter),[&](auto& m){peer(m);m.desiredType.group=group;m.desiredType.recruiter=recruiter;m.teams.at("old").group=4;},recruiter||group==-2||group==4);
    may("ordered second owner veto",[&](auto& m){peer(m);auto p=m.owners.at("yuri").teams.at(2);p.teamTypeId="missing";m.owners["zz"].teams[1]=p;},0);
    may("ordered first owner veto",[&](auto& m){peer(m);auto p=m.owners.at("yuri").teams.at(2);p.teamTypeId="missing";m.owners["aa"].teams[1]=p;},0);
    may("finished peer still reserves identity",[&](auto& m){peer(m);auto& p=m.owners.at("yuri").teams.at(2);p.missionFinished=true;p.outcomeReported=true;m.teams.at("old").recruitable=false;},0);
}
void groupCases(){for(int group:{-2,-1,0,4})for(bool present:{false,true}){auto m=seed();m.desiredType.group=group;m.forces.at("force").group=11;if(!present)m.forces.clear();compare("group lookup always "+std::to_string(group)+std::to_string(present),m,Operation::Group,group==-1&&present?11:group);}}
void recruitmentCases(){
    compare("single recruitment",seed(),Operation::Recruit,1,[](const auto& m){const auto& t=m.owners.at("yuri").teams.at(7);require(t.objectIds==std::vector<int>{41}&&t.joiningObjectIds==std::vector<int>{41}&&t.missingMembers.empty()&&t.lastProgressTick==91,"single vectors and live tick");require(m.actions==std::vector<std::string>({"detach:41","erase:41"})&&m.objects[0].mission=="Guard"&&!m.behaviors.contains(41),"detach erase mission order");});
    auto m=seed();target(m).missingMembers.clear();compare("empty missing",m,Operation::Recruit,0);
    m=seed();m.objects.clear();compare("empty world",m,Operation::Recruit,0);
    m=seed();target(m).launched=false;compare("not launched no joining publication",m,Operation::Recruit,1,[](const auto& v){require(v.owners.at("yuri").teams.at(7).joiningObjectIds.empty(),"no joining before launch");});
    m=seed();m.nullMutable.insert(41);compare("member reference absent after transfer",m,Operation::Recruit,1,[](const auto& v){require(v.objects[0].mission=="Guard"&&v.owners.at("yuri").teams.at(7).missingMembers.empty(),"absence still consumes missing");});
    for(int remaining:{-2,0,10}){m=seed();RuntimeComputerTacticProduction p;p.teamInstanceId=7;p.typeId="tank";p.remainingTicks=remaining;p.paidCost=83;p.totalTicks=19;tactic(m).production[0]=p;compare("production reserves regardless ticks "+std::to_string(remaining),m,Operation::Recruit,0);}
    m=seed();RuntimeComputerTacticProduction p;p.teamInstanceId=8;p.typeId="TANK";tactic(m).production={p};compare("other team production no reserve",m,Operation::Recruit,1);
    m=seed();p.teamInstanceId=7;p.typeId="";tactic(m).production={p};compare("empty production type no reserve",m,Operation::Recruit,1);
    m=seed();p.typeId=" TANK ";tactic(m).production={p};compare("production whitespace preserved",m,Operation::Recruit,1);
    m=seed();target(m).missingMembers={"tank","Tank"};p.typeId="TANK";tactic(m).production={p};compare("one production credit consumed in authored order",m,Operation::Recruit,1,[](const auto& v){require(v.owners.at("yuri").teams.at(7).missingMembers==std::vector<std::string>{"tank"},"first missing retained for production");});
    m=seed();target(m).missingMembers={" TANK ","Tank","NONE"};compare("missing no trim and stable erase",m,Operation::Recruit,1,[](const auto& v){require(v.owners.at("yuri").teams.at(7).missingMembers==std::vector<std::string>({" TANK ","NONE"}),"unmatched order retained");});
    for(bool reverse:{false,true}){m=seed();auto o=m.objects[0];o.instanceId=31;o.x=1;o.y=3;m.objects.push_back(o);if(reverse)std::reverse(m.objects.begin(),m.objects.end());compare("exact distance tie lowest id "+std::to_string(reverse),m,Operation::Recruit,1,[](const auto& v){require(v.owners.at("yuri").teams.at(7).objectIds==std::vector<int>{31},"lower id tie independent of vector order");});}
    m=seed();m.objects[0].instanceId=-4;compare("negative best id sentinel skips",m,Operation::Recruit,0);
    m=seed();m.distances[pointKey(m.ai.baseCenter,{3,3})]=std::numeric_limits<double>::quiet_NaN();compare("NaN never beats best",m,Operation::Recruit,0);
    m=seed();m.distances[pointKey(m.ai.baseCenter,{3,3})]=std::numeric_limits<double>::max();compare("max tie positive id never beats -1",m,Operation::Recruit,0);
    m=seed();auto o=m.objects[0];o.instanceId=42;o.x=10;m.objects.push_back(o);target(m).missingMembers={"TANK","TANK"};compare("ordered repeated missing recruits distinct owned members",m,Operation::Recruit,1,[](const auto& v){require(v.owners.at("yuri").teams.at(7).objectIds==std::vector<int>({41,42}),"distinct owned selection");});
    m=seed();m.teamOwned=false;m.detachedTeam.missingMembers={"TANK","TANK"};compare("detached team preserves original repeated candidate alias behavior",m,Operation::Recruit,1,[](const auto& v){require(v.detachedTeam.objectIds==std::vector<int>({41,41}),"detached identity not replaced with instance-id lookup");});
    m=seed();m.tacticsOwned=false;RuntimeComputerTacticProduction p2;p2.teamInstanceId=7;p2.typeId="TANK";m.detachedTactics.production={p2};compare("detached production borrows original passed owner",m,Operation::Recruit,0);
    m=seed();RuntimeTeamType old;old.id="old";old.priority=1;old.recruitable=true;m.teams[old.id]=old;auto& peer=m.owners.at("yuri").teams[1];peer.teamTypeId="old";peer.objectIds={41,90};peer.joiningObjectIds={41,91};compare("transfer original peer detach",m,Operation::Recruit,1,[](const auto& v){const auto& p3=v.owners.at("yuri").teams.at(1);require(p3.objectIds==std::vector<int>{90}&&p3.joiningObjectIds==std::vector<int>{91},"peer membership and pending joins both detached");});
}
Model joining(){auto m=seed();target(m).missingMembers.clear();target(m).objectIds={41,3,41,9};target(m).joiningObjectIds={41};return m;}
void joiningCases(){
    compare("join no mission follows companions",joining(),Operation::Join,1,[](const auto& v){require(v.objects[0].veterancy==2&&v.actions==std::vector<std::string>{"move:8,9:41"}&&v.owners.at("yuri").teams.at(7).joiningObjectIds.empty(),"join original veteran center fallback");});
    auto m=joining();m.objects.clear();compare("missing member erase returns false",m,Operation::Join,0,[](const auto& v){require(v.owners.at("yuri").teams.at(7).joiningObjectIds.empty(),"missing-only erase without changed");});
    m=joining();m.nullMutable.insert(41);compare("null mutable erase returns false",m,Operation::Join,0);
    m=joining();m.objects[0].factoryExitHostId=0;compare("factory host waits untouched",m,Operation::Join,0);
    for(const std::string mission:{"FactoryExit","factoryexit","FactoryExit "}){m=joining();m.objects[0].mission=mission;compare("factory exact case ["+mission+"]",m,Operation::Join,mission!="FactoryExit");}
    m=joining();m.objects[0].veterancy=7;compare("veteran never demoted",m,Operation::Join,1,[](const auto& v){require(v.objects[0].veterancy==7,"existing veteran retained");});
    m=joining();m.desiredType.veteranLevel=0;compare("nonpositive veteran level",m,Operation::Join,1,[](const auto& v){require(v.objects[0].veterancy==0,"max old veteran with -1");});
    m=joining();m.center={-1,7};compare("no usable center still consumes join",m,Operation::Join,1,[](const auto& v){require(v.actions.empty(),"negative x skips move");});
    m=joining();m.center={2,-7};m.moveResult=0;compare("only x checked move result ignored",m,Operation::Join,1,[](const auto& v){require(v.actions==std::vector<std::string>{"move:2,-7:41"},"negative y sent to original owner");});
    for(int action:{-1,0,4,46})for(bool issued:{false,true})for(int result:{-1,0,2}){
        m=joining();auto enemy=m.objects[0];enemy.instanceId=99;enemy.owner="enemy";m.objects.push_back(enemy);RuntimeTeamMissionState mission;mission.instanceId=9;mission.scriptId="script";mission.objectIds={3};mission.stepIssued=issued;mission.targetObjectId=99;mission.targetCell={14,-5};m.missions[9]=mission;RuntimeScriptType script;script.id="script";script.steps={{action,3}};m.scripts[script.id]=script;m.attackResult=result;
        const bool attacking=issued&&(action==0||action==46),ordered=attacking&&result>0;
        compare("join action/issued/result "+std::to_string(action)+std::to_string(issued)+std::to_string(result),m,Operation::Join,1,[=](const auto& v){require(v.missions.at(9).objectIds==std::vector<int>({3,41}),"mission append once");std::vector<std::string> expected;if(attacking)expected.push_back("attack:99:41");if(!ordered)expected.push_back("move:14,-5:41");require(v.actions==expected,"attack positive versus fallback exact");});
    }
    const auto missionSeed=[](){auto v=joining();auto enemy=v.objects[0];enemy.instanceId=99;v.objects.push_back(enemy);RuntimeTeamMissionState ms;ms.scriptId="script";ms.stepIssued=true;ms.targetObjectId=99;ms.targetCell={14,15};ms.objectIds={41};v.missions[9]=ms;RuntimeScriptType sc;sc.id="script";sc.steps={{0,0}};v.scripts[sc.id]=sc;return v;};
    m=missionSeed();m.attackResult=0;m.attackMutates=true;compare("attack side effect live mission fallback read",m,Operation::Join,1,[](const auto& v){require(v.actions==std::vector<std::string>({"attack:99:41","move:21,-3:41"}),"must not snapshot target cell before attack");require(v.missions.at(9).objectIds==std::vector<int>{41},"member never duplicated");});
    m=missionSeed();m.attackResult=1;m.attackMutates=true;compare("attack positive retains live mutation without move",m,Operation::Join,1);
    m=missionSeed();m.nullConst.insert(99);compare("null target preserves fallback",m,Operation::Join,1);
    m=missionSeed();m.scripts.clear();compare("missing script fallback",m,Operation::Join,1);
    m=missionSeed();m.missions.at(9).stepIndex=std::numeric_limits<std::size_t>::max();compare("size_t step index no narrowing",m,Operation::Join,1);
    m=missionSeed();m.missions.at(9).targetCell={-1,0};m.nullConst.insert(99);compare("negative mission target leaves original center",m,Operation::Join,1);
    m=joining();target(m).joiningObjectIds={999,41,998};compare("missing real missing stable loop",m,Operation::Join,1,[](const auto& v){require(v.owners.at("yuri").teams.at(7).joiningObjectIds.empty()&&v.actions.size()==1,"erase once per joining entry");});
    m=joining();target(m).joiningObjectIds={41,41};compare("original duplicate joining processed twice",m,Operation::Join,1,[](const auto& v){require(v.actions.size()==2,"no introduced dedup");});
}

void liveBorrowCases(){auto m=seed();Root r(m);Queries q(r);Effects e(r);domain::RuntimeComputerTacticRecruitmentState state{m.tick,m.missions,m.scripts};active=&m;
    require(&q.objects()==&m.objects&&&q.ownerTactics()==&m.owners,"live original world and owner maps borrowed");
    require(q.taskForce("force")==&m.forces.at("force"),"task force stable original node");m.forces.at("force").group=27;require(domain::runtimeComputerTacticGroup(q,m.desiredType)==27,"same query sees in-place task force change");
    m.forces.at("force").group=-1;m.engineer[41]=9;require(q.pendingEngineerCapture(41),"same query sees new engineer relation");m.engineer.clear();require(!q.pendingEngineerCapture(41),"same query sees removed engineer relation");
    m.tick=127;require(domain::recruitRuntimeComputerTacticMembers(state,q,e,m.ai,tactic(m),target(m),m.desiredType),"precreated state recruits after live tick mutation");require(target(m).lastProgressTick==127,"borrowed tick not a snapshot");
    auto enemy=m.objects[0];enemy.instanceId=99;m.objects.push_back(enemy);RuntimeTeamMissionState mission;mission.scriptId="late";mission.stepIssued=true;mission.targetObjectId=99;mission.targetCell={1,2};m.missions[9]=mission;RuntimeScriptType script;script.id="late";script.steps={{46,0}};m.scripts[script.id]=script;
    m.attackResult=0;m.attackMutates=true;m.afterAttackCell={23,-9};m.actions.clear();require(domain::joinRuntimeComputerTacticReinforcements(state,q,e,target(m),m.desiredType),"precreated state sees later mission and script nodes");require(m.actions==std::vector<std::string>({"attack:99:41","move:23,-9:41"}),"same borrowed mission observes downstream attack mutation");
    active=nullptr;
}
} // namespace
int main(){try{groupCases();eligibilityCases();recruitmentCases();joiningCases();liveBorrowCases();std::cout<<"recruitment checks="<<checks<<" model-states="<<models<<" injected-faults="<<faults<<" full-owned-state=exact ordered-dependencies=exact failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<"recruitment contract: "<<e.what()<<'\n';return 1;}}
