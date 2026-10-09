bool runRuntimeSuperWeaponAnimationRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& hosts,
    const std::vector<std::vector<SpecialMechanismRenderSnapshot>>& phases,std::string& error){
    const auto fail=[&](const std::string& why){error="Superweapon pixels: "+why; return false;};
    if(hosts.size()!=6 || phases.size()!=6) return fail("missing six original building clocks");
    constexpr int width=280,height=300,columns=5;
    constexpr std::uint32_t background=0x00667549;
    std::vector<std::uint32_t> pixels(width*height,background),sheet(width*height*columns*6,background);
    auto cache=std::make_unique<RuntimeMapRenderCache>(); auto probe=context;
    probe.vision=nullptr; probe.surfacePixels=pixels.data(); probe.surfaceWidth=width; probe.surfaceHeight=height;
    probe.fixedTickHz=30; const RECT viewport{0,0,width,height};
    int checked=0;
    for(int row=0;row<6;++row){
        for(bool damaged:{false,true}){
            auto host=hosts[row]; if(damaged) host.health=std::max(1,host.health/3);
            RuntimeMapCamera camera; camera.initialized=true;
            const auto origin=projectCellXY(host.x,host.y,0,camera);
            camera.worldOffsetX=origin.x-width/2; camera.worldOffsetY=origin.y-160;
            const auto center=runtimeObjectBodyAnchorForRender(host,viewport,probe,camera);
            const auto* art=findArtTypeForObject(host,probe); if(!art) return fail(host.type+" body art missing");
            for(int column=0;column<columns;++column){
                std::vector<SpecialMechanismRenderSnapshot> poses{phases[row][column]}; probe.specialMechanismSnapshots=&poses;
                const auto selected=runtimeSuperWeaponBuildingLayer(host,probe,*art);
                if(!selected.layer) return fail(host.type+" original state layer missing");
                const auto image=damaged && !selected.layer->damagedImage.empty() ? selected.layer->damagedImage : selected.layer->image;
                const auto* frameArt=probe.art->findArtType(image);
                std::fill(pixels.begin(),pixels.end(),background);
                if(!tryDrawObjectShpSprite(nullptr,viewport,host,center,probe,camera,*cache)) return fail(host.type+" original base missing");
                RuntimeStructureAnimationStats stats;
                if(drawRuntimeStructureActiveAnimations(nullptr,viewport,host,center,probe,camera,*cache,&stats,
                        false,nullptr,nullptr,2)<=0 || stats.missingLayers>0)
                    return fail(host.type+" original state animation did not draw");
                if(!damaged) for(int y=0;y<height;++y)
                    std::copy_n(pixels.begin()+y*width,width,sheet.begin()+(row*height+y)*width*columns+column*width);
                if(column==3 && frameArt && !frameArt->hasAnimationEnd
                    && frameArt->animationLoopEnd-frameArt->animationLoopStart>1){
                    const int first=runtimeSuperWeaponBuildingFrame(frameArt,selected.ageTicks,30);
                    poses.front().progress+=runtimeSuperWeaponBuildingFrameDelay(frameArt,30);
                    const auto next=runtimeSuperWeaponBuildingLayer(host,probe,*art);
                    if(runtimeSuperWeaponBuildingFrame(frameArt,next.ageTicks,30)==first)
                        return fail(host.type+" ready loop did not advance");
                    const auto sameTime=runtimeSuperWeaponBuildingFrame(frameArt,next.ageTicks*2,60);
                    if(sameTime!=runtimeSuperWeaponBuildingFrame(frameArt,next.ageTicks,30))
                        return fail(host.type+" native frame changed at 60 Hz");
                }
                ++checked;
            }
        }
        std::cout<<"Superweapon pixels "<<hosts[row].type<<" original healthy/damaged phases=ok\n";
    }
    if(const char* root=std::getenv("CONCORD_SUPERWEAPON_ANIMATION_DIR"))
        if(*root && !writeRuntimeBgraBmp(std::string(root)+"/superweapon-buildings.bmp",width*columns,height*6,sheet)) return fail("preview write failed");
    std::cout<<"Superweapon original layer checks="<<checked<<"\n";
    return true;
}
