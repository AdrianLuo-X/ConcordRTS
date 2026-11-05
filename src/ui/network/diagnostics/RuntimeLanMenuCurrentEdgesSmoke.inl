// Current-only diagnostics, using the actual public controller/native adapters.
namespace {
struct LanCurrentProbe {
    HWND parent=nullptr,page=nullptr,nested=nullptr;
    unsigned stage=0,ticks=0,creates=0;
    bool timedOut=false,resized=false,focus=false,capture=false,nestedCancelled=false;
};
thread_local LanCurrentProbe* lanCurrentProbe=nullptr;
LRESULT CALLBACK lanCurrentHook(int code,WPARAM value,LPARAM data){
    if(lanCurrentProbe&&code==HCBT_CREATEWND){wchar_t name[96]{};HWND window=reinterpret_cast<HWND>(value);GetClassNameW(window,name,96);
        if(std::wcscmp(name,L"ConcordRTS.RuntimeMultiplayerMenu")==0){lanCurrentProbe->page=window;++lanCurrentProbe->creates;}
        if(std::wcscmp(name,L"ConcordRTS.RuntimeSkirmishShell")==0)lanCurrentProbe->nested=window;
    }
    return CallNextHookEx(nullptr,code,value,data);
}
void CALLBACK lanCurrentTimer(HWND,UINT,UINT_PTR,DWORD){
    if(!lanCurrentProbe)return;auto& p=*lanCurrentProbe;++p.ticks;
    if(p.stage==0&&p.page&&IsWindowVisible(p.page)){
        auto* state=reinterpret_cast<RuntimeMultiplayerMenuUiState*>(GetWindowLongPtrW(p.page,GWLP_USERDATA));
        if(state&&state->backButton&&state->hostButton){
            p.stage=1;SetWindowPos(p.page,nullptr,0,0,1024,640,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            RECT client{};GetClientRect(p.page,&client);p.resized=client.right==1024&&client.bottom==640;
            SetFocus(state->playerNameEdit);p.focus=GetFocus()==state->playerNameEdit;
            SetCapture(p.page);p.capture=GetCapture()==p.page;ReleaseCapture();p.capture=p.capture&&GetCapture()!=p.page;
            SetWindowTextW(state->playerNameEdit,L"LAN owner fixture");
            // Real command enters the original Skirmish owner synchronously.
            SendMessageW(p.page,WM_COMMAND,MAKEWPARAM(HostButton,BN_CLICKED),reinterpret_cast<LPARAM>(state->hostButton));
            p.nestedCancelled=p.nested&& !IsWindow(p.nested)&&IsWindow(p.page)&&IsWindowVisible(p.page)
                &&state->page==MultiplayerMenuPage::Browser&&!state->hosting
                &&controlText(state->statusText)=="host setup selection was cancelled";
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
struct LanResumeDialogProbe {HWND expectedOwner=nullptr,dialog=nullptr;unsigned activations=0;bool ownerMatches=false;};
thread_local LanResumeDialogProbe* lanResumeDialogProbe=nullptr;
LRESULT CALLBACK lanResumeDialogHook(int code,WPARAM value,LPARAM data){
    if(lanResumeDialogProbe&&code==HCBT_ACTIVATE){wchar_t name[32]{};HWND window=reinterpret_cast<HWND>(value);GetClassNameW(window,name,32);
        if(std::wcscmp(name,L"#32770")==0){auto& p=*lanResumeDialogProbe;p.dialog=window;++p.activations;p.ownerMatches=GetWindow(window,GW_OWNER)==p.expectedOwner;PostMessageW(window,WM_COMMAND,IDCANCEL,0);}}
    return CallNextHookEx(nullptr,code,value,data);
}
int runLanCurrentNativeEdges(const RuntimeConfig& config){
    unsigned checks=0,failures=0;auto require=[&](bool b,const char* why){++checks;if(!b){++failures;std::cerr<<"LAN current native: "<<why<<"\n";}};
    HWND parent=CreateWindowExW(0,L"STATIC",L"Owned LAN current parent",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,40,40,1100,760,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(parent!=nullptr,"current parent created");if(!parent)return 1;ShowWindow(parent,SW_SHOW);
    RuntimeWindowParentWin32 parentLease(parent);
    auto unknown=runRuntimeMultiplayerMenu(config,RuntimeMultiplayerMenuNetwork::Lan,RuntimeWindowId{~std::uint64_t{0}});
    require(!unknown.accepted&&!unknown.bridge&&!unknown.error.empty(),"unknown allocated parent rejected");
    RuntimeWindowId retired{};{RuntimeWindowParentWin32 shortLease(parent);retired=shortLease.id();}
    auto revoked=runRuntimeMultiplayerMenu(config,RuntimeMultiplayerMenuNetwork::Lan,retired);require(!revoked.accepted&&!revoked.error.empty(),"revoked parent rejected");
    // Two real public entries borrow the same live caller, including nested cancel.
    for(unsigned repeat=0;repeat<2;++repeat){LanCurrentProbe p;p.parent=parent;lanCurrentProbe=&p;HHOOK hook=SetWindowsHookExW(WH_CBT,lanCurrentHook,nullptr,GetCurrentThreadId());UINT_PTR timer=SetTimer(nullptr,0,20,lanCurrentTimer);require(hook&&timer,"current actual thread hook and timer");
        auto result=runRuntimeMultiplayerMenu(config,RuntimeMultiplayerMenuNetwork::Lan,parentLease.id());
        KillTimer(nullptr,timer);if(hook)UnhookWindowsHookEx(hook);lanCurrentProbe=nullptr;
        require(!p.timedOut&&p.stage==3&&p.creates==1,"real public reentry exits");require(p.resized,"real page resize integer client");require(p.focus&&p.capture,"actual focus and capture/release");require(p.nestedCancelled,"actual Skirmish cancel restores LAN browser");require(!result.accepted&&result.error.empty()&&!result.bridge,"cancel original LAN result");require(!IsWindow(p.page)&&IsWindow(parent),"page retired caller parent survives");
    }
    for(unsigned hosted=0;hosted<2;++hosted){LanResumeDialogProbe p;p.expectedOwner=hosted?parent:nullptr;lanResumeDialogProbe=&p;HHOOK hook=SetWindowsHookExW(WH_CBT,lanResumeDialogHook,nullptr,GetCurrentThreadId());require(hook!=nullptr,"actual resume dialog hook");
        RuntimeMultiplayerResumeSave save;RuntimeMultiplayerHostSelectionProjection projection;const auto beforeSave=save;const auto beforeProjection=projection;RuntimeMultiplayerCompatibility compatibility;std::string error;
        const bool accepted=selectRuntimeMultiplayerResumeSave(config,compatibility,"Host","host",hosted?parentLease.id():RuntimeWindowId{},save,projection,error);
        if(hook)UnhookWindowsHookEx(hook);lanResumeDialogProbe=nullptr;
        require(!accepted&&error=="已取消选择联机存档。","actual common dialog cancel error");require(p.activations>=1&&p.ownerMatches,"actual common dialog parent identity");require(p.dialog&&!IsWindow(p.dialog),"actual common dialog retired");require(save.setup==beforeSave.setup&&save.compatibility==beforeSave.compatibility&&save.descriptor==beforeSave.descriptor&&save.bytes==beforeSave.bytes&&projection==beforeProjection,"cancel leaves output values unchanged");require(IsWindow(parent)!=FALSE,"resume owner survives");
    }
    {RuntimeMultiplayerResumeSave save;RuntimeMultiplayerHostSelectionProjection projection;RuntimeMultiplayerCompatibility compatibility;std::string error;
        require(!selectRuntimeMultiplayerResumeSave(config,compatibility,"Host","host",retired,save,projection,error)&&error=="unsupported multiplayer resume parent identity","revoked dialog owner rejected before dialog");}
    require(DestroyWindow(parent)!=FALSE,"destroy only owned current parent");
    std::cout<<"LAN current native edges: checks="<<checks<<" failures="<<failures<<" skipped=0 reentries=2 realNestedSkirmishCancel=2 realResumeDialogs=2\n";return failures?1:0;
}
}
