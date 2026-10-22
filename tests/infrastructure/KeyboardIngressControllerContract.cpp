#include "ui/input/RuntimeKeyboardIngressController.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_MINWINDEF_)
#error Public keyboard ingress controller included a Windows header
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <iterator>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// These native-shaped values are confined to the independent literal oracle.
using UINT = std::uint32_t;
using WPARAM = std::uint64_t;
using LPARAM = std::int64_t;
using LRESULT = std::int64_t;
using HWND = std::uintptr_t;
constexpr int FALSE = 0;
constexpr UINT WM_KEYDOWN=0x0100, WM_KEYUP=0x0101, WM_CHAR=0x0102;
constexpr UINT WM_SYSKEYDOWN=0x0104, WM_SYSKEYUP=0x0105;
constexpr UINT WM_MOUSEMOVE=0x0200, WM_LBUTTONDOWN=0x0201, WM_LBUTTONUP=0x0202;
constexpr UINT WM_LBUTTONDBLCLK=0x0203, WM_RBUTTONDOWN=0x0204, WM_RBUTTONUP=0x0205;
constexpr UINT WM_RBUTTONDBLCLK=0x0206, WM_MBUTTONDOWN=0x0207, WM_MBUTTONUP=0x0208;
constexpr UINT WM_MBUTTONDBLCLK=0x0209, WM_MOUSEWHEEL=0x020A, WM_XBUTTONDOWN=0x020B;
constexpr UINT WM_XBUTTONUP=0x020C, WM_XBUTTONDBLCLK=0x020D, WM_MOUSEHWHEEL=0x020E;
constexpr int VK_CONTROL=0x11;
constexpr unsigned RDW_INVALIDATE=1, RDW_NOERASE=0x20;
struct RECT { int left=0,top=0,right=0,bottom=0; };

