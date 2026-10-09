// Exercise the complete map overlay pass with original archive-only art. The
// expected surface is the same bare terrain plus an independently requested
// native SHP, so a placeholder cross cannot pass as a rendered fence or wall.
bool runRuntimeOverlayArchiveContract(const RuntimeMapRenderContext& assets, std::string& error) {
    const auto fail=[&](const std::string& why){error="Archive overlay: "+why;return false;};
    const std::string root=assets.assetRootPath;
    IniFile ri,ai;RulesDatabase rules;ArtDatabase art;
    if(!ri.loadFromFile(root+"/ra2md/localmd/rulesmd.ini")||!rules.loadFromIni(ri)
        ||!ai.loadFromFile(root+"/ra2md/localmd/artmd.ini")||!art.loadFromIni(ai))return fail("rules/art");
    constexpr int width=360,height=240;
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO bmi{};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=width;
    bmi.bmiHeader.biHeight=-height;bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    void* surface=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&surface,nullptr,0);
    if(!dc||!bitmap||!surface){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return fail("DIB");}
    const auto previous=SelectObject(dc,bitmap);
    struct Cleanup{HDC dc;HBITMAP bitmap;HGDIOBJ previous;~Cleanup(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}} cleanup{dc,bitmap,previous};
    auto* pixels=static_cast<std::uint32_t*>(surface);const RECT viewport{0,0,width,height};
    const auto copyPixels=[&](){GdiFlush();std::vector<std::uint32_t> result(pixels,pixels+width*height);for(auto& p:result)p&=0xffffffU;return result;};
    int cases=0;std::uint64_t spritePixels=0;
    const char* output=std::getenv("CONCORD_NATIVE_SHROUD_OUTPUT");
    const std::filesystem::path outputRoot=output&&*output?output:"build/native-shroud-smoke";
    for(const auto& theaterCase:{std::pair{"temperatmd","tem"},std::pair{"snowmd","sno"},
        std::pair{"urbanmd","urb"},std::pair{"urbannmd","ubn"},std::pair{"desertmd","des"},std::pair{"lunarmd","lun"}}) {
        IniFile ti;TheaterDatabase theater;const auto path=root+"/ra2md/localmd/"+theaterCase.first+".ini";
        if(!ti.loadFromFile(path)||!theater.loadFromIni(ti,path))return fail(path);
        RuntimeMapRenderContext context;context.assetRootPath=root;
        context.tileRootPath=assets.tileRootPath;context.structureRootPath=assets.structureRootPath;
        context.rules=&rules;context.art=&art;context.theater=&theater;
        context.palettePath=root+"/palettes/iso"+theaterCase.second+".pal";
        context.structurePalettePath=root+"/extracted_palettes/"+
            (std::string(theaterCase.second)=="ubn"||std::string(theaterCase.second)=="des"||std::string(theaterCase.second)=="lun"?"cachemd_unit":"cache_unit")+theaterCase.second+".pal";
        context.surfacePixels=pixels;context.surfaceWidth=width;context.surfaceHeight=height;
        context.useTileTextures=true;context.vision=nullptr;context.mapFacts=nullptr;
        RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=60;camera.tileHeight=30;
        camera.worldOffsetX=-width/2;camera.worldOffsetY=300-height/2;
        GameWorld world;MapDatabase emptyMap;IniFile emptyIni;
        if(!emptyIni.loadFromString("[Map]\nSize=0,0,20,20\nLocalSize=0,0,20,20\n")
            ||!emptyMap.loadFromIni(emptyIni)||!world.loadFromMap(emptyMap))return fail("fixture world");
        for(int y=1;y<=20;++y)for(int x=1;x<=20;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=0;world.accessTerrainCells().push_back(c);}
        auto& target=world.accessTerrainCells()[9*20+9];
        const POINT center=screenPoint(projectCell(target,camera),viewport,camera);
        RuntimeMapRenderCache plainCache;
        std::fill(pixels,pixels+width*height,0);
        const auto plainStats=drawRuntimeMap(dc,viewport,world,context,camera,plainCache);const auto base=copyPixels();
        if(!plainStats.rendered||!plainStats.texturedTerrainCells)return fail("fixture terrain not rendered");
        for(const char* id:{"GASAND","GAWALL","NAWALL","GAFWLL","CAFNCB","CAFNCW","CAKRMW","CAFNCP"}) {
            const auto* building=rules.findBuildingType(id);
            for(int frame:{0,5,10}) {
                if(frame&&(!building||!building->wall))continue;
                RuntimeMapRenderCache reference,actual;
                const auto* sprite=loadRuntimeAuxiliaryShpSpriteFromCache(id,frame,context,reference,false);
                if(!sprite||sprite->sourcePath.find('!')==std::string::npos)return fail(std::string(id)+" lacks native archive art");
                std::copy(base.begin(),base.end(),pixels);
                if(!drawRuntimeAuxiliaryShpSprite(dc,viewport,center,context,camera,reference,id,frame,0,0,false,false,false,RGB(255,255,255),nullptr,false))return fail(std::string(id)+" reference");
                const auto expected=copyPixels();std::size_t drawn=0;
                for(std::size_t p=0;p<expected.size();++p)if(expected[p]!=base[p])++drawn;
                if(!drawn)return fail(std::string(id)+" reference empty");spritePixels+=drawn;
                target.hasOverlay=true;target.overlayType=target.overlayImage=id;target.overlayFrame=frame;
                for(int warm=0;warm<2;++warm){
                    std::fill(pixels,pixels+width*height,0);
                    const auto stats=drawRuntimeMap(dc,viewport,world,context,camera,actual);const auto result=copyPixels();
                    if(stats.visibleOverlayCells!=1)return fail("fixture overlay not visited");
                    if(result!=expected){
                        const auto label=std::string(theaterCase.first)+"-"+id+"-"+std::to_string(frame);
                        writeRuntimeBgraBmp(outputRoot/("overlay-"+label+"-actual.bmp"),width,height,result);
                        writeRuntimeBgraBmp(outputRoot/("overlay-"+label+"-expected.bmp"),width,height,expected);
                        return fail(label+" map overlay differs from native sprite, warm="+std::to_string(warm));
                    }
                    if(warm==0&&std::string(id)=="GAWALL"&&frame==5){
                        writeRuntimeBgraBmp(outputRoot/("overlay-"+std::string(theaterCase.first)+"-wall.bmp"),width,height,result);
                    }
                }
                target.hasOverlay=false;target.overlayType.clear();target.overlayImage.clear();target.overlayFrame=0;++cases;
            }
        }
    }
    std::cout<<"Archive overlays: cases="<<cases<<" nativePixels="<<spritePixels<<" theaters=6 cold/warm/fullMap=exact\n";
    return true;
}
