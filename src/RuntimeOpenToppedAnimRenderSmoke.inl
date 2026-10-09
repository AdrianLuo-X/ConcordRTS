bool runRuntimeOpenToppedAnimRenderCheck(const RuntimeMapRenderContext& supplied,
    const std::vector<WorldObjectInstance>& hosts,const std::vector<RuntimeImpactRenderActor>& samples,
    const std::string& output,int& checks,std::string& error) {
    const auto fail=[&](const std::string& why){error="OpenTopped render: "+why;return false;};
    if(samples.size()!=24 || hosts.size()!=samples.size())return fail("missing direction/control samples");
    auto cache=std::make_unique<RuntimeMapRenderCache>();RuntimeMapRenderContext context=supplied;
    context.vision=nullptr;context.specialMechanismSnapshots=nullptr;context.fixedTickHz=30;context.animationTick=0;
    std::ofstream records(output+"/transport-frames.csv");records<<"sample,anim,frame,opacity,visible_pixels,source\n";
    constexpr std::uint32_t background=0x00242c30U;constexpr int width=128,height=128;
    std::vector<std::uint32_t> pixels(width*height,background);
    for(std::size_t index=0;index<samples.size();++index) {
        auto actor=samples[index];GameWorld world;world.accessObjects().push_back(hosts[index]);
        const auto* first=loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId,0,context,*cache,false);
        const auto* art=context.art->findArtType(actor.animId);const auto* palette=runtimeAnimPaletteForRender(context,*cache);
        if(!first || !art || !palette || first->frameCount<2)return fail("original SHP/ART/palette unavailable");
        const int count=first->frameCount;
        for(int frame=0;frame<count;++frame) {
            actor.frameIndex=frame;const auto* sprite=loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId,frame,context,*cache,false);
            if(!sprite || sprite->frameIndex!=frame)return fail(actor.animId+" frame missing/wrapped: "+std::to_string(frame));const auto& image=sprite->image;
            const int opacity=!art->translucent?255:frame*5<=count?255:frame*5<=count*2?191:frame*5<=count*3?128:64;
            context.surfacePixels=pixels.data();context.surfaceWidth=width;context.surfaceHeight=height;std::fill(pixels.begin(),pixels.end(),background);
            RuntimeMapCamera camera;camera.initialized=true;const auto point=projectCellXY(actor.x,actor.y,actor.level,camera);
            camera.worldOffsetX=point.x-width/2;camera.worldOffsetY=point.y-height/2;
            RuntimeMapRenderStats stats;drawRuntimeImpactActor(nullptr,RECT{0,0,width,height},world,context,camera,*cache,actor,stats);
            // This probe uses the native 60x30 scale: the fixed and scalable
            // parts sum to the supplied actor offsets. Replay those inputs in
            // the independent SHP oracle as well as the production draw.
            const int left=width/2+actor.screenOffsetX-image.globalWidth/2+image.offsetX,
                top=height/2+actor.screenOffsetY-image.globalHeight/2+image.offsetY;int visible=0;
            std::vector<std::uint32_t> expected(width*height,background);
            for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
                const auto pixel=image.pixels[y*image.width+x];if(!pixel)continue;
                if(left+x<0 || top+y<0 || left+x>=width || top+y>=height)return fail("SHP exceeds test canvas");
                const auto& color=(*palette)[pixel];const auto blend=[&](int c,int b){return(c*opacity+b*(255-opacity)+127)/255;};
                expected[(top+y)*width+left+x]=(blend(color.r,0x24)<<16)|(blend(color.g,0x2c)<<8)|blend(color.b,0x30);++visible;
            }
            if(pixels!=expected || (visible>0 && stats.impactActorDrawn!=1))return fail(actor.animId+" pixel/alpha/anchor mismatch frame="+std::to_string(frame));
            records<<index<<','<<actor.animId<<','<<frame<<','<<opacity<<','<<visible<<','<<sprite->sourcePath<<'\n';++checks;
        }
    }
    // Three sheets: Initiate fallback, Virus fallback, ordinary GI directional Anim.
    constexpr int panelWidth=240,panelHeight=180;
    std::vector<std::uint32_t> panel(panelWidth*panelHeight,background),sheet(panelWidth*8*panelHeight*3,background);
    for(int type=0;type<3;++type) {
        std::fill(sheet.begin(),sheet.end(),background);
        for(int facing=0;facing<8;++facing)for(int phase=0;phase<3;++phase) {
            const int index=type*8+facing;const auto& host=hosts[index];auto actor=samples[index];
            GameWorld world;world.accessObjects().push_back(host);std::fill(panel.begin(),panel.end(),background);
            context.surfacePixels=panel.data();context.surfaceWidth=panelWidth;context.surfaceHeight=panelHeight;
            RuntimeMapCamera camera;camera.initialized=true;const POINT center{panelWidth/2,panelHeight/2+20};
            const auto origin=projectCellXY(host.x,host.y,0,camera);camera.worldOffsetX=origin.x-center.x;camera.worldOffsetY=origin.y-center.y;
            if(!tryDrawVoxelSprite(nullptr,RECT{0,0,panelWidth,panelHeight},host,center,context,camera,*cache))return fail("original BFRT voxel missing");
            const auto* first=loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId,0,context,*cache,false);actor.frameIndex=phase*(first->frameCount-1)/2;
            RuntimeMapRenderStats stats;drawRuntimeImpactActor(nullptr,RECT{0,0,panelWidth,panelHeight},world,context,camera,*cache,actor,stats);
            for(int y=0;y<panelHeight;++y)std::copy_n(panel.data()+y*panelWidth,panelWidth,
                sheet.data()+(phase*panelHeight+y)*panelWidth*8+facing*panelWidth);
        }
        const char* names[]{"INIT","VIRUS","E1"};
        if(!writeRuntimeBgraBmp(output+"/BFRT-"+names[type]+"-muzzles.bmp",panelWidth*8,panelHeight*3,sheet))return fail("composite write");
    }
    return true;
}
