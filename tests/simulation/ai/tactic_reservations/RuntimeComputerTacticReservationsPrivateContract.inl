#include <memory>
#include <climits>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <sstream>
namespace concord_reservation_private_qualification {
using namespace concord::runtime::session_detail;
namespace original {
#include "RuntimeComputerTacticReservationsOriginal.inl"
}
int checks=0, states=0;
void require(bool value,const char* what){++checks;if(!value)throw std::runtime_error(what);}
std::string image(const RuntimeGameSessionState& s){
    std::ostringstream out;out<<s.campaignMode<<'|'<<s.tick<<'|'<<s.triggers.nextTeamMissionInstanceId<<'|'<<s.grinding.stateHash();
    const auto mapImage=[&out](const auto& values){std::map<int,int> ordered(values.begin(),values.end());for(const auto& [key,value]:ordered)out<<'|'<<key<<':'<<value;out<<';';};
    mapImage(s.pendingTransportTargets);mapImage(s.pendingEngineerCaptures);mapImage(s.pendingOccupyTargets);
    for(const auto& [owner,tactics]:s.triggers.computerTacticsByOwner){out<<owner<<'|'<<tactics.nextTriggerTick<<'|'<<tactics.selectionSerial<<'|'<<tactics.nextTeamInstanceId;
      for(const auto& [key,team]:tactics.teams){out<<key<<'|'<<team.teamTypeId<<'|'<<team.missionInstanceId<<'|'<<team.missionFinished;for(int id:team.objectIds)out<<'|'<<id;}}
    for(const auto& [key,mission]:s.triggers.teamMissions)out<<key<<'|'<<mission.stepIndex;
    for(const auto& [key,type]:s.triggers.teams)out<<key<<'|'<<type.scriptId;
    for(const auto& [key,script]:s.triggers.scripts){out<<key;for(const auto& step:script.steps)out<<'|'<<step.action<<':'<<step.argument;}
    return out.str();
}
void object(RuntimeGameSessionState& s,int id,bool answer){++states;const auto before=image(s);
    require(original::runtimeComputerTacticReservesObject(s,id)==answer,"actual-root original object expected answer");
    require(concord::runtime::session_detail::runtimeComputerTacticReservesObject(s,id)==answer,"actual production wrapper object answer");
    require(image(s)==before,"actual borrowed root unchanged");}
void weapon(RuntimeGameSessionState& s,const std::string& owner,const SuperWeaponRule* w,bool answer){++states;const auto before=image(s);
    require(original::runtimeComputerTacticReservesSuperWeapon(s,owner,w)==answer,"actual-root original weapon expected answer");
    require(concord::runtime::session_detail::runtimeComputerTacticReservesSuperWeapon(s,owner,w)==answer,"actual production wrapper weapon answer");
    require(image(s)==before,"actual borrowed root unchanged");}
void objectStates(){
    auto p=std::make_unique<RuntimeGameSessionState>();auto& s=*p;
    for(int id:std::vector<int>{-1,0,7,INT_MAX})for(int mask=0;mask<8;++mask){
       s.pendingTransportTargets.clear();s.pendingEngineerCaptures.clear();s.pendingOccupyTargets.clear();
       if(mask&1)s.pendingTransportTargets.emplace(id,101);if(mask&2)s.pendingEngineerCaptures.emplace(id,202);if(mask&4)s.pendingOccupyTargets.emplace(id,303);
       object(s,id,mask!=0);
    }
    s.pendingTransportTargets.clear();s.pendingEngineerCaptures.clear();s.pendingOccupyTargets.clear();
    auto& team=s.triggers.computerTacticsByOwner["unrelated-owner"].teams[99];team.objectIds={91,7,84};team.joiningObjectIds={8};team.missionFinished=true;
    object(s,7,true);object(s,8,false);object(s,99,false);
    team.objectIds.clear();object(s,7,false);s.triggers.computerTacticsByOwner.clear();object(s,7,false);
}
void weaponStates(){
    auto p=std::make_unique<RuntimeGameSessionState>();auto& s=*p;
    auto& team=s.triggers.computerTacticsByOwner["soviet"].teams[7];team.teamTypeId="team";team.missionInstanceId=91;
    s.triggers.teams["team"].scriptId="script";s.triggers.scripts["script"].steps={{0,73},{56,1},{0,88}};
    s.triggers.teamMissions[91].stepIndex=0;
    SuperWeaponRule chrono;chrono.id="CHRONOSPHERE";
    SuperWeaponRule iron;iron.id="IRONCURTAIN";
    for(int action:{-1,0,55,56,57,58})for(std::size_t start:std::vector<std::size_t>{0,1,2,3,std::numeric_limits<std::size_t>::max()}){
       s.triggers.scripts.at("script").steps[1].action=action;s.triggers.teamMissions.at(91).stepIndex=start;
       weapon(s," \tSoViEt\r\n",&chrono,(action==56 || action==57)&&start<=1);
       weapon(s,"soviet",&iron,action==55&&start<=1);
    }
    s.triggers.scripts.at("script").steps[1].action=56;s.triggers.teamMissions.clear();
    weapon(s,"soviet",&chrono,true);weapon(s,"missing",&chrono,false);weapon(s,"soviet",nullptr,false);
    s.campaignMode=true;weapon(s,"soviet",&chrono,false);s.campaignMode=false;
    team.missionFinished=true;weapon(s,"soviet",&chrono,false);team.missionFinished=false;
    s.triggers.teams.clear();weapon(s,"soviet",&chrono,false);s.triggers.teams["team"].scriptId="missing";weapon(s,"soviet",&chrono,false);
}
void grindingState(const std::string& fixture){
    auto p=std::make_unique<RuntimeGameSessionState>();auto& s=*p;IniFile ini;
    require(ini.loadFromFile(fixture)&&s.rules.loadFromIni(ini),"existing grinding fixture loads");
    WorldObjectInstance host;host.instanceId=100;host.kind=WorldObjectKind::Structure;host.owner="Yuri";host.type="YAGRND";host.alive=true;host.health=1000;host.poweredOperational=true;
    WorldObjectInstance source;source.instanceId=200;source.kind=WorldObjectKind::Infantry;source.owner="Yuri";source.type="GI";source.alive=true;source.health=100;source.poweredOperational=true;
    s.world.accessObjects()={host,source};RuntimeGrindingContext context;context.rules=&s.rules;context.objects=&s.world.getObjects();
    context.canonicalOwnerId=[](const std::string& owner){return runtime_save_text::toLowerAsciiLocal(owner);};
    context.areAllied=[](const std::string& left,const std::string& right){return runtime_save_text::toLowerAsciiLocal(left)==runtime_save_text::toLowerAsciiLocal(right);};
    context.isEntryActive=[](int,int){return true;};context.isInsideHostFootprint=[](int,int){return false;};
    context.purchaseValue=[](const WorldObjectInstance&){return 100;};context.currentValue=[](const WorldObjectInstance&){return 100;};
    const auto queue=s.grinding.queueEntry(context,100,200,17);require(queue.accepted&&s.grinding.relations().size()==1,"actual grinding owner queued relation");
    object(s,200,true);object(s,100,false);object(s,201,false);
}
int run(const std::string& fixture){try{checks=0;states=0;require(!fixture.empty(),"existing fixture explicit argument");objectStates();weaponStates();grindingState(fixture);std::cout<<"private-reservation checks="<<checks<<" actual-root-states="<<states<<" failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
}
