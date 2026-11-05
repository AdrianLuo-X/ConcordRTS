// Explicit diagnostic only: actual public Main runner, original resources and message pump.
#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include <type_traits>
#include <cwchar>

namespace {
struct MainHostProbe {
    int scene = 0;
    HWND parent = nullptr, page = nullptr;
    DWORD thread = 0;
    bool hosted = false, visible = false, focused = false, timedOut = false;
    unsigned creates = 0, ticks = 0;
};
thread_local MainHostProbe* mainHostProbe = nullptr;
LRESULT CALLBACK mainHostProbeHook(int code,WPARAM value,LPARAM data) {
    if(mainHostProbe && code==HCBT_CREATEWND) {
        wchar_t name[96]{};GetClassNameW(reinterpret_cast<HWND>(value),name,96);
        if(std::wcscmp(name,L"ConcordRTS.RuntimeMainMenu")==0) {
            ++mainHostProbe->creates;
            mainHostProbe->page=reinterpret_cast<HWND>(value);
            if(mainHostProbe->scene==4) return 1;
        }
    }
    return CallNextHookEx(nullptr,code,value,data);
}
void CALLBACK mainHostProbeTimer(HWND,UINT,UINT_PTR,DWORD) {
    if(!mainHostProbe) return;
    auto& p=*mainHostProbe;++p.ticks;
    if(p.page && IsWindow(p.page)) {
        p.hosted=GetParent(p.page)==p.parent;
        p.visible=IsWindowVisible(p.page)!=FALSE;
        p.focused=GetFocus()==p.page;
        if(p.scene==2) {if(p.parent&&IsWindow(p.parent))DestroyWindow(p.parent);}
        else if(p.scene==3) PostQuitMessage(17);
        else PostMessageW(p.page,WM_CLOSE,0,0);
    }
    if(p.ticks>500) {
        p.timedOut=true;
        if(p.page&&IsWindow(p.page))SendMessageW(p.page,WM_CLOSE,0,0);
        else PostQuitMessage(99);
    }
}
template<class Entry>
RuntimeMainMenuResult invokeMainHostProbe(Entry entry,RuntimeConfig& config,HWND parent) {
    if constexpr(std::is_invocable_r_v<RuntimeMainMenuResult,Entry,RuntimeConfig&,RuntimeMainMenuStartPage,RuntimeWindowId>) {
        RuntimeWindowParentWin32 lease(parent);
        return entry(config,RuntimeMainMenuStartPage::Home,lease.id());
    } else return entry(config,RuntimeMainMenuStartPage::Home,parent);
}
}

