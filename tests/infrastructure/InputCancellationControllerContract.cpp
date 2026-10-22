#include "ui/input/RuntimeInputCancellationController.h"
#include "ui/input/RuntimeKeyboardRouterController.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "fixtures/InputCancellationOld.inl"
#include "fixtures/InputCancellationPreludeOld.inl"

namespace {
struct State {
    RuntimeMapRightPointerState right;
    RuntimeMenuAction slider = RuntimeMenuAction::None, pressed = RuntimeMenuAction::None;
    bool repaint = false, leftActive = true, leftMoved = true, held = false;
    int capture = 0, effects = 0, preferences = 0, redraws = 0;

};
struct Trace {
    std::vector<std::string> events;
    int fault = -1, ordinal = 0, depth = 0;
    std::function<void(const std::string&)> callback;
    void add(std::string event) {
        events.push_back(std::to_string(depth) + ":" + event);
        if (ordinal++ == fault) throw std::runtime_error("injected original effect failure");
        if (callback) callback(event);
    }
};
std::string action(RuntimeMenuAction v) { return std::to_string(static_cast<int>(v)); }
struct Right final : RuntimeMapRightPointerGestureHost {
    State& s; Trace& t; std::function<void()> released;
    Right(State& value, Trace& trace) : s(value), t(trace) {}
    RuntimeMapRightPointerState& gestureState() override {
        t.add("right.state=" + std::to_string(s.right.active) + "," + std::to_string(s.right.moved));
        return s.right;
    }
    void requestRepaint() override { t.add("right.repaint"); s.repaint = true; }
    bool ownsCapture() override { t.add("capture=" + std::to_string(s.capture)); return s.capture == 1; }
    void releaseCapture() override {
        t.add("release"); s.capture = 0;
        if (released) { ++t.depth; released(); --t.depth; }
    }
    [[noreturn]] void unused() { throw std::logic_error("unselected right pointer primitive"); }
    bool worldLoaded() override { unused(); }
    bool menuOpen() override { unused(); }
    RuntimeInputPixelRect clientRect() override { unused(); }
    RuntimeInputPixelRect sidebarRect(RuntimeInputPixelRect) override { unused(); }
    RuntimeInputPixelRect mapViewport(RuntimeInputPixelRect) override { unused(); }
    void setMouseClientPointValid(bool) override { unused(); }
    void setMouseClientPoint(RuntimeInputPixelPoint) override { unused(); }
    bool cancelActiveSuperWeaponTargetSelection(const std::string&) override { unused(); }
    std::optional<std::int32_t> buildButtonAtPoint(RuntimeInputPixelRect,RuntimeInputPixelPoint) override { unused(); }
    void dispatchBuildButtonSecondary(std::int32_t) override { unused(); }
    bool sidebarToolActive() override { unused(); }
    void clearSidebarTool() override { unused(); }
    void focusWindow() override { unused(); }
    void captureWindow() override { unused(); }
    bool scrollCoastingEnabled() override { unused(); }
    std::int32_t scrollRate() override { unused(); }
    bool scrollCameraClamped(std::int32_t,std::int32_t) override { unused(); }
    void invalidateProjection() override { unused(); }
    void setInteractionStatus(const std::string&) override { unused(); }
    void redrawWindow() override { unused(); }
};
struct Menu final : RuntimeInputCaptureHost {
    State& s; Trace& t; int dynamicRead = 0;
    Menu(State& value, Trace& trace) : s(value), t(trace) {}
    RuntimeMenuAction sliderDrag() override {
        const auto result = s.slider;
        t.add("slider=" + action(result));
        if (dynamicRead-- == 1) s.slider = RuntimeMenuAction::AdjustVoiceVolume;
        return result;
    }
    RuntimeMenuAction pressed() override { t.add("pressed=" + action(s.pressed)); return s.pressed; }
    void setSliderDrag(RuntimeMenuAction v) override { t.add("setSlider=" + action(v)); s.slider = v; }
    void setPressed(RuntimeMenuAction v) override { t.add("setPressed=" + action(v)); s.pressed = v; }
    void applySliderRuntimeEffects(RuntimeMenuAction v) override { t.add("effects=" + action(v)); ++s.effects; }
    void persistPreferences() override { t.add("preferences"); ++s.preferences; }
    void requestRepaint() override { t.add("menu.repaint"); s.repaint = true; }
    void redraw() override { t.add("menu.redraw"); ++s.redraws; }
};
struct Force final : RuntimeForceAttackHeldHost {
    State& s; Trace& t;
    Force(State& value,Trace& trace):s(value),t(trace){}
    bool held() override { t.add("held=" + std::to_string(s.held)); return s.held; }
    bool controlDown() override { t.add("control=0"); return false; }
    void setHeld(bool v) override { t.add("setHeld="+std::to_string(v));s.held=v; }
    void requestRepaint() override { t.add("force.repaint");s.repaint=true; }
    void redraw() override { t.add("force.redraw");++s.redraws; }
};
bool equalState(const State& a,const State& b) {
    return a.right.active==b.right.active && a.right.moved==b.right.moved
        && a.right.start.x==b.right.start.x && a.right.start.y==b.right.start.y
        && a.right.last.x==b.right.last.x && a.right.last.y==b.right.last.y
        && a.right.cursorDelta.x==b.right.cursorDelta.x && a.right.cursorDelta.y==b.right.cursorDelta.y
        && a.slider==b.slider && a.pressed==b.pressed && a.repaint==b.repaint
        && a.leftActive==b.leftActive && a.leftMoved==b.leftMoved && a.held==b.held
        && a.capture==b.capture && a.effects==b.effects && a.preferences==b.preferences && a.redraws==b.redraws;
}
struct Saved { State state; std::vector<std::string> events; bool result=false,threw=false; };
Saved capture(State value,bool old,int fault=-1,int mutation=0) {
    Trace trace;trace.fault=fault;Right right(value,trace);Menu menu(value,trace);
    menu.dynamicRead=mutation==2?1:0;
    const auto invoke=[&] { return old?frozenCaptureChanged(right,menu):handleRuntimeInputCaptureChanged(right,menu); };
    bool nested=false;
    if(mutation==1)trace.callback=[&](const std::string& e) {
        if(!nested && e.starts_with("effects=")) {
            nested=true;value.slider=RuntimeMenuAction::CycleScrollRate;value.pressed=RuntimeMenuAction::Exit;
            value.right.active=value.right.moved=true;++trace.depth;(void)invoke();--trace.depth;
        }
    };
    Saved saved;
    try { saved.result=invoke(); } catch(const std::runtime_error&) { saved.threw=true; }
    saved.state=value;saved.events=trace.events;return saved;
}
Saved prelude(State value,bool old,RuntimeForceAttackInput input,RuntimeMapRightPointerCancelEvent event) {
    Trace trace;Right right(value,trace);Menu menu(value,trace);Force force(value,trace);
    right.released=[&]{if(old)(void)frozenCaptureChanged(right,menu);else(void)handleRuntimeInputCaptureChanged(right,menu);};
    if(old)frozenForceAttackHeld(input,force);else handleRuntimeForceAttackHeld(input,force);
    if(old)frozenCancelRightPointer(event,right);else cancelRuntimeMapRightPointer(event,right);
    Saved saved;saved.state=value;saved.events=trace.events;return saved;
}
} // namespace

