// Current-only diagnostics, using the actual public controller/native adapters.
namespace {
struct InternetCurrentProbe {
    HWND parent=nullptr,page=nullptr,nested=nullptr;
    unsigned stage=0,ticks=0,creates=0;
    bool timedOut=false,resized=false,focus=false,capture=false,nestedCancelled=false;
};
thread_local InternetCurrentProbe* internetCurrentProbe=nullptr;
LRESULT CALLBACK internetCurrentHook(int code,WPARAM value,LPARAM data){
    if(internetCurrentProbe&&code==HCBT_CREATEWND){wchar_t name[96]{};HWND window=reinterpret_cast<HWND>(value);GetClassNameW(window,name,96);
        if(std::wcscmp(name,L"ConcordRTS.RuntimeInternetMultiplayerMenu")==0){internetCurrentProbe->page=window;++internetCurrentProbe->creates;}
        if(std::wcscmp(name,L"ConcordRTS.RuntimeSkirmishShell")==0)internetCurrentProbe->nested=window;
    }
    return CallNextHookEx(nullptr,code,value,data);
}
void CALLBACK internetCurrentTimer(HWND,UINT,UINT_PTR,DWORD){
    if(!internetCurrentProbe)return;auto& p=*internetCurrentProbe;++p.ticks;
    if(p.stage==0&&p.page&&IsWindowVisible(p.page)){
        auto* state=reinterpret_cast<RuntimeInternetMultiplayerMenuUiState*>(GetWindowLongPtrW(p.page,GWLP_USERDATA));
        if(state&&state->accountHandleEdit&&state->hostButton){
            p.stage=1;SetWindowPos(p.page,nullptr,0,0,1024,640,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            RECT client{};GetClientRect(p.page,&client);p.resized=client.right==1024&&client.bottom==640;
            SetFocus(state->accountHandleEdit);p.focus=GetFocus()==state->accountHandleEdit;
            SetCapture(p.page);p.capture=GetCapture()==p.page;ReleaseCapture();p.capture=p.capture&&GetCapture()!=p.page;
            SetWindowTextW(state->accountHandleEdit,L"Internet owner fixture");
            // Explicit original command fixture while the owned TLS endpoint waits.
            // This exercises nested ownership, without claiming authenticated access.
            SendMessageW(p.page,WM_COMMAND,MAKEWPARAM(InternetHost,BN_CLICKED),reinterpret_cast<LPARAM>(state->hostButton));
            p.nestedCancelled=p.nested&& !IsWindow(p.nested)&&IsWindow(p.page)&&IsWindowVisible(p.page)
                &&state->page==InternetMenuPage::Account&&!state->hosting
                &&controlText(state->statusText)=="Host map and rules selection was cancelled";
            p.stage=3;PostMessageW(p.page,WM_CLOSE,0,0);
        }
    }else if(p.stage==1&&p.nested&&IsWindowVisible(p.nested)){
        p.stage=2;PostMessageW(p.nested,WM_CLOSE,0,0);
    }
    if(p.ticks>1500){p.timedOut=true;
        if(p.nested&&IsWindow(p.nested))PostMessageW(p.nested,WM_CLOSE,0,0);
        if(p.page&&IsWindow(p.page))PostMessageW(p.page,WM_CLOSE,0,0);
        if(!p.page||!IsWindow(p.page))PostQuitMessage(99);
    }
}
int runInternetCurrentNativeEdges(const RuntimeConfig& config){
    unsigned checks=0,failures=0;auto require=[&](bool b,const char* why){++checks;if(!b){++failures;std::cerr<<"Internet current native: "<<why<<"\n";}};
    HWND parent=CreateWindowExW(0,L"STATIC",L"Owned Internet current parent",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,40,40,1100,760,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(parent!=nullptr,"current parent created");if(!parent)return 1;ShowWindow(parent,SW_SHOW);
    RuntimeWindowParentWin32 parentLease(parent);
    auto unknown=runRuntimeInternetMultiplayerMenu(config,RuntimeWindowId{~std::uint64_t{0}});
    require(!unknown.accepted&&!unknown.bridge&&!unknown.error.empty(),"unknown allocated parent rejected");
    RuntimeWindowId retired{};{RuntimeWindowParentWin32 shortLease(parent);retired=shortLease.id();}
    auto revoked=runRuntimeInternetMultiplayerMenu(config,retired);require(!revoked.accepted&&!revoked.error.empty(),"revoked parent rejected");
    for(unsigned repeat=0;repeat<2;++repeat){InternetCurrentProbe p;p.parent=parent;internetCurrentProbe=&p;HHOOK hook=SetWindowsHookExW(WH_CBT,internetCurrentHook,nullptr,GetCurrentThreadId());UINT_PTR timer=SetTimer(nullptr,0,20,internetCurrentTimer);require(hook&&timer,"current actual thread hook and timer");
        auto result=runRuntimeInternetMultiplayerMenu(config,parentLease.id());
        KillTimer(nullptr,timer);if(hook)UnhookWindowsHookEx(hook);internetCurrentProbe=nullptr;
        require(!p.timedOut&&p.stage==3&&p.creates==1,"real public reentry exits");require(p.resized,"real page resize integer client");require(p.focus&&p.capture,"actual focus and capture/release");require(p.nestedCancelled,"explicit original host command cancels actual nested Skirmish and restores Internet");require(!result.accepted&&result.error.empty()&&!result.bridge,"cancel original Internet result");require(!IsWindow(p.page)&&IsWindow(parent),"page retired caller parent survives");
    }
    require(DestroyWindow(parent)!=FALSE,"destroy only owned current parent");
    std::cout<<"Internet current native edges: checks="<<checks<<" failures="<<failures<<" skipped=0 reentries=2 realNestedSkirmishCancel=2 authenticatedAccess=not-run\n";return failures?1:0;
}
}
