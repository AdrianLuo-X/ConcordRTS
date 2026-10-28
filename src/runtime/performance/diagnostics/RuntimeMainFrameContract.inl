// Same real WM_PAINT before and after S38; records every final client pixel,
// including FPS and cursor. Fixed presentation/UI clocks are private inputs.
namespace RuntimeMainFrameContractDetail {
#include "runtime/performance/diagnostics/RuntimeMapGpuFixtureScene.inl"
#include "runtime/performance/diagnostics/RuntimeMapGpuFixturePick.inl"
#include "runtime/performance/diagnostics/RuntimeMapGpuTerrainProof.inl"
#include "ui/surface/win32/diagnostics/MapMainFrameClientWin32.inl"
#include "ui/surface/win32/diagnostics/MapMainFrameVisibilityWin32.inl"
struct FixedUiClock final : RuntimeUiClock { RuntimeUiTimeMs nowMs() const noexcept override{return 100000;} };
struct Probe final : concord::ui::win32::MapFrameProbeWin32 {
    RuntimeBackBuffer capture;
    std::vector<unsigned char> last,lastComplete;
    bool got=false,success=false,cached=false,complete=false;
    int width=0,height=0;std::string error;
    concord::presentation::win32::D3D11FrameProbeWin32* gpu=nullptr;
    const concord::presentation::win32::D3D11FrameReport* gpuReport=nullptr;
#include "ui/surface/win32/diagnostics/MapMainFrameProbeWin32.inl"
    void observe(HDC dc,int w,int h,bool reused,bool submitted,concord::ui::ConstMenuPixelSurfaceView frame) override {
        got=true;success=submitted;cached=reused;width=w;height=h;complete=frame.valid();
        if(!capture.ensure(dc,w,h)||!BitBlt(capture.memoryDc,0,0,w,h,dc,0,0,SRCCOPY)){error="final client readback failed";return;}
        const auto pixels=capture.completedPixels();if(!pixels.valid()){error="final client flush failed";return;}
        last.assign(pixels.bytes.begin(),pixels.bytes.end());
        lastComplete.clear();
        if(frame.valid()){
            lastComplete.assign(frame.bytes.begin(),frame.bytes.end());
            if(frame.width!=w||frame.height!=h){error="completed frame dimensions";return;}
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(std::size_t c=0;c<3;++c){
                const auto i=static_cast<std::size_t>(y)*pixels.rowStride+static_cast<std::size_t>(x)*4+c;
                const auto j=static_cast<std::size_t>(y)*frame.rowStride+static_cast<std::size_t>(x)*4+c;
                if(last[i]!=frame.bytes[j]){error="complete view/window pixel mismatch x="+std::to_string(x)+" y="+std::to_string(y)+" channel="+std::to_string(c);return;}
            }
            if(gpu&&gpuReport&&gpuReport->selected==concord::presentation::FrameBackend::D3D11){
                if(gpu->uploadedPixels.size()!=static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4){error="actual GPU upload missing";return;}
                for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(std::size_t c=0;c<4;++c){
                    const auto i=(static_cast<std::size_t>(y)*static_cast<std::size_t>(w)+static_cast<std::size_t>(x))*4+c;
                    const auto j=static_cast<std::size_t>(y)*frame.rowStride+static_cast<std::size_t>(x)*4+c;
                    if(gpu->uploadedPixels[i]!=frame.bytes[j]){error="actual GPU upload differs from completed CPU frame";return;}
                }
            }
        }
    }
};
bool sameColor(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(i%4!=3&&a[i]!=b[i])return false;return true;}
std::uint64_t colorHash(const std::vector<unsigned char>& p){std::uint64_t h=14695981039346656037ULL;for(std::size_t i=0;i<p.size();++i)if(i%4!=3)h=(h^p[i])*1099511628211ULL;return h;}
}
bool runRuntimeMainFrameContract(RuntimeWindowState& state,std::string& error)try{
    using namespace RuntimeMainFrameContractDetail;
    const auto require=[&](bool value,const std::string& why){if(!value)throw std::runtime_error(why);};
    const char* configured=std::getenv("CONCORD_MAIN_FRAME_CONTRACT_OUTPUT");require(configured&&*configured,"private frame output required");
    const std::filesystem::path output(configured);require(std::filesystem::create_directory(output),"frame output must be new");
    state.runtimePreferencePersistenceEnabled=false;state.runtimeSoundsEnabled=false;state.runtimeMusicEnabled=false;
    state.runtimeSoundEffectsEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;state.runtimeMenuOpen=false;
    state.playerLocalCommands.cameraFollow=false;state.runtimeSoundVolume=state.runtimeVoiceVolume=0;
    const bool behindFixture=std::getenv("CONCORD_MAIN_FRAME_BEHIND_SCENE")!=nullptr;
    if(behindFixture)prepareRuntimeMapGpuFixtureScene(state);
    FixedUiClock uiClock;const auto* oldClock=state.uiClock;Probe probe;probe.fixedClock=true;probe.seconds=11;
    state.uiClock=&uiClock;state.frameProbeWin32=&probe;state.mapFrameProbeWin32=&probe;
    RuntimeWindowHostWin32 host(runtimeWindowProc);require(host.create(800,600)==RuntimeWindowCreateResult::Created,"real WindowHost create");
    const auto window=host.window();host.attachSession(reinterpret_cast<LONG_PTR>(&state));
    const char* backend=std::getenv("CONCORD_MAIN_FRAME_BACKEND");
    const bool s39Matrix=backend&&*backend;
    const bool s41Map=std::getenv("CONCORD_MAIN_FRAME_MAP_DRAWING")!=nullptr;
    const bool gpuRequested=s39Matrix&&std::strcmp(backend,"software")!=0;
    concord::presentation::win32::D3D11FrameProbeWin32 gpuProbe;
    if(s39Matrix)require(std::strcmp(backend,"software")==0||std::strcmp(backend,"hardware")==0||std::strcmp(backend,"warp")==0,"invalid diagnostic backend");
    if(gpuRequested){
        state.frameBackendRequest={concord::presentation::FrameBackend::D3D11,
            std::strcmp(backend,"warp")==0?concord::presentation::FrameDevice::Warp:concord::presentation::FrameDevice::Hardware};
        if(s41Map){state.frameBackendRequest.mapBackend=concord::presentation::FrameBackend::D3D11;
            state.frameBackendRequest.mapDevice=state.frameBackendRequest.device;}
        gpuProbe.captureUpload=true;probe.gpu=&gpuProbe;
        state.d3d11Presenter=concord::presentation::win32::D3D11FramePresenterWin32::create(window,state.frameBackendRequest,state.softwareFrameCounters,&gpuProbe);
        require(state.d3d11Presenter&&state.d3d11Presenter->active(),"actual diagnostic GPU backend required");
        probe.gpuReport=&state.d3d11Presenter->report();
        state.frameBackendInitialized=true;
    }
    struct Cleanup{RuntimeWindowState& state;RuntimeWindowHostWin32& host;const RuntimeUiClock* clock;~Cleanup(){state.d3d11Presenter.reset();state.frameProbeWin32=nullptr;state.mapFrameProbeWin32=nullptr;state.uiClock=clock;if(host.window()){host.attachSession(0);DestroyWindow(host.window());}releaseRuntimeBackBuffer(state.backBuffer);releaseRuntimeBackBuffer(state.sidebarStaticBuffer);releaseRuntimeBackBuffer(state.radarTerrainBuffer);}} cleanup{state,host,oldClock};
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    SetWindowPos(window,HWND_TOPMOST,work.left+8,work.top+8,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    require(IsWindowVisible(window)!=FALSE,"actual visible product window");
    require(mapMainFrameClientVisible(window,true),"initial actual client-point ownership");
    state.repaintRequested=true;InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
    require(probe.error.empty()&&state.renderStats.rendered,"actual initial map/UI paint: "+probe.error);
    std::string hashError;const auto before=runtimeStandaloneSimulationStateHash(state,hashError);require(before!=0&&hashError.empty(),"complete authority before frame matrix: "+hashError);const int tick=state.tick;
    std::ofstream manifest(output/"frames.tsv");manifest<<"case\ttick\twidth\theight\tcached\tfps\tcompleteColorHash\tauthority\n";
    int cases=0,viewFrames=0;
    const auto paint=[&](const std::string& label,bool dirty,bool expectComplete=true){
        require(mapMainFrameClientVisible(window,true),label+" native frame boundary/actual client-point ownership before paint");
        if(behindFixture&&dirty){const auto fixedClient=mapMainFrameClientRect(window);require(fixedClient.has_value(),"fixture client before fixed camera");centerRuntimeCameraOnCell(state,*fixedClient,{80,80});}
        state.repaintRequested=dirty;state.fpsDisplayMeter.reset();state.fpsDisplayMeter.recordPresentedFrame(10);state.fpsDisplayMeter.recordPresentedFrame(10.5);
        probe.got=false;probe.error.clear();InvalidateRect(window,nullptr,FALSE);SendMessageW(window,WM_PAINT,0,0);
        require(probe.got&&probe.success&&probe.error.empty(),label+": "+probe.error);
        require(mapMainFrameClientVisible(window),label+" actual client-point ownership after paint");
        require(probe.cached==!dirty,label+" cache/dirty");
        require(state.fpsDisplayMeter.hasSample()&&state.fpsDisplayMeter.framesPerSecond()==2,label+" successful submission FPS semantics actual="+std::to_string(state.fpsDisplayMeter.framesPerSecond())+" sampled="+std::to_string(state.fpsDisplayMeter.hasSample())+" visible="+std::to_string(IsWindowVisible(window)!=FALSE));
        if(behindFixture&&expectComplete){const auto pickClient=mapMainFrameClientRect(window);
            require(pickClient&&runtimeMapGpuFixturePositivePick(state,*pickClient),label+" positive original CPU authoritative building hit after actual frame");
            require(state.lastSelectedHealthBars>=1,label+" actual selected health/interaction overlay");require(state.renderStats.behindMarkerMissing==0&&state.renderStats.behindMarkerSprites==(state.config.seeHiddenObjects?1:0),label+" original BEHIND occlusion/option/real asset");
            if(gpuRequested&&state.d3d11Presenter->active()&&state.config.seeHiddenObjects)require(state.mapComposition.native().report().indexedDraws>=1,label+" actual BEHIND indexed GPU production");}
        const auto after=runtimeStandaloneSimulationStateHash(state,hashError);require(hashError.empty()&&after==before&&state.tick==tick,label+" full authority changed");
        if(std::getenv("CONCORD_MAIN_FRAME_REQUIRE_PRESENTER")&&expectComplete)require(probe.complete,label+" product did not hand off a complete SoftwareFrameView");
        std::ofstream raw(output/(label+".bgra"),std::ios::binary);raw.write(reinterpret_cast<const char*>(probe.last.data()),static_cast<std::streamsize>(probe.last.size()));require(bool(raw),label+" raw output");
        require(writeRuntimeSmokeBmp(pathString(output/(label+".bmp")),probe.last.data(),probe.width,probe.height,error),error);
        if(s41Map&&probe.complete)proveRuntimeMapGpuTerrainPacket(state,&output,label,gpuRequested&&state.d3d11Presenter->active());
        if(s41Map&&probe.complete){std::ofstream completeRaw(output/(label+".complete.bgra"),std::ios::binary);
            completeRaw.write(reinterpret_cast<const char*>(probe.lastComplete.data()),static_cast<std::streamsize>(probe.lastComplete.size()));require(bool(completeRaw),label+" complete BGRA output");}
        if(s41Map&&gpuRequested&&state.d3d11Presenter->active())std::cout<<"Map producer case="<<label
            <<" indexedDraws="<<state.mapComposition.native().report().indexedDraws<<" visionEffects="<<state.mapComposition.native().report().visionEffects
            <<" behind="<<state.renderStats.behindMarkerSprites<<" nativeCache="<<state.mapComposition.native().report().cacheHit<<'\n';
        manifest<<label<<'\t'<<tick<<'\t'<<probe.width<<'\t'<<probe.height<<'\t'<<probe.cached<<"\t2\t"<<colorHash(probe.last)<<'\t'<<after<<'\n';++cases;if(probe.complete)++viewFrames;return probe.last;
    };
    RECT client{};GetClientRect(window,&client);const auto hud=runtimeFpsDisplayRect(client);require(!IsRectEmpty(&hud),"FPS panel exists");
    bool cursor=false;for(const LONG inset:{6L,10L,2L})for(const LONG dy:{3L,8L,12L})if(!cursor){const POINT p{hud.right-inset,hud.top+dy};if(!runtimeCursorObjectAtPoint(state,runtimeMapViewport(client),p)){state.mouseClientPointValid=true;state.mouseClientPoint=p;if(runtimeMouseCursorFrameForPoint(state,client,p))cursor=true;}}
    require(cursor,"real cursor crossing FPS panel");
    const auto base=paint("full800",true);require(sameColor(paint("cached800",false),base),"cached final frame differs at fixed tick/clocks");
    state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Main;const auto paused=paint("paused800",true);require(sameColor(paint("paused-cached800",false),paused),"paused cached full frame differs");state.runtimeMenuOpen=false;
    RECT outer{};GetWindowRect(window,&outer);
    const auto resize=[&](int w,int h){RECT r{0,0,w,h};AdjustWindowRect(&r,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE);require(SetWindowPos(window,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"native resize");};
    resize(1280,720);const auto large=paint("full1280",true);require(sameColor(paint("cached1280",false),large),"large cached frame differs");resize(800,600);paint("restore800",true);
    ShowWindow(window,SW_MINIMIZE);require(IsIconic(window)&&!state.fpsDisplayMeter.hasSample(),"minimize reset");ShowWindow(window,SW_RESTORE);paint("minimize-restored",true);
    const auto style=GetWindowLongPtrW(window,GWL_STYLE);MONITORINFO info{};info.cbSize=sizeof(info);require(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info)!=FALSE,"actual fullscreen monitor");
    SetWindowLongPtrW(window,GWL_STYLE,(style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP);require(SetWindowPos(window,HWND_TOP,info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOACTIVATE)!=FALSE,"actual borderless fullscreen");paint("fullscreen",true);
    SetWindowLongPtrW(window,GWL_STYLE,style);require(SetWindowPos(window,nullptr,outer.left,outer.top,outer.right-outer.left,outer.bottom-outer.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"restore window style");paint("fullscreen-restored",true);
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(127,163));require(state.mouseClientPointValid&&state.mouseClientPoint.x==127&&state.mouseClientPoint.y==163,"unchanged native client input coordinates");
    HWND other=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Concord focus verification",WS_POPUP|WS_VISIBLE,work.left+820,work.top+8,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(other!=nullptr,"owned focus destination");SetFocus(window);state.forceAttackHeld=true;state.rightPointer.active=true;state.rightPointer.moved=true;SetCapture(window);SetFocus(other);
    const bool focusMoved=GetFocus()==other;DestroyWindow(other);require(focusMoved&&!state.forceAttackHeld&&!state.rightPointer.active&&GetCapture()!=window,"actual focus loss cleared held input/capture");
    if(s39Matrix){
        if(gpuRequested){require(state.d3d11Presenter->active()&&state.d3d11Presenter->report().presented>=10&&gpuProbe.readbacks>=10,"all normal production paints actually used GPU");
            if(s41Map)require(state.d3d11Presenter->report().mapDraws>=10,"actual normal main-window map packets required, no silent CPU fallback");gpuProbe.deviceRemoved=true;}
        paint("device-loss-fallback",true);
        if(gpuRequested){require(!state.d3d11Presenter->active()&&state.d3d11Presenter->report().removedReason==DXGI_ERROR_DEVICE_REMOVED,"device removal retires GPU before actual GDI fallback");
            require(state.d3d11Presenter->timingCount()==0,"diagnostic readbacks excluded from normal timings");}
    }
    releaseRuntimeBackBuffer(state.completedFrameBuffer);(void)state.fpsDeviceOverlay.release();
    probe.failCompletedAllocation=true;paint("completed-allocation-fallback",true,false);probe.failCompletedAllocation=false;
    releaseRuntimeBackBuffer(state.backBuffer);probe.failSceneAllocation=true;paint("scene-allocation-fallback",true,false);probe.failSceneAllocation=false;
    manifest.close();require(bool(manifest),"frame manifest close");
    if(std::getenv("CONCORD_MAIN_FRAME_REQUIRE_PRESENTER"))require(viewFrames>=cases-2&&state.softwareFrameCounters.successful+(s41Map&&state.d3d11Presenter?state.d3d11Presenter->report().mapDraws:0)>=static_cast<std::uint64_t>(viewFrames)&&state.softwareFallbackSubmissions>=2,"actual product presenter success/fallback counters");
    SendMessageW(window,WM_CLOSE,0,0);require(!host.window()&&!state.backBuffer.memoryDc&&!state.sidebarStaticBuffer.memoryDc&&!state.radarTerrainBuffer.memoryDc&&!state.completedFrameBuffer.memoryDc&&!state.fpsDeviceOverlay.dc(),"real close releases owner resources");require(!host.drainMessages(),"own quit drained");
    require(runtimeStandaloneSimulationStateHash(state,hashError)==before&&hashError.empty()&&state.tick==tick,"final complete authority unchanged");
    std::cout<<"Main frame contract passed cases="<<cases<<" completeViewFrames="<<viewFrames<<" fullPixels=all-BGR alpha=ignored-preserved clocks=fixed realWindow=yes fullAuthority="<<before<<" tick="<<tick<<" resize=minimize=fullscreen=focus=close=yes fallback=scene\n";return true;
}catch(const std::exception& e){error=e.what();return false;}
