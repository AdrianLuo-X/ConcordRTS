ComPtr<IDXGIAdapter3> memoryAdapter;
    ComPtr<ID3D11Query> completion;
    bool inFlight=false;unsigned rasterKind=0;
    ULONGLONG recoverAfter=0;
// Methods of the sole UI-thread Impl. One upload/submission at a time; no worker
// can own context or resource leases. Completion is an EVENT query, not pixels.
HRESULT waitForFrame() noexcept {
    if(!inFlight||!context)return S_OK;
    const auto deadline=GetTickCount64()+FrameResourceLimits::frameWaitMs;
    context->Flush();
    for(;;) {
        const HRESULT hr=probe&&probe->stallCompletion?S_FALSE:
            context->GetData(completion.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr==S_OK){inFlight=false;report.inFlight=0;++report.completed;return S_OK;}
        if(FAILED(hr))return hr;
        if(GetTickCount64()>=deadline){++report.waitTimeouts;return DXGI_ERROR_WAS_STILL_DRAWING;}
        ++report.waitSleeps;Sleep(1);
    }
}
void finishSubmission() noexcept {
    context->End(completion.Get());inFlight=true;report.inFlight=1;
    ++report.submitted;report.peakInFlight=1;
}
void trimRasters() noexcept {
    context->ClearState();map.reset();menu.reset();
    if(report.resourceGeneration!=UINT64_MAX)++report.resourceGeneration;
}
std::uint64_t kindPreparationBytes(std::uint64_t commands) const noexcept {
    return rasterKind==2 ? commands/64*20*5+65536 : 0;
}
bool packetBudget(int w,int h,std::uint64_t commands,std::uint64_t resources,
    std::uint64_t entries,std::uint64_t buckets,std::uint64_t constants) noexcept {
    constexpr std::uint64_t unit=4;
    const auto texture=static_cast<std::uint64_t>(w)*static_cast<std::uint64_t>(h)*unit;
    // The atlas is the owned packed raw resource slab. No hidden texture atlas.
    // One back buffer, optional diagnostic staging texture, three raw buffers.
    const auto gpu=texture+commands+std::max<std::uint64_t>(4,resources)+entries*unit+constants;
    const auto retained=map?map->resourceSnapshot.capacity()*unit:menu?menu->resourceSnapshot.capacity()*unit:0;
    // S43: additionally reserve five owned 20-byte routing snapshots (one
    // producer, four packets), four headers and bounded task metadata. Existing
    // entries*16 already dominates private results+merged output+tile scratch.
    const auto preparation=kindPreparationBytes(commands);
    const auto scratch=resources+retained+entries*16+buckets*sizeof(std::vector<std::uint32_t>)+preparation+
        (probe&&probe->captureUpload?texture*2:0);
    std::uint64_t cap=std::min(report.requested.gpuBudgetBytes,FrameResourceLimits::maxBytes);
    if(probe&&probe->budgetBytesOverride)cap=std::min(cap,probe->budgetBytesOverride);
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    report.memoryBudgetAvailable=memoryAdapter&&SUCCEEDED(memoryAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory))&&memory.Budget;
    if(report.memoryBudgetAvailable){++report.budgetQueries;report.osBudgetBytes=memory.Budget;report.osUsageBytes=memory.CurrentUsage;cap=std::min(cap,memory.Budget);}
    report.budgetBytes=cap;
    const auto extra=gpu>report.gpuPayloadBytes?gpu-report.gpuPayloadBytes:0;
    if(gpu+(probe&&probe->captureUpload?texture:0)>cap||scratch>FrameResourceLimits::maxBytes||
        (report.memoryBudgetAvailable&&(memory.CurrentUsage>memory.Budget||extra>memory.Budget-memory.CurrentUsage))) {
        ++report.budgetRejected;fail("resource-budget",E_OUTOFMEMORY);return false;
    }
    report.gpuPayloadBytes=gpu;report.cpuStagingBytes=scratch;
    report.peakGpuPayloadBytes=std::max(report.peakGpuPayloadBytes,gpu);
    report.peakCpuStagingBytes=std::max(report.peakCpuStagingBytes,scratch);return true;
}
bool beginFrame(int w,int h,unsigned kind,std::uint64_t commands=0,std::uint64_t resources=0,
    std::uint64_t entries=0,std::uint64_t buckets=0,std::uint64_t constants=0) noexcept {
    if(w<=0||h<=0||w>8192||h>8192){fail("frame-extent",E_INVALIDARG);return false;}
    const HRESULT done=waitForFrame();if(FAILED(done)){fail("frame-completion",done);return false;}
    if(rasterKind!=kind){trimRasters();rasterKind=kind;report.gpuPayloadBytes=0;}
    return packetBudget(w,h,commands,resources,entries,buckets,constants)&&ensureSize(w,h);
}

bool resetRasters() noexcept {
    const HRESULT hr=waitForFrame();if(FAILED(hr)){fail("resource-retirement",hr);return false;}
    trimRasters();rasterKind=0;report.gpuPayloadBytes=0;return true;
}
