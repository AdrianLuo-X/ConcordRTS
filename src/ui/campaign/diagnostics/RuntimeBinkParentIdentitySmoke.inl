// Private finite oracle for the existing modal player. No decoder or window owner is duplicated.
namespace {
struct BinkParentOracle {
    HWND host = nullptr;
    HWND child = nullptr;
    HWND focus = nullptr;
    WNDPROC childProc = nullptr;
    int mode = 0;
    int action = 0;
    bool destroyed = false;
    bool stopped = false;
    bool sawStandalone = false;
    bool sawHosted = false;
    bool refused = false;
    ULONGLONG started = 0;
    std::string problem;
};
thread_local BinkParentOracle* binkParentOracle = nullptr;
LRESULT CALLBACK binkParentObserveChild(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* oracle=binkParentOracle;
    if(!oracle||!oracle->childProc)return DefWindowProcW(window,message,wParam,lParam);
    auto* state=reinterpret_cast<BinkVideoPageState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_DESTROY)oracle->destroyed=true;
    const auto result=CallWindowProcW(oracle->childProc,window,message,wParam,lParam);
    if(message==WM_DESTROY&&state)oracle->stopped=state->window==nullptr&&state->animation.sequence()==0;
    return result;
}
BOOL CALLBACK binkParentFindStandalone(HWND window,LPARAM pointer) {
    wchar_t name[128]{};GetClassNameW(window,name,128);
    if(std::wcscmp(name,L"ConcordRTS.RuntimeBinkVideoPage")==0)
        reinterpret_cast<BinkParentOracle*>(pointer)->child=window;
    return TRUE;
}
LRESULT CALLBACK binkParentCbt(int code,WPARAM wParam,LPARAM lParam) {
    if(code==HCBT_CREATEWND&&binkParentOracle) {
        wchar_t name[128]{};GetClassNameW(reinterpret_cast<HWND>(wParam),name,128);
        if(std::wcscmp(name,L"ConcordRTS.RuntimeBinkVideoPage")==0){binkParentOracle->refused=true;return 1;}
    }
    return CallNextHookEx(nullptr,code,wParam,lParam);
}
LRESULT CALLBACK binkParentTimerProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message!=WM_TIMER||wParam!=147)return DefWindowProcW(window,message,wParam,lParam);
    auto* oracle=binkParentOracle;if(!oracle)return 0;
    if(!oracle->child) {
        if(oracle->host&&IsWindow(oracle->host))oracle->child=FindWindowExW(oracle->host,nullptr,L"ConcordRTS.RuntimeBinkVideoPage",nullptr);
        if(!oracle->child)EnumThreadWindows(GetCurrentThreadId(),binkParentFindStandalone,reinterpret_cast<LPARAM>(oracle));
        if(oracle->child) {
            auto* state=reinterpret_cast<BinkVideoPageState*>(GetWindowLongPtrW(oracle->child,GWLP_USERDATA));
            if(!state||!state->frame.valid()){oracle->problem="actual first frame missing";SendMessageW(oracle->child,WM_CLOSE,0,0);return 0;}
            oracle->sawStandalone=state->standalone;oracle->sawHosted=GetParent(oracle->child)==oracle->host;
            oracle->childProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(oracle->child,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(binkParentObserveChild)));
            if(!oracle->childProc)oracle->problem="oracle subclass failed";
        }
    }
    if(oracle->child&&IsWindow(oracle->child)&&oracle->action==0) {
        ++oracle->action;
        if(oracle->mode==2)DestroyWindow(oracle->host);
        else if(oracle->mode==3)PostQuitMessage(23);
        else SendMessageW(oracle->child,WM_KEYDOWN,VK_ESCAPE,1);
    }
    if(GetTickCount64()-oracle->started>5000&&oracle->child&&IsWindow(oracle->child)) {
        oracle->problem="parent oracle exceeded unchanged finite movie lifetime";
        SendMessageW(oracle->child,WM_CLOSE,0,0);
    }
    return 0;
}
bool runBinkParentIdentityOracle(const RuntimeConfig& config,std::string& errorMessage) {
    const char* destination=std::getenv("CONCORD_BINK_PARENT_IDENTITY_OUTPUT");
    if(!destination||!*destination){errorMessage="Bink parent oracle needs owned output";return false;}
    const fs::path output(destination);if(fs::exists(output)){errorMessage="Bink parent oracle output exists";return false;}
    std::ofstream trace(output);if(!trace){errorMessage="Bink parent oracle output unavailable";return false;}
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=binkParentTimerProc;cls.lpszClassName=L"ConcordRTS.BinkParentIdentityOracle";
    if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){errorMessage="oracle timer class registration failed";return false;}
    HWND timer=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
    if(!timer){errorMessage="oracle timer window failed";return false;}
    const auto movie=fs::path(config.localDataRootPath)/"resources/mix_exports/movmd03/s07_p01e.bik";
    int checks=0,failures=0;const auto check=[&](bool okay,const char* label){++checks;if(!okay){++failures;trace<<"FAIL,"<<label<<std::endl;}};
    trace<<"mode,play,result,host_alive,standalone,hosted,destroyed,stopped,focus_restored,quit23,refused,error"<<std::endl;
    for(int mode=0;mode<7;++mode) {
        HWND host=CreateWindowExW(0,L"STATIC",L"Owned Bink parent oracle",WS_OVERLAPPEDWINDOW,40,40,800,600,nullptr,nullptr,cls.hInstance,nullptr);
        check(host!=nullptr,"owned host creation");if(!host)break;
        HWND focus=CreateWindowExW(0,L"EDIT",L"focus",WS_CHILD|WS_VISIBLE,4,4,100,25,host,nullptr,cls.hInstance,nullptr);
        ShowWindow(host,SW_SHOW);SetForegroundWindow(host);SetFocus(focus);
        if(mode==5)DestroyWindow(host);
        const int plays=mode==6?2:1;
        for(int play=0;play<plays;++play) {
            BinkParentOracle oracle;oracle.host=host;oracle.focus=focus;oracle.mode=mode;oracle.started=GetTickCount64();binkParentOracle=&oracle;
            HHOOK hook=mode==4?SetWindowsHookExW(WH_CBT,binkParentCbt,nullptr,GetCurrentThreadId()):nullptr;
            if(mode==4)check(hook!=nullptr,"CBT creation refusal hook");
            check(SetTimer(timer,147,10,nullptr)!=0,"independent finite oracle timer");
            std::string videoError;
            const RuntimeWindowParentWin32 videoParent(mode==0?nullptr:host);
            const bool result=runRuntimeBinkVideo(config,videoParent.id(),movie.generic_string(),videoError,0,true);
            KillTimer(timer,147);if(hook)UnhookWindowsHookEx(hook);binkParentOracle=nullptr;
            MSG quit{};const bool quit23=PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)&&quit.wParam==23;
            const bool hostAlive=IsWindow(host)!=FALSE;
            const bool restored=mode==1||mode==3||mode==6?GetFocus()==focus:true;
            const bool childGone=!oracle.child||!IsWindow(oracle.child);
            check(oracle.problem.empty(),"oracle encountered no native failure");
            check(childGone,"own modal window is gone on return");
            check(quit23==(mode==3),"WM_QUIT23 repost policy");
            check(hostAlive==(mode!=2&&mode!=5),"original parent ownership/lifetime");
            if(mode==4) {
                check(!result&&oracle.refused&&videoError=="Bink video page window could not be created","real video create failure");
            } else {
                check(result&&videoError.empty(),"real modal playback returned successfully");
                check(oracle.destroyed&&oracle.stopped,"real WM_DESTROY stops original service and clears state");
                check(oracle.sawStandalone==(mode==0||mode==5),"zero or dead native parent preserves standalone policy");
                check(oracle.sawHosted==(mode!=0&&mode!=5),"live parent receives original child");
                check(restored,"original focus restoration");
            }
            trace<<mode<<','<<play<<','<<result<<','<<hostAlive<<','<<oracle.sawStandalone<<','<<oracle.sawHosted<<','<<oracle.destroyed<<','<<oracle.stopped<<','<<restored<<','<<quit23<<','<<oracle.refused<<','<<videoError<<std::endl;
        }
        if(IsWindow(host))DestroyWindow(host);
    }
    // Identity failures are current-only and stay separate from the eight raw-parent observations.
    RuntimeWindowId revoked;
    { const RuntimeWindowParentWin32 temporary(timer);revoked=temporary.id(); }
    for(const RuntimeWindowId bad:{RuntimeWindowId{999999999},revoked}) {
        std::string rejection;
        check(!runRuntimeBinkVideo(config,bad,movie.generic_string(),rejection,0,true)
            &&rejection=="Bink video parent identity is unavailable","unknown or revoked identity rejected after original first-frame startup");
        rejection.clear();
        check(!runRuntimeBinkVideo(config,bad,"",rejection,0,true)
            &&rejection=="campaign has no authored Bink movie","original empty-movie error retains earlier priority");
    }
    DestroyWindow(timer);
    std::cout<<"Bink parent identity oracle: checks="<<checks<<" failures="<<failures<<" cases=7 playbacks=8"<<std::endl;
    if(failures)errorMessage="Bink parent native lifecycle oracle failed";
    return failures==0;
}
} // namespace
