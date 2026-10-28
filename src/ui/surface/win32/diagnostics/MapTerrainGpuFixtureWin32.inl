// Extends only the existing private diagnostic owner, using the same device/pass.
namespace concord::ui::win32::diagnostics {
void MapVisionGpuFixture::beginTerrain(int width,int height,const std::vector<std::uint32_t>& input) {
    require(input.size()==static_cast<std::size_t>(width)*height,"terrain diagnostic input dimensions");
    require(SetWindowPos(window_,HWND_TOPMOST,0,0,width,height,SWP_NOMOVE|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE,"terrain actual client resize");
    HDC reference=GetDC(window_);const bool ready=source_.ensure(reference,width,height);ReleaseDC(window_,reference);require(ready,"terrain original owned DIB");
    require(source_.finishGdiWrites(),"terrain source flush");std::memcpy(source_.writablePixels(),input.data(),input.size()*4);
    list_.reset();width_=width;height_=height;list_.fill({0,0,width,height},0);
    require(native_.begin(source_.dc(),width,height,list_,reinterpret_cast<const std::uint32_t*>(source_.pixels()))!=nullptr,"terrain actual native packet owner");
}
void MapVisionGpuFixture::finishTerrain(const std::vector<std::uint32_t>& input,const std::vector<std::uint32_t>& expected,const std::string& label) {
    require(native_.finish(),native_.report().failure.data());require(list_.valid(),"terrain complete bounded packet");
    require(source_.finishGdiWrites()&&std::memcmp(source_.pixels(),input.data(),input.size()*4)==0,"terrain producer must not write CPU target RGB");
    require(list_.commands().front().kind==concord::render::MapDrawKind::Fill,"terrain GPU no completed CPU world image input");
    require(std::count_if(list_.commands().begin(),list_.commands().end(),[](const auto& c){return c.kind==concord::render::MapDrawKind::Terrain;})==1,"terrain actual material command");
    require(gpu_->presentMap({list_,++sequence_,width_,height_}),"terrain real GPU submit");require(probe_.readbacks==sequence_,"terrain explicit diagnostic readback count");
    const bool exact=equals(expected,label);
    if(const char* directory=std::getenv("CONCORD_S41_TERRAIN_GPU_OUTPUT")){
        const std::filesystem::path target(directory);std::filesystem::create_directories(target);
        const auto write=[&](const char* suffix,const void* data,std::size_t bytes){std::ofstream file(target/(label+suffix),std::ios::binary);file.write(static_cast<const char*>(data),static_cast<std::streamsize>(bytes));require(bool(file),"terrain full evidence output");};
        write(".expected.bgra",expected.data(),expected.size()*4);write(".gpu.bgra",probe_.uploadedPixels.data(),probe_.uploadedPixels.size());
        write(".materials.bin",list_.resources().data(),list_.resources().size()*4);
    }
    std::cout<<"S41 GPU terrain case="<<label<<" width="<<width_<<" height="<<height_<<" resourceBytes="<<list_.resources().size()*4<<" commands="<<list_.commands().size()<<" cpuTargetRgbWrites=0 fullBgraExact="<<exact<<'\n';
    require(exact,"terrain entire GPU BGRA must equal independent original CPU reference");
}
} // namespace concord::ui::win32::diagnostics
