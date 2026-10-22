#include "ui/input/RuntimeLocalNavigationController.h"
#include "ui/input/RuntimePlayerCommands.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
std::size_t checks=0,failures=0,cases=0,sourceQueries=0,bindingReads=0,traceEvents=0;
void check(bool ok,const std::string& label){++checks;if(!ok){++failures;if(failures<=24)std::cerr<<"FAIL "<<label<<'\n';}}
std::string idsText(const std::vector<int>& ids){std::ostringstream out;for(int id:ids)out<<id<<',';return out.str();}
std::string cellText(RuntimeTeamKeyCell cell){return std::to_string(cell.x)+","+std::to_string(cell.y);}
struct Fixture {
    RuntimePlayerLocalCommands local;
    std::vector<int> selection{1};
    std::vector<int> cycleIds{7,2,1,4};
    std::set<int> screen{1,2,4,6,7},eligible{1,2,3,4,5,6,7};
    std::vector<RuntimePlayerSelectionCandidate> candidates{
        {1,"TANK",0,true,true,200,200},{2,"TANK",1,true,true,50,100},
        {3,"TANK",2,true,false,30,200},{4,"MINER",0,false,true,100,100},
        {5,"INF",1,true,false,0,0},{6,"TANK",2,true,true,-1,2147483647},
        {7,"TANK",0,true,true,2147483647,2147483647}};
    RuntimeInputPixelRect viewport{0,0,633,601};
    RuntimeTeamKeyCell viewportCell{17,23};
    std::optional<RuntimeTeamKeyCell> lastRadar;
    std::vector<RuntimeTeamKeyCell> campaignRadar;
    std::map<std::string,std::int32_t> bindings;
    std::vector<std::string> trace;
    std::vector<int> applied;
    std::vector<std::vector<int>> centeredSelections;
    std::vector<RuntimeTeamKeyCell> centers;
    std::set<int> receivedScreen;
    std::string status="before";
    bool beacon=true;
    std::size_t bindingsRead=0,cycleQueries=0,screenQueries=0,candidateQueries=0;
    std::size_t viewportQueries=0,radarQueries=0,applyCalls=0,clearCalls=0,assignedCalls=0;
    std::optional<std::size_t> replaceSelectionAtBinding;
    std::vector<int> replacementSelection;
    std::optional<std::vector<int>> selectionAfterCycleQuery,selectionAfterApply;
    std::set<int> rejectAtApply;
    bool clearScreenAfterOwnedRead=false,clearCandidatesAfterOwnedRead=false;
    bool changeLastAtScreenQuery=false,changeRanksAtScreenQuery=false,changeLastAtClear=false;
};
Fixture seed(){Fixture f;f.local.teams[0]={1,2};f.local.views[0]=RuntimeMultiplayerGameplayCell{11,22};
    f.local.planning=true;f.local.selectedWaypoint=321;f.local.followTargeting=true;
    f.local.rallyTargeting=true;f.local.cameraFollow=true;f.local.showCoordinates=true;
    f.local.lastSelectionKey=999;f.local.lastSelectionAtMs=12345;return f;}
std::string stamp(const Fixture& f){return "|last="+std::to_string(f.local.lastSelectionKey)+"/"+std::to_string(f.local.lastSelectionAtMs)
    +"|rank="+std::to_string(f.local.veterancyRank)+"/"+std::to_string(f.local.healthRank)+"|follow="+std::to_string(f.local.cameraFollow);}
