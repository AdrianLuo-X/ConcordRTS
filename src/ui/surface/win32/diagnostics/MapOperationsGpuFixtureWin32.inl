// Generic operation evidence on the existing private owner; no new device/framework.
namespace concord::ui::win32::diagnostics {
void MapVisionGpuFixture::finishOperations(const std::vector<std::uint32_t>& input,const std::vector<std::uint32_t>& expected,
    const std::string& label,concord::render::MapDrawKind kind,std::uint64_t expectedOperations) {
    require(native_.finish(),native_.report().failure.data());require(list_.valid(),"operation bounded owned packet");
    require(source_.finishGdiWrites()&&std::memcmp(source_.pixels(),input.data(),input.size()*4)==0,"GPU operation no CPU target RGB writes");
    require(list_.commands().front().kind==concord::render::MapDrawKind::Fill,"operation starts constant, no CPU complete image");
    std::uint64_t operations=0;std::uint32_t flags=0;for(const auto& c:list_.commands()){require(c.kind!=concord::render::MapDrawKind::Bgra&&c.kind!=concord::render::MapDrawKind::Blend,"operation no CPU RGB payload");if(c.kind==kind){++operations;flags|=c.flags;}}
    require(operations==expectedOperations,"actual GPU operation count including explicit no-op cases");
    if(sequence_==0&&operations){const auto found=std::find_if(list_.commands().begin(),list_.commands().end(),[&](const auto& c){return c.kind==kind;});const auto index=static_cast<std::size_t>(found-list_.commands().begin());
        for(int test=0;test<2;++test){auto bad=list_;auto c=*found;if(test==0){if(kind==concord::render::MapDrawKind::Lookup)c.palette=UINT32_MAX;else if(kind==concord::render::MapDrawKind::LocalLight)c.width=193;else c.offset=UINT32_MAX;}else c.flags=UINT32_MAX;bad.replace(index,c);require(!bad.valid(),"operation invalid resource/flags rejected");}}
    require(gpu_->presentMap({list_,++sequence_,width_,height_}),"actual production GPU operation");require(probe_.readbacks==sequence_,"explicit diagnostic-only operation readback");const bool exact=equals(expected,label);
    if(const char* directory=std::getenv("CONCORD_S41_GPU_OPERATIONS_OUTPUT")){const std::filesystem::path target(directory);std::filesystem::create_directories(target);
        const auto write=[&](const char* suffix,const void* data,std::size_t bytes){std::ofstream file(target/(label+suffix),std::ios::binary);file.write(static_cast<const char*>(data),static_cast<std::streamsize>(bytes));require(bool(file),"complete operation evidence");};
        write(".expected.bgra",expected.data(),expected.size()*4);write(".gpu.bgra",probe_.uploadedPixels.data(),probe_.uploadedPixels.size());write(".resources.bin",list_.resources().data(),list_.resources().size()*4);}
    std::cout<<"S41 GPU operation case="<<label<<" kind="<<static_cast<unsigned>(kind)<<" operations="<<operations<<" flags="<<flags<<" width="<<width_<<" height="<<height_<<" resources="<<list_.resources().size()*4<<" cpuTargetRgbWrites=0 fullBgraExact="<<exact<<'\n';require(exact,"full operation BGRA equals original CPU renderer");
}
} // namespace concord::ui::win32::diagnostics
