// W014-03: only windows owned by this diagnostics process are operated on.
int runSkirmishWindowLifecycleSmoke(const RuntimeConfig& config, const fs::path& output) {
    int checks=0,failures=0;
    const auto check=[&](bool valid,const char* message) {
        ++checks;if(!valid){++failures;std::cerr<<"Skirmish lifecycle: "<<message<<'\n';}
    };
    fs::create_directories(output.parent_path());
    // A cold USER/theme/MCI process retains its own shared GDI resources.
    // The independent no-game probe records 0 -> 6 (show) -> 10 (MCI) -> 13
    // (minimize/restore). Establish these OS dependencies before measuring
    // owned menu visits; the per-visit equality assertion below stays strict.
    const DWORD cold=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    HWND bootstrap=CreateWindowExW(0,L"STATIC",L"Owned platform initialization",
        WS_OVERLAPPEDWINDOW,0,0,80,60,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(bootstrap!=nullptr,"create owned platform bootstrap");
    if(!bootstrap)return 1;
    ShowWindow(bootstrap,SW_SHOWNOACTIVATE);UpdateWindow(bootstrap);
    const MCIERROR unusedStop=mciSendStringW(L"stop concord_lifecycle_probe_unused",nullptr,0,nullptr);
    const MCIERROR unusedClose=mciSendStringW(L"close concord_lifecycle_probe_unused",nullptr,0,nullptr);
    check(unusedStop!=0&&unusedClose!=0,"probe alias owns no music device");
    ShowWindow(bootstrap,SW_MINIMIZE);check(IsIconic(bootstrap)!=FALSE,"bootstrap minimizes");
    ShowWindow(bootstrap,SW_RESTORE);check(IsIconic(bootstrap)==FALSE,"bootstrap restores");
    check(DestroyWindow(bootstrap)!=FALSE,"dispose owned bootstrap window");GdiFlush();
    std::cout<<"Skirmish lifecycle platform initialization: cold="<<cold
        <<" initialized="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<'\n';
    for(int visit=0;visit<3;++visit) {
        const DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        std::vector<HGDIOBJ> menuObjects;
        SkirmishGdiReleaseTrace disposal;
        {
            struct OwnedHost {
                HWND window=nullptr;
                ~OwnedHost(){if(window!=nullptr&&IsWindow(window))DestroyWindow(window);}
            } host;
            if(visit<2) host.window=CreateWindowExW(0,L"STATIC",L"Owned skirmish lifecycle host",
                WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,30,30,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            check(visit==2||host.window!=nullptr,"create owned parent");
            if(visit<2&&host.window==nullptr)continue;
            MenuState state;
            check(initializeState(state,config,true),"prepare actual resources");
            if(!state.initialized)continue;
            state.gdiReleaseTrace=&disposal;
            HWND window=createShellSkirmishWindow(state,config,host.window);
            check(window!=nullptr,"create actual skirmish page");if(!window)continue;
            check(state.hwnd==window&&GetWindowThreadProcessId(window,nullptr)==GetCurrentThreadId(),"attached on UI thread");
            check(GetParent(window)==host.window,"typed private parent link");
            if(host.window)ShowWindow(host.window,SW_SHOWNOACTIVATE);
            ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
            check(!state.composeFrame.bytes().empty()&&!state.hits.empty(),"UpdateWindow prevents paint starvation");
            menuObjects={reinterpret_cast<HGDIOBJ>(state.backBuffer.dc()),state.backBuffer.bitmap(),
                state.titleFont,state.captionFont,state.normalFont,state.smallFont};
            for(const auto object:menuObjects)check(object!=nullptr&&GetObjectType(object)!=0,"menu owns a valid GDI object");
            for(const auto size:{std::pair{800,600},std::pair{1024,640},std::pair{400,300},std::pair{1600,1200}}) {
                const DWORD style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE));
                RECT desired{0,0,size.first,size.second};
                if(!host.window)AdjustWindowRectEx(&desired,style,FALSE,0);
                const BOOL sized=SetWindowPos(window,nullptr,0,0,desired.right-desired.left,desired.bottom-desired.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE);
                RECT client{};GetClientRect(window,&client);
                check(sized&&client.right==size.first&&client.bottom==size.second,"real resize client dimensions");
                RuntimeSkirmishController expected(state.services);computeShellViewport(expected,size.first,size.second);
                check(state.viewLeft==expected.viewLeft&&state.viewTop==expected.viewTop&&state.viewWidth==expected.viewWidth&&state.viewHeight==expected.viewHeight,"native resize consumes same viewport controller");
                InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
                check(!state.backDirty,"resized frame synchronously completed");
            }
            HWND root=host.window?host.window:window;
            ShowWindow(root,SW_MINIMIZE);check(IsIconic(root)!=FALSE,"real minimize");
            ShowWindow(root,SW_RESTORE);check(IsIconic(root)==FALSE&&IsWindow(window),"real restore");
            if(!host.window){ShowWindow(root,SW_MAXIMIZE);check(IsZoomed(root)!=FALSE,"real maximize");ShowWindow(root,SW_RESTORE);check(!IsZoomed(root),"real restore maximized");}
            state.phase=ShellMenuPhase::Idle;state.backDirty=true;
            InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
            const auto slider=std::find_if(state.hits.begin(),state.hits.end(),[](const HitRegion& hit){return hit.target.kind==HitKind::SpeedSlider;});
            check(slider!=state.hits.end(),"actual slider hit region");
            if(slider!=state.hits.end()) {
                const int x=state.viewLeft+(slider->rect.left+slider->rect.right)/2*state.viewWidth/kBaseWidth;
                const int y=state.viewTop+(slider->rect.top+slider->rect.bottom)/2*state.viewHeight/kBaseHeight;
                SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
                check(GetCapture()==window&&state.draggingSlider==HitKind::SpeedSlider,"native slider capture");
                HWND other=CreateWindowExW(0,L"STATIC",L"Owned capture target",WS_CHILD,0,0,10,10,root,nullptr,GetModuleHandleW(nullptr),nullptr);
                check(other!=nullptr,"owned capture peer");
                if(other){SetCapture(other);check(state.draggingSlider==HitKind::None&&state.pressed.kind==HitKind::None,"synchronous native capture change");if(GetCapture()==other)ReleaseCapture();DestroyWindow(other);}
                SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
                SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(-40,-40));
                check(GetCapture()!=window&&state.draggingSlider==HitKind::None&&state.pressed.kind==HitKind::None,"native outside release");
            }
            SetFocus(window);if(host.window)SetFocus(host.window);
            check(IsWindow(window)&&!state.closing,"focus change preserves open menu");
            if(visit==0) {
                state.mouseVisible=false;state.backDirty=true;renderShellBackBuffer(state);
                std::string frameError;
                check(writeBgraBmp(pathString(output),state.backBuffer.pixels(),kBaseWidth,kBaseHeight,frameError),"write actual frame");
                if(!frameError.empty())std::cerr<<frameError<<'\n';
            }
            if(visit==1) {
                DestroyWindow(host.window);
                check(!IsWindow(window)&&state.hwnd==nullptr&&state.randomNameWindow==nullptr&&!state.selection.accepted,"parent-first destruction detaches child");
            } else {
                state.phase=ShellMenuPhase::SlideIn;state.phaseStarted=GetTickCount64();
                SendMessageW(window,WM_CLOSE,0,0);
                check(state.phase==ShellMenuPhase::SlideOut&&!state.selection.accepted&&IsWindow(window),"close during slide-in begins cancellation");
                state.phaseStarted=GetTickCount64()-1000;
                SendMessageW(window,WM_TIMER,2,0);
                check(!IsWindow(window)&&state.hwnd==nullptr&&state.closing,"native timer completes close and detaches");
                check(host.window==nullptr||IsWindow(host.window),"child close preserves parent");
            }
        }
        const DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        check(disposal.count==7,"exactly one restore and six owned releases");
        check(disposal.order==std::array<int,7>{0,1,2,3,4,5,6},"restore then bitmap/DC/fonts release order");
        for(const bool success:disposal.succeeded)check(success,"native GDI restore or release succeeded");
        check(after==before,"owned GDI resource balance");
        std::cout<<"Skirmish lifecycle visit="<<visit<<" gdiBefore="<<before<<" gdiAfter="<<after<<'\n';
    }
    std::cout<<"Skirmish window lifecycle: checks="<<checks<<" failures="<<failures<<" ownedWindowsOnly=yes\n";
    return failures?1:0;
}
