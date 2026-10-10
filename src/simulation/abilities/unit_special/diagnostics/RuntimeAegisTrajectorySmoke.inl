bool runRuntimeAegisTrajectorySmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* output=std::getenv("CONCORD_AEGIS_TRAJECTORY_OUTPUT");
    const std::string root=output?output:"build/aegis-trajectory";
    std::filesystem::create_directories(root);
    std::ofstream path(root+"/trajectories.csv");
    path<<std::setprecision(17);
    path<<"scene,tick,actor,age,x,y,z,yaw,pitch,remaining,source_x,source_y,source_z,target_x,target_y,target_z,launch_source_type,launch_source_validated,weapon,projectile,render_intent,speed_cells_per_tick,attacker_facing,damage\n";
    std::ofstream sceneRows(root+"/cases.csv"),controlRows(root+"/guidance_controls.csv");
    sceneRows<<std::setprecision(17);controlRows<<std::setprecision(17);
    sceneRows<<"scene,hz,rank,segment,distance,height,durable_fixture,missiles,max_side,damage,ticks,result\n";
    controlRows<<"hz,control,age,expected_lock,yaw_change,current_x,current_y,current_z,source_type,validated,weapon,projectile,raw_lock,result\n";
    const char* nativeOnlyValue=std::getenv("CONCORD_AEGIS_TRAJECTORY_NATIVE_ONLY");
    const bool nativeOnly=nativeOnlyValue&&std::string(nativeOnlyValue)=="1";
    std::ofstream nativeRows(root+"/native-captures.csv");nativeRows<<std::setprecision(17);
    nativeRows<<"scene,tick,age,actor,capture,body_drawn,body_missing,body_decode_missing,body_palette_missing,line_segments,line_pixels,trail_nodes,trail_start_x,trail_start_y,trail_head_x,trail_head_y,x,y,z,source_x,source_y,source_z,raw_screen_dx,raw_screen_dy,progress\n";
    std::string scene;
    const auto fail=[&](const std::string& why){error="Aegis "+scene+": "+why;return false;};
    IniFile native,overlay;RulesDatabase rules;
    if(!native.loadFromFile(live.report.rulesPath)||!overlay.loadFromString("[JUMPJET]\nStrength=100000\n"))
        return fail("durable aircraft fixture");
    native.appendFrom(overlay);if(!rules.loadFromIni(native))return fail("fixture rules");
    const auto fixtureRulesHash=rules.getDetailRulesHash();
    const auto fixtureRulesReady=[&](const RuntimeWindowState& q) {
        if(q.rules.getDetailRulesHash()!=fixtureRulesHash)return false;
        for(const char* id:{"Medusa","MedusaE"}) {
            const auto* profile=q.rules.findWeaponLifecycleProfile(id);
            if(!profile||!profile->omniFire)return false;
        }
        return true;
    };
    const auto loadFixtureRules=[&](RuntimeWindowState& q) {
        // Lifecycle profiles are cached by the actual RulesDatabase owner.
        // A value copy of the database does not populate that owner's cache.
        if(!q.rules.loadFromIni(native)||!fixtureRulesReady(q))return false;
        q.rulesLoaded=true;return true;
    };
    if(!live.rulesLoaded||!live.artLoaded||!live.theaterLoaded)
        return fail("original map assets unavailable");
    // Use the same legal literal Format5 representation as RuntimeRandomMap
    // and the native save fixtures. A small real map keeps every restore on
    // the same parsed terrain, instead of rebuilding the unrelated large map.
    constexpr int fixtureSize=80,fixtureCells=2*fixtureSize*fixtureSize-fixtureSize;
    int waterTile=-1;const int waterSet=live.theater.getGeneralTileSetIndex("WaterSet");
    for(const auto& set:live.theater.getTileSets())
        if(set.index==waterSet&&set.tilesInSet>0)waterTile=set.startTileIndex;
    TerrainCell water;water.tileIndex=waterTile;
    const auto waterInfo=runtimeTerrainInfoForCell(live,water);
    if(waterTile<0||!waterInfo.water||waterInfo.shore||waterInfo.ramp||waterInfo.cliff||waterInfo.bridge)
        return fail("original flat water TMP unavailable");
    using FixtureBytes=std::vector<unsigned char>;
    const auto append16=[](FixtureBytes& bytes,unsigned int value) {
        bytes.push_back(static_cast<unsigned char>(value));
        bytes.push_back(static_cast<unsigned char>(value>>8));
    };
    const auto append32=[&](FixtureBytes& bytes,unsigned int value) {
        append16(bytes,value);append16(bytes,value>>16);
    };
    const auto packTiles=[&](const FixtureBytes& raw) {
        FixtureBytes packed;
        for(std::size_t at=0;at<raw.size();at+=8192) {
            const std::size_t count=std::min<std::size_t>(8192,raw.size()-at);FixtureBytes chunk;
            if(count<=238)chunk.push_back(static_cast<unsigned char>(17+count));
            else {chunk.push_back(0);std::size_t extra=count-18;
                while(extra>255){chunk.push_back(0);extra-=255;}
                chunk.push_back(static_cast<unsigned char>(extra));}
            chunk.insert(chunk.end(),raw.begin()+at,raw.begin()+at+count);
            chunk.insert(chunk.end(),{17,0,0});
            append16(packed,static_cast<unsigned int>(chunk.size()));
            append16(packed,static_cast<unsigned int>(count));
            packed.insert(packed.end(),chunk.begin(),chunk.end());
        }
        return packed;
    };
    const auto base64=[](const FixtureBytes& bytes) {
        constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string text;text.reserve((bytes.size()+2)/3*4);
        for(std::size_t at=0;at<bytes.size();at+=3) {
            const unsigned int bits=(static_cast<unsigned int>(bytes[at])<<16)
                |(at+1<bytes.size()?static_cast<unsigned int>(bytes[at+1])<<8:0)
                |(at+2<bytes.size()?bytes[at+2]:0);
            text+=alphabet[(bits>>18)&63];text+=alphabet[(bits>>12)&63];
            text+=at+1<bytes.size()?alphabet[(bits>>6)&63]:'=';
            text+=at+2<bytes.size()?alphabet[bits&63]:'=';
        }
        return text;
    };
    FixtureBytes raw;raw.reserve(fixtureCells*11+4);
    for(int y=1;y<2*fixtureSize;++y)for(int x=1;x<2*fixtureSize;++x) {
        const int u=x-y+fixtureSize,v=x+y-fixtureSize;
        if(u<0||u>=2*fixtureSize||v<1||v>=2*fixtureSize)continue;
        append16(raw,x);append16(raw,y);append32(raw,static_cast<unsigned int>(waterTile));
        raw.insert(raw.end(),{0,0,0});
    }
    if(raw.size()!=fixtureCells*11U)return fail("native small-map diamond is incomplete");
    append32(raw,0);const std::string encoded=base64(packTiles(raw));
    const auto mapDirectory=std::filesystem::absolute(std::filesystem::path(root)/"fixture-maps").lexically_normal();
    std::filesystem::create_directories(mapDirectory);
    const std::string fixtureMapPath=(mapDirectory/"aegis-flat80.map").generic_string();
    {std::ofstream mapFile(fixtureMapPath,std::ios::binary|std::ios::trunc);
        mapFile<<std::setprecision(17)<<"[Basic]\nName=AegisTrajectoryFlat80\nMultiplayerOnly=yes\nNewINIFormat=4\n"
            <<"[Header]\nWidth=160\nHeight=160\nStartX=0\nStartY=0\nNumberStartingPoints=0\n"
            <<"[Map]\nTheater="<<live.report.map.theater<<"\nSize=0,0,80,80\nLocalSize=0,0,80,80\n"
            <<"[SpecialFlags]\nTiberiumGrows=no\nTiberiumSpreads=no\n[Lighting]\n"
            <<"Ambient="<<live.mapFacts.lighting.base.ambient<<"\nRed="<<live.mapFacts.lighting.base.red
            <<"\nGreen="<<live.mapFacts.lighting.base.green<<"\nBlue="<<live.mapFacts.lighting.base.blue
            <<"\nGround="<<live.mapFacts.lighting.base.ground<<"\nLevel="<<live.mapFacts.lighting.base.level
            <<"\n[IsoMapPack5]\n";
        for(std::size_t at=0;at<encoded.size();at+=70)
            mapFile<<at/70+1<<'='<<encoded.substr(at,70)<<'\n';
        mapFile.close();if(!mapFile)return fail("native small-map file could not be persisted");
    }
    auto fixtureBox=std::make_unique<RuntimeWindowState>();auto& fixture=*fixtureBox;
    seedRuntimeLoadScratch(fixture,live,false);fixture.config.defaultMapPath=fixtureMapPath;
    if(!loadFixtureRules(fixture))return fail("native small-map owner rules unavailable");
    IniFile mapIni;MapDatabase map;
    if(!mapIni.loadFromFile(fixtureMapPath)||!map.loadFromIni(mapIni,&native)
        ||!fixture.world.loadFromMap(map,&fixture.rules))return fail("native small-map parse/hydration failed");
    const auto& summary=map.getSummary();fixture.report.map.loaded=true;fixture.report.map.path=fixtureMapPath;
    fixture.report.map.name=summary.name;fixture.report.map.theater=summary.theater;
    fixture.report.map.size=summary.size;fixture.report.map.localSize=summary.localSize;
    fixture.report.map.width=summary.width;fixture.report.map.height=summary.height;
    fixture.report.map.isoTiles=static_cast<int>(map.getIsoTiles().size());
    fixture.report.map.overlayCells=static_cast<int>(map.getOverlayCells().size());
    fixture.report.map.structures=static_cast<int>(summary.structureCount);fixture.report.map.units=static_cast<int>(summary.unitCount);
    fixture.report.map.infantry=static_cast<int>(summary.infantryCount);fixture.report.map.triggers=static_cast<int>(summary.triggerCount);
    fixture.report.world=fixture.world.getSummary();fixture.mapFacts=map.buildMapFacts(&fixture.theater);
    fixture.mapFactsLoaded=!fixture.mapFacts.cells.empty();
    fixture.terrainByCell=buildRuntimeTerrainLookup(fixture.world);fixture.terrainInfoByCell.clear();
    fixture.initialOreFieldsSeeded=true;fixture.initialOreCellsSeeded=fixture.initialOreCellsDensified=0;
    initializeRuntimeNavigation(fixture);
    const auto fixtureTerrainReady=[&](const RuntimeWindowState& q) {
        return q.config.defaultMapPath==fixtureMapPath&&q.report.map.path==fixtureMapPath
            &&q.world.getTerrainCells().size()==fixtureCells&&q.mapFacts.cells.size()==fixtureCells
            &&q.mapFacts.terrainCellCount==fixtureCells&&q.terrainByCell.size()==fixtureCells
            &&q.terrainInfoByCell.size()==fixtureCells;
    };
    const auto fixturePointReady=[&](const RuntimeWindowState& q,double x,double y) {
        const int cx=static_cast<int>(std::lround(x)),cy=static_cast<int>(std::lround(y));
        const auto found=q.terrainByCell.find(runtimeCellKey(cx,cy));const MapQuery query(q.mapFacts);
        const auto* fact=query.getCell(cx,cy);
        // Runtime lookup keys use y*1000+x; native map/facts indexes use y*512+x.
        const int nativeIndex=cy*512+cx;
        return found!=q.terrainByCell.end()&&found->second&&found->second->x==cx&&found->second->y==cy
            &&found->second->level==0&&found->second->cellIndex512==nativeIndex
            &&found->second->tileIndex==waterTile&&found->second->subTileIndex==0
            &&fact&&fact->hasTerrain&&fact->cellIndex==nativeIndex&&fact->heightLevel==0
            &&fact->terrain.x==cx&&fact->terrain.y==cy&&fact->terrain.level==0
            &&fact->terrain.tileIndex==waterTile&&fact->terrain.subTileIndex==0;
    };
    if(!fixtureTerrainReady(fixture)||!fixture.world.getObjects().empty())
        return fail("native small-map world/facts/metadata domain differs");
    for(const auto& cell:fixture.world.getTerrainCells()) {
        const int nativeIndex=cell.y*512+cell.x;
        if(cell.level!=0||cell.cellIndex512!=nativeIndex
            ||cell.tileIndex!=waterTile||!fixturePointReady(fixture,cell.x,cell.y)) {
            const MapQuery query(fixture.mapFacts);const auto* fact=query.getCell(cell.x,cell.y);
            const auto found=fixture.terrainByCell.find(runtimeCellKey(cell.x,cell.y));
            std::ostringstream why;why<<"native small-map terrain/facts/512 identity differs"
                <<" cell="<<cell.x<<','<<cell.y<<" nativeIndex="<<cell.cellIndex512
                <<" expectedNativeIndex="<<nativeIndex<<" lookupKey="<<runtimeCellKey(cell.x,cell.y)
                <<" level="<<cell.level<<" tile="<<cell.tileIndex<<" expectedTile="<<waterTile
                <<" subTile="<<cell.subTileIndex<<" lookupPresent="<<(found!=fixture.terrainByCell.end());
            if(found!=fixture.terrainByCell.end()&&found->second)
                why<<" lookupCell="<<found->second->x<<','<<found->second->y
                    <<" lookupIndex="<<found->second->cellIndex512<<" lookupLevel="<<found->second->level
                    <<" lookupTile="<<found->second->tileIndex<<" lookupSubTile="<<found->second->subTileIndex;
            why<<" factPresent="<<(fact!=nullptr);
            if(fact)why<<" factHasTerrain="<<fact->hasTerrain<<" factIndex="<<fact->cellIndex
                <<" factXY="<<fact->terrain.x<<','<<fact->terrain.y<<" factLevel="<<fact->heightLevel
                <<" factTerrainLevel="<<fact->terrain.level<<" factTile="<<fact->terrain.tileIndex
                <<" factSubTile="<<fact->terrain.subTileIndex;
            return fail(why.str());
        }
    }
    const auto sameLight=[](const MapLightingProfile& a,const MapLightingProfile& b) {
        return a.ambient==b.ambient&&a.red==b.red&&a.green==b.green&&a.blue==b.blue
            &&a.ground==b.ground&&a.level==b.level;
    };
    if(!sameLight(fixture.mapFacts.lighting.base,live.mapFacts.lighting.base))
        return fail("native small-map changed original lighting");
    // This is explicitly a durable geometry fixture; the original-stock
    // health/ROF/Burst/native-pixel contract remains in AegisIfvPresentation.
    int cases=0,missiles=0,saves=0,paired=0,captures=0,postImpactSnapshots=0;
    int inFlightSnapshots=0,guidanceControls=0,exactPaired=0;
    for(int hz:{15,30,60})for(int rank:{0,2})for(int rangeCase:{0,1,2,3})for(int segment=0;segment<10;++segment) {
        const int distance=rangeCase==0?8:(rangeCase==1?4:(rangeCase==3?2:(rank==0?12:14)));
        int altitude=rangeCase==0?8:((rangeCase==1||rangeCase==3)?4:12);
        scene=std::to_string(hz)+"-rank"+std::to_string(rank)+"-distance"+std::to_string(distance)
            +"-height"+std::to_string(altitude)+"-segment"+std::to_string(segment);
        if(nativeOnly&&!(rangeCase==0&&hz==30&&rank==0&&(segment==0||segment==4||segment==9)))continue;
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        seedRuntimeLoadScratch(s,fixture,false);
        if(!loadFixtureRules(s))return fail("fixture owner lost original Medusa OmniFire/hash");
        s.config.fixedTickHz=hz;s.config.noCooldown=false;s.config.playerSlots.clear();
        s.config.playerCountry="Americans";s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.shortGame=false;
        s.visionEnabled=s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        initializeRuntimeLocalProductionLanes(s.economy);
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        WorldObjectInstance source;source.instanceId=1;source.type="AEGIS";source.kind=WorldObjectKind::Unit;
        source.owner="Americans";source.health=800;source.alive=true;source.x=source.y=75;
        source.cell=runtimeCellKey(75,75);source.mission="Guard";source.veterancy=rank;
        source.facing=source.turretFacing=runtimeFacingFromDelta(rangeCase==3?-1:1,0);
        auto target=source;target.instanceId=2;target.type="JUMPJET";target.kind=WorldObjectKind::Infantry;
        target.owner="Russians";target.health=100000;target.veterancy=0;target.x=75+distance;
        target.cell=runtimeCellKey(target.x,75);target.airborne=true;target.flightLevel=altitude;target.level=altitude;
        target.mission="Sleep";target.weaponCooldownTicksRemaining=100000;
        s.world.accessObjects()={source,target};s.selectedObjectIds={1};
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);
        const int targetGround=terrainLevelAtRuntimeCell(s,target.x,target.y);
        if(rangeCase==1||rangeCase==3) {
            // Near/low means one real level above the original loaded map,
            // not an absolute flight level that can be below its terrain.
            altitude=targetGround+1;
            auto* low=findMutableObjectById(s,2);if(!low)return fail("near aircraft missing");
            low->flightLevel=altitude;low->level=altitude;
            scene=std::to_string(hz)+"-rank"+std::to_string(rank)+"-distance"+std::to_string(distance)
                +"-height"+std::to_string(altitude)+"-segment"+std::to_string(segment);
        }
        const auto* preTarget=findObjectById(s,2);
        if(!preTarget||!runtimeObjectIsAirTarget(s,*preTarget)
            ||runtimeProjectileTargetPosition(s,*preTarget).level<=targetGround)
            return fail("pre-launch target is not a real above-ground air target");
        initializeRuntimeNavigation(s);
        if(!fixtureTerrainReady(s)||!fixturePointReady(s,source.x,source.y)||!fixturePointReady(s,target.x,target.y))
            return fail("scene source/target left native flat terrain domain");
        s.combatDetails.fireCursors[1].radialSegment=segment;
        RuntimeSnapshotActiveStateScope active(s);
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{75+distance,75},findObjectById(s,2)))
            return fail("actual player attack rejected");
        for(int n=0;n<hz*2&&s.projectiles.empty();++n){++s.tick;advanceRuntimeSimulation(s,false);}
        if(s.projectiles.empty()) {
            // Diagnostic only: a projectile may launch and expire inside one
            // complete simulation step. Preserve that real terminal evidence.
            std::ostringstream why;why<<std::setprecision(17)
                <<"no launch from player attack tick="<<s.tick<<" hz="<<hz
                <<" pendingBurst="<<s.combatDetails.hasPendingBurst(1)
                <<" burstCount="<<s.combatDetails.bursts.size()
                <<" expiredCount="<<s.recentExpiredProjectiles.size()
                <<" nextActor="<<s.nextProjectileActorId<<" status="<<s.simulationStatus
                <<" commandValid="<<s.lastCommand.valid<<" command="<<s.lastCommand.verb
                <<" accepted="<<s.lastCommand.acceptedCount<<" rejected="<<s.lastCommand.rejectedCount
                <<" commandTarget="<<s.lastCommand.targetObjectId;
            const auto* failedSource=findRuntimeObjectByIdIncludingInactive(s,1);
            const auto* failedTarget=findRuntimeObjectByIdIncludingInactive(s,2);
            if(failedSource) {
                why<<" sourceHP="<<failedSource->health<<" sourceAlive="<<failedSource->alive
                    <<" sourceFacing="<<failedSource->facing<<" sourceTurret="<<failedSource->turretFacing
                    <<" sourceMission="<<failedSource->mission
                    <<" sourceXYZ="<<runtimeObjectRenderX(*failedSource)<<','
                    <<runtimeObjectRenderY(*failedSource)<<','<<runtimeDrawLevelForObject(*failedSource)
                    <<" sourceCooldown="<<failedSource->weaponCooldownTicksRemaining;
                const auto profile=runtimeWeaponProfileForObject(s,*failedSource,failedTarget);
                const auto* lifecycle=profile.weapon
                    ?s.rules.findWeaponLifecycleProfile(profile.weapon->id):nullptr;
                why<<" weapon="<<(profile.weapon?profile.weapon->id:"none")
                    <<" projectile="<<profile.projectileId
                    <<" profileOmni="<<(lifecycle&&lifecycle->omniFire)
                    <<" minimumRange="<<(profile.weapon?profile.weapon->minimumRange:-1.0);
                if(failedTarget&&profile.weapon)why
                    <<" distance="<<runtimeWeaponDistanceToTarget(s,*failedSource,*failedTarget,*profile.weapon)
                    <<" effectiveRange="<<runtimeEffectiveWeaponRange(s,*failedSource,*failedTarget,*profile.weapon);
            }
            if(failedTarget) {
                const auto pose=runtimeProjectileTargetPosition(s,*failedTarget);
                why<<" targetHP="<<failedTarget->health<<" targetAlive="<<failedTarget->alive
                    <<" targetAir="<<runtimeObjectIsAirTarget(s,*failedTarget)
                    <<" targetMission="<<failedTarget->mission<<" targetXYZ="
                    <<pose.x<<','<<pose.y<<','<<pose.level
                    <<" targetGround="<<terrainLevelAtRuntimeCell(s,failedTarget->x,failedTarget->y);
            }
            const auto order=s.behaviors.find(1);
            if(order!=s.behaviors.end()) {
                const auto& b=order->second;
                why<<" attackOrder="<<runtimeBehaviorKindName(b.kind)
                    <<" orderState="<<static_cast<int>(b.state)<<" orderTarget="<<b.targetObjectId
                    <<" orderForce="<<b.forceFire<<" orderAuto="<<b.autoAcquired
                    <<" orderWeaponCooldown="<<b.weaponCooldownTicks
                    <<" orderMoveCooldown="<<b.moveCooldownTicks<<" orderPath="<<b.path.size()
                    <<" orderPathPending="<<b.pathRequestPending<<" orderBlocked="<<b.blockedTicks;
            } else why<<" attackOrder=none";
            for(const auto& shot:s.recentExpiredProjectiles)why
                <<" expired[actor="<<shot.actorId<<" attacker="<<shot.attackerObjectId
                <<" target="<<shot.targetObjectId<<" weapon="<<shot.weaponId
                <<" projectile="<<shot.projectileId<<" spawn="<<shot.spawnTick
                <<" impact="<<shot.impactTick<<" elapsed="<<shot.elapsedTicks
                <<" age="<<shot.elapsedTicks*15.0/hz<<" phase="<<shot.lifecycleState
                <<" remaining="<<shot.remainingTicks<<" damage="<<shot.damage
                <<" xyz="<<shot.currentX<<','<<shot.currentY<<','<<shot.currentZ<<']';
            const std::string savedPath=root+"/"+scene+"-no-launch-world.ini";
            std::ostringstream saved;std::string saveError;
            const bool wrote=writeRuntimeStateToStream(s,saved,saveError);
            bool artifact=false;
            if(wrote&&!std::filesystem::exists(savedPath)) {
                std::ofstream savedFile(savedPath);savedFile<<saved.str();artifact=savedFile.good();
            }
            why<<" worldSaved="<<artifact<<" worldPath="<<savedPath<<" worldSaveError="<<saveError;
            std::cout<<"Aegis no-launch diagnostic "<<scene<<": "<<why.str()<<'\n';
            return fail(why.str());
        }
        const int first=s.projectiles.front().actorId,spawn=s.projectiles.front().spawnTick;
        const auto* type=s.rules.findProjectileType(s.projectiles.front().projectileId);
        if(!type||type->id!="MedusaProjectile"||type->ranged||type->courseLockDuration!=15||type->rot!=20
            ||s.projectiles.front().renderIntent!="radialProjectile")return fail("stock missile bypassed radial guidance");
        const int expected=rank==0?1:2;
        const auto actualLaunch=s.projectiles.front();
        const auto* launchedTarget=findObjectById(s,2);
        if(!launchedTarget||!runtimeObjectIsAirTarget(s,*launchedTarget)
            ||actualLaunch.targetZ<=terrainLevelAtRuntimeCell(s,
                static_cast<int>(std::lround(actualLaunch.targetX)),static_cast<int>(std::lround(actualLaunch.targetY))))
            return fail("actual launched AA target endpoint is not above original terrain");
        if(!actualLaunch.launchSourceValidated||actualLaunch.launchSourceType!="AEGIS"
            ||actualLaunch.weaponId!=(rank==0?"Medusa":"MedusaE"))
            return fail("real launch lost immutable Aegis/Medusa provenance");
        // Direct-guidance identity controls copy one real launch. They never
        // replace a world actor, mutate rules or fabricate a damage event.
        if(rangeCase==0&&rank==0&&segment==9) {
            for(int control=0;control<6;++control) {
                auto probe=actualLaunch;auto probeType=*type;
                const char* name="qualified-AEGIS";int lock=3;
                if(control==1){probe.launchSourceType="FV";name="non-AEGIS-source";lock=15;}
                if(control==2){probe.launchSourceValidated=false;name="unvalidated-source";lock=15;}
                if(control==3){probe.weaponId="HoverMissile";name="non-Medusa-weapon";lock=15;}
                if(control==4){probe.projectileId="AAHeatSeeker";probeType.id="AAHeatSeeker";
                    name="non-Medusa-projectile-identity";lock=15;}
                if(control==5){probeType.courseLockDuration=2;name="raw-lock-two";lock=2;}
                probe.currentX=probe.sourceX;probe.currentY=probe.sourceY;probe.currentZ=probe.sourceZ;
                probe.elapsedTicks=0;probe.homingInitialized=true;
                probe.homingYaw=std::remainder(std::atan2(probe.targetY-probe.currentY,probe.targetX-probe.currentX)
                    +3.14159265358979323846,6.28318530717958647692);
                probe.homingPitch=0;const double initial=probe.homingYaw;bool sawTurn=false;
                const int ticks=static_cast<int>(std::ceil(lock*hz/15.0))+1;
                for(int tick=1;tick<=ticks;++tick) {
                    probe.elapsedTicks=tick;
                    (void)advanceRuntimeHomingMissilePosition(s,probe,probeType);
                    const double age=tick*15.0/hz;
                    const double change=std::abs(std::remainder(probe.homingYaw-initial,6.28318530717958647692));
                    if((age<lock&&change>1e-10)||(age>=lock&&change<=1e-10))
                        return fail(std::string("direct guidance provenance control ")+name+" age="+std::to_string(age));
                    sawTurn=sawTurn||change>1e-10;
                    controlRows<<hz<<','<<name<<','<<age<<','<<lock<<','<<change<<','<<probe.currentX<<','
                        <<probe.currentY<<','<<probe.currentZ<<','<<probe.launchSourceType<<','<<probe.launchSourceValidated
                        <<','<<probe.weaponId<<','<<probe.projectileId<<','<<probeType.courseLockDuration<<",PASS\n";
                }
                if(!sawTurn)return fail(std::string("direct guidance control never turned: ")+name);
                ++guidanceControls;
            }
        }
        const int launchFacing=actualLaunch.attackerFacing;
        eraseRuntimeBehavior(s,1);findMutableObjectById(s,1)->mission="Sleep";
        findMutableObjectById(s,1)->weaponCooldownTicksRemaining=100000;
        std::unique_ptr<RuntimeWindowState> restored,flightExact;
        std::map<int,RuntimeProjectileEffect> previous;std::set<int> seen,finished;
        std::set<int> images;double firstYaw=0,maxSide=0;bool haveYaw=false,turned=false,moved=false,removed=false;
        bool launchDiagnosticLogged=false;
        const bool hookCase=rangeCase==0&&(segment==0||segment==9);
        const double launchAngle=actualLaunch.attackerFacing*6.28318530717958647692/256.0;
        const double launchYaw=std::atan2(-std::cos(launchAngle)-std::sin(launchAngle),
            std::sin(launchAngle)-std::cos(launchAngle));
        const double launchDz=(actualLaunch.targetZ-actualLaunch.sourceZ)/runtimeMissileLevelsPerCell;
        const double launchHorizontal=std::hypot(actualLaunch.targetX-actualLaunch.sourceX,
            actualLaunch.targetY-actualLaunch.sourceY);
        const double launchPitch=!type->level&&std::abs(launchDz)*256.0>200.0
            ?std::atan2(launchDz,launchHorizontal):0.0;
        // Independent native-frame sum: one lepton initial velocity, then the
        // authored acceleration per 15Hz frame, capped at the original cruise.
        const double nativeCruise=actualLaunch.speedCellsPerTick*hz/15.0*256.0;
        const auto lockedDistanceCells=[&](double frames) {
            if(type->acceleration<=0)return frames*nativeCruise/256.0;
            const int whole=static_cast<int>(std::floor(frames));double leptons=0.0;
            const double initial=std::min(1.0,nativeCruise);
            for(int frame=1;frame<=whole;++frame)
                leptons+=std::min(nativeCruise,initial+frame*type->acceleration);
            leptons+=(frames-whole)*std::min(nativeCruise,initial+(whole+1)*type->acceleration);
            return leptons/256.0;
        };
        const double expectedLockSide=std::abs(std::sin(launchYaw)*std::cos(launchPitch)*lockedDistanceCells(3.0));
        bool sawLockEndpoint=false;
        const bool visual=rangeCase==0&&hz==30&&rank==0&&(segment==0||segment==4||segment==9);
        const double angle=launchFacing*6.28318530717958647692/256.0;
        const RuntimeCellPoint center{75+static_cast<int>(std::lround((std::sin(angle)-std::cos(angle))*5.65)),
            75+static_cast<int>(std::lround((-std::cos(angle)-std::sin(angle))*5.65))};
        if(visual) {
            // Inverse native 60x30 projection, including one tile half-width
            // and half-height beyond the actual 1280x800 viewport corners.
            for(int py:{-415,415})for(int px:{-670,670})
                if(!fixturePointReady(s,center.x+px/60.0+py/30.0,center.y-px/60.0+py/30.0))
                    return fail("native renderer viewport exceeds real flat terrain domain");
        }
        const auto step=[](RuntimeWindowState& q){RuntimeSnapshotActiveStateScope scope(q);++q.tick;
            advanceRuntimeCombatBursts(q);advanceRuntimeEffects(q);};
        for(int n=0;n<hz*12;++n) {
            step(s);
            if(restored) {
                step(*restored);++paired;
                const auto* a=findObjectById(s,2);const auto* b=findObjectById(*restored,2);
                if(!a||!b||a->health!=b->health||s.projectiles.size()!=restored->projectiles.size())
                    return fail("restored flight/damage differs");
                for(std::size_t i=0;i<s.projectiles.size();++i)
                    if(serializeRuntimeProjectileEffect(s.projectiles[i])!=serializeRuntimeProjectileEffect(restored->projectiles[i]))
                        return fail("restored phase/heading differs");
            }
            if(flightExact) {
                step(*flightExact);++exactPaired;
                const auto* a=findObjectById(s,2);const auto* b=findObjectById(*flightExact,2);
                if(!a||!b||a->health!=b->health||s.projectiles.size()!=flightExact->projectiles.size())
                    return fail("canonical in-flight restored flight/damage differs");
                for(std::size_t i=0;i<s.projectiles.size();++i)
                    if(serializeRuntimeProjectileEffect(s.projectiles[i])!=serializeRuntimeProjectileEffect(flightExact->projectiles[i]))
                        return fail("canonical in-flight pose/guidance/clock differs");
                if(s.recentExpiredProjectiles.size()!=flightExact->recentExpiredProjectiles.size())
                    return fail("canonical in-flight restored expiry count differs");
                for(std::size_t i=0;i<s.recentExpiredProjectiles.size();++i)
                    if(serializeRuntimeProjectileEffect(s.recentExpiredProjectiles[i])!=serializeRuntimeProjectileEffect(flightExact->recentExpiredProjectiles[i]))
                        return fail("canonical in-flight restored expiry record differs");
            }
            for(const auto& p:s.projectiles) {
                if(!fixturePointReady(s,p.currentX,p.currentY)||!fixturePointReady(s,p.sourceX,p.sourceY)
                    ||!fixturePointReady(s,p.targetX,p.targetY))return fail("flight left native flat terrain domain");
                seen.insert(p.actorId);
                maxSide=std::max(maxSide,std::abs(p.currentY-p.sourceY));
                if(p.renderIntent!="radialProjectile"||!p.homingInitialized)return fail("flight direction absent");
                if(const auto old=previous.find(p.actorId);old!=previous.end()) {
                    const double distance=std::hypot(std::hypot(p.currentX-old->second.currentX,p.currentY-old->second.currentY),
                        (p.currentZ-old->second.currentZ)/runtimeMissileLevelsPerCell);
                    if(distance>p.speedCellsPerTick+1e-8)return fail("flight jumped across the curved path");
                }
                previous[p.actorId]=p;
                const double age=p.elapsedTicks*15.0/hz;
                path<<scene<<','<<s.tick<<','<<p.actorId<<','<<age<<','<<p.currentX<<','<<p.currentY<<','<<p.currentZ
                    <<','<<p.homingYaw<<','<<p.homingPitch<<','<<p.remainingTicks<<','<<p.sourceX<<','<<p.sourceY
                    <<','<<p.sourceZ<<','<<p.targetX<<','<<p.targetY<<','<<p.targetZ<<','<<p.launchSourceType
                    <<','<<p.launchSourceValidated<<','<<p.weaponId<<','<<p.projectileId<<','<<p.renderIntent
                    <<','<<p.speedCellsPerTick<<','<<p.attackerFacing<<','<<p.damage<<'\n';
                if(p.actorId==first) {
                    if(!haveYaw){firstYaw=p.homingYaw;haveYaw=true;}
                    const double change=std::abs(std::remainder(p.homingYaw-firstYaw,6.28318530717958647692));
                    const double expectedLock=(rangeCase==1||rangeCase==3)?1.0:3.0;
                    if(age<expectedLock&&change>1e-10)return fail("turned before compact Aegis course lock elapsed");
                    if(hookCase&&age<=expectedLock+1e-10) {
                        const double travel=lockedDistanceCells(age);
                        const double expectedX=actualLaunch.sourceX+std::cos(launchYaw)*std::cos(launchPitch)*travel;
                        const double expectedY=actualLaunch.sourceY+std::sin(launchYaw)*std::cos(launchPitch)*travel;
                        const double expectedZ=actualLaunch.sourceZ+std::sin(launchPitch)*travel*runtimeMissileLevelsPerCell;
                        if(std::abs(p.currentX-expectedX)>1e-8||std::abs(p.currentY-expectedY)>1e-8
                            ||std::abs(p.currentZ-expectedZ)>1e-8)
                            return fail("locked radial path differs from native accelerated launch distance/direction");
                        sawLockEndpoint=sawLockEndpoint||std::abs(age-expectedLock)<1e-10;
                    }
                    turned=turned||change>.3;
                }
                const auto render=makeRuntimeProjectileRenderActor(p);
                if(!render.hasFlightDirection||std::abs(render.flightDirectionZ-std::sin(p.homingPitch)*runtimeMissileLevelsPerCell)>1e-10)
                    return fail("native sprite lost actual flight direction");
            }
            for(const auto& p:s.recentExpiredProjectiles)if(p.projectileId=="MedusaProjectile") {
                if(!fixturePointReady(s,p.currentX,p.currentY))return fail("impact left native flat terrain domain");
                seen.insert(p.actorId);finished.insert(p.actorId);
                maxSide=std::max(maxSide,std::abs(p.currentY-p.sourceY));
                RuntimeProjectileEffect decoded;
                if(!parseRuntimeProjectileEffect(serializeRuntimeProjectileEffect(p),decoded,runtimeProjectileSaveSchemaV6,true)
                    ||serializeRuntimeProjectileEffect(decoded)!=serializeRuntimeProjectileEffect(p))
                    return fail("expired missile clock cannot round-trip through multiplayer records");
                if(const auto old=previous.find(p.actorId);old!=previous.end()) {
                    const double distance=std::hypot(std::hypot(p.currentX-old->second.currentX,p.currentY-old->second.currentY),
                        (p.currentZ-old->second.currentZ)/runtimeMissileLevelsPerCell);
                    if(distance>old->second.speedCellsPerTick+1e-7)return fail("deadline teleported missile onto target");
                }
            }
            const double age=(s.tick-spawn)*15.0/hz;
            // Manual geometry relocation is explicitly limited to the old
            // eight-cell control. Near and far targets remain stationary.
            if(rangeCase==0&&segment==2&&!moved&&age>=4){auto* t=findMutableObjectById(s,2);if(!t)return fail("moving target lost");
                ++t->x;++t->y;t->cell=runtimeCellKey(t->x,t->y);clearRuntimeRenderPosition(*t);moved=true;}
            if(rangeCase!=3&&segment==7&&!removed&&age>=2
                &&static_cast<int>(seen.size())==expected&&!s.combatDetails.hasPendingBurst(1)){auto* t=findMutableObjectById(s,1);if(!t)return fail("source already lost");
                t->alive=false;t->health=0;removed=true;}
            // New two-cell targets are a stationary arrival control,
            // not an artificial checkpoint window. Near-four saves are early
            // flight (native 15Hz observes age2), not claimed as lock-phase.
            const double turnSaveAge=rangeCase==1?1.5:5.0;
            if(rangeCase!=3&&!restored&&((segment==0&&age>=1)||(segment==9&&age>=turnSaveAge))&&!s.projectiles.empty()) {
                std::ostringstream text;IniFile ini;restored=std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*restored,s,false);
                if(!loadFixtureRules(*restored))return fail("ordinary owner lifecycle reload");
                if(!writeRuntimeStateToStream(s,text,error)||!ini.loadFromString(text.str())
                    ||!materializeRuntimeLoadStateFromIni(*restored,ini,error)
                    ||!fixtureRulesReady(*restored)||!fixtureTerrainReady(*restored))return fail("in-flight save/lifecycle/hash/terrain: "+error);
                std::ofstream ordinaryFile(root+"/"+scene+"-ordinary14.ini");ordinaryFile<<text.str();
                if(!ordinaryFile.good())return fail("ordinary checkpoint artifact write failed");
                ++saves;
                const bool exactFocus=rangeCase==0&&((hz==30&&rank==0&&segment==0)||(hz==60&&rank==2&&segment==9));
                if(exactFocus) {
                    std::ostringstream extension,canonical;IniFile extensionIni;
                    if(!writeRuntimeStateToStream(s,extension,error)||!writeRuntimeMultiplayerSnapshotExtension(s,extension,error)
                        ||!extensionIni.loadFromString(extension.str())
                        ||!writeRuntimeMultiplayerSnapshotCanonicalIni(extensionIni,canonical,error)
                        ||!extensionIni.loadFromString(canonical.str()))return fail("canonical in-flight write: "+error);
                    flightExact=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*flightExact,s,false);
                    if(!loadFixtureRules(*flightExact))return fail("canonical owner lifecycle reload");
                    flightExact->projectiles.clear();flightExact->impactEffects.clear();flightExact->recentExpiredProjectiles.clear();
                    if(!materializeRuntimeLoadStateFromIni(*flightExact,ini,error)
                        ||!materializeRuntimeMultiplayerSnapshotExtension(*flightExact,extensionIni,error)
                        ||!fixtureRulesReady(*flightExact)||!fixtureTerrainReady(*flightExact))
                        return fail("canonical in-flight restore: "+error);
                    if(flightExact->projectiles.size()!=s.projectiles.size())return fail("canonical checkpoint lost live missiles");
                    for(std::size_t i=0;i<s.projectiles.size();++i)
                        if(serializeRuntimeProjectileEffect(s.projectiles[i])!=serializeRuntimeProjectileEffect(flightExact->projectiles[i]))
                            return fail("canonical checkpoint changed live missile record");
                    std::ofstream canonicalFile(root+"/"+scene+"-canonical-extension.ini");canonicalFile<<canonical.str();
                    if(!canonicalFile.good())return fail("canonical checkpoint artifact write failed");
                    ++inFlightSnapshots;
                }
            }
            if(visual&&!s.projectiles.empty()) {
                const int frame=static_cast<int>(age);
                // The native launch ramp can remain in one raster pixel at
                // frame1. Capture four forward-flight phases after departure;
                // each still requires a real directional body AND line trail.
                const bool wantedFrame=segment==4?(frame==8||frame==12||frame==16||frame==20)
                    :(frame==2||frame==4||frame==6||frame==8);
                const bool capture=wantedFrame&&images.insert(frame).second;
                const bool launchDiagnostic=frame==1&&!launchDiagnosticLogged;
                const std::string capturePath=capture?root+"/"+scene+"-frame"+std::to_string(frame)+".bmp"
                    :(launchDiagnostic?root+"/"+scene+"-launch-frame1.bmp":"");
                const auto stats=drawRuntimeStateOffscreenAtCell(s,center,1280,800,error,capturePath,false);
                if(!stats||!stats->rendered)return fail("native scene draw: "+error);
                if(capture||launchDiagnostic) {
                    launchDiagnosticLogged=launchDiagnosticLogged||launchDiagnostic;
                    RuntimeMapCamera camera;camera.initialized=true;
                    const auto projectedCenter=runtimeProjectCellXY(center.x,center.y,0,camera);
                    camera.worldOffsetX=projectedCenter.x-640;camera.worldOffsetY=projectedCenter.y-400;
                    const auto projectedTrailPoint=[&](const RuntimeLineTrailPoint& point) {
                        auto projected=runtimeProjectCellXY(point.x,point.y,point.z,camera);
                        projected.x+=runtimeMuzzlePixelX(point.offsetX,camera,point.fixedX)-camera.worldOffsetX;
                        projected.y+=runtimeMuzzlePixelY(point.offsetY,camera,point.fixedY)-camera.worldOffsetY;
                        return projected;
                    };
                    for(const auto& p:s.projectiles) {
                        RuntimeProjectedPoint start{-1,-1},head{-1,-1};std::size_t nodes=0;
                        if(const auto found=s.renderCache.projectileLineTrails.find(p.actorId);
                            found!=s.renderCache.projectileLineTrails.end()) {
                            const auto& trail=found->second;nodes=trail.points.size();
                            head=projectedTrailPoint(trail.head);
                            if(!trail.points.empty())start=projectedTrailPoint(trail.points.front());
                        }
                        const auto actor=makeRuntimeProjectileRenderActor(p);
                        const double screenDx=((p.currentX-p.sourceX)-(p.currentY-p.sourceY))*30.0;
                        const double screenDy=((p.currentX-p.sourceX)+(p.currentY-p.sourceY)-(p.currentZ-p.sourceZ))*15.0;
                        nativeRows<<scene<<','<<s.tick<<','<<age<<','<<p.actorId<<','<<capture
                            <<','<<stats->projectileActorTravelDrawn<<','<<stats->projectileActorTravelMissing
                            <<','<<stats->projectileActorDecodeMissing<<','<<stats->projectileActorPaletteMissing
                            <<','<<stats->projectileLineTrailSegments<<','<<stats->projectileLineTrailPixels
                            <<','<<nodes<<','<<start.x<<','<<start.y<<','<<head.x<<','<<head.y
                            <<','<<p.currentX<<','<<p.currentY<<','<<p.currentZ
                            <<','<<p.sourceX<<','<<p.sourceY<<','<<p.sourceZ
                            <<','<<screenDx<<','<<screenDy<<','<<actor.progress<<'\n';
                    }
                    nativeRows.flush();
                }
                if(capture) {
                    ++captures;
                    if(stats->projectileLineTrailPixels<=0||stats->projectileActorTravelDrawn<=0) {
                        const auto& p=s.projectiles.front();std::ostringstream why;why<<std::setprecision(17)
                            <<"MEDUSA body or line trail missing frame="<<frame<<" age="<<age
                            <<" body="<<stats->projectileActorTravelDrawn<<" bodyMissing="<<stats->projectileActorTravelMissing
                            <<" decodeMissing="<<stats->projectileActorDecodeMissing<<" paletteMissing="<<stats->projectileActorPaletteMissing
                            <<" trailSegments="<<stats->projectileLineTrailSegments<<" trailPixels="<<stats->projectileLineTrailPixels
                            <<" xyz="<<p.currentX<<','<<p.currentY<<','<<p.currentZ
                            <<" source="<<p.sourceX<<','<<p.sourceY<<','<<p.sourceZ<<" capture="<<capturePath;
                        return fail(why.str());
                    }
                }
            }
            if(s.projectiles.empty()&&s.combatDetails.bursts.empty())break;
        }
        const auto* finalTarget=findObjectById(s,2);
        if(!finalTarget||finalTarget->health>=100000||static_cast<int>(finished.size())!=expected
            ||static_cast<int>(seen.size())!=expected||!s.projectiles.empty()) {
            std::ostringstream why;why<<std::setprecision(17)<<"volley failed to arrive tick="<<s.tick
                <<" expected="<<expected<<" seen="<<seen.size()<<" finished="<<finished.size()
                <<" live="<<s.projectiles.size()<<" pendingBurst="<<s.combatDetails.hasPendingBurst(1);
            const auto* finalSource=findRuntimeObjectByIdIncludingInactive(s,1);
            if(finalSource)why<<" sourceHP="<<finalSource->health<<" sourceAlive="<<finalSource->alive
                <<" sourceXY="<<runtimeObjectRenderX(*finalSource)<<','<<runtimeObjectRenderY(*finalSource)
                <<" sourceDrawLevel="<<runtimeDrawLevelForObject(*finalSource);
            if(finalTarget) {
                const auto pose=runtimeProjectileTargetPosition(s,*finalTarget);
                why<<" targetHP="<<finalTarget->health<<" targetAlive="<<finalTarget->alive
                    <<" targetAir="<<runtimeObjectIsAirTarget(s,*finalTarget)<<" targetXYZ="<<pose.x<<','<<pose.y<<','<<pose.level
                    <<" targetGround="<<terrainLevelAtRuntimeCell(s,pose.cell.x,pose.cell.y)
                    <<" targetFlightLevel="<<finalTarget->flightLevel;
            }
            for(const auto& shot:s.projectiles)why<<" actor="<<shot.actorId<<" xyz="<<shot.currentX<<','<<shot.currentY<<','<<shot.currentZ
                <<" target="<<shot.targetX<<','<<shot.targetY<<','<<shot.targetZ<<" gap="
                <<std::hypot(std::hypot(shot.targetX-shot.currentX,shot.targetY-shot.currentY),
                    (shot.targetZ-shot.currentZ)/runtimeMissileLevelsPerCell)
                <<" speed="<<shot.speedCellsPerTick<<" yaw="<<shot.homingYaw<<" pitch="<<shot.homingPitch
                <<" age="<<shot.elapsedTicks*15.0/hz<<" remaining="<<shot.remainingTicks;
            return fail(why.str());
        }
        // The three-rule-frame lock now travels only the native launch ramp,
        // so its off-axis lower bound follows that distance rather than cruise.
        if(hookCase&&(!turned||!sawLockEndpoint||expectedLockSide<=1e-8||maxSide+1e-8<expectedLockSide))
            return fail("hook-shaped detour absent: side="+std::to_string(maxSide)
                +" native_lock_side="+std::to_string(expectedLockSide));
        if((rangeCase==1||rangeCase==3)&&maxSide>3.0)
            return fail("stationary near-target detour exceeds 3 cells: "+std::to_string(maxSide));
        if((rangeCase==0&&segment==2&&!moved)||(rangeCase!=3&&segment==7&&!removed)
            ||(rangeCase!=3&&(segment==0||segment==9)&&!restored))
            return fail("required geometry target move/source loss/in-flight checkpoint did not execute");
        if(rangeCase==0&&maxSide>7.5)return fail("8-cell compact detour exceeds 7.5 cells: "+std::to_string(maxSide));
        if(rangeCase==2&&maxSide>8.0)return fail("far/high compact detour exceeds 8 cells: "+std::to_string(maxSide));
        const auto cursor=s.combatDetails.fireCursors.find(1);
        if((cursor==s.combatDetails.fireCursors.end()&&!removed)
            ||(cursor!=s.combatDetails.fireCursors.end()&&cursor->second.radialSegment!=(segment+expected)%10))
            return fail("radial segment did not wrap");
        std::ostringstream snapshot;IniFile snapshotIni;
        auto after=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*after,s,false);
        if(!loadFixtureRules(*after))return fail("post-impact owner lifecycle reload");
        if(!writeRuntimeStateToStream(s,snapshot,error)||!writeRuntimeMultiplayerSnapshotExtension(s,snapshot,error)
            ||!snapshotIni.loadFromString(snapshot.str())||!materializeRuntimeLoadStateFromIni(*after,snapshotIni,error)
            ||!materializeRuntimeMultiplayerSnapshotExtension(*after,snapshotIni,error)
            ||!fixtureRulesReady(*after)||!fixtureRulesReady(s)||!fixtureTerrainReady(*after)||!fixtureTerrainReady(s))
            return fail("post-impact snapshot: "+error);
        if(after->recentExpiredProjectiles.size()!=s.recentExpiredProjectiles.size())return fail("snapshot lost expired missiles");
        for(std::size_t i=0;i<s.recentExpiredProjectiles.size();++i)
            if(serializeRuntimeProjectileEffect(after->recentExpiredProjectiles[i])
                !=serializeRuntimeProjectileEffect(s.recentExpiredProjectiles[i]))return fail("snapshot changed expired missile");
        ++postImpactSnapshots;
        std::cout<<"Aegis "<<scene<<" missiles="<<seen.size()<<" maxSide="<<maxSide
            <<" damage="<<100000-finalTarget->health<<" ticks="<<s.tick-spawn<<'\n';
        sceneRows<<scene<<','<<hz<<','<<rank<<','<<segment<<','<<distance<<','<<altitude<<",1,"<<seen.size()
            <<','<<maxSide<<','<<100000-finalTarget->health<<','<<s.tick-spawn<<",PASS\n";
        path.flush();sceneRows.flush();controlRows.flush();
        ++cases;missiles+=static_cast<int>(seen.size());
    }
    if(nativeOnly) {
        if(cases!=3||missiles!=3||saves!=2||inFlightSnapshots!=1||postImpactSnapshots!=3||guidanceControls!=6||captures!=12)
            return fail("incomplete isolated native diagnostic matrix");
        if(!path.good()||!sceneRows.good()||!controlRows.good()||!nativeRows.good())return fail("native diagnostic CSV write failed");
        std::cout<<"Aegis isolated native diagnostic passed: cases="<<cases<<" captures="<<captures
            <<" ordinarySaves="<<saves<<" canonicalSaves="<<inFlightSnapshots<<" guidanceControls="<<guidanceControls
            <<"; full 240-case acceptance requires CONCORD_AEGIS_TRAJECTORY_NATIVE_ONLY unset\n";
        return true;
    }
    if(cases!=240||missiles!=360||saves!=36||inFlightSnapshots!=2||postImpactSnapshots!=240||guidanceControls!=18||captures!=12)
        return fail("incomplete geometry/clock/save/control/native capture matrix");
    if(!path.good()||!sceneRows.good()||!controlRows.good()||!nativeRows.good())return fail("geometry CSV write failed");
    std::cout<<"Aegis trajectory passed: cases="<<cases<<" missiles="<<missiles<<" saves="<<saves
        <<" continuedTicks="<<paired<<" nativeCaptures="<<captures<<" postImpactSnapshots="<<postImpactSnapshots
        <<" inFlightCanonicalSnapshots="<<inFlightSnapshots<<" exactContinuedTicks="<<exactPaired
        <<" guidanceControls="<<guidanceControls<<" fixture=durable-geometry rawCourseLock=15 effectiveAegisLock=3 nearAegisLock=1 nativeFixtureSize=80 terrainCells=12720\n";
    return true;
}
