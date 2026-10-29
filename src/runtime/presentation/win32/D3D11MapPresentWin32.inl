// S41 submission. Normal rendering never calls the diagnostic capture path.
bool D3D11FramePresenterWin32::presentMap(const concord::render::MapDrawFrame& f,std::uint64_t expectedDeviceGeneration) noexcept {
    auto& p=*impl_;
    if(!onOwningThread())return false;
    if(expectedDeviceGeneration&&expectedDeviceGeneration!=p.report.deviceGeneration){++p.report.staleRejected;return false;}
    if(!active()||!IsWindow(p.window)||IsIconic(p.window)||!IsWindowVisible(p.window))return false;
    RECT client{};
    if(!f.valid()||f.sequence<=p.mapSequence||!GetClientRect(p.window,&client)
        ||client.right!=f.width||client.bottom!=f.height){++p.report.mapRejected;return false;}
    p.mapSequence=f.sequence;
    if(p.map&&(p.map->owner!=&f.list||p.map->generation!=f.generation)&&!resetResources())return false;
    std::uint64_t buckets=static_cast<std::uint64_t>((f.width+31)/32)*static_cast<std::uint64_t>((f.height+31)/32),entries=buckets*2;
    for(const auto& op:f.list.commands()) {
        if(op.kind==concord::render::MapDrawKind::NoOp||op.kind==concord::render::MapDrawKind::Shift)continue;
        const int l=std::clamp(op.left,0,f.width),r=std::clamp(op.right,0,f.width),t=std::clamp(op.top,0,f.height),b=std::clamp(op.bottom,0,f.height);
        if(l<r&&t<b)entries+=static_cast<std::uint64_t>((r-1)/32-l/32+1)*static_cast<std::uint64_t>((b-1)/32-t/32+1);
    }
    if(entries>concord::render::MapDrawList::maxBytes/4){++p.report.budgetRejected;p.fail("map-tile-budget",E_OUTOFMEMORY);return false;}
    if(!p.beginFrame(f.width,f.height,2,f.list.commands().size()*64,f.list.resources().size()*4,entries,buckets,48))return false;
    p.collect();
    if(p.probe&&p.probe->deviceRemoved){p.fail("map-device-removed-injected",DXGI_ERROR_DEVICE_REMOVED);p.report.removedReason=DXGI_ERROR_DEVICE_REMOVED;return false;}
    if(p.probe&&p.probe->failUpload){p.fail("map-upload-injected",E_FAIL);return false;}
    D3D11FrameTiming* sample=nullptr;Impl::Query* query=nullptr;
    if(!(p.probe&&p.probe->captureUpload)) {
        if(p.sampleCount<p.samples.size()) {
            sample=&p.samples[p.sampleCount++];sample->sequence=f.sequence;sample->tick=f.tick;sample->mapRaster=true;
            for(auto& q:p.queries)if(!q.pending){query=&q;break;}
            if(query){query->sample=p.sampleCount-1;p.context->Begin(query->disjoint.Get());}
            else ++p.report.gpuDropped;
        }else ++p.report.timingDropped;
    }
    const auto submit=Clock::now();std::uint64_t uploaded=0;HRESULT hr=S_OK;
    try {
        if(!p.map)p.map=std::make_unique<MapRasterD3D11>(p.report.requested.frameWorkers);
        if(p.probe&&p.probe->failShader)hr=p.device->CreatePixelShader(s41_ps,1,nullptr,nullptr);
        else hr=p.map->draw(p.device.Get(),p.context.Get(),p.back.Get(),f,uploaded,query?query->start.Get():nullptr,p.report.deviceGeneration);
    }catch(...){hr=E_OUTOFMEMORY;}
    if(sample){sample->uploadCpuMs=elapsed(submit);sample->uploadBytes=uploaded;}
    if(query){p.context->End(query->end.Get());p.context->End(query->disjoint.Get());query->pending=true;}
    if(FAILED(hr)){p.fail("map-raster-resource",hr);return false;}
    p.report.uploadedBytes+=uploaded;
    p.finishSubmission();
    if(!p.capture())return false;
    const auto present=Clock::now();hr=p.chain->Present(p.probe&&p.probe->failPresent?5U:1U,0);
    if(sample)sample->presentCpuMs=elapsed(present);
    if(FAILED(hr)){p.fail("map-present",hr);return false;}
    if(hr==DXGI_STATUS_OCCLUDED)return false;
    if(p.probe&&p.probe->captureUpload)DwmFlush();
    for(const auto& command:f.list.commands()) {
        const auto kind=static_cast<std::size_t>(command.kind);if(kind<p.mapKinds.size())++p.mapKinds[kind];
        if(command.kind==concord::render::MapDrawKind::Indexed)++p.mapIndexedCommands;
        if(command.kind==concord::render::MapDrawKind::Vision)++p.mapVisionCommands;
    }
    ++p.report.mapDraws;++p.report.presented;return true;
}
