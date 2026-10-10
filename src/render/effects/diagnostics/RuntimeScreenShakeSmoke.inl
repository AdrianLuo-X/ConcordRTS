bool runRuntimeScreenShakeSmoke(const RuntimeConfig& config,std::string& error){
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& message){error="Screen shake: "+message;return false;};
    std::ifstream file("tests/fixtures/mod_extensions.ini");std::ostringstream text;text<<file.rdbuf();
    if(!file)return fail("fixture missing");
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->report=buildRuntimeStartupReport(config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    if(!stock->report.success)return fail("original resources");
    const auto make=[&](bool shake){
        auto s=std::make_unique<RuntimeWindowState>();s->config=config;s->report=stock->report;
        s->config.computerAiEnabled=false;s->config.playerSlots.clear();s->config.shortGame=false;
        s->runtimeSoundsEnabled=false;s->runtimeSuppressPlayerAudioFeedback=true;s->runtimePreferencePersistenceEnabled=false;
        IniFile fixture;fixture.loadFromString(text.str()+(shake?"\n[NORMAL]\nShakeXlo=10\nShakeXhi=10\nShakeYlo=10\nShakeYhi=20\n":""));
        s->rules.loadFromIni(fixture);s->rulesLoaded=true;s->art=stock->art;s->artLoaded=true;
        s->economy.owner="Alpha";s->tick=10;s->visionEnabled=false;
        for(int y=0;y<30;++y)for(int x=0;x<30;++x){TerrainCell cell;cell.x=x;cell.y=y;cell.tileIndex=0;cell.cellIndex512=y*512+x;
            s->world.accessTerrainCells().push_back(cell);s->terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});}
        for(int id=1;id<=2;++id){WorldObjectInstance object;object.instanceId=id;object.kind=WorldObjectKind::Unit;
            object.type=id==1?"SHOOTER":"TARGET";object.owner=id==1?"Alpha":"Beta";object.health=1000;
            object.x=5+id*2;object.y=10;object.cell=runtimeCellKey(object.x,object.y);object.mission="Guard";
            runtimeInitializeModExtensions(object,s->rules,s->config.fixedTickHz);s->world.accessObjects().push_back(object);}
        s->terrainByCell=buildRuntimeTerrainLookup(s->world);initializeRuntimeNavigation(*s);return s;
    };
    const auto hit=[](RuntimeWindowState& s,const char* animation){
        RuntimeProjectileEffect p;p.actorId=71;p.attackerObjectId=1;p.targetObjectId=2;p.owner="Alpha";
        p.fromCell={7,10};p.toCell=p.currentCell={9,10};p.weaponId="GUN";p.warheadId="NORMAL";
        p.damage=60;p.ag=true;p.affectsEnemies=true;p.affectsAllies=true;p.impactAnimId=animation;
        p.totalTicks=p.remainingTicks=1;p.lifecycle=RuntimeProjectileLifecycleState::Impacting;p.lifecycleState="Impacting";
        return applyRuntimeProjectileImpact(s,p);
    };
    auto s=make(true);auto quiet=make(false);
    if(!hit(*s,"RING1") || !hit(*quiet,"RING1"))return fail("actual projectile hit");
    if(s->world.deterministicSimulationStateHash()!=quiet->world.deterministicSimulationStateHash()
        || findObjectById(*s,2)->modExtensions.shieldHealth!=40)return fail("shake parameters changed damage or positions");
    RuntimeMapRenderContext context;context.rules=&s->rules;context.fixedTickHz=s->config.fixedTickHz;
    context.impactActors=runtimeImpactRenderActorsForRender(*s);
    const auto offset=runtimeScreenShakeForRender(context);
    if(std::abs(offset.x)!=10 || std::abs(offset.y)<10 || std::abs(offset.y)>20)return fail("actual impact did not use authored shake range");
    auto quietContext=context;quietContext.rules=&quiet->rules;quietContext.impactActors=runtimeImpactRenderActorsForRender(*quiet);
    if(auto off=runtimeScreenShakeForRender(quietContext);off.x || off.y)return fail("unconfigured explosion shook");
    auto duplicate=*context.impactActors;duplicate.insert(duplicate.end(),context.impactActors->begin(),context.impactActors->end());
    auto stacked=context;stacked.impactActors=&duplicate;
    if(auto off=runtimeScreenShakeForRender(stacked);off.x!=offset.x*2 || off.y!=offset.y*2)return fail("overlapping impacts did not combine");
    std::reverse(duplicate.begin(),duplicate.end());
    if(auto off=runtimeScreenShakeForRender(stacked);off.x!=offset.x*2 || off.y!=offset.y*2)return fail("overlap depends on insertion order");
    auto ignored=duplicate;for(auto& actor:ignored)actor.renderIntent="weaponAnim";stacked.impactActors=&ignored;
    if(auto off=runtimeScreenShakeForRender(stacked);off.x || off.y)return fail("muzzle animation duplicated quake");
    std::ostringstream save;if(!writeRuntimeStateToStream(*s,save,error))return fail("save: "+error);
    const auto worldHash=s->world.deterministicSimulationStateHash();const auto camera=s->camera;
    constexpr int width=640,height=240;const RECT viewport{20,30,460,220};
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);void* raw=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    if(!dc || !bitmap || !raw){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return fail("render surface");}
    const auto old=SelectObject(dc,bitmap);auto* pixels=static_cast<std::uint32_t*>(raw);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        pixels[y*width+x]=x>=480?0x003f4f77:y<30?0x00212c40:(x%30<2 || y%30<2)?0x0080b080:0x002c543c;
    for(int y=80;y<110;++y)for(int x=140;x<175;++x)pixels[y*width+x]=0x00e0d060;
    const std::vector<std::uint32_t> before(pixels,pixels+width*height);
    context.surfacePixels=pixels;context.surfaceWidth=width;context.surfaceHeight=height;
    bool ok=applyRuntimeScreenShake(dc,viewport,context,s->renderCache);
    const std::vector<std::uint32_t> direct(pixels,pixels+width*height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const bool inside=x>=viewport.left && x<viewport.right && y>=viewport.top && y<viewport.bottom;
        const int sourceX=inside?std::clamp(x-offset.x,static_cast<int>(viewport.left),static_cast<int>(viewport.right)-1):x;
        const int sourceY=inside?std::clamp(y-offset.y,static_cast<int>(viewport.top),static_cast<int>(viewport.bottom)-1):y;
        ok=ok && direct[y*width+x]==before[sourceY*width+sourceX];
    }
    std::copy(before.begin(),before.end(),pixels);auto fallback=context;fallback.surfacePixels=nullptr;
    ok=applyRuntimeScreenShake(dc,viewport,fallback,s->renderCache) && ok;GdiFlush();
    ok=ok && std::equal(direct.begin(),direct.end(),pixels);
    if(const char* path=std::getenv("CONCORD_SCREEN_SHAKE_BMP");path && *path){
        std::vector<std::uint32_t> pair(width*2*height);
        for(int y=0;y<height;++y){std::copy_n(before.data()+y*width,width,pair.data()+y*width*2);
            std::copy_n(direct.data()+y*width,width,pair.data()+y*width*2+width);}
        ok=writeRuntimeSmokeBmp(path,pair.data(),width*2,height,error) && ok;
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    if(!ok)return fail("actual DIB/GDI pixels or stationary UI boundaries");
    std::ostringstream after;if(!writeRuntimeStateToStream(*s,after,error) || after.str()!=save.str()
        || worldHash!=s->world.deterministicSimulationStateHash() || camera.worldOffsetX!=s->camera.worldOffsetX
        || camera.worldOffsetY!=s->camera.worldOffsetY)return fail("render changed saved simulation or camera");
    for(int redraw=0;redraw<20;++redraw){context.animationTick=redraw*100;
        const auto off=runtimeScreenShakeForRender(context);if(off.x!=offset.x || off.y!=offset.y)return fail("paused redraw changed shake");}
    RuntimeImpactEffect snapshotImpact;
    if(s->impactEffects.empty() || !parseRuntimeMultiplayerSnapshotImpactRecord(
        runtimeMultiplayerSnapshotImpactRecord(s->impactEffects.front()),snapshotImpact))return fail("snapshot impact record");
    std::vector<RuntimeImpactRenderActor> snapshotActors{makeRuntimeImpactRenderActor(snapshotImpact)};
    auto snapshotContext=context;snapshotContext.impactActors=&snapshotActors;
    if(auto off=runtimeScreenShakeForRender(snapshotContext);off.x!=offset.x || off.y!=offset.y)return fail("snapshot phase differs");
    IniFile saved;saved.loadFromString(save.str());auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,*s,false);
    if(!materializeRuntimeLoadStateFromIni(*restored,saved,error))return fail("restore: "+error);
    // Normal saves discard transient impacts. They must not retain a stale
    // quake, and copying the rules must preserve shaking on subsequent hits.
    auto resumed=context;resumed.rules=&restored->rules;resumed.impactActors=runtimeImpactRenderActorsForRender(*restored);
    if(auto off=runtimeScreenShakeForRender(resumed);off.x || off.y)return fail("normal load retained a stale quake");
    if(!hit(*restored,"RING1"))return fail("post-load hit");
    resumed.impactActors=runtimeImpactRenderActorsForRender(*restored);
    if(auto off=runtimeScreenShakeForRender(resumed);off.x!=offset.x || off.y!=offset.y)return fail("post-load shake rules differ");
    const int ticks=(runtimeScreenShakeFrames*s->config.fixedTickHz+14)/15;
    for(int n=0;n<ticks;++n){++s->tick;advanceRuntimeEffects(*s);}
    context.impactActors=runtimeImpactRenderActorsForRender(*s);
    if(auto off=runtimeScreenShakeForRender(context);off.x || off.y)return fail("shake outlived eight authored frames");
    auto invisible=make(true);if(!hit(*invisible,""))return fail("animation-free hit");
    context.rules=&invisible->rules;context.impactActors=runtimeImpactRenderActorsForRender(*invisible);
    if(auto off=runtimeScreenShakeForRender(context);!off.x && !off.y)return fail("missing animation suppressed shake");
    for(int n=0;n<ticks+2;++n){++invisible->tick;advanceRuntimeEffects(*invisible);}
    context.impactActors=runtimeImpactRenderActorsForRender(*invisible);
    if(auto off=runtimeScreenShakeForRender(context);off.x || off.y)return fail("expired impact retained quake");
    std::cout<<"Screen shake: passed; real hit, pixels, overlap, paused redraw, save/restore, expiry and unchanged gameplay\n";return true;
}
