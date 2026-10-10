bool runRuntimeSquidGrappleSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "squid grapple: " + why; return false; };
    const char* output = std::getenv("CONCORD_SQUID_GRAPPLE_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    std::optional<TerrainCell> water;
    for (const auto& cell : live.world.getTerrainCells())
        if (runtimeCachedTerrainInfoForCell(live, cell).water) { water = cell; break; }
    if (!water) return fail("original water tile missing");
    for (const int hz : {15, 30, 60}) for (const std::string ship : {"DEST", "DRED", "AEGIS", "CARRIER", "SUB", "BSUB", "DLPH", "SQD"}) {
        auto owned = std::make_unique<RuntimeWindowState>(); auto& s = *owned;
        seedRuntimeLoadScratch(s, live, false); s.config.fixedTickHz = hz;
        IniFile stockRules;
        if(!stockRules.loadFromFile(live.report.rulesPath)||!s.rules.loadFromIni(stockRules))
            return fail("stock rules unavailable");
        if (s.rules.getAudioVisualValue("SplashList")!="H2O_EXP3,H2O_EXP2,H2O_EXP1")
            return fail("stock CombatDamage SplashList was not loaded");
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts={};s.mapFactsLoaded=false;s.terrainInfoByCell.clear();s.terrainByCell.clear();
        s.economy.owner = "Russians"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        for (int y=48; y<=80; ++y) for (int x=48; x<=80; ++x) {
            auto cell = *water; cell.x=x; cell.y=y; cell.level=0;
            cell.cellIndex512=y*512+x; s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance squid;
        squid.instanceId=1; squid.kind=WorldObjectKind::Unit; squid.type="SQD"; squid.owner="Russians";
        squid.alive=true; squid.health=200; squid.x=63; squid.y=64; squid.cell=runtimeCellKey(63,64);
        squid.mission="Guard"; squid.submerged=true;
        auto host=squid; host.instanceId=2; host.type=ship; host.owner="Americans";
        host.facing=0;
        host.x=64; host.cell=runtimeCellKey(64,64);
        host.submerged=s.rules.findUnitType("UnitType",ship)->underwater;
        host.health=s.rules.findUnitType("UnitType",ship)->strength;
        const int healthBefore=host.health;
        s.world.accessObjects()={squid,host}; initializeRuntimeNavigation(s);
        RuntimeSnapshotActiveStateScope scope(s);
        ensureRuntimeSpecialMechanismsInitialized(s);
        ++s.tick; advanceRuntimeEffects(s);
        if ((ship=="DEST" || ship=="CARRIER")
            && !s.specialMechanisms.carrierParents().contains(2))
            return fail(ship+" native aircraft parent was not initialized");
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,1),findObjectById(s,2));
        if(ship=="DLPH"||ship=="SQD") {
            if(!profile.weapon||profile.weapon->id!="SquidPunch")return fail(ship+" organic target did not choose punch");
            s.selectedObjectIds={1};issueRuntimeAttackCommand(s,*findObjectById(s,2));
            for(int step=0;step<hz*3&&s.projectiles.empty();++step) {
                ++s.tick;auto behavior=s.behaviors.find(1);
                if(behavior==s.behaviors.end())return fail(ship+" punch command missing");
                (void)advanceRuntimeAttackBehavior(s,*findMutableObjectById(s,1),behavior->second);
            }
            if(s.projectiles.empty()||s.projectiles.front().weaponId!="SquidPunch"
                ||!s.specialMechanisms.parasiteLinks().empty()||!findObjectById(s,1)
                ||findObjectById(s,1)->weaponCooldownTicksRemaining<=0)return fail(ship+" organic punch failed");
            const auto shot=s.projectiles.front();s.projectiles.clear();
            if(!applyRuntimeProjectileImpact(s,shot)||findObjectById(s,2)->health>=healthBefore)
                return fail(ship+" punch did not damage target");
            std::ostringstream saved;IniFile savedIni;auto restored=std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*restored,s,false);
            if(!writeRuntimeStateToStream(s,saved,error)||!savedIni.loadFromString(saved.str())
                ||!materializeRuntimeLoadStateFromIni(*restored,savedIni,error)
                ||findObjectById(*restored,1)->weaponCooldownTicksRemaining!=findObjectById(s,1)->weaponCooldownTicksRemaining)
                return fail(ship+" punch reload: "+error);
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{64,64},800,600,error,
                root.empty()||hz!=30?"":root+"/"+ship+"-punch.bmp",false);
            if(!stats||stats->squidGrappleDrawn)return fail(ship+" organic target was grappled");
            std::cout<<"Squid organic: "<<ship<<" hz="<<hz<<" real attack, punch damage, no parasite, animation reload passed\n";
            continue;
        }
        if (!profile.weapon || profile.weapon->id!="SquidGrab"
            || !launchRuntimeParasiteProjectile(s,*findObjectById(s,1),*findObjectById(s,2),*profile.weapon,profile)
            || s.projectiles.size()!=1) return fail(ship+" real launch failed: "+s.simulationStatus);
        if (std::any_of(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& e){return e.animId.rfind("SQDG",0)==0;}))
            return fail("grapple was still emitted at the muzzle");
        {
            std::ostringstream pendingSave; IniFile pendingIni;
            auto pendingLoad=std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*pendingLoad,s,false);
            if (!writeRuntimeStateToStream(s,pendingSave,error)
                || !pendingIni.loadFromString(pendingSave.str())
                || !materializeRuntimeLoadStateFromIni(*pendingLoad,pendingIni,error)
                || pendingLoad->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash())
                return fail(ship+" in-flight parasite reload: "+error);
        }
        const auto projectile=s.projectiles.front(); s.projectiles.clear();
        if (!applyRuntimeProjectileImpact(s,projectile) || !s.specialMechanisms.parasiteLinks().contains(2)
            || findObjectById(s,1)!=nullptr) return fail(ship+" did not attach / enter limbo");
        const auto capture=[&](const std::string& tag, bool attached, std::vector<std::uint32_t>* pixels=nullptr) {
            refreshRuntimeSpecialMechanismRenderSnapshots(s);
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{64,64},800,600,error,
                root.empty() || hz!=30 ? "" : root+"/"+ship+"-"+tag+".bmp",false,true,-1,pixels);
            const int splashes=static_cast<int>(std::count_if(s.impactEffects.begin(),s.impactEffects.end(),
                [](const auto& impact){return impact.lifecycleState=="SquidSplash";}));
            if (!stats || stats->squidGrappleMissing || stats->squidGrappleDrawn!=(attached?1:0)
                || stats->squidSplashMissing || stats->squidSplashDrawn!=splashes) {
                error=ship+" "+tag+" drawn="+std::to_string(stats?stats->squidGrappleDrawn:-1)
                    +" missing="+std::to_string(stats?stats->squidGrappleMissing:-1)+" "+error;
                return false;
            }
            return true;
        };
        if (!capture("attach",true)) return fail(error);
        std::vector<std::uint32_t> firstHold, laterHold;
        const int attachedAt=s.tick;
        for (int age=0; age<hz*5; ++age) {
            ++s.tick; advanceRuntimeEffects(s);
            const int nativeAge=(s.tick-attachedAt)*15/hz;
            const int splashCount=static_cast<int>(std::count_if(s.impactEffects.begin(),s.impactEffects.end(),
                [](const auto& impact){return impact.lifecycleState=="SquidSplash";}));
            if (splashCount!=(nativeAge>=70?3:0))
                return fail(ship+" splash boundary/count age="+std::to_string(nativeAge)+" count="+std::to_string(splashCount));
            if (s.tick-attachedAt==hz*3 && !capture("hold-3s",true,&firstHold)) return fail(error);
            if (s.tick-attachedAt==hz*4 && !capture("hold-4s",true,&laterHold)) return fail(error);
            if (s.tick-attachedAt==hz*10/3 && !capture("peak-roll",true)) return fail(error);
        }
        if (!capture("water-5s",true)) return fail(error);
        std::vector<RuntimeImpactEffect> waterEffects;
        for (const auto& effect:s.impactEffects) if (effect.lifecycleState=="SquidSplash") {
            const auto* splashArt=s.art.findArtType(effect.animId);
            if (!splashArt || effect.animId.rfind("H2O_EXP",0)!=0 || effect.animationFrameCount!=16
                || effect.totalTicks!=16*hz/15 || effect.remainingTicks!=11*hz/15
                || effect.damage!=0 || effect.worldLevel!=0.0 || !effect.hasWorldPosition)
                return fail(ship+" original one-shot water animation/plane/lifetime");
            // The host initially faces authored 224: its water offsets are
            // diagonal, still exactly 128/64 leptons from the saved origin.
            const double yaw=-3.0*3.141592653589793/4;
            const double dx=effect.worldX-64.0,dy=effect.worldY-64.0;
            const double forward=dx*std::cos(yaw)+dy*std::sin(yaw);
            const double lateral=-dx*std::sin(yaw)+dy*std::cos(yaw);
            if (std::abs(std::abs(lateral)-0.25)>1e-8
                || std::abs(forward-(static_cast<int>(waterEffects.size())-1)*0.5)>1e-8)
                return fail(ship+" native splash placement");
            waterEffects.push_back(effect);
            RuntimeImpactEffect transferred;
            if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(effect),transferred))
                return fail(ship+" network splash record rejected");
            const auto before=makeRuntimeImpactRenderActor(effect),after=makeRuntimeImpactRenderActor(transferred);
            std::uint64_t beforeHash=0,afterHash=0;
            runtimeMultiplayerAuthoritativeMixImpact(beforeHash,effect);
            runtimeMultiplayerAuthoritativeMixImpact(afterHash,transferred);
            if (beforeHash!=afterHash || before.animId!=after.animId || before.frameIndex!=after.frameIndex
                || before.x!=after.x || before.y!=after.y || before.level!=after.level
                || before.centerOnAnchor!=after.centerOnAnchor)
                return fail(ship+" network splash lost native image/frame/position or authority");
        }
        if (firstHold==laterHold || findObjectById(s,2)->health>=healthBefore)
            return fail(ship+" held animation/damage did not advance: health="+std::to_string(findObjectById(s,2)->health));
        for (int facing=0; facing<256; facing+=32) {
            findMutableObjectById(s,2)->facing=facing;
            if (!capture("facing-"+std::to_string(facing),true)) return fail(error);
        }
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(s,saved,error)) return fail("save: "+error);
        if (!root.empty()) {
            std::ofstream fixture(root+"/"+ship+"-"+std::to_string(hz)+"-attached.ini");
            fixture << saved.str();
        }
        auto restored=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored,s,false);
        IniFile ini;
        if (!ini.loadFromString(saved.str()) || !materializeRuntimeLoadStateFromIni(*restored,ini,error)
            || restored->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash())
        {
            const auto& link=s.specialMechanisms.parasiteLinks().at(2);
            const auto* inactive=findRuntimeObjectByIdIncludingInactive(s,1);
            const auto* hostNow=findObjectById(s,2);
            return fail(ship+" attached save/load: "+error
                +" source="+inactive->type+"/"+inactive->mission+"/"+std::to_string(inactive->health)
                +" host="+hostNow->type+"/"+hostNow->mission+"/"+std::to_string(hostNow->health)
                +" tick="+std::to_string(s.tick)+" attached="+std::to_string(link.attachedTick)
                +" next="+std::to_string(link.nextDamageTick)+" interval="+std::to_string(link.damageIntervalTicks)
                +" damage="+std::to_string(link.damage)+" maxHP="+std::to_string(link.targetMaximumHealth)
                +" paralyzes="+std::to_string(link.paralyzes)+" threshold="+std::to_string(link.cullingThresholdPerMyriad));
        }
        refreshRuntimeSpecialMechanismRenderSnapshots(*restored);
        const auto afterLoad=drawRuntimeStateOffscreenAtCell(*restored,{64,64},800,600,error,"",false);
        if (!afterLoad || afterLoad->squidGrappleDrawn!=1 || afterLoad->squidGrappleMissing)
            return fail(ship+" restored animation missing: "+error);
        std::vector<RuntimeImpactEffect> loadedWater;
        for (const auto& effect:restored->impactEffects) if (effect.lifecycleState=="SquidSplash") loadedWater.push_back(effect);
        if (loadedWater.size()!=3 || afterLoad->squidSplashDrawn!=3 || afterLoad->squidSplashMissing)
            return fail(ship+" saved water animation missing");
        for (std::size_t i=0;i<loadedWater.size();++i) {
            const auto& a=waterEffects[i]; const auto& b=loadedWater[i];
            if (a.animId!=b.animId || a.frameIndex!=b.frameIndex || a.remainingTicks!=b.remainingTicks
                || a.totalTicks!=b.totalTicks || a.worldX!=b.worldX || a.worldY!=b.worldY || a.worldLevel!=b.worldLevel)
                return fail(ship+" water position/frame/lifetime changed after load");
        }
        {
            RuntimeSnapshotActiveStateScope restoredScope(*restored);
            // Resume the saved attachment through the opposite rocking strip
            // and its second splash event, without resetting animation age.
            for (int step=1;step<=40*hz/15;++step) {
                ++restored->tick; advanceRuntimeEffects(*restored);
                const int age=75+step*15/hz;
                const int expected=age<86 || age>=110 ? 3 : 0;
                const int count=static_cast<int>(std::count_if(restored->impactEffects.begin(),restored->impactEffects.end(),
                    [](const auto& impact){return impact.lifecycleState=="SquidSplash";}));
                if (count!=expected || !restored->specialMechanisms.parasiteLinks().contains(2))
                    return fail(ship+" restored second rocking/splash cycle age="+std::to_string(age));
            }
        }
        if (!handleRuntimeParasiteHostEvent(s,2,ParasiteHostEventKind::SonicHit,7)
            || !s.specialMechanisms.parasiteLinks().empty() || findObjectById(s,1)==nullptr
            || !capture("released",false)) return fail(ship+" release: "+error);
        // Already emitted water belongs to the world; detachment stops new
        // bursts but lets these three finish their original sixteen frames.
        for (int remaining=11*hz/15;remaining>0;--remaining) {
            ++s.tick; advanceRuntimeEffects(s);
            const int count=static_cast<int>(std::count_if(s.impactEffects.begin(),s.impactEffects.end(),
                [](const auto& impact){return impact.lifecycleState=="SquidSplash";}));
            if (count!=(remaining>1?3:0)) return fail(ship+" released water did not finish once");
        }
        if (!capture("settled",false)) return fail(error);
        std::cout<<"Squid grapple: "<<ship<<" hz="<<hz
            <<" real launch/attach, 8 directions, hull roll, three original splashes, damage, pending/attached/water reload, sonic release/settling PASS\n";
    }
    RuntimeMapRenderContext context;context.rules=&live.rules;context.art=&live.art;
    context.theater=live.theaterLoaded?&live.theater:nullptr;
    context.assetRootPath=runtimeAssetRootPath(live.config);
    context.tileRootPath=runtimeTileRootPath(live.config,live.report.map);
    context.palettePath=runtimePalettePath(live.config,live.report.map);
    context.structureRootPath=runtimeStructureRootPath(live.config,live.report.map);
    context.structurePalettePath=runtimeStructurePalettePath(live.config,live.report.map);
    return runRuntimeSquidAnimationRenderContractCheck(context,error);
}
