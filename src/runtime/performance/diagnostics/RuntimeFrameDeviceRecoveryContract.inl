// Optional private extension of the original full-window/authority contract.
// Reuses its real WM_PAINT, exact images, CPU picks and unchanged FPS2 gates.
if(std::getenv("CONCORD_S42_RECOVERY_CONTRACT")) {
    require(gpuRequested&&s41Map,"S42 recovery needs actual map GPU");
    for(unsigned cycle=0;cycle<2;++cycle) {
        const auto old=state.d3d11Presenter->report().deviceGeneration;
        require(!state.d3d11Presenter->active(),"S42 starts from actual software loss fallback");
        gpuProbe.deviceRemoved=false;
        Sleep(concord::presentation::FrameResourceLimits::recoveryDelayMs+10);
        const auto label=std::to_string(cycle);
        const auto rebuilt=paint("device-recovered-"+label,true);
        const auto& current=state.d3d11Presenter->report();
        require(state.d3d11Presenter->active()&&current.deviceGeneration==old+1
            &&current.recovered==cycle+1,"S42 production WM_PAINT rebuilds actual device");
        require(state.d3d11Presenter->resetResources(),"S42 production hot refresh retires prior GPU frame");
        const auto refreshed=paint("resource-refresh-"+label,true);
        require(sameColor(rebuilt,refreshed),"S42 complete visible pixels stable across resource reset");
        resize(1280,720);paint("recovered-resize1280-"+label,true);
        resize(800,600);const auto restored=paint("recovered-restore800-"+label,true);
        require(sameColor(refreshed,restored),"S42 complete image stable after recovered resize roundtrip");
        gpuProbe.deviceRemoved=true;paint("device-loss-again-"+label,true);
        require(!state.d3d11Presenter->active()&&current.selected==concord::presentation::FrameBackend::Software,
            "S42 repeated loss really paints GDI in same WM_PAINT");
    }
    const auto& final=state.d3d11Presenter->report();
    require(final.recoveryAttempts==2&&final.recovered==2&&!final.recoveryPending&&final.peakInFlight==1,
        "S42 recovery budget exhausted with bounded queue and permanent usable software");
    std::cout<<"S42 production recovery passed driver="<<backend<<" recoveries=2 resourceResets=2 peakInFlight=1 fullAuthority="<<before<<" unchanged=1\n";
}