enum class Op {
    Campaign, Locked, Finished, Menu, Diplomacy, Capture, Control,
    Release, Invalidate, Repaint, Redraw,
    Modal, MenuCapture, Result, Options, SaveLoad, MenuNavigation,
    CommandClient, CommandKey, CommandClock, Command, Selection, Camera
};
struct Event {
    Op op;
    std::uint64_t raw=0;
    std::int64_t value=0;
    bool repeated=false;
    RuntimeKeyDownSource source=RuntimeKeyDownSource::KeyDown;
    std::string detail{};
    bool operator==(const Event&) const = default;
};
struct Model {
    bool campaign=false,locked=false,finished=false,menu=false,diplomacy=false;
    bool capture=false,control=false,repaint=false;
    int releases=0,invalidations=0,redraws=0,width=800;
    int keyMask=0;
    std::uint64_t stamp=777;
    std::array<bool,23> consumes{};
    RuntimeSelectionKeyResult selection=RuntimeSelectionKeyResult::Unhandled;
    bool operator==(const Model&) const = default;
};
struct Driver;
struct Flag {
    Driver* driver;
    Op op;
    operator bool() const;
    Flag& operator=(bool value);
};
struct RuntimeWindowState {
    Driver* driver;
    Flag campaignMode;
    struct { Flag campaignInputLocked; } triggers;
    Flag skirmishMatchFinished,runtimeMenuOpen;
    struct { Flag diplomacyOpen; } playerCommunication;
    Flag repaintRequested;
    explicit RuntimeWindowState(Driver& d):driver(&d),campaignMode{&d,Op::Campaign},
        triggers{{&d,Op::Locked}},skirmishMatchFinished{&d,Op::Finished},
        runtimeMenuOpen{&d,Op::Menu},playerCommunication{{&d,Op::Diplomacy}},
        repaintRequested{&d,Op::Repaint} {}
};
struct Driver {
    std::array<Model,2> roots{};
    std::size_t current=0;
    std::vector<Event> trace;
    std::function<void(Driver&,const Event&)> hook;
    RuntimeKeyDownSource source=RuntimeKeyDownSource::KeyDown;
    bool modern=false;
    RuntimeWindowState state{*this};
    Model& model(){return roots[current];}
    std::size_t emit(Event e){const auto i=trace.size();trace.push_back(e);if(hook)hook(*this,e);return i;}
    bool query(Op op){
        const auto i=emit({op});bool value=false;
        switch(op){
        case Op::Campaign:value=model().campaign;break;
        case Op::Locked:value=model().locked;break;
        case Op::Finished:value=model().finished;break;
        case Op::Menu:value=model().menu;break;
        case Op::Diplomacy:value=model().diplomacy;break;
        case Op::Capture:value=model().capture;break;
        case Op::Control:value=model().control;break;
        default:throw std::runtime_error("invalid query");
        }
        trace[i].value=value;return value;
    }
    void effect(Op op){
        switch(op){
        case Op::Release:model().capture=false;++model().releases;break;
        case Op::Invalidate:++model().invalidations;break;
        case Op::Repaint:model().repaint=true;break;
        case Op::Redraw:++model().redraws;break;
        default:throw std::runtime_error("invalid effect");
        }
        emit({op,0,1});
    }
    bool child(Op op,const RuntimeKeyboardIngressInput& input,std::string detail={}){
        emit({op,input.rawKey,static_cast<std::int32_t>(input.rawKey),
            input.repeated,input.source,std::move(detail)});
        return model().consumes[static_cast<std::size_t>(op)];
    }
    RuntimeSelectionKeyResult select(const RuntimeKeyboardIngressInput& input){
        emit({Op::Selection,input.rawKey,static_cast<std::int32_t>(input.rawKey),
            input.repeated,input.source,"existing-selection-result"});
        return model().selection;
    }
    bool run(const RuntimeKeyboardIngressInput& input);
};
Flag::operator bool()const{return driver->query(op);}
Flag& Flag::operator=(bool value){
    if(op!=Op::Repaint||!value)throw std::runtime_error("unexpected original store");
    driver->effect(op);return *this;
}
thread_local Driver* activeDriver=nullptr;
struct Active {
    Driver* previous;
    Driver& driver;
    RuntimeKeyDownSource previousSource;
    Active(Driver& d,RuntimeKeyDownSource s):previous(activeDriver),driver(d),
        previousSource(d.source){activeDriver=&d;d.source=s;}
    ~Active(){activeDriver=previous;driver.source=previousSource;}
};
HWND GetCapture(){return activeDriver->query(Op::Capture)?1U:2U;}
void ReleaseCapture(){activeDriver->effect(Op::Release);}
void InvalidateRect(HWND window,std::nullptr_t,int erase){
    if(window!=1U||erase!=FALSE)throw std::runtime_error("invalidate native shape");
    activeDriver->effect(Op::Invalidate);
}
void RedrawWindow(HWND window,std::nullptr_t,std::nullptr_t,unsigned flags){
    if(window!=1U||flags!=(RDW_INVALIDATE|RDW_NOERASE))
        throw std::runtime_error("redraw native shape");
    activeDriver->effect(Op::Redraw);
}
int GetKeyState(int key){
    if(key!=VK_CONTROL)throw std::runtime_error("unexpected physical sample");
    return activeDriver->query(Op::Control)?0x8000:0;
}
void GetClientRect(HWND window,RECT* client){
    if(window!=1U)throw std::runtime_error("client owner");
    activeDriver->emit({Op::CommandClient});
    *client=RECT{0,0,activeDriver->model().width,600};
}
int runtimePlayerCurrentKeyCode(WPARAM raw){
    const auto at=activeDriver->emit({Op::CommandKey,raw});
    const int result=static_cast<std::int32_t>(raw)^activeDriver->model().keyMask;
    activeDriver->trace[at].value=result;return result;
}
std::uint64_t runtimeUiClockNow(const RuntimeWindowState& state){
    const auto at=state.driver->emit({Op::CommandClock});
    const auto result=state.driver->model().stamp;
    state.driver->trace[at].detail=std::to_string(result);return result;
}
bool handleRuntimePlayerCommandCode(RuntimeWindowState& state,const RECT& client,
    int code,bool repeated,std::uint64_t now){
    RuntimeKeyboardIngressInput input{static_cast<std::uint32_t>(code),
        repeated,state.driver->source};
    return state.driver->child(Op::Command,input,
        std::to_string(client.right)+":"+std::to_string(now));
}
bool handleRuntimePlayerModalKey(RuntimeWindowState& state,int key,bool repeated){
    return state.driver->child(Op::Modal,{static_cast<std::uint32_t>(key),
        repeated,state.driver->source});
}
bool handleRuntimeMenuKeyCapture(RuntimeWindowState& state,WPARAM raw,bool repeated){
    return state.driver->child(Op::MenuCapture,{raw,repeated,state.driver->source});
}
struct RuntimeSessionKeyInput {std::uint64_t rawKey;bool repeated;};
struct RuntimeMenuKeyInput {std::uint64_t rawKey;bool repeated;};
struct RuntimeCameraKeyInput {std::uint64_t rawKey;bool repeated;};
struct RuntimeResultSessionKeyUiHost {
    RuntimeWindowState& state;
    RuntimeResultSessionKeyUiHost(RuntimeWindowState& s,HWND):state(s){}
};
struct RuntimeOptionsSessionKeyUiHost {
    RuntimeWindowState& state;
    RuntimeOptionsSessionKeyUiHost(RuntimeWindowState& s,HWND):state(s){}
};
struct RuntimeSaveLoadSessionKeyUiHost {
    RuntimeWindowState& state;
    RuntimeSaveLoadSessionKeyUiHost(RuntimeWindowState& s,HWND):state(s){}
};
struct RuntimeMenuNavigationUiHost {
    RuntimeWindowState& state;
    RuntimeMenuNavigationUiHost(RuntimeWindowState& s,HWND):state(s){}
};
struct RuntimeSelectionKeyWin32Host {
    RuntimeWindowState& state;std::uint64_t raw;
    RuntimeSelectionKeyWin32Host(RuntimeWindowState& s,WPARAM r):state(s),raw(r){}
};
struct RuntimeCameraKeyWin32Host {
    RuntimeWindowState& state;
    RuntimeCameraKeyWin32Host(RuntimeWindowState& s,HWND):state(s){}
};
bool handleRuntimeResultSessionKey(const RuntimeSessionKeyInput& i,RuntimeResultSessionKeyUiHost& h){
    return h.state.driver->child(Op::Result,{i.rawKey,i.repeated,h.state.driver->source});
}
bool handleRuntimeOptionsSessionKey(const RuntimeSessionKeyInput& i,RuntimeOptionsSessionKeyUiHost& h){
    return h.state.driver->child(Op::Options,{i.rawKey,i.repeated,h.state.driver->source});
}
bool handleRuntimeSaveLoadSessionKey(const RuntimeSessionKeyInput& i,bool control,RuntimeSaveLoadSessionKeyUiHost& h){
    return h.state.driver->child(Op::SaveLoad,{i.rawKey,i.repeated,h.state.driver->source},
        std::string("control:")+(control?"1":"0"));
}
bool handleRuntimeMenuNavigationKey(const RuntimeMenuKeyInput& i,RuntimeMenuNavigationUiHost& h){
    return h.state.driver->child(Op::MenuNavigation,{i.rawKey,i.repeated,h.state.driver->source});
}
RuntimeSelectionKeyResult handleRuntimeSelectionKey(const RuntimeSelectionKeyInput& i,RuntimeSelectionKeyWin32Host& h){
    if(i.rawKey!=static_cast<std::int32_t>(h.raw))throw std::runtime_error("selection narrowing");
    return h.state.driver->select({h.raw,i.repeated,i.source});
}
bool handleRuntimeCameraKey(const RuntimeCameraKeyInput& i,RuntimeCameraKeyWin32Host& h){
    return h.state.driver->child(Op::Camera,{i.rawKey,i.repeated,h.state.driver->source});
}

