// S41 submission. Normal rendering never calls the diagnostic capture path.
bool D3D11FramePresenterWin32::presentMap(const concord::render::MapDrawFrame& f) noexcept {
    auto& p=*impl_;
    if(!onOwningThread())return false;
    if(!active()||!IsWindow(p.window)||IsIconic(p.window)||!IsWindowVisible(p.window))return false;
    RECT client{};
    if(!f.valid()||f.sequence<=p.mapSequence||!GetClientRect(p.window,&client)
        ||client.right!=f.width||client.bottom!=f.height){++p.report.mapRejected;return false;}
    p.mapSequence=f.sequence;
    if(!p.ensureSize(f.width,f.height))return false;
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
        if(!p.map)p.map=std::make_unique<MapRasterD3D11>();
        if(p.probe&&p.probe->failShader)hr=p.device->CreatePixelShader(s41_ps,1,nullptr,nullptr);
        else hr=p.map->draw(p.device.Get(),p.context.Get(),p.back.Get(),f,uploaded,query?query->start.Get():nullptr);
    }catch(...){hr=E_OUTOFMEMORY;}
    if(sample){sample->uploadCpuMs=elapsed(submit);sample->uploadBytes=uploaded;}
    if(query){p.context->End(query->end.Get());p.context->End(query->disjoint.Get());query->pending=true;}
    if(FAILED(hr)){p.fail("map-raster-resource",hr);return false;}
    p.report.uploadedBytes+=uploaded;
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