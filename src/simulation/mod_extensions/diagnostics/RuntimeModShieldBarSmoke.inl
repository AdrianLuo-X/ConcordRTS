bool runRuntimeModShieldBarCheck(RuntimeWindowState& state, WorldObjectInstance& target, std::string& error) {
    constexpr int panelWidth=128,width=panelWidth*4,height=96;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* raw=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    if(!dc || !bitmap || !raw){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);error="shield surface";return false;}
    const auto old=SelectObject(dc,bitmap);auto* pixels=static_cast<std::uint32_t*>(raw);
    std::fill(pixels,pixels+width*height,0x00384838);
    const auto saved=target.modExtensions;
    bool ok=true;
    for(int panel=0;panel<4;++panel){
        target.modExtensions.shieldCapacity=100;target.modExtensions.shieldHealth=panel==0?100:panel==2?0:50;
        target.modExtensions.shieldOnline=panel!=3;
        RuntimeProjectedObject projected;projected.object=&target;
        projected.selectionBounds={panel*panelWidth+46,48,panel*panelWidth+82,84};
        projected.center=projected.renderCenter={panel*panelWidth+64,64};
        drawRuntimeSelectedHealthBar(dc,state,projected,nullptr);
        GdiFlush();
        const auto color=pixels[34*width+panel*panelWidth+72]&0xffffff;
        if(panel==0)ok=ok && color==0x0060d0ff;
        else ok=ok && color!=0x0060d0ff;
    }
    target.modExtensions=saved;
    if(const char* path=std::getenv("CONCORD_MOD_SHIELD_BAR_BMP");path && *path)
        ok=writeRuntimeSmokeBmp(path,pixels,width,height,error) && ok;
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    if(!ok){if(error.empty())error="shield bar full/partial/broken/power states";return false;}
    std::cout<<"Mod shield bar: actual selected health UI full/partial/broken/offline passed\n";
    return true;
}
