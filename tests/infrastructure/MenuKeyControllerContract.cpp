#include "ui/input/RuntimeMenuKeyController.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_MINWINDEF_)
#error RuntimeMenuKeyController.h must not include Windows headers
#endif

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<std::underlying_type_t<RuntimeMenuPage>, int>);
static_assert(std::is_same_v<std::underlying_type_t<RuntimeMenuAction>, int>);
static_assert(std::is_same_v<decltype(RuntimeMenuKeyInput{}.rawKey), std::uint64_t>);
static_assert(std::is_same_v<decltype(RuntimeMenuKeyInput{}.repeated), bool>);
static_assert(RuntimeMenuKeyInput{}.rawKey == 0 && !RuntimeMenuKeyInput{}.repeated);
static_assert(std::numeric_limits<int>::digits == 31);
static_assert(static_cast<int>(RuntimeMenuPage::Main) == 0);
static_assert(static_cast<int>(RuntimeMenuPage::ExitConfirm) == 5);
static_assert(static_cast<int>(RuntimeMenuAction::None) == 0);
static_assert(static_cast<int>(RuntimeMenuAction::KeyboardResetAll) == 35);
static_assert(runtimeMenuKeyModifierOnly(0x10) && runtimeMenuKeyModifierOnly(0x5C));
static_assert(!runtimeMenuKeyModifierOnly(0x1B));
static_assert(!runtimeMenuKeyModifierOnly((std::uint64_t{1} << 32) | 0x10));
static_assert(noexcept(runtimeMenuKeyModifierOnly(0)));
static_assert(!noexcept(handleRuntimeMenuCaptureKey(
    std::declval<const RuntimeMenuKeyInput&>(), std::declval<RuntimeMenuCaptureHost&>())));
static_assert(!noexcept(handleRuntimeMenuHorizontalKey(
    std::declval<const RuntimeMenuKeyInput&>(), std::declval<RuntimeMenuNavigationHost&>())));
static_assert(!noexcept(handleRuntimeMenuNavigationKey(
    std::declval<const RuntimeMenuKeyInput&>(), std::declval<RuntimeMenuNavigationHost&>())));

