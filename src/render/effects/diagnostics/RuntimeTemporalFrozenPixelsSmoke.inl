bool runRuntimeTemporalFrozenPixelsSmoke(RuntimeWindowState& assets,
    const std::string& root,std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why){error="Temporal body pixels: "+scene+": "+why;return false;};
    constexpr int width=640,height=480;
    struct Surface {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
        Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            dc=CreateCompatibleDC(nullptr);void* raw=nullptr;if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
            pixels=static_cast<std::uint32_t*>(raw);if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
        ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } surface;
    if(!surface.dc||!surface.bitmap||!surface.pixels)return fail("surface");
    auto context=runtimeTerrainMetadataContext(assets);
    context.rules=&assets.rules;context.art=&assets.art;context.theater=&assets.theater;
    context.assetRootPath=runtimeAssetRootPath(assets.config);
    context.tileRootPath=runtimeTileRootPath(assets.config,assets.report.map);
    context.structureRootPath=runtimeStructureRootPath(assets.config,assets.report.map);
    context.palettePath=runtimePalettePath(assets.config,assets.report.map);
    context.structurePalettePath=runtimeStructurePalettePath(assets.config,assets.report.map);
    context.surfacePixels=surface.pixels;context.surfaceWidth=width;context.surfaceHeight=height;
    context.shadowsEnabled=false;context.vision=nullptr;context.animationTick=70;context.fixedTickHz=30;
    GameWorld world=assets.world;world.accessObjects().clear();world.accessTerrainObjects().clear();world.accessSmudges().clear();
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    std::vector<SpecialMechanismRenderSnapshot> snapshots;
    context.specialMechanismSnapshots=&snapshots;
    const RECT viewport{0,0,width,height};int cases=0;std::uint64_t blended=0;
    for(const char* type:{"E1","MTNK","HTNK","GAPOWR","GAWEAP","ATESLA"})
        for(bool damaged:{false,true})for(int zoom:{48,60,72}) {
        scene=std::string(type)+" damaged="+std::to_string(damaged)+" zoom="+std::to_string(zoom);
        WorldObjectInstance object;object.instanceId=12000;object.type=type;object.owner="Russians";
        object.kind=std::string(type)=="E1"?WorldObjectKind::Infantry:
            (std::string(type)=="MTNK"||std::string(type)=="HTNK")?WorldObjectKind::Unit:WorldObjectKind::Structure;
        object.alive=true;object.x=114;object.y=215;object.cell=runtimeCellKey(object.x,object.y);
        object.facing=64;object.turretFacing=128;object.mission="Guard";object.poweredOperational=true;
        object.health=runtimeMaxHealthForObject(assets,object);if(damaged)object.health=std::max(1,object.health/4);
        RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=zoom;camera.tileHeight=zoom/2;
        const auto point=runtimeProjectCellXY(object.x,object.y,0,camera);
        camera.worldOffsetX=point.x-width/2;camera.worldOffsetY=point.y-height/2-40;
        const auto draw=[&](std::vector<std::uint32_t>& pixels){
            const auto stats=drawRuntimeMap(surface.dc,viewport,world,context,camera,*cache);GdiFlush();
            pixels.assign(surface.pixels,surface.pixels+width*height);return stats.rendered;};
        std::vector<std::uint32_t> background,normal,frozen,repeat,restored;
        snapshots.clear();world.accessObjects().clear();if(!draw(background))return fail("background");
        world.accessObjects()={object};if(!draw(normal))return fail("normal body");
        SpecialMechanismRenderSnapshot temporal;temporal.kind="temporal";temporal.targetObjectId=object.instanceId;
        temporal.requiredProgress=10000;temporal.progress=100;snapshots={temporal};
        if(!draw(frozen)||!draw(repeat)||repeat!=frozen)return fail("paused repaint");
        std::size_t changed=0;
        for(std::size_t i=0;i<normal.size();++i) {
            std::uint32_t expected=0;
            for(int shift:{0,8,16})expected|=static_cast<std::uint32_t>(
                ((((normal[i]>>shift)&255U)*128U)+(((background[i]>>shift)&255U)*127U)+127U)/255U)<<shift;
            if((frozen[i]&0xFFFFFFU)!=expected)return fail("body/layers not blended once at half opacity, pixel="+std::to_string(i));
            changed+=(normal[i]&0xFFFFFFU)!=(background[i]&0xFFFFFFU);
        }
        if(changed<40)return fail("original body missing");blended+=changed;
        if(!root.empty()&&!damaged&&zoom==60) {
            if(!writeRuntimeSmokeBmp(root+"/"+type+"-body-normal.bmp",normal.data(),width,height,error)
                ||!writeRuntimeSmokeBmp(root+"/"+type+"-body-frozen.bmp",frozen.data(),width,height,error))return fail(error);
        }
        snapshots.clear();if(!draw(restored)||restored!=normal)return fail("unfreeze did not restore exact normal body");
        ++cases;
    }
    std::cout<<"Temporal body pixels: "<<cases<<" infantry/voxel/turret/animated-building/damage/zoom cases; "
        <<blended<<" native pixels at half opacity; ground unchanged, paused repeat and release exact PASS\n";
    return true;
}