// Frozen complete wrapper: original client sampling and unspecified key/time
// argument evaluation stay in this same unchanged function for both model routes.
bool handleRuntimePlayerCommandKey(RuntimeWindowState& state, HWND window, WPARAM key, LPARAM flags) {
    RECT client{}; GetClientRect(window, &client);
    return handleRuntimePlayerCommandCode(state, client, runtimePlayerCurrentKeyCode(key),
        (flags & (static_cast<LPARAM>(1) << 30)) != 0, runtimeUiClockNow(state));
}


namespace frozen {
LRESULT admission(UINT message,RuntimeWindowState* campaignState){
    if (campaignState != nullptr
        && campaignState->campaignMode
        && campaignState->triggers.campaignInputLocked
        && !campaignState->skirmishMatchFinished
        && !campaignState->runtimeMenuOpen) {
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
            return 0;
        default:
            break;
        }
    }
    return 1;
}
LRESULT ingress(HWND window,WPARAM wParam,LPARAM lParam,UINT message,RuntimeWindowState* state){
        const bool wasDiplomacyOpen = state->playerCommunication.diplomacyOpen;
        if (!state->runtimeMenuOpen && !state->skirmishMatchFinished
            && handleRuntimePlayerModalKey(*state, static_cast<int>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0)) {
            if (wasDiplomacyOpen && !state->playerCommunication.diplomacyOpen && GetCapture() == window)
                ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE); return 0;
        }
        if (handleRuntimeMenuKeyCapture(*state, wParam,
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0)) {
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        {
            const RuntimeSessionKeyInput sessionKey{
                static_cast<std::uint64_t>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0
            };
            RuntimeResultSessionKeyUiHost resultHost(*state, window);
            if (::handleRuntimeResultSessionKey(sessionKey, resultHost)) return 0;
            const bool controlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            RuntimeOptionsSessionKeyUiHost optionsHost(*state, window);
            if (::handleRuntimeOptionsSessionKey(sessionKey, optionsHost)) return 0;
            RuntimeSaveLoadSessionKeyUiHost saveLoadHost(*state, window);
            if (::handleRuntimeSaveLoadSessionKey(sessionKey, controlDown, saveLoadHost)) return 0;
        }
        {
            RuntimeMenuNavigationUiHost host(*state, window);
            const RuntimeMenuKeyInput input{
                static_cast<std::uint64_t>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0
            };
            if (::handleRuntimeMenuNavigationKey(input, host)) return 0;
        }

        if (handleRuntimePlayerCommandKey(*state, window, wParam, lParam)) {
            if (state->playerCommunication.diplomacyOpen && GetCapture() == window) ReleaseCapture();
            state->repaintRequested = true;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        {
            RuntimeSelectionKeyWin32Host host(*state, wParam);
            const RuntimeSelectionKeyInput input{
                static_cast<std::int32_t>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0,
                message == WM_SYSKEYDOWN
                    ? RuntimeKeyDownSource::SystemKeyDown
                    : RuntimeKeyDownSource::KeyDown
            };
            const auto result = handleRuntimeSelectionKey(input, host);
            if (result == RuntimeSelectionKeyResult::RepeatedConsumed) return 0;
            if (result == RuntimeSelectionKeyResult::DispatchedConsumed) {
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }
        }

        {
            RuntimeCameraKeyWin32Host host(*state, window);
            const RuntimeCameraKeyInput input{
                static_cast<std::uint64_t>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0
            };
            if (handleRuntimeCameraKey(input, host)) {
                state->repaintRequested = true;
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
                return 0;
            }
        }
    return 1;
}
} // namespace frozen

