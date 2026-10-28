// Fixed original resources/camera/tick; the real production WM_PAINT performs
// every sample. No screenshot/readback probe, no simulation/input loop.
bool runRuntimeMapGpuTiming(RuntimeWindowState& state,std::string& error)try {
    using namespace RuntimeMainFrameContractDetail;
    using namespace concord::presentation;
    using namespace concord::presentation::win32;
    const auto require=[](bool value,const std::string& why){if(!value)throw std::runtime_error(why);};
    const char* requested=std::getenv("CONCORD_S41_MAP_TIMING");require(requested!=nullptr,"map timing driver required");
    const std::string mode=requested;require(mode=="software"||mode=="hardware"||mode=="warp","map timing driver");
    const bool gpuRequested=mode!="software";
    state.runtimePreferencePersistenceEnabled=false;state.runtimeSoundsEnabled=false;state.runtimeMusicEnabled=false;
    state.runtimeSoundEffectsEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;state.runtimeMenuOpen=false;
    state.playerLocalCommands.cameraFollow=false;state.runtimeSoundVolume=state.runtimeVoiceVolume=0;
    prepareRuntimeMapGpuFixtureScene(state);state.frameProbeWin32=nullptr;state.mapFrameProbeWin32=nullptr;
    RuntimeWindowHostWin32 host(runtimeWindowProc);require(host.create(800,600)==RuntimeWindowCreateResult::Created,"map timing real host");
    HWND window=host.window();host.attachSession(reinterpret_cast<LONG_PTR>(&state));
    D3D11FrameProbeWin32 probe; // captureUpload remains false: no GPU output readback.
    struct Cleanup{RuntimeWindowState& state;RuntimeWindowHostWin32& host;~Cleanup(){if(host.window()){host.attachSession(0);DestroyWindow(host.window());}
        state.mapComposition.release();state.d3d11Presenter.reset();releaseRuntimeBackBuffer(state.backBuffer);releaseRuntimeBackBuffer(state.completedFrameBuffer);
        releaseRuntimeBackBuffer(state.sidebarStaticBuffer);releaseRuntimeBackBuffer(state.radarTerrainBuffer);(void)state.fpsDeviceOverlay.release();}} cleanup{state,host};
    if(gpuRequested){state.frameBackendRequest={FrameBackend::D3D11,mode=="warp"?FrameDevice::Warp:FrameDevice::Hardware};
        state.frameBackendRequest.mapBackend=FrameBackend::D3D11;state.frameBackendRequest.mapDevice=state.frameBackendRequest.device;
        state.d3d11Presenter=D3D11FramePresenterWin32::create(window,state.frameBackendRequest,state.softwareFrameCounters,&probe);
        require(state.d3d11Presenter&&state.d3d11Presenter->active(),"actual requested map timing device");state.d3d11Presenter->enableTimingOutput();state.frameBackendInitialized=true;}
    require(SetWindowPos(window,HWND_TOPMOST,24,24,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE,"map timing actual visible window");
    const auto client=mapMainFrameClientRect(window);require(client&&client->right==800&&client->bottom==600,"map timing exact client");
    std::string hashError;std::vector<std::pair<std::string,std::uint64_t>> beforeTrace;const auto authority=runtimeStandaloneSimulationStateHash(state,hashError,&beforeTrace);require(authority&&hashError.empty(),"map timing complete authority");const int tick=state.tick;
    constexpr int warmup=4,samples=120;double warmupMs=0;std::int64_t measurementBeginUnixMs=0;std::chrono::steady_clock::time_point previous{};
    for(int index=-warmup;index<samples;++index){
        state.repaintRequested=true;const auto before=gpuRequested?state.d3d11Presenter->report().presented:state.softwareFrameCounters.successful;
        if(index==0)measurementBeginUnixMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto begin=std::chrono::steady_clock::now();require(InvalidateRect(window,nullptr,FALSE)!=FALSE,"normal map invalidate");SendMessageW(window,WM_PAINT,0,0);
        const auto end=std::chrono::steady_clock::now();const double total=std::chrono::duration<double,std::milli>(end-begin).count();
        const auto after=gpuRequested?state.d3d11Presenter->report().presented:state.softwareFrameCounters.successful;require(after==before+1,"one actual normal visible submission per paint");
        require(state.lastSelectedHealthBars>=1,"normal timing selected interaction overlay");
        require(state.renderStats.behindMarkerSprites==1&&state.renderStats.behindMarkerMissing==0,"visible original BEHIND in normal timing");
        if(gpuRequested){require(state.d3d11Presenter->active()&&state.mapComposition.native().report().indexedDraws>=1
            &&state.mapComposition.native().report().visionEffects>0,"actual selected map GPU paths, no fallback or empty workload");require(probe.readbacks==0,"normal GPU readbacks must remain zero");}
        if(index<0)warmupMs+=total;
        else {const double gap=previous.time_since_epoch().count()?std::chrono::duration<double,std::milli>(end-previous).count():0;
            std::cout<<"S41 normal map sample index="<<index<<" driver="<<mode<<" tick="<<tick<<" totalCpuMs="<<total
                <<" reportedPaintMs="<<state.perf.lastPaintMs<<" mapPreparationMs="<<state.perf.lastMapDrawMs<<" visibleCompletionGapMs="<<gap
                <<" nativeCompileMs="<<(gpuRequested?state.mapComposition.native().report().compileMs:0)
                <<" nativeCache="<<(gpuRequested&&state.mapComposition.native().report().cacheHit)<<'\n';}
        previous=end;
    }
    const auto pickClient=mapMainFrameClientRect(window);require(pickClient&&runtimeMapGpuFixturePositivePick(state,*pickClient),"normal timing original positive authoritative hit");
    std::vector<std::pair<std::string,std::uint64_t>> afterTrace;const auto afterAuthority=runtimeStandaloneSimulationStateHash(state,hashError,&afterTrace);
    if(afterAuthority!=authority||state.tick!=tick||!hashError.empty()) {
        std::cerr<<"Normal map authority mismatch before="<<authority<<" after="<<afterAuthority<<" tickBefore="<<tick<<" tickAfter="<<state.tick<<" error="<<hashError<<'\n';
        const auto count=std::min(beforeTrace.size(),afterTrace.size());for(std::size_t i=0;i<count;++i)if(beforeTrace[i]!=afterTrace[i]) {
            std::cerr<<"Normal map first authority difference block="<<beforeTrace[i].first<<" before="<<beforeTrace[i].second<<" after="<<afterTrace[i].second<<'\n';break;}
    }
    require(afterAuthority==authority&&hashError.empty()&&state.tick==tick,"normal timing complete authority/tick unchanged");
    if(gpuRequested){require(state.d3d11Presenter->timingCount()==samples+warmup&&probe.readbacks==0,"all normal GPU timing samples, no diagnostic readback");state.d3d11Presenter->printReport();}
    const auto measurementEndUnixMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    proveRuntimeMapGpuTerrainPacket(state,nullptr,"normal-timing-postinterval",gpuRequested);
    std::cout<<"S41 normal map interval beginUnixMs="<<measurementBeginUnixMs<<" endUnixMs="<<measurementEndUnixMs<<'\n';
    std::cout<<"S41 normal map timing passed driver="<<mode<<" samples="<<samples<<" warmup="<<warmup<<" warmupPaintMs="<<warmupMs
        <<" width=800 height=600 tick="<<tick<<" authority="<<authority<<" diagnosticReadbacks="<<probe.readbacks
        <<" softwarePacing=uncapped-diagnostic-paint-loop gpuVsync=1 displayFpsMeasured=false physicalInputToDisplayMeasured=false\n";
    SendMessageW(window,WM_CLOSE,0,0);require(!host.window()&&!state.backBuffer.memoryDc&&!state.completedFrameBuffer.memoryDc&&!state.fpsDeviceOverlay.dc(),"normal timing real close releases owners");
    require(!host.drainMessages(),"normal timing quit drained");return true;
}catch(const std::exception& e){error=e.what();return false;}
