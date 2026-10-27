// S40 native diagnostic only. Reuses the unmodified P11 production-input
// script/oracle; no second controller or renderer is used for real pages.
int runGpuMenuContract(const RuntimeConfig& config,const fs::path& output) {
    using namespace concord::presentation;
    using namespace concord::presentation::win32;
    int checks=0,failures=0;
    const auto check=[&](bool ok,const std::string& what){++checks;if(!ok){++failures;std::cerr<<"S40 "<<what<<'\n';}};
    if(fs::exists(output)){std::cerr<<"S40 preserve existing evidence\n";return 1;}
    fs::create_directories(output);
    struct CursorRestore {POINT p{};CursorRestore(){GetCursorPos(&p);}~CursorRestore(){SetCursorPos(p.x,p.y);}} cursor;
    // Warm per-process native/theme/D3D dependencies before strict balances.
    for(int gpu:{0,1,2}) {
        SecondWindowVisit visit(false,checks,failures,output/(gpu==2?"warm-warp":gpu?"warm-gpu":"warm-software"));
        if(visit.open(config)) {
            if(gpu){visit.state.menuRequest.menuBackend=FrameBackend::D3D11;visit.state.menuRequest.menuDevice=gpu==2?FrameDevice::Warp:FrameDevice::Hardware;visit.pump();}
            visit.resize(810,610);visit.resize(800,600);visit.key(VK_DOWN);visit.pump();
            visit.message(WM_CLOSE);visit.settle();
        }
    }
    const auto dumpWindows=[](const char* label) {
        std::cout<<"S40 native thread windows "<<label<<'\n';
        EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL {
            wchar_t name[256]{};GetClassNameW(window,name,256);
            std::wcout<<L"S40 owned class="<<name<<L" visible="<<IsWindowVisible(window)<<L"\n";return TRUE;
        },0);
    };
    dumpWindows("after-warm");
    const auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    const auto user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    std::cout<<"S40 native baseline gdi="<<gdi<<" user="<<user<<'\n';
    const bool resourceOnly=std::getenv("CONCORD_GPU_MENU_RESOURCE_ONLY")!=nullptr;
    if(resourceOnly) {
        for(int visitNumber=0;visitNumber<8;++visitNumber) {
            {
                SecondWindowVisit visit(false,checks,failures,output/("resource-"+std::to_string(visitNumber)));
                if(visit.open(config)) {
                    visit.state.menuRequest.menuBackend=FrameBackend::D3D11;
                    visit.state.menuRequest.menuDevice=visitNumber%2?FrameDevice::Warp:FrameDevice::Hardware;
                    visit.pump();visit.resize(810,610);visit.resize(800,600);
                    check(visit.state.menuGpu&&visit.state.menuGpu->active(),"resource visit actual raster owner");
                    visit.message(WM_CLOSE);visit.settle();
                }
            }
            const auto actualGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),actualUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
            std::cout<<"S40 native after visit="<<visitNumber<<" gdi="<<actualGdi<<" user="<<actualUser<<'\n';
            dumpWindows("after-resource-visit");
            check(actualGdi==gdi,"resource-only strict GDI balance");check(actualUser==user,"resource-only strict USER balance");
        }
        std::ofstream result(output/"result.json");result<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<"}\n";
        return failures?1:0;
    }
    std::uint64_t compared=0;
    for(bool accepted:{false,true}) {
        std::vector<SecondWindowSample> reference;std::string inputs;
        {std::vector<std::string> nativeTrace;SecondWindowVisit visit(false,checks,failures,output/(accepted?"software-accept":"software-cancel"));
            if(visit.open(config)){visit.state.menuInputTrace=&nativeTrace;visit.script(accepted);reference=std::move(visit.samples);inputs=visit.inputs.str();
                std::ofstream trace(visit.output/"native-input.txt");for(const auto& row:nativeTrace)trace<<row<<'\n';}}
        check(!reference.empty(),"software reference produced all observations");
        for(auto driver:{FrameDevice::Hardware,FrameDevice::Warp}) {
            const std::string name=driver==FrameDevice::Hardware?"hardware":"warp";
            D3D11FrameProbeWin32 probe;probe.captureUpload=true;
            {std::vector<std::string> nativeTrace;SecondWindowVisit visit(false,checks,failures,output/(name+(accepted?"-accept":"-cancel")));
                if(visit.open(config)) {
                    visit.state.menuInputTrace=&nativeTrace;
                    const auto softwareBefore=visit.state.softwareCompositions;
                    visit.state.menuRequest.menuBackend=FrameBackend::D3D11;visit.state.menuRequest.menuDevice=driver;
                    visit.state.menuProbe=&probe;visit.pump();
                    check(visit.state.menuGpu&&visit.state.menuGpu->active(),name+" actual GPU owner");
                    if(visit.state.menuGpu&&visit.state.menuGpu->active()) {
                        check(visit.state.menuGpu->report().softwareDevice==(driver==FrameDevice::Warp),name+" actual device classification");
                        check(visit.state.menuGpu->timingCount()==0,"diagnostic readback excludes timing");
                    }
                    visit.script(accepted);
                    std::ofstream trace(visit.output/"native-input.txt");for(const auto& row:nativeTrace)trace<<row<<'\n';
                    check(visit.state.softwareCompositions==softwareBefore,"complete GPU pages execute zero CPU composition passes");
                    check(!visit.state.menuGpu,"close releases GPU before cached menu state is retained");
                    check(inputs==visit.inputs.str(),"same native input messages");
                    check(reference.size()==visit.samples.size(),"same complete observation count");
                    for(std::size_t i=0;i<std::min(reference.size(),visit.samples.size());++i) {
                        const auto& a=reference[i];const auto& b=visit.samples[i];
                        check(a.label==b.label&&a.width==b.width&&a.height==b.height,"actual frame dimensions "+a.label);
                        check(a.controller==b.controller,"all input/controller/setup/authority fields "+a.label);
                        std::uint64_t different=0;
                        if(a.pixels.size()!=b.pixels.size())different=1;
                        else for(std::size_t j=0;j<a.pixels.size();++j)if(j%4!=3){++compared;different+=a.pixels[j]!=b.pixels[j];}
                        check(different==0,"all displayed BGR "+name+" "+a.label+" differences="+std::to_string(different));
                    }
                }
            }
            check(probe.readbacks>0,"GPU raster result was actually read in diagnostic phase");
            check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"strict GDI balance after "+name);
            const auto actualUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
            std::cout<<"S40 native after "<<name<<" accepted="<<accepted<<" gdi="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<" user="<<actualUser<<" expectedUser="<<user<<'\n';
            check(actualUser==user,"strict USER balance after "+name);
        }
    }
    // Actual shader/API errors and explicit upload/device-loss seams must make
    // a one-way transition before the original GDI renderer resumes.
    for(int fault=0;fault<5;++fault) {
        D3D11FrameProbeWin32 probe;
        SecondWindowVisit visit(false,checks,failures,output/("fallback-"+std::to_string(fault)));
        if(!visit.open(config))continue;
        visit.state.menuRequest.menuBackend=FrameBackend::D3D11;visit.state.menuProbe=&probe;
        visit.pump();check(visit.state.menuGpu&&visit.state.menuGpu->active(),"pre-fault GPU active");
        if(fault==0)probe.failShader=true;
        if(fault==1)probe.failUpload=true;
        if(fault==2)probe.failPresent=true;
        if(fault==3)probe.deviceRemoved=true;
        if(fault==4)probe.failResize=true;
        const auto softwareBefore=visit.state.softwareCompositions;
        if(fault==4)visit.resize(810,610);else visit.pump();
        check(visit.state.menuGpu&&!visit.state.menuGpu->active(),"GPU resources retired on fault");
        check(visit.state.softwareCompositions>softwareBefore,"original CPU/GDI fallback really rendered");
        const auto drawCount=visit.state.menuGpu?visit.state.menuGpu->report().menuDraws:0;
        visit.pump();check(!visit.state.menuGpu||visit.state.menuGpu->report().menuDraws==drawCount,"permanent fallback, no alternating HWND owners");
        visit.sample("fallback-visible");
        if(!visit.samples.empty()) {
            const auto& actual=visit.samples.back();concord::ui::MenuPixelBuffer expected;
            check(expected.reset(actual.width,actual.height),"fallback oracle allocation");
            concord::ui::clearMenuPixels(expected.view());
            scaleSkirmishClientFrame(expected.view(),std::as_const(visit.state.composeFrame).view(),
                {visit.state.viewLeft,visit.state.viewTop,visit.state.viewLeft+visit.state.viewWidth,visit.state.viewTop+visit.state.viewHeight});
            std::uint64_t differences=0;
            for(std::size_t i=0;i<std::min(expected.bytes().size(),actual.pixels.size());++i)
                if(i%4!=3)differences+=expected.bytes()[i]!=actual.pixels[i];
            check(expected.bytes().size()==actual.pixels.size()&&differences==0,"full fallback client BGR equals software reference");
        }
        if(visit.state.menuGpu)check(visit.state.menuGpu->report().failureCode<0,"actual failing HRESULT recorded");
        visit.message(WM_CLOSE);visit.settle();
    }
    std::ofstream summary(output/"result.json");
    summary<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"comparedBgrBytes\":"<<compared
        <<",\"fullPages\":true,\"authorityTicks\":0,\"displayFpsMeasured\":false}\n";
    std::cout<<"S40 complete GPU menu: checks="<<checks<<" failures="<<failures<<" BGR bytes="<<compared<<'\n';
    return failures?1:0;
}