class Admission final:public RuntimeCampaignInputAdmissionHost {
    Driver& d;
public:
    explicit Admission(Driver& driver):d(driver){}
    bool campaignMode()override{return d.query(Op::Campaign);}
    bool campaignInputLocked()override{return d.query(Op::Locked);}
    bool finished()override{return d.query(Op::Finished);}
    bool menuOpen()override{return d.query(Op::Menu);}
};
class Ingress final:public RuntimeKeyboardIngressHost {
    Driver& d;
public:
    explicit Ingress(Driver& driver):d(driver){}
    bool diplomacyOpen()override{return d.query(Op::Diplomacy);}
    bool menuOpen()override{return d.query(Op::Menu);}
    bool finished()override{return d.query(Op::Finished);}
    bool captureOwned()override{return d.query(Op::Capture);}
    bool controlDown()override{return d.query(Op::Control);}
    void releaseCapture()override{d.effect(Op::Release);}
    void invalidate()override{d.effect(Op::Invalidate);}
    void requestRepaint()override{d.effect(Op::Repaint);}
    void redraw()override{d.effect(Op::Redraw);}
    bool handleModalKey(std::int32_t raw,bool repeated)override{
        return d.child(Op::Modal,{static_cast<std::uint32_t>(raw),repeated,d.source});
    }
    bool handleMenuCaptureKey(const RuntimeKeyboardIngressInput& i)override{return d.child(Op::MenuCapture,i);}
    bool handleResultSessionKey(const RuntimeKeyboardIngressInput& i)override{return d.child(Op::Result,i);}
    bool handleOptionsSessionKey(const RuntimeKeyboardIngressInput& i)override{return d.child(Op::Options,i);}
    bool handleSaveLoadSessionKey(const RuntimeKeyboardIngressInput& i,bool control)override{
        return d.child(Op::SaveLoad,i,std::string("control:")+(control?"1":"0"));
    }
    bool handleMenuNavigationKey(const RuntimeKeyboardIngressInput& i)override{return d.child(Op::MenuNavigation,i);}
    bool handleCommandKey(const RuntimeKeyboardIngressInput& i)override{
        return handleRuntimePlayerCommandKey(d.state,1,i.rawKey,i.repeated?(LPARAM{1}<<30):0);
    }
    RuntimeSelectionKeyResult handleSelectionKey(const RuntimeKeyboardIngressInput& i)override{return d.select(i);}
    bool handleCameraKey(const RuntimeKeyboardIngressInput& i)override{return d.child(Op::Camera,i);}
};
bool Driver::run(const RuntimeKeyboardIngressInput& input){
    Active active(*this,input.source);
    if(modern){Ingress host(*this);return handleRuntimeKeyboardIngressKey(input,host);}
    return frozen::ingress(1,input.rawKey,input.repeated?(LPARAM{1}<<30):0,
        input.source==RuntimeKeyDownSource::SystemKeyDown?WM_SYSKEYDOWN:WM_KEYDOWN,&state)==0;
}