int runRuntimeMainMenuHostLifecycleSmoke(const RuntimeConfig& input) {
    unsigned checks=0,failures=0;
    const auto require=[&](bool value,const char* detail){++checks;if(!value){++failures;std::cerr<<"Main host lifecycle: "<<detail<<"\n";}};
    for(int scene=0;scene<5;++scene) {
        prewarmedMainMenuState.reset();
        MainHostProbe probe;probe.scene=scene;probe.thread=GetCurrentThreadId();
        if(scene!=0) {
            probe.parent=CreateWindowExW(0,L"STATIC",L"Concord Main owned parent",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,60,60,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            require(probe.parent!=nullptr,"parent creation");
            if(probe.parent) {ShowWindow(probe.parent,SW_SHOW);UpdateWindow(probe.parent);}
        }
        mainHostProbe=&probe;
        HHOOK hook=SetWindowsHookExW(WH_CBT,mainHostProbeHook,nullptr,probe.thread);
        require(hook!=nullptr,"own-thread creation observer");
        UINT_PTR timer=SetTimer(nullptr,0,20,mainHostProbeTimer);require(timer!=0,"owned native message timer");
        RuntimeConfig config=input;
        const auto result=invokeMainHostProbe(&runRuntimeMainMenu,config,probe.parent);
        KillTimer(nullptr,timer);if(hook)UnhookWindowsHookEx(hook);mainHostProbe=nullptr;
        require(!probe.timedOut,"original public runner timeout");
        require(result==RuntimeMainMenuResult::Exit,"original Exit result");
        require(probe.creates==1,"exact original single page create attempt");
        if(scene==4) require(!probe.page||!IsWindow(probe.page),"actual page creation rejection");
        else {require(probe.visible,"real visible page");require(probe.focused,"same-thread page focus");require(probe.hosted,"original standalone/child parent");}
        if(scene==2) require(!IsWindow(probe.parent)&&!IsWindow(probe.page),"parent close destroys page before return");
        else if(scene==3) {
            MSG quit{}; bool reposted = false; unsigned drained = 0;
            const auto quitDeadline = GetTickCount64() + 10000;
            // WM_QUIT is generated at low priority; a single immediate filtered
            // Peek can precede queue delivery. Keep the exact code assertion and
            // the original 500*20ms fixture timeout, dispatching only our thread.
            while (!reposted && GetTickCount64() < quitDeadline) {
                while (PeekMessageW(&quit, nullptr, 0, 0, PM_REMOVE)) {
                    if (quit.message == WM_QUIT) { reposted = true; break; }
                    ++drained; TranslateMessage(&quit); DispatchMessageW(&quit);
                }
                if (!reposted) MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            }
            std::cout<<"Main quit delivery: found="<<reposted<<" code="<<quit.wParam
                <<" drained="<<drained<<" withinOriginal10000ms="<<(GetTickCount64()<=quitDeadline)<<"\n";
            require(reposted&&quit.wParam==17,"original WM_QUIT code repost");
            require(probe.page&&IsWindow(probe.page),"original pending page retained until caller parent teardown");
        } else require(!probe.page||!IsWindow(probe.page),"closed/rejected page gone before return");
        if(probe.parent && scene!=2) require(IsWindow(probe.parent)!=FALSE,"child close does not destroy parent");
        if(probe.parent&&IsWindow(probe.parent))DestroyWindow(probe.parent);
        if(probe.page&&IsWindow(probe.page))DestroyWindow(probe.page);
        prewarmedMainMenuState.reset();
        require(!probe.page||!IsWindow(probe.page),"fixture-owned page/resource owner retired");
        std::cout<<"Main host scene "<<scene<<": exit="<<(result==RuntimeMainMenuResult::Exit)<<" creates="<<probe.creates<<" hosted="<<probe.hosted<<" visible="<<probe.visible<<" focus="<<probe.focused<<" timeout="<<probe.timedOut<<"\n";
    }
    std::cout<<"Main host lifecycle: checks="<<checks<<" scenes=5 failures="<<failures<<" skipped=0\n";
    return failures?1:0;
}

int runRuntimeMainMenuControllerHostEdgesSmoke(const RuntimeConfig& input) {
    unsigned checks=0,failures=0;
    const auto require=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<"Main controller host edge: "<<why<<"\n";}};
    prewarmedMainMenuState.reset();
    HWND parent=CreateWindowExW(0,L"STATIC",L"Owned Main controller edge parent",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,40,40,1100,720,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(parent!=nullptr,"actual edge parent");if(!parent)return 1;
    ShowWindow(parent,SW_SHOW);UpdateWindow(parent);
    {
        MenuState state;
        require(initializeMenu(state,input,true),"original complete resources initialize");
        HWND page=createMainMenuWindow(state,input,parent);
        require(page!=nullptr,"actual production native child");
        if(page) {
            ShowWindow(page,SW_SHOW);UpdateWindow(page);require(focusMainMenuWindow(page,parent),"actual child focus");
            require(SetWindowPos(page,nullptr,0,0,1024,640,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"actual child resize");
            UpdateWindow(page);
            require(state.viewWidth==800&&state.viewHeight==600&&state.viewLeft==112&&state.viewTop==20,"original integer pixel grid after WM_SIZE");
            require(ShowWindow(parent,SW_MINIMIZE)!=FALSE,"minimize original visible parent");
            require(IsIconic(parent)!=FALSE,"actual minimized parent");
            ShowWindow(parent,SW_RESTORE);UpdateWindow(parent);
            require(IsIconic(parent)==FALSE&&IsWindow(page),"restore retains page");
            require(focusMainMenuWindow(page,parent),"restore child focus");
            state.phase=MenuPhase::Idle;state.page=MenuPage::Home;
            SendMessageW(page,WM_MOUSEMOVE,0,MAKELPARAM(112+670,20+210));
            require(state.mouseVisible&&state.hover==0&&state.mousePoint.x==670&&state.mousePoint.y==210,"real signed message to canonical pixel policy");
            SendMessageW(page,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(112+670,20+210));
            require(GetCapture()==page&&state.pressed==0,"native capture owns actual page");
            ReleaseCapture();require(GetCapture()!=page&&state.pressed==-1,"actual capture-lost callback clears policy");
            HWND other=CreateWindowExW(0,L"STATIC",L"Owned alternate focus",WS_CHILD|WS_VISIBLE,0,0,10,10,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
            require(other!=nullptr,"own alternate focus window");
            if(other){SetFocus(other);require(GetFocus()==other,"actual same-thread focus loss");require(focusMainMenuWindow(page,parent),"focus re-entry");DestroyWindow(other);}
            require(startMainMenuVideo(state,input,0),"actual asynchronous decoder start");
            require(state.videoFirstFramePending||state.videoAnimation.sequence()>0,"actual pending or decoded first frame");
            SendMessageW(page,WM_CLOSE,0,0);
            require(state.closing&&state.window==nullptr&&!IsWindow(page)&&IsWindow(parent),"close while media active destroys child and retains parent");
            require(!state.audio.musicOpen,"native close stops music");
        }
    }
    require(IsWindow(parent)!=FALSE,"native state destruction retains caller parent");
    // Actual public re-entry reuses initialized resource state; each invocation owns a fresh page.
    for(int visit=0;visit<2;++visit){
        MainHostProbe probe;probe.scene=1;probe.parent=parent;probe.thread=GetCurrentThreadId();mainHostProbe=&probe;
        HHOOK hook=SetWindowsHookExW(WH_CBT,mainHostProbeHook,nullptr,probe.thread);UINT_PTR timer=SetTimer(nullptr,0,20,mainHostProbeTimer);
        require(hook!=nullptr&&timer!=0,"real re-entry observers");
        RuntimeConfig config=input;const auto result=invokeMainHostProbe(&runRuntimeMainMenu,config,parent);
        KillTimer(nullptr,timer);if(hook)UnhookWindowsHookEx(hook);mainHostProbe=nullptr;
        require(result==RuntimeMainMenuResult::Exit&&!probe.timedOut&&probe.creates==1&&probe.hosted&&probe.focused&&probe.visible,"actual public re-entry creates focused child");
        require(prewarmedMainMenuState&&prewarmedMainMenuState->initialized&&prewarmedMainMenuState->window==nullptr&&IsWindow(parent),"prewarmed resources survive child close without native handle retention");
    }
    RuntimeConfig config=input;
    const auto rejected=runRuntimeMainMenu(config,RuntimeMainMenuStartPage::Home,RuntimeWindowId{~std::uint64_t{0}});
    require(rejected==RuntimeMainMenuResult::Exit&&IsWindow(parent),"unknown public identity fails closed without taking caller parent");
    prewarmedMainMenuState.reset();require(DestroyWindow(parent)!=FALSE,"caller destroys owned parent");
    std::cout<<"Main controller host edges: checks="<<checks<<" failures="<<failures<<" skipped=0 resize=minimize-restore capture=actual focus=actual reentry=2 activeMediaClose=1\n";
    return failures?1:0;
}
