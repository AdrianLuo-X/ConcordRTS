#include "ui/input/RuntimeCursorController.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_WINNT_)
#error Public cursor header introduced Windows declarations
#endif
#include "ui/input/diagnostics/RuntimeCursorOldOracle.inl"
#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
using Point = RuntimeInputPixelPoint;
using Rect = RuntimeInputPixelRect;
using Request = RuntimeCursorRequest;
using Row = RuntimeCursorSuperWeaponRow;
std::string pointText(Point p) { return std::to_string(p.x)+","+std::to_string(p.y); }
std::string rectText(Rect r) { return std::to_string(r.left)+","+std::to_string(r.top)+","+std::to_string(r.right)+","+std::to_string(r.bottom); }
struct State {
    bool present=true,loaded=true,menu=false,finished=false,diplomacy=false;
    Point point{30,40}; Rect client{0,0,1024,768};
    std::array<bool,6> hits{}; int page=0;
    bool placement=false,validPlacement=false,tool=false; int movable=0;
    bool local=false,routerMode=false; std::optional<std::string> router;
    std::string owner="  AmerICANS "; std::vector<Row> rows;
    bool projection=true,object=true,ground=false,validPreview=true; int objectId=42;
    Point cell{7,9}; unsigned lazy=0,previewCalls=0,effects=0;
    std::optional<Request> request;
    bool operator==(const State& b) const {
        if(present!=b.present||loaded!=b.loaded||menu!=b.menu||finished!=b.finished||diplomacy!=b.diplomacy
            ||point.x!=b.point.x||point.y!=b.point.y||rectText(client)!=rectText(b.client)
            ||hits!=b.hits||page!=b.page||placement!=b.placement||validPlacement!=b.validPlacement
            ||tool!=b.tool||movable!=b.movable||local!=b.local||routerMode!=b.routerMode||router!=b.router
            ||owner!=b.owner||rows.size()!=b.rows.size()||projection!=b.projection||object!=b.object
            ||ground!=b.ground||validPreview!=b.validPreview||objectId!=b.objectId||cell.x!=b.cell.x||cell.y!=b.cell.y
            ||lazy!=b.lazy||previewCalls!=b.previewCalls||effects!=b.effects||request!=b.request) return false;
        for(std::size_t n=0;n<rows.size();++n) if(rows[n].owner!=b.rows[n].owner||rows[n].id!=b.rows[n].id
            ||rows[n].targetSelectionActive!=b.rows[n].targetSelectionActive) return false;
        return true;
    }
};
struct Host final : RuntimeCursorHost {
    State s; std::vector<std::string> trace; std::map<std::string,unsigned> counts;
    std::function<void(Host&,const std::string&,unsigned)> after;
    std::optional<std::size_t> throwAt; bool throwing=false;
    void record(const std::string& name,const std::string& args,const std::string& result) {
        const auto count=++counts[name]; trace.push_back(name+"("+args+")->"+result);
        if(after) after(*this,name,count);
        if(!throwing&&throwAt&&trace.size()==*throwAt){throwing=true;throw std::runtime_error("partial-effect-fixture");}
    }
    bool flag(const std::string& name,bool value) { record(name,"",value?"1":"0");return value; }
    Rect sidebarRect(Rect r) override { const auto v=runtimeInputPixelSidebarRect(r,168);record("sidebarRect",rectText(r),rectText(v));return v; }
    Rect mapViewport(Rect r) override { const auto v=runtimeInputPixelMapViewport(r,168);record("mapViewport",rectText(r),rectText(v));return v; }
    bool hit(const std::string& name,Rect r,Point p,bool v){record(name,rectText(r)+";"+pointText(p),v?"1":"0");return v;}
    bool superWeaponAtPoint(Rect r,Point p) override{return hit("superWeaponAtPoint",r,p,s.hits[0]);}
    bool buildButtonAtPoint(Rect r,Point p) override{return hit("buildButtonAtPoint",r,p,s.hits[1]);}
    std::int32_t buildPageButtonAtPoint(Rect r,Point p) override{const int v=s.hits[2]?s.page:0;record("buildPageButtonAtPoint",rectText(r)+";"+pointText(p),std::to_string(v));return v;}
    bool buildTabAtPoint(Rect r,Point p) override{return hit("buildTabAtPoint",r,p,s.hits[3]);}
    bool sidebarToolAtPoint(Rect r,Point p) override{return hit("sidebarToolAtPoint",r,p,s.hits[4]);}
    Rect radarRect(Rect r) override{const Rect v{r.left+5,r.top+7,r.right-5,r.top+120};record("radarRect",rectText(r),rectText(v));return v;}
    bool radarCellAtPoint(Rect r,Point p) override{return hit("radarCellAtPoint",r,p,s.hits[5]);}
    bool selectedPlacementPresent() override{return flag("selectedPlacementPresent",s.placement);}
    bool placementValid() override{return flag("placementValid",s.validPlacement);}
    bool activeSidebarToolPresent() override{return flag("activeSidebarToolPresent",s.tool);}
    std::int32_t selectedMovableCount() override{const int v=s.movable;record("selectedMovableCount","",std::to_string(v));return v;}
    bool acquireWindowState() override{return flag("acquireWindowState",s.present);}
    bool worldLoaded() override{return flag("worldLoaded",s.loaded);}
    bool menuOpen() override{return flag("menuOpen",s.menu);}
    bool finished() override{return flag("finished",s.finished);}
    bool diplomacyOpen() override{return flag("diplomacyOpen",s.diplomacy);}
    Point cursorPoint() override{const auto v=s.point;record("cursorPoint","",pointText(v));return v;}
    Rect clientRect() override{const auto v=s.client;record("clientRect","",rectText(v));return v;}
    void ensureSuperWeaponsInitialized() override{++s.lazy;record("ensureSuperWeaponsInitialized","","void");}
    bool activeLocalSuperWeaponTargeting() override{return flag("activeLocalSuperWeaponTargeting",s.local);}
    void beginRouterSuperWeaponObservation() override{record("beginRouterSuperWeaponObservation","","void");}
    bool routerSuperWeaponPresent() override{return flag("routerSuperWeaponPresent",s.router.has_value());}
    std::string routerSuperWeaponId() override{const auto v=s.router.value();record("routerSuperWeaponId","",v);return v;}
    std::string economyOwner() override{const auto v=s.owner;record("economyOwner","",v);return v;}
    std::vector<Row> superWeaponSnapshots() override{const auto v=s.rows;std::string text;for(const auto& r:v) text+=r.owner+"|"+r.id+"|"+(r.targetSelectionActive?"1":"0")+";";record("superWeaponSnapshots","",text);return v;}
    bool usesIntentRouter() override{return flag("usesIntentRouter",s.routerMode);}
    Point surfaceCell(Rect r,Point p) override{const auto v=s.cell;record("surfaceCell",rectText(r)+";"+pointText(p),pointText(v));return v;}
    void prepareAndPickProjection(Rect r,Point p) override{++s.lazy;record("prepareAndPickProjection",rectText(r)+";"+pointText(p),"void");}
    bool pickedProjectionPresent() override{return flag("pickedProjectionPresent",s.projection);}
    bool pickedObjectPresent() override{return flag("pickedObjectPresent",s.object);}
    bool superWeaponTargetsGroundArea(const std::string& id) override{const bool v=s.ground;record("superWeaponTargetsGroundArea",id,v?"1":"0");return v;}
    std::int32_t pickedObjectId() override{const int v=s.objectId;record("pickedObjectId","",std::to_string(v));return v;}
    bool previewSuperWeaponTarget(const std::string& owner,const std::string& id,Point cell,std::int32_t objectId) override{const bool v=s.validPreview;++s.previewCalls;record("previewSuperWeaponTarget",owner+";"+id+";"+pointText(cell)+";"+std::to_string(objectId),v?"1":"0");return v;}
    void setCursorRequest(Request request) override{++s.effects;s.request=request;record("setCursorRequest",std::to_string(static_cast<unsigned>(request)),"void");}
};
struct Result { Host host; bool handled=false,thrown=false; Request request=Request::Hidden; };
std::size_t checks=0,failures=0,cases=0,traceEvents=0,exceptionCases=0,dynamicCases=0,nestedCases=0;
void need(bool v,const std::string& label){++checks;if(!v){++failures;if(failures<25)std::cerr<<"Cursor contract failure: "<<label<<'\n';}}
Result run(bool old,bool helper,bool client,State state,const std::function<void(Host&,const std::string&,unsigned)>& mutation={},std::optional<std::size_t> throwAt={}) {
    Result r;r.host.s=std::move(state);r.host.after=mutation;r.host.throwAt=throwAt;
    try{if(helper)r.request=old?RuntimeCursorFrozenOld::cursorForPoint(r.host,r.host.s.client,r.host.s.point):runtimeCursorForInputPoint(r.host,r.host.s.client,r.host.s.point);
        else r.handled=old?RuntimeCursorFrozenOld::setCursor(client,r.host):handleRuntimeCursor(client,r.host);}
    catch(const std::runtime_error& e){r.thrown=true;need(std::string(e.what())=="partial-effect-fixture","known fixture exception only");}
    return r;
}
Result compare(const std::string& label,bool helper,bool client,const State& state,const std::function<void(Host&,const std::string&,unsigned)>& mutation={},std::optional<std::size_t> throwAt={}) {
    auto a=run(true,helper,client,state,mutation,throwAt);auto b=run(false,helper,client,state,mutation,throwAt);++cases;traceEvents+=a.host.trace.size()+b.host.trace.size();
    need(a.handled==b.handled,label+" handled");need(a.request==b.request,label+" helper request");need(a.thrown==b.thrown,label+" exception");need(a.host.s==b.host.s,label+" complete owned state");need(a.host.trace==b.host.trace,label+" ordered query/effect args and results");need(a.host.counts==b.host.counts,label+" counts");return b;
}
}
int main(){
    for(unsigned bits=0;bits<64;++bits)for(bool client:{false,true}){State s;s.present=(bits&1)!=0;s.loaded=(bits&2)!=0;s.menu=(bits&4)!=0;s.finished=(bits&8)!=0;s.diplomacy=(bits&16)!=0;s.local=(bits&32)!=0;const auto r=compare("gates",false,client,s);if(!client)need(r.host.trace.empty(),"non-client performs zero queries");}
    const std::array<Point,13> edges{{{-1,-1},{0,0},{855,767},{856,0},{856,768},{1024,0},{1024,768},{1025,769},{10,10},{900,100},{-30,0},{0,769},{856,-1}}};
    for(unsigned mask=0;mask<64;++mask)for(const auto p:edges)for(unsigned policy=0;policy<16;++policy){State s;s.point=p;s.page=(policy&1)?-1:1;for(unsigned n=0;n<6;++n)s.hits[n]=(mask&(1U<<n))!=0;s.placement=(policy&1)!=0;s.validPlacement=(policy&2)!=0;s.tool=(policy&4)!=0;s.movable=(policy&8)?3:0;compare("helper whole policy",true,true,s);}
    for(const Rect rect:std::array<Rect,5>{{{0,0,168,0},{-100,-80,200,160},{20,30,10,5},{-500,-500,-100,-100},{0,0,1024,768}}})for(const auto p:edges){State s;s.client=rect;s.point=p;compare("inclusive signed representable rectangles",true,true,s);compare("native viewport decision",false,true,s);}
    for(unsigned bits=0;bits<64;++bits)for(unsigned choice=0;choice<4;++choice){State s;s.routerMode=(bits&1)!=0;s.router=(bits&2)?std::optional<std::string>{"cHrOnO"}:std::nullopt;s.owner=choice==0?"":choice==1?"  ":choice==2?" Americans ":" Russians ";s.rows={{"wrong","other",true},{"Americans","CHRONO",(bits&4)!=0},{" Russians ","chrono",(bits&8)!=0},{"Americans","CHRONO",true}};s.projection=(bits&16)!=0;s.object=(bits&32)!=0;s.ground=(choice&1)!=0;s.validPreview=(choice&2)!=0;compare("ordered snapshot owner/router/preview",false,true,s);}
    State active;active.rows={{"Americans","Chrono",true}};
    for(unsigned bits=0;bits<16;++bits){auto s=active;s.projection=(bits&1)!=0;s.object=(bits&2)!=0;s.ground=(bits&4)!=0;s.validPreview=(bits&8)!=0;auto r=compare("projection short circuits",false,true,s);need(r.host.counts["pickedObjectId"]==static_cast<unsigned>(s.projection&&s.object&&!s.ground),"instanceId read only original three gates");need(r.host.counts["superWeaponTargetsGroundArea"]==static_cast<unsigned>(s.projection&&s.object),"ground area original short circuit");need(r.host.s.request==(s.validPreview?Request::StockAction:Request::StockForbidden),"preview cursor numbers semantic mapping");}
    const std::array<std::string,10> dynamicNames{{"menuOpen","clientRect","mapViewport","ensureSuperWeaponsInitialized","beginRouterSuperWeaponObservation","economyOwner","usesIntentRouter","surfaceCell","pickedObjectPresent","superWeaponTargetsGroundArea"}};
    for(const auto& name:dynamicNames){auto s=active;s.routerMode=true;s.router="Chrono";const auto change=[name](Host& h,const std::string& actual,unsigned count){if(actual!=name||count!=1)return;h.s.finished=true;h.s.local=name=="ensureSuperWeaponsInitialized";h.s.objectId=731;h.s.cell={13,17};h.s.owner="Russians";h.s.router="CHRONO";};compare("fresh original query position "+name,false,true,s,change);++dynamicCases;}
    for(bool helper:{false,true}){auto s=active;s.point=helper?Point{900,100}:Point{30,40};const auto full=compare("exception source trace",helper,true,s);for(std::size_t n=1;n<=full.host.trace.size();++n){compare("every partial-effect prefix",helper,true,s,{},n);++exceptionCases;}}
    std::array<std::vector<std::string>,2> nestedTraces;std::array<State,2> nestedStates;std::array<std::map<std::string,unsigned>,2> nestedCounts;
    for(bool old:{false,true}){Host h;h.s=active;bool nesting=false;h.after=[old,&nesting](Host& host,const std::string& name,unsigned count){if(nesting||name!="ensureSuperWeaponsInitialized"||count!=1)return;nesting=true;host.s.menu=true;const bool handled=old?RuntimeCursorFrozenOld::setCursor(true,host):handleRuntimeCursor(true,host);need(handled,"nested finite cursor handled");host.s.menu=false;host.s.local=true;nesting=false;};const bool handled=old?RuntimeCursorFrozenOld::setCursor(true,h):handleRuntimeCursor(true,h);need(handled&&h.s.effects==2&&h.s.request==Request::Hidden,"nested call completes before fresh local targeting read");nestedTraces[old?1:0]=h.trace;nestedStates[old?1:0]=h.s;nestedCounts[old?1:0]=h.counts;++nestedCases;}
    need(nestedTraces[0]==nestedTraces[1],"nested full ordered query/effect parameters and returns");need(nestedStates[0]==nestedStates[1],"nested complete owned state");need(nestedCounts[0]==nestedCounts[1],"nested complete query counts");
    // Query callbacks may change the next root value. Both independent policies
    // read the second viewport and repeated per-row router queries at the old positions.
    auto repeat=active;repeat.rows={{"wrong","x",false},{"wrong","y",false},{"Americans","z",true}};repeat.routerMode=true;repeat.router="missing";
    auto repeated=compare("repeat observations",false,true,repeat);need(repeated.host.counts["mapViewport"]==2&&repeated.host.counts["usesIntentRouter"]==3&&repeated.host.counts["routerSuperWeaponPresent"]==3&&repeated.host.counts["routerSuperWeaponId"]==3,"original repeated query counts");
    std::cout<<"cursor controller contract "<<(failures?"failed":"passed")<<"; checks="<<checks<<"; differentialCases="<<cases<<"; traceEvents="<<traceEvents<<"; dynamicCases="<<dynamicCases<<"; nestedCases="<<nestedCases<<"; exceptionCases="<<exceptionCases<<"; fullHelper=1; fullSetCursor=1; independentOldLiteral=1; windowsHeader=absent; failures="<<failures<<'\n';
    return failures?1:0;
}
