bool runRuntimeGrindingAnimationRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& hosts,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& poses,std::string& error){
    const auto fail=[&](const std::string& why){error="Grinder pixels: "+why; return false;};
    if(hosts.size()!=2 || poses.size()!=2) return fail("missing normal/damaged combat cycles");
    constexpr int width=260,height=280,columns=4;
    constexpr std::uint32_t background=0x00667549;
    std::vector<std::uint32_t> pixels(width*height,background),sheet(width*columns*height*2,background);
    auto cache=std::make_unique<RuntimeMapRenderCache>(); auto probe=context;
    probe.surfacePixels=pixels.data(); probe.surfaceWidth=width; probe.surfaceHeight=height;
    probe.fixedTickHz=30; probe.animationTick=0; probe.vision=nullptr;
    std::unordered_map<std::string,COLORREF> colors{{"yuricountry",RGB(200,45,200)}};
    probe.ownerRemapColors=&colors;
    const RECT viewport{0,0,width,height};
    const auto copy=[&](int row,int column){for(int y=0;y<height;++y)
        std::copy_n(pixels.begin()+y*width,width,sheet.begin()+(row*height+y)*width*columns+column*width);};
    for(int row=0;row<2;++row){
        const auto& host=hosts[row]; RuntimeMapCamera camera; camera.initialized=true;
        const auto origin=projectCellXY(host.x,host.y,host.level,camera);
        camera.worldOffsetX=origin.x-width/2; camera.worldOffsetY=origin.y-170;
        const auto center=runtimeObjectBodyAnchorForRender(host,viewport,probe,camera);
        const auto draw=[&](){
            std::fill(pixels.begin(),pixels.end(),background);
            if(!tryDrawObjectShpSprite(nullptr,viewport,host,center,probe,camera,*cache)) return false;
            return drawRuntimeStructureActiveAnimations(nullptr,viewport,host,center,probe,camera,*cache)>0;
        };
        probe.impactActors=nullptr;
        if(!draw()) return fail("original grinder model missing");
        const auto idle=pixels; copy(row,0);
        std::set<std::uint64_t> frameHashes;
        for(std::size_t index=0;index<poses[row].size();++index){
            std::vector<RuntimeImpactRenderActor> actors{poses[row][index]}; probe.impactActors=&actors;
            RuntimeStructureAnimationStats stats;
            std::fill(pixels.begin(),pixels.end(),background);
            const int count=drawRuntimeStructureActiveAnimations(nullptr,viewport,host,center,probe,camera,*cache,
                &stats,false,nullptr,nullptr,1);
            if(count!=1 || stats.specialLayers!=1 || stats.firstDrawnFrame!=actors.front().frameIndex
                || stats.firstSpecialSourcePath.empty()) return fail("cycle did not use the original authored frame");
            if(!draw()) return fail("building cycle failed to draw");
            std::uint64_t hash=1469598103934665603ULL;
            for(auto pixel:pixels){hash^=pixel; hash*=1099511628211ULL;}
            frameHashes.insert(hash);
            if(index==0) copy(row,1);
            if(index==poses[row].size()/2) copy(row,2);
            if(index+1==poses[row].size()) copy(row,3);
        }
        probe.impactActors=nullptr;
        if(!draw() || pixels!=idle || frameHashes.size()<12) return fail("grinder remained frozen or failed to return to idle");
        std::cout<<"Grinder pixels damaged="<<row<<" distinctFrames="<<frameHashes.size()<<" original/idle-return=ok\n";
    }
    if(const char* root=std::getenv("CONCORD_GRINDER_DIR"))
        if(*root && !writeRuntimeBgraBmp(std::string(root)+"/grinder-native.bmp",width*columns,height*2,sheet)) return fail("preview write failed");
    return true;
}
