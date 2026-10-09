bool runRuntimeAirbornePickingOrderSmokeProbe(RuntimeWindowState& live, std::string& error) {
    constexpr int width=320,height=240;
    const RECT viewport{0,0,width,height};
    const char* output=std::getenv("CONCORD_AIR_DAMAGE_SMOKE_OUTPUT");
    const std::filesystem::path root=output&&*output?output:
        "build/smoke/feature-set-19/02-air-damage-smoke/checks";
    std::filesystem::create_directories(root);
    std::ofstream rows(root/"airborne-picking-order.csv");
    rows<<"scene,expected_front,projected_first,projected_last,pixel_mismatch,overlap_pixels,pick_failures,cache_reused\n";
    std::vector<std::string> failures;
    const auto fail=[&](const std::string& detail){failures.push_back(detail);
        std::cout<<"Airborne picking FAIL "<<detail<<'\n';};
    const auto* type=findRuntimeUnitTypeById(live.rules,"SHAD");
    if(!rows||!type){error="Airborne picking: stock fixture or evidence unavailable";return false;}
    struct Surface {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
        Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            dc=CreateCompatibleDC(nullptr);void* raw=nullptr;
            if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
            pixels=static_cast<std::uint32_t*>(raw);if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
        ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } surface;
    if(!surface.dc||!surface.bitmap||!surface.pixels){error="Airborne picking: surface unavailable";return false;}
    int scenes=0,clicks=0,cacheHits=0;
    for(bool sameDepth:{false,true}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=live.config;s.config.playerSlots.clear();s.config.computerAiEnabled=s.config.multiplayerActive=false;
        s.rules=live.rules;s.art=live.art;s.theater=live.theater;s.report=live.report;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;s.tick=30;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        RuntimeHumanHouseAuthority authority;authority.owner="Americans";
        authority.canonicalOwner="americans";authority.localFacade=true;s.humanHouseAuthorities.push_back(authority);
        s.ownerRemapColors["Americans"]=RGB(50,130,230);s.ownerRemapColors["Russians"]=RGB(235,55,55);
        IniFile mapIni;MapDatabase map;
        if(!mapIni.loadFromString("[Header]\nWidth=16\nHeight=16\n[Map]\nTheater=TEMPERATE\nSize=0,0,16,16\nLocalSize=0,0,16,16\n")
            ||!map.loadFromIni(mapIni)||!s.world.loadFromMap(map,&s.rules)){
            error="Airborne picking: empty loaded world unavailable";return false;}
        // Independently chosen geometry: case 1 has greater X+Y but smaller
        // screen Y for 102; case 2 has equal X+Y and a higher body for 102.
        // The known visible order is 101 then 102 in both cases.
        for(int id:{101,102}) {
            WorldObjectInstance body;body.instanceId=id;body.type="SHAD";body.kind=WorldObjectKind::Unit;
            body.owner=id==101?"Americans":"Russians";body.alive=true;body.health=type->strength;
            body.mission="Guard";body.facing=64;body.subCell=-1;
            body.airborne=body.hasRenderPosition=body.renderMoveActive=true;
            body.renderX=id==101?7.0:sameDepth?7.25:7.7;
            body.renderY=id==101?7.0:sameDepth?6.75:7.7;
            body.renderLevel=id==101?3.0:sameDepth?3.5:4.5;
            body.x=static_cast<int>(std::lround(body.renderX));body.y=static_cast<int>(std::lround(body.renderY));
            body.cell=runtimeCellKey(body.x,body.y);body.level=body.flightLevel=static_cast<int>(std::floor(body.renderLevel));
            s.world.accessObjects().push_back(body);
        }
        initializeRuntimeNavigation(s);
        // Navigation intentionally discards orphaned interpolation fields.
        // Install a real paused Jumpjet flight authority after initialization;
        // production pose application supplies the continuous render fields.
        for(auto& body:s.world.accessObjects()) {
            auto& flight=runtimeJumpjetFlightFor(s,body);
            flight.active=true;flight.fromX=flight.fromY=7;flight.toX=8;
            flight.toY=body.instanceId==102&&sameDepth?6:8;
            flight.length=std::llround(std::sqrt(2.0)*runtimeJumpjetCellQ16);
            const double fraction=body.instanceId==101?0.0:sameDepth?0.25:0.7;
            flight.progress=std::llround(fraction*flight.length);
            flight.height=std::llround((body.instanceId==101?3.0:sameDepth?3.5:4.5)*runtimeJumpjetCellQ16);
            flight.lastTick=flight.verticalTick=s.tick;
            applyRuntimeJumpjetPose(s,body);
            const double poseTolerance=0.5/static_cast<double>(flight.length)
                +16.0*std::numeric_limits<double>::epsilon();
            if(!body.hasRenderPosition||!body.renderMoveActive
                ||std::abs(body.renderX-(7.0+fraction))>poseTolerance
                ||std::abs(body.renderY-(7.0+(sameDepth&&body.instanceId==102?-fraction:fraction)))>poseTolerance){
                error="Airborne picking: authoritative continuous flight pose missing";return false;}
        }
        s.camera.initialized=true;s.camera.tileWidth=60;s.camera.tileHeight=30;
        s.camera.worldOffsetX=-width/2;s.camera.worldOffsetY=0;
        RuntimeSnapshotActiveStateScope scope(s);
        // Let one-time renderer snapshot initialization settle before testing
        // true cache reuse at the same tick.
        (void)runtimeProjectedObjectsForViewport(s,viewport);
        (void)runtimeProjectedObjectsForViewport(s,viewport);
        GameWorld renderWorld=s.world;
        auto cache=std::make_unique<RuntimeMapRenderCache>();
        RuntimeMapRenderContext context;context.rules=&s.rules;context.art=&s.art;
        context.theater=&s.theater;context.ownerRemapColors=&s.ownerRemapColors;
        context.assetRootPath=runtimeAssetRootPath(s.config);
        context.palettePath=runtimePalettePath(s.config,s.report.map);
        context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        context.animationTick=s.tick;context.fixedTickHz=s.config.fixedTickHz;
        context.useTileTextures=context.shadowsEnabled=false;context.vision=nullptr;
        context.surfacePixels=surface.pixels;context.surfaceWidth=width;context.surfaceHeight=height;
        const auto render=[&](const std::vector<WorldObjectInstance>& objects,std::vector<std::uint32_t>& frame){
            renderWorld.accessObjects()=objects;auto camera=s.camera;
            const auto stats=drawRuntimeMap(surface.dc,viewport,renderWorld,context,camera,*cache);
            GdiFlush();frame.assign(surface.pixels,surface.pixels+width*height);return stats;
        };
        const auto check=[&](const std::string& label,int frontId,bool conflict){
            const int rearId=frontId==102?101:102;
            const auto& projection=runtimeProjectedObjectsForViewport(s,viewport);
            if(projection.size()!=2){fail(label+" projection count="+std::to_string(projection.size()));return;}
            const auto view=[&](int id)->const RuntimeProjectedObject*{
                for(const auto& p:projection)if(p.object&&p.object->instanceId==id)return &p;return nullptr;};
            const auto* rear=view(rearId);const auto* front=view(frontId);
            if(!rear||!front||!rear->airborneForeground||!front->airborneForeground){fail(label+" air projections missing");return;}
            if(conflict&&front->projectedY>=rear->projectedY)fail(label+" lacks projected-Y/depth conflict");
            const int first=projection.front().object->instanceId,last=projection.back().object->instanceId;
            if(first!=rearId||last!=frontId)fail(label+" projected order "+std::to_string(first)+","+std::to_string(last));
            const auto* cachedData=projection.data();const auto cachedSignature=s.projectedObjectsCacheSignature;
            const auto& cached=runtimeProjectedObjectsForViewport(s,viewport);
            const bool reused=cached.data()==cachedData&&s.projectedObjectsCacheSignature==cachedSignature;
            if(!reused)fail(label+" unchanged projection rebuilt");else ++cacheHits;
            std::vector<std::uint32_t> blank,rearPixels,frontPixels,actual,paused;
            const auto emptyStats=render({},blank);
            const auto rearStats=render({*rear->object},rearPixels),frontStats=render({*front->object},frontPixels);
            const auto fullStats=render(s.world.getObjects(),actual),pauseStats=render(s.world.getObjects(),paused);
            if(!emptyStats.rendered||!rearStats.rendered||!frontStats.rendered||!fullStats.rendered||!pauseStats.rendered
                ||fullStats.airborneForegroundBodiesDrawn!=2||actual!=paused)fail(label+" full renderer body count/pause");
            std::vector<std::uint32_t> expected(actual.size()),reversed(actual.size());
            int mismatch=0,overlap=0,pickFailures=0;
            for(std::size_t p=0;p<actual.size();++p){
                const auto bg=blank[p]&0xffffffU,a=rearPixels[p]&0xffffffU,b=frontPixels[p]&0xffffffU;
                // Full-health stock SHAD voxel pixels are opaque; compose their
                // separately rendered silhouettes in the explicit geometric
                // order. No production sort key/comparator supplies the oracle.
                expected[p]=b!=bg?frontPixels[p]:rearPixels[p];
                reversed[p]=a!=bg?rearPixels[p]:frontPixels[p];
                mismatch+=(actual[p]&0xffffffU)!=(expected[p]&0xffffffU);
                if(a==bg||b==bg||a==b)continue;
                ++overlap;const POINT point{static_cast<LONG>(p%width),static_cast<LONG>(p/width)};
                const auto* picked=findProjectedObjectAtPoint(cached,point);
                if(!pointInRectInclusive(rear->bounds,point)||!pointInRectInclusive(front->bounds,point)
                    ||!picked||!picked->object||picked->object->instanceId!=frontId)++pickFailures;
                ++clicks;
            }
            if(mismatch||!overlap||pickFailures)fail(label+" pixels="+std::to_string(mismatch)
                +" overlap="+std::to_string(overlap)+" picks="+std::to_string(pickFailures));
            std::vector<std::uint32_t> sheet(width*3*height);
            for(int row=0;row<height;++row)for(int column=0;column<3;++column){
                const auto& frame=column==0?actual:column==1?expected:reversed;
                std::copy_n(frame.begin()+row*width,width,sheet.begin()+row*width*3+column*width);}
            std::string imageError;
            if(!writeRuntimeSmokeBmp((root/("airborne-picking-"+label+".bmp")).string(),sheet.data(),width*3,height,imageError))
                fail(imageError);
            rows<<label<<','<<frontId<<','<<first<<','<<last<<','<<mismatch<<','<<overlap<<','<<pickFailures<<','<<reused<<'\n';
            std::cout<<"Airborne picking: "<<label<<" expectedFront="<<frontId<<" order="<<first<<','<<last
                <<" pixelMismatch="<<mismatch<<" overlap="<<overlap<<" pickFailures="<<pickFailures
                <<" cacheReused="<<reused<<'\n';++scenes;
        };
        const std::string prefix=sameDepth?"same-depth":"depth-height-conflict";
        check(prefix+"-forward",102,!sameDepth);
        const auto originalSignature=s.projectedObjectsCacheSignature;
        std::reverse(s.world.accessObjects().begin(),s.world.accessObjects().end());
        check(prefix+"-reverse",102,!sameDepth);
        if(s.projectedObjectsCacheSignature==originalSignature)fail(prefix+" insertion order did not invalidate cache");
        if(sameDepth){
            const auto before=s.projectedObjectsCacheSignature;
            for(auto& body:s.world.accessObjects()){
                s.jumpjetFlights.at(body.instanceId).height=
                    std::llround((body.instanceId==101?3.5:3.0)*runtimeJumpjetCellQ16);
                applyRuntimeJumpjetPose(s,body);}
            // Same positions, IDs, insertion order and tick; the newly higher
            // 101 is now visibly last. The cached ordering must be replaced.
            check(prefix+"-height-swapped",101,false);
            if(s.projectedObjectsCacheSignature==before)fail("height change did not invalidate cache");
        }
    }
    std::cout<<"Airborne picking: scenes="<<scenes<<" overlapClicks="<<clicks<<" cacheHits="<<cacheHits
        <<" failures="<<failures.size()<<'\n';
    if(!failures.empty()){error="Airborne picking: "+failures.front();return false;}
    error.clear();return true;
}
