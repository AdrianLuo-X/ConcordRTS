bool runRuntimeSpecialDamageSmokeRenderContractCheck(const GameWorld& world,
    const RuntimeMapRenderContext& context, int sourceObjectId, const RuntimeParticlePoint& origin,
    const std::vector<RuntimeParticleRenderSnapshot>& smoke, const std::string& label, std::string& error) {
    const auto objectIt=std::find_if(world.getObjects().begin(),world.getObjects().end(),
        [&](const auto& object){return object.instanceId==sourceObjectId;});
    if(objectIt==world.getObjects().end()||smoke.empty()||!context.rules){error=label+" missing real source/puff";return false;}
    const auto& object=*objectIt;
    const auto* unit=findObjectUnitTypeForRender(context,object);
    if(!unit){error=label+" missing stock type";return false;}
    const char* output=std::getenv("CONCORD_SPECIAL_DAMAGE_SMOKE_OUTPUT");
    const std::filesystem::path root=output&&*output?output:
        "build/smoke/feature-set-19-complete/02-damage-smoke/special-checks";
    std::filesystem::create_directories(root);
    constexpr int width=640,height=480;
    const RECT viewport{0,0,width,height};
    constexpr std::uint32_t background=0x00363d44U;
    struct Surface {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
        Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            dc=CreateCompatibleDC(nullptr);void* raw=nullptr;if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
            pixels=static_cast<std::uint32_t*>(raw);if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
        ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } surface;
    if(!surface.dc||!surface.pixels){error=label+" DIB allocation";return false;}
    auto cache=std::make_unique<RuntimeMapRenderCache>();auto view=context;
    view.vision=nullptr;view.mapFacts=nullptr;view.useTileTextures=false;view.shadowsEnabled=false;
    view.surfacePixels=surface.pixels;view.surfaceWidth=width;view.surfaceHeight=height;
    std::ofstream csv(root/(label+"-pixels.csv"));
    csv<<"zoom,pan,expected_x,expected_y,body_dx,body_dy,mismatch,visible_smoke,drawn,pause_equal,result\n";
    int failures=0;std::string first;
    const auto fail=[&](const std::string& why){++failures;if(first.empty())first=why;
        std::cout<<"Special smoke render FAIL "<<label<<' '<<why<<'\n';};
    const auto difference=[](const auto& a,const auto& b){int n=0;for(std::size_t i=0;i<a.size();++i)
        n+=(a[i]&0xffffffU)!=(b[i]&0xffffffU);return n;};
    // A just-created puff can be fully covered by its launcher or the opening
    // factory. Require real visible pixels once the same source reaches a
    // mature/clear phase; keep exact isolated anchor and draw-count checks in
    // every early phase, including the first opaque smoke image.
    const bool mustBeVisible=label.find("-over")!=std::string::npos||label.find("-middle")!=std::string::npos
        ||label.find("-departed")!=std::string::npos||label.find("-free-flight")!=std::string::npos;
    for(int zoom:{48,60,72})for(int pan:{0,37}){
        RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=zoom;camera.tileHeight=zoom/2;
        const double x=objectRenderX(object),y=objectRenderY(object),z=objectRenderLevel(object);
        const auto raw=projectCellXY(x,y,z,camera);
        camera.worldOffsetX=raw.x-width/2+pan;camera.worldOffsetY=raw.y-height/2-pan;
        // Independent geometry oracle: original world axes, authored art height,
        // and launcher snapshot. Do not read any puff presentation or call its
        // new capture/lift helper to obtain the expected anchor.
        POINT expectedBody=screenPoint(projectCellXY(x,y,z,camera),viewport,camera);
        double sourceX=x,sourceY=y,sourceLevel=z;
        if(unit->missileSpawn){
            const SpecialMechanismRenderSnapshot* flight=nullptr;
            if(context.specialMechanismSnapshots)for(const auto& snapshot:*context.specialMechanismSnapshots)
                if(snapshot.kind=="spawnedMissile"&&snapshot.objectId==sourceObjectId){flight=&snapshot;break;}
            if(!flight){fail("dedicated missile body snapshot absent");continue;}
            sourceX=flight->sourceX;sourceY=flight->sourceY;sourceLevel=flight->sourceLevel;
            expectedBody=screenPoint(projectCellXY(sourceX,sourceY,sourceLevel,camera),viewport,camera);
            expectedBody.x+=static_cast<int>(std::lround(flight->sourceNativeFixedX
                +(flight->sourceScreenOffsetX-flight->sourceNativeFixedX)*zoom/60.0));
            expectedBody.y+=static_cast<int>(std::lround(flight->sourceNativeFixedY
                +(flight->sourceScreenOffsetY-flight->sourceNativeFixedY)*camera.tileHeight/30.0))-camera.tileHeight/2;
        }else if(object.airborne){
            expectedBody.y-=34;int lift=0;bool producing=false;
            for(const auto& roof:world.getObjects()){
                if(roof.kind!=WorldObjectKind::Structure||!roof.alive)continue;
                const auto foundation=runtimeObjectFoundationDimensions(roof,context);
                if(x<roof.x-.5||x>roof.x+foundation.width-.5||y<roof.y-.5||y>roof.y+foundation.height-.5)continue;
                if(roof.instanceId==object.factoryExitHostId){
                    const double progress=unit->jumpJetHeight>0?std::clamp((z-roof.level)*256.0/unit->jumpJetHeight,0.0,1.0):1.0;
                    if(progress<1){expectedBody.y+=static_cast<int>(std::lround(34*(1-progress)));producing=true;break;}
                    continue;
                }
                const auto* art=findArtTypeForObject(roof,context);
                int authored=art?std::clamp(art->height,0,512):0,native=0;
                if(authored<=0){auto* sprite=loadRuntimeObjectShpSpriteFromCache(roof,context,*cache);
                    if(!sprite){fail("original building SHP absent");continue;}
                    native=std::clamp(sprite->image.globalHeight>0?sprite->image.globalHeight:sprite->image.height,0,512);}
                const int tall=std::clamp(std::max({std::max(18,camera.tileHeight),authored*std::max(4,camera.tileHeight/3),native}),
                    std::max(18,camera.tileHeight),512);
                const auto roofPoint=screenPoint(projectCellXY(roof.x,roof.y,roof.level,camera),viewport,camera);
                lift=std::max(lift,static_cast<int>(expectedBody.y-(roofPoint.y+camera.tileHeight/2-tall-std::max(4,camera.tileHeight/6))));
            }
            if(!producing)expectedBody.y-=std::clamp(lift,0,512);
        }
        const auto body=runtimeObjectWorldRenderPlacement(object,world,viewport,view,camera,*cache).center;
        const auto base=projectCellXY(sourceX,sourceY,sourceLevel,camera);
        const auto offset=unit->damageSmokeOffset;
        const auto outlet=projectCellXY(sourceX+offset[0]/256.0,sourceY+offset[1]/256.0,sourceLevel+offset[2]/256.0,camera);
        const POINT expected{expectedBody.x+outlet.x-base.x,expectedBody.y+outlet.y-base.y};
        auto sample=smoke.front();sample.rawX=origin.x;sample.rawY=origin.y;sample.rawZ=origin.z;
        sample.worldX=origin.x/256.0;sample.worldY=origin.y/256.0;sample.worldLevel=origin.z/256.0;sample.frameIndex=0;
        const std::vector<RuntimeParticleRenderSnapshot> sourceOnly{sample};
        std::fill_n(surface.pixels,width*height,background);view.particleSnapshots=&sourceOnly;
        RuntimeMapRenderStats actualStats;drawRuntimeParticleSnapshots(nullptr,viewport,view,camera,*cache,actualStats);
        const std::vector<std::uint32_t> actual(surface.pixels,surface.pixels+width*height);
        std::fill_n(surface.pixels,width*height,background);
        const auto* palette=runtimePaletteForAuxiliaryEffectSprite(view,*cache,sample.imageId,RuntimeAuxiliaryEffectKind::Impact);
        const int opacity=((100-std::clamp(sample.translucency,0,100))*255+50)/100;
        const bool expectedDrawn=palette&&drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,expected,view,camera,*cache,
            sample.imageId,0,*palette,0,0,false,false,RGB(255,255,255),opacity,true);
        const std::vector<std::uint32_t> expectedPixels(surface.pixels,surface.pixels+width*height);
        const int mismatch=difference(actual,expectedPixels);
        const auto full=[&](bool puff,std::vector<std::uint32_t>& frame){view.particleSnapshots=puff?&smoke:nullptr;
            const auto stats=drawRuntimeMap(surface.dc,viewport,world,view,camera,*cache);GdiFlush();
            frame.assign(surface.pixels,surface.pixels+width*height);return stats;};
        std::vector<std::uint32_t> bare,withSmoke,repeat;
        const auto bareStats=full(false,bare),smokeStats=full(true,withSmoke),repeatStats=full(true,repeat);
        const int visible=difference(bare,withSmoke);
        const bool opaque=std::any_of(expectedPixels.begin(),expectedPixels.end(),[&](auto pixel){return (pixel&0xffffffU)!=background;});
        const bool valid=body.x==expectedBody.x&&body.y==expectedBody.y&&mismatch==0&&expectedDrawn&&opaque
            &&actualStats.particleActorShpDrawn==1&&actualStats.particleActorFallbackDrawn==0
            &&bareStats.rendered&&smokeStats.rendered&&repeatStats.rendered&&(!mustBeVisible||visible>0)&&withSmoke==repeat
            &&smokeStats.particleActorShpDrawn==static_cast<int>(smoke.size())&&smokeStats.particleActorMissing==0;
        if(!valid)fail("zoom="+std::to_string(zoom)+" pan="+std::to_string(pan)+" bodyDelta="
            +std::to_string(body.x-expectedBody.x)+","+std::to_string(body.y-expectedBody.y)
            +" mismatch="+std::to_string(mismatch)+" visible="+std::to_string(visible)
            +" drawn="+std::to_string(smokeStats.particleActorShpDrawn)+" expected="+std::to_string(smoke.size()));
        csv<<zoom<<','<<pan<<','<<expected.x<<','<<expected.y<<','<<body.x-expectedBody.x<<','<<body.y-expectedBody.y
            <<','<<mismatch<<','<<visible<<','<<smokeStats.particleActorShpDrawn<<','<<(withSmoke==repeat)<<','<<(valid?"PASS":"FAIL")<<'\n';
        if(pan==0){std::vector<std::uint32_t> sheet(width*3*height);
            for(int c=0;c<3;++c){const auto& frame=c==0?bare:c==1?withSmoke:actual;
                for(int row=0;row<height;++row)std::copy_n(frame.begin()+row*width,width,sheet.begin()+row*width*3+c*width);}
            if(!writeRuntimeBgraBmp(root/(label+"-"+std::to_string(zoom)+".bmp"),width*3,height,sheet))fail("image write");}
    }
    if(failures){error=label+": "+first;return false;}error.clear();return true;
}
