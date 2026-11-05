// Diagnostic only: observe the real public Internet entry and original message pump.
#include <cstdlib>
#include <cwchar>
#include <type_traits>
#include <iostream>
namespace {
#include "ui/network/RuntimeResumeMenuCapture.inl"
struct InternetHostProbe {
    int scene=0;HWND parent=nullptr,page=nullptr,startupDialog=nullptr;HBRUSH brush=nullptr;unsigned creates=0,ticks=0,startupDialogs=0;
    bool dialogTitleMatched=false,dialogDismissed=false,acted=false,timedOut=false,visible=false,hosted=false,focusOwned=false,captured=false,flowOwned=false;
    std::filesystem::path frame;
};
thread_local InternetHostProbe* internetHostProbe=nullptr;
LRESULT CALLBACK internetHostProbeHook(int code,WPARAM value,LPARAM data){
    if(internetHostProbe&&code==HCBT_CREATEWND){wchar_t name[96]{};GetClassNameW(reinterpret_cast<HWND>(value),name,96);
        if(std::wcscmp(name,L"ConcordRTS.RuntimeInternetMultiplayerMenu")==0){++internetHostProbe->creates;internetHostProbe->page=reinterpret_cast<HWND>(value);if(internetHostProbe->scene==4)return 1;}}
    if(internetHostProbe&&code==HCBT_ACTIVATE){wchar_t name[32]{};HWND dialog=reinterpret_cast<HWND>(value);GetClassNameW(dialog,name,32);if(std::wcscmp(name,L"#32770")==0&&GetWindow(dialog,GW_OWNER)==internetHostProbe->parent){++internetHostProbe->startupDialogs;internetHostProbe->startupDialog=dialog;}}
    return CallNextHookEx(nullptr,code,value,data);
}
BOOL CALLBACK internetProbeDialogChildren(HWND child,LPARAM){wchar_t name[80]{};GetClassNameW(child,name,80);std::cout<<"Internet owned dialog child id="<<GetDlgCtrlID(child)<<" class="<<std::string(name,name+std::wcslen(name))<<" visible="<<IsWindowVisible(child)<<" enabled="<<IsWindowEnabled(child)<<" hwnd="<<reinterpret_cast<std::uintptr_t>(child)<<"\n";return TRUE;}
BOOL CALLBACK internetProbeThreadWindows(HWND window,LPARAM){wchar_t name[80]{};GetClassNameW(window,name,80);std::cout<<"Internet own thread window class="<<std::string(name,name+std::wcslen(name))<<" visible="<<IsWindowVisible(window)<<" enabled="<<IsWindowEnabled(window)<<" hwnd="<<reinterpret_cast<std::uintptr_t>(window)<<" owner="<<reinterpret_cast<std::uintptr_t>(GetWindow(window,GW_OWNER))<<"\n";if(std::wcscmp(name,L"#32770")==0)EnumChildWindows(window,internetProbeDialogChildren,0);return TRUE;}
void CALLBACK internetHostProbeTimer(HWND,UINT,UINT_PTR,DWORD){
    if(!internetHostProbe)return;auto& p=*internetHostProbe;++p.ticks;
    if(p.scene==4&&(p.ticks==2||p.ticks==50||p.ticks==250||p.ticks==500)){
        std::cout<<"Internet dialog diagnostic tick="<<p.ticks<<" observed="<<p.startupDialogs<<" dialog="<<reinterpret_cast<std::uintptr_t>(p.startupDialog)<<" valid="<<IsWindow(p.startupDialog)<<" visible="<<IsWindowVisible(p.startupDialog)<<" enabled="<<IsWindowEnabled(p.startupDialog)<<" button="<<reinterpret_cast<std::uintptr_t>(GetDlgItem(p.startupDialog,IDOK))<<" expectedParent="<<reinterpret_cast<std::uintptr_t>(p.parent)<<"\n";
        EnumThreadWindows(GetCurrentThreadId(),internetProbeThreadWindows,0);std::cout.flush();
    }
    if(p.scene==4&&!p.dialogDismissed&&p.startupDialog&&IsWindow(p.startupDialog)&&IsWindowVisible(p.startupDialog)){
        wchar_t title[128]{};GetWindowTextW(p.startupDialog,title,128);
        p.dialogTitleMatched=std::wcscmp(title,L"ConcordRTS Internet")==0;
        HWND button=GetDlgItem(p.startupDialog,IDOK);
        if(!button)button=GetDlgItem(p.startupDialog,IDCANCEL);
        std::cout<<"Internet error actual title="<<std::string(title,title+std::wcslen(title))<<" titleMatch="<<p.dialogTitleMatched<<" observedButtonId="<<(button?GetDlgCtrlID(button):-1)<<"\n";std::cout.flush();
        if(p.dialogTitleMatched&&button&&IsWindowVisible(button)&&IsWindowEnabled(button)){
            SetActiveWindow(p.startupDialog);p.dialogDismissed=true;SendMessageW(button,BM_CLICK,0,0);
        }
    }
    if(p.scene!=4&&!p.acted&&p.page&&IsWindow(p.page)&&IsWindowVisible(p.page)){
        auto* state=reinterpret_cast<RuntimeInternetMultiplayerMenuUiState*>(GetWindowLongPtrW(p.page,GWLP_USERDATA));
        if(state&&state->leaveButton&&state->roomList){
            p.acted=true;p.visible=true;p.hosted=GetParent(p.page)==p.parent;
            HWND focused=GetFocus();p.focusOwned=focused==p.page||(focused&&IsChild(p.page,focused));
            p.flowOwned=state->flowBackend!=nullptr&&state->transport!=nullptr;p.brush=state->skinControlBrush;p.captured=captureRuntimeResumeMenu(p.page,p.frame);
            if(p.scene==2){if(p.parent&&IsWindow(p.parent))DestroyWindow(p.parent);}
            else if(p.scene==3)PostQuitMessage(23);
            else PostMessageW(p.page,WM_CLOSE,0,0);
        }
    }
    if(p.ticks>500){p.timedOut=true;if(p.page&&IsWindow(p.page))PostMessageW(p.page,WM_CLOSE,0,0);else PostQuitMessage(99);}
}
template<class Entry>
RuntimeMultiplayerMenuResult invokeInternetHostProbe(Entry entry,const RuntimeConfig& config,HWND parent){
    if constexpr(std::is_invocable_r_v<RuntimeMultiplayerMenuResult,Entry,const RuntimeConfig&,RuntimeWindowId>){RuntimeWindowParentWin32 lease(parent);return entry(config,lease.id());}
    else return entry(config,parent);
}
}
#include "ui/network/diagnostics/RuntimeInternetMenuCurrentEdgesSmoke.inl"
int runRuntimeInternetMenuHostLifecycleSmoke(const RuntimeConfig& inputConfig,const std::string& outputPath){
    const std::filesystem::path output(outputPath);if(!output.is_absolute()||std::filesystem::exists(output)){std::cerr<<"Internet lifecycle requires a new absolute output directory\n";return 1;}std::filesystem::create_directories(output);
    unsigned checks=0,failures=0,unsafeQuitWindows=0;const auto require=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<"Internet host lifecycle: "<<why<<"\n";}};
    const char* original=std::getenv("CONCORD_INTERNET_MENU_HOST_EXPECT_ORIGINAL_QUIT_LIVE");const bool expectOriginalQuitLive=original&&std::string(original)=="1";
    RuntimeTcpServer ownedEndpoint;std::string endpointError;
    require(ownedEndpoint.open(0,16,RuntimeTcpBindScope::LoopbackIpv4,endpointError),"owned loopback TCP endpoint");
    if(!ownedEndpoint.isOpen())return 1;
    RuntimeConfig config=inputConfig;config.configPath=(std::filesystem::temp_directory_path()/"internet-owned-native.ini").string();
    {std::ofstream file(config.configPath,std::ios::binary);file<<"[Internet]\nServiceAddress=127.0.0.1\nServicePort="<<ownedEndpoint.port()<<"\nServerName=localhost\nServiceId=owned-native-fixture\nPeerId=peer-fixture\nCredentialHex=00112233445566778899aabbccddeeff\n";require(file.good(),"owned local service configuration");}
    // Actual unchanged TLS connector may wait on this owned socket. No TLS
    // authentication/certificate success is claimed and no validation bypass.
    for(int scene=0;scene<5;++scene){
        InternetHostProbe probe;probe.scene=scene;probe.frame=output/("scene"+std::to_string(scene)+".bmp");
        if(scene!=0){RECT size{0,0,800,650};AdjustWindowRectEx(&size,WS_OVERLAPPEDWINDOW,FALSE,0);probe.parent=CreateWindowExW(0,L"STATIC",L"Owned Internet parent",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,60,60,size.right-size.left,size.bottom-size.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(probe.parent!=nullptr,"owned native parent");if(probe.parent){ShowWindow(probe.parent,SW_SHOW);UpdateWindow(probe.parent);}}
        internetHostProbe=&probe;HHOOK hook=SetWindowsHookExW(WH_CBT,internetHostProbeHook,nullptr,GetCurrentThreadId());require(hook!=nullptr,"own-thread actual page observer");UINT_PTR timer=SetTimer(nullptr,0,20,internetHostProbeTimer);require(timer!=0,"own native deadline timer");
        RuntimeMultiplayerMenuResult result=invokeInternetHostProbe(&runRuntimeInternetMultiplayerMenu,config,probe.parent);
        KillTimer(nullptr,timer);if(hook)UnhookWindowsHookEx(hook);internetHostProbe=nullptr;
        require(!probe.timedOut,"public runner exits before deadline");require(!result.accepted&&result.bridge==nullptr,"unaccepted result retains no running bridge");require(probe.creates==1,"one actual page attempt");
        const bool pageLive=probe.page&&IsWindow(probe.page);
        if(scene==4){require(!pageLive,"actual CBT create rejection");require(!result.error.empty(),"creation failure carries original error");require(probe.startupDialogs==1,"actual owned startup error dialog acknowledged");require(probe.dialogDismissed,"visible actual owned error button activated");require(probe.dialogTitleMatched,"exact production startup error dialog title");}
        else {require(probe.visible&&probe.hosted,"actual visible standalone/child parent");require(probe.captured,"actual existing native capture path");require(probe.flowOwned,"actual original Internet browsing flow initialized");require(result.error.empty(),"original close keeps result error empty");}
        if(scene==3){MSG quit{};require(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)&&quit.wParam==23,"original WM_QUIT23 repost");if(pageLive)++unsafeQuitWindows;require(pageLive==expectOriginalQuitLive,"observed original quit lifetime / corrected current lifetime");}
        else require(!pageLive,"normal close/create failure destroys page");
        if(scene==2)require(!IsWindow(probe.parent),"parent-first destruction");else if(probe.parent)require(IsWindow(probe.parent)!=FALSE,"caller parent survives page close/failure");
        // The baseline may expose an already-returned stack pointer in userdata.
        // Never dereference it after the public call; safely retire only our own
        // observed orphan after recording the actual defect, without dispatching it.
        if(pageLive){SetWindowLongPtrW(probe.page,GWLP_USERDATA,0);KillTimer(probe.page,internetMenuPollTimer);DestroyWindow(probe.page);if(probe.brush)DeleteObject(probe.brush);}
        if(probe.parent&&IsWindow(probe.parent))DestroyWindow(probe.parent);
        std::cout<<"Internet host scene "<<scene<<": creates="<<probe.creates<<" visible="<<probe.visible<<" hosted="<<probe.hosted<<" focusOwned="<<probe.focusOwned<<" flowOwned="<<probe.flowOwned<<" pageLiveAfterReturn="<<pageLive<<" timeout="<<probe.timedOut<<"\n";
    }
    require(unsafeQuitWindows==(expectOriginalQuitLive?1U:0U),"explicit original defect count retained");
    std::cout<<"Internet host lifecycle observation: checks="<<checks<<" failures="<<failures<<" skipped=0 scenes=5 actualPublicRunner=1 unsafeQuitWindows="<<unsafeQuitWindows<<" expectOriginalQuitLive="<<expectOriginalQuitLive<<" originalOrCurrentObservationOnly=1\n";
    const int extraStatus = runInternetCurrentNativeEdges(config);
    return (failures || extraStatus)?1:0;
}
