// Explicit diagnostic: the actual public Campaign/LoadGame SDK runner and pump.
#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include <type_traits>
#include <cwchar>
#include <thread>

namespace {
struct CampaignHostProbe {
    int scene=0; bool loadGame=false;
    HWND parent=nullptr,page=nullptr;
    DWORD thread=0;
    unsigned creates=0,ticks=0,actionAttempts=0;
    bool visible=false,focused=false,hosted=false,timedOut=false;
};
thread_local CampaignHostProbe* campaignHostProbe=nullptr;
LRESULT CALLBACK campaignHostProbeHook(int code,WPARAM value,LPARAM data) {
    if(campaignHostProbe&&code==HCBT_CREATEWND){
        wchar_t name[96]{};GetClassNameW(reinterpret_cast<HWND>(value),name,96);
        if(std::wcscmp(name,L"ConcordRTS.RuntimeCampaignMenu")==0){
            ++campaignHostProbe->creates;campaignHostProbe->page=reinterpret_cast<HWND>(value);
            if(campaignHostProbe->scene==4)return 1;
        }
    }
    return CallNextHookEx(nullptr,code,value,data);
}
void CALLBACK campaignHostProbeTimer(HWND,UINT,UINT_PTR,DWORD){
    if(!campaignHostProbe)return;auto& p=*campaignHostProbe;++p.ticks;
    if(p.page&&IsWindow(p.page)){
        p.visible=IsWindowVisible(p.page)!=FALSE;p.focused=GetFocus()==p.page;p.hosted=GetParent(p.page)==p.parent;
        // Trigger the original action only after the original real readiness
        // assertions can be observed. ShowWindow may dispatch a timer reentrantly.
        if(p.visible&&p.focused&&p.hosted){
            ++p.actionAttempts;
            if(p.scene==2){if(p.parent&&IsWindow(p.parent))DestroyWindow(p.parent);}
            else if(p.scene==3)PostQuitMessage(23);
            else PostMessageW(p.page,WM_CLOSE,0,0);
        }
    }
    if(p.ticks>500){
        p.timedOut=true;
        if(p.page&&IsWindow(p.page))SendMessageW(p.page,WM_CLOSE,0,0);
        else PostQuitMessage(99);
    }
}
template<class Entry>
RuntimeCampaignSelection invokeCampaignHostProbe(Entry entry,const RuntimeConfig& config,HWND parent,bool loadGame){
    if constexpr(std::is_invocable_r_v<RuntimeCampaignSelection,Entry,const RuntimeConfig&,RuntimeWindowId,bool>){
        RuntimeWindowParentWin32 lease(parent);return entry(config,lease.id(),loadGame);
    }else return entry(config,parent,loadGame);
}
}
#include "ui/campaign/diagnostics/RuntimeCampaignMenuHostEdgesSmoke.inl"

int runRuntimeCampaignHostLifecycleSmoke(const RuntimeConfig& config){
    unsigned checks=0,failures=0;
    const auto require=[&](bool v,const char* why){++checks;if(!v){++failures;std::cerr<<"Campaign host lifecycle: "<<why<<"\n";}};
    for(bool loadGame:{false,true})for(int scene=0;scene<5;++scene){
        CampaignHostProbe probe;probe.loadGame=loadGame;probe.scene=scene;probe.thread=GetCurrentThreadId();
        if(scene!=0){
            probe.parent=CreateWindowExW(0,L"STATIC",L"Owned Campaign parent",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,60,60,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            require(probe.parent!=nullptr,"own parent creation");if(probe.parent){ShowWindow(probe.parent,SW_SHOW);UpdateWindow(probe.parent);}
        }
        campaignHostProbe=&probe;
        HHOOK hook=SetWindowsHookExW(WH_CBT,campaignHostProbeHook,nullptr,probe.thread);require(hook!=nullptr,"own-thread page creation observer");
        UINT_PTR timer=SetTimer(nullptr,0,20,campaignHostProbeTimer);require(timer!=0,"own safety/message timer");
        const auto messageDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        const auto diagnosticStarted=std::chrono::steady_clock::now();
        const RuntimeCampaignSelection result=invokeCampaignHostProbe(&runRuntimeCampaignMenu,config,probe.parent,loadGame);
        const auto diagnosticElapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-diagnosticStarted).count();
        std::cout<<"Campaign quit diagnostic: loadGame="<<loadGame<<" scene="<<scene<<" ticks="<<probe.ticks<<" actionAttempts="<<probe.actionAttempts<<" entryMs="<<diagnosticElapsed<<" deadlineLeftMs="<<std::chrono::duration_cast<std::chrono::milliseconds>(messageDeadline-std::chrono::steady_clock::now()).count()<<"\n";
        KillTimer(nullptr,timer);if(hook)UnhookWindowsHookEx(hook);campaignHostProbe=nullptr;
        require(!probe.timedOut,"actual public runner completes before deadline");
        require(!result.accepted&&!result.loadedFromSave&&!result.loadedSkirmishSave&&result.savePath.empty()&&result.campaignId.empty()&&result.missionId.empty(),"original unaccepted default selection on close/failure");
        require(probe.creates==1,"one real page creation attempt");
        if(scene==4)require(!probe.page||!IsWindow(probe.page),"actual CBT creation rejection");
        else {require(probe.visible,"actual visible page");require(probe.focused,"actual same-thread focused page");require(probe.hosted,"actual standalone/child parent");}
        require(!probe.page||!IsWindow(probe.page),"original close/WM_QUIT destroys page before returning");
        if(scene==2)require(!IsWindow(probe.parent),"parent close retires native parent");
        else if(probe.parent)require(IsWindow(probe.parent)!=FALSE,"child close/failure retains caller parent");
        if(scene==3){
            MSG quit{};bool found=PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE;
            const bool firstFound=found;const auto firstValue=quit.wParam;unsigned reads=1;
            while(!found&&std::chrono::steady_clock::now()<messageDeadline){
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                found=PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE;++reads;
            }
            std::cout<<"Campaign original quit observation: firstFound="<<firstFound<<" firstValue="<<firstValue<<" reads="<<reads<<" finalFound="<<found<<" finalValue="<<quit.wParam<<"\n";
            require(found&&quit.wParam==23,"original WM_QUIT23 repost after closing page");
        }
        if(probe.parent&&IsWindow(probe.parent))DestroyWindow(probe.parent);
        if(probe.page&&IsWindow(probe.page))DestroyWindow(probe.page);
        std::cout<<"Campaign host scene "<<(loadGame?"save":"campaign")<<scene<<": creates="<<probe.creates<<" visible="<<probe.visible<<" focus="<<probe.focused<<" hosted="<<probe.hosted<<" timeout="<<probe.timedOut<<" accepted="<<result.accepted<<"\n";
    }
    std::cout<<"Campaign host lifecycle: checks="<<checks<<" scenes=10 failures="<<failures<<" skipped=0 actualPublicRunner=1\n";
    const int edges=runRuntimeCampaignHostEdgesSmoke(config);
    return failures||edges?1:0;
}
