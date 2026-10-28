// Private diagnostic: original software renderer vs actual world GPU production.
namespace concord::ui::win32::diagnostics {
std::vector<std::uint32_t> MapVisionGpuFixture::softwareWorld(int width,int height,const WorldPaint& draw) {
    require(SetWindowPos(window_,HWND_TOPMOST,0,0,width,height,SWP_NOMOVE|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE,"world CPU reference client");
    HDC reference=GetDC(window_);const bool ready=source_.ensure(reference,width,height);ReleaseDC(window_,reference);require(ready,"world original CPU owner");
    require(source_.finishGdiWrites(),"world CPU source ready");std::fill_n(reinterpret_cast<std::uint32_t*>(source_.writablePixels()),static_cast<std::size_t>(width)*height,0x57332f12U);
    draw(source_);require(source_.finishGdiWrites(),"world original CPU renderer completion");const auto* pixels=reinterpret_cast<const std::uint32_t*>(source_.pixels());return {pixels,pixels+static_cast<std::size_t>(width)*height};
}
std::array<std::uint64_t,3> MapVisionGpuFixture::worldFrame(int width,int height,const std::vector<std::uint32_t>& expected,const std::string& label,const WorldPaint& draw) {
    require(SetWindowPos(window_,HWND_TOPMOST,0,0,width,height,SWP_NOMOVE|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE,"world GPU real client");
    HDC reference=GetDC(window_);const bool ready=source_.ensure(reference,width,height);ReleaseDC(window_,reference);require(ready,"world original GPU input owner");
    require(source_.finishGdiWrites(),"world input completion");std::vector<std::uint32_t> input(static_cast<std::size_t>(width)*height);
    for(std::size_t i=0;i<input.size();++i)input[i]=0xa5c39127U^static_cast<std::uint32_t>(i*1949U);std::memcpy(source_.writablePixels(),input.data(),input.size()*4);
    MapCompositionWin32 composition;
    const auto materialCaptures=runtimeGpuSpriteMaterialCaptures;
    {MapCompositionRequestWin32 request(composition,source_.dc(),reinterpret_cast<const std::uint32_t*>(source_.pixels()),width,height);draw(source_);}
    require(composition.finish(),composition.native().report().failure.data());require(composition.prepare(),"world owned production packet");
    require(source_.finishGdiWrites()&&std::memcmp(source_.pixels(),input.data(),input.size()*4)==0,"world GPU producer must not write CPU complete target RGB");
    const auto& list=composition.frame();require(list.valid()&&!list.commands().empty()&&list.commands().front().kind==concord::render::MapDrawKind::Fill,"world starts constant, no CPU completed world input");
    std::array<std::uint64_t,3> counts{};for(const auto& c:list.commands()){if(c.kind==concord::render::MapDrawKind::Save)++counts[0];if(c.kind==concord::render::MapDrawKind::RestoreBlend||c.kind==concord::render::MapDrawKind::RestoreMask)++counts[1];if(c.kind==concord::render::MapDrawKind::Chain)++counts[2];}
    std::uint64_t sprites=0,materialOps=0;for(const auto& c:list.commands()){require(c.kind!=concord::render::MapDrawKind::Bgra&&c.kind!=concord::render::MapDrawKind::Blend,"whole world no CPU sprite RGB packet");
        if(c.kind==concord::render::MapDrawKind::Sprite)++sprites;
        if(c.kind==concord::render::MapDrawKind::Chain&&c.flags==1){const auto* words=reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const unsigned char*>(list.resources().data())+c.palette);
            for(std::uint32_t i=0;i<c.parameter;++i)materialOps+=words[i*4+3]==1;}}
    require(sprites>0&&runtimeGpuSpriteMaterialCaptures>materialCaptures,"whole world actual GPU source material");
    std::uint64_t masked=0;for(std::size_t index=0;index<list.commands().size();++index){const auto& command=list.commands()[index];if(command.kind!=concord::render::MapDrawKind::RestoreMask)continue;++masked;
        auto bad=list;auto invalid=command;invalid.offset=UINT32_MAX;bad.replace(index,invalid);require(!bad.valid(),"factory mask bounds rejected before GPU");}
    if(label.find("factory-exit")!=std::string::npos)require(masked>0,"actual factory foreground saved colour mask restore");
    std::cout<<"S41 factory mask restores="<<masked<<" full32bitSavedColour=1\n";
    std::cout<<"S41 frozen material sprites="<<sprites<<" materialChainOps="<<materialOps<<" cpuSourceRgbPixels=0\n";
    std::array<std::uint64_t,4> lightKinds{};for(const auto& c:list.commands()){if(c.kind==concord::render::MapDrawKind::Lookup)++lightKinds[0];if(c.kind==concord::render::MapDrawKind::ReceiverLookup)++lightKinds[1];if(c.kind==concord::render::MapDrawKind::LocalLight)++lightKinds[2];if(c.kind==concord::render::MapDrawKind::Vision)++lightKinds[3];}
    if(label.find("light-global")!=std::string::npos)require(lightKinds[0]>0,"actual global light GPU");
    if(label.find("light-map")!=std::string::npos||label.find("light-radiation")!=std::string::npos)require(lightKinds[1]>0,"actual receiver/radiation light GPU");
    if(label.find("light-local")!=std::string::npos)require(lightKinds[2]>0,"actual local light GPU");
    if(label.find("light-combined")!=std::string::npos)require(lightKinds[1]>0&&lightKinds[2]>0,"actual combined world lights GPU");
    if(label.find("vision")!=std::string::npos)require(lightKinds[3]>0,"actual light before vision GPU");
    if(label.find("light-expired")!=std::string::npos)require(lightKinds[0]+lightKinds[1]+lightKinds[2]==0,"expired light emits no colour operation");
    std::cout<<"G007 world light operations global="<<lightKinds[0]<<" receiver="<<lightKinds[1]<<" local="<<lightKinds[2]<<" vision="<<lightKinds[3]<<'\n';
    width_=width;height_=height;require(gpu_->presentMap({list,++sequence_,width,height}),"actual world GPU production submit");require(probe_.readbacks==sequence_,"world diagnostic-only image readback");const bool exact=equals(expected,label);
    if(const char* directory=std::getenv("CONCORD_S41_GPU_FROZEN_OUTPUT")){const std::filesystem::path target(directory);std::filesystem::create_directories(target);
        const auto write=[&](const char* suffix,const void* data,std::size_t bytes){std::ofstream file(target/(label+suffix),std::ios::binary);file.write(static_cast<const char*>(data),static_cast<std::streamsize>(bytes));require(bool(file),"whole world frozen output");};
        write(".expected.bgra",expected.data(),expected.size()*4);write(".gpu.bgra",probe_.uploadedPixels.data(),probe_.uploadedPixels.size());write(".commands.bin",list.commands().data(),list.commands().size()*sizeof(concord::render::MapDrawCommand));}
    std::cout<<"S41 GPU frozen case="<<label<<" width="<<width<<" height="<<height<<" save="<<counts[0]<<" restore="<<counts[1]<<" chains="<<counts[2]<<" resources="<<list.resources().size()*4<<" commands="<<list.commands().size()<<" nativeScratch="<<composition.native().report().scratchBytes<<" cpuTargetRgbWrites=0 fullBgraExact="<<exact<<'\n';
    require(exact,"whole frozen world GPU BGRA must equal unchanged CPU renderer");composition.release();return counts;
}
} // namespace concord::ui::win32::diagnostics
