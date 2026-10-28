// Reuses the existing private GPU diagnostic owner and unmodified CPU sprite renderer.
namespace concord::ui::win32::diagnostics {
void MapVisionGpuFixture::finishSprites(const std::vector<std::uint32_t>& input,const std::vector<std::uint32_t>& expected,const std::string& label) {
    require(native_.finish(),native_.report().failure.data());require(list_.valid(),"sprite complete bounded packet");
    require(source_.finishGdiWrites()&&std::memcmp(source_.pixels(),input.data(),input.size()*4)==0,"sprite GPU producer no CPU target RGB write");
    require(list_.commands().front().kind==concord::render::MapDrawKind::Fill,"sprite packet constant input");
    std::uint32_t flags=0;int sprites=0;for(const auto& c:list_.commands()){require(c.kind!=concord::render::MapDrawKind::Bgra&&c.kind!=concord::render::MapDrawKind::Blend,"sprite no CPU source RGB payload");
        if(c.kind==concord::render::MapDrawKind::Sprite){++sprites;concord::render::MapSpriteMaterial m;std::memcpy(&m,reinterpret_cast<const unsigned char*>(list_.resources().data())+c.offset,sizeof(m));flags|=m.flags;}}
    require(sprites>0,"actual GPU source material command");
    if(sequence_==0){const auto selected=std::find_if(list_.commands().begin(),list_.commands().end(),[](const auto& c){return c.kind==concord::render::MapDrawKind::Sprite;});
        const auto index=static_cast<std::size_t>(selected-list_.commands().begin());concord::render::MapSpriteMaterial source;
        std::memcpy(&source,reinterpret_cast<const unsigned char*>(list_.resources().data())+selected->offset,sizeof(source));
        for(int test=0;test<10;++test){auto bad=list_;auto c=*selected;auto m=source;
            switch(test){case 0:c.offset=UINT32_MAX;break;case 1:c.auxiliary=UINT32_MAX;break;case 2:m.width=0;break;case 3:m.flags=32;break;
                case 4:m.reserved=1;break;case 5:m.airstrike=65536;break;case 6:m.indices=UINT32_MAX;break;case 7:m.flags=2;break;
                case 8:m.flags=4;m.litColors=UINT32_MAX;break;case 9:m.baseColors=UINT32_MAX;break;}
            if(test>1)c.offset=bad.internValues<concord::render::MapSpriteMaterial>({&m,1});bad.replace(index,c);require(!bad.valid(),"malformed sprite header/plane/table rejected before GPU");}}
    require(gpu_->presentMap({list_,++sequence_,width_,height_}),"actual GPU material sprite submit");require(probe_.readbacks==sequence_,"sprite explicit diagnostic readback");const bool exact=equals(expected,label);
    if(const char* directory=std::getenv("CONCORD_S41_GPU_SPRITE_OUTPUT")){const std::filesystem::path target(directory);std::filesystem::create_directories(target);
        const auto write=[&](const char* suffix,const void* data,std::size_t bytes){std::ofstream file(target/(label+suffix),std::ios::binary);file.write(static_cast<const char*>(data),static_cast<std::streamsize>(bytes));require(bool(file),"whole sprite material evidence");};
        write(".expected.bgra",expected.data(),expected.size()*4);write(".gpu.bgra",probe_.uploadedPixels.data(),probe_.uploadedPixels.size());write(".materials.bin",list_.resources().data(),list_.resources().size()*4);}
    std::cout<<"S41 GPU sprite case="<<label<<" width="<<width_<<" height="<<height_<<" sprites="<<sprites<<" materialFlags="<<flags<<" resourceBytes="<<list_.resources().size()*4<<" cpuSourceRgbPixels=0 cpuTargetRgbWrites=0 fullBgraExact="<<exact<<'\n';
    require(exact,"complete GPU sprite BGRA equals unchanged original CPU renderer");
}
} // namespace concord::ui::win32::diagnostics
