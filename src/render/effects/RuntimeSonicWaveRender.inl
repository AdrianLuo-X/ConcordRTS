// WaveClass's non-magnetic effect refracts the already composed scene.
// Native tables: abs(trunc(12*sin((radius+intensity)*.125)+.49)),
// displacement 0..3 pixels, and green/blue gain (110+8*index)/256.
// Geometry follows the saved advancing front and trailing edge, not a laser.
int drawRuntimeSonicWave(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    const RuntimeProjectileRenderActor& actor) {
    if (runtimeActorLifecycleIsExpired(actor.lifecycleState) || actor.remainingTicks <= 0) return 0;
    const double dx=actor.toX-actor.fromX, dy=actor.toY-actor.fromY;
    const double length=std::hypot(dx,dy);
    if (length < 240.0/256.0) return 0;
    const double front=std::clamp(actor.progress,0.0,1.0);
    const double tail=std::clamp(((actor.currentX-actor.fromX)*dx
        +(actor.currentY-actor.fromY)*dy)/(length*length),0.0,1.0);
    if (tail >= front) return 0;
    const double ux=dx/length, uy=dy/length;
    const auto point=[&](double t,int side) {
        const double inset=side ? (30.0/256.0)*(1.0-2.0*t) : 0.0;
        const double width=side*(100.0/256.0);
        POINT p=screenPoint(projectCellXY(actor.fromX+dx*t+ux*inset-uy*width,
            actor.fromY+dy*t+uy*inset+ux*width,
            actor.fromLevel+(actor.toLevel-actor.fromLevel)*t,camera),viewport,camera);
        p.x+=static_cast<LONG>(std::lround((1.0-t)*runtimeMuzzlePixelX(
            actor.sourceScreenOffsetX,camera,actor.sourceNativeFixedX)));
        p.y+=static_cast<LONG>(std::lround((1.0-t)*runtimeMuzzlePixelY(
            actor.sourceScreenOffsetY,camera,actor.sourceNativeFixedY)));
        return p;
    };
    const std::array<POINT,6> polygon{{point(front,-1),point(front,0),point(front,1),
        point(tail,1),point(tail,0),point(tail,-1)}};
    RECT bounds{polygon[0].x,polygon[0].y,polygon[0].x+1,polygon[0].y+1};
    for (const auto& p:polygon) {
        bounds.left=std::min(bounds.left,p.x);bounds.top=std::min(bounds.top,p.y);
        bounds.right=std::max(bounds.right,p.x+1);bounds.bottom=std::max(bounds.bottom,p.y+1);
    }
    RECT clip=viewport;
    const bool direct=hasPixelSurface(context);
    if (!direct && !dc) return 0;
    if (direct) {
        clip.left=std::max<LONG>(0,clip.left);clip.top=std::max<LONG>(0,clip.top);
        clip.right=std::min<LONG>(context.surfaceWidth,clip.right);
        clip.bottom=std::min<LONG>(context.surfaceHeight,clip.bottom);
    }
    RECT output{};if(!IntersectRect(&output,&clip,&bounds))return 0;
    const double scaleX=std::max(1,camera.tileWidth)/60.0;
    const double scaleY=std::max(1,camera.tileHeight)/30.0;
    const int margin=static_cast<int>(std::ceil(3*std::max(scaleX,scaleY)))+1;
    RECT sample{std::max(clip.left,output.left-margin),std::max(clip.top,output.top-margin),
        std::min(clip.right,output.right+margin),std::min(clip.bottom,output.bottom+margin)};
    const int width=sample.right-sample.left,height=sample.bottom-sample.top;
    std::vector<std::uint32_t> background(static_cast<std::size_t>(width)*height);
    if(direct)GdiFlush();
    for(int y=sample.top;y<sample.bottom;++y)for(int x=sample.left;x<sample.right;++x)
        background[static_cast<std::size_t>(y-sample.top)*width+x-sample.left]=direct
            ? context.surfacePixels[static_cast<std::size_t>(y)*context.surfaceWidth+x]
            : packRuntimeDibColor(concord::ui::win32::MapNativeOverlayWin32::readPixel(dc,x,y));
    const auto inside=[&](double x,double y) {
        bool contained=false;
        for(std::size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++) {
            const auto& a=polygon[i];const auto& b=polygon[j];
            if((a.y>y)!=(b.y>y) && x<(b.x-a.x)*(y-a.y)/static_cast<double>(b.y-a.y)+a.x)
                contained=!contained;
        }
        return contained;
    };
    static constexpr std::array<POINT,8> steps{{{0,-1},{1,-1},{1,0},{1,1},
        {0,1},{-1,1},{-1,0},{-1,-1}}};
    const auto origin=point(0,0);
    const POINT direction=steps[static_cast<std::size_t>(std::clamp(actor.facing,0,7))];
    const int age=std::max(0,actor.frameIndex)*15/std::max(1,context.fixedTickHz);
    const int intensity=std::max(0,100-age);
    int changed=0;
    for(int y=output.top;y<output.bottom;++y)for(int x=output.left;x<output.right;++x) {
        if(!inside(x+.5,y+.5))continue;
        const int radius=static_cast<int>(std::hypot((x-origin.x)/scaleX,(y-origin.y)/scaleY));
        const int index=std::clamp(std::abs(static_cast<int>(12.0*std::sin((radius+intensity)*.125)+.49)),0,12);
        const int displacement=index<2?0:index<7?1:index<10?2:3;
        const int sx=std::clamp(x+static_cast<int>(std::lround(direction.x*displacement*scaleX)),
            static_cast<int>(sample.left),static_cast<int>(sample.right)-1);
        const int sy=std::clamp(y+static_cast<int>(std::lround(direction.y*displacement*scaleY)),
            static_cast<int>(sample.top),static_cast<int>(sample.bottom)-1);
        const auto old=background[static_cast<std::size_t>(y-sample.top)*width+x-sample.left];
        const auto source=background[static_cast<std::size_t>(sy-sample.top)*width+sx-sample.left];
        const int gain=110+8*index;
        const auto boost=[&](int channel){return std::min(255,channel+(channel*gain)/256);};
        const auto color=(source&0xffff0000U)|(static_cast<std::uint32_t>(boost((source>>8U)&255U))<<8U)
            |static_cast<std::uint32_t>(boost(source&255U));
        if(color==old)continue;
        if(direct)context.surfacePixels[static_cast<std::size_t>(y)*context.surfaceWidth+x]=color;
        else SetPixelV(dc,x,y,colorRefFromRuntimeDibColor(color));
        ++changed;
    }
    return changed;
}

