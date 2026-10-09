bool runRuntimeMagnetronLiftShadowContractCheck(const RuntimeMapRenderContext& context,
    const GameWorld& loadedWorld, std::string& error) {
    const auto fail=[&](const std::string& why){error="Magnetron ground shadow: "+why;return false;};
    constexpr int width=480,height=320;
    const RECT viewport{0,0,width,height};
    int cases=0;
    for(const char* type:{"MTNK","HTNK","FV","DEST"}) for(int groundLevel:{0,2}) for(int lift:{1,3}) {
        GameWorld world=loadedWorld; world.accessObjects().clear(); world.accessTerrainCells().clear();
        world.accessTerrainObjects().clear(); world.accessSmudges().clear();
        TerrainCell ground; ground.x=ground.y=20; ground.cellIndex512=20*512+20;
        ground.tileIndex=ground.subTileIndex=0; ground.level=groundLevel;
        world.accessTerrainCells().push_back(ground);
        WorldObjectInstance target; target.instanceId=123; target.type=type; target.owner="Americans";
        target.kind=WorldObjectKind::Unit; target.x=target.y=20; target.level=groundLevel;
        target.health=400; target.alive=target.airborne=true;
        target.hasRenderPosition=target.renderMoveActive=true; target.renderX=target.renderY=20;
        target.renderLevel=groundLevel+lift; target.mission="MagBeamSuspended";
        world.accessObjects().push_back(target);
        RuntimeMapCamera camera; camera.initialized=true;
        const auto origin=projectCellXY(20,20,groundLevel,camera);
        camera.worldOffsetX=origin.x-width/2; camera.worldOffsetY=origin.y-220;
        RuntimeShadowReceiverTerrainLookup receivers{{runtimeTerrainCellCoordinateKey(20,20),&ground}};
        const auto receiver=runtimeShadowReceiverLevelForObject(target,false,receivers);
        const auto body=screenPoint(runtimeObjectDrawPoint(target,{},camera),viewport,camera);
        const auto shadow=screenPoint(runtimeObjectShadowDrawPoint(target,{},camera,false,receiver),viewport,camera);
        if(receiver!=groundLevel||shadow.x!=body.x||shadow.y-body.y!=lift*camera.tileHeight/2)
            return fail(std::string(type)+" shadow followed the suspended chassis");
        auto cache=std::make_unique<RuntimeMapRenderCache>();
        auto probe=context; probe.vision=nullptr; probe.mapFacts=nullptr; probe.specialMechanismSnapshots=nullptr;
        probe.useTileTextures=false; probe.surfaceWidth=width; probe.surfaceHeight=height;
        std::vector<std::uint32_t> pixels(width*height,0x00667549),without;
        probe.surfacePixels=pixels.data(); probe.shadowsEnabled=false;
        drawRuntimeMap(nullptr,viewport,world,probe,camera,*cache); without=pixels;
        std::fill(pixels.begin(),pixels.end(),0x00667549); probe.shadowsEnabled=true;
        const auto stats=drawRuntimeMap(nullptr,viewport,world,probe,camera,*cache);
        int changed=0;
        for(int y=shadow.y-18;y<=shadow.y+18;++y) for(int x=shadow.x-48;x<=shadow.x+48;++x)
            if(x>=0&&x<width&&y>=0&&y<height&&pixels[y*width+x]!=without[y*width+x]) ++changed;
        if(stats.unitShadowsDrawn!=1||changed<10) return fail(std::string(type)+" native ground shadow pixels missing");
        ++cases;
    }
    std::cout<<"Magnetron ground shadow: "<<cases<<" native ground/raised-terrain cases, independent anchors and actual pixels passed\n";
    return true;
}

