bool runRuntimeOccupantAnimRenderCheck(const RuntimeMapRenderContext& supplied,
    const WorldObjectInstance& host, const std::vector<RuntimeImpactRenderActor>& samples,
    const std::string& output, int& checks, std::string& error) {
    const auto fail=[&](const std::string& why){error="Occupant render: "+why;return false;};
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapRenderContext context=supplied; context.vision=nullptr; context.specialMechanismSnapshots=nullptr;
    context.fixedTickHz=30; context.animationTick=0;
    std::ofstream records(output+"/"+host.type+"-frames.csv");
    records<<"anim,window,frame,opacity,visible_pixels,source\n";
    constexpr std::uint32_t background=0x00242c30U;
    constexpr int width=128,height=128;
    std::vector<std::uint32_t> pixels(width*height,background);
    GameWorld world; world.accessObjects().push_back(host);
    const std::array<std::string,3> animations{"UCFLASH","UCCONS","UCINIT"};
    for (std::size_t index=0;index<samples.size();++index) {
        auto actor=samples[index];
        const auto* first=loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId,0,context,*cache,false);
        const auto* art=context.art->findArtType(actor.animId);
        const auto* palette=runtimeAnimPaletteForRender(context,*cache);
        if (!first || !art || !art->translucent || !palette || first->frameCount<2) return fail("original window SHP/ART/palette");
        const int count=first->frameCount;
        for (int frame=0;frame<count;++frame) {
            actor.frameIndex=frame;
            const auto* sprite=loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId,frame,context,*cache,false);
            if (!sprite || sprite->frameIndex!=frame) return fail("original frame wrapped/missing");
            const auto& image=sprite->image;
            // The original renderer's thresholds are >20%, >40%, >60%.
            const int opacity=frame*5<=count?255:frame*5<=count*2?191:frame*5<=count*3?128:64;
            context.surfacePixels=pixels.data(); context.surfaceWidth=width; context.surfaceHeight=height;
            std::fill(pixels.begin(),pixels.end(),background);
            RuntimeMapCamera camera; camera.initialized=true;
            const auto point=projectCellXY(actor.x,actor.y,actor.level,camera);
            camera.worldOffsetX=point.x-width/2; camera.worldOffsetY=point.y-height/2;
            RuntimeMapRenderStats stats;
            drawRuntimeImpactActor(nullptr,RECT{0,0,width,height},world,context,camera,*cache,actor,stats);
            int visible=0;
            const int left=width/2-image.globalWidth/2+image.offsetX;
            const int top=height/2-image.globalHeight/2+image.offsetY;
            for (int y=0;y<image.height;++y) for (int x=0;x<image.width;++x) {
                const auto pixel=image.pixels[y*image.width+x]; if (!pixel) continue;
                if (left+x<0 || top+y<0 || left+x>=width || top+y>=height) return fail("window SHP exceeds isolated canvas");
                const auto& color=(*palette)[pixel];
                const auto blend=[&](int channel,int back){return (channel*opacity+back*(255-opacity)+127)/255;};
                const std::uint32_t expected=(blend(color.r,0x24)<<16)|(blend(color.g,0x2c)<<8)|blend(color.b,0x30);
                if (pixels[(top+y)*width+left+x]!=expected) return fail(actor.animId+" frame="+std::to_string(frame)
                    +" original pixel/alpha/anchor mismatch");
                ++visible;
            }
            if (visible>0 && stats.impactActorDrawn!=1) return fail("nonempty muzzle not drawn once");
            records<<actor.animId<<','<<index%(samples.size()/3)<<','<<frame<<','<<opacity<<','<<visible<<','<<sprite->sourcePath<<'\n';
            ++checks;
        }
    }
    // Composite the actual building and all authored windows; columns are the
    // three factions, rows are first/middle/final frames. No synthetic flames.
    constexpr int panelWidth=420,panelHeight=360;
    std::vector<std::uint32_t> panel(panelWidth*panelHeight,background);
    std::vector<std::uint32_t> sheet(panelWidth*3*panelHeight*3,background);
    for (int faction=0;faction<3;++faction) for (int phase=0;phase<3;++phase) {
        std::fill(panel.begin(),panel.end(),background);
        context.surfacePixels=panel.data(); context.surfaceWidth=panelWidth; context.surfaceHeight=panelHeight;
        RuntimeMapCamera camera; camera.initialized=true;
        const POINT center{panelWidth/2,100};
        const auto origin=projectCellXY(host.x,host.y,0,camera);
        camera.worldOffsetX=origin.x-center.x; camera.worldOffsetY=origin.y-center.y;
        if (!tryDrawObjectShpSprite(nullptr,RECT{0,0,panelWidth,panelHeight},host,center,context,camera,*cache))
            return fail(host.type+" occupied building body missing");
        for (auto actor:samples) {
            if (actor.animId!=animations[faction]) continue;
            const auto* first=loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId,0,context,*cache,false);
            actor.frameIndex=phase*(first->frameCount-1)/2;
            RuntimeMapRenderStats stats;
            drawRuntimeImpactActor(nullptr,RECT{0,0,panelWidth,panelHeight},world,context,camera,*cache,actor,stats);
        }
        for (int y=0;y<panelHeight;++y) std::copy_n(panel.data()+y*panelWidth,panelWidth,
            sheet.data()+(phase*panelHeight+y)*panelWidth*3+faction*panelWidth);
    }
    if (!writeRuntimeBgraBmp(output+"/"+host.type+"-windows.bmp",panelWidth*3,panelHeight*3,sheet)) return fail("window composite write");
    return true;
}