struct Common {
    Fixture& f;
    explicit Common(Fixture& fixture):f(fixture){}
    void emit(const std::string& text){f.trace.push_back(text+stamp(f));}
    std::int32_t readBinding(const std::string& action,std::int32_t fallback){++f.bindingsRead;
        const auto it=f.bindings.find(action);const auto value=it==f.bindings.end()?fallback:it->second;
        emit("binding:"+action+":"+std::to_string(fallback)+"="+std::to_string(value));
        if(f.replaceSelectionAtBinding==f.bindingsRead){f.selection=f.replacementSelection;emit("binding-selection:"+idsText(f.selection));}return value;}
    void apply(const std::vector<int>& ids){++f.applyCalls;f.applied=ids;emit("apply-owned:"+idsText(ids));
        for(int id:f.rejectAtApply)f.eligible.erase(id);
        std::erase_if(f.selection,[&](int id){return !f.eligible.contains(id);});
        std::vector<int> eligible;for(int id:ids)if(f.eligible.contains(id))eligible.push_back(id);
        f.selection=std::move(eligible);if(f.selectionAfterApply)f.selection=*f.selectionAfterApply;
        emit("apply-result:"+idsText(f.selection));}
    void clear(){++f.clearCalls;emit("beacon-clear");f.beacon=false;
        if(f.changeLastAtClear){f.local.lastSelectionKey=-123;f.local.lastSelectionAtMs=777777;}}
    void center(RuntimeTeamKeyCell cell){emit("camera:"+cellText(cell));f.centers.push_back(cell);f.local.cameraFollow=false;}
};
class CycleHost final:public RuntimeObjectCycleKeyHost,private Common {
public:explicit CycleHost(Fixture& f):Common(f){}
    std::int32_t binding(const std::string& a,std::int32_t b)override{return readBinding(a,b);}
    std::vector<int> eligibleMobileObjectIds()override{++f.cycleQueries;emit("cycle-ids:"+idsText(f.cycleIds));
        auto ids=f.cycleIds;if(f.selectionAfterCycleQuery){f.selection=*f.selectionAfterCycleQuery;emit("cycle-selection:"+idsText(f.selection));}return ids;}
    void applySelection(const std::vector<int>& ids)override{apply(ids);}
    void clearSelectedBeacon()override{clear();}
    void centerCurrentSelection()override{emit("center-current:"+idsText(f.selection));f.centeredSelections.push_back(f.selection);}
};
class ViewHost final:public RuntimeViewNavigationKeyHost,private Common {
public:explicit ViewHost(Fixture& f):Common(f){}
    std::int32_t binding(const std::string& a,std::int32_t b)override{return readBinding(a,b);}
    RuntimeTeamKeyCell currentViewportCenterCell()override{++f.viewportQueries;
        const auto midpoint=runtimeInputPixelBoundsMidpoint({f.viewport.left,f.viewport.top},{f.viewport.right,f.viewport.bottom});
        emit("viewport-midpoint:"+std::to_string(midpoint.x)+","+std::to_string(midpoint.y)+"->"+cellText(f.viewportCell));return f.viewportCell;}
    void centerCamera(RuntimeTeamKeyCell cell)override{center(cell);}
    void viewSaved(std::int32_t number)override{++f.assignedCalls;const auto& saved=f.local.views[static_cast<std::size_t>(number-1)];
        emit("view-saved:"+std::to_string(number)+":"+(saved?std::to_string(saved->x)+","+std::to_string(saved->y):"empty"));f.status="view "+std::to_string(number)+" saved";}
    std::optional<RuntimeTeamKeyCell> latestRadarEventCell()override{++f.radarQueries;
        if(f.lastRadar){emit("radar-last:"+cellText(*f.lastRadar));return f.lastRadar;}
        if(!f.campaignRadar.empty()){emit("radar-campaign-back:"+cellText(f.campaignRadar.back()));return f.campaignRadar.back();}
        emit("radar-empty");return std::nullopt;}
};
class QuickHost final:public RuntimeQuickSelectionKeyHost,private Common {
public:explicit QuickHost(Fixture& f):Common(f){}
    std::int32_t binding(const std::string& a,std::int32_t b)override{return readBinding(a,b);}
    std::set<int> onScreenObjectIds()override{++f.screenQueries;
        const auto owned=f.screen;emit("screen-owned:"+idsText(std::vector<int>(owned.begin(),owned.end())));
        if(f.clearScreenAfterOwnedRead)f.screen.clear();
        if(f.changeLastAtScreenQuery){f.local.lastSelectionKey=9876;f.local.lastSelectionAtMs=888888;}
        if(f.changeRanksAtScreenQuery){f.local.healthRank=1;f.local.veterancyRank=1;}
        return owned;}
    std::vector<RuntimePlayerSelectionCandidate> mobileCandidates(const std::set<int>& screen)override{
        ++f.candidateQueries;f.receivedScreen=screen;auto result=f.candidates;
        for(auto& candidate:result)candidate.onScreen=screen.contains(candidate.objectId);
        std::ostringstream text;text<<"candidates-owned:";for(const auto& c:result)text<<c.objectId<<','<<c.type<<','<<c.veterancy<<','<<c.combatant<<','<<c.onScreen<<','<<c.health<<','<<c.maximumHealth<<';';emit(text.str());
        if(f.clearCandidatesAfterOwnedRead)f.candidates.clear();return result;}
    void applySelection(const std::vector<int>& ids)override{apply(ids);}
    void clearSelectedBeacon()override{clear();}
};
std::string stateText(const Fixture& f){std::ostringstream out;
    for(const auto& team:f.local.teams)out<<'['<<idsText(team)<<']';
    for(const auto& view:f.local.views){if(view)out<<view->x<<','<<view->y;else out<<"empty";out<<';';}
    out<<f.local.planning<<','<<f.local.selectedWaypoint<<','<<f.local.followTargeting<<','<<f.local.rallyTargeting<<','
        <<f.local.cameraFollow<<','<<f.local.showCoordinates<<','<<f.local.lastSelectionKey<<','<<f.local.lastSelectionAtMs<<','
        <<f.local.veterancyRank<<','<<f.local.healthRank<<'|'<<idsText(f.selection)<<'|'<<idsText(f.applied)<<'|'<<f.beacon<<'|'<<f.status;
    for(const auto& cell:f.centers)out<<"|camera="<<cellText(cell);
    for(const auto& ids:f.centeredSelections)out<<"|center-current="<<idsText(ids);
    out<<"|received-screen="<<idsText(std::vector<int>(f.receivedScreen.begin(),f.receivedScreen.end()))
        <<"|backing-screen="<<idsText(std::vector<int>(f.screen.begin(),f.screen.end()));
    for(const auto& c:f.candidates)out<<"|candidate="<<c.objectId<<','<<c.type<<','<<c.veterancy<<','<<c.combatant<<','<<c.onScreen<<','<<c.health<<','<<c.maximumHealth;
    for(int id:f.eligible)out<<"|eligible="<<id;
    out<<"|counts="<<f.bindingsRead<<','<<f.cycleQueries<<','<<f.screenQueries<<','<<f.candidateQueries<<','<<f.viewportQueries<<','<<f.radarQueries<<','<<f.applyCalls<<','<<f.clearCalls<<','<<f.assignedCalls;
    return out.str();}