int main(int argc,char** argv) {
    std::ofstream file;
    if(argc==3 && std::string(argv[1])=="--trace") { file.open(argv[2],std::ios::binary); if(!file)return 2; }
    std::size_t checks=0,failures=0,cases=0,events=0,exceptionCases=0,reentries=0;
    const auto need=[&](bool v,const std::string& label){++checks;if(!v){++failures;std::cerr<<"Input cancellation failure: "<<label<<'\n';}};
    const auto compare=[&](const Saved& old,const Saved& current,const std::string& label) {
        ++cases;events+=old.events.size()+current.events.size();
        need(old.events==current.events,label+" complete ordered query/effect trace");
        need(equalState(old.state,current.state),label+" complete gesture/menu/effect state");
        need(old.result==current.result && old.threw==current.threw,label+" return and partial failure");
        need(current.state.leftActive && current.state.leftMoved,label+" left gesture remains with left-up owner");
        if(file){file<<"case "<<label<<'\n';for(const auto& e:old.events)file<<"old "<<e<<'\n';for(const auto& e:current.events)file<<"new "<<e<<'\n';}
    };
    constexpr int last=static_cast<int>(RuntimeMenuAction::KeyboardResetAll);
    for(int slider=0;slider<=last;++slider)for(int pressed=0;pressed<=last;++pressed)
        for(int flags=0;flags<4;++flags) {
            State state;state.slider=static_cast<RuntimeMenuAction>(slider);state.pressed=static_cast<RuntimeMenuAction>(pressed);
            state.right.active=(flags&1)!=0;state.right.moved=(flags&2)!=0;
            state.right.start={-31,47};state.right.last={303,-99};state.right.cursorDelta={17,-11};
            const auto name=std::to_string(slider)+"/"+std::to_string(pressed)+"/"+std::to_string(flags);
            auto old=capture(state,true);auto actual=capture(state,false);compare(old,actual,name);
            need(actual.result==(slider!=0||pressed!=0),name+" original default fallthrough");
            if(flags&1)need(!actual.state.right.active&&!actual.state.right.moved,name+" right owner clears only active gesture");
            else need(actual.state.right.moved==state.right.moved,name+" inactive moved flag not silently repaired");
        }
    for(int mutation=0;mutation<3;++mutation) {
        State state;state.slider=RuntimeMenuAction::AdjustMusicVolume;state.pressed=state.slider;state.right.active=state.right.moved=true;
        const auto baseline=capture(state,true,-1,mutation);
        compare(baseline,capture(state,false,-1,mutation),"dynamic-"+std::to_string(mutation));
        if(mutation==1)++reentries;
        for(std::size_t fault=0;fault<baseline.events.size();++fault) {
            compare(capture(state,true,static_cast<int>(fault),mutation),capture(state,false,static_cast<int>(fault),mutation),
                "partial-"+std::to_string(mutation)+"-"+std::to_string(fault));++exceptionCases;
        }
    }
    const RuntimeForceAttackInput inputs[]={
        {RuntimeForceAttackEvent::KillFocus,0},{RuntimeForceAttackEvent::ActivateApplication,0},
        {RuntimeForceAttackEvent::ActivateApplication,1},{RuntimeForceAttackEvent::ActivateApplication,std::uint64_t{1}<<32},
        {RuntimeForceAttackEvent::Other,0}};
    for(std::size_t event=0;event<5;++event)for(int flags=0;flags<4;++flags)
        for(int owner=0;owner<3;++owner)for(int menuState=0;menuState<3;++menuState)for(int held=0;held<2;++held) {
            State state;state.right.active=(flags&1)!=0;state.right.moved=(flags&2)!=0;state.capture=owner;state.held=held!=0;
            if(menuState==1)state.pressed=RuntimeMenuAction::Options;
            if(menuState==2)state.slider=state.pressed=RuntimeMenuAction::CycleGameSpeed;
            const auto cancel=event==0?RuntimeMapRightPointerCancelEvent::FocusLost:
                event==1?RuntimeMapRightPointerCancelEvent::ApplicationDeactivated:
                event==4?RuntimeMapRightPointerCancelEvent::CancelMode:RuntimeMapRightPointerCancelEvent::Other;
            compare(prelude(state,true,inputs[event],cancel),prelude(state,false,inputs[event],cancel),
                "prelude-"+std::to_string(event)+"-"+std::to_string(flags)+"-"+std::to_string(owner)+"-"+std::to_string(menuState)+"-"+std::to_string(held));
        }
    std::cout<<"Input cancellation contract "<<(failures?"failed":"passed")<<": checks="<<checks<<" cases="<<cases
        <<" traceEvents="<<events<<" partialFailureCases="<<exceptionCases<<" reentries="<<reentries<<" failures="<<failures
        <<" sameProductionCpp=1 independentOldLiteral=1 nativeWindows=not-run physicalInput=not-run\n";
    return failures?1:0;
}
