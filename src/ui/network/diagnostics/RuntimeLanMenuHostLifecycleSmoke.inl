// Diagnostic only: observe the real public LAN entry and original message pump.
#include <cstdlib>
#include <cwchar>
#include <type_traits>
#include <iostream>
namespace {
struct LanHostProbe {
    int scene=0;HWND parent=nullptr,page=nullptr;unsigned creates=0,ticks=0;
    bool acted=false,timedOut=false,visible=false,hosted=false,focusOwned=false,captured=false,flowReady=false;
    std::filesystem::path frame;
};
thread_local LanHostProbe* lanHostProbe=nullptr;
LRESULT CALLBACK lanHostProbeHook(int code,WPARAM value,LPARAM data){
    if(lanHostProbe&&code==HCBT_CREATEWND){wchar_t name[96]{};GetClassNameW(reinterpret_cast<HWND>(value),name,96);
        if(std::wcscmp(name,L"ConcordRTS.RuntimeMultiplayerMenu")==0){++lanHostProbe->creates;lanHostProbe->page=reinterpret_cast<HWND>(value);if(lanHostProbe->scene==4)return 1;}}
    return CallNextHookEx(nullptr,code,value,data);
}
void CALLBACK lanHostProbeTimer(HWND,UINT,UINT_PTR,DWORD){
    if(!lanHostProbe)return;auto& p=*lanHostProbe;++p.ticks;
    if(!p.acted&&p.page&&IsWindow(p.page)&&IsWindowVisible(p.page)){
        auto* state=reinterpret_cast<RuntimeMultiplayerMenuUiState*>(GetWindowLongPtrW(p.page,GWLP_USERDATA));
        if(state&&state->backButton&&state->roomList){
            p.acted=true;p.visible=true;p.hosted=GetParent(p.page)==p.parent;
            HWND focused=GetFocus();p.focusOwned=focused==p.page||(focused&&IsChild(p.page,focused));
            p.flowReady=state->flow.initialized();p.captured=captureRuntimeResumeMenu(p.page,p.frame);
            if(p.scene==2){if(p.parent&&IsWindow(p.parent))DestroyWindow(p.parent);}
            else if(p.scene==3)PostQuitMessage(23);
            else PostMessageW(p.page,WM_CLOSE,0,0);
        }
    }
    if(p.ticks>500){p.timedOut=true;if(p.page&&IsWindow(p.page))PostMessageW(p.page,WM_CLOSE,0,0);else PostQuitMessage(99);}
}
template<class Entry>
RuntimeMultiplayerMenuResult invokeLanHostProbe(Entry entry,const RuntimeConfig& config,HWND parent){
    if constexpr(std::is_invocable_r_v<RuntimeMultiplayerMenuResult,Entry,const RuntimeConfig&,RuntimeMultiplayerMenuNetwork,RuntimeWindowId>){RuntimeWindowParentWin32 lease(parent);return entry(config,RuntimeMultiplayerMenuNetwork::Lan,lease.id());}
    else return entry(config,RuntimeMultiplayerMenuNetwork::Lan,parent);
}
}
#include "ui/network/diagnostics/RuntimeLanMenuCurrentEdgesSmoke.inl"
int runRuntimeLanMenuHostLifecycleSmoke(const RuntimeConfig& config,const std::string& outputPath){
    const std::filesystem::path output(outputPath);if(!output.is_absolute()||std::filesystem::exists(output)){std::cerr<<"LAN lifecycle requires a new absolute output directory\n";return 1;}std::filesystem::create_directories(output);
    unsigned checks=0,failures=0,unsafeQuitWindows=0;const auto require=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<"LAN host lifecycle: "<<why<<"\n";}};
    const char* original=std::getenv("CONCORD_LAN_MENU_HOST_EXPECT_ORIGINAL_QUIT_LIVE");const bool expectOriginalQuitLive=original&&std::string(original)=="1";
    for(int scene=0;scene<5;++scene){
        LanHostProbe probe;probe.scene=scene;probe.frame=output/("scene"+std::to_string(scene)+".bmp");
        if(scene!=0){RECT size{0,0,800,650};AdjustWindowRectEx(&size,WS_OVERLAPPEDWINDOW,FALSE,0);probe.parent=CreateWindowExW(0,L"STATIC",L"Owned LAN parent",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,60,60,size.right-size.left,size.bottom-size.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(probe.parent!=nullptr,"owned native parent");if(probe.parent){ShowWindow(probe.parent,SW_SHOW);UpdateWindow(probe.parent);}}
        lanHostProbe=&probe;HHOOK hook=SetWindowsHookExW(WH_CBT,lanHostProbeHook,nullptr,GetCurrentThreadId());require(hook!=nullptr,"own-thread actual page observer");UINT_PTR timer=SetTimer(nullptr,0,20,lanHostProbeTimer);require(timer!=0,"own native deadline timer");
        RuntimeMultiplayerMenuResult result=invokeLanHostProbe(&runRuntimeMultiplayerMenu,config,probe.parent);
        KillTimer(nullptr,timer);if(hook)UnhookWindowsHookEx(hook);lanHostProbe=nullptr;
        require(!probe.timedOut,"public runner exits before deadline");require(!result.accepted&&result.bridge==nullptr,"unaccepted result retains no running bridge");require(probe.creates==1,"one actual page attempt");
        const bool pageLive=probe.page&&IsWindow(probe.page);
        if(scene==4){require(!pageLive,"actual CBT create rejection");require(!result.error.empty(),"creation failure carries original error");}
        else {require(probe.visible&&probe.hosted,"actual visible standalone/child parent");require(probe.captured,"actual existing native capture path");require(probe.flowReady,"actual original LAN browsing flow initialized");require(result.error.empty(),"original close keeps result error empty");}
        if(scene==3){MSG quit{};require(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)&&quit.wParam==23,"original WM_QUIT23 repost");if(pageLive)++unsafeQuitWindows;require(pageLive==expectOriginalQuitLive,"observed original quit lifetime / corrected current lifetime");}
        else require(!pageLive,"normal close/create failure destroys page");
        if(scene==2)require(!IsWindow(probe.parent),"parent-first destruction");else if(probe.parent)require(IsWindow(probe.parent)!=FALSE,"caller parent survives page close/failure");
        // The baseline may expose an already-returned stack pointer in userdata.
        // Never dereference it after the public call; safely retire only our own
        // observed orphan after recording the actual defect, without dispatching it.
        if(pageLive){SetWindowLongPtrW(probe.page,GWLP_USERDATA,0);KillTimer(probe.page,multiplayerMenuPollTimer);DestroyWindow(probe.page);}
        if(probe.parent&&IsWindow(probe.parent))DestroyWindow(probe.parent);
        std::cout<<"LAN host scene "<<scene<<": creates="<<probe.creates<<" visible="<<probe.visible<<" hosted="<<probe.hosted<<" focusOwned="<<probe.focusOwned<<" flowReady="<<probe.flowReady<<" pageLiveAfterReturn="<<pageLive<<" timeout="<<probe.timedOut<<"\n";
    }
    require(unsafeQuitWindows==(expectOriginalQuitLive?1U:0U),"explicit original defect count retained");
    std::cout<<"LAN host lifecycle observation: checks="<<checks<<" failures="<<failures<<" skipped=0 scenes=5 actualPublicRunner=1 unsafeQuitWindows="<<unsafeQuitWindows<<" expectOriginalQuitLive="<<expectOriginalQuitLive<<" originalOrCurrentObservationOnly=1\n";
    const int extraStatus = runLanCurrentNativeEdges(config);
    return (failures || extraStatus)?1:0;
}
