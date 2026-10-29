// Exercises real devices, completion queries, exact output and GDI recovery.
void lifetimeCase(FrameDevice driver){
    Window w(97,61);FramePresentStatistics stats;D3D11FrameProbeWin32 probe;probe.captureUpload=true;
    auto p=D3D11FramePresenterWin32::create(w.handle,{FrameBackend::D3D11,driver},stats,&probe);
    check(p&&p->active(),"S42 actual lifetime device");if(!p||!p->active())return;
    concord::render::MapDrawList list;std::uint64_t sequence=1;
    list.fill({0,0,97,61},0x7F345678U);
    const auto epoch=p->report().deviceGeneration;
    check(p->presentMap({list,sequence++,97,61},epoch),"S42 initial GPU raster");
    check(probe.uploadedPixels.size()==97U*61U*4U,"S42 entire frame captured");
    const auto expected=probe.uploadedPixels;
    for(int cycle=0;cycle<12;++cycle){
        check(p->resetResources(),"S42 hot resource retirement waits for prior GPU");
        auto stale=concord::render::MapDrawFrame(list,sequence++,97,61);
        list.reset();list.fill({0,0,97,61},0x7F345678U);
        check(!p->presentMap(stale,epoch),"S42 old source generation rejected");
        check(p->presentMap({list,sequence++,97,61},epoch),"S42 new map generation raster");
        check(probe.uploadedPixels==expected,"S42 reload preserves complete BGRA");
    }
    std::thread foreign([&]{check(!p->resetResources()&&!p->tryRecoverDevice(),"S42 foreign lifecycle rejected");});foreign.join();
    for(unsigned attempt=0;attempt<FrameResourceLimits::maxRecoveryAttempts;++attempt){
        const auto old=p->report().deviceGeneration;
        probe.deviceRemoved=true;
        check(!p->presentMap({list,sequence++,97,61},old)&&!p->active(),"S42 device loss selects software same frame");
        check(p->report().recoveryPending,"S42 device loss schedules bounded recovery");
        probe.deviceRemoved=false;
        check(!p->tryRecoverDevice(),"S42 recovery cooldown prevents retry loop");
        ShowWindow(w.handle,SW_MINIMIZE);pump();Sleep(FrameResourceLimits::recoveryDelayMs+10);
        check(!p->tryRecoverDevice(),"S42 minimized window cannot rebuild");
        ShowWindow(w.handle,SW_RESTORE);w.show(97,61);
        check(p->tryRecoverDevice()&&p->active(),"S42 real device and shader resources rebuild");
        check(!p->presentMap({list,sequence++,97,61},old),"S42 old device epoch cannot publish");
        check(p->presentMap({list,sequence++,97,61},p->report().deviceGeneration),"S42 current epoch publishes after rebuild");
        check(probe.uploadedPixels==expected,"S42 recovery complete BGRA exact");
    }
    check(p->report().recovered==2&&p->report().staleRejected==2,"S42 recovery and stale counters exact");
    probe.deviceRemoved=true;check(!p->presentMap({list,sequence++,97,61}),"S42 persistent device loss fails");
    check(!p->report().recoveryPending&&!p->tryRecoverDevice(),"S42 exhausted recovery remains software");
    HDC dc=GetDC(w.handle);MainBackBufferWin32 software;check(software.ensure(dc,97,61),"S42 software remains available");
    std::copy(expected.begin(),expected.end(),static_cast<unsigned char*>(software.pixels));
    DibFramePresenterWin32 fallback(dc,software,stats);
    check(fallback.present({software.completedPixels(),sequence++,1,120,SoftwareFrameContent::Redrawn})==FramePresentResult::Presented,"S42 GDI output after exhausted recovery");
    check(same(visible(w.handle,97,61),expected,97,61,97*4,false),"S42 entire visible software fallback exact");ReleaseDC(w.handle,dc);
    check(p->report().peakInFlight==1&&p->report().inFlight==0,"S42 single in-flight frame cap");p.reset();
    probe={};auto budget=D3D11FramePresenterWin32::create(w.handle,{FrameBackend::D3D11,driver},stats,&probe);
    check(budget&&budget->active(),"S42 budget device");if(budget&&budget->active()){
        probe.budgetBytesOverride=1024;
        check(!budget->presentMap({list,sequence++,97,61})&&!budget->active(),"S42 aggregate budget refuses before allocation");
        check(budget->report().budgetRejected==1&&budget->report().submitted==0,"S42 budget accounting and empty upload queue");
        check(budget->report().gpuPayloadBytes==0&&budget->report().cpuStagingBytes==0,"S42 budget failure releases owned payload");
    }budget.reset();probe={};
    auto stalled=D3D11FramePresenterWin32::create(w.handle,{FrameBackend::D3D11,driver},stats,&probe);
    check(stalled&&stalled->active(),"S42 queue timeout device");if(stalled&&stalled->active()){
        auto bytes=pattern(97,61,97*4);
        check(stalled->present(frame(bytes,97,61,97*4,sequence++))==FramePresentResult::Presented,"S42 first queued frame");
        probe.stallCompletion=true;const auto start=GetTickCount64();
        check(stalled->present(frame(bytes,97,61,97*4,sequence++))==FramePresentResult::Failed&&!stalled->active(),"S42 bounded completion timeout selects software");
        check(GetTickCount64()-start<2000&&stalled->report().waitSleeps>0&&stalled->report().waitTimeouts>0,"S42 no unbounded wait or busy loop");
        check(stalled->report().peakInFlight==1,"S42 blocked queue never grows");
    }
    std::cout<<"S42 lifetime driver="<<(driver==FrameDevice::Warp?"warp":"hardware")<<" sourceReloads=12 recoveries=2 maxInFlight=1 completeBGRA=exact persistentFallback=GDI timeout=bounded budget=preflight\n";
}