namespace {
using Page = RuntimeMenuPage;
using Action = RuntimeMenuAction;
using Input = RuntimeMenuKeyInput;
using WPARAM = std::uint64_t;
constexpr WPARAM VK_SHIFT=0x10, VK_LSHIFT=0xA0, VK_RSHIFT=0xA1;
constexpr WPARAM VK_CONTROL=0x11, VK_LCONTROL=0xA2, VK_RCONTROL=0xA3;
constexpr WPARAM VK_MENU=0x12, VK_LMENU=0xA4, VK_RMENU=0xA5, VK_LWIN=0x5B, VK_RWIN=0x5C;
constexpr WPARAM VK_ESCAPE=0x1B, VK_BACK=0x08, VK_LEFT=0x25, VK_RIGHT=0x27;
constexpr WPARAM VK_UP=0x26, VK_DOWN=0x28, VK_HOME=0x24, VK_END=0x23;
constexpr WPARAM VK_PRIOR=0x21, VK_NEXT=0x22, VK_RETURN=0x0D, VK_SPACE=0x20;
constexpr unsigned RDW_INVALIDATE=1, RDW_NOERASE=32;
constexpr bool FALSE=false;
constexpr int runtimeSoundIntentSidebarButtonClick=7;
struct RECT {};
struct RuntimeMenuButton { Action action=Action::None; };
enum class Entry : int { Capture, Horizontal, Navigation };
enum class Op : std::size_t {
    Enter, Return, Caught, Open, Page, Index, Sample, Count, Default, SetIndex,
    Status, Assign, Client, Actions, ActionValue, Focus, SetFocus, ChangePage,
    Adjust, Hover, Repaint, Redraw, Invalidate, Sound, Activate, Limit
};
// Every event compares all six fields. Owned action queries emit one ActionValue
// event per item, so their complete content is compared without a digest.
struct Event {
    Op op; std::int64_t a; std::int64_t b; bool flag;
    std::uint64_t raw; bool repeated;
    bool operator==(const Event&) const = default;
};
struct Assignment {
    std::int32_t index, code; bool restored;
    bool operator==(const Assignment&) const = default;
};
struct State {
    bool open=true; Page page=Page::Keyboard; std::int32_t index=1, sampled=0x141;
    std::size_t count=4; std::int32_t defaultValue=0x43, focus=0;
    Action hover=Action::Save; bool repaint=false, assignAccepted=true, sliderAccepted=true;
    std::vector<Action> actions{Action::Load,Action::KeyboardCategory,Action::Back};
    std::vector<Assignment> assignments;
    std::vector<Action> activations;
    std::int32_t status=-1, pageTurns=0, categoryTurns=0, sliderTurns=0;
    unsigned clients=0, redraws=0, invalidations=0, sounds=0, epoch=0;
    bool operator==(const State&) const = default;
};
struct SyntheticFailure { Op op; };
struct Model;
using Setup=std::function<void(Model&,Input&)>;
struct Model {
    State state; std::vector<State> retired; std::vector<Event> trace;
    Input active{}; bool useNew=false, armed=true;
    std::function<void(Model&,Op)> hook;
    std::array<unsigned,static_cast<std::size_t>(Op::Limit)> calls{};
    Op throwAt=Op::Limit; unsigned throwOccurrence=1;
    void fire(Op op) {
        const auto n=++calls[static_cast<std::size_t>(op)];
        if(hook) hook(*this,op);
        if(op==throwAt && n==throwOccurrence) throw SyntheticFailure{op};
    }
    void emit(Op op,std::int64_t a=0,std::int64_t b=0,bool flag=false,
              const Input* supplied=nullptr) {
        const Input in=supplied?*supplied:active;
        trace.push_back({op,a,b,flag,in.rawKey,in.repeated}); fire(op);
    }
    std::int64_t read(Op op) {
        std::int64_t value=0;
        switch(op) {
        case Op::Open:value=state.open;break;
        case Op::Page:value=static_cast<int>(state.page);break;
        case Op::Index:value=state.index;break;
        case Op::Focus:value=state.focus;break;
        default:throw std::logic_error("invalid field read");
        }
        emit(op,value); return value;
    }
    void setIndex(std::int32_t v){state.index=v;emit(Op::SetIndex,v);}
    void setFocus(std::int32_t v){state.focus=v;emit(Op::SetFocus,v);}
    void status(RuntimeMenuCaptureStatus s){state.status=static_cast<int>(s);emit(Op::Status,state.status);}
    std::int32_t sample(const Input& input) {
        const auto v=state.sampled;emit(Op::Sample,v,0,false,&input);return v;
    }
    std::size_t bindingCount(){const auto n=state.count;emit(Op::Count,static_cast<std::int64_t>(n));return n;}
    std::int32_t defaultAt(std::int32_t index){const auto v=state.defaultValue;emit(Op::Default,index,v);return v;}
    bool assign(std::int32_t index,std::int32_t code,bool restored) {
        const bool accepted=state.assignAccepted;
        state.assignments.push_back({index,code,restored});
        if(accepted)state.index=-1;
        emit(Op::Assign,index,code,restored);return accepted;
    }
    std::vector<Action> actions() {
        const auto result=state.actions;
        trace.push_back({Op::Actions,static_cast<std::int64_t>(result.size()),0,false,active.rawKey,active.repeated});
        for(std::size_t i=0;i<result.size();++i)
            trace.push_back({Op::ActionValue,static_cast<std::int64_t>(i),static_cast<int>(result[i]),false,active.rawKey,active.repeated});
        fire(Op::Actions);return result;
    }
    void prepare(){++state.clients;emit(Op::Client);}
    void pageChange(std::int32_t delta,bool category) {
        if(category)state.categoryTurns+=delta;else state.pageTurns+=delta;
        emit(Op::ChangePage,delta,0,category);
    }
    bool adjust(Action action,std::int32_t delta) {
        const bool accepted=state.sliderAccepted;
        if(accepted)state.sliderTurns+=delta;
        emit(Op::Adjust,static_cast<int>(action),delta,accepted);return accepted;
    }
    void hover(){state.hover=Action::None;emit(Op::Hover,static_cast<int>(Action::None));}
    void repaint(){state.repaint=true;emit(Op::Repaint,1);}
    void redraw(){++state.redraws;emit(Op::Redraw,RDW_INVALIDATE|RDW_NOERASE);}
    void invalidate(){++state.invalidations;emit(Op::Invalidate,0);}
    void sound(){++state.sounds;emit(Op::Sound,runtimeSoundIntentSidebarButtonClick);}
    void activate(Action action){state.activations.push_back(action);emit(Op::Activate,static_cast<int>(action));}
    void swapRoot() {
        const auto epoch=state.epoch+1;retired.push_back(std::move(state));state=State{};
        state.epoch=epoch;state.actions={Action::Exit};state.focus=99;
    }
};
struct Host final : RuntimeMenuCaptureHost,RuntimeMenuNavigationHost {
    Model& m;
    explicit Host(Model& model):m(model){}
    bool menuOpen()override{return m.read(Op::Open)!=0;}
    Page page()override{return static_cast<Page>(m.read(Op::Page));}
    std::int32_t captureIndex()override{return static_cast<std::int32_t>(m.read(Op::Index));}
    std::int32_t sampleCurrentKeyCode(const Input& in)override{return m.sample(in);}
    std::size_t bindingCount()override{return m.bindingCount();}
    std::int32_t defaultCode(std::int32_t i)override{return m.defaultAt(i);}
    void setCaptureIndex(std::int32_t i)override{m.setIndex(i);}
    void setCaptureStatus(RuntimeMenuCaptureStatus s)override{m.status(s);}
    bool assignBinding(std::int32_t i,std::int32_t c,bool d)override{return m.assign(i,c,d);}
    void prepareClient()override{m.prepare();}
    std::vector<Action> focusableActions()override{return m.actions();}
    std::int32_t focus()override{return static_cast<std::int32_t>(m.read(Op::Focus));}
    void setFocus(std::int32_t i)override{m.setFocus(i);}
    void changeKeyboardPage(std::int32_t d,bool c)override{m.pageChange(d,c);}
    bool adjustSlider(Action a,std::int32_t d)override{return m.adjust(a,d);}
    void clearHover()override{m.hover();}
    void requestRepaint()override{m.repaint();}
    void redraw()override{m.redraw();}
    void invalidate()override{m.invalidate();}
    void buttonSound()override{m.sound();}
    void activate(Action a)override{m.activate(a);}
};
// Old-field proxies map individual legacy reads/stores to the same observable
// primitive boundary. They contain only the root Model, never a child address.
template<class T> struct OldReadField {
    Model& m; Op op;
    operator T()const{return static_cast<T>(m.read(op));}
};
struct OldIndexField : OldReadField<int> {
    explicit OldIndexField(Model& model):OldReadField<int>{model,Op::Index}{}
    void operator=(int v){m.setIndex(v);}
};
struct OldFocusField : OldReadField<int> {
    explicit OldFocusField(Model& model):OldReadField<int>{model,Op::Focus}{}
    void operator=(int v){m.setFocus(v);}
};
struct OldHoverField {
    Model& m;void operator=(Action action){
        if(action!=Action::None)throw std::logic_error("old hover store");
        m.hover();
    }
};
struct OldRepaintField {
    Model& m;void operator=(bool v){if(!v)throw std::logic_error("old repaint store");m.repaint();}
};
struct OldStatusField {
    Model& m;void operator=(const wchar_t* text) {
        const std::wstring_view value(text);
        if(value==L"已取消改键。")m.status(RuntimeMenuCaptureStatus::Cancelled);
        else if(value==L"请同时按下组合键中的字母、数字或功能键。")
            m.status(RuntimeMenuCaptureStatus::ModifierNeedsKey);
        else throw std::logic_error("unexpected old status literal");
    }
};
struct OldBindings { Model& m;std::size_t size()const{return m.bindingCount();} };
struct RuntimeWindowState {
    OldReadField<bool> runtimeMenuOpen;
    OldReadField<Page> runtimeMenuPage;
    OldIndexField runtimeMenuKeyCaptureIndex;
    OldFocusField runtimeMenuFocus;
    OldHoverField runtimeMenuHover;
    OldRepaintField repaintRequested;
    OldStatusField runtimeMenuStatus;
    OldBindings runtimeMenuKeyboardBindings;
    explicit RuntimeWindowState(Model& m):runtimeMenuOpen{m,Op::Open},
        runtimeMenuPage{m,Op::Page},runtimeMenuKeyCaptureIndex(m),runtimeMenuFocus(m),
        runtimeMenuHover{m},repaintRequested{m},runtimeMenuStatus{m},runtimeMenuKeyboardBindings{m}{}
};
struct LegacyOracle {
    // Preserve each frozen std::clamp expression verbatim while making its old
    // scalar focus read observable. No overload is added to namespace ::std.
    struct std {
        template<class T> using vector=::std::vector<T>;
        using size_t=::std::size_t;
        using logic_error=::std::logic_error;
        static int clamp(const OldFocusField& value,int low,int high) {
            return ::std::clamp(static_cast<int>(value),low,high);
        }
    };
    Model& m;
    explicit LegacyOracle(Model& model):m(model){}
    std::vector<RuntimeMenuButton> runtimeMenuFocusableButtons(const RuntimeWindowState&,const RECT&) {
        std::vector<RuntimeMenuButton> buttons;
        for(const auto action:m.actions())buttons.push_back({action});
        return buttons;
    }
    int runtimePlayerCurrentKeyCode(WPARAM raw) {
        const Input input{raw,m.active.repeated};return m.sample(input);
    }
    int runtimeKeyboardDefaultAt(int index){return m.defaultAt(index);}
    bool assignRuntimeMenuKeyboardBinding(RuntimeWindowState&,int i,int c,bool d){return m.assign(i,c,d);}
    void runtimeKeyboardChangePage(RuntimeWindowState&,int d,bool c){m.pageChange(d,c);}
    bool adjustRuntimeMenuSliderPosition(RuntimeWindowState&,Action a,int d){return m.adjust(a,d);}
    void GetClientRect(Model*,RECT*){m.prepare();}
    void RedrawWindow(Model*,void*,void*,unsigned flags) {
        if(flags!=(RDW_INVALIDATE|RDW_NOERASE))throw std::logic_error("old redraw flags");
        m.redraw();
    }
    void InvalidateRect(Model*,void*,bool erase) {
        if(erase)throw std::logic_error("old invalidate erase");m.invalidate();
    }
    void emitRuntimeSound(RuntimeWindowState&,int intent) {
        if(intent!=runtimeSoundIntentSidebarButtonClick)throw std::logic_error("old sound");
        m.sound();
    }
    void activateRuntimeMenuAction(Model*,RuntimeWindowState&,Action action){m.activate(action);}
    // BEGIN frozen original modifier, focused-action, capture, horizontal and navigation.
bool runtimeKeyboardModifierOnly(WPARAM key) {
    switch (key) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

RuntimeMenuAction runtimeMenuFocusedAction(
    const RuntimeWindowState& state,
    const RECT& clientRect
) {
    const std::vector<RuntimeMenuButton> focusable =
        runtimeMenuFocusableButtons(state, clientRect);
    if (focusable.empty()) {
        return RuntimeMenuAction::None;
    }
    const int index = std::clamp(
        state.runtimeMenuFocus,
        0,
        static_cast<int>(focusable.size()) - 1
    );
    return focusable[static_cast<std::size_t>(index)].action;
}

bool handleRuntimeMenuKeyCapture(
    RuntimeWindowState& state,
    WPARAM key
) {
    if (!state.runtimeMenuOpen
        || state.runtimeMenuPage != RuntimeMenuPage::Keyboard
        || state.runtimeMenuKeyCaptureIndex < 0) {
        return false;
    }
    if (key == VK_ESCAPE) {
        state.runtimeMenuKeyCaptureIndex = -1;
        state.runtimeMenuStatus = L"已取消改键。";
        return true;
    }
    if (runtimeKeyboardModifierOnly(key)) {
        state.runtimeMenuStatus =
            L"请同时按下组合键中的字母、数字或功能键。";
        return true;
    }

    const int bindingIndex = state.runtimeMenuKeyCaptureIndex;
    int keyCode = runtimePlayerCurrentKeyCode(key);
    bool restoredDefault = false;
    if (key == VK_BACK) {
        if (bindingIndex < 0
            || bindingIndex >= static_cast<int>(state.runtimeMenuKeyboardBindings.size())) {
            state.runtimeMenuKeyCaptureIndex = -1;
            state.runtimeMenuStatus = L"已取消改键。";
            return true;
        }
        keyCode = runtimeKeyboardDefaultAt(bindingIndex);
        restoredDefault = true;
    }
    (void)assignRuntimeMenuKeyboardBinding(
        state,
        bindingIndex,
        keyCode,
        restoredDefault
    );
    return true;
}

bool handleRuntimeMenuHorizontalKey(
    RuntimeWindowState& state,
    const RECT& clientRect,
    WPARAM key
) {
    if (key != VK_LEFT && key != VK_RIGHT) {
        return false;
    }
    if (state.runtimeMenuPage==RuntimeMenuPage::Keyboard) {
        runtimeKeyboardChangePage(state,key==VK_RIGHT?1:-1,
            runtimeMenuFocusedAction(state,clientRect)==RuntimeMenuAction::KeyboardCategory);
        return true;
    }
    return adjustRuntimeMenuSliderPosition(
        state,
        runtimeMenuFocusedAction(state, clientRect),
        key == VK_RIGHT ? 1 : -1
    );
}

    int navigation(RuntimeWindowState* state,Model* window,WPARAM wParam) {
        if (state->runtimeMenuOpen) {
            if (state->runtimeMenuPage==RuntimeMenuPage::Keyboard && (wParam==VK_PRIOR || wParam==VK_NEXT)) {
                runtimeKeyboardChangePage(*state,wParam==VK_PRIOR?-1:1,false);
                InvalidateRect(window,nullptr,FALSE);return 0;
            }
            RECT menuClientRect{};
            GetClientRect(window, &menuClientRect);
            const std::vector<RuntimeMenuButton> buttons =
                runtimeMenuFocusableButtons(*state, menuClientRect);
            if (!buttons.empty()
                && (wParam == VK_UP || wParam == VK_DOWN
                    || wParam == VK_HOME || wParam == VK_END)) {
                const int count = static_cast<int>(buttons.size());
                if (wParam == VK_HOME) {
                    state->runtimeMenuFocus = 0;
                } else if (wParam == VK_END) {
                    state->runtimeMenuFocus = count - 1;
                } else {
                    state->runtimeMenuFocus =
                        (state->runtimeMenuFocus
                            + (wParam == VK_UP ? count - 1 : 1))
                        % count;
                }
                state->runtimeMenuHover = RuntimeMenuAction::None;
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }
            if (!buttons.empty()
                && (wParam == VK_LEFT || wParam == VK_RIGHT)) {
                if (handleRuntimeMenuHorizontalKey(
                        *state,
                        menuClientRect,
                        wParam
                    )) {
                    emitRuntimeSound(
                        *state,
                        runtimeSoundIntentSidebarButtonClick
                    );
                    state->runtimeMenuHover = RuntimeMenuAction::None;
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
            if (!buttons.empty()
                && (wParam == VK_RETURN || wParam == VK_SPACE)) {
                const int index = std::clamp(
                    state->runtimeMenuFocus,
                    0,
                    static_cast<int>(buttons.size()) - 1
                );
                activateRuntimeMenuAction(
                    window,
                    *state,
                    buttons[static_cast<std::size_t>(index)].action
                );
                return 0;
            }
            return 0;
        }
        return 1;
    }
};

struct InputScope {
    Model& m;Input previous;
    InputScope(Model& model,Input owned):m(model),previous(model.active){m.active=owned;}
    ~InputScope(){m.active=previous;}
};
bool invoke(Model& m,Entry entry,Input& input) {
    const Input owned=input;InputScope scope(m,owned);
    m.trace.push_back({Op::Enter,static_cast<int>(entry),0,false,owned.rawKey,owned.repeated});
    bool result=false;
    if(m.useNew) {
        Host host(m);
        if(entry==Entry::Capture)result=handleRuntimeMenuCaptureKey(input,host);
        else if(entry==Entry::Horizontal)result=::handleRuntimeMenuHorizontalKey(input,host);
        else result=handleRuntimeMenuNavigationKey(input,host);
    } else {
        LegacyOracle old(m);RuntimeWindowState state(m);
        if(entry==Entry::Capture)result=old.handleRuntimeMenuKeyCapture(state,owned.rawKey);
        else if(entry==Entry::Horizontal)result=old.handleRuntimeMenuHorizontalKey(state,RECT{},owned.rawKey);
        else result=old.navigation(&state,&m,owned.rawKey)==0;
    }
    m.trace.push_back({Op::Return,result,0,false,owned.rawKey,owned.repeated});return result;
}
struct Run {
    State state;std::vector<State> retired;std::vector<Event> trace;
    bool returned=false,result=false;Op exception=Op::Limit;
};
Run run(bool modern,Entry entry,Input input,const State& state,const Setup& setup) {
    Model model;model.state=state;model.useNew=modern;
    if(setup)setup(model,input);
    bool returned=false,result=false;Op exception=Op::Limit;
    try{result=invoke(model,entry,input);returned=true;}
    catch(const SyntheticFailure& failure) {
        exception=failure.op;
        model.trace.push_back({Op::Caught,static_cast<std::int64_t>(failure.op),0,false,input.rawKey,input.repeated});
    }
    return {model.state,model.retired,model.trace,returned,result,exception};
}
struct Stats {
    std::size_t checks=0,cases=0,traceRows=0,bindingQueries=0,ownedChecks=0;
    std::size_t captureCases=0,horizontalCases=0,navigationCases=0;
    std::size_t dynamicCases=0,nestedCases=0,exceptionCases=0;
    void check(bool condition,const char* why) {
        ++checks;if(!condition)throw std::runtime_error(why);
    }
    Run compare(Entry entry,Input input,const State& state=State{},const Setup& setup={}) {
        const Run old=run(false,entry,input,state,setup);
        const Run actual=run(true,entry,input,state,setup);
        ++cases;traceRows+=actual.trace.size();
        if(entry==Entry::Capture)++captureCases;
        else if(entry==Entry::Horizontal)++horizontalCases;
        else ++navigationCases;
        check(old.state==actual.state,"complete state differs");
        check(old.retired==actual.retired,"retired root children differ");
        check(old.trace==actual.trace,"ordered six-field query/effect trace differs");
        check(old.returned==actual.returned&&old.result==actual.result
            &&old.exception==actual.exception,"return/exception differs");
        for(const auto& e:actual.trace)if(e.op==Op::Sample)++bindingQueries;
        return actual;
    }
};
bool has(const Run& r,Op op) {
    return std::any_of(r.trace.begin(),r.trace.end(),[op](const Event& e){return e.op==op;});
}
std::size_t count(const Run& r,Op op) {
    return static_cast<std::size_t>(std::count_if(r.trace.begin(),r.trace.end(),
        [op](const Event& e){return e.op==op;}));
}
Setup once(Op trigger,std::function<void(Model&)> action,unsigned occurrence=1) {
    return [trigger,action=std::move(action),occurrence](Model& model,Input&) {
        model.hook=[trigger,action,occurrence](Model& m,Op op) {
            if(m.armed&&op==trigger&&m.calls[static_cast<std::size_t>(op)]==occurrence) {
                m.armed=false;action(m);
            }
        };
    };
}
void enumContract(Stats& s) {
    const std::array pages{Page::Main,Page::Settings,Page::Display,Page::Sound,Page::Keyboard,Page::ExitConfirm};
    for(std::size_t i=0;i<pages.size();++i)s.check(static_cast<int>(pages[i])==static_cast<int>(i),"page int changed");
    const std::array actions{
        Action::None,Action::Options,Action::Save,Action::Load,Action::Exit,Action::Resume,
        Action::OpenDisplay,Action::OpenSound,Action::OpenKeyboard,Action::CycleGameSpeed,
        Action::CycleScrollRate,Action::CycleVisualDetail,Action::ToggleTooltips,Action::ToggleTargetLines,
        Action::ToggleScrollCoasting,Action::ToggleHiddenObjects,Action::CycleResolution,Action::ApplyResolution,
        Action::AdjustMusicVolume,Action::AdjustSoundVolume,Action::AdjustVoiceVolume,Action::ToggleMusic,
        Action::ToggleSound,Action::RebindOptions,Action::RebindStopObject,Action::RebindGuardObject,
        Action::RebindScatterObject,Action::RebindDeployObject,Action::RebindCenterView,Action::Back,
        Action::ConfirmExit,Action::CancelExit,Action::KeyboardCategory,Action::KeyboardPreviousPage,
        Action::KeyboardNextPage,Action::KeyboardResetAll};
    for(std::size_t i=0;i<actions.size();++i)s.check(static_cast<int>(actions[i])==static_cast<int>(i),"action int changed");
    const std::array<std::uint64_t,11> modifiers{0x10,0xA0,0xA1,0x11,0xA2,0xA3,0x12,0xA4,0xA5,0x5B,0x5C};
    Model model;LegacyOracle oracle(model);
    for(std::uint64_t raw=0;raw<256;++raw) {
        s.check(runtimeMenuKeyModifierOnly(raw)==oracle.runtimeKeyboardModifierOnly(raw),"modifier literal differs");
        s.check(runtimeMenuKeyModifierOnly(raw)==(std::find(modifiers.begin(),modifiers.end(),raw)!=modifiers.end()),"modifier set differs");
    }
    for(const auto raw:modifiers)s.check(!runtimeMenuKeyModifierOnly((std::uint64_t{1}<<48)|raw),"high raw became modifier");
}
void captureCases(Stats& s) {
    const std::array<std::uint64_t,20> keys{
        VK_ESCAPE,VK_BACK,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_CONTROL,VK_LCONTROL,VK_RCONTROL,
        VK_MENU,VK_LMENU,VK_RMENU,VK_LWIN,VK_RWIN,0,'A',0xFF,0x100,
        (std::uint64_t{1}<<32)|VK_ESCAPE,(std::uint64_t{1}<<48)|VK_BACK,
        std::numeric_limits<std::uint64_t>::max()};
    for(const auto raw:keys)for(bool repeated:{false,true})for(int variant=0;variant<4;++variant) {
        State state;
        if(variant==1)state.assignAccepted=false;
        if(variant==2){state.index=8;state.count=2;}
        if(variant==3){state.index=0;state.count=0;}
        const auto result=s.compare(Entry::Capture,{raw,repeated},state);
        s.check(result.result,"capture key must be consumed");
        if(raw==VK_ESCAPE)s.check(result.state.index==-1&&result.state.status==0&&!has(result,Op::Sample),"Escape precedes sample");
        else if(runtimeMenuKeyModifierOnly(raw))s.check(result.state.status==1&&!has(result,Op::Sample),"modifier has no sample");
        else s.check(count(result,Op::Sample)==1&&count(result,Op::Index)==2,"fresh index then one sample");
    }
    for(int guard=0;guard<4;++guard)for(const auto raw:std::array<std::uint64_t,3>{VK_ESCAPE,VK_BACK,'A'}) {
        State state;
        if(guard==0)state.open=false;
        if(guard==1)state.page=Page::Sound;
        if(guard==2)state.index=-1;
        if(guard==3)state.index=std::numeric_limits<std::int32_t>::min();
        const auto r=s.compare(Entry::Capture,{raw,true},state);
        s.check(!r.result&&!has(r,Op::Sample)&&!has(r,Op::Status),"capture guard short circuit");
        if(guard==0)s.check(!has(r,Op::Page),"closed capture must not read page");
        if(guard<=1)s.check(!has(r,Op::Index),"page guard must not read index");
    }
    for(std::int32_t modifiers=0;modifiers<8;++modifiers)for(bool repeated:{false,true}) {
        State state;state.sampled=0x41|(modifiers<<8);
        const auto r=s.compare(Entry::Capture,{'A',repeated},state);
        s.check(r.state.assignments==std::vector<Assignment>{{1,state.sampled,false}},"modified sample passed unchanged");
    }
    for(const auto index:std::array<std::int32_t,4>{0,3,4,std::numeric_limits<std::int32_t>::max()}) {
        State state;state.index=index;
        const auto r=s.compare(Entry::Capture,{VK_BACK,false},state);
        s.check(has(r,Op::Default)==(index<4),"Backspace original signed bound");
    }
    State huge;huge.count=std::numeric_limits<std::size_t>::max();
    s.compare(Entry::Capture,{VK_BACK,false},huge); // Same original size_t -> signed32 narrowing.
}
void navigationCases(Stats& s) {
    const std::array<std::uint64_t,13> keys{VK_UP,VK_DOWN,VK_HOME,VK_END,VK_LEFT,VK_RIGHT,
        VK_RETURN,VK_SPACE,VK_PRIOR,VK_NEXT,'?',(std::uint64_t{1}<<32)|VK_LEFT,
        std::numeric_limits<std::uint64_t>::max()};
    for(int page=0;page<6;++page)for(const auto raw:keys)for(bool empty:{false,true}) {
        State state;state.page=static_cast<Page>(page);state.focus=-2;
        if(empty)state.actions.clear();
        const auto r=s.compare(Entry::Navigation,{raw,(raw&1)!=0},state);
        s.check(r.result,"open menu consumes every key");
        if(page==4&&(raw==VK_PRIOR||raw==VK_NEXT))
            s.check(!has(r,Op::Client)&&!has(r,Op::Actions)&&has(r,Op::Invalidate),"paging precedes client/layout");
        else if(empty)s.check(!has(r,Op::Focus)&&!has(r,Op::Activate)&&!has(r,Op::Sound),"empty actions never read focus");
    }
    for(const auto raw:keys) {
        State closed;closed.open=false;
        const auto r=s.compare(Entry::Navigation,{raw,true},closed);
        s.check(!r.result&&!has(r,Op::Page)&&!has(r,Op::Client),"closed navigation does not query layout");
    }
    // No signed addition outside the original domain: extremes use clamp/Home/End.
    for(const auto focus:std::array<std::int32_t,7>{-7,-2,0,1,9,
            std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()})
        for(const auto raw:std::array<std::uint64_t,4>{VK_HOME,VK_END,VK_RETURN,VK_SPACE}) {
            State state;state.focus=focus;s.compare(Entry::Navigation,{raw,true},state);
        }
    State negative;negative.focus=-5;
    const auto up=s.compare(Entry::Navigation,{VK_UP,false},negative);
    s.check(up.state.focus==0,"negative signed remainder must use original arithmetic");
    const auto down=s.compare(Entry::Navigation,{VK_DOWN,false},negative);
    s.check(down.state.focus==-1,"negative focus must not be normalized/clamped");
    for(bool accepted:{false,true}) {
        State state;state.page=Page::Sound;state.sliderAccepted=accepted;
        const auto r=s.compare(Entry::Navigation,{VK_RIGHT,true},state);
        s.check(has(r,Op::Adjust)&&has(r,Op::Sound)==accepted&&has(r,Op::Repaint)==accepted,"horizontal rejection/acceptance effects");
    }
}
void horizontalCases(Stats& s) {
    for(const auto raw:std::array<std::uint64_t,7>{VK_LEFT,VK_RIGHT,VK_UP,0,'A',
            (std::uint64_t{1}<<32)|VK_RIGHT,std::numeric_limits<std::uint64_t>::max()})
        for(const auto page:std::array<Page,3>{Page::Main,Page::Sound,Page::Keyboard})
            for(int layout=0;layout<3;++layout) {
                State state;state.page=page;state.focus=layout==0?-10:10;
                if(layout==0)state.actions.clear();
                if(layout==1)state.actions={Action::KeyboardCategory};
                if(layout==2)state.actions={Action::AdjustSoundVolume,Action::Back};
                const auto r=s.compare(Entry::Horizontal,{raw,true},state);
                if(raw!=VK_LEFT&&raw!=VK_RIGHT)s.check(!r.result&&!has(r,Op::Page),"horizontal raw guard first");
                else if(layout==0)s.check(!has(r,Op::Focus),"empty horizontal never reads focus");
            }
    for(const auto focus:std::array<std::int32_t,4>{-9,2,std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max()}) {
        State state;state.focus=focus;state.page=Page::Sound;
        s.compare(Entry::Horizontal,{VK_LEFT,false},state);
    }
}
void dynamicCases(Stats& s) {
    const auto compare=[&](Entry entry,Input input,State state,const Setup& setup) {
        ++s.dynamicCases;return s.compare(entry,input,state,setup);
    };
    for(const auto raw:std::array<std::uint64_t,2>{VK_BACK,'A'}) {
        const auto r=compare(Entry::Capture,{raw,false},State{},
            once(Op::Index,[](Model& m){m.state.index=-2;}));
        s.check(raw==VK_BACK?!has(r,Op::Count):r.state.assignments[0].index==-2,"second capture index is fresh");
    }
    const auto cancelled=compare(Entry::Capture,{VK_BACK,false},State{},
        once(Op::Sample,[](Model& m){m.state.count=0;}));
    s.check(!has(cancelled,Op::Default)&&cancelled.state.status==0,"sample precedes size/default");
    const auto freshDefault=compare(Entry::Capture,{VK_BACK,false},State{},
        once(Op::Sample,[](Model& m){m.state.defaultValue=0x222;m.state.index=3;}));
    s.check(freshDefault.state.assignments==std::vector<Assignment>{{1,0x222,true}},"owned index with fresh default");
    State sound;sound.page=Page::Sound;
    const auto late=compare(Entry::Navigation,{VK_LEFT,false},sound,
        once(Op::Actions,[](Model& m){m.state.actions={Action::KeyboardCategory};m.state.focus=0;m.state.page=Page::Keyboard;}));
    s.check(count(late,Op::Actions)==2&&late.state.categoryTurns==-1,"horizontal gets fresh second page/actions/focus");
    const auto emptyLate=compare(Entry::Navigation,{VK_LEFT,false},sound,
        once(Op::Actions,[](Model& m){m.state.actions.clear();}));
    s.check(count(emptyLate,Op::Actions)==2&&!has(emptyLate,Op::Focus),"second empty actions avoid focus");
    const auto prepared=compare(Entry::Navigation,{VK_RETURN,false},sound,
        once(Op::Client,[](Model& m){m.state.actions={Action::Save};}));
    s.check(prepared.state.activations==std::vector<Action>{Action::Save},"client query precedes first owned actions");
    const auto owned=compare(Entry::Navigation,{VK_RETURN,false},sound,
        once(Op::Actions,[](Model& m){m.swapRoot();}));
    s.check(owned.state.activations==std::vector<Action>{Action::Back}&&owned.retired.size()==1,"activate owns first action list across swap");
    ++s.ownedChecks;
    const auto closed=compare(Entry::Navigation,{VK_RETURN,false},sound,
        once(Op::Activate,[](Model& m){m.swapRoot();m.state.open=false;}));
    s.check(!closed.state.open&&closed.trace.back().op==Op::Return,"activation close returns without more root queries");
    ++s.ownedChecks;
    for(const auto entry:std::array<Entry,3>{Entry::Capture,Entry::Horizontal,Entry::Navigation}) {
        const std::uint64_t raw=entry==Entry::Capture?'A':entry==Entry::Horizontal?VK_RIGHT:VK_RETURN;
        const auto r=compare(entry,{raw,true},State{},[](Model& m,Input& carrier) {
            m.hook=[&carrier](Model& model,Op) {if(model.armed){model.armed=false;carrier={VK_ESCAPE,false};}};
        });
        s.check(r.trace.front().raw==raw&&r.trace.front().repeated,"input carrier ownership");
        if(entry==Entry::Capture)s.check(r.state.assignments.size()==1,"callback cannot replace copied raw input");
        if(entry==Entry::Horizontal)s.check(r.state.pageTurns==1,"horizontal owns original right direction");
        if(entry==Entry::Navigation)s.check(r.state.activations.size()==1,"navigation owns original activation key");
        ++s.ownedChecks;
    }
}
void nestedAndExceptionCases(Stats& s) {
    for(const auto trigger:std::array<Op,7>{Op::Open,Op::Page,Op::Index,Op::Sample,
            Op::Actions,Op::Focus,Op::Activate}) {
        Entry entry=(trigger==Op::Index||trigger==Op::Sample)?Entry::Capture:Entry::Navigation;
        const Input input{entry==Entry::Capture?'A':VK_RETURN,true};
        ++s.nestedCases;
        const auto r=s.compare(entry,input,State{},once(trigger,[](Model& m) {
            m.swapRoot();Input nested{VK_HOME,false};(void)invoke(m,Entry::Navigation,nested);
        }));
        s.check(r.retired.size()==1&&count(r,Op::Enter)==2,"synchronous nested entry through same controller");
        ++s.ownedChecks;
    }
    struct ThrowCase { Entry entry;std::uint64_t raw;Op op;Page page; };
    const std::array cases{
        ThrowCase{Entry::Capture,'A',Op::Open,Page::Keyboard},
        ThrowCase{Entry::Capture,'A',Op::Page,Page::Keyboard},
        ThrowCase{Entry::Capture,'A',Op::Index,Page::Keyboard},
        ThrowCase{Entry::Capture,'A',Op::Sample,Page::Keyboard},
        ThrowCase{Entry::Capture,VK_BACK,Op::Count,Page::Keyboard},
        ThrowCase{Entry::Capture,VK_BACK,Op::Default,Page::Keyboard},
        ThrowCase{Entry::Capture,VK_ESCAPE,Op::SetIndex,Page::Keyboard},
        ThrowCase{Entry::Capture,VK_ESCAPE,Op::Status,Page::Keyboard},
        ThrowCase{Entry::Capture,'A',Op::Assign,Page::Keyboard},
        ThrowCase{Entry::Navigation,VK_HOME,Op::Client,Page::Main},
        ThrowCase{Entry::Navigation,VK_HOME,Op::Actions,Page::Main},
        ThrowCase{Entry::Navigation,VK_DOWN,Op::Focus,Page::Main},
        ThrowCase{Entry::Navigation,VK_HOME,Op::SetFocus,Page::Main},
        ThrowCase{Entry::Navigation,VK_HOME,Op::Hover,Page::Main},
        ThrowCase{Entry::Navigation,VK_HOME,Op::Repaint,Page::Main},
        ThrowCase{Entry::Navigation,VK_HOME,Op::Redraw,Page::Main},
        ThrowCase{Entry::Navigation,VK_PRIOR,Op::ChangePage,Page::Keyboard},
        ThrowCase{Entry::Navigation,VK_PRIOR,Op::Invalidate,Page::Keyboard},
        ThrowCase{Entry::Navigation,VK_LEFT,Op::Adjust,Page::Sound},
        ThrowCase{Entry::Navigation,VK_LEFT,Op::Sound,Page::Sound},
        ThrowCase{Entry::Navigation,VK_RETURN,Op::Activate,Page::Main}};
    for(const auto& c:cases) {
        State state;state.page=c.page;++s.exceptionCases;
        const auto r=s.compare(c.entry,{c.raw,true},state,[&c](Model& m,Input&){m.throwAt=c.op;});
        s.check(!r.returned&&r.exception==c.op,"original primitive exception propagates without later effects");
    }
}
} // namespace

int main() {
    Stats s;
    try {
        enumContract(s);captureCases(s);horizontalCases(s);navigationCases(s);
        dynamicCases(s);nestedAndExceptionCases(s);
        std::cout<<"menu-key contract checks="<<s.checks<<" differentialCases="<<s.cases
            <<" captureCases="<<s.captureCases<<" horizontalCases="<<s.horizontalCases
            <<" navigationCases="<<s.navigationCases<<" dynamicCases="<<s.dynamicCases
            <<" nestedCases="<<s.nestedCases<<" exceptionCases="<<s.exceptionCases
            <<" bindingQueries="<<s.bindingQueries<<" orderedTraceRows="<<s.traceRows
            <<" ownedChecks="<<s.ownedChecks
            <<" modifierCombinations=8 modifierOnlyKeys=11 enumPages=6 enumActions=36"
            <<" oldAdapterTrace=all-six-fields-equal windowsHeader=absent"
            <<" nativeKeyAdapter=not-run preferencePersistence=not-covered layoutGeometry=not-covered"
            <<" originalSignedOverflowDomain=not-covered rootSwapModel=only failures=0\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"menu-key contract failure: "<<error.what()<<'\n';return 1;
    }
}
