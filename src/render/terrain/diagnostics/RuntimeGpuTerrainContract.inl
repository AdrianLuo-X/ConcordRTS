// GPU consumer checks retain the original software/retail-instruction oracle.
bool runRuntimeGpuTerrainContractCheck(const RuntimeMapRenderContext& assets,bool warp,std::string& error)try {
    if(std::getenv("CONCORD_S41_VXL_GPU_CONTRACT"))return runRuntimeGpuVxlModeContractCheck(assets,warp,error);
    if(std::getenv("CONCORD_S41_LIGHT_WORLD_CONTRACT"))return runRuntimeGpuLightWorldContractCheck(assets,warp,error);
    if(std::getenv("CONCORD_S41_LIGHT_GPU_CONTRACT"))return runRuntimeGpuLightPrecisionContractCheck(assets,warp,error);
    if(std::getenv("CONCORD_S41_SHADOW_GPU_CONTRACT"))return runRuntimeGpuShadowContractCheck(assets,warp,error);
    if(std::getenv("CONCORD_S41_SPRITE_GPU_CONTRACT"))return runRuntimeGpuSpriteMaterialContractCheck(assets,warp,error);
    if(std::getenv("CONCORD_S41_FROZEN_GPU_CONTRACT"))return runRuntimeGpuFrozenWorldContractCheck(assets,warp,error);
    RuntimeRendererEnvironmentScope environment;
    using namespace RuntimeTerrainNativeDepthContractDetail;
    const auto require=[](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);};
    MapVisionGpuFixture gpu(warp);Report report;int cases=0;
    std::array<std::uint32_t,256> colors{},tags{};std::vector<RuntimePaletteColor> palette(256);
    for(unsigned i=0;i<256;++i){colors[i]=((i*3U&255U)<<16U)|((i*23U&255U)<<8U)|(i*19U&255U);tags[i]=0x80000000U|i;}
    colors[255]=0;for(unsigned i=0;i<256;++i)palette[i]={static_cast<unsigned char>(colors[i]>>16U),static_cast<unsigned char>(colors[i]>>8U),static_cast<unsigned char>(colors[i])};
    const auto show=[&](RuntimeTerrainLayerCache& material,const RuntimeTerrainLayerCache& expected,const RuntimeMapCamera& camera,
        const std::vector<RuntimePaletteColor>& actualPalette,const std::string& label,int width,int height,bool alreadyBegun){
        auto decoded=material;std::size_t indexed=0;for(auto& p:decoded.pixels)if(p&0x80000000U){require((p&0x7fffff00U)==0,"terrain raw index tag");const auto c=actualPalette[p&255U];p=(static_cast<std::uint32_t>(c.r)<<16U)|(static_cast<std::uint32_t>(c.g)<<8U)|c.b;++indexed;}
        require(indexed>0,"terrain nonempty real index input");if(!compare(label.c_str(),expected,decoded,error,report))throw std::runtime_error(error);
        std::vector<std::uint32_t> input(static_cast<std::size_t>(width)*height);for(std::size_t i=0;i<input.size();++i)input[i]=0xa3152729U^static_cast<std::uint32_t>(i*1949U);
        if(!alreadyBegun)gpu.beginTerrain(width,height,input);
        auto context=assets;context.surfacePixels=nullptr;context.surfaceWidth=width;context.surfaceHeight=height;
        {RuntimeGpuWorldScope scope(gpu.drawDc());RuntimeGpuTerrainPaletteLease paletteLease(&actualPalette);const RECT viewport{0,0,width,height};
            (void)runtimeGpuWorldFill(gpu.drawDc(),viewport,0x000F161FU);require(drawTerrainLayerToViewport(gpu.drawDc(),viewport,context,camera,material),"terrain production viewport emitter");}
        std::vector<std::uint32_t> view(static_cast<std::size_t>(width)*height,0x000F161FU);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){const int sx=x+camera.worldOffsetX-expected.originX,sy=y+camera.worldOffsetY-expected.originY;
            if(sx>=0&&sy>=0&&sx<expected.width&&sy<expected.height)view[static_cast<std::size_t>(y)*width+x]=expected.pixels[static_cast<std::size_t>(sy)*expected.width+sx];}
        gpu.finishTerrain(input,view,std::to_string(cases++)+'-'+label);
    };
    const auto nearImage=fixture(1,12,5),farImage=fixture(2,220,240);auto noZ=fixture(2,8,8);noZ.hasBaseZData=false;noZ.hasExtraZData=false;
    auto partial=fixture(2,8,8);partial.hasExtraZData=false;auto transparent=noZ;std::fill(transparent.pixels.begin(),transparent.pixels.end(),0);
    struct Scene{const char* label;std::vector<RuntimeTmpSubTileImage> images;POINT center;int level,tileWidth;};
    const std::vector<Scene> scenes{
        {"native-nearImage-farImage",{nearImage,farImage},{80,65},0,60},{"native-farImage-nearImage",{farImage,nearImage},{80,65},0,60},
        {"equal-Z-opaque-black-index255",{nearImage,fixture(255,12,5)},{80,65},0,60},
        {"unsigned-high-Z",{fixture(1,130,135),farImage},{80,65},0,60},{"base-extra-overlap",{fixture(2,40,230)},{80,65},0,60},
        {"native-noZ-native",{nearImage,noZ,farImage},{80,65},0,60},{"partial-extra-Z",{nearImage,partial,farImage},{80,65},0,60},
        {"transparent0-preserves-Z",{nearImage,transparent,farImage},{80,65},0,60},{"top-left-clip",{nearImage,farImage},{-5,10},7,60},
        {"bottom-right-clip",{nearImage,farImage},{158,111},14,60},{"level-seed-cancellation",{nearImage,farImage},{80,65},14,60},
        {"nonnative-grid120",{nearImage,farImage},{80,65},0,120}};
    for(const auto& scene:scenes){auto material=emptyLayer(160,120),expected=material;RuntimeMapCamera camera;camera.tileWidth=scene.tileWidth;camera.tileHeight=scene.tileWidth/2;int index=0;
        for(const auto& image:scene.images){TerrainCell cell;cell.x=10+index;cell.y=10-index;cell.level=scene.level;cell.cellIndex512=cell.y*512+cell.x;
            oracleDraw(expected,scene.center,cell,camera,image,colors);require(drawTmpTileImageZBufferedToLayer(material,material.terrainDepthWords,scene.center,cell,camera,image,tags,cell.cellIndex512),"terrain raw material raster");++index;}
        material.built=true;show(material,expected,camera,palette,scene.label,160,120,false);
    }
    {auto material=emptyLayer(160,120),expected=material;RuntimeMapCamera camera;TerrainCell cell;cell.x=cell.y=10;cell.cellIndex512=5130;const POINT center{80,65};
        drawTmpTileImageZBufferedToLayer(material,material.terrainDepthWords,center,cell,camera,nearImage,tags,5130);oracleDraw(expected,center,cell,camera,nearImage,colors);
        drawDiamondToLayer(material,center,60,30,RGB(1,2,3),5131);drawDiamondToLayer(expected,center,60,30,RGB(1,2,3),5131);
        drawTmpTileImageZBufferedToLayer(material,material.terrainDepthWords,center,cell,camera,farImage,tags,5132);cell.cellIndex512=5132;oracleDraw(expected,center,cell,camera,farImage,colors);
        putLayerPixel(material,80,65,tags[1],-1,5133);putLayerPixel(expected,80,65,colors[1],-1,5133);material.built=true;
        show(material,expected,camera,palette,"fallback-diamond-bridge-surface-owner",160,120,false);}
    auto context=assets;context.mapFacts=nullptr;context.useTileTextures=true;context.stableRenderConfiguration=true;RuntimeMapRenderCache cache;
    require(context.rules&&context.theater&&ensureRuntimePalette(context,cache)&&cache.palette.size()>=256,"terrain actual original resources");
    RuntimeMapCamera camera;camera.initialized=true;int extraTile=-1,extraSub=-1,rampTile=-1,rampSub=-1;const int limit=static_cast<int>(std::min<std::size_t>(512,context.theater->getTotalTileSlots()));
    for(int tile=0;tile<limit&&(extraTile<0||rampTile<0);++tile){const auto* file=loadRuntimeTerrainTileForIndex(tile,context,cache);if(!file)continue;
        for(std::size_t sub=0;sub<file->images.size();++sub){const auto& image=file->images[sub];if(!image.present||!nativeEligible(image,camera))continue;
            if(extraTile<0&&image.hasExtra){extraTile=tile;extraSub=static_cast<int>(sub);}if(rampTile<0&&image.rampType){rampTile=tile;rampSub=static_cast<int>(sub);}}}
    require(extraTile>=0&&rampTile>=0,"original native TMP extra and ramp assets required, no skips");
    IniFile ini;MapDatabase map;GameWorld world;require(ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\nLocalSize=0,0,32,32\n")&&map.loadFromIni(ini)&&world.loadFromMap(map,context.rules),"terrain local original map fixture");
    world.accessTerrainCells().clear();for(int y=4;y<12;++y)for(int x=12;x<20;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.level=(x+y)%3;
        c.tileIndex=(x+y)%3==0?extraTile:(x+y)%3==1?rampTile:0;c.subTileIndex=c.tileIndex==extraTile?extraSub:c.tileIndex==rampTile?rampSub:0;world.accessTerrainCells().push_back(c);}
    for(int pass=0;pass<4;++pass){camera.worldOffsetX=pass<2?20+pass*20:380;camera.worldOffsetY=pass<2?20+pass*20:300;const int width=pass==3?540:360,height=pass==3?330:240;
        std::vector<std::uint32_t> input(static_cast<std::size_t>(width)*height);for(std::size_t i=0;i<input.size();++i)input[i]=0xa3152729U^static_cast<std::uint32_t>(i*1949U);gpu.beginTerrain(width,height,input);context.surfacePixels=nullptr;context.surfaceWidth=width;context.surfaceHeight=height;
        {RuntimeGpuWorldScope scope(gpu.drawDc());require(buildTerrainLayer(world,context,camera,width,height,cache),"stock GPU material cache production");}
        require(cache.terrainLayer.key.find("|gpu-material=1")!=std::string::npos,"stock actual material cache mode");
        require(cache.terrainLayerCacheHits==(pass>=1?1U:0U)&&cache.terrainLayerCacheMisses==static_cast<std::uint64_t>(pass==0?1:pass),"stock material cache hit/movement/resize lifecycle");
        auto expected=emptyLayer(cache.terrainLayer.width,cache.terrainLayer.height);expected.originX=cache.terrainLayer.originX;expected.originY=cache.terrainLayer.originY;
        std::array<std::uint32_t,256> stockColors{};for(std::size_t i=0;i<256;++i)stockColors[i]=packRuntimeDibColor(cache.palette[i]);
        for(const auto& c:world.getTerrainCells()){bool empty=false;const auto* image=resolveTerrainVisualCellImage(c,context,cache,&empty);if(empty)continue;require(image!=nullptr,"stock selected TMP source required");
            const POINT center{(c.x-c.y)*30-expected.originX,(c.x+c.y-c.level)*15-expected.originY};oracleDraw(expected,center,c,camera,*image,stockColors);report.stockNativeDraws+=nativeEligible(*image,camera);report.stockExtraDraws+=image->hasExtra&&nativeEligible(*image,camera);}
        show(cache.terrainLayer,expected,camera,cache.palette,"stock-native-extra-ramp-pass"+std::to_string(pass),width,height,true);
    }
    require(report.stockNativeDraws>0&&report.stockExtraDraws>0&&cases==17,"all GPU terrain cases required");gpu.close();
    std::cout<<"S41 GPU terrain contract passed driver="<<(warp?"warp":"hardware")<<" cases="<<cases<<" checkedGeometryPixels="<<report.checkedPixels<<" stockNativeDraws="<<report.stockNativeDraws<<" stockExtraDraws="<<report.stockExtraDraws<<" extraTile="<<extraTile<<" rampTile="<<rampTile<<" CPUauthority=original fullBGRA=exact noCpuWorldImage=1\n";return true;
}catch(const std::exception& e){error=e.what();return false;}
