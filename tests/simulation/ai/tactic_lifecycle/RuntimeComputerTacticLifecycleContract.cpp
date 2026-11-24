#include "simulation/ai/RuntimeComputerTacticLifecycle.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "data/rules/RulesDatabase.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <bit>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
// Dependency effects are models; original full World/routes run separately.
namespace fixture {
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_authority_hash::runtimeStableHashString;
namespace policy=concord::simulation::ai;
struct Model;
thread_local Model* active=nullptr;
struct Rules { ComputerTacticsRules tactics; const ComputerTacticsRules& getComputerTacticsRules() const; };
struct Model {
 int tick=100; Rules rules; RuntimeTriggerState triggers; RuntimeComputerAiState ai;
 std::map<int,WorldObjectInstance> objects; std::set<int> pending;
 std::map<std::string,RuntimeBuildCatalogItem> catalog;
 RuntimeComputerAiMetrics metrics;
 bool productionChanged=false,joinChanged=false,recruitChanged=false,selectChanged=false;
 bool megawealth=false,needsEngineer=false; std::optional<RuntimeBuildCatalogItem> harvester,engineer;
 bool recruitAvailable=false,newSelection=false; int tickAfterProduction=-1;
 std::vector<std::string> trace; int throwAt=-1;
 void visit(std::string text) {trace.push_back(std::move(text));if(static_cast<int>(trace.size())==throwAt)throw std::runtime_error("dependency fault");}
};
const ComputerTacticsRules& Rules::getComputerTacticsRules() const {active->visit("rules");return tactics;}
std::string canonical(const std::string& s){return toLowerAsciiLocal(trimAsciiLocal(s));}
const WorldObjectInstance* findRuntimeObjectByIdIncludingInactive(const Model& s,int id) {active->visit("inactive:"+std::to_string(id));auto i=s.objects.find(id);return i==s.objects.end()?nullptr:&i->second;}
bool runtimeComputerAiOwnerMatches(const WorldObjectInstance& o,const std::string& owner){active->visit("owner:"+std::to_string(o.instanceId)+":"+owner);return canonical(o.owner)==canonical(owner);}
bool runtimeObjectIsPendingTeamRejoinAfterLimbo(const Model& s,int id){active->visit("limbo:"+std::to_string(id));return s.pending.contains(id);}
const RuntimeTeamType* findRuntimeTriggerTeamById(const Model& s,const std::string& id){active->visit("team:"+id);auto i=s.triggers.teams.find(id);return i==s.triggers.teams.end()?nullptr:&i->second;}
const WorldObjectInstance* findObjectById(const Model& s,int id){active->visit("active:"+std::to_string(id));auto i=s.objects.find(id);return i==s.objects.end()||!i->second.alive?nullptr:&i->second;}
WorldObjectInstance* findMutableObjectById(Model& s,int id){active->visit("mutable:"+std::to_string(id));auto i=s.objects.find(id);return i==s.objects.end()||!i->second.alive?nullptr:&i->second;}
int runtimeLegacyScriptTicks(const Model&,int n){active->visit("legacy:"+std::to_string(n));return std::max(1,n*2);}
int runtimeComputerMiningDifficultyIndex(const std::string& d){active->visit("difficulty:"+d);auto v=toUpperAsciiLocal(trimAsciiLocal(d));return v=="EASY"?2:(v=="HARD"||v=="BRUTAL")?0:1;}
RuntimeComputerAiMetrics runtimeComputerAiMetrics(const Model& s,const RuntimeComputerAiState& ai){active->visit("metrics:"+ai.owner);return s.metrics;}
std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectNeededHarvester(const Model& s,const RuntimeComputerAiState& ai,const RuntimeComputerAiMetrics& m){active->visit("harvester:"+ai.owner+":"+std::to_string(m.harvesters));return s.harvester;}
bool runtimeIsMegawealthMode(const Model& s){active->visit("megawealth");return s.megawealth;}
bool runtimeComputerAiNeedsOilEngineer(const Model& s,const RuntimeComputerAiState& ai){active->visit("need-engineer:"+ai.owner);return s.needsEngineer;}
enum class RuntimeComputerAiUnitRole {Engineer};
std::optional<RuntimeBuildCatalogItem> runtimeComputerAiSelectUnitForRole(const Model& s,const RuntimeComputerAiState& ai,RuntimeComputerAiUnitRole){active->visit("engineer:"+ai.owner);return s.engineer;}
std::optional<RuntimeBuildCatalogItem> runtimeComputerAiBuildItem(const Model& s,const std::string& id,const std::string& owner){active->visit("build:"+id+":"+owner);auto i=s.catalog.find(id);return i==s.catalog.end()?std::nullopt:std::optional<RuntimeBuildCatalogItem>{i->second};}
bool advanceRuntimeComputerTacticProduction(Model& s,RuntimeComputerAiState& ai,RuntimeComputerTacticsState& t){active->visit("advance-production");if(s.tickAfterProduction>=0)s.tick=s.tickAfterProduction;if(s.productionChanged){++ai.unitsBuilt;--t.production[0].remainingTicks;}return s.productionChanged;}
bool joinRuntimeComputerTacticReinforcements(Model& s,RuntimeComputerTacticTeam& t,const RuntimeTeamType& type){active->visit("join:"+std::to_string(t.instanceId)+":"+type.id);if(s.joinChanged){t.joiningObjectIds.clear();++t.lastProgressTick;}return s.joinChanged;}
bool recruitRuntimeComputerTacticMembers(Model& s,RuntimeComputerAiState&,RuntimeComputerTacticsState&,RuntimeComputerTacticTeam& t,const RuntimeTeamType& type){active->visit("recruit:"+std::to_string(t.instanceId)+":"+type.id);if(s.recruitAvailable&&!t.missingMembers.empty()){auto u=std::find_if(s.objects.begin(),s.objects.end(),[&](const auto& row){return toUpperAsciiLocal(row.second.type)==toUpperAsciiLocal(t.missingMembers.front())&&std::find(t.objectIds.begin(),t.objectIds.end(),row.first)==t.objectIds.end();});if(u!=s.objects.end()){t.objectIds.push_back(u->first);t.missingMembers.erase(t.missingMembers.begin());t.lastProgressTick=s.tick;return true;}}return s.recruitChanged;}
void issueRuntimeTriggerTeamOrder(Model& s,const RuntimeTeamType& type,const std::vector<int>& ids,const std::string& trigger,int build){active->visit("issue:"+type.id+":"+type.house+":"+trigger+":"+std::to_string(build));RuntimeTeamMissionState m;m.instanceId=s.triggers.nextTeamMissionInstanceId++;m.teamTypeId=type.id;m.scriptId=type.scriptId;m.objectIds=ids;m.sourceAiTriggerId=trigger;m.sourceAiBuildId=build;m.status="issued";s.triggers.teamMissions[m.instanceId]=m;}
bool selectRuntimeComputerTactic(Model& s,RuntimeComputerAiState&,RuntimeComputerTacticsState& t){active->visit("select");if(s.newSelection){RuntimeComputerTacticTeam team;team.instanceId=t.nextTeamInstanceId++;team.teamTypeId="type";team.triggerId="trigger";team.createdTick=s.tick;team.lastProgressTick=s.tick;team.missingMembers={"INF"};t.teams[team.instanceId]=team;return true;}return s.selectChanged;}
bool queueRuntimeComputerTacticProduction(Model& s,RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& item,int team=-1){active->visit("queue:"+item.typeId+":"+std::to_string(team));auto lane=item.kind==RuntimeBuildKind::Infantry?0:item.typeId=="AIR"?2:1;auto& order=s.triggers.computerTacticsByOwner[canonical(ai.owner)].production[lane];if(order.remainingTicks>0||ai.credits<item.cost)return false;order={team,item.typeId,item.buildTicks,item.cost,item.buildTicks};ai.credits-=item.cost;ai.creditsSpent+=item.cost;return true;}
namespace original {using RuntimeGameSessionState=Model;using RuntimeProductionHostState=Model;
#include "RuntimeComputerTacticLifecycleOriginal.inl"
}
class Queries final:public policy::RuntimeComputerTacticLifecycleQueries {
 const Model& s;
public:explicit Queries(const Model& v):s(v){}
 const ComputerTacticsRules& rules()const override{return s.rules.getComputerTacticsRules();}
 const RuntimeTeamType* teamType(const std::string& id)const override{return findRuntimeTriggerTeamById(s,id);}
 const WorldObjectInstance* objectIncludingInactive(int id)const override{return findRuntimeObjectByIdIncludingInactive(s,id);}
 const WorldObjectInstance* object(int id)const override{return findObjectById(s,id);}
 bool ownerMatches(const WorldObjectInstance& o,const std::string& n)const override{return runtimeComputerAiOwnerMatches(o,n);}
 bool pendingTeamRejoinAfterLimbo(int id)const override{return runtimeObjectIsPendingTeamRejoinAfterLimbo(s,id);}
 int legacyScriptTicks(int n)const override{return runtimeLegacyScriptTicks(s,n);}
 int difficultyIndex(const std::string& d)const override{return runtimeComputerMiningDifficultyIndex(d);}
 RuntimeComputerAiMetrics metrics(const RuntimeComputerAiState& ai)const override{return runtimeComputerAiMetrics(s,ai);}
 std::optional<RuntimeBuildCatalogItem> neededHarvester(const RuntimeComputerAiState& ai,const RuntimeComputerAiMetrics& m)const override{return runtimeComputerAiSelectNeededHarvester(s,ai,m);}
 bool isMegawealthMode()const override{return runtimeIsMegawealthMode(s);}
 bool needsOilEngineer(const RuntimeComputerAiState& ai)const override{return runtimeComputerAiNeedsOilEngineer(s,ai);}
 std::optional<RuntimeBuildCatalogItem> selectEngineer(const RuntimeComputerAiState& ai)const override{return runtimeComputerAiSelectUnitForRole(s,ai,RuntimeComputerAiUnitRole::Engineer);}
 std::optional<RuntimeBuildCatalogItem> buildItem(const std::string& id,const std::string& owner)const override{return runtimeComputerAiBuildItem(s,id,owner);}
};
class Effects final:public policy::RuntimeComputerTacticLifecycleEffects {
 Model& s;
public:explicit Effects(Model& v):s(v){}
 bool advanceProduction(RuntimeComputerAiState& ai,RuntimeComputerTacticsState& t)override{return advanceRuntimeComputerTacticProduction(s,ai,t);}
 bool joinReinforcements(RuntimeComputerTacticTeam& t,const RuntimeTeamType& type)override{return joinRuntimeComputerTacticReinforcements(s,t,type);}
 bool recruitMembers(RuntimeComputerAiState& ai,RuntimeComputerTacticsState& ts,RuntimeComputerTacticTeam& t,const RuntimeTeamType& type)override{return recruitRuntimeComputerTacticMembers(s,ai,ts,t,type);}
 WorldObjectInstance* mutableObject(int id)override{return findMutableObjectById(s,id);}
 void issueTeamOrder(const RuntimeTeamType& type,const std::vector<int>& ids,const std::string& trigger,int team)override{issueRuntimeTriggerTeamOrder(s,type,ids,trigger,team);}
 bool select(RuntimeComputerAiState& ai,RuntimeComputerTacticsState& t)override{return selectRuntimeComputerTactic(s,ai,t);}
 bool queueProduction(RuntimeComputerAiState& ai,const RuntimeBuildCatalogItem& item,int team)override{return queueRuntimeComputerTacticProduction(s,ai,item,team);}
};

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

template<class T> void fill(T& v,unsigned seed) {
    if constexpr(std::is_same_v<T,bool>)v=(seed&1u)!=0;
    else if constexpr(std::is_enum_v<T>)v=static_cast<T>(seed%3u);
    else if constexpr(std::is_integral_v<T>)v=static_cast<T>(static_cast<int>(seed%101u)-50);
    else if constexpr(std::is_floating_point_v<T>)v=static_cast<T>(static_cast<int>(seed%97u)-48)/8.0;
    else if constexpr(std::is_same_v<T,std::string>)v="sentinel-"+std::to_string(seed)+"; // |";
    else if constexpr(requires {v.emplace();v.has_value();}) {if(seed%3u){v.emplace();fill(*v,seed+1u);}else v.reset();}
    else if constexpr (requires { v.kind;v.moveTarget; }) {fill(v.kind,seed+1u); fill(v.state,seed+2u); fill(v.moveTarget,seed+3u); fill(v.targetObjectId,seed+4u); fill(v.autoAcquired,seed+5u); fill(v.forceFire,seed+6u); fill(v.attackGround,seed+7u); fill(v.attackBridge,seed+8u); fill(v.attackBridgeCell,seed+9u); fill(v.path,seed+10u); fill(v.pathRequestPending,seed+11u); fill(v.pendingPathTowardRange,seed+12u); fill(v.pendingPathStart,seed+13u); fill(v.pendingPathGoal,seed+14u); fill(v.pendingHasSharedGoal,seed+15u); fill(v.pendingSharedGoal,seed+16u); fill(v.pendingAllowedOccupiedObjectId,seed+17u); fill(v.pendingTargetObjectId,seed+18u); fill(v.pendingRangeCells,seed+19u); fill(v.pendingAllowNearbyTarget,seed+20u); fill(v.pendingIgnoredObjectIds,seed+21u); fill(v.repackMoveGroup,seed+22u); fill(v.moveCooldownTicks,seed+23u); fill(v.moveSegmentFrom,seed+24u); fill(v.moveIntervalRemainderQ16,seed+25u); fill(v.moveStartupDistanceQ16,seed+26u); fill(v.renderStartupAccelerationDistance,seed+27u); fill(v.factoryExitClearanceHostId,seed+28u); fill(v.transportTransferHostId,seed+29u); fill(v.transportTransferKind,seed+30u); fill(v.weaponCooldownTicks,seed+31u); fill(v.blockedTicks,seed+32u); fill(v.pathReplans,seed+33u); fill(v.nextRepathTick,seed+34u); fill(v.returnToDeployed,seed+35u); fill(v.approachSlotIndex,seed+36u); fill(v.renderFrom,seed+37u); fill(v.renderTo,seed+38u); fill(v.renderFromOffsetX,seed+39u); fill(v.renderFromOffsetY,seed+40u); fill(v.renderToOffsetX,seed+41u); fill(v.renderToOffsetY,seed+42u); fill(v.renderFromLevel,seed+43u); fill(v.renderToLevel,seed+44u); fill(v.renderFromOnBridge,seed+45u); fill(v.renderToOnBridge,seed+46u); fill(v.renderMoveTicksTotal,seed+47u); fill(v.renderMoveTicksRemaining,seed+48u); fill(v.renderArrivalDistanceAfter,seed+49u);}
    else if constexpr (requires { v.instanceId;v.mapId;v.renderX; }) {fill(v.instanceId,seed+1u); fill(v.kind,seed+2u); fill(v.mapId,seed+3u); fill(v.owner,seed+4u); fill(v.type,seed+5u); fill(v.health,seed+6u); fill(v.x,seed+7u); fill(v.y,seed+8u); fill(v.cell,seed+9u); fill(v.level,seed+10u); fill(v.facing,seed+11u); fill(v.turretFacing,seed+12u); fill(v.bodyFacingClock,seed+13u); fill(v.turretFacingClock,seed+14u); fill(v.subCell,seed+15u); fill(v.mission,seed+16u); fill(v.tag,seed+17u); fill(v.veterancy,seed+18u); fill(v.veterancyExperience,seed+19u); fill(v.crateArmorMultiplier,seed+20u); fill(v.crateFirepowerMultiplier,seed+21u); fill(v.crateSpeedMultiplier,seed+22u); fill(v.crateCloakable,seed+23u); fill(v.harvestAutoSearchSuspended,seed+24u); fill(v.hasBeenCaptured,seed+25u); fill(v.onBridge,seed+26u); fill(v.authoredAircraft,seed+27u); fill(v.alive,seed+28u); fill(v.rubble,seed+29u); fill(v.submerged,seed+30u); fill(v.cloaked,seed+31u); fill(v.cloakCooldownTicks,seed+32u); fill(v.submergeCooldownTicks,seed+33u); fill(v.deployed,seed+34u); fill(v.deployStateTicksRemaining,seed+35u); fill(v.prone,seed+36u); fill(v.proneTicksRemaining,seed+37u); fill(v.weaponCooldownTicksRemaining,seed+38u); fill(v.spriteFiringStartTick,seed+39u); fill(v.radiationEruptionCooldownTicksRemaining,seed+40u); fill(v.hasRenderPosition,seed+41u); fill(v.renderX,seed+42u); fill(v.renderY,seed+43u); fill(v.renderMoveActive,seed+44u); fill(v.renderLevel,seed+45u); fill(v.renderOnBridge,seed+46u); fill(v.movementAnimationDistance,seed+47u); fill(v.buildAnimTicksTotal,seed+48u); fill(v.buildAnimTicksRemaining,seed+49u); fill(v.structureRecoilStartTick,seed+50u); fill(v.chargeTurretStartTick,seed+51u); fill(v.workAnimationActive,seed+52u); fill(v.poweredOperational,seed+53u); fill(v.powerAnimationPausedAtTick,seed+54u); fill(v.powerAnimationPausedTicks,seed+55u); fill(v.factoryExitAnimationStartTick,seed+56u); fill(v.factoryExitAnimationEndTick,seed+57u); fill(v.factoryExitAirborne,seed+58u); fill(v.factoryExitHostId,seed+59u); fill(v.specialCashNextTick,seed+60u); fill(v.specialCloneDeliveryObjectId,seed+61u); fill(v.transportPassengerIds,seed+62u); fill(v.bioReactorAnimationStartTick,seed+63u); fill(v.bioReactorAnimationPoweredStartTick,seed+64u); fill(v.activeGunnerMode,seed+65u); fill(v.activeGunnerWeaponIndex,seed+66u); fill(v.activeGunnerTurretIndex,seed+67u); fill(v.tankBunkerHostId,seed+68u); fill(v.tankBunkerOccupantId,seed+69u); fill(v.tankBunkerAnimationStartTick,seed+70u); fill(v.tankBunkerLowering,seed+71u); fill(v.dockedToObjectId,seed+72u); fill(v.dockIndex,seed+73u); fill(v.airborne,seed+74u); fill(v.flightLevel,seed+75u); fill(v.aircraftDockFlightProgress,seed+76u); fill(v.aircraftBuildingClearanceLevel,seed+77u); fill(v.wallConnectionMask,seed+78u); fill(v.garrisonHostObjectId,seed+79u); fill(v.garrisonOccupantIds,seed+80u); fill(v.garrisonFireCursor,seed+81u); fill(v.currentAmmo,seed+82u); fill(v.pendingAircraftAuxSounds,seed+83u); fill(v.turretSoundActivityTick,seed+84u); fill(v.activeTurretSoundEvent,seed+85u); fill(v.pendingTurretSounds,seed+86u); fill(v.modExtensions,seed+87u);}
    else if constexpr (requires { v.elapsedNumerator;v.lastFacing; }) {fill(v.initial,seed+1u); fill(v.target,seed+2u); fill(v.rate,seed+3u); fill(v.elapsedNumerator,seed+4u); fill(v.lastTick,seed+5u); fill(v.lastFacing,seed+6u);}
    else if constexpr (requires { v.technoType;v.shieldType;v.effects; }) {fill(v.initialized,seed+1u); fill(v.technoType,seed+2u); fill(v.shieldType,seed+3u); fill(v.shieldCapacity,seed+4u); fill(v.shieldHealth,seed+5u); fill(v.shieldTimer,seed+6u); fill(v.shieldOnline,seed+7u); fill(v.innateDelayTicks,seed+8u); fill(v.effects,seed+9u);}
    else if constexpr (requires { v.sourceType;v.fromWarhead; }) {fill(v.sourceType,seed+1u); fill(v.fromWarhead,seed+2u); fill(v.remainingTicks,seed+3u); fill(v.elapsedTicks,seed+4u);}
    else if constexpr (requires { v.event;v.takingOff; }) {fill(v.event,seed+1u); fill(v.tick,seed+2u); fill(v.x,seed+3u); fill(v.y,seed+4u); fill(v.level,seed+5u); fill(v.takingOff,seed+6u);}
    else if constexpr (requires { v.event;v.rotating; }) {fill(v.event,seed+1u); fill(v.tick,seed+2u); fill(v.x,seed+3u); fill(v.y,seed+4u); fill(v.level,seed+5u); fill(v.rotating,seed+6u);}
    else if constexpr (requires { v.center;v.members; }) {fill(v.center,seed+1u); fill(v.members,seed+2u);}
    else if constexpr (requires { v.x;v.y; }) {fill(v.x,seed+1u); fill(v.y,seed+2u);}
    else if constexpr(requires {v.begin();v.end();typename T::value_type;}) {
        v.clear();for(unsigned n=0;n<seed%4u;++n){typename T::value_type item{};fill(item,seed+5u*n);
            if constexpr(requires {v.push_back(item);})v.push_back(item);else v.insert(item);}
    }else static_assert(sizeof(T)==0,"uninitialized complete field");
}
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
void save(const RuntimeTeamMissionState&);
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


void save(const RuntimeTeamMissionState& v){save(v.instanceId);save(v.teamTypeId);save(v.scriptId);sequence(v.objectIds);save(static_cast<int>(v.stepIndex));save(v.stepStartedTick);save(v.stepIssued);save(v.targetObjectId);save(v.targetCell);save(v.completed);save(v.sourceAiTriggerId);save(v.sourceAiBuildId);save(v.aiOutcomeReported);save(v.achievedGreatSuccess);save(v.targetInteractionCompleted);mapping(v.transportReturnCells);save(v.regrouping);save(v.regroupCell);save(v.regroupStartedTick);mapping(v.regroupDestinations);save(v.status);}
std::string snapshot(const Model& s){image.clear();save(s.tick);save(s.ai);mapping(s.triggers.computerTacticsByOwner);mapping(s.triggers.teamMissions);save(s.triggers.nextTeamMissionInstanceId);save(static_cast<int>(s.objects.size()));for(const auto& [id,o]:s.objects){save(id);save(encode(o));}sequence(s.trace);return image;}
policy::RuntimeComputerTacticLifecycleState view(Model& s){return {s.triggers.computerAiTriggers,s.triggers.computerTacticsByOwner,s.triggers.taskForces,s.triggers.teamMissions,s.triggers.nextTeamMissionInstanceId,s.tick};}
struct Outcome {bool result=false;std::string error,state;std::vector<std::string> trace;RuntimeComputerAiState ai;RuntimeComputerTacticsState tactics;std::map<int,RuntimeTeamMissionState> missions;};
int checks=0,scenarios=0,faults=0;
void require(bool condition,const std::string& text){++checks;if(!condition)throw std::runtime_error(text);}
Outcome execute(Model s,bool old,int mode=0,int id=1){active=&s;Queries q(s);Effects e(s);auto state=view(s);Outcome out;auto& ts=s.triggers.computerTacticsByOwner[canonical(s.ai.owner)];try{if(mode==0)out.result=old?original::advanceRuntimeComputerTactics(s,s.ai):policy::advanceRuntimeComputerTactics(state,q,e,s.ai);else if(mode==1){auto& team=ts.teams.at(42);if(old)original::reportRuntimeComputerTacticOutcome(s,ts,team);else policy::reportRuntimeComputerTacticOutcome(state,q,ts,team);}else if(mode==2)out.result=old?original::runtimeComputerTacticMemberPresent(s,s.ai,id):policy::runtimeComputerTacticMemberPresent(q,s.ai,id);else {auto& team=ts.teams.at(42);const auto& type=s.triggers.teams.at("type");if(old)original::refreshRuntimeComputerTacticMissing(s,team,type);else policy::refreshRuntimeComputerTacticMissing(state,q,team,type);}}catch(const std::out_of_range& ex){out.error=std::string("range:")+ex.what();}catch(const std::runtime_error& ex){out.error=std::string("runtime:")+ex.what();}out.state=snapshot(s);out.trace=s.trace;out.ai=s.ai;out.tactics=ts;out.missions=s.triggers.teamMissions;active=nullptr;return out;}
Outcome compare(const std::string& label,const Model& source,int mode=0,int id=1){auto before=execute(source,true,mode,id),after=execute(source,false,mode,id);++scenarios;require(before.result==after.result,label+":return");require(before.error==after.error,label+":exception");require(before.trace==after.trace,label+":phase");require(before.state==after.state,label+":full AI/tactics/mission/world87 state");return after;}
void faultMatrix(const std::string& label,const Model& source,int mode=0,int id=1){auto complete=compare(label,source,mode,id);require(complete.error.empty(),label+":baseline");for(int n=1;n<=static_cast<int>(complete.trace.size());++n){auto s=source;s.throwAt=n;auto out=compare(label+" fault "+std::to_string(n),s,mode,id);require(!out.error.empty(),label+":injected");++faults;}}
Model basic(){Model s;s.ai.owner=" AI ";s.ai.country="Americans";s.ai.difficulty="Hard";s.ai.initialized=true;s.ai.credits=10000;s.ai.status="unpublished";s.ai.productionSerial=17;s.ai.attackWaves=4;s.ai.attacksOrdered=13;s.metrics.harvesters=2;
 s.rules.tactics.dissolveUnfilledTeamDelay=10;s.rules.tactics.successWeightDelta=1.25;s.rules.tactics.failureWeightDelta=-2.75;s.rules.tactics.trackRecordCoefficient=0.5;s.rules.tactics.fillEarliestTeamProbability={50,50,50};
 RuntimeCampaignAiTriggerDefinition trigger;trigger.id="trigger";trigger.currentWeight=3.125;trigger.minimumWeight=0.125;trigger.maximumWeight=100.25;s.triggers.computerAiTriggers.push_back(trigger);
 RuntimeTeamType type;type.id="type";type.name="Authored type";type.house="original-house";type.scriptId="script";type.taskForceId="task";type.veteranLevel=4;s.triggers.teams[type.id]=type;
 RuntimeTaskForce task;task.id="task";task.members={{2,"TANK"},{1,"INF"}};s.triggers.taskForces[task.id]=task;
 for(int id=1;id<=6;++id){WorldObjectInstance o;fill(o,static_cast<unsigned>(id*11));o.instanceId=id;o.owner="AI";o.type=id==3?"inf":"tank";o.alive=true;o.mission="Guard";o.factoryExitHostId=-1;o.veterancy=0;s.objects[id]=o;}s.objects[3].alive=false;s.objects[3].mission="InsideTransport";s.objects[4].alive=false;s.objects[4].mission="Removed";s.pending.insert(4);s.objects[5].owner="enemy";s.objects[6].alive=false;
 RuntimeComputerTacticTeam team;team.instanceId=42;team.triggerId="trigger";team.teamTypeId="type";team.targetOwner="Enemy";team.objectIds={1,2,3};team.joiningObjectIds={3,4};team.missingMembers={"sentinel"};team.createdTick=50;team.lastProgressTick=100;team.missionInstanceId=70;auto& t=s.triggers.computerTacticsByOwner["ai"];t.nextTeamInstanceId=80;t.teams[42]=team;t.history["other"]={-0.0,4,5,6};t.captureDecisions[7]={8,9,10};s.triggers.computerTacticsByOwner["unrelated"].nextTriggerTick=777;
 RuntimeTeamMissionState mission;mission.instanceId=70;mission.objectIds={4,5};mission.status="original old mission";mission.transportReturnCells[4]={8,9};mission.regroupDestinations[5]={10,11};s.triggers.teamMissions[70]=mission;s.triggers.nextTeamMissionInstanceId=103;
 for(auto id:{"TANK","INF","HARV","ENG"}){RuntimeBuildCatalogItem item;item.typeId=id;item.kind=(item.typeId=="INF"||item.typeId=="ENG")?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit;item.cost=100;item.buildTicks=6;item.owner="AI";s.catalog[id]=item;}return s;}
void run(){auto base=basic();auto s=base;auto& bt=base.triggers.computerTacticsByOwner.at("ai");(void)bt;
 auto launched=compare("launch complete",base);require(launched.tactics.teams.at(42).launched&&launched.ai.attackWaves==5&&launched.ai.attacksOrdered==16&&launched.missions.at(103).sourceAiBuildId==42,"original launch publication");faultMatrix("launch phases",base);
 s=base;s.triggers.computerAiTriggers.clear();require(compare("empty triggers",s).trace.empty(),"empty does not advance production");
 s=base;s.productionChanged=true;s.ai.nextThinkTick=101;auto early=compare("production before think",s);require(early.result&&early.trace==std::vector<std::string>{"advance-production"},"early return after production");
 s=base;s.productionChanged=true;s.ai.nextThinkTick=101;s.tickAfterProduction=102;compare("tick remains live after production",s);
 for(int id:{1,3,4,5,6,999})faultMatrix("member shortcircuit "+std::to_string(id),base,2,id);
 for(int bits=0;bits<64;++bits){s=base;auto& team=s.triggers.computerTacticsByOwner["ai"].teams.at(42);team.launched=(bits&1)!=0;team.missionFinished=(bits&2)!=0;team.baseDefense=(bits&4)!=0;s.triggers.teams["type"].reinforce=(bits&8)!=0;if(bits&16)team.objectIds.clear();if(bits&32)s.triggers.teamMissions[70].achievedGreatSuccess=true;compare("lifecycle flags "+std::to_string(bits),s);compare("missing flags "+std::to_string(bits),s,3);}
 for(int variant=0;variant<20;++variant){s=base;auto& team=s.triggers.computerTacticsByOwner["ai"].teams.at(42);switch(variant){case 0:team.teamTypeId="missing";break;case 1:team.objectIds={1,4,5,6,999};break;case 2:s.objects[1].factoryExitHostId=9;break;case 3:s.objects[1].mission="FactoryExit";break;case 4:team.objectIds={1};team.lastProgressTick=79;break;case 5:team.objectIds={1};team.lastProgressTick=80;break;case 6:s.triggers.taskForces.clear();break;case 7:team.objectIds.clear();break;case 8:team.objectIds={1};s.recruitAvailable=true;break;case 9:s.newSelection=true;s.recruitAvailable=true;break;case 10:s.joinChanged=true;team.launched=true;s.triggers.teams["type"].reinforce=true;break;case 11:s.productionChanged=true;break;case 12:s.recruitChanged=true;break;case 13:s.selectChanged=true;break;case 14:s.triggers.teams["type"].veteranLevel=0;break;case 15:s.objects[1].veterancy=10;break;case 16:s.objects[3].alive=true;s.objects[3].mission="Guard";break;case 17:team.objectIds={1,1,2,3,3};break;case 18:s.objects.erase(2);break;case 19:s.tick=0;team.lastProgressTick=-21;break;}faultMatrix("branch "+std::to_string(variant),s);}
 s=base;auto& finished=s.triggers.computerTacticsByOwner["ai"];finished.teams.at(42).launched=true;finished.teams.at(42).missionFinished=true;finished.production[1]={42,"TANK",5,100,10};auto cleanup=compare("finish paid order",s);require(!cleanup.tactics.teams.contains(42)&&!cleanup.missions.contains(70)&&cleanup.tactics.production[1].teamInstanceId==-1&&cleanup.tactics.production[1].remainingTicks==5,"cleanup detaches paid work");faultMatrix("cleanup phases",s);
 for(int secondary=0;secondary<2;++secondary)for(int reported=0;reported<2;++reported)for(int success=0;success<2;++success){s=base;auto& team=s.triggers.computerTacticsByOwner["ai"].teams.at(42);team.secondary=secondary!=0;team.outcomeReported=reported!=0;team.achievedGreatSuccess=success!=0;faultMatrix("outcome flags",s,1);}
 for(double weight:{-1.0,-0.0,0.0,0.125,99.75,100.25,500.0,std::numeric_limits<double>::quiet_NaN()})for(int success:{0,1,7}){s=base;auto& t=s.triggers.computerTacticsByOwner["ai"];t.history["trigger"]={weight,18,success,9-success};t.teams.at(42).achievedGreatSuccess=(success&1)!=0;compare("prior record clamp",s,1);}
 s=base;s.triggers.computerAiTriggers.front().id="unknown";auto noTrigger=compare("outcome no definition",s,1);require(noTrigger.tactics.teams.at(42).outcomeReported&&!noTrigger.tactics.history.contains("trigger"),"outcome marks before missing definition");
 for(int choice=0;choice<6;++choice){s=base;s.triggers.computerTacticsByOwner["ai"].teams.at(42).objectIds={1};s.harvester=choice==1?std::optional<RuntimeBuildCatalogItem>{s.catalog.at("HARV")}:std::nullopt;s.megawealth=choice>=2;s.needsEngineer=choice>=3;if(choice>=4)s.engineer=s.catalog.at("ENG");if(choice==5)s.ai.credits=0;faultMatrix("economic before combat "+std::to_string(choice),s);}
 for(int serial=0;serial<96;++serial){s=base;s.tick=serial*7;s.ai.difficulty=serial%3==0?"Easy":serial%3==1?"Hard":"Unknown";s.ai.owner=serial%2?"AI":" AI ";auto& ts=s.triggers.computerTacticsByOwner["ai"];for(int n=0;n<4;++n){auto t=ts.teams.at(42);t.instanceId=42+n;t.objectIds={1};t.createdTick=s.tick;t.lastProgressTick=s.tick;t.missingMembers={"INF","TANK"};ts.teams[t.instanceId]=t;}s.rules.tactics.fillEarliestTeamProbability={serial%101,serial%101,serial%101};compare("ordered fill hash "+std::to_string(serial),s);}
 for(int count:{-1,0,1,2,5}){s=base;s.triggers.taskForces["task"].members={{count,"tank"},{2,"TANK"},{1," inf "},{1,"INF"}};compare("duplicate case and exact strings",s,3);}
 s=base;auto other=s.triggers.computerTacticsByOwner["ai"].teams.at(42);other.instanceId=43;s.triggers.computerTacticsByOwner["ai"].teams[43]=other;auto two=compare("two mission IDs live",s);require(two.tactics.teams.at(42).missionInstanceId==103&&two.tactics.teams.at(43).missionInstanceId==104,"borrowed nextMissionId live");
}
}
int main(){try{fixture::run();std::cout<<"Computer tactic lifecycle: checks="<<fixture::checks<<" complete-states="<<fixture::scenarios<<" injected-faults="<<fixture::faults<<" original-body/reference=yes full-world-fields=87 model-only=yes\n";return 0;}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
