bool runRuntimeOpenToppedAnimSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& why){error="OpenToppedAnim: "+why;return false;};
    const char* requested=std::getenv("CONCORD_OPEN_TOPPED_ANIM_DIR");
    const std::string root=requested && *requested ? requested
        : "build/smoke/feature-checks/12-open-topped-anim/checks";
    std::filesystem::create_directories(root);
    auto stock=std::make_unique<RuntimeWindowState>(); stock->config=config;
    stock->config.defaultMapPath="local_data/resources/mix_exports/multimd/2peaks.map";
    stock->report=buildRuntimeStartupReport(stock->config,&stock->world,&stock->theater,
        &stock->art,&stock->rules,&stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    for (const char* id:{"PsychicJab","PsychicJabE","Virusgun","VirusgunE"}) {
        const auto* w=stock->rules.findWeaponType(id);
        if (!w || w->openToppedAnim!="GUNFIRE" || !w->anim.empty()) return fail(std::string("stock weapon ")+id);
    }
    const std::array<const char*,3> types{"INIT","VIRUS","E1"};
    struct Fixture {std::unique_ptr<RuntimeWindowState> box;int host=-1,shooter=-1,target=-1;};
    const auto make=[&](int hz,int type,bool elite,int level) {
        Fixture f;f.box=std::make_unique<RuntimeWindowState>();auto& s=*f.box;
        s.config=stock->config;s.config.fixedTickHz=hz;s.config.playerSlots.clear();
        s.config.computerAiEnabled=s.config.noCooldown=s.config.multiplayerActive=s.config.crates=false;
        s.config.shortGame=s.config.superweapons=false;s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.report=stock->report;s.rules=stock->rules;s.art=stock->art;s.theater=stock->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.runtimePreferencePersistenceEnabled=s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.economy.credits=10000;s.economy.techLevel=10;initializeRuntimeLocalProductionLanes(s.economy);
        for (int y=1;y<=32;++y) for (int x=1;x<=32;++x) {
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=c.subTileIndex=0;c.level=level;
            s.world.accessTerrainCells().push_back(c);
        }
        auto* host=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"BFRT",{16,16});
        f.host=host->instanceId;host->level=level;host->mission="Guard";clearRuntimeRenderPosition(*host);
        f.shooter=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,types[type],{16,17})->instanceId;
        findMutableObjectById(s,f.shooter)->veterancy=elite?2:0;
        f.target=addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Infantry,"E2",{19,16})->instanceId;
        auto* target=findMutableObjectById(s,f.target);target->health=100000;target->mission="Sleep";target->level=level;
        initializeRuntimeNavigation(s);rebuildRuntimeEconomyPower(s);return f;
    };
    const auto board=[&](Fixture& f,int id) {
        auto& s=*f.box;applySelection(s,{id},false,false);
        if (!queueRuntimeTransportTarget(s,*findMutableObjectById(s,f.host)) || !waitForRuntimeTransportSmokeMotion(s,f.host,id)) return false;
        eraseRuntimeBehavior(s,id);return true;
    };
    const auto shoot=[&](Fixture& f) {
        auto& s=*f.box;auto* source=findMutableRuntimeObjectByIdIncludingInactive(s,f.shooter);
        const auto* host=findObjectById(s,f.host);const auto* target=findObjectById(s,f.target);
        auto pose=runtimeCombatFiringPose(*source,host);
        auto profile=runtimeCombatPassengerWeaponProfile(s,pose,target);
        source->weaponCooldownTicksRemaining=0;s.projectiles.clear();s.impactEffects.clear();
        return profile.weapon && fireRuntimeIndependentCombatWeapon(s,*source,host,*target,profile);
    };
    const auto closeEnough=[](double a,double b){return std::abs(a-b)<1e-8;};
    RuntimeMapRenderContext context;context.rules=&stock->rules;context.art=&stock->art;context.theater=&stock->theater;
    context.assetRootPath=runtimeAssetRootPath(stock->config);
    context.structureRootPath=runtimeStructureRootPath(stock->config,stock->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(stock->config,stock->report.map);
    context.palettePath=runtimePalettePath(stock->config,stock->report.map);
    context.tileRootPath=runtimeTileRootPath(stock->config,stock->report.map);
    std::vector<RuntimeImpactRenderActor> renderSamples;std::vector<WorldObjectInstance> renderHosts;
    std::ofstream shotsFile(root+"/transport-shots.csv"),timelineFile(root+"/transport-timeline.csv");
    shotsFile<<"type,hz,elite,level,direction,firing_facing,weapon,anim,frames,ticks,x,y,z\n";
    timelineFile<<"type,hz,elapsed,frame,remaining\n";
    int shots=0,scheduled=0,attachment=0,snapshots=0,timeline=0,boundaries=0,renders=0;
    static constexpr int offsets[8][2]={{0,-3},{3,-3},{3,0},{3,3},{0,3},{-3,3},{-3,0},{-3,-3}};
    for (int type=0;type<3;++type) for (int hz:{30,60}) for (bool elite:{false,true})
        for (int level:{0,2}) for (int direction=0;direction<8;++direction) {
        auto f=make(hz,type,elite,level);auto& s=*f.box;
        if (!board(f,f.shooter)) return fail("real boarding");
        auto* host=findMutableObjectById(s,f.host);host->facing=direction*32;
        if(host->level!=level)return fail("fixture terrain height was not applied");
        auto* target=findMutableObjectById(s,f.target);target->x=host->x+offsets[direction][0];
        target->y=host->y+offsets[direction][1];target->cell=runtimeCellKey(target->x,target->y);clearRuntimeRenderPosition(*target);
        if (!shoot(f) || s.projectiles.size()!=1) return fail("single passenger discharge");
        const auto shot=s.projectiles.front();
        const std::string expected=type==0?(elite?"PsychicJabE":"PsychicJab"):
            type==1?(elite?"VirusgunE":"Virusgun"):(elite?"ParaE":"Para");
        if(shot.weaponId!=expected)return fail("weapon selection actual="+shot.weaponId+" expected="+expected);
        // Stock GI explicitly selects its secondary aboard. ParaE has neither
        // Anim nor OpenToppedAnim; inventing a flash would differ from retail.
        if(type==2 && elite) {
            if(!s.impactEffects.empty() || !shot.weaponAnimId.empty())return fail("stock ParaE gained an unauthored flash");
            shotsFile<<types[type]<<','<<hz<<','<<elite<<','<<level<<','<<direction<<','<<shot.attackerFacing<<','
                <<shot.weaponId<<",<none>,0,0,"<<shot.sourceX<<','<<shot.sourceY<<','<<shot.sourceZ<<'\n';
            ++shots;++boundaries;continue;
        }
        if(s.impactEffects.size()!=1)return fail("expected one passenger muzzle for "+expected);
        const auto initial=s.impactEffects.front();
        if (shot.weaponId!=expected || initial.lifecycleState!="TransportMuzzle"
            || (type<2?initial.animId!="GUNFIRE":initial.animId.rfind("MGUN-",0)!=0)
            || initial.animationFrameCount<2 || initial.animationFrameDelayTicks!=hz/15
            || initial.totalTicks!=initial.animationSequenceSteps*(hz/15)+1
            || initial.muzzleFacing!=shot.attackerFacing
            || initial.screenOffsetX!=shot.sourceScreenOffsetX || initial.screenOffsetY!=shot.sourceScreenOffsetY
            || initial.nativeFixedX!=shot.sourceNativeFixedX || initial.nativeFixedY!=shot.sourceNativeFixedY
            || !makeRuntimeProjectileRenderActor(shot,&s.rules,&s.art).weaponAnimId.empty())
            return fail("original muzzle contract: "+expected+" anim="+initial.animId+" lifecycle="+initial.lifecycleState
                +" frames="+std::to_string(initial.animationFrameCount)+" delay="+std::to_string(initial.animationFrameDelayTicks)
                +" sequence="+std::to_string(initial.animationSequenceSteps)+" total="+std::to_string(initial.totalTicks));
        // Visual projection never changes the authoritative authored port.
        if (!closeEnough(initial.worldX,shot.sourceX) || !closeEnough(initial.worldY,shot.sourceY)
            || !closeEnough(initial.worldLevel,shot.sourceZ))
            return fail("launch alignment changed");
        shotsFile<<types[type]<<','<<hz<<','<<elite<<','<<level<<','<<direction<<','<<shot.attackerFacing<<','
            <<shot.weaponId<<','<<initial.animId<<','<<initial.animationFrameCount<<','<<initial.totalTicks<<','
            <<initial.worldX<<','<<initial.worldY<<','<<initial.worldLevel<<'\n';++shots;
        const auto* actors=runtimeImpactRenderActorsForRender(s);
        if (!actors || actors->size()!=1) return fail("initial render actor");
        if (hz==30 && !elite && level==0) {renderSamples.push_back(actors->front());renderHosts.push_back(*host);}
        RuntimeImpactEffect restored;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(initial),restored)
            || !restored.animId.empty() || restored.screenOffsetX!=initial.screenOffsetX
            || restored.screenOffsetY!=initial.screenOffsetY
            || restored.nativeFixedX!=initial.nativeFixedX || restored.nativeFixedY!=initial.nativeFixedY) return fail("transport snapshot parse");
        std::uint64_t a=0,b=0;runtimeMultiplayerAuthoritativeMixImpact(a,initial);runtimeMultiplayerAuthoritativeMixImpact(b,restored);
        if (a!=b) return fail("transport snapshot authoritative hash");
        s.impactEffects[0]=restored;actors=runtimeImpactRenderActorsForRender(s);
        if (!actors || actors->size()!=1 || actors->front().animId!=initial.animId
            || !closeEnough(actors->front().x,initial.worldX) || !closeEnough(actors->front().y,initial.worldY)) return fail("rehydrated muzzle");
        ++snapshots;
        // Rendering between updates and the next simulation update must agree.
        host->hasRenderPosition=true;host->renderMoveActive=true;host->renderX=host->x+.25;host->renderY=host->y-.125;
        host->renderLevel=level+.5;host->facing=(host->facing+64)%256;
        actors=runtimeImpactRenderActorsForRender(s);
        if (!actors || actors->size()!=1 || !closeEnough(actors->front().x,initial.worldX+.25)
            || !closeEnough(actors->front().y,initial.worldY-.125) || !closeEnough(actors->front().level,initial.worldLevel+.5)
            || actors->front().animId!=initial.animId) return fail("moving/turning host attachment: "+expected+" level="+std::to_string(level)+" direction="+std::to_string(direction));
        s.projectiles.clear();advanceRuntimeEffects(s);
        if (s.impactEffects.size()!=1 || !closeEnough(s.impactEffects.front().worldX,initial.worldX+.25)
            || !closeEnough(s.impactEffects.front().worldY,initial.worldY-.125)) return fail("attachment update doubled movement");
        ++attachment;
    }
    std::cout<<"Passenger shots, snapshot rehydration and movement passed: "<<shots<<'\n';
    if (!runRuntimeOpenToppedAnimRenderCheck(context,renderHosts,renderSamples,root,renders,error)) return false;
    std::cout<<"Passenger original sprite render passed: "<<renders<<'\n';
    // Real automatic and ordered mixed manifests: prohibited Brute fire stays prohibited.
    for (int hz:{30,60}) for (bool elite:{false,true}) for (bool ordered:{false,true}) {
        auto f=make(hz,0,elite,0);auto& s=*f.box;std::vector<int> passengers{f.shooter};
        for (const char* type:{"VIRUS","E1","BRUTE"}) {
            auto* unit=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,type,{17,17});
            unit->veterancy=elite?2:0;passengers.push_back(unit->instanceId);
        }
        for (int id:passengers) if (!board(f,id)) return fail("mixed manifest boarding");
        if (ordered) {applySelection(s,{f.host},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,f.target));}
        else {RuntimeObjectBehavior move;move.kind=RuntimeBehaviorKind::Move;s.behaviors[f.host]=move;}
        advanceRuntimeCombatIndependentFire(s);
        for (int i=0;i<4;++i) {
            const auto shot=std::find_if(s.projectiles.begin(),s.projectiles.end(),[&](const auto& p){return p.attackerObjectId==passengers[i];});
            const auto effect=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& p){return p.attackerObjectId==passengers[i];});
            if (i==3) {if (shot!=s.projectiles.end() || effect!=s.impactEffects.end()) return fail("forbidden Brute flash/fire");}
            else if (i==2 && elite) {
                if(shot==s.projectiles.end() || shot->weaponId!="ParaE" || effect!=s.impactEffects.end())return fail("mixed elite GI empty animation");
            }
            else if (shot==s.projectiles.end() || effect==s.impactEffects.end()
                || effect->lifecycleState!="TransportMuzzle" || (i<2 && effect->animId!="GUNFIRE")) return fail("mixed automatic/ordered passenger fire");
            ++scheduled;
        }
    }
    std::cout<<"Passenger mixed scheduler passed: "<<scheduled<<'\n';
    for (int hz:{30,60}) for (int type=0;type<3;++type) {
        auto f=make(hz,type,false,0);auto& s=*f.box;
        if (!board(f,f.shooter) || !shoot(f)) return fail("timeline discharge");
        const auto initial=s.impactEffects.front();s.projectiles.clear();
        for (int elapsed=1;elapsed<=initial.totalTicks;++elapsed) {
            advanceRuntimeEffects(s);++s.tick;
            if (elapsed==initial.totalTicks) {if (!s.impactEffects.empty()) return fail("flash did not expire");}
            else {
                if (s.impactEffects.size()!=1 || s.impactEffects.front().frameIndex!=(elapsed-1)/(hz/15)) return fail("frame duration/creation skip");
                timelineFile<<types[type]<<','<<hz<<','<<elapsed<<','<<s.impactEffects.front().frameIndex<<','<<s.impactEffects.front().remainingTicks<<'\n';
            }
            ++timeline;
        }
        if (!shoot(f)) return fail("unload discharge");
        const auto before=s.impactEffects.front();s.projectiles.clear();
        if (unloadRuntimeTransport(s,*findMutableObjectById(s,f.host))!=1) return fail("real unload");
        auto* source=findMutableObjectById(s,f.shooter);if (!source) return fail("unloaded owner missing");
        const auto sub=worldObjectSubCellOffset(*source);const auto* actors=runtimeImpactRenderActorsForRender(s);
        if (!actors || actors->size()!=1 || !closeEnough(actors->front().x,before.worldX+runtimeObjectRenderX(*source)+sub.x-before.muzzleAnchorX)
            || !closeEnough(actors->front().y,before.worldY+runtimeObjectRenderY(*source)+sub.y-before.muzzleAnchorY)) return fail("flash did not follow unloaded owner");
        source->health=0;actors=runtimeImpactRenderActorsForRender(s);
        if (actors && !actors->empty()) return fail("dead owner flash still drawn");
        advanceRuntimeEffects(s);if (!s.impactEffects.empty()) return fail("dead owner flash not removed");
        attachment+=2;
        // A fresh ground discharge has no OpenToppedAnim fallback.
        auto g=make(hz,type,false,0);auto& ground=*g.box;
        const auto profile=runtimeWeaponProfileForObject(ground,*findObjectById(ground,g.shooter),findObjectById(ground,g.target));
        spawnRuntimeProjectile(ground,*findObjectById(ground,g.shooter),*findObjectById(ground,g.target),*profile.weapon,profile,1);
        if (type<2 ? !ground.impactEffects.empty() : ground.impactEffects.empty() || ground.impactEffects.front().lifecycleState!="Muzzle") return fail("ground muzzle behavior changed");
        ++boundaries;
    }
    {
        std::uint64_t defaultHash=0;
        for (const char* anim:{"","GUNFIRE"}) {
            IniFile ini;RulesDatabase rules;ini.loadFromString("[InfantryTypes]\n0=S\n[S]\nPrimary=G\n[G]\nOpenToppedAnim="+std::string(anim)+"\n");
            if (!rules.loadFromIni(ini) || rules.findWeaponType("G")->openToppedAnim!=anim) return fail("INI parsing");
            if (!*anim) defaultHash=rules.getDetailRulesHash();else if (defaultHash==rules.getDetailRulesHash()) return fail("field absent from rules hash");++boundaries;
        }
        auto f=make(30,0,false,0);auto& s=*f.box;if (!board(f,f.shooter)) return fail("boundary boarding");
        auto* weapon=const_cast<WeaponType*>(s.rules.findWeaponType("PsychicJab"));
        for (const char* ordinary:{"MGUN-N","none","<none>"}) {
            weapon->anim=ordinary;if (!shoot(f) || s.impactEffects.size()!=1
                || s.impactEffects.front().animId!=(std::string(ordinary)=="MGUN-N"?"MGUN-N":"GUNFIRE")) return fail("explicit normal Anim priority/null handling");++boundaries;
        }
        weapon->anim.clear();
        for (const char* absent:{"","none","<none>"}) {
            weapon->openToppedAnim=absent;if (!shoot(f) || !s.impactEffects.empty()) return fail("empty fallback created a flash");++boundaries;
        }
        weapon->openToppedAnim="GUNFIRE";if (!shoot(f)) return fail("snapshot boundary shot");
        const auto original=s.impactEffects.front();const auto encoded=runtimeMultiplayerSnapshotImpactRecord(original);
        const auto fields=splitRuntimeSaveFields(encoded);if (fields.size()!=49) return fail("snapshot extension width");
        const auto join=[](const std::vector<std::string>& f,std::size_t n){std::string out;for(std::size_t i=0;i<n;++i){if(i)out+='|';out+=f[i];}return out;};
        for (std::size_t width:{36U,39U}) {
            auto legacy=fields;legacy[26]=iniHexEncode("Muzzle");RuntimeImpactEffect decoded;
            if (!parseRuntimeMultiplayerSnapshotImpactRecord(join(legacy,width),decoded) || decoded.muzzleFacing!=-1 || decoded.muzzleAnchorX!=0) return fail("legacy impact record rejected");++snapshots;
        }
        for (int corruption=0;corruption<4;++corruption) {
            auto invalid=fields;if(corruption==0)invalid[42]="256";if(corruption==1)invalid[42]="-1";
            if(corruption==2)invalid[39]=std::to_string(runtimeMultiplayerSnapshotCanonicalDoubleBits(std::numeric_limits<double>::infinity()));
            RuntimeImpactEffect decoded;
            if(parseRuntimeMultiplayerSnapshotImpactRecord(join(invalid,corruption==3?40:43),decoded))return fail("invalid transport snapshot accepted");++snapshots;
        }
        findMutableObjectById(s,f.host)->health=0;
        s.projectiles.clear();advanceRuntimeEffects(s);if(!s.impactEffects.empty())return fail("destroyed host retained passenger flash");++attachment;
    }
    // Stock fallback weapons are single shot; retain the same choice on a custom burst and save/load.
    for (int hz:{30,60}) {
        auto f=make(hz,0,false,0);auto& s=*f.box;if(!board(f,f.shooter))return fail("burst boarding");
        const_cast<WeaponType*>(s.rules.findWeaponType("PsychicJab"))->burst=3;
        if(!shoot(f) || !s.combatDetails.hasPendingBurst(f.shooter))return fail("custom burst start");
        const auto path=root+"/transport-burst-"+std::to_string(hz)+".ini";
        if(!saveRuntimeState(s,path,error) || !loadRuntimeState(s,path,error))return fail("full burst save/load: "+error);
        for(int round=1;round<3;++round) {
            s.tick=s.combatDetails.bursts.at(f.shooter).nextFireTick;s.projectiles.clear();s.impactEffects.clear();
            if(!advanceRuntimeCombatBursts(s) || s.impactEffects.size()!=1 || s.impactEffects.front().animId!="GUNFIRE"
                || s.impactEffects.front().lifecycleState!="TransportMuzzle")return fail("saved burst lost passenger fallback");++boundaries;
        }
        if(s.combatDetails.hasPendingBurst(f.shooter))return fail("finished burst remained pending");
    }
    std::cout<<"OpenToppedAnim passed: shots="<<shots<<" scheduled="<<scheduled<<" attachment="<<attachment
        <<" snapshots="<<snapshots<<" timeline="<<timeline<<" render="<<renders<<" boundaries="<<boundaries
        <<"; original normal/elite weapons, eight shot directions, elevations, 30/60 Hz, mixed manifests, "
        <<"movement/unload/death, actual SHP pixels, old/new snapshots, saved bursts and ground controls\n";
    return true;
}
