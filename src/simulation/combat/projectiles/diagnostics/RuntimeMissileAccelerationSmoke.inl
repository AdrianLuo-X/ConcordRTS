bool runRuntimeMissileAccelerationSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "missile startup acceleration: " + why; return false; };
    const char* output = std::getenv("CONCORD_MISSILE_ACCELERATION_OUTPUT");
    const std::string root = output && *output ? output
        : "build/smoke/feature-set-24/02-startup-acceleration/missile-checks";
    std::filesystem::create_directories(root);
    std::ofstream trace(root + "/actual-flight.csv");
    trace << "hz,case,tick,elapsedTicks,x,y,z,measuredCells,cruiseCells,targetX,targetY,remainingTicks\n";
    int cases=0, samples=0, restores=0, continuationTicks=0, caps=0, dynamicHits=0;
    struct Case { const char* name; int acceleration; int distance; bool moving; };
    const Case scenarios[] = {{"positive-long",3,8,false},{"positive-close",3,2,false},
        {"positive-moving",3,8,true},{"explicit-zero",0,8,false},{"explicit-negative",-2,8,false}};
    for (int hz : {15,30,60}) for (const auto& test : scenarios) {
        const std::string label=std::string(test.name)+" "+std::to_string(hz)+"Hz";
        auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
        s.config=live.config; s.config.fixedTickHz=hz;
        s.config.computerAiEnabled=s.config.noCooldown=s.config.multiplayerActive=false;
        s.config.crates=s.config.superweapons=false;
        s.report=live.report; s.world=live.world;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.runtimePreferencePersistenceEnabled=s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true; s.visionEnabled=false;
        s.economy.owner="Russians"; s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true;
        IniFile ini;
        const std::string text="[VehicleTypes]\n0=ACCELCAR\n1=DEFAULTCAR\n"
            "[ACCELCAR]\nStrength=400\nSpeed=5\nPrimary=ACCELGUN\nSpeedType=Wheel\n"
            "[DEFAULTCAR]\nStrength=400\nSpeed=5\nPrimary=DEFAULTGUN\n"
            "[ACCELGUN]\nDamage=20\nROF=1000\nRange=16\nSpeed=40\nProjectile=ACCELMISSILE\nWarhead=ACCELWH\n"
            "[DEFAULTGUN]\nDamage=20\nRange=16\nSpeed=40\nProjectile=DEFAULTMISSILE\nWarhead=ACCELWH\n"
            "[DEFAULTMISSILE]\nImage=DRAGON\nROT=8\nRanged=yes\nLevel=yes\n"
            "[ACCELMISSILE]\nImage=DRAGON\nROT=8\nRanged=yes\nLevel=yes\nAcceleration="
            + std::to_string(test.acceleration)+"\n"
            "[ACCELWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n";
        if (!ini.loadFromString(text) || !s.rules.loadFromIni(ini)) return fail(label+" rules");
        s.rulesLoaded=true;
        // Define an authored zero-height muzzle so Level=yes stays on the
        // target plane. Missing art otherwise uses the real 80-lepton fallback.
        IniFile muzzleArt;
        if (!muzzleArt.loadFromString("[ACCELCAR]\nPrimaryFireFLH=0,0,0\n")
            || !s.art.loadFromIni(muzzleArt)) return fail(label+" zero-height muzzle art");
        s.artLoaded=true;
        const auto* implicit=s.rules.findProjectileType("DEFAULTMISSILE");
        const auto* missile=s.rules.findProjectileType("ACCELMISSILE");
        if (!implicit || implicit->acceleration!=3 || implicit->profile.acceleration!=3
            || !missile || missile->acceleration!=test.acceleration)
            return fail(label+" original default/explicit acceleration parsing");
        for (int y=1;y<=24;++y) for (int x=1;x<=32;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance source;
        source.instanceId=1; source.kind=WorldObjectKind::Unit; source.type="ACCELCAR";
        source.owner="Russians"; source.alive=true; source.health=400;
        source.x=8; source.y=10; source.cell=runtimeCellKey(source.x,source.y);
        source.mission="Sleep"; source.facing=source.turretFacing=runtimeFacingFromDelta(1,0);
        auto target=source; target.instanceId=2; target.owner="Americans";
        target.x+=test.distance; target.cell=runtimeCellKey(target.x,target.y);
        s.world.accessObjects()={source,target}; initializeRuntimeNavigation(s);
        RuntimeSnapshotActiveStateScope scope(s);
        const auto* weapon=s.rules.findWeaponType("ACCELGUN");
        if (!weapon) return fail(label+" weapon");
        RuntimeWeaponProfile profile;
        finalizeRuntimeWeaponProfile(profile,s,RuntimeWeaponSlot::Primary,*weapon,"missile startup smoke");
        spawnRuntimeProjectile(s,source,target,*weapon,profile,20);
        if (s.projectiles.size()!=1) return fail(label+" actual launch");
        const int actor=s.projectiles.front().actorId;
        const double cruise=s.projectiles.front().speedCellsPerTick;
        if (!runtimeUsesHomingMissileGuidance(s.projectiles.front(),*missile)
            || std::abs(s.projectiles.front().sourceZ-s.projectiles.front().targetZ)>1e-9)
            return fail(label+" flat physical guidance fixture: sourceZ="
                +std::to_string(s.projectiles.front().sourceZ)+" targetZ="
                +std::to_string(s.projectiles.front().targetZ)+" rot="+std::to_string(missile->rot)
                +" ranged="+std::to_string(missile->ranged));
        const int delay=runtimeMissileAccelerationStartupDelayTicks(cruise,test.acceleration,hz);
        const int oldFuel=runtimeProjectileTravelTicks(s.rules,*weapon,weapon->range*2,hz);
        if (s.projectiles.front().totalTicks!=oldFuel+delay)
            return fail(label+" fuel lifetime did not retain its authored cruising distance");
        std::optional<RuntimeProjectileEffect> resumed;
        double previousX=s.projectiles.front().currentX, previousY=s.projectiles.front().currentY;
        double previousZ=s.projectiles.front().currentZ, previousStep=-1.0;
        bool hit=false, increased=false, movingTarget=false, speedCapped=false;
        for (int step=0;step<hz*12;++step) {
            if (test.moving && !movingTarget && s.tick>=hz/2) {
                auto* moving=findMutableObjectById(s,2);
                ++moving->y; moving->cell=runtimeCellKey(moving->x,moving->y);
                movingTarget=true;
            }
            ++s.tick; advanceRuntimeEffects(s);
            bool resumedReached=false;
            if (resumed) {
                --resumed->remainingTicks;
                resumed->elapsedTicks=std::clamp(resumed->totalTicks-resumed->remainingTicks,0,resumed->totalTicks);
                syncRuntimeProjectileTargetEndpoint(s,*resumed);
                resumedReached=advanceRuntimeRangedProjectilePosition(s,*resumed,*missile);
            }
            const auto current=std::find_if(s.projectiles.begin(),s.projectiles.end(),
                [&](const auto& shot){return shot.actorId==actor;});
            if (current==s.projectiles.end()) {
                const auto impact=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),
                    [&](const auto& effect){return effect.projectileActorId==actor && effect.renderIntent=="warheadImpact";});
                if (impact==s.impactEffects.end() || !findObjectById(s,2)
                    || findObjectById(s,2)->health>=400 || (resumed && !resumedReached))
                    return fail(label+" expired or damaged before actual target arrival");
                hit=true; break;
            }
            const double measured=std::hypot(std::hypot(current->currentX-previousX,
                current->currentY-previousY),(current->currentZ-previousZ)/runtimeMissileLevelsPerCell);
            if (measured<=0.0 || measured>cruise+1e-9) return fail(label+" invalid physical step/cruise cap");
            if (current->elapsedTicks==1) {
                if (test.acceleration>0 && !(measured<cruise*0.5))
                    return fail(label+" first actual flight step was already at cruise speed");
                if (test.acceleration<=0 && std::abs(measured-cruise)>1e-9)
                    return fail(label+" nonpositive compatibility changed");
            }
            if (previousStep>=0.0 && measured+1e-9<previousStep)
                return fail(label+" physical speed decreased during launch/cruise");
            increased=increased || (previousStep>=0.0 && measured>previousStep+1e-9);
            speedCapped=speedCapped || std::abs(measured-cruise)<1e-9;
            if (resumed) {
                if (resumedReached || std::abs(current->currentX-resumed->currentX)>1e-12
                    || std::abs(current->currentY-resumed->currentY)>1e-12
                    || std::abs(current->currentZ-resumed->currentZ)>1e-12
                    || current->elapsedTicks!=resumed->elapsedTicks || current->remainingTicks!=resumed->remainingTicks)
                    return fail(label+" decoded in-flight launch age diverged on continuation");
                ++continuationTicks;
            } else if (current->elapsedTicks>=std::max(1,hz/5)) {
                const std::string row=serializeRuntimeProjectileEffect(*current);
                RuntimeProjectileEffect decoded;
                if (!parseRuntimeProjectileEffect(row,decoded) || serializeRuntimeProjectileEffect(decoded)!=row)
                    return fail(label+" existing projectile save/snapshot codec changed the acceleration state");
                resumed=std::move(decoded); ++restores;
            }
            trace << hz << ',' << test.name << ',' << s.tick << ',' << current->elapsedTicks
                << ',' << std::setprecision(17) << current->currentX << ',' << current->currentY << ',' << current->currentZ
                << ',' << measured << ',' << cruise << ',' << current->targetX << ',' << current->targetY
                << ',' << current->remainingTicks << '\n';
            previousX=current->currentX; previousY=current->currentY; previousZ=current->currentZ;
            previousStep=measured; ++samples;
        }
        if (!hit || (test.acceleration>0 && !increased)) return fail(label+" acceleration/arrival missing");
        if (test.distance==8 && !speedCapped) return fail(label+" never reached its authored cruise speed");
        if (speedCapped) ++caps;
        if (test.moving && movingTarget) ++dynamicHits;
        ++cases;
    }
    // Keep nonmissile and special rocket eligibility independent of defaults.
    RuntimeProjectileEffect shot; ProjectileType type;
    shot.speedCellsPerTick=1.0; shot.acceleration=3; type.rot=8; type.ranged=true;
    if (!runtimeUsesHomingMissileGuidance(shot,type)) return fail("ordinary guidance eligibility");
    for (int excluded=0;excluded<7;++excluded) {
        auto probe=shot; auto authored=type;
        if (excluded==0) authored.arcing=true;
        if (excluded==1) authored.vertical=true;
        if (excluded==2) authored.dropping=true;
        if (excluded==3) authored.inviso=true;
        if (excluded==4) probe.spawnedMissileDamage=true;
        if (excluded==5) probe.laser=true;
        if (excluded==6) probe.voxelDebris.enabled=true;
        if (runtimeUsesHomingMissileGuidance(probe,authored)) return fail("special/nonmissile guidance exclusion");
    }
    int specialRadialSteps=0;
    for (int hz : {15,30,60}) {
        auto probeState=std::make_unique<RuntimeWindowState>();
        probeState->config.fixedTickHz=hz;
        IniFile specialRules;
        if (!specialRules.loadFromString("[VehicleTypes]\n0=RADIALCAR\n1=DISKCAR\n"
            "[RADIALCAR]\nPrimary=RADIALGUN\nSecondary=PARASITEGUN\n"
            "[DISKCAR]\nPrimary=DISKGUN\n"
            "[DISKGUN]\nProjectile=RADIALMISSILE\nWarhead=RADIALWH\nDiskLaser=yes\n"
            "[RADIALGUN]\nProjectile=RADIALMISSILE\nWarhead=RADIALWH\n"
            "[PARASITEGUN]\nProjectile=RADIALMISSILE\nWarhead=PARASITEWH\nLimboLaunch=yes\n"
            "[RADIALMISSILE]\nROT=8\nRanged=yes\nLevel=yes\n"
            "[RADIALWH]\nVerses=100%\n[PARASITEWH]\nParasite=yes\n")
            || !probeState->rules.loadFromIni(specialRules)) return fail("special radial rules");
        probeState->rulesLoaded=true;
        const auto* radialType=probeState->rules.findProjectileType("RADIALMISSILE");
        if (!radialType || !probeState->rules.findWeaponType("PARASITEGUN")
            || !probeState->rules.findWeaponType("DISKGUN"))
            return fail("special radial/parasite fixture registry");
        RuntimeProjectileEffect radial;
        radial.speedCellsPerTick=0.25*15.0/hz; radial.acceleration=3; radial.elapsedTicks=1;
        radial.renderIntent="radialProjectile"; radial.projectileId="RADIALMISSILE";
        radial.currentX=radial.sourceX=8.0; radial.currentY=radial.sourceY=10.0;
        radial.targetX=18.0; radial.targetY=10.0; radial.hasTargetPosition=true;
        radial.homingInitialized=true; radial.homingYaw=radial.homingPitch=0.0;
        if (!runtimeUsesMissileStartupAcceleration(*probeState,radial,*radialType))
            return fail("ordinary ranged startup eligibility");
        auto onlyRadial=*radialType; onlyRadial.ranged=false;
        if (!runtimeUsesMissileStartupAcceleration(*probeState,radial,onlyRadial))
            return fail("ordinary non-Ranged radial startup eligibility");
        auto straight=radial; straight.renderIntent="projectile";
        if (runtimeUsesMissileStartupAcceleration(*probeState,straight,onlyRadial))
            return fail("non-Ranged direct projectile startup eligibility");
        for (int excluded=0;excluded<11;++excluded) {
            auto special=radial; auto authored=*radialType;
            if (excluded==0) authored.arcing=true;
            if (excluded==1) authored.vertical=true;
            if (excluded==2) authored.dropping=true;
            if (excluded==3) authored.inviso=true;
            if (excluded==4) special.spawnedMissileDamage=true;
            if (excluded==5) special.laser=true;
            if (excluded==6) special.electricBolt=true;
            if (excluded==7) special.voxelDebris.enabled=true;
            if (excluded==8) { special.weaponId="PARASITEGUN"; special.warheadId="PARASITEWH"; }
            if (excluded==9) special.renderIntent="diskLaserCharge";
            if (excluded==10) special.weaponId="DISKGUN";
            if (excluded==8 && !runtimeProjectileIsParasiteLeap(*probeState,special))
                return fail("special radial parasite identity");
            if (runtimeUsesMissileStartupAcceleration(*probeState,special,authored))
                return fail("special radial startup eligibility " + std::to_string(excluded));
            if (advanceRuntimeHomingMissilePosition(*probeState,special,authored)
                || std::abs(std::hypot(special.currentX-radial.currentX,
                    special.currentY-radial.currentY)-radial.speedCellsPerTick)>1e-12
                || std::abs(special.currentZ-radial.currentZ)>1e-12)
                return fail("special radial actual cruising step " + std::to_string(excluded));
            ++specialRadialSteps;
        }
    }
    std::cout << "Missile startup acceleration: cases=" << cases << " physicalSteps=" << samples
        << " cruiseCaps=" << caps << " decodedContinuations=" << restores << '/' << continuationTicks
        << " movingTargetHits=" << dynamicHits << " specialRadialSteps=" << specialRadialSteps
        << " 15/30/60Hz default3/explicit0/negative/exclusions passed\n";
    return true;
}
