int runGpuMenuTiming(const RuntimeConfig& config,const fs::path& output) {
    using namespace concord::presentation;
    using namespace concord::presentation::win32;
    using TimingClock=std::chrono::steady_clock;
    int checks=0,failures=0;
    const auto check=[&](bool ok,const std::string& what){++checks;if(!ok){++failures;std::cerr<<"S40 timing "<<what<<'\n';}};
    if(fs::exists(output))return 1;
    fs::create_directories(output);
    struct CursorRestore {POINT p{};CursorRestore(){GetCursorPos(&p);}~CursorRestore(){SetCursorPos(p.x,p.y);}} cursor;
    SYSTEM_POWER_STATUS power{};GetSystemPowerStatus(&power);
    std::ofstream meta(output/"environment.json");
    meta<<"{\"acLineStatus\":"<<static_cast<int>(power.ACLineStatus)<<",\"warmup\":60,\"samples\":180,\"rounds\":5,\"width\":1280,\"height\":720,\"logicalWidth\":800,\"logicalHeight\":600,\"softwarePacingMs\":16.666667,\"d3dVsync\":1,\"displayFpsMeasured\":false,\"readback\":false,\"inputToScanoutMeasured\":false,\"temperatureOrClockLocked\":false}\n";
    for(int round=0;round<5;++round)for(bool hardware:{false,true}) {
        const std::string name=std::string(hardware?"hardware-":"software-")+std::to_string(round+1);
        SecondWindowVisit visit(false,checks,failures,output/name);
        if(!visit.open(config))continue;
        KillTimer(visit.handle,2);visit.resize(1280,720);
        visit.state.mouseVisible=true;visit.state.phase=ShellMenuPhase::Idle;
        D3D11FrameProbeWin32 probe; // captureUpload stays false for ALL samples.
        if(hardware) {
            visit.state.menuRequest.menuBackend=FrameBackend::D3D11;visit.state.menuProbe=&probe;visit.pump();
            check(visit.state.menuGpu&&visit.state.menuGpu->active(),"actual hardware raster owner");
            if(!visit.state.menuGpu||!visit.state.menuGpu->active())continue;
            check(!visit.state.menuGpu->report().softwareDevice,"WARP excluded from hardware timing");
            visit.state.menuGpu->enableTimingOutput();
        }
        DWORD process=0;GetWindowThreadProcessId(visit.handle,&process);
        check(process==GetCurrentProcessId()&&IsWindowVisible(visit.handle)&&!IsIconic(visit.handle),"visible owned client");
        check((GetWindowLongPtrW(visit.handle,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"topmost timing window");
        for(POINT point:{POINT{100,100},POINT{640,360},POINT{1180,650}}) {
            ClientToScreen(visit.handle,&point);check(WindowFromPoint(point)==visit.handle,"actual visible client point belongs to target");
        }
        const auto softwareBefore=visit.state.softwareCompositions;
        std::ofstream csv(output/(name+".csv"));
        csv<<"sample,sequence,cpuPrepareMs,submitUploadCpuMs,gpuRasterMs,gpuValid,gpuFrequency,gpuStart,gpuEnd,presentCpuMs,uploadBytes,visibleSubmissionIntervalMs,totalCpuMs\n";
        struct Row {int sample;std::uint64_t sequence;double prepare,submit,present,total,interval;};
        std::vector<Row> rows;
        auto previous=TimingClock::now();
        HDC dc=GetDC(visit.handle);check(dc!=nullptr,"native software destination owned by visit");
        for(int i=0;i<240&&dc;++i) {
            const auto start=TimingClock::now();
            // Same deterministic UI-only animation/mouse input in both modes.
            visit.state.mousePoint={120+i%400,100+i%300};visit.state.backDirty=true;
            const bool ok=renderShellClient(visit.state,dc,1280,720);check(ok,"actual complete production paint");
            const auto end=TimingClock::now();
            const double total=std::chrono::duration<double,std::milli>(end-start).count();
            const double interval=std::chrono::duration<double,std::milli>(end-previous).count();previous=end;
            if(i>=60)rows.push_back({i-60,visit.state.menuSequence,visit.state.lastMenuPrepareMs,
                visit.state.lastMenuSubmitMs,visit.state.lastMenuPresentMs,total,interval});
            // Reproduce the nominal 16-ms Win32 menu timer without dispatching
            // unrelated paints between the explicitly counted samples.
            const auto budget=std::chrono::duration<double,std::milli>(16.666667)-std::chrono::duration<double,std::milli>(TimingClock::now()-start);
            if(budget.count()>0)std::this_thread::sleep_for(budget);
        }
        if(dc)ReleaseDC(visit.handle,dc);
        if(hardware) {
            check(visit.state.softwareCompositions==softwareBefore,"no CPU composition in measured GPU path");
            check(probe.readbacks==0,"no diagnostic output readbacks in timing");
            check(visit.state.menuGpu->report().gpuValid>0,"asynchronous GPU raster timestamps available");
        }
        for(const auto& row:rows) {
            const D3D11FrameTiming* sample=nullptr;
            if(hardware)for(std::size_t j=0;j<visit.state.menuGpu->timingCount();++j)
                if(visit.state.menuGpu->timings()[j].sequence==row.sequence)sample=visit.state.menuGpu->timings()+j;
            csv<<row.sample<<','<<row.sequence<<','<<row.prepare<<','<<row.submit<<','<<(sample?sample->gpuUploadMs:0)<<','
                <<(sample&&sample->gpuValid)<<','<<(sample?sample->gpuFrequency:0)<<','<<(sample?sample->gpuStart:0)<<','<<(sample?sample->gpuEnd:0)<<','
                <<row.present<<','<<(sample?sample->uploadBytes:0)<<','<<row.interval<<','<<row.total<<'\n';
        }
        check(rows.size()==180,"fixed warmup and complete sampling window");
        if(hardware) {
            std::ofstream packed(output/(name+"-resources.bin"),std::ios::binary);
            packed.write(reinterpret_cast<const char*>(visit.state.menuCommands.resources.data()),static_cast<std::streamsize>(visit.state.menuCommands.resources.size()*4));
            check(packed.good(),"save actual owned upload input after timing");
            if(round==0) {
                std::ofstream font(output/"actual-font.bin",std::ios::binary);
                font.write(reinterpret_cast<const char*>(visit.state.gameFont.bytes.data()),static_cast<std::streamsize>(visit.state.gameFont.bytes.size()));
                check(font.good(),"save original font byte identity after timing");
            }
        }
        // Sampling disabled the timer; restore the original close animation clock.
        SetTimer(visit.handle,2,16,nullptr);
        SendMessageW(visit.handle,WM_CLOSE,0,0);visit.settle();
    }
    std::ofstream result(output/"result.json");result<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"displayFpsMeasured\":false}\n";
    return failures?1:0;
}
