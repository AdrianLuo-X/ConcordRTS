// Actual GPU check for packed receiver255, strides/clip/ownership, and old32bit257.
template<class Check>void runPackedReceiverGpuContract(HWND window,FrameDevice driver,Check check) {
    constexpr int width=384,height=224;FramePresentStatistics stats;D3D11FrameProbeWin32 probe;probe.captureUpload=true;
    auto gpu=D3D11FramePresenterWin32::create(window,{FrameBackend::D3D11,driver},stats,&probe);check(gpu&&gpu->active(),"packed receiver actual requested device");if(!gpu||!gpu->active())return;
    std::vector<std::array<std::uint32_t,256>> tables(257);for(unsigned id=0;id<tables.size();++id)for(unsigned v=0;v<256;++v)tables[id][v]=(((v+id)&255U)<<16U)|(((v^id)&255U)<<8U)|((v*3U+id*7U)&255U);
    MapDrawList list;std::vector<std::uint32_t> expected(static_cast<std::size_t>(width)*height);
    const auto background=[&]{list.begin();for(int x=0;x<width;++x){const auto color=0x9a000000U|((static_cast<unsigned>(x)&255U)<<16U)|(((x*7U)&255U)<<8U)|((x*11U)&255U);list.fill({x,0,x+1,height},color);for(int y=0;y<height;++y)expected[static_cast<std::size_t>(y)*width+x]=color;}};
    const auto transform=[&](auto receiver){for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto& p=expected[static_cast<std::size_t>(y)*width+x];const auto& t=tables[receiver(x,y)];p=(p&0xff000000U)|(t[(p>>16U)&255U]&0xff0000U)|(t[(p>>8U)&255U]&0xff00U)|(t[p&255U]&255U);}};
    const auto exact=[&]{return probe.uploadedPixels.size()==expected.size()*4&&std::memcmp(probe.uploadedPixels.data(),expected.data(),expected.size()*4)==0;};
    background();MapDrawCommand c;c.right=width;c.bottom=height;c.kind=MapDrawKind::ReceiverLookup;c.originX=-3;c.originY=-5;c.width=width+7;c.height=height+9;c.stride=c.width+5;c.flags=1;c.parameter=256;
    std::vector<unsigned char> packed(static_cast<std::size_t>(c.stride)*c.height);for(std::size_t i=0;i<packed.size();++i)packed[i]=static_cast<unsigned char>(i*7U+255U);
    c.offset=list.intern(packed);c.palette=list.internValues<std::array<std::uint32_t,256>>(tables);const auto index=list.commands().size();list.push(c);transform([&](int x,int y){return packed[static_cast<std::size_t>(y+5)*c.stride+x+3];});
    check(list.valid(),"packed receiver255 padded stride and negative origin bounded");check(gpu->presentMap({list,1,width,height}),"packed receiver8 actualGPU");check(exact(),"packed receiver8 entireBGRA A and all256indices exact");
    for(int mode=0;mode<5;++mode){auto bad=list;auto invalid=c;if(mode==0)invalid.flags=2;else if(mode==1)invalid.parameter=257;else if(mode==2)invalid.parameter=255;else if(mode==3)invalid.offset=static_cast<std::uint32_t>(bad.resources().size()*4-1);else invalid.stride=invalid.width-1;bad.replace(index,invalid);check(!bad.valid(),"packed receiver invalid flags/count/id/extent/stride");check(!gpu->presentMap({bad,2,width,height}),"packed receiver invalid rejected beforeGPU");}
    std::fill(packed.begin(),packed.end(),0);for(auto& t:tables)t.fill(0);check(gpu->presentMap({list,2,width,height}),"packed receiver owned bytes survive source mutation");check(exact(),"owned receiver8 completeBGRA immutable");
    for(unsigned id=0;id<tables.size();++id)for(unsigned v=0;v<256;++v)tables[id][v]=(((v+id)&255U)<<16U)|(((v^id)&255U)<<8U)|((v*3U+id*7U)&255U);
    list.reset();background();c={};c.right=width;c.bottom=height;c.kind=MapDrawKind::ReceiverLookup;c.width=width;c.height=height;c.stride=width*4;c.parameter=257;
    std::vector<std::uint32_t> wide(static_cast<std::size_t>(width)*height);for(std::size_t i=0;i<wide.size();++i)wide[i]=static_cast<unsigned>(i%257);c.offset=list.internValues<std::uint32_t>(wide);c.palette=list.internValues<std::array<std::uint32_t,256>>(tables);list.push(c);transform([&](int x,int y){return wide[static_cast<std::size_t>(y)*width+x];});
    check(list.valid(),"old32bit receiver257 bounded and unchanged");check(gpu->presentMap({list,3,width,height}),"old32bit receiver257 actualGPU");check(exact(),"old32bit receiver256 fullBGRA A exact");
    std::cout<<"G007 packed receiver GPU driver="<<(driver==FrameDevice::Warp?"warp":"hardware")<<" frames=3 index255=covered count257old32=covered invalidCases=5 ownedMutation=covered fullBGRA=exact\n";
}
