// Current-only real SDK edges. Original ten qualified lifecycle scenes remain intact.
// CPU VQA close is verified; this fixture does not claim audible device output.
namespace {
struct CampaignHostEdgeProbe {
    CampaignHostProbe* base=nullptr;
    unsigned checks=0,failures=0,ticks=0;
    bool exercised=false;
    void require(bool value,const char* why){++checks;if(!value){++failures;std::cerr<<"Campaign host edge: "<<why<<"\n";}}
};
thread_local CampaignHostEdgeProbe* campaignHostEdgeProbe=nullptr;
void CALLBACK campaignHostEdgeTimer(HWND,UINT,UINT_PTR,DWORD){
    if(!campaignHostEdgeProbe)return;auto& e=*campaignHostEdgeProbe;auto& p=*e.base;++e.ticks;
    if(e.exercised)return;
    if(p.page&&IsWindow(p.page)&&IsWindowVisible(p.page)){
        e.exercised=true;
        auto* state=reinterpret_cast<CampaignMenuState*>(GetWindowLongPtrW(p.page,GWLP_USERDATA));
        e.require(state!=nullptr,"actual public state available");
        if(!state){PostMessageW(p.page,WM_CLOSE,0,0);return;}
        e.require(GetFocus()==p.page,"initial actual focus");
        SetWindowPos(p.page,nullptr,0,0,1024,640,SWP_NOACTIVATE|SWP_NOZORDER);
        RECT size{};GetClientRect(p.page,&size);
        e.require(size.right==1024&&size.bottom==640,"actual resized client dimensions");
        state->pressedControl=0;state->hoveredControl=1;state->focusedControl=2;
        SendMessageW(p.page,WM_CANCELMODE,0,0);
        e.require(state->pressedControl==-1&&state->hoveredControl==-1&&state->focusedControl==-1,"cancel clears all highlights");
        HWND sibling=CreateWindowExW(0,L"STATIC",L"Owned Campaign focus/capture",WS_CHILD|WS_VISIBLE,1,1,8,8,p.parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        e.require(sibling!=nullptr,"owned sibling creation");
        if(sibling){
            SetCapture(p.page);state->pressedControl=0;state->mapPresentation.pressedColor=3;SetCapture(sibling);
            e.require(GetCapture()==sibling&&state->pressedControl==-1&&state->mapPresentation.pressedColor==0,"real capture transfer clears press");ReleaseCapture();
            state->hoveredControl=2;state->focusedControl=2;SetFocus(sibling);
            e.require(GetFocus()==sibling&&state->hoveredControl==-1&&state->focusedControl==-1,"real focus loss resets input");
            SetFocus(p.page);e.require(GetFocus()==p.page,"real focus return");DestroyWindow(sibling);
        }
        ShowWindow(p.page,SW_MINIMIZE);e.require(IsIconic(p.page)!=FALSE,"actual child minimized");
        ShowWindow(p.page,SW_RESTORE);e.require(IsIconic(p.page)==FALSE&&IsWindowVisible(p.page),"actual child restored");
        if(!p.loadGame){
            e.require(state->mapPresentation.vqa.loadFromMemory(makeCampaignClockEightFrames()),"actual original CPU VQA decoder loaded");
            e.require(state->mapPresentation.vqa.isLoaded()&&state->mapPresentation.vqa.frameCount()==8U,"actual eight-frame media active before close");
            state->mapPresentation.phase=CampaignMapPresentationPhase::VqaIntro;
            state->page=CampaignPage::Missions;
            SetCapture(p.page);
        }
        SendMessageW(p.page,WM_CLOSE,0,0);
        e.require(!IsWindow(p.page)&&GetCapture()!=p.page,"actual close destroys page and releases capture");
    }
    if(e.ticks>500){e.exercised=true;e.require(false,"current native edge deadline");if(p.page&&IsWindow(p.page))SendMessageW(p.page,WM_CLOSE,0,0);else PostQuitMessage(99);}
}
}
int runRuntimeCampaignHostEdgesSmoke(const RuntimeConfig& config){
    unsigned checks=0,failures=0;
    HWND parent=CreateWindowExW(0,L"STATIC",L"Owned Campaign reentry edges",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,80,80,1100,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!parent){std::cerr<<"Campaign host edge parent creation failed\n";return 1;}ShowWindow(parent,SW_SHOW);UpdateWindow(parent);
    for(bool loadGame:{false,true}){
        CampaignHostProbe base;base.parent=parent;base.loadGame=loadGame;base.thread=GetCurrentThreadId();CampaignHostEdgeProbe edge;edge.base=&base;
        campaignHostProbe=&base;campaignHostEdgeProbe=&edge;
        HHOOK hook=SetWindowsHookExW(WH_CBT,campaignHostProbeHook,nullptr,base.thread);edge.require(hook!=nullptr,"own page observer");
        UINT_PTR timer=SetTimer(nullptr,0,20,campaignHostEdgeTimer);edge.require(timer!=0,"own native timer");
        const auto result=invokeCampaignHostProbe(&runRuntimeCampaignMenu,config,parent,loadGame);
        KillTimer(nullptr,timer);if(hook)UnhookWindowsHookEx(hook);campaignHostProbe=nullptr;campaignHostEdgeProbe=nullptr;
        edge.require(edge.exercised&&base.creates==1,"actual public reentry page");edge.require(!result.accepted&&!result.loadedFromSave&&!result.loadedSkirmishSave,"original cancelled default result");edge.require(!IsWindow(base.page)&&IsWindow(parent),"page retired caller parent survives");
        checks+=edge.checks;failures+=edge.failures;
    }
    const auto unknown=runRuntimeCampaignMenu(config,RuntimeWindowId{~std::uint64_t{0}},false);
    ++checks;if(unknown.accepted||unknown.loadedFromSave||unknown.loadedSkirmishSave||!unknown.savePath.empty())++failures;
    DestroyWindow(parent);
    std::cout<<"Campaign controller host edges: checks="<<checks<<" failures="<<failures<<" skipped=0 reentry=2 resize=1024x640 minimizeRestore=1 focusReturn=1 captureLoss=1 activeCpuVqaClose=1 audioDevice=not-run unknownIdentity=1\n";
    return failures?1:0;
}
