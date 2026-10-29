// S40 menu submission on the existing private device/Present owner.
bool D3D11FramePresenterWin32::presentMenu(const concord::ui::MenuDrawFrame& f,std::uint64_t expectedDeviceGeneration) noexcept {
    auto& p=*impl_;
    if(!onOwningThread())return false;
    if(expectedDeviceGeneration&&expectedDeviceGeneration!=p.report.deviceGeneration){++p.report.staleRejected;return false;}
    if(!active()||!IsWindow(p.window)||IsIconic(p.window)||!IsWindowVisible(p.window))return false;
    RECT client{};
    if(!GetClientRect(p.window,&client)||client.right!=f.width||client.bottom!=f.height
        ||f.width<=0||f.height<=0||f.viewport.left<0||f.viewport.top<0
        ||f.viewport.right>f.width||f.viewport.bottom>f.height
        ||f.viewport.right<=f.viewport.left||f.viewport.bottom<=f.viewport.top
        ||f.sequence==0||f.sequence<=p.menuSequence||!f.list.valid()
        ||f.generation!=f.list.generation||f.resourceRevision!=f.list.revision
        ||f.commandRevision!=f.list.frameRevision){++p.report.menuRejected;return false;}
    p.menuSequence=f.sequence;
    if(p.menu&&(p.menu->owner!=&f.list||p.menu->generation!=f.generation)&&!resetResources())return false;
    constexpr std::uint64_t buckets=25*19;std::uint64_t entries=buckets*2+static_cast<std::uint64_t>(f.viewport.right-f.viewport.left)+static_cast<std::uint64_t>(f.viewport.bottom-f.viewport.top);
    for(const auto& op:f.list.commands) {
        const int l=std::clamp(op.left,0,800),r=std::clamp(op.right,0,800),t=std::clamp(op.top,0,600),b=std::clamp(op.bottom,0,600);
        if(l<r&&t<b)entries+=static_cast<std::uint64_t>((r-1)/32-l/32+1)*static_cast<std::uint64_t>((b-1)/32-t/32+1);
    }
    if(entries>concord::ui::MenuDrawList::maxBytes/4){++p.report.budgetRejected;p.fail("menu-tile-budget",E_OUTOFMEMORY);return false;}
    if(!p.beginFrame(f.width,f.height,1,f.list.commands.size()*64,f.list.resources.size()*4,entries,buckets,32))return false;
    p.collect();
    if(p.probe&&p.probe->deviceRemoved){p.fail("device-removed-injected",DXGI_ERROR_DEVICE_REMOVED);return false;}
    if(p.probe&&p.probe->failUpload){p.fail("menu-upload-injected",E_FAIL);return false;}
    D3D11FrameTiming* sample=nullptr;Impl::Query* query=nullptr;
    if(!(p.probe&&p.probe->captureUpload)&&p.sampleCount<p.samples.size()) {
        sample=&p.samples[p.sampleCount++];sample->sequence=f.sequence;sample->menuRaster=true;
        for(auto& q:p.queries)if(!q.pending){query=&q;break;}
        if(query){query->sample=p.sampleCount-1;p.context->Begin(query->disjoint.Get());}
        else ++p.report.gpuDropped;
    }
    const auto submit=Clock::now();std::uint64_t uploaded=0;HRESULT hr=S_OK;
    try {
        if(!p.menu)p.menu=std::make_unique<MenuRasterD3D11>();
        if(p.probe&&p.probe->failShader)hr=p.device->CreatePixelShader(s40_ps,1,nullptr,nullptr);
        else hr=p.menu->draw(p.device.Get(),p.context.Get(),p.back.Get(),f,uploaded,query?query->start.Get():nullptr);
    }catch(...){hr=E_OUTOFMEMORY;}
    if(sample){sample->uploadCpuMs=elapsed(submit);sample->uploadBytes=uploaded;}
    if(query){p.context->End(query->end.Get());p.context->End(query->disjoint.Get());query->pending=true;}
    if(FAILED(hr)){p.fail("menu-raster-resource",hr);return false;}
    p.report.uploadedBytes+=uploaded;
    p.finishSubmission();
    if(!p.capture())return false;
    const auto present=Clock::now();hr=p.chain->Present(p.probe&&p.probe->failPresent?5U:1U,0);
    if(sample)sample->presentCpuMs=elapsed(present);
    if(FAILED(hr)){p.fail("menu-present",hr);return false;}
    if(hr==DXGI_STATUS_OCCLUDED)return false;
    if(p.probe&&p.probe->captureUpload)DwmFlush();
    ++p.report.menuDraws;++p.report.presented;return true;
}
