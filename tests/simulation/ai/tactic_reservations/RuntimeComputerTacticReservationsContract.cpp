#include "simulation/ai/RuntimeComputerTacticReservations.h"
#include "simulation/buildings/RuntimeGrindingSystem.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "data/rules/RulesDatabase.h"
#include <algorithm>
#include <bit>
#include <climits>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
int runRuntimeComputerTacticReservationsLiveBorrowContract();
namespace domain = concord::simulation::ai;
namespace {
int checks=0, models=0, faults=0;
void require(bool value,const std::string& detail){++checks;if(!value)throw std::runtime_error(detail);}
struct Trace {
    std::vector<std::string> reads;
    int failAt=0;
    void hit(const std::string& name) { reads.push_back(name);if(static_cast<int>(reads.size())==failAt)throw std::runtime_error("read fault "+name); }
};
struct Model {
    bool campaignMode=false;
    std::set<int> transport,engineer,occupy;
    std::vector<RuntimeGrindingRelation> grinding;
    std::map<std::string,RuntimeComputerTacticsState> owners;
    std::map<std::string,RuntimeTeamType> teams;
    std::map<std::string,RuntimeScriptType> scripts;
    std::map<int,RuntimeTeamMissionState> missions;
};
struct PendingRef {
    const std::set<int>& data;Trace& trace;std::string name;
    bool contains(int id)const {trace.hit(name+"="+std::to_string(id));return data.contains(id);}
};
struct GrindingRef {
    const std::vector<RuntimeGrindingRelation>& data;Trace& trace;
    const std::vector<RuntimeGrindingRelation>& relations()const {trace.hit("grinding");return data;}
};
struct OwnersRef {
    const std::map<std::string,RuntimeComputerTacticsState>& data;Trace& trace;
    auto begin()const {trace.hit("owners");return data.begin();}
    auto end()const {return data.end();}
    auto find(const std::string& owner)const {return data.find(owner);}
};
struct TriggerRef {
    OwnersRef computerTacticsByOwner;
    const std::map<std::string,RuntimeTeamType>& teams;
    const std::map<std::string,RuntimeScriptType>& scripts;
    const std::map<int,RuntimeTeamMissionState>& teamMissions;
};
struct RootRef {
    const bool& campaignMode;
    PendingRef pendingTransportTargets,pendingEngineerCaptures,pendingOccupyTargets;
    GrindingRef grinding;
    TriggerRef triggers;
    RootRef(const Model& m,Trace& t):campaignMode(m.campaignMode),
        pendingTransportTargets{m.transport,t,"transport"},pendingEngineerCaptures{m.engineer,t,"engineer"},
        pendingOccupyTargets{m.occupy,t,"occupy"},grinding{m.grinding,t},
        triggers{{m.owners,t},m.teams,m.scripts,m.missions}{}
};
namespace original {
using RuntimeGameSessionState=RootRef;
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
#include "RuntimeComputerTacticReservationsOriginal.inl"
}
class Queries final:public domain::RuntimeComputerTacticObjectReservationQueries {
    const Model& model;Trace& trace;
public:
    Queries(const Model& m,Trace& t):model(m),trace(t){}
    bool pendingTransportTarget(int id)const override {trace.hit("transport="+std::to_string(id));return model.transport.contains(id);}
    bool pendingEngineerCapture(int id)const override {trace.hit("engineer="+std::to_string(id));return model.engineer.contains(id);}
    bool pendingOccupyTarget(int id)const override {trace.hit("occupy="+std::to_string(id));return model.occupy.contains(id);}
    const std::vector<RuntimeGrindingRelation>& grindingRelations()const override {trace.hit("grinding");return model.grinding;}
    const std::map<std::string,RuntimeComputerTacticsState>& ownerTactics()const override {trace.hit("owners");return model.owners;}
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
void save(const RuntimeScriptStep&);
void save(const RuntimeGrindingRelation&);
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
void save(const RuntimeTeamMissionState& v){save(v.instanceId);save(v.teamTypeId);save(v.scriptId);sequence(v.objectIds);save(v.stepIndex);save(v.stepStartedTick);save(v.stepIssued);save(v.targetObjectId);save(v.targetCell);save(v.completed);save(v.sourceAiTriggerId);save(v.sourceAiBuildId);save(v.aiOutcomeReported);save(v.achievedGreatSuccess);save(v.targetInteractionCompleted);mapping(v.transportReturnCells);save(v.regrouping);save(v.regroupCell);save(v.regroupStartedTick);mapping(v.regroupDestinations);save(v.status);}
void save(const RuntimeTeamType& v){
save(v.id);save(v.name);save(v.house);save(v.waypointId);save(v.scriptId);save(v.taskForceId);save(v.priority);save(v.max);save(v.group);save(v.autocreate);save(v.prebuild);save(v.reinforce);save(v.full);save(v.aggressive);save(v.isBaseDefense);save(v.recruitable);save(v.recruiter);save(v.looseRecruit);save(v.avoidThreats);save(v.suicide);save(v.guardSlower);save(v.onlyTargetHouseEnemy);save(v.transportsReturnOnUnload);save(v.veteranLevel);save(v.mindControlDecision);
}
void save(const RuntimeScriptStep& v){save(v.action);save(v.argument);}
void save(const RuntimeScriptType& v){save(v.id);save(v.name);sequence(v.steps);}
void save(const RuntimeGrindingRelation& v){save(v.hostId);save(v.sourceId);save(static_cast<int>(v.phase));save(v.hostOwner);save(v.sourceOwner);save(v.hostType);save(v.sourceType);save(v.queuedTick);save(v.enteringTick);sequence(v.passengerIds);}
std::string modelImage(const Model& m){image.clear();save(m.campaignMode);sequence(m.transport);sequence(m.engineer);sequence(m.occupy);sequence(m.grinding);mapping(m.owners);mapping(m.teams);mapping(m.scripts);mapping(m.missions);return image;}
struct Outcome {bool result=false;std::string error;};
template<class F> Outcome invoke(F&& action){Outcome out;try{out.result=action();}catch(const std::runtime_error& e){out.error=e.what();}return out;}
void objectCase(const std::string& name,const Model& model,int id,bool expected){
    ++models;const auto before=modelImage(model);
    Trace base;Queries baseQueries(model,base);
    const bool baseResult=domain::runtimeComputerTacticReservesObject(baseQueries,id);
    require(baseResult==expected,name+" independent object answer");
    require(modelImage(model)==before,name+" initial no mutation");
    for(int fail=0;fail<=static_cast<int>(base.reads.size())+1;++fail){
        Trace a,b;a.failAt=fail;b.failAt=fail;
        RootRef root(model,a);Queries query(model,b);
        const auto left=invoke([&]{return original::runtimeComputerTacticReservesObject(root,id);});
        const auto right=invoke([&]{return domain::runtimeComputerTacticReservesObject(query,id);});
        require(left.result==right.result && left.error==right.error,name+" original result/error");
        require(a.reads==b.reads,name+" original ordered lazy reads");
        require(modelImage(model)==before,name+" full model retained");
        if(fail>0 && fail<=static_cast<int>(base.reads.size())){require(!right.error.empty(),name+" reached fault retained");++faults;}
        else require(right.error.empty() && right.result==expected,name+" unreachable read not invoked");
    }
}
void superCase(const std::string& name,const Model& model,const std::string& owner,const SuperWeaponRule* weapon,bool expected){
    ++models;const auto before=modelImage(model);Trace trace;RootRef root(model,trace);
    const auto left=original::runtimeComputerTacticReservesSuperWeapon(root,owner,weapon);
    const domain::RuntimeComputerTacticSuperReservationState state{model.campaignMode,model.owners,model.teams,model.scripts,model.missions};
    const auto right=domain::runtimeComputerTacticReservesSuperWeapon(state,owner,weapon);
    require(left==right,name+" original answer");require(right==expected,name+" independent answer");
    require(trace.reads.empty(),name+" no object reservation callbacks");
    require(modelImage(model)==before,name+" full model retained");
}
Model superModel(int action){
    Model m;
    auto& owner=m.owners["soviet"];owner.nextTriggerTick=17;owner.selectionSerial=INT_MAX;owner.nextTeamInstanceId=94;
    RuntimeComputerTacticTeam team;team.instanceId=73;team.triggerId="trigger";team.teamTypeId="task";team.targetOwner="target";
    team.objectIds={19,3};team.joiningObjectIds={7};team.missingMembers={"E1","HTNK"};team.createdTick=31;team.lastProgressTick=49;
    team.missionInstanceId=9;team.secondary=true;team.baseDefense=true;team.launched=true;team.outcomeReported=true;team.achievedGreatSuccess=true;
    owner.teams.emplace(73,team);
    RuntimeTeamType type;type.id="authored";type.name="type";type.house="owner";type.scriptId="script";type.taskForceId="force";type.max=2;type.priority=11;
    m.teams.emplace("task",type);
    RuntimeScriptType script;script.id="authored-script";script.name="script-name";script.steps={{5,91},{action,17},{0,29}};
    m.scripts.emplace("script",script);
    RuntimeTeamMissionState mission;mission.instanceId=9;mission.teamTypeId="other-team";mission.scriptId="unrelated-key";mission.objectIds={19,3};mission.stepIndex=0;
    mission.stepStartedTick=74;mission.stepIssued=true;mission.targetObjectId=INT_MAX;mission.targetCell={-9,17};mission.completed=true;
    mission.sourceAiTriggerId="source";mission.sourceAiBuildId=82;mission.aiOutcomeReported=true;mission.achievedGreatSuccess=true;mission.targetInteractionCompleted=true;
    mission.transportReturnCells.emplace(17,RuntimeCellPoint{21,29});mission.regrouping=true;mission.regroupCell={77,91};mission.regroupStartedTick=93;
    mission.regroupDestinations.emplace(19,RuntimeCellPoint{31,47});mission.status="still borrowed";m.missions.emplace(9,mission);
    owner.history["unused"]={0.375,4,1,3};
    return m;
}
void objectCases(){
    for(int id:std::vector<int>{-1,0,7,INT_MAX})for(int mask=0;mask<8;++mask){Model m;
        if(mask&1)m.transport.insert(id);if(mask&2)m.engineer.insert(id);if(mask&4)m.occupy.insert(id);
        m.transport.insert(51);m.engineer.insert(52);m.occupy.insert(53);
        objectCase("pending masks",m,id,mask!=0);
    }
    for(int position=0;position<3;++position){Model m;
        for(int i=0;i<3;++i){RuntimeGrindingRelation rel;rel.hostId=700+i;rel.sourceId=i==position?7:50+i;rel.hostOwner="host";rel.sourceOwner="source";rel.hostType="grinder";rel.sourceType="tank";rel.phase=RuntimeGrindingRelationPhase::Entering;rel.queuedTick=39;rel.enteringTick=42;rel.passengerIds={80,81};m.grinding.push_back(rel);}
        objectCase("grinding first/middle/last",m,7,true);
        objectCase("passenger is not root",m,80,false);objectCase("host is not root",m,700,false);
    }
    for(const std::string owner:{"","alpha","zulu"})for(int position=0;position<3;++position)for(bool finished:{false,true}){
        Model m=superModel(0);m.owners.clear();m.owners["unrelated"].teams[1].objectIds={5,9};
        for(int i=0;i<3;++i){auto& team=m.owners[owner].teams[10+i];team.instanceId=90+i;team.objectIds={11,12};if(i==position)team.objectIds.push_back(7);team.missionFinished=finished;team.outcomeReported=true;team.joiningObjectIds={8};team.missingMembers={"7"};}
        objectCase("any owner and finished team",m,7,true);objectCase("joining member not root",m,8,false);objectCase("missing type not object",m,6,false);
    }
    {Model m;objectCase("empty all",m,7,false);m.owners["soviet"];objectCase("empty teams",m,7,false);}
}
void superCases(){
    const std::vector<std::pair<std::string,int>> identities={{"ChronoSphere",1},{"IRONCURTAIN",2},{"Chrono IronCurtain",3},{"chrono-looking-suffix",1},{"Iron Curtain",0},{"nuke",0},{"",0}};
    const std::vector<std::size_t> starts={0,1,2,3,4,std::numeric_limits<std::size_t>::max()};
    for(const auto& [identity,bits]:identities)for(int action:{-1,0,54,55,56,57,58})for(auto start:starts)for(bool missing:{false,true}){
        auto m=superModel(action);if(missing)m.missions.clear();else m.missions.at(9).stepIndex=start;
        SuperWeaponRule w;w.id=identity;w.type="runtime";
        const bool relevant=(action==55 && (bits&2)) || ((action==56 || action==57) && (bits&1));
        const bool beforeStep=missing || start<=1;
        superCase("identity/action/step",m," \tSoViEt\r\n",&w,relevant && beforeStep);
    }
    SuperWeaponRule chrono;chrono.id="irrelevant";chrono.type="CHRONOSPHERE";
    SuperWeaponRule iron;iron.id="irrelevant";iron.type="IRONCURTAIN";
    {auto m=superModel(56);superCase("identity in type",m,"soviet",&chrono,true);
        superCase("null rule",m,"soviet",nullptr,false);m.campaignMode=true;superCase("campaign early exit",m,"soviet",&chrono,false);}
    {auto m=superModel(55);superCase("iron in type",m,"soviet",&iron,true);superCase("owner not found",m,"allied",&iron,false);
        m.owners.at("soviet").teams.at(73).missionFinished=true;superCase("finished team skipped",m,"soviet",&iron,false);}
    {auto m=superModel(56);m.teams.clear();superCase("missing team type",m,"soviet",&chrono,false);}
    {auto m=superModel(56);m.scripts.clear();superCase("missing script",m,"soviet",&chrono,false);}
    {auto m=superModel(56);m.scripts.at("script").steps.clear();superCase("empty steps",m,"soviet",&chrono,false);}
    {auto m=superModel(56);auto team=m.owners.at("soviet").teams.at(73);m.owners.at("soviet").teams.at(73).missionFinished=true;team.missionFinished=false;m.owners.at("soviet").teams.emplace(99,team);superCase("later live team",m,"soviet",&chrono,true);}
    {auto m=superModel(56);const domain::RuntimeComputerTacticSuperReservationState borrowed{m.campaignMode,m.owners,m.teams,m.scripts,m.missions};
        require(domain::runtimeComputerTacticReservesSuperWeapon(borrowed,"soviet",&chrono),"initial live refs");
        m.campaignMode=true;require(!domain::runtimeComputerTacticReservesSuperWeapon(borrowed,"soviet",&chrono),"campaign live ref");
        m.campaignMode=false;m.scripts.at("script").steps[1].action=0;require(!domain::runtimeComputerTacticReservesSuperWeapon(borrowed,"soviet",&chrono),"script live ref");
        m.scripts.at("script").steps[1].action=56;m.missions.at(9).stepIndex=2;require(!domain::runtimeComputerTacticReservesSuperWeapon(borrowed,"soviet",&chrono),"mission step live ref");
        m.missions.clear();require(domain::runtimeComputerTacticReservesSuperWeapon(borrowed,"soviet",&chrono),"mission missing starts at zero");
        m.teams.clear();require(!domain::runtimeComputerTacticReservesSuperWeapon(borrowed,"soviet",&chrono),"team type live ref");m.owners.clear();require(!domain::runtimeComputerTacticReservesSuperWeapon(borrowed,"soviet",&chrono),"owner live ref");}
}
}
int main(){try{const int liveChecks=runRuntimeComputerTacticReservationsLiveBorrowContract();if(liveChecks!=24)throw std::runtime_error("live query scenario count");std::cout<<"live-borrow checks="<<liveChecks<<" failures=0\n";objectCases();superCases();std::cout<<"reservation checks="<<checks<<" model-states="<<models<<" faults="<<faults<<" failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<"reservation failure: "<<e.what()<<"\n";return 1;}}
