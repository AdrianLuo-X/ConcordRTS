// Finite stock-asset contract. No rule/resource overrides; no save/timing suite.
bool runRuntimeBuildupTheaterSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& why){error="Buildup theater: "+why;return false;};
    const char* env=std::getenv("CONCORD_BUILDUP_THEATER_OUTPUT");
    if(!env||!*env)return fail("CONCORD_BUILDUP_THEATER_OUTPUT is required");
    const std::string root=env;
    std::filesystem::create_directories(root);
    std::ofstream rows(root+"/frames.csv"), assets(root+"/assets.csv");
    rows<<"theater,type,phase,cache_pass,simulation_tick,remaining,total,expected_frame,actual_frame,actual_raw_frames,actual_resolved,actual_source,actual_fingerprint,expected_source,source_bytes,source_fnv64,decoded_pixel_fnv64,screen_fnv64,drawn,colored_pixels\n";
    assets<<"theater,type,role,expected_file,expected_source,bytes,fnv64,raw_frames,width,height\n";
    if(!rows||!assets)return fail("output CSV open failed");
    const auto hash=[](const auto& bytes){std::uint64_t h=14695981039346656037ULL;
        const auto* p=reinterpret_cast<const unsigned char*>(bytes.data());
        for(std::size_t i=0;i<bytes.size()*sizeof(typename std::decay_t<decltype(bytes)>::value_type);++i){h^=p[i];h*=1099511628211ULL;}return h;};
    const auto norm=[](std::string p){std::replace(p.begin(),p.end(),'\\','/');return toLowerAsciiLocal(p);};
    const auto quote=[](const std::string& s){std::string v="\"";for(char c:s){if(c=='\"')v+='\"';v+=c;}return v+'\"';};
    struct Stock { const char* type; const char* body[2]; const char* bodyMix[2];
        const char* mk[2]; const char* mkMix[2]; };
    // Explicit original archive entries, not a reimplementation of candidate resolution.
    const std::array<Stock,5> stock{{
        {"GAPOWR",{"ggpowr.shp","gapowr.shp"},{"ra2/generic.mix","ra2/snow.mix"},
            {"gtpowrmk.shp","gapowrmk.shp"},{"ra2/isotemp.mix","ra2/isosnow.mix"}},
        {"NAPOWR",{"ngpowr.shp","napowr.shp"},{"ra2/generic.mix","ra2/snow.mix"},
            {"ntpowrmk.shp","napowrmk.shp"},{"ra2/isotemp.mix","ra2/isosnow.mix"}},
        {"YAPOWR",{"ygpowr.shp","yapowr.shp"},{"ra2md/genermd.mix","ra2md/snowmd.mix"},
            {"ygpowrmk.shp","yapowrmk.shp"},{"ra2md/genermd.mix","ra2md/snowmd.mix"}},
        {"GAWEAP",{"ggweap.shp","gaweap.shp"},{"ra2/generic.mix","ra2/snow.mix"},
            {"gtweapmk.shp","gaweapmk.shp"},{"ra2/isotemp.mix","ra2/isosnow.mix"}},
        {"GACNST",{"ggcnst.shp","gacnst.shp"},{"ra2/generic.mix","ra2/snow.mix"},
            {"gtcnstmk.shp","gacnstmk.shp"},{"ra2/isotemp.mix","ra2/isosnow.mix"}}
    }};
    struct Reference { std::string archive,file,source;std::vector<std::uint8_t> bytes;int frames=0,width=0,height=0; };
    std::array<std::unique_ptr<RuntimeWindowState>,2> states;
    std::array<std::array<Reference,5>,2> bodyRefs,mkRefs;
    auto referenceCache=std::make_unique<RuntimeMapRenderCache>();
    const auto readReference=[&](Reference& ref,const std::string& archive,const std::string& file)->bool{
        ref.archive=archive;ref.file=file;ref.source=archive+"!"+file;
        MixArchive mix;if(!mix.loadFromFile(archive)||!mix.isIndexReadable())return fail("reference MIX "+archive);
        const auto* entry=mix.findEntryByName(file);
        if(!entry||!mix.readEntryData(*entry,ref.bytes,error)||ref.bytes.size()<8)return fail("reference entry "+ref.source);
        const auto u16=[&](std::size_t i){return int(ref.bytes[i])|(int(ref.bytes[i+1])<<8);};
        ref.width=u16(2);ref.height=u16(4);ref.frames=u16(6);
        return ref.frames>0&&ref.width>0&&ref.height>0?true:fail("reference SHP header "+ref.source);
    };
    const auto contextFor=[&](RuntimeWindowState& s,int tick){
        RuntimeMapRenderContext c;c.rules=&s.rules;c.art=&s.art;c.theater=&s.theater;c.mapFacts=&s.mapFacts;
        c.assetRootPath=runtimeAssetRootPath(s.config);c.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        c.palettePath=runtimePalettePath(s.config,s.report.map);c.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        c.fixedTickHz=s.config.fixedTickHz;c.animationTick=tick;return c;
    };
    for(int t=0;t<2;++t){
        states[t]=std::make_unique<RuntimeWindowState>();auto& s=*states[t];s.config=config;
        s.config.fixedTickHz=30;s.config.playerSlots.clear();s.config.multiplayerActive=false;s.config.computerAiEnabled=false;
        s.config.defaultMapPath=pathString(std::filesystem::path(config.localDataRootPath)/"resources"/"mix_exports"
            /(t?"mapsmd03/all07smd.map":"multimd/2peaks.map"));
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        if(!s.report.success)return fail("original startup "+s.config.defaultMapPath);
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=s.mapFactsLoaded=true;
        if(s.theater.getTileExtension()!=(t?".sno":".tem"))return fail("original map theater mismatch");
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.triggers.bootstrapComplete=true;s.visionEnabled=false;s.economy.owner="BuildupTheaterProbe";
        s.economy.credits=100000;s.economy.techLevel=10;initializeRuntimeLocalProductionLanes(s.economy);
        // Isolate stock construction from unrelated map actors; original terrain/MapFacts remain intact.
        s.world.accessObjects().clear();initializeRuntimeNavigation(s);
        const auto assetRoot=runtimeAssetRootPath(s.config);
        for(std::size_t i=0;i<stock.size();++i){
            if(!readReference(bodyRefs[t][i],pathString(std::filesystem::path(assetRoot)/stock[i].bodyMix[t]),stock[i].body[t])
                ||!readReference(mkRefs[t][i],pathString(std::filesystem::path(assetRoot)/stock[i].mkMix[t]),stock[i].mk[t]))return false;
            for(int role=0;role<2;++role){const auto& r=role?mkRefs[t][i]:bodyRefs[t][i];
                assets<<(t?"SNOW":"TEMPERATE")<<','<<stock[i].type<<','<<(role?"buildup":"body")<<','<<r.file<<','
                    <<quote(r.source)<<','<<r.bytes.size()<<','<<hash(r.bytes)<<','<<r.frames<<','<<r.width<<','<<r.height<<'\n';}
        }
    }
    constexpr int width=384,height=384;
    constexpr std::uint32_t background=0xff243039U;
    RuntimeMapCamera camera;camera.worldOffsetX=-width/2;camera.worldOffsetY=-288;
    std::array<std::array<WorldObjectInstance,5>,2> middleObjects;
    std::array<std::array<int,5>,2> middleTicks{};
    std::array<std::array<std::vector<std::uint32_t>,5>,2> middlePixels;
    int scenes=0,draws=0,roundtrips=0;
    const auto sameImage=[](const RuntimeShpFrameImage& a,const RuntimeShpFrameImage& b){
        return a.offsetX==b.offsetX&&a.offsetY==b.offsetY&&a.width==b.width&&a.height==b.height
            &&a.globalWidth==b.globalWidth&&a.globalHeight==b.globalHeight&&a.pixels==b.pixels;};
    const auto capture=[&](int t,std::size_t i,const WorldObjectInstance& live,int tick,const char* phase,
                           const char* pass,RuntimeMapRenderCache& cache,std::vector<std::uint32_t>& pixels)->bool{
        auto& s=*states[t];auto object=live;object.x=object.y=object.cell=0;clearRuntimeRenderPosition(object);
        auto c=contextFor(s,tick);pixels.assign(width*height,background);
        c.surfacePixels=pixels.data();c.surfaceWidth=width;c.surfaceHeight=height;
        const bool drawn=drawRuntimeObjectPreviewSprite(nullptr,RECT{0,0,width,height},object,c,camera,cache);
        const bool constructing=live.buildAnimTicksRemaining>0;
        const auto& ref=constructing?mkRefs[t][i]:bodyRefs[t][i];
        const int expectedFrame=constructing?runtimeBuildupFrameAtProgress(
            runtimeBuildupFrameCount(ref.frames,false,0),live.buildAnimTicksTotal,live.buildAnimTicksRemaining,s.config.fixedTickHz,false):-1;
        const auto& frames=constructing?cache.auxiliaryShpSpritesByImage:cache.objectShpSpritesByType;
        const RuntimeObjectShpSpriteCacheEntry* selected=nullptr;
        for(const auto& [key,e]:frames)if(e.loaded&&norm(e.sourcePath)==norm(ref.source)
            &&e.frameIndex>=0&&e.frameIndex<ref.frames/2&&(!constructing||e.frameIndex==expectedFrame)){selected=&e;break;}
        if(!selected)return fail(std::string(stock[i].type)+" "+(t?"SNOW":"TEMPERATE")+" "+phase+" selected source differs: expected "+ref.source);
        if(selected->frameCount!=ref.frames)return fail("actual original raw frame count differs");
        const auto& byteEntries=constructing?cache.auxiliaryShpBytesByImage:cache.objectShpBytesByImage;
        bool originalBytes=false;for(const auto& [key,e]:byteEntries)if(e.loaded&&norm(e.sourcePath)==norm(ref.source)&&e.bytes==ref.bytes)originalBytes=true;
        if(!originalBytes)return fail("actual cached source bytes differ from explicit original archive entry");
        RuntimeShpFrameImage expected;std::string expectedSource,decodeError;
        if(!loadRuntimeShpFrameFromMixArchive(ref.archive,*referenceCache,ref.file,selected->frameIndex,
                expected,expectedSource,decodeError)||norm(expectedSource)!=norm(ref.source)||!sameImage(selected->image,expected))
            return fail(std::string(stock[i].type)+" decoded original frame differs: "+decodeError);
        if(selected->hasShadowFrame){RuntimeShpFrameImage shadow;std::string source;
            if(!loadRuntimeShpFrameFromMixArchive(ref.archive,*referenceCache,ref.file,selected->shadowFrameIndex,
                    shadow,source,decodeError)||!sameImage(selected->shadowImage,shadow))return fail("original shadow frame differs");}
        const auto colored=std::count_if(pixels.begin(),pixels.end(),[&](auto p){return p!=background;});
        const bool referenceHasPixels=std::any_of(expected.pixels.begin(),expected.pixels.end(),[](auto p){return p!=0;});
        if(referenceHasPixels&&(!drawn||colored==0))return fail(std::string(stock[i].type)+" nonempty original frame did not draw: "+phase);
        rows<<(t?"SNOW":"TEMPERATE")<<','<<stock[i].type<<','<<phase<<','<<pass<<','<<tick<<','
            <<live.buildAnimTicksRemaining<<','<<live.buildAnimTicksTotal<<','<<expectedFrame<<','<<selected->frameIndex<<','<<selected->frameCount<<','
            <<quote(selected->resolvedImageId)<<','<<quote(selected->sourcePath)<<','<<selected->sourceFingerprint<<','
            <<quote(ref.source)<<','<<ref.bytes.size()<<','<<hash(ref.bytes)<<','<<hash(selected->image.pixels)<<','<<hash(pixels)<<','
            <<drawn<<','<<colored<<'\n';++draws;return true;
    };
    for(int t=0;t<2;++t)for(std::size_t i=0;i<stock.size();++i){
        auto& s=*states[t];const auto* type=findRuntimeBuildingTypeById(s.rules,stock[i].type);
        const auto* art=s.art.findArtType(stock[i].type);
        if(!type||!art||!art->newTheater||art->buildup.empty()||s.art.findArtType(art->buildup))return fail("stock parent-only buildup ART contract");
        std::optional<RuntimeCellPoint> point;
        RuntimeBuildCatalogItem item;item.kind=RuntimeBuildKind::Building;item.typeId=stock[i].type;
        for(const auto& cell:s.world.getTerrainCells())if(runtimeCellCanPlaceBuilding(s,item,{cell.x,cell.y},-1,nullptr,s.economy.owner)){
            point=RuntimeCellPoint{cell.x,cell.y};break;}
        if(!point)return fail(std::string(stock[i].type)+" no original map legal foundation");
        auto* placed=addRuntimeObjectInstanceForOwner(s,s.economy.owner,RuntimeBuildKind::Building,stock[i].type,*point);
        if(!placed)return fail("native building creation");const int id=placed->instanceId;
        std::string timingSource;const auto timing=runtimeStructureBuildupTiming(s,*placed,&timingSource);
        if(norm(timingSource)!=norm(mkRefs[t][i].source)||timing.fixedTicks<=1||placed->buildAnimTicksRemaining!=timing.fixedTicks)
            return fail(std::string(stock[i].type)+" native timing source/timer differs");
        std::vector<std::uint32_t> sheet(width*4*height,background);
        int current=0,column=0;
        // First natural stage, midpoint, final construction stage, completed body.
        // Authored empty frames retain their source contract rather than inventing pixels.
        const std::array<int,4> sample{{std::max(1,timing.fixedTicks/std::max(1,timing.frames)),timing.fixedTicks/2,timing.fixedTicks-1,timing.fixedTicks}};
        for(int elapsed:sample){
            while(current<elapsed){++s.tick;advanceRuntimeBuildAnimations(s);++current;}
            const auto* live=findObjectById(s,id);if(!live||!live->alive)return fail("native construction actor lost");
            if((elapsed==timing.fixedTicks)!=(live->buildAnimTicksRemaining==0))return fail("natural construction completion differs");
            const char* phase=column==0?"first":column==1?"middle":column==2?"last":"body";
            auto cache=std::make_unique<RuntimeMapRenderCache>();std::vector<std::uint32_t> cold,warm;
            if(!capture(t,i,*live,s.tick,phase,"cold",*cache,cold)||!capture(t,i,*live,s.tick,phase,"warm",*cache,warm))return false;
            if(cold!=warm)return fail("cold/warm whole preview pixels differ");
            for(int y=0;y<height;++y)std::copy_n(cold.data()+y*width,width,sheet.data()+y*width*4+column*width);
            if(column==1){middleObjects[t][i]=*live;middleTicks[t][i]=s.tick;middlePixels[t][i]=cold;}++column;
        }
        if(!writeRuntimeSmokeBmp(root+"/"+(t?"snow-":"temperate-")+stock[i].type+".bmp",sheet.data(),width*4,height,error))return fail(error);
        if(i==0&&!drawRuntimeStateOffscreenAtCell(s,*point,640,480,error,root+"/"+(t?"snow-map.bmp":"temperate-map.bmp"),false,false))return fail(error);
        ++scenes;
    }
    auto shared=std::make_unique<RuntimeMapRenderCache>();
    for(int t:{0,1,0})for(std::size_t i=0;i<stock.size();++i){std::vector<std::uint32_t> p;
        if(!capture(t,i,middleObjects[t][i],middleTicks[t][i],"middle","shared-theater-roundtrip",*shared,p))return false;
        if(p!=middlePixels[t][i])return fail("shared-cache theater roundtrip whole pixels differ");++roundtrips;}
    // A plain auxiliary request has no parent and must retain its original direct GA source in both theaters.
    auto plain=std::make_unique<RuntimeMapRenderCache>();RuntimeShpFrameImage plainFirst;
    std::string plainSourceFirst,plainResolvedFirst;
    for(int t:{0,1,0}){auto c=contextFor(*states[t],0);RuntimeShpFrameImage actual,expected;
        std::string resolved,source,why,refSource;int rawFrames=0;
        if(!loadRuntimeAuxiliaryShpFrameImage(c,*plain,"GAPOWRMK",7,actual,resolved,source,why,false,&rawFrames)
            ||!loadRuntimeShpFrameFromMixArchive(mkRefs[1][0].archive,*referenceCache,"gapowrmk.shp",7,expected,refSource,why)
            ||norm(source)!=norm(refSource)||!sameImage(actual,expected)||rawFrames!=mkRefs[1][0].frames)return fail("plain auxiliary default changed: "+why);
        if(plainSourceFirst.empty()){plainFirst=actual;plainSourceFirst=source;plainResolvedFirst=resolved;}
        else if(norm(source)!=norm(plainSourceFirst)||resolved!=plainResolvedFirst||!sameImage(actual,plainFirst))return fail("plain auxiliary theater contamination");
        rows<<(t?"SNOW":"TEMPERATE")<<",GAPOWRMK,plain-negative,shared,0,0,0,7,7,"<<rawFrames<<','<<quote(resolved)<<','<<quote(source)
            <<",0,"<<quote(refSource)<<','<<mkRefs[1][0].bytes.size()<<','<<hash(mkRefs[1][0].bytes)<<','<<hash(actual.pixels)<<",0,0,0\n";
    }
    rows.flush();assets.flush();if(!rows||!assets)return fail("output CSV write failed");
    std::cout<<"Buildup theater passed: stockScenes="<<scenes<<", actualPreviewDraws="<<draws
        <<", sharedTheaterRoundtrip="<<roundtrips<<", plainAuxiliaryNegative=3, stockSheets=10, originalMapFrames=2.\n";
    return scenes==10&&draws==95&&roundtrips==15;
}