bool runRuntimeMagnetronRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<SpecialMechanismRenderSnapshot>& beams, std::string& error) {
    const auto fail = [&](const std::string& text) { error = "Magnetron pixels: " + text; return false; };
    if (beams.size() != 3 || objects.size() != beams.size()*2) return fail("missing stock combat poses");
    constexpr int width=480, height=280;
    constexpr std::uint32_t background=0x00667549;
    std::vector<std::uint32_t> pixels(width*height,background), sheet(width*height*3,background);
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    auto probe=context;
    probe.vision=nullptr; probe.surfacePixels=pixels.data(); probe.surfaceWidth=width; probe.surfaceHeight=height;
    probe.fixedTickHz=30; probe.animationTick=18;
    std::unordered_map<std::string,COLORREF> colors{{"yuricountry",RGB(220,45,45)}, {"americans",RGB(45,85,220)}};
    probe.ownerRemapColors=&colors;
    const RECT viewport{0,0,width,height};
    struct NativePixel { COLORREF input; int strength; COLORREF output; };
    const NativePixel nativePixels[]={
        {RGB(0,0,0),110,RGB(0,0,0)}, {RGB(32,64,16),110,RGB(38,64,43)},
        {RGB(32,64,16),182,RGB(43,64,61)}, {RGB(102,117,73),146,RGB(131,117,239)},
        {RGB(255,0,0),182,RGB(255,0,0)}, {RGB(0,200,0),146,RGB(0,200,0)},
        {RGB(16,8,4),110,RGB(19,8,10)}, {RGB(16,8,4),182,RGB(21,8,15)}
    };
    for(const auto& value:nativePixels)
        if(runtimeMagBeamPixelColor(value.input,value.strength)!=value.output)
            return fail("retail 0x760190 RGB truth vector mismatch");
    // Native WaveClass::magnetic pixel routine (gamemd 0x760190) amplifies
    // existing red/blue channels and preserves green; black stays black.
    // Opaque source-color painting cannot satisfy this background contract.
    for(const std::uint32_t ground:{0x00000000U,0x00667549U,0x00304460U,0x0090A0B0U}) {
        std::fill(pixels.begin(),pixels.end(),ground);
        drawRuntimeMagBeamStatus(nullptr,viewport,probe,{40,40},{440,40},208,7,"255,200,255");
        const auto center=pixels[40*width+240];
        if((center&0x0000FF00U)!=(ground&0x0000FF00U)
            || (ground==0 && center!=0))
            return fail("native transparent beam must preserve underlying green/black pixels");
    }
    // Compare the interactive GDI fallback with the offscreen surface on
    // patterned ground, including clipped horizontal/vertical/diagonal beams.
    std::vector<std::uint32_t> patterned(width*height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        patterned[y*width+x]=((x/7+y/7)%2)?0x00506030U:0x00283018U;
    pixels=patterned;probe.surfacePixels=pixels.data();
    const auto drawPattern=[&](HDC dc,const RuntimeMapRenderContext& target) {
        drawRuntimeMagBeamStatus(dc,viewport,target,{-30,30},{510,30},208,7,"255,200,255");
        drawRuntimeMagBeamStatus(dc,viewport,target,{150,-30},{150,310},208,7,"255,200,255");
        drawRuntimeMagBeamStatus(dc,viewport,target,{-30,-30},{510,310},208,7,"255,200,255");
    };
    drawPattern(nullptr,probe);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    HDC memory=CreateCompatibleDC(nullptr);
    if(!bitmap||!memory||!bits) {
        if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);
        return fail("native GDI test surface unavailable");
    }
    auto old=SelectObject(memory,bitmap);auto fallback=probe;fallback.surfacePixels=nullptr;
    std::copy(patterned.begin(),patterned.end(),static_cast<std::uint32_t*>(bits));
    drawPattern(memory,fallback);GdiFlush();
    const bool matched=std::equal(pixels.begin(),pixels.end(),static_cast<std::uint32_t*>(bits));
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
    if(!matched)return fail("GDI and direct transparent beam pixels differ");
    for(std::size_t i=0;i<pixels.size();++i)
        if((pixels[i]&0x0000FF00U)!=(patterned[i]&0x0000FF00U))
            return fail("beam erased background detail on patterned ground");
    if(const char* folder=std::getenv("CONCORD_MAGNETRON_DIR"))
        if(*folder&&!writeRuntimeBgraBmp(std::string(folder)+"/magnetron-transparency.bmp",width,height,pixels))
            return fail("transparency preview write failed");
    std::cout<<"Magnetron pixels: 8 retail RGB vectors, 4 backgrounds, clipped GDI/direct paths and detail preservation passed\n";
    for(std::size_t row=0; row<beams.size(); ++row) {
        RuntimeMapCamera camera; camera.initialized=true;
        const auto center=projectCellXY(objects[row*2].x,objects[row*2].y,0,camera);
        camera.worldOffsetX=center.x-105; camera.worldOffsetY=center.y-175;
        GameWorld world; world.accessObjects()={objects[row*2],objects[row*2+1]};
        std::vector<SpecialMechanismRenderSnapshot> snapshots{beams[row]}; probe.specialMechanismSnapshots=&snapshots;
        const auto draw=[&]() {
            RuntimeMapRenderStats stats;
            drawRuntimeSpecialMechanismOverlays(nullptr,viewport,world,probe,camera,*cache,stats);
            return stats.specialMechanismRenderActors;
        };
        std::fill(pixels.begin(),pixels.end(),background);
        if(draw()!=1) return fail("active link was invisible on the pixel surface");
        const auto beamPixels=pixels;
        auto from=screenPoint(projectCellXY(beams[row].sourceX,beams[row].sourceY,beams[row].sourceLevel,camera),viewport,camera);
        from.x+=beams[row].sourceScreenOffsetX; from.y+=beams[row].sourceScreenOffsetY;
        const auto to=screenPoint(projectCellXY(beams[row].targetX,beams[row].targetY,beams[row].targetLevel,camera),viewport,camera);
        const double dx=to.x-from.x,dy=to.y-from.y,square=dx*dx+dy*dy;
        int count=0;
        for(int y=0; y<height; ++y) for(int x=0; x<width; ++x) {
            if(pixels[y*width+x]==background) continue;
            ++count;
            const double t=square==0 ? 0 : std::clamp(((x-from.x)*dx+(y-from.y)*dy)/square,0.0,1.0);
            if(std::hypot(x-from.x-t*dx,y-from.y-t*dy)>3.6)
                return fail("electric zigzag or arrow remained outside the purple band");
        }
        if(count<300) return fail("purple band was too sparse");
        std::fill(pixels.begin(),pixels.end(),background);
        probe.fixedTickHz=60; probe.animationTick=36;
        if(draw()!=1 || pixels!=beamPixels) return fail("same-time repaint changed with simulation frequency");
        probe.fixedTickHz=30; probe.animationTick=18;
        std::fill(pixels.begin(),pixels.end(),background);
        const auto& target=world.getObjects().back();
        const auto ground=screenPoint(projectCellXY(target.renderX,target.renderY,0,camera),viewport,camera);
        for(int y=-8; y<=8; ++y) for(int x=-36; x<=36; ++x)
            if(x*x/1296.0+y*y/64.0<=1.0) blendSurfacePixelWithTint(probe,ground.x+x,ground.y+y,RGB(25,35,18),110);
        for(const auto& object:world.getObjects()) {
            const auto origin=runtimeObjectBodyAnchorForRender(object,viewport,probe,camera);
            if(!tryDrawVoxelSprite(nullptr,viewport,object,origin,probe,camera,*cache))
                return fail("original " + object.type + " voxel missing");
        }
        draw();
        std::copy(pixels.begin(),pixels.end(),sheet.begin()+row*width*height);
        std::cout << "Magnetron pixels " << target.type << " lifted-model/muzzle/purple-band=ok\n";
    }
    if(const char* folder=std::getenv("CONCORD_MAGNETRON_DIR"))
        if(*folder && !writeRuntimeBgraBmp(std::string(folder)+"/magnetron-native.bmp",width,height*3,sheet))
            return fail("preview write failed");
    return true;
}
