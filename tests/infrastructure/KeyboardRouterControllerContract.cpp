#include "ui/input/RuntimeKeyboardRouterController.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_MINWINDEF_)
#error RuntimeKeyboardRouterController.h must remain platform independent
#endif
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_same_v<decltype(RuntimeForceAttackInput{}.raw),std::uint64_t>);
static_assert(std::is_same_v<decltype(RuntimeKeyboardRouterInput{}.code),std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeKeyboardRouterInput{}.nowMs),RuntimeUiTimeMs>);
static_assert(!noexcept(handleRuntimeForceAttackHeld(std::declval<const RuntimeForceAttackInput&>(),std::declval<RuntimeForceAttackHeldHost&>())));
static_assert(!noexcept(handleRuntimeKeyboardRouterKey(std::declval<const RuntimeKeyboardRouterInput&>(),std::declval<RuntimeKeyboardRouterHost&>())));
static_assert(std::numeric_limits<int>::digits==31);

namespace {
using FInput=RuntimeForceAttackInput;using RInput=RuntimeKeyboardRouterInput;using FEvent=RuntimeForceAttackEvent;
using WPARAM=std::uint64_t;using UINT=std::uint32_t;
constexpr UINT WM_KEYDOWN=0x100,WM_SYSKEYDOWN=0x104,WM_KEYUP=0x101,WM_SYSKEYUP=0x105;
constexpr UINT WM_KILLFOCUS=8,WM_ACTIVATEAPP=0x1C,WM_MOUSEMOVE=0x200,WM_LBUTTONDOWN=0x201,WM_LBUTTONUP=0x202;
constexpr WPARAM VK_CONTROL=0x11,VK_LCONTROL=0xA2,VK_RCONTROL=0xA3,MK_CONTROL=8;
constexpr int VK_NUMPAD0=0x60,VK_NUMPAD9=0x69;
constexpr bool FALSE=false;constexpr unsigned RDW_INVALIDATE=1,RDW_NOERASE=32;
struct RECT {int left=1,top=2,right=901,bottom=702;bool operator==(const RECT&)const=default;};
enum class Kind {Force,Restore,Router,PointerPress};
enum class Op:std::size_t {
    Enter,Return,Caught,Held,Control,SetHeld,Repaint,Redraw,Focus,Capture,
    Menu,Finished,Observer,Reference,Prepare,Cycle,Team,View,Quick,Planning,Radar,Limit
};
constexpr std::array<Op,7> childOps{Op::Reference,Op::Cycle,Op::Team,Op::View,Op::Quick,Op::Planning,Op::Radar};
struct Event {
    Op op;std::int64_t value;std::string detail;std::uint64_t raw,stamp;bool repeated;
    bool operator==(const Event&)const=default;
};
struct Frame {
    RInput input;RECT client,viewport;std::vector<int> selection;bool usesViewport=false;unsigned epoch=0;
    bool operator==(const Frame& b)const{return input.code==b.input.code&&input.repeated==b.input.repeated&&input.nowMs==b.input.nowMs
        &&client==b.client&&viewport==b.viewport&&selection==b.selection&&usesViewport==b.usesViewport&&epoch==b.epoch;}
};
struct State {
    bool held=false,control=false,menu=false,finished=false,observer=false;
    bool dragActive=true,dragMoved=true,repaint=false;
    unsigned redraws=0,heldStores=0,rejects=0,prepares=0,epoch=0;
    std::vector<int> selection{9,2,9};
    RECT viewport{31,42,630,590};
    std::array<std::optional<std::int32_t>,7> bindings{};
    std::array<unsigned,7> calls{};
    std::array<std::optional<Frame>,7> frames{};
    std::vector<int> accepted;
    bool operator==(const State&)const=default;
};
struct Config {
    Kind kind=Kind::Router;FInput force{};RInput route{};
    bool rootPresent=true,repeated=false;RECT client{};
};
struct SyntheticFailure {Op op;};
std::string rectText(const RECT& r){return std::to_string(r.left)+","+std::to_string(r.top)+","+std::to_string(r.right)+","+std::to_string(r.bottom);}
std::string idsText(const std::vector<int>& ids){std::string s;for(int id:ids)s+=std::to_string(id)+",";return s;}
struct Model {
    State state;std::vector<State> retired;std::vector<Event> trace;Config active{};
    bool modern=false,armed=true;
    std::array<unsigned,static_cast<std::size_t>(Op::Limit)> counts{};
    std::function<void(Model&,Op)> hook;Op throwAt=Op::Limit;unsigned throwOccurrence=1;
    void emit(Op op,std::int64_t value=0,std::string detail={},std::optional<RuntimeUiTimeMs> stamp=std::nullopt){
        const bool router=active.kind==Kind::Router;
        trace.push_back({op,value,std::move(detail),router?static_cast<std::uint32_t>(active.route.code):active.force.raw,
            stamp.value_or(router?active.route.nowMs:0),router?active.route.repeated:active.repeated});
        const auto n=++counts[static_cast<std::size_t>(op)];if(hook)hook(*this,op);
        if(op==throwAt&&n==throwOccurrence)throw SyntheticFailure{op};
    }
    bool held(){const bool v=state.held;emit(Op::Held,v);return v;}
    bool control(){const bool v=state.control;emit(Op::Control,v);return v;}
    void held(bool v){state.held=v;++state.heldStores;emit(Op::SetHeld,v);}
    void repaint(){state.repaint=true;emit(Op::Repaint,1);}
    void redraw(){++state.redraws;emit(Op::Redraw,RDW_INVALIDATE|RDW_NOERASE);}
    bool menu(){const bool v=state.menu;emit(Op::Menu,v);return v;}
    bool finished(){const bool v=state.finished;emit(Op::Finished,v);return v;}
    bool reject(){
        const bool rejected=state.observer;++state.rejects;
        if(rejected){state.selection.clear();state.dragMoved=state.dragActive=false;state.repaint=true;}
        emit(Op::Observer,rejected,idsText(state.selection));return rejected;
    }
    RECT prepare(const RECT& client){const RECT result=state.viewport;++state.prepares;emit(Op::Prepare,0,rectText(client)+";"+rectText(result));return result;}
    bool consume(std::size_t child,RInput input,const RECT& client={},const RECT& viewport={},bool usesViewport=false){
        const bool handled=state.bindings[child]&&*state.bindings[child]==input.code;
        ++state.calls[child];state.frames[child]=Frame{input,client,viewport,state.selection,usesViewport,state.epoch};
        if(handled)state.accepted.push_back(static_cast<int>(child));
        emit(childOps[child],input.code,std::to_string(handled)+";"+rectText(client)+";"+(usesViewport?rectText(viewport):"unused")+";"+idsText(state.selection),input.nowMs);
        return handled;
    }
    void swapRoot(){const auto epoch=state.epoch+1;retired.push_back(std::move(state));state=State{};state.epoch=epoch;}
};
struct ForceHost final:RuntimeForceAttackHeldHost {
    Model& m;explicit ForceHost(Model& model):m(model){}
    bool held()override{return m.held();}bool controlDown()override{return m.control();}
    void setHeld(bool v)override{m.held(v);}void requestRepaint()override{m.repaint();}void redraw()override{m.redraw();}
};
struct RouterHost final:RuntimeKeyboardRouterHost {
    Model& m;RECT client,viewport{};
    RouterHost(Model& model,const RECT& value):m(model),client(value){}
    bool menuOpen()override{return m.menu();}bool finished()override{return m.finished();}
    void rejectObserverSelection()override{(void)m.reject();}
    bool handleReferenceKey(std::int32_t code,bool repeated)override{return m.consume(0,{code,repeated,0},client);}
    void prepareNavigation()override{viewport=m.prepare(client);}
    bool handleObjectCycleKey(const RInput& in)override{return m.consume(1,in,client,viewport,true);}
    bool handleTeamKey(const RInput& in)override{return m.consume(2,in,client);}
    bool handleViewNavigationKey(const RInput& in)override{return m.consume(3,in,client,viewport,true);}
    bool handleQuickSelectionKey(const RInput& in)override{return m.consume(4,in,client,viewport,true);}
    bool handlePlanningFactoryKey(const RInput& in)override{return m.consume(5,{in.code,in.repeated,0});}
    bool handleRadarNavigationKey(const RInput& in)override{return m.consume(6,in,client,viewport,true);}
};
// These proxies expose original scalar reads/stores through the stable model root.
struct ReadBool {Model& m;Op op;operator bool()const{return op==Op::Menu?m.menu():op==Op::Finished?m.finished():m.held();}};
struct HeldField:ReadBool {
    explicit HeldField(Model& model):ReadBool{model,Op::Held}{}
    bool operator=(bool v){m.held(v);return v;}
};
struct RepaintField {Model& m;bool operator=(bool v){if(!v)throw std::logic_error("unexpected original repaint store");m.repaint();return v;}};
struct RootBorrow {Model& m;};
struct RuntimeWindowState {
    Model& model;HeldField forceAttackHeld;RepaintField repaintRequested;
    ReadBool runtimeMenuOpen,skirmishMatchFinished;RootBorrow playerLocalCommands,selectedObjectIds;
    explicit RuntimeWindowState(Model& m):model(m),forceAttackHeld(m),repaintRequested{m},
        runtimeMenuOpen{m,Op::Menu},skirmishMatchFinished{m,Op::Finished},playerLocalCommands{m},selectedObjectIds{m}{}
};
struct ChildInput {std::int32_t code;bool repeated;RuntimeUiTimeMs nowMs;};
struct PlanningInput {std::int32_t code;bool repeated;};
struct RuntimeObjectCycleUiHost {Model& m;RECT client,viewport;RuntimeObjectCycleUiHost(RuntimeWindowState& s,const RECT& c,const RECT& v):m(s.model),client(c),viewport(v){}};
struct RuntimeTeamKeyUiHost {Model& m;RECT client;RuntimeTeamKeyUiHost(RuntimeWindowState& s,const RECT& c):m(s.model),client(c){}};
struct RuntimeViewNavigationUiHost {Model& m;RECT client,viewport;RuntimeViewNavigationUiHost(RuntimeWindowState& s,const RECT& c,const RECT& v):m(s.model),client(c),viewport(v){}};
struct RuntimeQuickSelectionUiHost {Model& m;RECT client,viewport;RuntimeQuickSelectionUiHost(RuntimeWindowState& s,const RECT& c,const RECT& v):m(s.model),client(c),viewport(v){}};
struct RuntimePlanningFactoryKeyUiHost {Model& m;explicit RuntimePlanningFactoryKeyUiHost(RuntimeWindowState& s):m(s.model){}};
struct LegacyOracle {
    Model& m;explicit LegacyOracle(Model& model):m(model){}
    int GetKeyState(WPARAM key){if(key!=VK_CONTROL)throw std::logic_error("unexpected original key sample");return m.control()?0x8000:0;}
    void RedrawWindow(int,void*,void*,unsigned flags){if(flags!=(RDW_INVALIDATE|RDW_NOERASE))throw std::logic_error("original redraw flags");m.redraw();}
    bool rejectRuntimeObserverSelection(RuntimeWindowState&){return m.reject();}
    bool runtimePlayerLocalSelectable(RuntimeWindowState&,int){return false;} // Original unused closure remains unused.
    bool handleRuntimePlayerReferenceKey(RuntimeWindowState&,const RECT& client,int code,bool repeated){return m.consume(0,{code,repeated,0},client);}
    RECT runtimeMapViewport(const RECT& client){return m.prepare(client);}
    bool handleRuntimeObjectCycleKey(ChildInput in,RootBorrow&,RootBorrow&,RuntimeObjectCycleUiHost& h){return m.consume(1,{in.code,in.repeated,in.nowMs},h.client,h.viewport,true);}
    bool handleRuntimeTeamKey(ChildInput in,RootBorrow&,RootBorrow&,RuntimeTeamKeyUiHost& h){return m.consume(2,{in.code,in.repeated,in.nowMs},h.client);}
    bool handleRuntimeViewNavigationKey(ChildInput in,RootBorrow&,RuntimeViewNavigationUiHost& h){return m.consume(3,{in.code,in.repeated,in.nowMs},h.client,h.viewport,true);}
    bool handleRuntimeQuickSelectionKey(ChildInput in,RootBorrow&,RootBorrow&,RuntimeQuickSelectionUiHost& h){return m.consume(4,{in.code,in.repeated,in.nowMs},h.client,h.viewport,true);}
    bool handleRuntimePlanningFactoryKey(PlanningInput in,RootBorrow&,RootBorrow&,RuntimePlanningFactoryKeyUiHost&){return m.consume(5,{in.code,in.repeated,0});}
    bool handleRuntimeRadarNavigationKey(ChildInput in,RuntimeViewNavigationUiHost& h){return m.consume(6,{in.code,in.repeated,in.nowMs},h.client,h.viewport,true);}
    void force(RuntimeWindowState* campaignState,UINT message,WPARAM wParam,int window) {
    if (campaignState != nullptr) {
        bool forceAttackHeld = campaignState->forceAttackHeld;
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            && (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)) {
            forceAttackHeld = true;
        } else if ((message == WM_KEYUP || message == WM_SYSKEYUP)
            && (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)) {
            forceAttackHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        } else if (message == WM_KILLFOCUS
            || (message == WM_ACTIVATEAPP && wParam == FALSE)) {
            forceAttackHeld = false;
        } else if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN
            || message == WM_LBUTTONUP) {
            forceAttackHeld = (wParam & MK_CONTROL) != 0;
        }
        if (forceAttackHeld != campaignState->forceAttackHeld) {
            campaignState->forceAttackHeld = forceAttackHeld;
            campaignState->repaintRequested = true;
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        }
    }
    }
    void restore(RuntimeWindowState* state,WPARAM wParam) {
        // Focus/capture can deliver nested messages. Restore this click's
        // modifiers and position before exposing the pressed cursor.
        state->forceAttackHeld = (wParam & MK_CONTROL) != 0;
    }
