#include "simulation/abilities/unit_special/RuntimeTimedBomb.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void need(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }
}
int main() try {
    std::map<int,TimedBombObjectView> world{
        {1,{true,true,100,"Source",{1,3}}},
        {2,{true,true,100,"Source",{2,3}}},
        {3,{true,true,100,"Source",{3,3}}},
        {10,{true,true,100,"Target",{10,3}}},
        {20,{true,true,100,"Target",{20,3}}},
        {30,{true,true,100,"Target",{30,3}}}};
    std::vector<int> queries;
    const TimedBombObjectQuery query=[&](int id) {
        queries.push_back(id);
        const auto found=world.find(id);
        return found==world.end()?TimedBombObjectView{}:found->second;
    };
    int eligibilityCalls=0;
    const TimedBombEligibilityQuery eligibility=[&](int id,TimedBombEligibility kind) {
        ++eligibilityCalls;
        switch(kind) {
        case TimedBombEligibility::C4Source:return id==1;
        case TimedBombEligibility::IvanSource:return id==2;
        case TimedBombEligibility::C4Target:return id==30;
        case TimedBombEligibility::IvanTarget:return id>=10;
        }
        return false;
    };
    std::vector<SpecialMechanismEvent> events;
    const SpecialEventSink emit=[&](const auto& e){events.push_back(e);};
    const TimedBombDisarmQuery disarms=[](const auto& id){return id=="DisarmWH";};
    const TimedBombRules rules{.01,"Super",9,450,"IvanWH"};
    RuntimeTimedBombState s;std::string why;
    need(!s.tryPlantC4(query,eligibility,rules,99,30,100,15,emit,why),"missing source rejected");
    need(queries==std::vector<int>({99,30}) && eligibilityCalls==0,"source,target query before lazy eligibility");
    need(why=="C4 source is not alive" && events.back().cell.x==30,"original rejection and target cell");
    need(s.tryPlantC4(query,eligibility,rules,1,30,100,15,emit,why),"plant C4");
    need(s.tryAttachIvanBomb(query,eligibility,rules,2,20,100,15,emit,why),"plant Ivan20");
    need(s.tryAttachIvanBomb(query,eligibility,rules,2,10,100,15,emit,why),"plant Ivan10");
    need(s.records().at(30).detonateTick==109 && s.records().at(10).detonateTick==109,"original deadlines");
    need(!s.tryAttachIvanBomb(query,eligibility,rules,2,20,100,15,emit,why)
        && why=="target already has a timed bomb","duplicate gate");
    need(!s.tryDisarmTimedBomb(query,disarms,3,20,"IvanWH",101,emit,why),"wrong disarm");
    need(s.tryDisarmTimedBomb(query,disarms,3,20,"DisarmWH",105,emit,why),"early disarm");
    s.onOwnerChanged(2,"CapturedSource");
    s.onObjectRemoved(query,2,105,emit,"source removed",nullptr);
    need(s.records().size()==2 && s.records().at(10).owner=="CapturedSource","source removal retains captured owner");
    auto copied=s;RuntimeTimedBombState empty;copied.swap(empty);
    need(copied.records().empty() && empty.records().size()==2,"nothrow swap/copy transaction support");
    need(s.tickTimedBombs(108,emit).empty(),"early quiet");
    const auto actions=s.tickTimedBombs(109,emit);
    need(actions.size()==2 && actions[0].targetObjectId==10 && actions[1].targetObjectId==30,"same tick sorted");
    need(actions[0].damage==450 && !actions[0].forceDestruction
        && actions[0].warheadId=="IvanWH" && actions[1].forceDestruction
        && actions[1].warheadId=="Super","unchanged damage action values");
    need(s.tickTimedBombs(110,emit).empty() && s.records().empty(),"exactly once");

    need(s.tryAttachIvanBomb(query,eligibility,rules,2,20,0,15,emit,why),"dead target seed");
    world.at(20).health=0;
    std::vector<SpecialMechanismAction> death;
    s.reconcile(query,1,emit,&death);
    need(death.size()==1 && death[0].cell.x==20
        && death[0].reason=="Ivan bomb target died during reconciliation","present dead target action");
    world.at(20).health=100;
    need(s.tryAttachIvanBomb(query,eligibility,rules,2,20,0,15,emit,why),"missing target seed");
    world.erase(20);death.clear();s.reconcile(query,1,emit,&death);
    need(death.empty() && events.back().reason=="target missing" && events.back().cell.x==-1,"missing target no blast");
    need(s.tryAttachIvanBomb(query,eligibility,rules,2,10,0,15,emit,why),"null action sink seed");
    s.onObjectRemoved(query,10,1,emit,"removed",nullptr);
    need(s.records().empty() && events.back().kind=="IvanBombTargetRemoved","null action sink semantics");
    s.reset();need(s.records().empty(),"reset");
    std::cout<<"TimedBomb independent module: lazy queries,original rules,events/actions,same-tick order,early disarm,owner/source removal,dead/missing targets,null action sink,copy/swap passed\n";
    std::cout<<"ABI|"<<sizeof(SpecialCell)<<'|'<<sizeof(SpecialMechanismEvent)
             <<'|'<<sizeof(SpecialMechanismAction)<<'|'<<sizeof(TimedBombState)
             <<'|'<<alignof(TimedBombState)<<'|'<<offsetof(TimedBombState,owner)
             <<'|'<<offsetof(TimedBombState,detonateTick)<<'|'<<offsetof(TimedBombState,forceDestruction)<<'\n';
    return 0;
} catch(const std::exception& e) {std::cerr<<"TimedBomb independent module failed: "<<e.what()<<'\n';return 1;}