// Frozen old consumer decisions. Native-only collection/query expressions are
// adapted at the original points; existing local.quickSelect is never copied.
bool oldCycle(const RuntimeLocalNavigationInput& input,Fixture& state){
    CycleHost host(state);auto& local=state.local;const auto code=input.code;const bool repeated=input.repeated;
    const auto matches=[&](const std::string& action,int fallback){return code==host.binding(action,fallback);};
    const bool previous=matches("PreviousObject",'M');
    if(previous||matches("NextObject",'N')){
        if(repeated)return true;
        std::vector<int> ids=host.eligibleMobileObjectIds();
        if(ids.empty())return true;
        std::sort(ids.begin(),ids.end());
        const int current=state.selection.empty()?(previous?std::numeric_limits<int>::max():0):state.selection.front();
        auto next=previous?std::lower_bound(ids.begin(),ids.end(),current):std::upper_bound(ids.begin(),ids.end(),current);
        if(previous){if(next==ids.begin())next=ids.end();--next;}
        else if(next==ids.end())next=ids.begin();
        host.applySelection({*next});host.clearSelectedBeacon();local.cameraFollow=false;host.centerCurrentSelection();return true;
    }return false;
}
bool oldView(const RuntimeLocalNavigationInput& input,Fixture& state){
    ViewHost host(state);auto& local=state.local;const auto code=input.code;
    const auto matches=[&](const std::string& action,int fallback){return code==host.binding(action,fallback);};
    for(int index=0;index<4;++index){
        const std::string suffix=std::to_string(index+1);
        if(matches("SetView"+suffix,(0x70+index)|runtimePlayerControlKey)){
            const auto point=host.currentViewportCenterCell();
            local.views[static_cast<std::size_t>(index)]=RuntimeMultiplayerGameplayCell{point.x,point.y};
            host.viewSaved(index+1);return true;
        }
        if(matches("View"+suffix,0x70+index)){
            const auto& saved=local.views[static_cast<std::size_t>(index)];
            if(saved)host.centerCamera({saved->x,saved->y});return true;
        }
    }return false;
}
bool oldQuick(const RuntimeLocalNavigationInput& input,Fixture& state){
    QuickHost host(state);auto& local=state.local;const auto code=input.code;const bool repeated=input.repeated;const auto now=input.nowMs;
    const auto matches=[&](const std::string& action,int fallback){return code==host.binding(action,fallback);};
    const bool type=matches("TypeSelect",'T');const bool combat=matches("CombatantSelect",'P');
    const bool veteran=matches("VeterancyNav",'Y');const bool health=matches("HealthNav",'U');
    if(type||combat||veteran||health){
        if(repeated)return true;
        const bool wholeMap=local.lastSelectionKey==code&&runtimeUiWithinDoubleSelection(now,local.lastSelectionAtMs);
        const std::set<int> onScreen=host.onScreenObjectIds();
        const std::vector<RuntimePlayerSelectionCandidate> candidates=host.mobileCandidates(onScreen);
        const auto selected=local.quickSelect(type?RuntimePlayerQuickSelection::Type
            :combat?RuntimePlayerQuickSelection::Combatant
            :health?RuntimePlayerQuickSelection::Health:RuntimePlayerQuickSelection::Veterancy,
            state.selection,candidates,wholeMap,false,wholeMap);
        host.applySelection(selected);host.clearSelectedBeacon();
        local.lastSelectionKey=code;local.lastSelectionAtMs=now;return true;
    }return false;
}
bool oldRadar(const RuntimeLocalNavigationInput& input,Fixture& state){
    ViewHost host(state);const auto code=input.code;
    if(code==host.binding("CenterOnRadarEvent",0x20)){
        if(const auto cell=host.latestRadarEventCell())host.centerCamera(*cell);return true;
    }return false;
}
enum class Entry{Cycle,View,Quick,Radar};
bool invokeNew(Entry entry,const RuntimeLocalNavigationInput& input,Fixture& f){
    switch(entry){
    case Entry::Cycle:{CycleHost host(f);return handleRuntimeObjectCycleKey(input,f.local,f.selection,host);}
    case Entry::View:{ViewHost host(f);return handleRuntimeViewNavigationKey(input,f.local,host);}
    case Entry::Quick:{QuickHost host(f);return handleRuntimeQuickSelectionKey(input,f.local,f.selection,host);}
    case Entry::Radar:{ViewHost host(f);return handleRuntimeRadarNavigationKey(input,host);}
    }return false;
}
bool invokeOld(Entry entry,const RuntimeLocalNavigationInput& input,Fixture& f){
    switch(entry){case Entry::Cycle:return oldCycle(input,f);case Entry::View:return oldView(input,f);
    case Entry::Quick:return oldQuick(input,f);case Entry::Radar:return oldRadar(input,f);}return false;
}
std::size_t queryCount(const Fixture& f){return f.cycleQueries+f.screenQueries+f.candidateQueries+f.viewportQueries+f.radarQueries;}
struct Expected{
    std::optional<bool> handled;std::optional<std::size_t> bindings,queries,applies,clears,centers,currentCenters,saved;
    std::optional<int> lastKey,healthRank,veterancyRank;std::optional<RuntimeUiTimeMs> lastAt;
};
Fixture compareCase(const std::string& label,Entry entry,const Fixture& start,
    const RuntimeLocalNavigationInput& input,const Expected& expected={}){
    Fixture actual=start,old=start;const bool result=invokeNew(entry,input,actual);const bool legacy=invokeOld(entry,input,old);
    ++cases;bindingReads+=actual.bindingsRead-start.bindingsRead;sourceQueries+=queryCount(actual)-queryCount(start);traceEvents+=actual.trace.size()-start.trace.size();
    check(result==legacy,label+" handled differential");check(actual.trace==old.trace,label+" full ordered trace");check(stateText(actual)==stateText(old),label+" complete local/host state");
    if(expected.handled)check(result==*expected.handled,label+" expected handled");
    if(expected.bindings)check(actual.bindingsRead-start.bindingsRead==*expected.bindings,label+" exact binding reads");
    if(expected.queries)check(queryCount(actual)-queryCount(start)==*expected.queries,label+" exact owned queries");
    if(expected.applies)check(actual.applyCalls-start.applyCalls==*expected.applies,label+" apply count");
    if(expected.clears)check(actual.clearCalls-start.clearCalls==*expected.clears,label+" beacon count");
    if(expected.centers)check(actual.centers.size()-start.centers.size()==*expected.centers,label+" camera count");
    if(expected.currentCenters)check(actual.centeredSelections.size()-start.centeredSelections.size()==*expected.currentCenters,label+" current-selection center count");
    if(expected.saved)check(actual.assignedCalls-start.assignedCalls==*expected.saved,label+" view-save count");
    if(expected.lastKey)check(actual.local.lastSelectionKey==*expected.lastKey,label+" last code");
    if(expected.lastAt)check(actual.local.lastSelectionAtMs==*expected.lastAt,label+" last time");
    if(expected.healthRank)check(actual.local.healthRank==*expected.healthRank,label+" health rank");
    if(expected.veterancyRank)check(actual.local.veterancyRank==*expected.veterancyRank,label+" veteran rank");
    return actual;
}
Expected basic(bool handled,std::size_t bindings,std::size_t queries=0,std::size_t applies=0,std::size_t centers=0){
    Expected e;e.handled=handled;e.bindings=bindings;e.queries=queries;e.applies=applies;e.centers=centers;return e;
}
Expected quickExpected(bool handled,bool repeated,int code,RuntimeUiTimeMs now){
    auto e=basic(handled,4,handled&&!repeated?2:0,handled&&!repeated?1:0);e.clears=handled&&!repeated?1:0;
    if(handled&&!repeated){e.lastKey=code;e.lastAt=now;}return e;
}