// Only the frozen unused local lambda is covered by this diagnostic scope.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
bool handleRuntimePlayerCommandCode(RuntimeWindowState& state, const RECT& client,
    int code, bool repeated, std::uint64_t now) {
    if (state.runtimeMenuOpen || state.skirmishMatchFinished) return false;
    rejectRuntimeObserverSelection(state);
    if (handleRuntimePlayerReferenceKey(state, client, code, repeated)) return true;
    // With NumLock on the keypad is a second set of group-selection keys.
    const int key = code & 0xff;
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
        code = (code & ~0xff) | ('0' + key - VK_NUMPAD0);
    const auto selectable = [&](int id) { return runtimePlayerLocalSelectable(state, id); };
    const RECT viewport = runtimeMapViewport(client);
    auto& local = state.playerLocalCommands;
    RuntimeObjectCycleUiHost cycleHost(state, client, viewport);
    if (handleRuntimeObjectCycleKey({code, repeated, now}, local, state.selectedObjectIds, cycleHost))
        return true;
    RuntimeTeamKeyUiHost teamHost(state, client);
    if (handleRuntimeTeamKey({code, repeated, now}, local, state.selectedObjectIds, teamHost))
        return true;
    RuntimeViewNavigationUiHost viewHost(state, client, viewport);
    if (handleRuntimeViewNavigationKey({code, repeated, now}, local, viewHost))
        return true;
    RuntimeQuickSelectionUiHost quickHost(state, client, viewport);
    if (handleRuntimeQuickSelectionKey({code, repeated, now}, local, state.selectedObjectIds, quickHost))
        return true;
    RuntimePlanningFactoryKeyUiHost planningFactoryHost(state);
    if (handleRuntimePlanningFactoryKey({code, repeated}, local, state.selectedObjectIds, planningFactoryHost))
        return true;
    if (handleRuntimeRadarNavigationKey({code, repeated, now}, viewHost))
        return true;
    return false;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
};
struct Scope {
    Model& m;Config old;Scope(Model& model,const Config& c):m(model),old(model.active){m.active=c;}
    ~Scope(){m.active=old;}
};
UINT nativeMessage(FEvent e){
    switch(e){
    case FEvent::KeyDown:return WM_KEYDOWN;case FEvent::SystemKeyDown:return WM_SYSKEYDOWN;
    case FEvent::KeyUp:return WM_KEYUP;case FEvent::SystemKeyUp:return WM_SYSKEYUP;
    case FEvent::KillFocus:return WM_KILLFOCUS;case FEvent::ActivateApplication:return WM_ACTIVATEAPP;
    case FEvent::MouseMove:return WM_MOUSEMOVE;case FEvent::LeftButtonDown:return WM_LBUTTONDOWN;
    case FEvent::LeftButtonUp:return WM_LBUTTONUP;default:return 0x7FFF;
    }
}
bool invoke(Model& m,Config& carrier){
    const Config owned=carrier;Scope scope(m,owned);
    m.trace.push_back({Op::Enter,static_cast<int>(owned.kind),rectText(owned.client),
        owned.kind==Kind::Router?static_cast<std::uint32_t>(owned.route.code):owned.force.raw,
        owned.kind==Kind::Router?owned.route.nowMs:0,owned.kind==Kind::Router?owned.route.repeated:owned.repeated});
    LegacyOracle old(m);RuntimeWindowState state(m);bool handled=false;
    if(owned.kind==Kind::Router){
        if(m.modern){RouterHost host(m,owned.client);handled=handleRuntimeKeyboardRouterKey(carrier.route,host);}
        else handled=old.handleRuntimePlayerCommandCode(state,owned.client,owned.route.code,owned.route.repeated,owned.route.nowMs);
    }else if(owned.kind==Kind::Restore||owned.force.event==FEvent::PointerPressRestore){
        if(m.modern){ForceHost host(m);handleRuntimeForceAttackHeld(carrier.force,host);}
        else old.restore(&state,owned.force.raw);
    }else if(owned.kind==Kind::PointerPress){
        FInput press{FEvent::LeftButtonDown,owned.force.raw};
        if(m.modern){ForceHost host(m);handleRuntimeForceAttackHeld(press,host);}
        else old.force(&state,WM_LBUTTONDOWN,owned.force.raw,1);
        m.emit(Op::Focus);m.emit(Op::Capture);
        if(m.modern){ForceHost host(m);handleRuntimeForceAttackHeld({FEvent::PointerPressRestore,owned.force.raw},host);}
        else old.restore(&state,owned.force.raw);
    }else if(m.modern){
        if(owned.rootPresent){ForceHost host(m);handleRuntimeForceAttackHeld(carrier.force,host);}
    }else old.force(owned.rootPresent?&state:nullptr,nativeMessage(owned.force.event),owned.force.raw,1);
    m.trace.push_back({Op::Return,handled,{},owned.kind==Kind::Router?static_cast<std::uint32_t>(owned.route.code):owned.force.raw,
        owned.kind==Kind::Router?owned.route.nowMs:0,owned.kind==Kind::Router?owned.route.repeated:owned.repeated});
    return handled;
}
using Setup=std::function<void(Model&,Config&)>;
struct Run {
    State state;std::vector<State> retired;std::vector<Event> trace;bool returned=false,handled=false;Op exception=Op::Limit;
};
Run run(bool modern,Config config,const State& state,const Setup& setup){
    Model m;m.modern=modern;m.state=state;if(setup)setup(m,config);
    const Config original=config;bool returned=false,handled=false;Op exception=Op::Limit;
    try{handled=invoke(m,config);returned=true;}
    catch(const SyntheticFailure& e){exception=e.op;m.trace.push_back({Op::Caught,static_cast<int>(e.op),{},
        original.kind==Kind::Router?static_cast<std::uint32_t>(original.route.code):original.force.raw,
        original.kind==Kind::Router?original.route.nowMs:0,original.kind==Kind::Router?original.route.repeated:original.repeated});}
    return {m.state,m.retired,m.trace,returned,handled,exception};
}
struct Stats {
    std::size_t checks=0,cases=0,forceCases=0,routerCases=0,restoreCases=0,pointerCases=0;
    std::size_t dynamicCases=0,nestedCases=0,exceptionCases=0,traceRows=0,ownedChecks=0;
    void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
    Run compare(Config c,const State& state=State{},const Setup& setup={}){
        const auto old=run(false,c,state,setup),actual=run(true,c,state,setup);++cases;traceRows+=actual.trace.size();
        if(c.kind==Kind::Router)++routerCases;else if(c.kind==Kind::Restore||c.force.event==FEvent::PointerPressRestore)++restoreCases;
        else if(c.kind==Kind::PointerPress)++pointerCases;else ++forceCases;
        check(old.state==actual.state,"complete root state differs");check(old.retired==actual.retired,"retired owned child values differ");
        check(old.trace==actual.trace,"six-field ordered query/effect trace differs");
        check(old.returned==actual.returned&&old.handled==actual.handled&&old.exception==actual.exception,"return/exception differs");
        return actual;
    }
};
std::size_t count(const Run& r,Op op){return static_cast<std::size_t>(std::count_if(r.trace.begin(),r.trace.end(),[op](const Event& e){return e.op==op;}));}
Setup once(Op op,std::function<void(Model&)> effect,unsigned occurrence=1){
    return [op,effect=std::move(effect),occurrence](Model& m,Config&){m.hook=[op,effect,occurrence](Model& model,Op got){
        if(model.armed&&got==op&&model.counts[static_cast<std::size_t>(got)]==occurrence){model.armed=false;effect(model);}};};
}
Config forceConfig(FEvent event,std::uint64_t raw){Config c;c.kind=event==FEvent::PointerPressRestore?Kind::Restore:Kind::Force;c.force={event,raw};return c;}
Config routeConfig(std::int32_t code,bool repeated=false,RuntimeUiTimeMs now=0xFFFFFFFFFFFFFFFEULL){Config c;c.kind=Kind::Router;c.route={code,repeated,now};return c;}
void forceCases(Stats& s){
    const std::array events{FEvent::Other,FEvent::KeyDown,FEvent::SystemKeyDown,FEvent::KeyUp,FEvent::SystemKeyUp,
        FEvent::KillFocus,FEvent::ActivateApplication,FEvent::MouseMove,FEvent::LeftButtonDown,FEvent::LeftButtonUp,
        FEvent::PointerPressRestore,static_cast<FEvent>(255)};
    const std::array<std::uint64_t,14> values{0,8,0x11,0xA2,0xA3,0x12,1,9,
        std::numeric_limits<std::uint64_t>::max(),(std::uint64_t{1}<<32)|0x11,std::uint64_t{1}<<32,
        0x10008,0x200A2,(std::uint64_t{1}<<63)|8};
    for(const auto event:events)for(const auto raw:values)for(bool held:{false,true})for(bool physicalControl:{false,true}){
        State state;state.held=held;state.control=physicalControl;
        Config c=forceConfig(event,raw);c.repeated=(raw&1)!=0;const auto r=s.compare(c,state);
        const bool controlKey=raw==0x11||raw==0xA2||raw==0xA3;
        const bool keyUp=event==FEvent::KeyUp||event==FEvent::SystemKeyUp;
        if(event==FEvent::PointerPressRestore){
            s.check(count(r,Op::Held)==0&&count(r,Op::Control)==0&&count(r,Op::SetHeld)==1
                &&count(r,Op::Repaint)==0&&count(r,Op::Redraw)==0,"post-capture restore is one unconditional store only");
            s.check(r.state.held==((raw&8)!=0),"restore retains this event pointer control bit");
        }else{
            s.check(count(r,Op::Held)==2,"ordinary event retains initial and fresh comparison reads");
            s.check(count(r,Op::Control)==(keyUp&&controlKey?1U:0U),"only exact full Control key-up samples physical Control");
            s.check(count(r,Op::SetHeld)==count(r,Op::Repaint)&&count(r,Op::SetHeld)==count(r,Op::Redraw),"only changed held value repaints/redraws");
            if(event==FEvent::KillFocus||(event==FEvent::ActivateApplication&&raw==0))s.check(!r.state.held,"focus loss/inactive application clears held");
            if(event==FEvent::MouseMove||event==FEvent::LeftButtonDown||event==FEvent::LeftButtonUp)s.check(r.state.held==((raw&8)!=0),"pointer event uses MK_CONTROL snapshot");
        }
    }
    Config missing=forceConfig(FEvent::KeyDown,0x11);missing.rootPresent=false;
    const auto r=s.compare(missing);s.check(count(r,Op::Held)==0&&count(r,Op::SetHeld)==0,"retained outer null-root adapter guard");
}
struct CodePair {std::int32_t original,normalized;};
void checkRoute(Stats& s,CodePair code,bool repeated,int stop,RuntimeUiTimeMs now){
    State state;state.observer=(stop&1)!=0;state.held=true;
    for(int i=stop;i<7;++i)state.bindings[static_cast<std::size_t>(i)]=i==0?code.original:code.normalized;
    const auto r=s.compare(routeConfig(code.original,repeated,now),state);
    s.check(r.handled==(stop<7),"matching component consumes; complete miss falls through");
    s.check(r.state.accepted==(stop<7?std::vector<int>{stop}:std::vector<int>{}),"first matching rebound component wins collision");
    s.check(count(r,Op::Prepare)==(stop==0?0U:1U),"viewport is prepared once only after Reference misses");
    s.check(count(r,Op::Control)==0&&count(r,Op::Held)==0,"router neither samples physical modifiers nor handles force state");
    if(state.observer)s.check(r.state.selection.empty()&&!r.state.dragActive&&!r.state.dragMoved,"observer clears selection then allowed local routing continues");
    const int final=stop<7?stop:6;
    for(int i=0;i<7;++i){const auto slot=static_cast<std::size_t>(i);s.check(r.state.calls[slot]==(i<=final?1U:0U),"all seven exact priority stop points");
        if(i<=final){const auto& f=r.state.frames[slot];s.check(f.has_value()&&f->input.code==(i==0?code.original:code.normalized)
            &&f->input.repeated==repeated&&f->input.nowMs==(i==0||i==5?0:now),"Reference original code, normalized later code, repeat and time passthrough");}}
}
void routerCases(Stats& s){
    // Explicit independent key pairs; there is no call to the new normalizer.
    const std::array<CodePair,18> keys{{{0x60,'0'},{0x61,'1'},{0x62,'2'},{0x63,'3'},{0x64,'4'},
        {0x65,'5'},{0x66,'6'},{0x67,'7'},{0x68,'8'},{0x69,'9'},
        {'0','0'},{'9','9'},{'T','T'},{'?','?'},{0,0},{0x5F,0x5F},{0x6A,0x6A},{0xFF,0xFF}}};
    for(std::int32_t mods=0;mods<8;++mods)for(const auto key:keys)for(bool repeated:{false,true})for(int stop=0;stop<8;++stop){
        const auto mask=((mods&1)?0x100:0)|((mods&2)?0x200:0)|((mods&4)?0x400:0);
        checkRoute(s,{key.original|mask,key.normalized|mask},repeated,stop,mods==7?std::numeric_limits<RuntimeUiTimeMs>::max():123456);
    }
    const auto lo=std::numeric_limits<std::int32_t>::min(),hi=std::numeric_limits<std::int32_t>::max();
    for(const auto key:std::array<CodePair,4>{{{lo,lo},{hi,hi},{lo|0x65,lo|'5'},{lo|0x69,lo|'9'}}})
        for(bool repeated:{false,true})for(int stop=0;stop<8;++stop)checkRoute(s,key,repeated,stop,0);
    for(bool menu:{false,true})for(bool finished:{false,true})for(bool observer:{false,true})for(bool repeated:{false,true}){
        State state;state.menu=menu;state.finished=finished;state.observer=observer;const auto r=s.compare(routeConfig(0x265,repeated),state);
        s.check(!r.handled,"guard/complete miss returns false");
        s.check(count(r,Op::Finished)==(menu?0U:1U),"menu guard short-circuits finished query");
        s.check(count(r,Op::Observer)==(!menu&&!finished?1U:0U),"outer menu/result guards precede observer and all components");
    }
}
void dynamicCases(Stats& s){
    const auto compare=[&](Config c,const State& state,const Setup& setup){++s.dynamicCases;return s.compare(c,state,setup);};
    const auto second=compare(forceConfig(FEvent::Other,0),State{},once(Op::Held,[](Model& m){m.state.held=true;}));
    s.check(!second.state.held&&count(second,Op::SetHeld)==1,"fresh held comparison after initial query callback");
    State release;release.held=true;release.control=false;
    const auto sampled=compare(forceConfig(FEvent::KeyUp,0x11),release,once(Op::Control,[](Model& m){m.state.held=false;}));
    s.check(count(sampled,Op::SetHeld)==0&&count(sampled,Op::Held)==2,"Control query callback changes final comparison field");
    const auto late=compare(forceConfig(FEvent::KeyUp,0xA2),State{},once(Op::Held,[](Model& m){m.state.control=true;}));
    s.check(late.state.held&&count(late,Op::Control)==1,"physical Control is sampled late on the original key-up branch");
    const auto forceOwned=compare(forceConfig(FEvent::KeyDown,0x11),State{},[](Model& m,Config& carrier){
        m.hook=[&carrier](Model& model,Op op){if(model.armed&&op==Op::Held){model.armed=false;carrier.force={FEvent::Other,(std::uint64_t{1}<<32)|0x11};}};});
    s.check(forceOwned.state.held,"force entry owns full raw/event before callbacks");++s.ownedChecks;
    const auto finished=compare(routeConfig('T'),State{},once(Op::Menu,[](Model& m){m.state.finished=true;}));
    s.check(count(finished,Op::Finished)==1&&count(finished,Op::Observer)==0,"fresh second guard sees callback state");
    State open;open.menu=true;
    const auto cached=compare(routeConfig('T'),open,once(Op::Menu,[](Model& m){m.state.menu=false;}));
    s.check(count(cached,Op::Finished)==0&&count(cached,Op::Reference)==0,"original returned menu value is owned");
    State observer;observer.observer=true;
    const auto swap=compare(routeConfig(0x65),observer,once(Op::Observer,[](Model& m){m.swapRoot();m.state.bindings[0]=0x65;}));
    s.check(swap.handled&&swap.retired.size()==1&&swap.state.accepted==std::vector<int>{0},"observer callback reborrows fresh root for Reference");
    const auto continueLocal=compare(routeConfig(0x65),State{},once(Op::Reference,[](Model& m){m.state.menu=m.state.finished=true;m.state.bindings[2]='5';}));
    s.check(continueLocal.handled&&count(continueLocal,Op::Prepare)==1&&continueLocal.state.accepted==std::vector<int>{2},"guards are not repeated after Reference callback");
    State cycle;cycle.bindings[1]='5';const RECT oldViewport=cycle.viewport;
    const auto viewport=compare(routeConfig(0x65),cycle,once(Op::Prepare,[](Model& m){m.state.viewport={-10,-20,30,40};}));
    s.check(viewport.state.frames[1]->viewport==oldViewport,"prepared viewport value remains owned across callback");++s.ownedChecks;
    const auto team=compare(routeConfig(0x65),State{},once(Op::Cycle,[](Model& m){m.state.bindings[2]='5';}));
    s.check(team.state.accepted==std::vector<int>{2},"next component reads fresh rebound binding");
    State quick;quick.bindings[4]='5';
    const auto carried=compare(routeConfig(0x65,false,std::numeric_limits<RuntimeUiTimeMs>::max()),quick,[](Model& m,Config& carrier){
        m.hook=[&carrier](Model& model,Op op){if(model.armed&&op==Op::Menu){model.armed=false;carrier.route={0x69,true,0};}};});
    s.check(carried.state.frames[4]->input.code=='5'&&!carried.state.frames[4]->input.repeated
        &&carried.state.frames[4]->input.nowMs==std::numeric_limits<RuntimeUiTimeMs>::max(),"router owns code/repeat/full time before callbacks");++s.ownedChecks;
    State selection;selection.bindings[1]='5';
    const auto freshSelection=compare(routeConfig(0x65),selection,once(Op::Reference,[](Model& m){m.state.selection={123,456};}));
    s.check(freshSelection.state.frames[1]->selection==std::vector<int>({123,456}),"child assembly borrows current selection after Reference");++s.ownedChecks;
}
void pointerRestoration(Stats& s){
    for(bool initially:{false,true})for(bool control:{false,true})for(int callbacks=0;callbacks<4;++callbacks){
        State state;state.held=initially;
        Config c;c.kind=Kind::PointerPress;c.force={FEvent::LeftButtonDown,control?WPARAM{9}:WPARAM{1}};
        const auto r=s.compare(c,state,[callbacks](Model& m,Config&){
            m.hook=[callbacks](Model& model,Op op){if((op==Op::Focus&&(callbacks==1||callbacks==3))||(op==Op::Capture&&(callbacks==2||callbacks==3))){
                Config nested=forceConfig(FEvent::KillFocus,0);(void)invoke(model,nested);}};});
        s.check(r.state.held==control,"post-focus/capture restore uses original event snapshot after nested messages");
        s.check(count(r,Op::Focus)==1&&count(r,Op::Capture)==1,"restore remains after both synchronous callback points");
        const auto last=std::find_if(r.trace.rbegin(),r.trace.rend(),[](const Event& e){return e.op!=Op::Return;});
        s.check(last!=r.trace.rend()&&last->op==Op::SetHeld&&last->value==control,"last restore is unconditional store without a new read/repaint");
    }
}
void nestedAndExceptions(Stats& s){
    for(const auto op:std::array{Op::Held,Op::SetHeld,Op::Repaint}){
        ++s.nestedCases;
        const auto r=s.compare(forceConfig(FEvent::KeyDown,0x11),State{},once(op,[](Model& m){
            m.swapRoot();Config nested=forceConfig(FEvent::PointerPressRestore,0);(void)invoke(m,nested);}));
        s.check(r.retired.size()==1&&count(r,Op::Enter)==2,"synchronous force reentry uses stable root");++s.ownedChecks;
    }
    for(const auto op:std::array{Op::Observer,Op::Reference,Op::Prepare,Op::Cycle,Op::View,Op::Quick,Op::Planning,Op::Radar}){
        ++s.nestedCases;
        const auto r=s.compare(routeConfig(0x65),State{},once(op,[](Model& m){
            m.swapRoot();m.state.bindings[0]='A';Config nested=routeConfig('A',true,7);(void)invoke(m,nested);}));
        s.check(r.retired.size()==1&&count(r,Op::Enter)==2,"synchronous component reentry owns independent input/viewport frame");++s.ownedChecks;
    }
    struct Point{Config config;Op op;unsigned occurrence=1;};
    const std::array points{
        Point{forceConfig(FEvent::Other,0),Op::Held},Point{forceConfig(FEvent::Other,0),Op::Held,2},
        Point{forceConfig(FEvent::KeyUp,0x11),Op::Control},
        Point{forceConfig(FEvent::KeyDown,0x11),Op::SetHeld},Point{forceConfig(FEvent::KeyDown,0x11),Op::Repaint},
        Point{forceConfig(FEvent::KeyDown,0x11),Op::Redraw},
        Point{routeConfig(0x65),Op::Menu},Point{routeConfig(0x65),Op::Finished},Point{routeConfig(0x65),Op::Observer},
        Point{routeConfig(0x65),Op::Reference},Point{routeConfig(0x65),Op::Prepare},Point{routeConfig(0x65),Op::Cycle},
        Point{routeConfig(0x65),Op::Team},Point{routeConfig(0x65),Op::View},Point{routeConfig(0x65),Op::Quick},
        Point{routeConfig(0x65),Op::Planning},Point{routeConfig(0x65),Op::Radar},
        Point{forceConfig(FEvent::PointerPressRestore,8),Op::SetHeld}};
    for(const auto& p:points){++s.exceptionCases;const auto r=s.compare(p.config,State{},[&p](Model& m,Config&){m.throwAt=p.op;m.throwOccurrence=p.occurrence;});
        s.check(!r.returned&&r.exception==p.op,"primitive exceptions propagate with identical partial state and no later effects");}
}
} // namespace