namespace contract {
using Input=RuntimeKeyboardIngressInput;
using Kind=RuntimeCampaignInputEvent;
struct Stats {
    std::size_t checks=0,cases=0,admissionCases=0,ingressCases=0;
    std::size_t dynamicCases=0,nestedCases=0,exceptionCases=0,traceRows=0,ownedChecks=0;
    void need(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
};
struct Outcome {
    bool consumed=false;
    std::string error;
    bool operator==(const Outcome&)const=default;
};
using Setup=std::function<void(Driver&,Input&)>;
using Inspect=std::function<void(const Driver&,const Driver&,const Outcome&)>;
std::size_t count(const Driver& d,Op op){
    return static_cast<std::size_t>(std::count_if(d.trace.begin(),d.trace.end(),
        [op](const Event& e){return e.op==op;}));
}
Outcome invoke(Driver& d,const Input& input){
    try{return {d.run(input),{}};}
    catch(const std::exception& e){return {false,e.what()};}
}
void compare(Stats& s,const Input& input,const Setup& setup={},const Inspect& inspect={},
    bool expectThrow=false){
    Driver old,newer;newer.modern=true;Input a=input,b=input;
    if(setup){setup(old,a);setup(newer,b);}
    const auto left=invoke(old,a),right=invoke(newer,b);
    s.need(left==right,"old/new result and exception text");
    s.need(old.trace==newer.trace,"complete ordered six-field query/effect trace");
    s.need(old.roots==newer.roots&&old.current==newer.current,"full current and retired model roots");
    s.need(!expectThrow?left.error.empty():!left.error.empty(),"expected callback exception boundary");
    s.need(a.rawKey==b.rawKey&&a.repeated==b.repeated&&a.source==b.source,"carrier callback effects");
    if(inspect)inspect(old,newer,left);
    s.traceRows+=old.trace.size();++s.cases;++s.ingressCases;
}
void stopAt(Model& m,int stop){
    m=Model{};
    constexpr Op stages[]={Op::Modal,Op::MenuCapture,Op::Result,Op::Options,
        Op::SaveLoad,Op::MenuNavigation,Op::Command,Op::Selection,Op::Selection,Op::Camera};
    for(int i=stop;i<10;++i)m.consumes[static_cast<std::size_t>(stages[i])]=true;
    m.selection=stop==7?RuntimeSelectionKeyResult::RepeatedConsumed:
        stop==8?RuntimeSelectionKeyResult::DispatchedConsumed:RuntimeSelectionKeyResult::Unhandled;
}
const std::array<std::pair<Kind,UINT>,23> messages{{
    {Kind::Other,0x0000},{Kind::LeftButtonDown,WM_LBUTTONDOWN},{Kind::LeftButtonUp,WM_LBUTTONUP},
    {Kind::LeftButtonDoubleClick,WM_LBUTTONDBLCLK},{Kind::RightButtonDown,WM_RBUTTONDOWN},
    {Kind::RightButtonUp,WM_RBUTTONUP},{Kind::RightButtonDoubleClick,WM_RBUTTONDBLCLK},
    {Kind::MiddleButtonDown,WM_MBUTTONDOWN},{Kind::MiddleButtonUp,WM_MBUTTONUP},
    {Kind::MiddleButtonDoubleClick,WM_MBUTTONDBLCLK},{Kind::AuxiliaryButtonDown,WM_XBUTTONDOWN},
    {Kind::AuxiliaryButtonUp,WM_XBUTTONUP},{Kind::AuxiliaryButtonDoubleClick,WM_XBUTTONDBLCLK},
    {Kind::PointerMove,WM_MOUSEMOVE},{Kind::VerticalWheel,WM_MOUSEWHEEL},
    {Kind::HorizontalWheel,WM_MOUSEHWHEEL},{Kind::KeyDown,WM_KEYDOWN},{Kind::KeyUp,WM_KEYUP},
    {Kind::Character,WM_CHAR},{Kind::SystemKeyDown,WM_SYSKEYDOWN},
    {Kind::SystemKeyUp,WM_SYSKEYUP},{static_cast<Kind>(254),0xDEAD},
    {static_cast<Kind>(255),0xFFFFFFFFU}
}};
bool blockedRaw(UINT message){
    constexpr UINT blocked[]={WM_LBUTTONDOWN,WM_LBUTTONUP,WM_LBUTTONDBLCLK,
        WM_RBUTTONDOWN,WM_RBUTTONUP,WM_RBUTTONDBLCLK,WM_MBUTTONDOWN,WM_MBUTTONUP,
        WM_MBUTTONDBLCLK,WM_XBUTTONDOWN,WM_XBUTTONUP,WM_XBUTTONDBLCLK,
        WM_MOUSEMOVE,WM_MOUSEWHEEL,WM_MOUSEHWHEEL,WM_KEYDOWN,WM_KEYUP,WM_CHAR};
    return std::find(std::begin(blocked),std::end(blocked),message)!=std::end(blocked);
}
void admission(Stats& s){
    for(const auto& [kind,raw]:messages)for(int bits=0;bits<16;++bits){
        Driver old,newer;
        for(auto* d:{&old,&newer}){auto& m=d->model();m.campaign=(bits&1)!=0;
            m.locked=(bits&2)!=0;m.finished=(bits&4)!=0;m.menu=(bits&8)!=0;}
        bool left=false,right=false;
        {Active active(old,RuntimeKeyDownSource::KeyDown);left=frozen::admission(raw,&old.state)==0;}
        {Admission host(newer);right=rejectRuntimeCampaignInput(kind,host);}
        const bool expected=bits==3&&blockedRaw(raw);
        s.need(left==right&&right==expected,"finite campaign admission against independent original messages");
        s.need(old.trace==newer.trace&&old.roots==newer.roots,"lazy admission queries and full model");
        const std::size_t queries=(bits&1)==0?1U:(bits&2)==0?2U:(bits&4)!=0?3U:4U;
        s.need(old.trace.size()==queries,"campaign guard short-circuit query count");
        s.traceRows+=old.trace.size();++s.admissionCases;++s.cases;
    }
    Driver empty;
    s.need(frozen::admission(WM_KEYDOWN,nullptr)==1&&empty.trace.empty(),
        "native root-null guard remains outside standard host");
    ++s.admissionCases;++s.cases;
}
void ingressMatrix(Stats& s){
    for(int stop=0;stop<=10;++stop)for(unsigned mods=0;mods<8;++mods)
        for(bool repeated:{false,true})for(auto source:{RuntimeKeyDownSource::KeyDown,RuntimeKeyDownSource::SystemKeyDown}){
        const Input input{0x7CU|(static_cast<std::uint64_t>(mods)<<8),repeated,source};
        compare(s,input,[stop,mods](Driver& d,Input&){
            stopAt(d.model(),stop);d.model().control=(mods&1U)!=0;
            d.model().diplomacy=(mods&2U)!=0;d.model().capture=(mods&4U)!=0;
        },[&s,stop,mods](const Driver& d,const Driver&,const Outcome& result){
            s.need(result.consumed==(stop!=10),"selected first consumer or native fallback");
            s.need(count(d,Op::Control)==(stop>=3?1U:0U),"unique control snapshot only after Result miss");
            s.need(count(d,Op::Release)==(stop==6&&(mods&2U)!=0&&(mods&4U)!=0?1U:0U),
                "no unrelated capture release");
            if(stop==7)s.need(count(d,Op::Repaint)==0&&count(d,Op::Redraw)==0,
                "repeated Selection consumed without outer effects");
            if(stop==8||stop==9)s.need(count(d,Op::Repaint)==1&&count(d,Op::Redraw)==1,
                "dispatched Selection or Camera owns repaint/redraw");
        });
    }
    constexpr std::uint64_t rawCases[]={0,0x11,0x80000000ULL,0xFFFFFFFFULL,
        0x10000007CULL,0x800000000000007CULL,0xFFFFFFFFFFFFFF7CULL,
        std::numeric_limits<std::uint64_t>::max()};
    for(auto raw:rawCases)for(int stop=0;stop<=10;++stop)
        for(auto source:{RuntimeKeyDownSource::KeyDown,RuntimeKeyDownSource::SystemKeyDown})
        compare(s,{raw,(raw&1U)!=0,source},[stop](Driver& d,Input&){stopAt(d.model(),stop);});
    for(int bits=0;bits<16;++bits)compare(s,{0x41,false,RuntimeKeyDownSource::KeyDown},
        [bits](Driver& d,Input&){stopAt(d.model(),0);d.model().menu=(bits&1)!=0;
            d.model().finished=(bits&2)!=0;d.model().diplomacy=(bits&4)!=0;d.model().capture=(bits&8)!=0;},
        [&s,bits](const Driver& d,const Driver&,const Outcome&){
            s.need(count(d,Op::Modal)==((bits&3)==0?1U:0U),"menu/finished guard only suppresses modal");
            s.need(count(d,Op::MenuCapture)==((bits&3)==0?0U:1U),"capture admission follows modal miss");
        });
}
void releaseAndSnapshots(Stats& s){
    for(bool was:{false,true})for(bool post:{false,true})for(bool owns:{false,true}){
        compare(s,{0x7C,false,RuntimeKeyDownSource::SystemKeyDown},
            [was,post,owns](Driver& d,Input&){stopAt(d.model(),0);d.model().diplomacy=was;
                d.model().capture=owns;d.hook=[post](Driver& owner,const Event& e){
                    if(e.op==Op::Modal)owner.model().diplomacy=post;};},
            [&s,was,post,owns](const Driver& d,const Driver&,const Outcome&){
                s.need(count(d,Op::Release)==(was&&!post&&owns?1U:0U),"modal original closed-diplomacy release condition");
                s.need(count(d,Op::Capture)==(was&&!post?1U:0U),"modal capture lookup stays lazy");
                s.need(d.trace.back().op==Op::Invalidate,"modal invalidates after conditional release");
            });
        compare(s,{0x7C,true,RuntimeKeyDownSource::KeyDown},
            [was,post,owns](Driver& d,Input&){stopAt(d.model(),6);d.model().diplomacy=was;
                d.model().capture=owns;d.hook=[post](Driver& owner,const Event& e){
                    if(e.op==Op::Command)owner.model().diplomacy=post;};},
            [&s,post,owns](const Driver& d,const Driver&,const Outcome&){
                s.need(count(d,Op::Release)==(post&&owns?1U:0U),"command uses fresh post-dispatch diplomacy");
                s.need(count(d,Op::Capture)==(post?1U:0U),"command capture lookup stays lazy");
                s.need(d.trace[d.trace.size()-2].op==Op::Repaint&&d.trace.back().op==Op::Invalidate,
                    "command outer repaint then invalidate");
            });
    }
    for(bool initial:{false,true}){
        compare(s,{0x46,false,RuntimeKeyDownSource::KeyDown},
            [initial](Driver& d,Input&){stopAt(d.model(),4);d.model().control=initial;
                d.hook=[](Driver& owner,const Event& e){if(e.op==Op::Options)owner.model().control=!owner.model().control;};},
            [&s,initial](const Driver& d,const Driver&,const Outcome&){
                const auto save=std::find_if(d.trace.begin(),d.trace.end(),[](const Event& e){return e.op==Op::SaveLoad;});
                s.need(save!=d.trace.end()&&save->detail==std::string("control:")+(initial?"1":"0"),
                    "SaveLoad borrows the sole original Control snapshot despite Options callback");
            });
        ++s.dynamicCases;
    }
}
void dynamicAndOwned(Stats& s){
    const Op triggers[]={Op::Diplomacy,Op::Menu,Op::Finished,Op::Modal,
        Op::MenuCapture,Op::Result,Op::Control,Op::Options,
        Op::SaveLoad,Op::MenuNavigation,Op::CommandClient,Op::CommandKey,
        Op::CommandClock,Op::Command,Op::Selection,Op::Camera};
    for(auto trigger:triggers){
        compare(s,{0x800000010000007CULL,true,RuntimeKeyDownSource::SystemKeyDown},
            [trigger](Driver& d,Input& carrier){stopAt(d.model(),10);d.roots[1]=d.roots[0];
                d.roots[1].control=true;d.roots[1].diplomacy=true;d.roots[1].capture=true;
                d.roots[1].stamp=999;d.roots[1].width=901;d.roots[1].keyMask=0x100;
                d.hook=[trigger,&carrier,done=false](Driver& owner,const Event& e)mutable{
                    if(!done&&e.op==trigger){done=true;owner.current=1;
                        carrier={0,false,RuntimeKeyDownSource::KeyDown};}
                };},
            [&s](const Driver& d,const Driver&,const Outcome&){
                s.need(d.current==1,"callback committed a fresh model root");
            });
        ++s.dynamicCases;++s.ownedChecks;
    }
    for(auto trigger:{Op::Campaign,Op::Locked,Op::Finished,Op::Menu}){
        Driver old,newer;Kind a=Kind::KeyDown,b=a;
        for(auto* d:{&old,&newer}){d->model().campaign=d->model().locked=true;
            d->roots[1]=d->model();d->roots[1].menu=true;}
        old.hook=[trigger,&a,done=false](Driver& d,const Event& e)mutable{
            if(!done&&e.op==trigger){done=true;d.current=1;a=Kind::SystemKeyDown;}};
        newer.hook=[trigger,&b,done=false](Driver& d,const Event& e)mutable{
            if(!done&&e.op==trigger){done=true;d.current=1;b=Kind::SystemKeyDown;}};
        bool left=false,right=false;
        {Active active(old,RuntimeKeyDownSource::KeyDown);left=frozen::admission(WM_KEYDOWN,&old.state)==0;}
        {Admission host(newer);right=rejectRuntimeCampaignInput(b,host);}
        s.need(left==right&&old.trace==newer.trace&&old.roots==newer.roots&&a==b,
            "admission by-value event and fresh root between lazy guards");
        ++s.dynamicCases;++s.ownedChecks;++s.admissionCases;++s.cases;s.traceRows+=old.trace.size();
    }
}
void nestedAndExceptions(Stats& s){
    for(auto trigger:{Op::Diplomacy,Op::MenuCapture,Op::Control,Op::Options,
        Op::CommandClient,Op::Command,Op::Selection}){
        compare(s,{0x7C,false,RuntimeKeyDownSource::SystemKeyDown},
            [trigger](Driver& d,Input&){stopAt(d.model(),10);
                d.hook=[trigger,done=false](Driver& owner,const Event& e)mutable{
                    if(!done&&e.op==trigger){done=true;
                        const auto prior=owner.model().consumes;
                        owner.model().consumes[static_cast<std::size_t>(Op::Result)]=true;
                        const Input nested{0x100000041ULL,true,RuntimeKeyDownSource::KeyDown};
                        if(!owner.run(nested))throw std::runtime_error("nested result missed");
                        owner.model().consumes=prior;owner.model().stamp+=10;
                    }
                };});
        ++s.nestedCases;
    }
    constexpr Op throwPoints[]={Op::Diplomacy,Op::Menu,Op::Finished,Op::Modal,
        Op::MenuCapture,Op::Result,Op::Control,Op::Options,Op::SaveLoad,
        Op::MenuNavigation,Op::CommandClient,Op::CommandKey,Op::CommandClock,
        Op::Command,Op::Selection,Op::Camera,Op::Capture,Op::Release,Op::Invalidate,Op::Repaint,Op::Redraw};
    for(auto point:throwPoints){
        const int stop=point==Op::Release||point==Op::Capture?0:point==Op::Invalidate?0:
            point==Op::Repaint||point==Op::Redraw?1:10;
        compare(s,{0x7C,true,RuntimeKeyDownSource::SystemKeyDown},
            [point,stop](Driver& d,Input&){stopAt(d.model(),stop);
                if(point==Op::Release||point==Op::Capture){d.model().diplomacy=true;d.model().capture=true;}
                d.hook=[point](Driver& owner,const Event& e){
                    if(e.op==Op::Modal&&(point==Op::Release||point==Op::Capture))owner.model().diplomacy=false;
                    if(e.op==point)throw std::runtime_error("at:"+std::to_string(static_cast<int>(point)));
                };},{},true);
        ++s.exceptionCases;
    }
    for(auto point:{Op::Campaign,Op::Locked,Op::Finished,Op::Menu}){
        Driver old,newer;for(auto* d:{&old,&newer}){d->model().campaign=d->model().locked=true;
            d->hook=[point](Driver&,const Event& e){if(e.op==point)throw std::runtime_error("admission callback");};}
        std::string a,b;try{Active active(old,RuntimeKeyDownSource::KeyDown);(void)frozen::admission(WM_KEYDOWN,&old.state);}catch(const std::exception& e){a=e.what();}
        try{Admission host(newer);(void)rejectRuntimeCampaignInput(Kind::KeyDown,host);}catch(const std::exception& e){b=e.what();}
        s.need(!a.empty()&&a==b&&old.trace==newer.trace&&old.roots==newer.roots,
            "admission callback exception and query prefix");
        ++s.exceptionCases;++s.admissionCases;++s.cases;s.traceRows+=old.trace.size();
    }
}
} // namespace contract
int main(){
    contract::Stats s;
    try{
        contract::admission(s);contract::ingressMatrix(s);contract::releaseAndSnapshots(s);
        contract::dynamicAndOwned(s);contract::nestedAndExceptions(s);
        std::cout<<"keyboard-ingress contract checks="<<s.checks<<" differentialCases="<<s.cases
            <<" admissionCases="<<s.admissionCases<<" ingressCases="<<s.ingressCases
            <<" dynamicCases="<<s.dynamicCases<<" nestedCases="<<s.nestedCases
            <<" exceptionCases="<<s.exceptionCases<<" orderedTraceRows="<<s.traceRows
            <<" ownedChecks="<<s.ownedChecks
            <<" modifierCombinations=8 oldAdapterTrace=all-six-fields-equal"
            <<" frozenAdmissionOwner=1 frozenIngressOwner=1 unchangedCommandWrapper=literal"
            <<" windowsHeader=absent childAlgorithms=existing-owner-boundaries"
            <<" realCaptureDestroyPersistence=model-only nativeKeyAdapter=not-run"
            <<" wrapperKeyClockEvaluationOrder=unchanged-not-fixed callbackRootReplacement=model-only failures=0\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"keyboard-ingress contract failure: "<<e.what()<<'\n';return 1;}
}
