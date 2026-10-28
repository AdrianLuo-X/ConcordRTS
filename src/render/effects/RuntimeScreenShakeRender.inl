RuntimeScreenShakeOffset runtimeScreenShakeForRender(const RuntimeMapRenderContext& context) {
    if(!context.rules || !context.impactActors)return {};
    std::int64_t horizontal=0,vertical=0;
    for(const auto& actor:*context.impactActors){
        if(actor.remainingTicks<=0 || (actor.renderIntent!="warheadImpact" && actor.renderIntent!="bombImpact"
            && actor.renderIntent!="superWeaponEffect" && actor.renderIntent!="impact"))continue;
        const auto* profile=context.rules->findWarheadLifecycleProfile(actor.warheadId);
        if(!profile)continue;
        std::uint32_t seed=2166136261U;
        for(unsigned char ch:actor.warheadId){seed^=ch;seed*=16777619U;}
        seed^=static_cast<std::uint32_t>(actor.projectileActorId)*0x85ebca6bU;
        seed^=static_cast<std::uint32_t>(actor.cellX)*0xc2b2ae35U;
        seed^=static_cast<std::uint32_t>(actor.cellY)*0x27d4eb2fU;
        const auto offset=runtimeScreenShakeRange(profile->shakeXlo,profile->shakeXhi,
            profile->shakeYlo,profile->shakeYhi,std::max(0,actor.totalTicks-actor.remainingTicks),context.fixedTickHz,seed);
        horizontal+=offset.x;vertical+=offset.y;
    }
    return {static_cast<int>(std::clamp<std::int64_t>(horizontal,-runtimeScreenShakeMaximumPixels,runtimeScreenShakeMaximumPixels)),
        static_cast<int>(std::clamp<std::int64_t>(vertical,-runtimeScreenShakeMaximumPixels,runtimeScreenShakeMaximumPixels))};
}

bool applyRuntimeScreenShake(HDC dc,const RECT& viewport,const RuntimeMapRenderContext& context,RuntimeMapRenderCache& cache) {
    const auto offset=runtimeScreenShakeForRender(context);
    if(!offset.x && !offset.y){
        if(auto* native=concord::ui::win32::MapNativeOverlayWin32::current();native&&native->dc()==dc){
            // No colour operation: close the dynamic map interaction segment
            // before the separately sampled sidebar/text/bitmap overlay.
            concord::render::MapDrawCommand boundary;boundary.kind=concord::render::MapDrawKind::NoOp;
            boundary.right=native->width();boundary.bottom=native->height();(void)native->emit(boundary);
        }
        return false;
    }
    if(auto* native=concord::ui::win32::MapNativeOverlayWin32::current();native&&native->dc()==dc) {
        concord::render::MapDrawCommand command;command.kind=concord::render::MapDrawKind::Shift;
        command.left=viewport.left;command.top=viewport.top;command.right=viewport.right;command.bottom=viewport.bottom;
        command.originX=offset.x;command.originY=offset.y;return native->emit(command);
    }

    GdiFlush();
    if(context.surfacePixels)return runtimeTranslateScreenShakePixels(context.surfacePixels,
        context.surfaceWidth,context.surfaceHeight,viewport.left,viewport.top,viewport.right,viewport.bottom,
        offset,cache.screenShakePixels);
    if(!dc)return false;
    const int width=viewport.right-viewport.left,height=viewport.bottom-viewport.top;
    if(width<=0 || height<=0)return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC capture=CreateCompatibleDC(dc);void* raw=nullptr;
    HBITMAP bitmap=CreateDIBSection(capture,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    if(!capture || !bitmap || !raw){if(bitmap)DeleteObject(bitmap);if(capture)DeleteDC(capture);return false;}
    const auto old=SelectObject(capture,bitmap);
    bool ok=BitBlt(capture,0,0,width,height,dc,viewport.left,viewport.top,SRCCOPY)!=FALSE;
    GdiFlush();
    if(ok)ok=runtimeTranslateScreenShakePixels(static_cast<std::uint32_t*>(raw),width,height,0,0,width,height,offset,cache.screenShakePixels);
    if(ok)ok=BitBlt(dc,viewport.left,viewport.top,width,height,capture,0,0,SRCCOPY)!=FALSE;
    SelectObject(capture,old);DeleteObject(bitmap);DeleteDC(capture);return ok;
}