bool runRuntimeSonicWaveRenderContractCheck(std::string& error) {
    constexpr int width=640, height=480;
    const RECT viewport{12,13,width-12,height-13};
    std::vector<std::uint32_t> original(width*height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        original[static_cast<std::size_t>(y)*width+x]=0x00200000U
            | static_cast<std::uint32_t>((40+(y/4)%100)<<8 | (35+(x/4)%120));
    RuntimeMapRenderContext context;
    context.surfaceWidth=width;context.surfaceHeight=height;context.fixedTickHz=30;
    int cases=0;
    const auto fail=[&](const char* why){error=std::string("Sonic render: ")+why;return false;};
    for(int tileWidth:{30,60,120})for(int direction=0;direction<8;++direction) {
        RuntimeMapCamera camera;
        camera.tileWidth=tileWidth;camera.tileHeight=tileWidth/2;
        camera.worldOffsetX=viewport.left-width/2;camera.worldOffsetY=viewport.top-height/2;
        RuntimeProjectileRenderActor actor;
        const double angle=direction*3.14159265358979323846/4.0;
        actor.toX=3.0*std::cos(angle);actor.toY=3.0*std::sin(angle);
        actor.facing=direction;actor.remainingTicks=100;actor.lifecycleState="traveling";
        actor.hasCurrentPosition=true;actor.frameIndex=18;
        std::vector<std::uint32_t> previous;
        for(double front:{.45,.90}) {
            actor.progress=front;actor.currentX=actor.toX*(front-.15);
            actor.currentY=actor.toY*(front-.15);
            auto pixels=original;context.surfacePixels=pixels.data();
            const int changed=drawRuntimeSonicWave(nullptr,viewport,context,camera,actor);
            if(changed<=0 || changed>20000)return fail("pulse missing or filling the screen");
            int counted=0;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                const auto index=static_cast<std::size_t>(y)*width+x;
                if(pixels[index]==original[index])continue;
                ++counted;
                if(x<viewport.left||x>=viewport.right||y<viewport.top||y>=viewport.bottom)
                    return fail("pixel escaped the viewport");
                if((pixels[index]&0xffff0000U)!=(original[index]&0xffff0000U))
                    return fail("unexpected red/alpha gain");
                if(std::abs(x-width/2)<5 && std::abs(y-height/2)<5)
                    return fail("pulse left a continuous line back to the source");
            }
            if(counted!=changed)return fail("pixel count differs");
            auto replay=original;context.surfacePixels=replay.data();
            if(drawRuntimeSonicWave(nullptr,viewport,context,camera,actor)!=changed||replay!=pixels)
                return fail("same phase is not deterministic");
            if(!previous.empty()&&previous==pixels)return fail("front did not advance");
            previous=std::move(pixels);
            actor.currentX=actor.toX*front;actor.currentY=actor.toY*front;
            auto gap=original;context.surfacePixels=gap.data();
            if(drawRuntimeSonicWave(nullptr,viewport,context,camera,actor)!=0||gap!=original)
                return fail("completed tail left a visible wave");
            ++cases;
        }
        actor.remainingTicks=0;context.surfacePixels=original.data();
        if(drawRuntimeSonicWave(nullptr,viewport,context,camera,actor)!=0)
            return fail("expired actor rendered");
    }
    // Exercise a wave straddling both the viewport and a real GDI surface.
    RuntimeMapCamera camera;camera.worldOffsetX=-(viewport.right-20-viewport.left);
    camera.worldOffsetY=-(height/2-viewport.top);
    RuntimeProjectileRenderActor actor;actor.toX=3;actor.progress=.45;
    actor.currentX=.15;actor.remainingTicks=20;actor.frameIndex=5;actor.facing=3;
    auto expected=original;context.surfacePixels=expected.data();
    if(drawRuntimeSonicWave(nullptr,viewport,context,camera,actor)<=0)return fail("clipped pulse missing");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return fail("GDI fixture");}
    const auto old=SelectObject(dc,bitmap);
    std::copy(original.begin(),original.end(),static_cast<std::uint32_t*>(bits));
    context.surfacePixels=nullptr;
    const int fallback=drawRuntimeSonicWave(dc,viewport,context,camera,actor);
    GdiFlush();
    const bool equal=std::equal(expected.begin(),expected.end(),static_cast<std::uint32_t*>(bits));
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    if(fallback<=0||!equal)return fail("GDI and direct pixels differ");
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        if((x<viewport.left||x>=viewport.right||y<viewport.top||y>=viewport.bottom)
            &&expected[static_cast<std::size_t>(y)*width+x]!=original[static_cast<std::size_t>(y)*width+x])
            return fail("clipped pulse escaped the viewport");
    std::cout<<"Sonic render passed: directionalPhases="<<cases<<" clippedGdi=1\n";
    return true;
}