int main(){
    Stats s;
    try{
        forceCases(s);routerCases(s);dynamicCases(s);pointerRestoration(s);nestedAndExceptions(s);
        std::cout<<"keyboard-router contract checks="<<s.checks<<" differentialCases="<<s.cases
            <<" forceCases="<<s.forceCases<<" routerCases="<<s.routerCases<<" pointerRestoreCases="<<s.restoreCases
            <<" pointerCallbackCases="<<s.pointerCases<<" dynamicCases="<<s.dynamicCases<<" nestedCases="<<s.nestedCases
            <<" exceptionCases="<<s.exceptionCases<<" orderedTraceRows="<<s.traceRows<<" ownedChecks="<<s.ownedChecks
            <<" modifierCombinations=8 oldAdapterTrace=all-six-fields-equal frozenForceOwner=1 frozenRouterOwner=1"
            <<" windowsHeader=absent componentAlgorithms=existing-owner-boundaries nativeKeyAdapter=not-run"
            <<" realCaptureAndFocus=model-only actualAuthority=not-covered geometry=not-covered"
            <<" wrapperKeyClockEvaluationOrder=unchanged-not-fixed callbackRootReplacement=model-only failures=0\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"keyboard-router contract failure: "<<e.what()<<'\n';return 1;}
}