void cycleCases(){
    for(bool previous:{false,true})for(int mask=0;mask<8;++mask)for(bool repeated:{false,true})for(bool rebound:{false,true}){
        auto f=seed();const int code=runtimePlayerKeyCode(rebound?0x7c:(previous?'M':'N'),(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
        if(rebound)f.bindings[previous?"PreviousObject":"NextObject"]=code;
        const bool handled=rebound||mask==0;auto e=basic(handled,handled&&previous?1:2,handled&&!repeated?1:0,handled&&!repeated?1:0);
        e.clears=handled&&!repeated?1:0;e.currentCenters=handled&&!repeated?1:0;
        const auto r=compareCase("cycle-matrix-"+std::to_string(previous)+"-"+std::to_string(mask)+"-"+std::to_string(repeated)+"-"+std::to_string(rebound),Entry::Cycle,f,{code,repeated,50000},e);
        if(handled&&!repeated){check(r.applied==std::vector<int>({previous?7:2}),"cycle independent expected ID");check(!r.local.cameraFollow&&!r.beacon,"cycle clears follow/beacon");}
    }
    const std::vector<std::vector<int>> populations={{},{1},{7,2,1,4},{4,1,1,2},
        {std::numeric_limits<int>::min(),0,2,std::numeric_limits<int>::max()}};
    const std::vector<std::vector<int>> selections={{},{std::numeric_limits<int>::min()},{0},{1},{3},{9},{std::numeric_limits<int>::max()},{404,1}};
    for(const auto& ids:populations)for(const auto& selected:selections)for(bool previous:{false,true})for(bool repeated:{false,true}){
        auto f=seed();f.cycleIds=ids;f.selection=selected;const bool effects=!repeated&&!ids.empty();
        auto e=basic(true,previous?1:2,repeated?0:1,effects?1:0);e.currentCenters=effects?1:0;e.clears=effects?1:0;
        compareCase("cycle-population-front-sentinel-edge",Entry::Cycle,f,{previous?'M':'N',repeated,50000},e);
    }
    struct Choice{std::vector<int> ids,selected;bool previous;int expected;};
    const std::vector<Choice> choices={{{7,2,1,4},{},true,7},{{7,2,1,4},{},false,1},
        {{7,2,1,4},{2},true,1},{{7,2,1,4},{3},true,2},{{7,2,1,4},{3},false,4},
        {{7,2,1,4},{8},false,1},{{7,1,1,2},{1},false,2},
        {{std::numeric_limits<int>::min(),0,2,std::numeric_limits<int>::max()},{},true,2},
        {{std::numeric_limits<int>::min(),0,2,std::numeric_limits<int>::max()},{},false,2}};
    for(const auto& c:choices){auto f=seed();f.cycleIds=c.ids;f.selection=c.selected;
        const auto r=compareCase("cycle-fixed-original-choice",Entry::Cycle,f,{c.previous?'M':'N',false,50000});check(r.applied==std::vector<int>({c.expected}),"cycle fixed sentinel/bounds expected");}
    auto f=seed();f.bindings["PreviousObject"]=0x7d;f.bindings["NextObject"]=0x7d;
    auto e=basic(true,1,1,1);e.currentCenters=1;e.clears=1;
    auto r=compareCase("cycle-collision-previous-short-circuit",Entry::Cycle,f,{0x7d,false,50000},e);check(r.applied==std::vector<int>({7}),"previous collision wins");
    f=seed();f.selectionAfterCycleQuery=std::vector<int>{4};e=basic(true,2,1,1);e.currentCenters=1;e.clears=1;
    r=compareCase("cycle-current-borrow-after-query",Entry::Cycle,f,{'N',false,50000},e);check(r.applied==std::vector<int>({7}),"cycle reads selection after owned ID query");
    f=seed();f.selectionAfterApply=std::vector<int>{6};
    r=compareCase("cycle-center-post-apply-selection",Entry::Cycle,f,{'N',false,50000},e);
    check(r.applied==std::vector<int>({2})&&r.centeredSelections==std::vector<std::vector<int>>({{6}}),"cycle camera uses actual post-apply root selection");
    check(std::find(r.trace.begin(),r.trace.end(),"center-current:6,|last=999/12345|rank=-1/-1|follow=0")!=r.trace.end(),"follow false written between beacon and current center");
    f=seed();f.rejectAtApply={2};r=compareCase("cycle-second-eligibility-refusal",Entry::Cycle,f,{'N',false,50000},e);
    check(r.applied==std::vector<int>({2})&&r.selection.empty()&&r.centeredSelections.back().empty(),"second eligibility may empty cycle selection before camera");
}
void viewCases(){
    for(int index=0;index<4;++index)for(bool save:{false,true})for(int mask=0;mask<8;++mask)for(bool repeated:{false,true})for(bool rebound:{false,true}){
        auto f=seed();f.local.views[static_cast<std::size_t>(index)]=RuntimeMultiplayerGameplayCell{11+index,22+index};
        const int code=runtimePlayerKeyCode(rebound?0x80+index:0x70+index,(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
        if(rebound)f.bindings[std::string(save?"SetView":"View")+std::to_string(index+1)]=code;
        const bool set=rebound?save:mask==2;const bool recall=rebound?!save:mask==0;const bool handled=set||recall;
        auto e=basic(handled,handled?static_cast<std::size_t>(2*index+(set?1:2)):8,set?1:0,0,recall?1:0);e.saved=set?1:0;
        const auto r=compareCase("view-matrix-i"+std::to_string(index)+"-s"+std::to_string(save)+"-m"+std::to_string(mask)+"-r"+std::to_string(repeated)+"-b"+std::to_string(rebound),Entry::View,f,{code,repeated,50000},e);
        if(set){const auto& slot=r.local.views[static_cast<std::size_t>(index)];check(slot&&slot->x==17&&slot->y==23,"saved view is owned exact current query");check(r.status=="view "+std::to_string(index+1)+" saved","saved view exact text including repeat");}
        if(recall)check(r.centers.size()==1&&r.centers[0].x==11+index&&r.centers[0].y==22+index&&!r.local.cameraFollow,"view recall centers and disables follow");
    }
    for(int index=0;index<4;++index)for(bool repeated:{false,true}){
        auto f=seed();f.local.views[static_cast<std::size_t>(index)].reset();
        auto e=basic(true,static_cast<std::size_t>(2*index+2));e.saved=0;
        const auto r=compareCase("empty-view-handled-no-follow-change",Entry::View,f,{0x70+index,repeated,50000},e);
        check(r.local.cameraFollow&&r.beacon,"empty recall has no camera/beacon side effect");
        f.bindings["SetView"+std::to_string(index+1)]=0x7e;f.bindings["View"+std::to_string(index+1)]=0x7e;
        e=basic(true,static_cast<std::size_t>(2*index+1),1);e.saved=1;
        compareCase("view-same-index-save-priority",Entry::View,f,{0x7e,repeated,50000},e);
    }
    auto f=seed();f.bindings["View1"]=0x7d;f.bindings["SetView4"]=0x7d;
    const auto early=compareCase("earlier-recall-before-later-save",Entry::View,f,{0x7d,false,50000},basic(true,2,0,0,1));
    check(early.centers[0].x==11&&early.centers[0].y==22&&early.viewportQueries==0,"earliest view wins without eager center query");
    struct Midpoint{RuntimeInputPixelRect bounds;RuntimeInputPixelPoint expected;};
    const Midpoint edges[]={{{-7,-5,8,10},{0,2}},{{-8,-10,7,5},{0,-2}},{{0,0,1,1},{0,0}},
        {{2147483645,2147483645,2,2},{1073741823,1073741823}},
        {{(-2147483647-1),(-2147483647-1),0,0},{-1073741824,-1073741824}}};
    for(const auto& edge:edges){f=seed();f.viewport=edge.bounds;f.viewportCell={-7,2147483647};
        auto e=basic(true,1,1);e.saved=1;const auto r=compareCase("viewport-old-midpoint-defined-domain",Entry::View,f,{0x70|runtimePlayerControlKey,true,50000},e);
        const auto marker="viewport-midpoint:"+std::to_string(edge.expected.x)+","+std::to_string(edge.expected.y)+"->-7,2147483647";
        check(std::any_of(r.trace.begin(),r.trace.end(),[&](const std::string& text){return text.starts_with(marker);}),"original signed32 midpoint independent expected");
        check(r.local.views[0]&&r.local.views[0]->x==-7&&r.local.views[0]->y==2147483647,"view cell not clamped or normalized");
    }
}
void radarCases(){
    for(int scenario=0;scenario<5;++scenario)for(int mask=0;mask<8;++mask)for(bool repeated:{false,true})for(bool rebound:{false,true}){
        auto f=seed();if(scenario==1||scenario==3)f.lastRadar=RuntimeTeamKeyCell{1,2};
        if(scenario>=2)f.campaignRadar={{3,4},{5,6}};if(scenario==4)f.campaignRadar={{-2147483647-1,2147483647}};
        const int code=runtimePlayerKeyCode(rebound?0x7c:32,(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
        if(rebound)f.bindings["CenterOnRadarEvent"]=code;const bool handled=rebound||mask==0;
        const bool hasCell=scenario!=0;auto e=basic(handled,1,handled?1:0,0,handled&&hasCell?1:0);
        const auto r=compareCase("radar-priority-repeat-rebind-"+std::to_string(scenario),Entry::Radar,f,{code,repeated,50000},e);
        if(handled&&hasCell){const auto expected=f.lastRadar?*f.lastRadar:f.campaignRadar.back();check(r.centers.size()==1&&cellText(r.centers[0])==cellText(expected),"radar last priority or final campaign cell");}
        if(handled&&!hasCell)check(r.local.cameraFollow,"empty radar does not disable follow");
    }
}

void quickCases(){
    const char* const names[]={"TypeSelect","CombatantSelect","VeterancyNav","HealthNav"};const int keys[]={'T','P','Y','U'};
    for(int mode=0;mode<4;++mode)for(int mask=0;mask<8;++mask)for(bool repeated:{false,true})for(bool rebound:{false,true}){
        auto f=seed();const int code=runtimePlayerKeyCode(rebound?0x7c:keys[mode],(mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
        if(rebound)f.bindings[names[mode]]=code;const bool handled=rebound||mask==0;
        const auto r=compareCase("quick-matrix-mode"+std::to_string(mode)+"-m"+std::to_string(mask)+"-r"+std::to_string(repeated)+"-b"+std::to_string(rebound),Entry::Quick,f,{code,repeated,50000},quickExpected(handled,repeated,code,50000));
        if(handled&&!repeated){const auto expected=mode<2?std::vector<int>{1,2,6,7}:std::vector<int>{1,4,7};check(r.applied==expected,"quick independent first-scope expected IDs");
            if(mode==2)check(r.local.veterancyRank==0&&r.local.healthRank==-1,"veterancy increments only veteran rank");
            if(mode==3)check(r.local.healthRank==0&&r.local.veterancyRank==-1,"health increments only health rank");}
    }
    // Every nonempty subset collision, all four queries still run. Use source
    // fixture values that distinguish health from veterancy's priority.
    for(int subset=1;subset<16;++subset)for(bool repeated:{false,true}){
        auto f=seed();f.local.veterancyRank=1;f.local.healthRank=0;f.candidates[3].combatant=true;
        for(int mode=0;mode<4;++mode)if(subset&(1<<mode))f.bindings[names[mode]]=0x7e;
        auto e=quickExpected(true,repeated,0x7e,50000);
        const auto r=compareCase("quick-subset-collision"+std::to_string(subset),Entry::Quick,f,{0x7e,repeated,50000},e);
        check(r.trace.size()>=4&&r.trace[0].starts_with("binding:TypeSelect:")&&r.trace[1].starts_with("binding:CombatantSelect:")
            &&r.trace[2].starts_with("binding:VeterancyNav:")&&r.trace[3].starts_with("binding:HealthNav:"),"quick complete fixed binding order");
        if(!repeated){const auto expected=(subset&1)?std::vector<int>{1,2,6,7}:(subset&2)?std::vector<int>{1,2,4,6,7}:(subset&8)?std::vector<int>{2}:std::vector<int>{6};
            check(r.applied==expected,"quick Type Combat Health Veteran consumption priority");}
    }
    struct TimeCase{RuntimeUiTimeMs now,last;bool twice;};constexpr auto max=std::numeric_limits<RuntimeUiTimeMs>::max();
    const TimeCase times[]={{1499,1000,true},{1500,1000,true},{1501,1000,false},{0,0,true},{99,100,false},
        {0,max,true},{499,max,true},{500,max,false},{max,0,false},{max,max,true},{0,max-499,true},{1,max-499,false}};
    for(int mode=0;mode<4;++mode)for(const auto& t:times)for(int previous=0;previous<3;++previous){
        auto f=seed();const int code=keys[mode]|runtimePlayerShiftKey;f.bindings[names[mode]]=code;
        f.local.lastSelectionKey=previous==0?code:previous==1?(code|runtimePlayerControlKey):'1';
        f.local.lastSelectionAtMs=t.last;f.local.veterancyRank=0;f.local.healthRank=0;
        const auto r=compareCase("quick-time-mode"+std::to_string(mode)+"-now"+std::to_string(t.now)+"-last"+std::to_string(t.last)+"-p"+std::to_string(previous),Entry::Quick,f,{code,false,t.now},quickExpected(true,false,code,t.now));
        const bool whole=previous==0&&t.twice;const auto expected=mode==0?(whole?std::vector<int>{1,2,3,6,7}:std::vector<int>{1,2,6,7})
            :mode==1?(whole?std::vector<int>{1,2,3,5,6,7}:std::vector<int>{1,2,6,7})
            :(whole?std::vector<int>{1,4,7}:std::vector<int>{2});
        check(r.applied==expected,"quick unsigned-time complete-code independent scope/rank");
        check(r.screenQueries==1&&r.candidateQueries==1,"wholeMap still performs both original owned queries");
    }
    for(int mode=2;mode<4;++mode)for(int initial:{-2,-1,0,1,2,3})for(bool whole:{false,true})for(bool repeated:{false,true}){
        auto f=seed();f.local.veterancyRank=initial;f.local.healthRank=initial;
        f.local.lastSelectionKey=whole?keys[mode]:999;f.local.lastSelectionAtMs=1000;
        compareCase("rank-existing-owner-domain",Entry::Quick,f,{keys[mode],repeated,1500},quickExpected(true,repeated,keys[mode],1500));
    }
    auto f=seed();f.bindings["TypeSelect"]='1';f.local.lastSelectionKey='1';f.local.lastSelectionAtMs=1000;
    auto r=compareCase("team-digit-shared-last-then-rebound-quick",Entry::Quick,f,{'1',false,1500},quickExpected(true,false,'1',1500));
    check(r.applied==std::vector<int>({1,2,3,6,7}),"shared digit may enable rebound quick whole-map");
    f=seed();f.bindings["TypeSelect"]='1'|runtimePlayerControlKey;f.local.lastSelectionKey='1';f.local.lastSelectionAtMs=1000;
    r=compareCase("team-digit-is-not-full-modified-quick-code",Entry::Quick,f,{'1'|runtimePlayerControlKey,false,1500},quickExpected(true,false,'1'|runtimePlayerControlKey,1500));
    check(r.applied==std::vector<int>({1,2,6,7}),"shared digit does not ignore modifier bits");
    f=seed();f.replaceSelectionAtBinding=1;f.replacementSelection={4};
    r=compareCase("quick-current-selection-not-entry-snapshot",Entry::Quick,f,{'T',false,50000},quickExpected(true,false,'T',50000));
    check(r.applied==std::vector<int>({4}),"Type observes current borrowed selection after binding");
    f=seed();f.rejectAtApply={1};
    r=compareCase("quick-owned-result-second-eligibility",Entry::Quick,f,{'T',false,50000},quickExpected(true,false,'T',50000));
    check(r.applied==std::vector<int>({1,2,6,7})&&r.selection==std::vector<int>({2,6,7}),"fresh apply may reject but does not change owned owner output");
    f=seed();f.clearScreenAfterOwnedRead=true;f.clearCandidatesAfterOwnedRead=true;
    r=compareCase("quick-query-return-values-owned",Entry::Quick,f,{'T',false,50000},quickExpected(true,false,'T',50000));
    check(r.screen.empty()&&r.candidates.empty()&&r.receivedScreen==std::set<int>({1,2,4,6,7})&&r.applied==std::vector<int>({1,2,6,7}),"backing query storage may change after owned return");
    f=seed();f.local.lastSelectionKey='Y';f.local.lastSelectionAtMs=1000;f.changeLastAtScreenQuery=true;f.changeRanksAtScreenQuery=true;f.changeLastAtClear=true;
    r=compareCase("quick-whole-before-query-rank-after-query-last-final",Entry::Quick,f,{'Y',false,1500},quickExpected(true,false,'Y',1500));
    check(r.applied==std::vector<int>({2,5})&&r.local.veterancyRank==1,"wholeMap decided before query while rank owner reads after query");
    check(std::any_of(r.trace.begin(),r.trace.end(),[](const std::string& t){return t.starts_with("apply-owned:2,5,|last=9876/888888");}),"callback observes query-mutated last fields before final overwrite");
    f=seed();f.candidates[1].type="tank";
    r=compareCase("type-value-case-is-not-renormalized",Entry::Quick,f,{'T',false,50000},quickExpected(true,false,'T',50000));
    check(r.applied==std::vector<int>({1,6,7}),"existing owner type comparison uses provided values exactly");
    f=seed();f.screen.clear();
    auto e=quickExpected(true,false,'U',50000);e.healthRank=2;
    r=compareCase("rank-three-attempts-no-onscreen-match",Entry::Quick,f,{'U',false,50000},e);check(r.applied.empty(),"rank cycle with no onscreen results remains original");
    f=seed();f.candidates.clear();e=quickExpected(true,false,'Y',50000);e.veterancyRank=2;
    r=compareCase("empty-candidates-still-three-rank-attempts",Entry::Quick,f,{'Y',false,50000},e);check(r.selection.empty()&&!r.beacon,"empty quick output still applies clears and writes last");
    f=seed();f.selection.clear();
    r=compareCase("empty-type-source",Entry::Quick,f,{'T',false,50000},quickExpected(true,false,'T',50000));check(r.applied.empty(),"Type without selected types returns empty");
    f=seed();f.candidates={
        {1,"X",0,true,true,std::numeric_limits<int>::min(),std::numeric_limits<int>::min()},
        {2,"X",1,true,true,std::numeric_limits<int>::max(),std::numeric_limits<int>::max()},
        {3,"X",2,true,true,0,0},{4,"X",0,true,true,1,-1}};
    f.screen={1,2,3,4};
    for(int i=0;i<3;++i){const auto next=compareCase("health-int-extrema-existing-owner-cycle",Entry::Quick,f,{'U',false,static_cast<RuntimeUiTimeMs>(50000+i*1000)},quickExpected(true,false,'U',static_cast<RuntimeUiTimeMs>(50000+i*1000)));
        const auto expected=i==0?std::vector<int>{2,4}:i==1?std::vector<int>{1,3}:std::vector<int>{2,4};
        check(next.applied==expected,"health bands use existing int64/max1 owner policy");f=next;}
    f=seed();
    auto first=compareCase("veterancy-first-screen",Entry::Quick,f,{'Y',false,1000},quickExpected(true,false,'Y',1000));
    auto second=compareCase("veterancy-double-keeps-rank-expands",Entry::Quick,first,{'Y',false,1500},quickExpected(true,false,'Y',1500));
    check(first.local.veterancyRank==0&&second.local.veterancyRank==0,"double selection expands while preserving previous rank");
    auto third=compareCase("veterancy-next-cycle-after-501",Entry::Quick,second,{'Y',false,2001},quickExpected(true,false,'Y',2001));
    check(third.local.veterancyRank==1&&third.applied==std::vector<int>({2}),"501ms resumes rank cycle");
}
void unknownCases(){
    for(const int code:{0,-1,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()})for(bool repeated:{false,true}){
        compareCase("cycle-unknown-full-code",Entry::Cycle,seed(),{code,repeated,0},basic(false,2));
        compareCase("view-unknown-full-code",Entry::View,seed(),{code,repeated,0},basic(false,8));
        compareCase("quick-unknown-full-code",Entry::Quick,seed(),{code,repeated,0},quickExpected(false,repeated,code,0));
        compareCase("radar-unknown-full-code",Entry::Radar,seed(),{code,repeated,0},basic(false,1));
        auto f=seed();f.bindings["TypeSelect"]=code;
        compareCase("quick-extreme-rebound-exact-code",Entry::Quick,f,{code,repeated,0},quickExpected(true,repeated,code,0));
    }
}
} // namespace

int main(){
    cycleCases();viewCases();radarCases();quickCases();unknownCases();
    if(failures){std::cerr<<"Local-navigation contract failed: "<<failures<<'/'<<checks<<'\n';return 1;}
    std::cout<<"Local-navigation contract passed: checks="<<checks<<" differentialCases="<<cases
        <<" bindingReads="<<bindingReads<<" ownedQueries="<<sourceQueries<<" traceEvents="<<traceEvents
        <<" entries=4 hosts=3 sameProductionCpp=1 existingQuickOwner=1 fullOrderedTrace=1 fullLocalState=1"
        <<" projectionAlgorithms=not-covered nativeWindowRouting=not-covered physicalModifierMatrix=not-run"
        <<" wholeMpSnapshot=not-run legacyArithmeticOverflow=not-covered\n";
    return 0;
}
