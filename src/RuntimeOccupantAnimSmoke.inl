bool runRuntimeOccupantAnimSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& message) { error = "OccupantAnim: " + message; return false; };
    const char* requested = std::getenv("CONCORD_OCCUPANT_ANIM_DIR");
    const std::string root = requested && *requested ? requested
        : "build/smoke/feature-checks/11-occupant-anim/checks";
    std::filesystem::create_directories(root);
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    const std::array<const char*, 3> infantry{"E1", "E2", "INIT"};
    const std::array<const char*, 3> animations{"UCFLASH", "UCCONS", "UCINIT"};
    // Copied from the untouched stock ART, independently of the new parser.
    struct Building { const char* id; std::vector<POINT> windows; };
    const Building buildings[] = {
        {"NABNKR", {{5,18},{-14,15},{21,9},{-21,10},{11,17}}},
        {"CAHSE01", {{-30,26},{40,34},{24,-4},{-4,30},{10,45}}},
        {"CAPRS03", {{11,89},{104,97},{-103,39},{-58,71},{138,73},
            {64,130},{73,126},{114,105},{-66,52},{-83,50}}}
    };
    struct Fixture { std::unique_ptr<RuntimeWindowState> box; int host, target; std::vector<int> occupants; };
    const auto make = [&](int hz, const Building& building, bool elite) {
        Fixture f; f.box = std::make_unique<RuntimeWindowState>(); auto& s = *f.box;
        s.config = stock->config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.computerAiEnabled = s.config.noCooldown = s.config.crates = s.config.multiplayerActive = false;
        s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.report = stock->report; s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.triggers.bootstrapComplete = true;
        s.economy.owner = "Americans"; s.economy.credits = 10000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; initializeRuntimeLocalProductionLanes(s.economy);
        for (int y=1;y<=40;++y) for (int x=1;x<=40;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
        }
        auto* host=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,building.id,{16,16});
        if (!host) return f;
        f.host=host->instanceId; host->mission="Guard";
        host->buildAnimTicksTotal=host->buildAnimTicksRemaining=0; clearRuntimeRenderPosition(*host);
        for (std::size_t i=0;i<building.windows.size();++i) {
            auto* object=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,infantry[i%3],{4+static_cast<int>(i),4});
            if (!object) return f;
            object->veterancy=elite?2:0; object->mission="Guard"; clearRuntimeRenderPosition(*object);
            f.occupants.push_back(object->instanceId);
        }
        auto* target=addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Unit,"MTNK",{13,16});
        if (!target) return f;
        f.target=target->instanceId; target->health=100000; target->mission="Sleep";
        initializeRuntimeNavigation(s); ensureRuntimeGarrisonsInitialized(s);
        for (int id:f.occupants) {
            auto* unit=findMutableObjectById(s,id);
            unit->x=16; unit->y=16; unit->cell=runtimeCellKey(16,16);
            clearRuntimeRenderPosition(*unit);
            s.pendingOccupyTargets[id]=f.host;
            if (!completeRuntimeOccupyEntry(s,id)) { f.occupants.clear(); break; }
        }
        return f;
    };
    RuntimeMapRenderContext context;
    context.rules=&stock->rules; context.art=&stock->art; context.theater=&stock->theater;
    context.assetRootPath=runtimeAssetRootPath(stock->config);
    context.structureRootPath=runtimeStructureRootPath(stock->config,stock->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(stock->config,stock->report.map);
    context.palettePath=runtimePalettePath(stock->config,stock->report.map);
    context.tileRootPath=runtimeTileRootPath(stock->config,stock->report.map);
    std::ofstream records(root+"/shots.csv"), timeline(root+"/timeline.csv");
    records<<"building,hz,elite,infantry,window,facing,anim,source_x,source_y,source_z,frames,total_ticks\n";
    timeline<<"anim,hz,elapsed,frame,remaining\n";
    int shots=0, scheduled=0, timelineChecks=0, renderChecks=0, boundaryChecks=0;
    for (const auto& building:buildings) {
        const ArtType* art=stock->art.findArtType(building.id);
        if (!art) return fail("stock building ART missing");
        for (std::size_t i=0;i<building.windows.size();++i)
            if (art->muzzleFlashes[i].x!=building.windows[i].x || art->muzzleFlashes[i].y!=building.windows[i].y)
                return fail(std::string(building.id)+" window parser "+std::to_string(i));
        std::vector<RuntimeImpactRenderActor> renderSamples;
        WorldObjectInstance renderHost;
        for (int hz:{30,60}) for (bool elite:{false,true}) {
            auto f=make(hz,building,elite); auto& s=*f.box;
            if (f.occupants.size()!=building.windows.size()) return fail("real mixed garrison entry");
            auto* host=findMutableObjectById(s,f.host); const auto* target=findObjectById(s,f.target);
            RuntimeObjectBehavior attack; attack.kind=RuntimeBehaviorKind::Attack; attack.targetObjectId=f.target;
            for (std::size_t index=0;index<f.occupants.size();++index) {
                s.projectiles.clear(); s.impactEffects.clear();
                bool handled=false;
                for (int attempt=0;attempt<300 && s.projectiles.empty();++attempt) {
                    s.tick=std::max(s.tick+1,s.garrisons.nextEligibleFireTick(f.host));
                    advanceRuntimeGarrisonAttackBehavior(s,*host,attack,handled);
                }
                if (!handled || s.projectiles.size()!=1 || s.impactEffects.size()!=1
                    || s.impactEffects.front().animId!=animations[index%3]
                    || host->garrisonFireCursor!=static_cast<int>((index+1)%f.occupants.size()))
                    return fail(std::string(building.id)+" actual mixed scheduler index="+std::to_string(index));
                const auto& effect=s.impactEffects.front(); const auto expected=building.windows[index];
                if (std::abs((effect.worldX-effect.worldY-(host->x-host->y))*30-expected.x)>.2
                    || std::abs((effect.worldX+effect.worldY-(host->x+host->y))*15-expected.y)>.2)
                    return fail("actual scheduler selected wrong muzzle window");
                ++scheduled;
            }
            for (int type=0;type<3;++type) for (int facing=0;facing<8;++facing)
                for (std::size_t index=0;index<building.windows.size();++index) {
                auto* occupant=findMutableRuntimeObjectByIdIncludingInactive(s,f.occupants[type]);
                RuntimeWeaponProfile profile=runtimeGarrisonWeaponProfileForOccupant(s,*occupant,target);
                profile.occupantMuzzleIndex=static_cast<int>(index);
                if (!profile.weapon || profile.weapon->occupantAnim!=animations[type]
                    || profile.slot!=(elite?RuntimeWeaponSlot::EliteOccupy:RuntimeWeaponSlot::Occupy))
                    return fail("original normal/elite OccupantAnim selection");
                WorldObjectInstance pose=*host; pose.facing=facing*32; pose.level=2;
                clearRuntimeRenderPosition(pose);
                s.projectiles.clear(); s.impactEffects.clear();
                // Move the actual shot endpoint around the host; merely changing
                // the building facing would leave the computed firing direction unchanged.
                static constexpr int targetOffsets[8][2]={{0,-5},{5,-5},{5,0},{5,5},
                    {0,5},{-5,5},{-5,0},{-5,-5}};
                WorldObjectInstance directedTarget=*target;
                directedTarget.x=pose.x+targetOffsets[facing][0];
                directedTarget.y=pose.y+targetOffsets[facing][1];
                directedTarget.cell=runtimeCellKey(directedTarget.x,directedTarget.y);
                clearRuntimeRenderPosition(directedTarget);
                spawnRuntimeProjectile(s,pose,directedTarget,*profile.weapon,profile,1);
                if (s.projectiles.size()!=1 || s.impactEffects.size()!=1) return fail("one shot did not create one muzzle");
                const auto& projectile=s.projectiles.front(); const auto& effect=s.impactEffects.front();
                const auto expected=building.windows[index];
                if (effect.animId!=animations[type] || effect.lifecycleState!="OccupantMuzzle"
                    || effect.renderIntent!="weaponAnim" || effect.frameIndex!=0
                    || effect.worldLevel!=2 || effect.worldX!=projectile.sourceX || effect.worldY!=projectile.sourceY
                    || std::abs((effect.worldX-effect.worldY-(pose.x-pose.y))*30-expected.x)>.2
                    || std::abs((effect.worldX+effect.worldY-(pose.x+pose.y))*15-expected.y)>.2
                    || effect.animationFrameCount<=0 || effect.animationFrameDelayTicks!=hz/15
                    || effect.totalTicks!=effect.animationFrameCount*(hz/15)+1
                    || !makeRuntimeProjectileRenderActor(projectile,&s.rules,&s.art).weaponAnimId.empty())
                    return fail("window/timing/launch/double-draw contract "+std::string(building.id)+"/"+animations[type]);
                records<<building.id<<','<<hz<<','<<elite<<','<<infantry[type]<<','<<index<<','<<projectile.attackerFacing<<','
                    <<effect.animId<<','<<effect.worldX<<','<<effect.worldY<<','<<effect.worldLevel<<','
                    <<effect.animationFrameCount<<','<<effect.totalTicks<<'\n';
                RuntimeImpactEffect restored;
                if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(effect),restored)
                    || !restored.animId.empty()) return fail("impact record parse");
                restored.remainingTicks-=restored.animationFrameDelayTicks+1;
                s.impactEffects[0]=restored;
                const auto* restoredActors=runtimeImpactRenderActorsForRender(s);
                if (!restoredActors || restoredActors->size()!=1 || restoredActors->front().animId!=animations[type]
                    || restoredActors->front().frameIndex!=1 || !restoredActors->front().centerOnAnchor
                    || restoredActors->front().x!=projectile.sourceX || restoredActors->front().y!=projectile.sourceY)
                    return fail("snapshot lost animation/window/phase");
                if (hz==30 && !elite && facing==0) {
                    auto actor=restoredActors->front(); actor.frameIndex=0; actor.remainingTicks=actor.totalTicks;
                    actor.level=0; renderSamples.push_back(actor); renderHost=*host;
                }
                ++shots;
            }
        }
        if (!runRuntimeOccupantAnimRenderCheck(context,renderHost,renderSamples,root,renderChecks,error)) return false;
    }
    // Real effect updates, including the creation pass, must visit every SHP
    // frame and expire without looping or changing position.
    for (int hz:{30,60}) for (int type=0;type<3;++type) {
        auto f=make(hz,buildings[0],false); auto& s=*f.box;
        auto profile=runtimeGarrisonWeaponProfileForOccupant(s,*findRuntimeObjectByIdIncludingInactive(s,f.occupants[type]),
            findObjectById(s,f.target)); profile.occupantMuzzleIndex=type;
        spawnRuntimeProjectile(s,*findObjectById(s,f.host),*findObjectById(s,f.target),*profile.weapon,profile,1);
        if (s.impactEffects.size()!=1) return fail("timeline launch");
        const auto initial=s.impactEffects.front(); s.projectiles.clear();
        for (int elapsed=1;elapsed<=initial.totalTicks;++elapsed) {
            advanceRuntimeEffects(s); ++s.tick;
            if (elapsed==initial.totalTicks) {
                if (!s.impactEffects.empty()) return fail("muzzle survived its last frame");
            } else {
                if (s.impactEffects.size()!=1 || s.impactEffects.front().frameIndex!=(elapsed-1)/(hz/15)
                    || s.impactEffects.front().worldX!=initial.worldX || s.impactEffects.front().worldY!=initial.worldY)
                    return fail("actual frame advancement drifted");
                timeline<<initial.animId<<','<<hz<<','<<elapsed<<','<<s.impactEffects.front().frameIndex<<','
                    <<s.impactEffects.front().remainingTicks<<'\n';
            }
            ++timelineChecks;
        }
    }
    // Null OccupantAnim suppresses ordinary Anim; ordinary firing retains its
    // directional Anim list. Boundary indices and absent ART points stay safe.
    {
        auto f=make(30,buildings[0],false); auto& s=*f.box;
        auto profile=runtimeGarrisonWeaponProfileForOccupant(s,*findRuntimeObjectByIdIncludingInactive(s,f.occupants[0]),
            findObjectById(s,f.target)); profile.occupantMuzzleIndex=0;
        auto* weapon=const_cast<WeaponType*>(profile.weapon); const auto saved=*weapon;
        std::uint64_t defaultHash=0;
        for (const char* value:{"","UCFLASH"}) {
            IniFile ini; RulesDatabase parsed;
            ini.loadFromString("[InfantryTypes]\n0=TEST\n[TEST]\nPrimary=GUN\n[GUN]\nOccupantAnim="+std::string(value)+"\n");
            if (!parsed.loadFromIni(ini) || !parsed.findWeaponType("GUN")
                || parsed.findWeaponType("GUN")->occupantAnim!=value) return fail("weapon default/field parser");
            if (!*value) defaultHash=parsed.getDetailRulesHash();
            else if (parsed.getDetailRulesHash()==defaultHash) return fail("OccupantAnim missing from rules hash");
        }
        ++boundaryChecks;
        for (const char* value:{"","none","<none>"}) {
            weapon->occupantAnim=value; s.projectiles.clear(); s.impactEffects.clear();
            spawnRuntimeProjectile(s,*findObjectById(s,f.host),*findObjectById(s,f.target),*weapon,profile,1);
            if (s.projectiles.size()!=1 || !s.impactEffects.empty() || !s.projectiles.front().weaponAnimId.empty())
                return fail("null OccupantAnim fell back to ordinary Anim"); ++boundaryChecks;
        }
        *weapon=saved; profile.occupied=false; s.projectiles.clear(); s.impactEffects.clear();
        spawnRuntimeProjectile(s,*findObjectById(s,f.host),*findObjectById(s,f.target),*weapon,profile,1);
        if (s.impactEffects.size()!=1 || s.impactEffects.front().animId.rfind("MGUN-",0)!=0
            || s.impactEffects.front().lifecycleState!="Muzzle") return fail("ordinary directional muzzle changed"); ++boundaryChecks;
        IniFile ini; ArtDatabase parsed;
        if (!ini.loadFromString("[Test]\nMuzzleFlash0=-2, +3\nMuzzleFlash1=broken\nMuzzleFlash2=65535,65535\n"
            "MuzzleFlash9=9,-9\nMuzzleFlash10=8,8\n") || !parsed.loadFromIni(ini)) return fail("ART boundary fixture");
        const auto* art=parsed.findArtType("Test");
        if (!art || art->muzzleFlashes[0].x!=-2 || art->muzzleFlashes[0].y!=3
            || art->muzzleFlashes[1].x || art->muzzleFlashes[2].x || art->muzzleFlashes[8].y
            || art->muzzleFlashes[9].x!=9 || art->muzzleFlashes[9].y!=-9) return fail("ART missing/malformed/window-9 boundary");
        ++boundaryChecks;
    }
    // Stock occupant weapons currently have one round. Exercise a custom burst
    // so the continuation path cannot silently revert to normal gun flashes.
    for (int hz:{30,60}) {
        auto f=make(hz,buildings[0],false); auto& s=*f.box;
        auto* host=findMutableObjectById(s,f.host); const auto* target=findObjectById(s,f.target);
        auto profile=runtimeGarrisonWeaponProfileForOccupant(s,*findRuntimeObjectByIdIncludingInactive(s,f.occupants[0]),target);
        profile.occupantMuzzleIndex=4; auto* weapon=const_cast<WeaponType*>(profile.weapon); weapon->burst=3;
        if (beginRuntimeCombatBurst(s,*host,*target,*weapon,profile,1,f.host,true)!=1
            || !s.combatDetails.hasPendingBurst(f.host)) return fail("custom burst launch");
        const auto first=s.impactEffects.front();
        std::ostringstream encoded; s.combatDetails.save(encoded);
        std::string old=encoded.str(); auto schema=old.find("Schema=2"); old.replace(schema,8,"Schema=1");
        auto row=old.find("Burst0="); auto end=old.find('\n',row); auto finalSpace=old.rfind(' ',end);
        old.erase(finalSpace,end-finalSpace);
        IniFile legacy; legacy.loadFromString(old); RuntimeCombatDetailsState legacyState;
        if (!legacyState.load(legacy,error) || legacyState.bursts.at(f.host).occupantMuzzleIndex!=-1)
            return fail("schema 1 pending-burst compatibility");
        const auto save=root+"/burst-"+std::to_string(hz)+".ini";
        if (!saveRuntimeState(s,save,error) || !loadRuntimeState(s,save,error)) return fail("full burst save/load: "+error);
        if (s.combatDetails.bursts.at(f.host).occupantMuzzleIndex!=4) return fail("full save lost selected window");
        findMutableObjectById(s,f.host)->garrisonFireCursor=2;
        for (int round=1;round<3;++round) {
            s.tick=s.combatDetails.bursts.at(f.host).nextFireTick; s.impactEffects.clear(); s.projectiles.clear();
            if (!advanceRuntimeCombatBursts(s) || s.impactEffects.size()!=1
                || s.impactEffects.front().animId!="UCFLASH" || s.impactEffects.front().lifecycleState!="OccupantMuzzle"
                || s.impactEffects.front().worldX!=first.worldX || s.impactEffects.front().worldY!=first.worldY)
                return fail("burst continuation changed window/animation after save");
        }
        if (s.combatDetails.hasPendingBurst(f.host)) return fail("completed burst still pending");
        boundaryChecks+=4;
    }
    std::cout<<"OccupantAnim passed: shots="<<shots<<" scheduled="<<scheduled<<" timeline="<<timelineChecks
        <<" render="<<renderChecks<<" boundaries="<<boundaryChecks
        <<"; stock six weapons/three buildings/all windows/eight facings/30-60 Hz, real mixed scheduler, "
        <<"snapshots, translucent frames, null fallback, ordinary Anim and burst full save/load\n";
    return true;
}
