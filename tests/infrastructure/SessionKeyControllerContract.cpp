#include "ui/input/RuntimeSessionKeyController.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_MINWINDEF_)
#error RuntimeSessionKeyController.h must remain platform independent
#endif
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<decltype(RuntimeSessionKeyInput{}.rawKey),std::uint64_t>);
static_assert(std::is_same_v<decltype(RuntimeSessionKeyInput{}.repeated),bool>);
static_assert(RuntimeSessionKeyInput{}.rawKey==0&&!RuntimeSessionKeyInput{}.repeated);
static_assert(std::numeric_limits<int>::digits==31);
static_assert(!noexcept(handleRuntimeResultSessionKey(std::declval<const RuntimeSessionKeyInput&>(),std::declval<RuntimeResultSessionKeyHost&>())));
static_assert(!noexcept(handleRuntimeOptionsSessionKey(std::declval<const RuntimeSessionKeyInput&>(),std::declval<RuntimeOptionsSessionKeyHost&>())));
static_assert(!noexcept(handleRuntimeSaveLoadSessionKey(std::declval<const RuntimeSessionKeyInput&>(),false,std::declval<RuntimeSaveLoadSessionKeyHost&>())));

namespace {
using Input=RuntimeSessionKeyInput;
using Page=RuntimeMenuPage;
using WPARAM=std::uint64_t;
using LPARAM=std::int64_t;
constexpr WPARAM VK_RETURN=0x0D,VK_ESCAPE=0x1B,VK_LEFT=0x25,VK_RIGHT=0x27;
constexpr WPARAM VK_PRIOR=0x21,VK_NEXT=0x22,VK_CONTROL=0x11,VK_F5=0x74,VK_F9=0x78;
constexpr unsigned RDW_INVALIDATE=1,RDW_NOERASE=32;
constexpr bool FALSE=false;
constexpr int runtimeSoundIntentMenuOpen=1,runtimeSoundIntentMenuClose=2;
enum class RuntimeSidebarTool { None,Repair,Sell };
enum class Entry:int { Result,Options,SaveLoad,Chain };
enum class Op:std::size_t {
    Enter,Return,Caught,Finished,Pending,Banner,Campaign,Now,BeginScore,QueueScore,
    Repaint,Exit,Destroy,Rows,ScorePage,SetScorePage,Control,Binding,Sample,
    Menu,Beacon,Follow,Rally,Tool,SetBeacon,SetRally,SetFollow,ClearTool,
    SetDragMoved,SetDragActive,Capture,Release,CancelWeapon,Dismiss,
    MenuPage,ParentPage,ResetPage,SetMenu,MenuSound,Redraw,Invalidate,
    Bindings,BindingItem,Dispatch,MP,Path,SetStatus,Status,EconomyStatus,FileSound,Limit
};
// Six complete fields: operation, two numeric values, owned text, raw key, repeat.
struct Event {
    Op op;std::int64_t a,b;std::string text;std::uint64_t raw;bool repeated;
    bool operator==(const Event&)const=default;
};
struct Binding {
    std::string id;std::int32_t code;
    bool operator==(const Binding&)const=default;
};
struct State {
    bool finished=false,pending=false,banner=false,campaign=false,queued=false;
    RuntimeUiTimeMs now=0xFFFFFFFFFFFFFFFEULL;
    std::size_t rows=17;std::int32_t score=1;
    bool exit=false,destroyed=false,menu=false,beacon=false,follow=false,rally=false;
    int tool=0;bool dragMoved=true,dragActive=true,capture=true,cancel=false,dismiss=false;
    Page page=Page::Main,parent=Page::Settings;
    std::int32_t options=0x1B;
    std::vector<Binding> bindings{{"Options",0x1B},{"StopObject",'S'}};
    bool dispatchOk=true,mp=false;
    std::string error="original error",path="private/save.ini",status="before",economy="before";
    unsigned repaint=0,redraw=0,invalidate=0,begin=0,queueCalls=0,releases=0,dispatches=0;
    unsigned menuSounds=0,fileSounds=0,epoch=0;
    bool saved=false;
    bool operator==(const State&)const=default;
};
struct SyntheticFailure {Op op;};
struct Model {
    State state;std::vector<State> retired;std::vector<Event> trace;
    Input active{};bool modern=false,control=false,armed=true;
    std::int32_t modifiers=0;std::vector<std::int32_t> samples;std::size_t sampleAt=0;
    std::array<unsigned,static_cast<std::size_t>(Op::Limit)> calls{};
    std::function<void(Model&,Op)> hook;
    Op throwAt=Op::Limit;unsigned throwOccurrence=1;
    void fire(Op op){const auto n=++calls[static_cast<std::size_t>(op)];if(hook)hook(*this,op);if(op==throwAt&&n==throwOccurrence)throw SyntheticFailure{op};}
    void emit(Op op,std::int64_t a=0,std::int64_t b=0,std::string text={},const Input* input=nullptr){
        const Input in=input?*input:active;trace.push_back({op,a,b,std::move(text),in.rawKey,in.repeated});fire(op);
    }
    std::int64_t get(Op op){
        std::int64_t v=0;
        switch(op){
        case Op::Finished:v=state.finished;break;case Op::Pending:v=state.pending;break;
        case Op::Banner:v=state.banner;break;case Op::Campaign:v=state.campaign;break;
        case Op::ScorePage:v=state.score;break;case Op::Menu:v=state.menu;break;
        case Op::Beacon:v=state.beacon;break;case Op::Follow:v=state.follow;break;
        case Op::Rally:v=state.rally;break;case Op::Tool:v=state.tool;break;
        case Op::Capture:v=state.capture;break;case Op::MenuPage:v=static_cast<int>(state.page);break;
        case Op::MP:v=state.mp;break;default:throw std::logic_error("unexpected field read");
        }
        emit(op,v);return v;
    }
    void set(Op op,std::int64_t v){
        switch(op){
        case Op::Exit:state.exit=v!=0;break;case Op::SetScorePage:state.score=static_cast<std::int32_t>(v);break;
        case Op::SetBeacon:state.beacon=v!=0;break;case Op::SetRally:state.rally=v!=0;break;
        case Op::SetFollow:state.follow=v!=0;break;case Op::ClearTool:state.tool=static_cast<int>(v);break;
        case Op::SetDragMoved:state.dragMoved=v!=0;break;case Op::SetDragActive:state.dragActive=v!=0;break;
        case Op::SetMenu:state.menu=v!=0;break;default:throw std::logic_error("unexpected field store");
        }
        emit(op,v);
    }
    RuntimeUiTimeMs time(){const auto v=state.now;emit(Op::Now,0,0,std::to_string(v));return v;}
    void begin(RuntimeUiTimeMs time){++state.begin;state.banner=false;emit(Op::BeginScore,0,0,std::to_string(time));}
    bool queue(){const bool v=state.queued;++state.queueCalls;emit(Op::QueueScore,v);return v;}
    void repaint(){++state.repaint;emit(Op::Repaint,1);}
    void destroy(){state.destroyed=true;emit(Op::Destroy);}
    std::size_t rows(){const auto v=state.rows;emit(Op::Rows,static_cast<std::int64_t>(v));return v;}
    bool readControl(){const bool v=control;emit(Op::Control,v);return v;}
    std::int32_t binding(const std::string& id,std::int32_t fallback){const auto v=state.options;emit(Op::Binding,fallback,v,id);return v;}
    std::int32_t sample(const Input& in){const auto v=sampleAt<samples.size()?samples[sampleAt]:static_cast<std::int32_t>(in.rawKey&0xFF)|modifiers;++sampleAt;emit(Op::Sample,v,0,{},&in);return v;}
    void release(){state.capture=false;++state.releases;emit(Op::Release);}
    bool cancel(RuntimeSessionCancelReason reason){const bool v=state.cancel;emit(Op::CancelWeapon,static_cast<int>(reason),v);return v;}
    bool dismiss(){const bool v=state.dismiss;emit(Op::Dismiss,v);return v;}
    Page parent(Page page){const auto v=state.parent;emit(Op::ParentPage,static_cast<int>(page),static_cast<int>(v));return v;}
    void reset(Page page){state.page=page;emit(Op::ResetPage,static_cast<int>(page));}
    void menuSound(bool open){++state.menuSounds;emit(Op::MenuSound,open);}
    void redraw(){++state.redraw;emit(Op::Redraw,RDW_INVALIDATE|RDW_NOERASE);}
    void invalidate(){++state.invalidate;emit(Op::Invalidate,0);}
    std::vector<Binding> bindingSnapshot(){
        const auto values=state.bindings;trace.push_back({Op::Bindings,static_cast<std::int64_t>(values.size()),0,{},active.rawKey,active.repeated});
        for(std::size_t i=0;i<values.size();++i)trace.push_back({Op::BindingItem,values[i].code,static_cast<std::int64_t>(i),values[i].id,active.rawKey,active.repeated});
        fire(Op::Bindings);return values;
    }
    bool dispatch(bool save,std::string& error){const bool ok=state.dispatchOk;error=state.error;++state.dispatches;state.saved=save;emit(Op::Dispatch,save,ok,error);return ok;}
    std::string path(){const auto p=state.path;emit(Op::Path,0,0,p);return p;}
    void status(std::string s){state.status=std::move(s);emit(Op::SetStatus,0,0,state.status);}
    std::string status(){const auto s=state.status;emit(Op::Status,0,0,s);return s;}
    void economy(std::string s){state.economy=std::move(s);emit(Op::EconomyStatus,0,0,state.economy);}
    void fileSound(RuntimeSessionFileSound s){++state.fileSounds;emit(Op::FileSound,static_cast<int>(s));}
    void swapRoot(){const auto epoch=state.epoch+1;retired.push_back(std::move(state));state=State{};state.epoch=epoch;state.status="fresh root";}
};
struct Host final:RuntimeResultSessionKeyHost,RuntimeOptionsSessionKeyHost,RuntimeSaveLoadSessionKeyHost {
    Model& m;explicit Host(Model& model):m(model){}
    bool finished()override{return m.get(Op::Finished)!=0;}
    bool exitAfterPendingVideos()override{return m.get(Op::Pending)!=0;}
    bool bannerVisible()override{return m.get(Op::Banner)!=0;}
    bool campaignMode()override{return m.get(Op::Campaign)!=0;}
    RuntimeUiTimeMs nowMs()override{return m.time();}
    void beginScorePage(RuntimeUiTimeMs now)override{m.begin(now);}
    bool queueCampaignPostScore()override{return m.queue();}
    void requestRepaint()override{m.repaint();}
    void setExitRequested(bool v)override{m.set(Op::Exit,v);}
    void destroyWindow()override{m.destroy();}
    std::size_t scoreRowCount()override{return m.rows();}
    std::int32_t scorePage()override{return static_cast<std::int32_t>(m.get(Op::ScorePage));}
    void setScorePage(std::int32_t v)override{m.set(Op::SetScorePage,v);}
    std::int32_t binding(const std::string& id,std::int32_t fallback)override{return m.binding(id,fallback);}
    std::int32_t sampleCurrentKeyCode(const Input& in)override{return m.sample(in);}
    bool menuOpen()override{return m.get(Op::Menu)!=0;}
    bool beaconTargeting()override{return m.get(Op::Beacon)!=0;}
    bool followTargeting()override{return m.get(Op::Follow)!=0;}
    bool rallyTargeting()override{return m.get(Op::Rally)!=0;}
    bool sidebarToolIsActive()override{return m.get(Op::Tool)!=0;}
    void setBeaconTargeting(bool v)override{m.set(Op::SetBeacon,v);}
    void setRallyTargeting(bool v)override{m.set(Op::SetRally,v);}
    void setFollowTargeting(bool v)override{m.set(Op::SetFollow,v);}
    void clearSidebarTool()override{m.set(Op::ClearTool,0);}
    void setSelectionDragMoved(bool v)override{m.set(Op::SetDragMoved,v);}
    void setSelectionDragActive(bool v)override{m.set(Op::SetDragActive,v);}
    bool ownsCapture()override{return m.get(Op::Capture)!=0;}
    void releaseCapture()override{m.release();}
    bool cancelSuperWeaponTargetSelection(RuntimeSessionCancelReason r)override{return m.cancel(r);}
    bool dismissPlacement()override{return m.dismiss();}
    Page page()override{return static_cast<Page>(m.get(Op::MenuPage));}
    Page parentPage(Page p)override{return m.parent(p);}
    void resetPage(Page p)override{m.reset(p);}
    void setMenuOpen(bool v)override{m.set(Op::SetMenu,v);}
    void menuSound(bool v)override{m.menuSound(v);}
    void redraw()override{m.redraw();}
    void invalidate()override{m.invalidate();}
    std::vector<RuntimeSessionKeyBinding> bindings()override{std::vector<RuntimeSessionKeyBinding> result;for(const auto& b:m.bindingSnapshot())result.push_back({b.id,b.code});return result;}
    bool dispatch(bool s,std::string& e)override{return m.dispatch(s,e);}
    bool multiplayerBridgePresent()override{return m.get(Op::MP)!=0;}
    std::string defaultSavePath()override{return m.path();}
    void setInteractionStatus(std::string s)override{m.status(std::move(s));}
    std::string interactionStatus()override{return m.status();}
    void setEconomyStatus(std::string s)override{m.economy(std::move(s));}
    void fileSound(RuntimeSessionFileSound s)override{m.fileSound(s);}
};
// Proxies observe original scalar reads/stores. They retain only the stable root.
template<class T>struct ReadField {Model& m;Op op;operator T()const{return static_cast<T>(m.get(op));}};
struct BoolField:ReadField<bool> {
    Op write;
    BoolField(Model& model,Op read,Op store):ReadField<bool>{model,read},write(store){}
    bool operator=(bool v){m.set(write,v);return v;} // preserves chained-store order
};
struct IntField:ReadField<int> {
    IntField(Model& model):ReadField<int>{model,Op::ScorePage}{}
    int operator=(int v){m.set(Op::SetScorePage,v);return v;}
};
struct ToolField:ReadField<RuntimeSidebarTool> {
    ToolField(Model& model):ReadField<RuntimeSidebarTool>{model,Op::Tool}{}
    RuntimeSidebarTool operator=(RuntimeSidebarTool v){m.set(Op::ClearTool,static_cast<int>(v));return v;}
};
struct RepaintField {Model& m;bool operator=(bool v){if(!v)throw std::logic_error("old repaint");m.repaint();return v;}};
struct StatusField {
    Model& m;operator std::string()const{return m.status();}
    void operator=(std::string s){m.status(std::move(s));}
};
struct EconomyField {Model& m;void operator=(std::string s){m.economy(std::move(s));}};
struct BridgeField {Model& m;bool operator!=(std::nullptr_t)const{return m.get(Op::MP)!=0;}};
struct BindingField {
    Model& m;mutable std::optional<std::vector<std::pair<std::string,int>>> owned;
    void ensure()const{if(!owned){const auto values=m.bindingSnapshot();owned.emplace();for(const auto& b:values)owned->emplace_back(b.id,b.code);}}
    auto begin()const{ensure();return owned->cbegin();}
    auto end()const{ensure();return owned->cend();}
};
struct RuntimeWindowState {
    ReadField<bool> skirmishMatchFinished,exitAfterPendingCampaignVideos,campaignMode;
    struct Result {ReadField<bool> bannerVisible;IntField scorePage;} resultPresentation;
    BoolField runtimeExitRequested;
    RepaintField repaintRequested;
    BoolField runtimeMenuOpen;
    struct Communication {BoolField beaconTargeting;} playerCommunication;
    struct Local {BoolField followTargeting,rallyTargeting;} playerLocalCommands;
    ToolField activeSidebarTool;
    BoolField selectionDragActive,selectionDragMoved;
    ReadField<Page> runtimeMenuPage;
    BindingField runtimeMenuKeyboardBindings;
    BridgeField multiplayerBridge;
    StatusField interactionStatus;
    struct Economy {EconomyField status;} economy;
    explicit RuntimeWindowState(Model& m):skirmishMatchFinished{m,Op::Finished},
        exitAfterPendingCampaignVideos{m,Op::Pending},campaignMode{m,Op::Campaign},
        resultPresentation{{m,Op::Banner},IntField(m)},runtimeExitRequested(m,Op::Limit,Op::Exit),
        repaintRequested{m},runtimeMenuOpen(m,Op::Menu,Op::SetMenu),
        playerCommunication{BoolField(m,Op::Beacon,Op::SetBeacon)},
        playerLocalCommands{BoolField(m,Op::Follow,Op::SetFollow),BoolField(m,Op::Rally,Op::SetRally)},
        activeSidebarTool(m),selectionDragActive(m,Op::Limit,Op::SetDragActive),
        selectionDragMoved(m,Op::Limit,Op::SetDragMoved),runtimeMenuPage{m,Op::MenuPage},
        runtimeMenuKeyboardBindings{m,{}},multiplayerBridge{m},interactionStatus{m},economy{{m}}{}
};
struct OwnedRows {std::size_t value;std::size_t size()const{return value;}};
struct LegacyOracle {
    Model& m;explicit LegacyOracle(Model& model):m(model){}
    int GetKeyState(WPARAM key){if(key!=VK_CONTROL)throw std::logic_error("old control query");return m.readControl()?0x8000:0;}
    RuntimeUiTimeMs runtimeUiClockNow(RuntimeWindowState&){return m.time();}
    void beginRuntimeResultScorePage(RuntimeWindowState&,RuntimeUiTimeMs t){m.begin(t);}
    bool queueRuntimeCampaignPostScore(RuntimeWindowState&){return m.queue();}
    void DestroyWindow(int){m.destroy();}
    OwnedRows runtimeMatchScoreRows(RuntimeWindowState&){return {m.rows()};}
    int runtimeMenuKeyboardBindingCode(RuntimeWindowState&,const std::string& id,int fallback){return m.binding(id,fallback);}
    int runtimePlayerCurrentKeyCode(WPARAM raw){return m.sample(Input{raw,m.active.repeated});}
    int GetCapture(){return m.get(Op::Capture)!=0?1:0;}
    void ReleaseCapture(){m.release();}
    bool cancelRuntimeActiveSuperWeaponTargetSelection(RuntimeWindowState&,const char* reason){
        return m.cancel(std::string(reason)=="escape"?RuntimeSessionCancelReason::Escape:RuntimeSessionCancelReason::Options);}
    bool dismissRuntimePlacementFromUi(RuntimeWindowState&){return m.dismiss();}
    void setRuntimeMenuOpen(RuntimeWindowState&,bool open){m.set(Op::SetMenu,open);}
    Page runtimeMenuParentPage(Page page){return m.parent(page);}
    void resetRuntimeMenuPage(RuntimeWindowState&,Page page){m.reset(page);}
    void emitRuntimeSound(RuntimeWindowState&,int intent){if(intent!=runtimeSoundIntentMenuOpen&&intent!=runtimeSoundIntentMenuClose)throw std::logic_error("old menu sound");m.menuSound(intent==runtimeSoundIntentMenuOpen);}
    void emitRuntimeSound(RuntimeWindowState&,const char* intent){const std::string text(intent);m.fileSound(text=="save"?RuntimeSessionFileSound::Save:text=="load"?RuntimeSessionFileSound::Load:RuntimeSessionFileSound::Blocked);}
    void InvalidateRect(int,void*,bool erase){if(erase)throw std::logic_error("old erase");m.invalidate();}
    void RedrawWindow(int,void*,void*,unsigned flags){if(flags!=(RDW_INVALIDATE|RDW_NOERASE))throw std::logic_error("old redraw flags");m.redraw();}
    bool dispatchRuntimeSaveLoadFromUi(RuntimeWindowState&,bool save,std::string& error){return m.dispatch(save,error);}
    std::string runtimeDefaultSavePath(RuntimeWindowState&){return m.path();}
    // This compiled oracle is the complete original continuous consumer, including
    // its one Control sample. Individual entry tests below use the same old pieces.
    int chain(RuntimeWindowState* state,int window,WPARAM wParam,LPARAM lParam) {
        if (state->skirmishMatchFinished
            && (wParam == VK_RETURN || wParam == 'R' || wParam == VK_ESCAPE)) {
            // The persistent menu host is recreated by runRuntimeWindow after
            // this mission window closes. Campaign progress already points at
            // the next mission on victory or the same mission on defeat, so
            // the Campaign selector provides both next and retry paths.
            // A victorious mission's authored PostScore movie belongs after
            // this result screen, not at the moment the victory trigger fires.
            // Esc keeps its advertised direct-to-menu behavior; Enter/R
            // confirms the result and starts the optional ending.
            if (state->exitAfterPendingCampaignVideos) {
                return 0;
            }
            if ((lParam & (static_cast<LPARAM>(1) << 30)) != 0) return 0;
            if (state->resultPresentation.bannerVisible
                && (!state->campaignMode || wParam != VK_ESCAPE)) {
                beginRuntimeResultScorePage(*state, runtimeUiClockNow(*state));
                return 0;
            }
            if (state->campaignMode && wParam != VK_ESCAPE
                && queueRuntimeCampaignPostScore(*state)) {
                state->repaintRequested = true;
                return 0;
            }
            state->runtimeExitRequested = true;
            DestroyWindow(window);
            return 0;
        }

        if (state->skirmishMatchFinished) {
            if (!state->campaignMode && !state->resultPresentation.bannerVisible
                && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_PRIOR || wParam == VK_NEXT)) {
                const int lastPage = std::max(0, (static_cast<int>(runtimeMatchScoreRows(*state).size()) - 1) / 8);
                state->resultPresentation.scorePage = std::clamp(state->resultPresentation.scorePage
                    + ((wParam == VK_LEFT || wParam == VK_PRIOR) ? -1 : 1), 0, lastPage);
                state->repaintRequested = true;
            }
            return 0;
        }
        const bool controlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const WPARAM optionsKey = static_cast<WPARAM>(
            runtimeMenuKeyboardBindingCode(
                *state,
                "Options",
                VK_ESCAPE
            )
        );
        const bool menuNavigationKey =
            (runtimePlayerCurrentKeyCode(wParam) == static_cast<int>(optionsKey))
            || (state->runtimeMenuOpen && wParam == VK_ESCAPE);
        if (menuNavigationKey) {
            if ((lParam & (static_cast<LPARAM>(1) << 30)) != 0) return 0;
            if (!state->runtimeMenuOpen && (state->playerCommunication.beaconTargeting
                || state->playerLocalCommands.followTargeting || state->playerLocalCommands.rallyTargeting
                || state->activeSidebarTool != RuntimeSidebarTool::None)) {
                state->playerCommunication.beaconTargeting = false;
                state->playerLocalCommands.followTargeting = state->playerLocalCommands.rallyTargeting = false;
                state->activeSidebarTool = RuntimeSidebarTool::None;
                state->selectionDragActive = state->selectionDragMoved = false;
                if (GetCapture() == window) ReleaseCapture();
                state->repaintRequested = true; InvalidateRect(window, nullptr, FALSE); return 0;
            }
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            if (!state->runtimeMenuOpen
                && cancelRuntimeActiveSuperWeaponTargetSelection(
                    *state,
                    wParam == VK_ESCAPE ? "escape" : "options"
                )) {
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }
            if (!state->runtimeMenuOpen && dismissRuntimePlacementFromUi(*state)) {
                state->repaintRequested = true;
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            bool openingMenu = false;
            if (!state->runtimeMenuOpen) {
                setRuntimeMenuOpen(*state, true);
                openingMenu = true;
            } else if (state->runtimeMenuPage != RuntimeMenuPage::Main) {
                resetRuntimeMenuPage(
                    *state,
                    runtimeMenuParentPage(state->runtimeMenuPage)
                );
            } else {
                setRuntimeMenuOpen(*state, false);
            }
            emitRuntimeSound(
                *state,
                openingMenu
                    ? runtimeSoundIntentMenuOpen
                    : runtimeSoundIntentMenuClose
            );
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        const int actualBindingCode = runtimePlayerCurrentKeyCode(wParam);
        const bool configuredGameplayBinding = std::any_of(
            state->runtimeMenuKeyboardBindings.begin(), state->runtimeMenuKeyboardBindings.end(),
            [&](const auto& binding) {
                return binding.first != "Options" && binding.second == actualBindingCode;
            });
        if (!configuredGameplayBinding
            && ((controlDown && (wParam == 'S' || wParam == 'L'))
                || wParam == VK_F5 || wParam == VK_F9)) {
            std::string errorMessage;
            const bool save =
                (controlDown && wParam == 'S') || wParam == VK_F5;
            const bool ok = dispatchRuntimeSaveLoadFromUi(
                *state,
                save,
                errorMessage
            );
            const bool multiplayer = state->multiplayerBridge != nullptr;
            state->interactionStatus = ok
                ? (multiplayer
                    ? std::string(save
                        ? "multiplayer save proposed"
                        : "multiplayer load proposed")
                    : std::string(save ? "saved " : "loaded ")
                        + runtimeDefaultSavePath(*state))
                : std::string(save ? "save failed: " : "load failed: ")
                    + errorMessage;
            if (!multiplayer) {
                state->economy.status = state->interactionStatus;
            }
            emitRuntimeSound(
                *state,
                ok ? (save ? "save" : "load") : "blocked"
            );
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return 1;
    }
    int result(RuntimeWindowState* state,int window,WPARAM wParam,LPARAM lParam) {
        if (state->skirmishMatchFinished
            && (wParam == VK_RETURN || wParam == 'R' || wParam == VK_ESCAPE)) {
            // The persistent menu host is recreated by runRuntimeWindow after
            // this mission window closes. Campaign progress already points at
            // the next mission on victory or the same mission on defeat, so
            // the Campaign selector provides both next and retry paths.
            // A victorious mission's authored PostScore movie belongs after
            // this result screen, not at the moment the victory trigger fires.
            // Esc keeps its advertised direct-to-menu behavior; Enter/R
            // confirms the result and starts the optional ending.
            if (state->exitAfterPendingCampaignVideos) {
                return 0;
            }
            if ((lParam & (static_cast<LPARAM>(1) << 30)) != 0) return 0;
            if (state->resultPresentation.bannerVisible
                && (!state->campaignMode || wParam != VK_ESCAPE)) {
                beginRuntimeResultScorePage(*state, runtimeUiClockNow(*state));
                return 0;
            }
            if (state->campaignMode && wParam != VK_ESCAPE
                && queueRuntimeCampaignPostScore(*state)) {
                state->repaintRequested = true;
                return 0;
            }
            state->runtimeExitRequested = true;
            DestroyWindow(window);
            return 0;
        }

        if (state->skirmishMatchFinished) {
            if (!state->campaignMode && !state->resultPresentation.bannerVisible
                && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_PRIOR || wParam == VK_NEXT)) {
                const int lastPage = std::max(0, (static_cast<int>(runtimeMatchScoreRows(*state).size()) - 1) / 8);
                state->resultPresentation.scorePage = std::clamp(state->resultPresentation.scorePage
                    + ((wParam == VK_LEFT || wParam == VK_PRIOR) ? -1 : 1), 0, lastPage);
                state->repaintRequested = true;
            }
            return 0;
        }
        return 1;
    }
    // Control belongs to the caller, outside the standalone Options entry.
    // Its original line remains executed verbatim in chain() above.
    int options(RuntimeWindowState* state,int window,WPARAM wParam,LPARAM lParam) {
        const WPARAM optionsKey = static_cast<WPARAM>(
            runtimeMenuKeyboardBindingCode(
                *state,
                "Options",
                VK_ESCAPE
            )
        );
        const bool menuNavigationKey =
            (runtimePlayerCurrentKeyCode(wParam) == static_cast<int>(optionsKey))
            || (state->runtimeMenuOpen && wParam == VK_ESCAPE);
        if (menuNavigationKey) {
            if ((lParam & (static_cast<LPARAM>(1) << 30)) != 0) return 0;
            if (!state->runtimeMenuOpen && (state->playerCommunication.beaconTargeting
                || state->playerLocalCommands.followTargeting || state->playerLocalCommands.rallyTargeting
                || state->activeSidebarTool != RuntimeSidebarTool::None)) {
                state->playerCommunication.beaconTargeting = false;
                state->playerLocalCommands.followTargeting = state->playerLocalCommands.rallyTargeting = false;
                state->activeSidebarTool = RuntimeSidebarTool::None;
                state->selectionDragActive = state->selectionDragMoved = false;
                if (GetCapture() == window) ReleaseCapture();
                state->repaintRequested = true; InvalidateRect(window, nullptr, FALSE); return 0;
            }
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            if (!state->runtimeMenuOpen
                && cancelRuntimeActiveSuperWeaponTargetSelection(
                    *state,
                    wParam == VK_ESCAPE ? "escape" : "options"
                )) {
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }
            if (!state->runtimeMenuOpen && dismissRuntimePlacementFromUi(*state)) {
                state->repaintRequested = true;
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            bool openingMenu = false;
            if (!state->runtimeMenuOpen) {
                setRuntimeMenuOpen(*state, true);
                openingMenu = true;
            } else if (state->runtimeMenuPage != RuntimeMenuPage::Main) {
                resetRuntimeMenuPage(
                    *state,
                    runtimeMenuParentPage(state->runtimeMenuPage)
                );
            } else {
                setRuntimeMenuOpen(*state, false);
            }
            emitRuntimeSound(
                *state,
                openingMenu
                    ? runtimeSoundIntentMenuOpen
                    : runtimeSoundIntentMenuClose
            );
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return 1;
    }
    int saveLoad(RuntimeWindowState* state,int window,WPARAM wParam,bool controlDown) {
        const int actualBindingCode = runtimePlayerCurrentKeyCode(wParam);
        const bool configuredGameplayBinding = std::any_of(
            state->runtimeMenuKeyboardBindings.begin(), state->runtimeMenuKeyboardBindings.end(),
            [&](const auto& binding) {
                return binding.first != "Options" && binding.second == actualBindingCode;
            });
        if (!configuredGameplayBinding
            && ((controlDown && (wParam == 'S' || wParam == 'L'))
                || wParam == VK_F5 || wParam == VK_F9)) {
            std::string errorMessage;
            const bool save =
                (controlDown && wParam == 'S') || wParam == VK_F5;
            const bool ok = dispatchRuntimeSaveLoadFromUi(
                *state,
                save,
                errorMessage
            );
            const bool multiplayer = state->multiplayerBridge != nullptr;
            state->interactionStatus = ok
                ? (multiplayer
                    ? std::string(save
                        ? "multiplayer save proposed"
                        : "multiplayer load proposed")
                    : std::string(save ? "saved " : "loaded ")
                        + runtimeDefaultSavePath(*state))
                : std::string(save ? "save failed: " : "load failed: ")
                    + errorMessage;
            if (!multiplayer) {
                state->economy.status = state->interactionStatus;
            }
            emitRuntimeSound(
                *state,
                ok ? (save ? "save" : "load") : "blocked"
            );
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return 1;
    }
};

struct Scope {
    Model& m;Input old;
    Scope(Model& model,Input in):m(model),old(model.active){m.active=in;}
    ~Scope(){m.active=old;}
};
bool invoke(Model& m,Entry entry,Input& carrier){
    const Input owned=carrier;Scope scope(m,owned);
    m.trace.push_back({Op::Enter,static_cast<int>(entry),0,{},owned.rawKey,owned.repeated});
    bool result=false;
    if(m.modern){
        Host h(m);
        if(entry==Entry::Result)result=handleRuntimeResultSessionKey(carrier,h);
        else if(entry==Entry::Options)result=handleRuntimeOptionsSessionKey(carrier,h);
        else if(entry==Entry::SaveLoad)result=handleRuntimeSaveLoadSessionKey(carrier,m.control,h);
        else {
            // Mirrors the producer: original wParam is stable across all entries.
            if(handleRuntimeResultSessionKey(owned,h))result=true;
            else {const bool controlDown=m.readControl();
                result=handleRuntimeOptionsSessionKey(owned,h)
                    ||handleRuntimeSaveLoadSessionKey(owned,controlDown,h);}
        }
    }else{
        LegacyOracle old(m);RuntimeWindowState state(m);
        const LPARAM repeat=owned.repeated?(LPARAM{1}<<30):0;
        if(entry==Entry::Result)result=old.result(&state,1,owned.rawKey,repeat)==0;
        else if(entry==Entry::Options)result=old.options(&state,1,owned.rawKey,repeat)==0;
        else if(entry==Entry::SaveLoad)result=old.saveLoad(&state,1,owned.rawKey,m.control)==0;
        else result=old.chain(&state,1,owned.rawKey,repeat)==0;
    }
    m.trace.push_back({Op::Return,result,0,{},owned.rawKey,owned.repeated});return result;
}
using Setup=std::function<void(Model&,Input&)>;
struct Run {
    State state;std::vector<State> retired;std::vector<Event> trace;
    bool returned=false,result=false,control=false;std::int32_t modifiers=0;
    std::size_t samples=0;Op exception=Op::Limit;
};
Run run(bool modern,Entry entry,Input input,const State& state,const Setup& setup){
    Model m;m.state=state;m.modern=modern;if(setup)setup(m,input);
    const Input original=input;bool returned=false,result=false;Op exception=Op::Limit;
    try{result=invoke(m,entry,input);returned=true;}
    catch(const SyntheticFailure& e){exception=e.op;m.trace.push_back({Op::Caught,static_cast<std::int64_t>(e.op),0,{},original.rawKey,original.repeated});}
    return {m.state,m.retired,m.trace,returned,result,m.control,m.modifiers,m.sampleAt,exception};
}
struct Stats {
    std::size_t checks=0,cases=0,traceRows=0,samples=0,ownedChecks=0;
    std::size_t resultCases=0,optionsCases=0,fileCases=0,chainCases=0,dynamicCases=0,nestedCases=0,exceptionCases=0;
    void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
    Run compare(Entry entry,Input input,const State& state=State{},const Setup& setup={}){
        const auto old=run(false,entry,input,state,setup),actual=run(true,entry,input,state,setup);
        ++cases;traceRows+=actual.trace.size();samples+=actual.samples;
        if(entry==Entry::Result)++resultCases;else if(entry==Entry::Options)++optionsCases;
        else if(entry==Entry::SaveLoad)++fileCases;else ++chainCases;
        check(old.state==actual.state,"full state differs");check(old.retired==actual.retired,"owned retired children differ");
        check(old.trace==actual.trace,"complete six-field ordered trace differs");
        check(old.returned==actual.returned&&old.result==actual.result&&old.exception==actual.exception,"return/exception differs");
        check(old.control==actual.control&&old.modifiers==actual.modifiers&&old.samples==actual.samples,"controlled input observations differ");
        return actual;
    }
};
bool has(const Run& r,Op op){return std::any_of(r.trace.begin(),r.trace.end(),[op](const Event& e){return e.op==op;});}
std::size_t count(const Run& r,Op op){return static_cast<std::size_t>(std::count_if(r.trace.begin(),r.trace.end(),[op](const Event& e){return e.op==op;}));}
Setup once(Op op,std::function<void(Model&)> effect,unsigned occurrence=1){
    return [op,effect=std::move(effect),occurrence](Model& model,Input&){
        model.hook=[op,effect,occurrence](Model& m,Op got){
            if(m.armed&&got==op&&m.calls[static_cast<std::size_t>(got)]==occurrence){m.armed=false;effect(m);}
        };
    };
}
void resultCases(Stats& s){
    const std::array<std::uint64_t,12> keys{VK_RETURN,'R',VK_ESCAPE,VK_LEFT,VK_RIGHT,VK_PRIOR,VK_NEXT,
        'r','?',0,(std::uint64_t{1}<<32)|VK_RETURN,std::numeric_limits<std::uint64_t>::max()};
    for(int mode=0;mode<12;++mode)for(const auto raw:keys)for(bool repeated:{false,true}){
        State state;state.finished=mode!=0;state.banner=mode==1||mode==3;
        state.pending=mode==2;state.campaign=mode==3||mode==4||mode==5;
        state.queued=mode==4;state.rows=mode==6?0:mode==7?1:mode==8?8:mode==9?9:17;
        state.score=mode==10?-2:mode==11?99:1;
        const auto r=s.compare(Entry::Result,{raw,repeated},state);
        s.check(r.result==state.finished,"finished consumes every raw key");
        if(repeated)s.check(!has(r,Op::Now)&&!has(r,Op::QueueScore)&&!has(r,Op::Destroy),"repeat continue does not start effects");
        if(has(r,Op::BeginScore))s.check(count(r,Op::Now)==1,"score begins with exactly one late clock sample");
        if(has(r,Op::Destroy))s.check(r.state.exit&&r.trace.back().op==Op::Return,"exit store precedes close and immediate return");
    }
    for(const auto rows:std::array<std::size_t,7>{0,1,7,8,9,16,static_cast<std::size_t>(std::numeric_limits<int>::max())})
        for(const auto raw:std::array<std::uint64_t,4>{VK_LEFT,VK_RIGHT,VK_PRIOR,VK_NEXT}){
            State state;state.finished=true;state.rows=rows;s.compare(Entry::Result,{raw,true},state);
        }
    for(const auto page:std::array<std::int32_t,4>{-4,0,2,99})
        for(const auto raw:std::array<std::uint64_t,4>{VK_LEFT,VK_RIGHT,VK_PRIOR,VK_NEXT}){
            State state;state.finished=true;state.score=page;s.compare(Entry::Result,{raw,true},state);
        }
    State low;low.finished=true;low.score=std::numeric_limits<int>::min();
    s.compare(Entry::Result,{VK_RIGHT,true},low);s.compare(Entry::Result,{VK_NEXT,true},low);
    State high;high.finished=true;high.score=std::numeric_limits<int>::max();
    s.compare(Entry::Result,{VK_LEFT,true},high);s.compare(Entry::Result,{VK_PRIOR,true},high);
    // INT_MIN-1, INT_MAX+1 and unrepresentable row-count arithmetic retain their original uncovered domain.
}
void optionsCases(Stats& s){
    const std::array<std::uint64_t,6> keys{VK_ESCAPE,0x7C,'?',0,(std::uint64_t{1}<<32)|VK_ESCAPE,std::numeric_limits<std::uint64_t>::max()};
    for(int mode=0;mode<14;++mode)for(const auto raw:keys)for(bool repeated:{false,true}){
        State state;state.options=mode==13?0x7C:VK_ESCAPE;
        state.menu=mode==1||mode==2;state.page=mode==2?Page::Keyboard:Page::Main;
        state.beacon=mode==3;state.follow=mode==4;state.rally=mode==5;state.tool=mode==6?2:0;
        state.cancel=mode==7;state.dismiss=mode==8;state.capture=mode!=9;
        if(mode==10)state.options=std::numeric_limits<std::int32_t>::min();
        if(mode==11)state.options=std::numeric_limits<std::int32_t>::max();
        if(mode==12)state.options=-1;
        const auto r=s.compare(Entry::Options,{raw,repeated},state);
        s.check(count(r,Op::Binding)==1&&count(r,Op::Sample)==1&&!has(r,Op::Control),"Options owns binding then fresh sample, not Control");
        if(repeated)s.check(!has(r,Op::Capture)&&!has(r,Op::SetMenu)&&!has(r,Op::SetBeacon),"repeat Options has no cancellation effects");
    }
    State target;target.beacon=target.follow=target.rally=true;target.tool=2;
    const auto r=s.compare(Entry::Options,{VK_ESCAPE,false},target);
    const std::array order{Op::SetBeacon,Op::SetRally,Op::SetFollow,Op::ClearTool,Op::SetDragMoved,Op::SetDragActive,Op::Capture,Op::Release,Op::Repaint,Op::Invalidate};
    std::vector<Op> actual;for(const auto& e:r.trace)if((e.op>=Op::SetBeacon&&e.op<=Op::Release)||e.op==Op::Repaint||e.op==Op::Invalidate)actual.push_back(e.op);
    s.check(std::equal(order.begin(),order.end(),actual.begin(),actual.end()),"exact original chained clearing order before capture");
    State escape;escape.options=0x1B;
    const auto high=s.compare(Entry::Options,{(std::uint64_t{1}<<32)|VK_ESCAPE,false},escape);
    const auto reason=std::find_if(high.trace.begin(),high.trace.end(),[](const Event& e){return e.op==Op::CancelWeapon;});
    s.check(reason!=high.trace.end()&&reason->a==static_cast<int>(RuntimeSessionCancelReason::Options),"full raw high Escape uses options reason");
}
void fileCases(Stats& s){
    const std::array<std::uint64_t,10> keys{'S','L','s','l',VK_F5,VK_F9,0,'?',
        (std::uint64_t{1}<<32)|VK_F5,std::numeric_limits<std::uint64_t>::max()};
    for(int mode=0;mode<6;++mode)for(const auto raw:keys)for(bool repeated:{false,true}){
        State state;state.dispatchOk=mode!=1&&mode!=3;state.mp=mode==2||mode==3;
        state.error=mode==5?"owned utf8 error \xE9\x94\x99":"original error";
        const auto r=s.compare(Entry::SaveLoad,{raw,repeated},state,[mode](Model& m,Input&){m.control=mode==4;m.modifiers=mode==4?0x200:0;});
        s.check(count(r,Op::Sample)==1&&count(r,Op::Bindings)==1&&!has(r,Op::Control),"SaveLoad samples and queries bindings once");
        if(has(r,Op::Dispatch)){
            s.check(count(r,Op::MP)==1,"MP sampled only after dispatch");
            s.check(has(r,Op::Path)==(state.dispatchOk&&!state.mp),"path is lazy for successful offline dispatch only");
            s.check(has(r,Op::EconomyStatus)==!state.mp,"post-dispatch route decides economy status");
        }
    }
    for(int binding=0;binding<7;++binding)for(const auto raw:std::array<std::uint64_t,4>{'S','L',VK_F5,VK_F9}){
        State state;const auto code=static_cast<std::int32_t>(raw)|0x200;
        if(binding==0)state.bindings={{"Options",code}};
        if(binding==1)state.bindings={{"options",code}};
        if(binding==2)state.bindings={{"OPTIONS",code}};
        if(binding==3)state.bindings={{"",code}};
        if(binding==4)state.bindings={{"Options",code},{"other",code}};
        if(binding==5)state.bindings={{"first",code+1},{"second",code}};
        if(binding==6)state.bindings={{"other",code+1}};
        const auto r=s.compare(Entry::SaveLoad,{raw,true},state,[](Model& m,Input&){m.control=true;m.modifiers=0x200;});
        s.check(has(r,Op::Dispatch)==(binding==0||binding==6),"exact case-sensitive Options exclusion and configured binding priority");
    }
}
void chainCases(Stats& s){
    for(std::int32_t modifiers=0;modifiers<8;++modifiers)for(const auto raw:std::array<std::uint64_t,6>{'S','L',VK_F5,VK_F9,VK_ESCAPE,'?'})for(bool repeated:{false,true}){
        const auto r=s.compare(Entry::Chain,{raw,repeated},State{},[modifiers](Model& m,Input&){
            m.control=(modifiers&2)!=0;m.modifiers=((modifiers&1)?0x100:0)|((modifiers&2)?0x200:0)|((modifiers&4)?0x400:0);
        });
        s.check(count(r,Op::Control)==1,"producer Control sampled exactly once after unconsumed Result");
        if(!has(r,Op::Dispatch)&&!has(r,Op::MenuSound))s.check(!r.state.destroyed,"unhandled input does not close a window model");
    }
    State terminal;terminal.finished=true;
    const auto r=s.compare(Entry::Chain,{'?',false},terminal);
    s.check(r.result&&!has(r,Op::Control)&&!has(r,Op::Binding),"terminal consumers precede Control and Options");
    State collision;collision.options=VK_F5;
    const auto c=s.compare(Entry::Chain,{VK_F5,true},collision);
    s.check(c.result&&count(c,Op::Sample)==1&&!has(c,Op::Bindings)&&!has(c,Op::Dispatch),"rebound Options repeat wins SaveLoad");
}
void dynamicCases(Stats& s){
    const auto compare=[&](Entry e,Input in,State state,const Setup& setup){++s.dynamicCases;return s.compare(e,in,state,setup);};
    State finished;
    const auto second=compare(Entry::Result,{'?',false},finished,once(Op::Finished,[](Model& m){m.state.finished=true;}));
    s.check(second.result&&count(second,Op::Finished)==2,"second finished read cannot be cached");
    finished.finished=true;
    const auto changed=compare(Entry::Result,{'?',false},finished,once(Op::Finished,[](Model& m){m.state.finished=false;}));
    s.check(!changed.result&&count(changed,Op::Finished)==2,"first finished read does not force second result");
    State banner;banner.finished=banner.banner=true;
    const auto clock=compare(Entry::Result,{VK_RETURN,false},banner,once(Op::Now,[](Model& m){m.state.now=0;}));
    const auto begin=std::find_if(clock.trace.begin(),clock.trace.end(),[](const Event& e){return e.op==Op::BeginScore;});
    s.check(begin!=clock.trace.end()&&begin->text=="18446744073709551614","clock value is owned across callback");
    State open;
    const auto released=compare(Entry::Options,{VK_ESCAPE,false},open,once(Op::Release,[](Model& m){m.swapRoot();m.state.menu=true;m.state.page=Page::Keyboard;}));
    s.check(!has(released,Op::CancelWeapon)&&!has(released,Op::Dismiss)&&has(released,Op::ResetPage),"release callback reborrows current menu root");
    const auto cancel=compare(Entry::Options,{VK_ESCAPE,false},open,once(Op::CancelWeapon,[](Model& m){m.state.menu=true;}));
    s.check(!has(cancel,Op::Dismiss),"fresh menu read after unsuccessful SW cancellation");
    State parentState;parentState.menu=true;parentState.page=Page::Keyboard;
    const auto parent=compare(Entry::Options,{VK_ESCAPE,false},parentState,
        once(Op::MenuPage,[](Model& m){m.state.page=Page::Sound;}));
    const auto parentQuery=std::find_if(parent.trace.begin(),parent.trace.end(),[](const Event& e){return e.op==Op::ParentPage;});
    s.check(parentQuery!=parent.trace.end()&&parentQuery->a==static_cast<int>(Page::Sound),"parent argument rereads current page");
    const auto binding=compare(Entry::Options,{VK_ESCAPE,false},open,once(Op::Binding,[](Model& m){m.state.options=0x7C;}));
    s.check(binding.result&&has(binding,Op::SetMenu),"Options stores original binding but samples fresh key");
    const auto sampled=compare(Entry::Chain,{VK_F5,false},State{},once(Op::Sample,[](Model& m){m.samples={0x1B,0x7C};m.state.bindings={{"changed",0x7C}};}));
    s.check(count(sampled,Op::Sample)==2&&!has(sampled,Op::Dispatch),"SaveLoad fresh sample is not the Options sample");
    const auto controls=compare(Entry::Chain,{'S',false},State{},[](Model& m,Input&){
        m.control=true;m.modifiers=0x200;m.hook=[](Model& model,Op op){if(model.armed&&op==Op::Binding){model.armed=false;model.control=false;}};
    });
    s.check(controls.state.saved&&controls.state.dispatches==1&&count(controls,Op::Control)==1,"original Control snapshot survives later callback");
    const auto mp=compare(Entry::SaveLoad,{VK_F5,false},State{},once(Op::Dispatch,[](Model& m){m.swapRoot();m.state.mp=true;}));
    s.check(mp.state.status=="multiplayer save proposed"&&!has(mp,Op::Path)&&!has(mp,Op::EconomyStatus),"route sampled after dispatch/root swap");
    State oldMp;oldMp.mp=true;
    const auto offline=compare(Entry::SaveLoad,{VK_F9,true},oldMp,once(Op::Dispatch,[](Model& m){m.state.mp=false;m.state.path="fresh/path";}));
    s.check(offline.state.status=="loaded fresh/path"&&has(offline,Op::EconomyStatus),"post-dispatch offline status queries fresh path");
    State failure;failure.dispatchOk=false;failure.error="owned original";
    const auto error=compare(Entry::SaveLoad,{VK_F5,false},failure,once(Op::Dispatch,[](Model& m){m.state.error="later error";}));
    s.check(error.state.status=="save failed: owned original"&&!has(error,Op::Path),"owned error survives dispatch callback");
    const auto path=compare(Entry::SaveLoad,{VK_F5,false},State{},once(Op::Path,[](Model& m){m.swapRoot();m.state.path="later/path";}));
    s.check(path.state.status=="saved private/save.ini","owned path survives root child swap");++s.ownedChecks;
    const auto status=compare(Entry::SaveLoad,{VK_F5,false},State{},once(Op::SetStatus,[](Model& m){m.state.status="callback status";m.state.mp=true;}));
    s.check(status.state.economy=="callback status"&&has(status,Op::EconomyStatus),"fresh status with retained post-dispatch MP decision");
    for(const auto e:std::array<Entry,4>{Entry::Result,Entry::Options,Entry::SaveLoad,Entry::Chain}){
        State state;state.finished=e==Entry::Result;
        const std::uint64_t raw=e==Entry::Result?'R':e==Entry::Options?VK_ESCAPE:VK_F5;
        const Op trigger=e==Entry::Result||e==Entry::Chain?Op::Finished:e==Entry::Options?Op::Binding:Op::Sample;
        const auto r=compare(e,{raw,false},state,[trigger](Model& m,Input& carrier){
            m.hook=[&carrier,trigger](Model& model,Op op){if(model.armed&&op==trigger){model.armed=false;carrier={VK_F9,true};}};
        });
        if(e==Entry::Result)s.check(r.state.destroyed,"Result owns its input copy");
        if(e==Entry::Options)s.check(r.state.menu,"Options owns its input copy");
        if(e==Entry::SaveLoad||e==Entry::Chain)s.check(r.state.saved,"SaveLoad/combined producer owns original key");
        ++s.ownedChecks;
    }
}
void nestedAndExceptions(Stats& s){
    struct Point{Entry entry;std::uint64_t raw;Op op;int mode;};
    const std::array points{
        Point{Entry::Result,VK_RETURN,Op::BeginScore,1},Point{Entry::Result,VK_RETURN,Op::Destroy,2},
        Point{Entry::Options,VK_ESCAPE,Op::Release,0},Point{Entry::Options,VK_ESCAPE,Op::CancelWeapon,0},
        Point{Entry::Options,VK_ESCAPE,Op::SetMenu,0},Point{Entry::SaveLoad,VK_F5,Op::Dispatch,0},
        Point{Entry::SaveLoad,VK_F5,Op::Path,0},Point{Entry::SaveLoad,VK_F5,Op::SetStatus,0}};
    for(const auto& p:points){
        State state;state.finished=p.mode!=0;state.banner=p.mode==1;++s.nestedCases;
        const auto r=s.compare(p.entry,{p.raw,false},state,once(p.op,[](Model& m){m.swapRoot();Input nested{VK_ESCAPE,true};(void)invoke(m,Entry::Options,nested);}));
        s.check(r.retired.size()==1&&count(r,Op::Enter)==2,"same-root synchronous nested controller");++s.ownedChecks;
    }
    const std::array errors{
        Point{Entry::Result,VK_RETURN,Op::Finished,1},Point{Entry::Result,VK_RETURN,Op::Pending,1},
        Point{Entry::Result,VK_RETURN,Op::Banner,1},Point{Entry::Result,VK_RETURN,Op::Campaign,1},
        Point{Entry::Result,VK_RETURN,Op::Now,1},Point{Entry::Result,VK_RETURN,Op::BeginScore,1},
        Point{Entry::Result,VK_RETURN,Op::QueueScore,3},Point{Entry::Result,VK_RETURN,Op::Exit,2},
        Point{Entry::Result,VK_RETURN,Op::Destroy,2},Point{Entry::Result,VK_RIGHT,Op::Rows,2},
        Point{Entry::Result,VK_RIGHT,Op::ScorePage,2},Point{Entry::Result,VK_RIGHT,Op::SetScorePage,2},
        Point{Entry::Chain,VK_F5,Op::Control,0},Point{Entry::Options,VK_ESCAPE,Op::Binding,0},
        Point{Entry::Options,VK_ESCAPE,Op::Sample,0},Point{Entry::Options,VK_ESCAPE,Op::Menu,0},
        Point{Entry::Options,VK_ESCAPE,Op::Beacon,0},Point{Entry::Options,VK_ESCAPE,Op::Follow,0},
        Point{Entry::Options,VK_ESCAPE,Op::Rally,0},Point{Entry::Options,VK_ESCAPE,Op::Tool,0},
        Point{Entry::Options,VK_ESCAPE,Op::SetBeacon,4},Point{Entry::Options,VK_ESCAPE,Op::SetRally,4},
        Point{Entry::Options,VK_ESCAPE,Op::SetFollow,4},Point{Entry::Options,VK_ESCAPE,Op::ClearTool,4},
        Point{Entry::Options,VK_ESCAPE,Op::SetDragMoved,4},Point{Entry::Options,VK_ESCAPE,Op::SetDragActive,4},
        Point{Entry::Options,VK_ESCAPE,Op::Capture,0},Point{Entry::Options,VK_ESCAPE,Op::Release,0},
        Point{Entry::Options,VK_ESCAPE,Op::CancelWeapon,0},Point{Entry::Options,VK_ESCAPE,Op::Dismiss,0},
        Point{Entry::Options,VK_ESCAPE,Op::MenuPage,5},Point{Entry::Options,VK_ESCAPE,Op::ParentPage,5},
        Point{Entry::Options,VK_ESCAPE,Op::ResetPage,5},Point{Entry::Options,VK_ESCAPE,Op::SetMenu,0},
        Point{Entry::Options,VK_ESCAPE,Op::MenuSound,0},Point{Entry::Options,VK_ESCAPE,Op::Repaint,0},
        Point{Entry::Options,VK_ESCAPE,Op::Redraw,0},Point{Entry::Options,VK_ESCAPE,Op::Invalidate,4},
        Point{Entry::SaveLoad,VK_F5,Op::Bindings,0},Point{Entry::SaveLoad,VK_F5,Op::Dispatch,0},
        Point{Entry::SaveLoad,VK_F5,Op::MP,0},Point{Entry::SaveLoad,VK_F5,Op::Path,0},
        Point{Entry::SaveLoad,VK_F5,Op::SetStatus,0},Point{Entry::SaveLoad,VK_F5,Op::Status,0},
        Point{Entry::SaveLoad,VK_F5,Op::EconomyStatus,0},Point{Entry::SaveLoad,VK_F5,Op::FileSound,0}};
    for(const auto& p:errors){
        State state;state.finished=p.mode==1||p.mode==2||p.mode==3;state.banner=p.mode==1;
        state.campaign=p.mode==3;state.beacon=p.mode==4;state.menu=p.mode==5;
        if(p.mode==5)state.page=Page::Keyboard;
        ++s.exceptionCases;
        const auto r=s.compare(p.entry,{p.raw,false},state,[&p](Model& m,Input&){m.throwAt=p.op;});
        s.check(!r.returned&&r.exception==p.op,"primitive exception propagates without post-error effects");
    }
}
} // namespace

int main(){
    Stats s;
    try{
        resultCases(s);optionsCases(s);fileCases(s);chainCases(s);dynamicCases(s);nestedAndExceptions(s);
        std::cout<<"session-key contract checks="<<s.checks<<" differentialCases="<<s.cases
            <<" resultCases="<<s.resultCases<<" optionsCases="<<s.optionsCases<<" saveLoadCases="<<s.fileCases
            <<" chainCases="<<s.chainCases<<" dynamicCases="<<s.dynamicCases<<" nestedCases="<<s.nestedCases
            <<" exceptionCases="<<s.exceptionCases<<" sourceReads="<<s.samples<<" orderedTraceRows="<<s.traceRows
            <<" ownedChecks="<<s.ownedChecks<<" modifierCombinations=8 oldAdapterTrace=all-six-fields-equal"
            <<" frozenContinuousConsumer=1 windowsHeader=absent nativeKeyAdapter=not-run"
            <<" actualWindowClose=model-only actualPersistence=not-covered runningMPFileHandshake=not-run"
            <<" originalSignedOverflowDomain=not-covered rootSwapModel=only failures=0\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"session-key contract failure: "<<error.what()<<'\n';return 1;}
}
