bool runRuntimeFlakHeightRenderContractCheck(const RuntimeMapRenderContext& context,
    const WorldObjectInstance& source,std::string& error){
    const auto fail=[&](const std::string& why){error="Flak height pixels: "+why; return false;};
    constexpr int width=160,height=150,columns=2;
    constexpr std::uint32_t background=0x00667549;
    std::vector<std::uint32_t> pixels(width*height,background),sheet(width*height*columns*8,background);
    auto cache=std::make_unique<RuntimeMapRenderCache>(); auto probe=context;
    RulesDatabase rules=*context.rules; probe.rules=&rules;
    auto* building=const_cast<BuildingType*>(rules.findBuildingType("NAFLAK"));
    if(!building) return fail("original flak rules missing");
    probe.vision=nullptr; probe.surfacePixels=pixels.data(); probe.surfaceWidth=width; probe.surfaceHeight=height;
    std::unordered_map<std::string,COLORREF> colors{{"russians",RGB(230,190,35)}};
    probe.ownerRemapColors=&colors;
    RuntimeMapCamera camera; camera.initialized=true;
    const auto origin=projectCellXY(source.x,source.y,source.level,camera);
    camera.worldOffsetX=origin.x-width/2; camera.worldOffsetY=origin.y-115;
    const RECT viewport{0,0,width,height};
    for(int row=0;row<8;++row){
        auto object=source; object.facing=object.turretFacing=row*32;
        const auto center=runtimeObjectBodyAnchorForRender(object,viewport,probe,camera);
        double oldY=0,newY=0; int oldCount=0,newCount=0;
        for(int column=0;column<2;++column){
            std::fill(pixels.begin(),pixels.end(),background);
            if(column==0) building->turretAnimY+=camera.tileHeight/2;
            if(!tryDrawRuntimeStructureTurretAnim(nullptr,viewport,object,center,1,1,probe,camera,*cache))
                return fail("original FLAKTUR voxel missing");
            for(int y=0;y<height;++y) for(int x=0;x<width;++x) if(pixels[y*width+x]!=background){
                if(column==0){oldY+=y; ++oldCount;}else{newY+=y; ++newCount;}
            }
            std::fill(pixels.begin(),pixels.end(),background);
            if(!tryDrawObjectShpSprite(nullptr,viewport,object,center,probe,camera,*cache)
                || !tryDrawRuntimeStructureTurretAnim(nullptr,viewport,object,center,1,1,probe,camera,*cache))
                return fail("complete base and turret missing");
            if(column==0) building->turretAnimY-=camera.tileHeight/2;
            for(int y=0;y<height;++y)
                std::copy_n(pixels.begin()+y*width,width,sheet.begin()+(row*height+y)*width*columns+column*width);
        }
        if(oldCount<100 || newCount!=oldCount || std::abs(oldY/oldCount-newY/newCount-15.0)>0.001)
            return fail("native turret was clipped or did not expose the full pedestal");
    }
    if(const char* folder=std::getenv("CONCORD_FINAL_DEFENSE_DIR"))
        if(*folder && !writeRuntimeBgraBmp(std::string(folder)+"/flak-height.bmp",width*columns,height*8,sheet)) return fail("preview write failed");
    std::cout<<"Flak height pixels: 8 original facings, full pedestal, stationary base and unclipped turret=ok\n";
    return true;
}
