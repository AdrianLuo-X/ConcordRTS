bool runRuntimeProjectileSpeedSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "projectile speed: " + why; return false; };
    const char* output = std::getenv("CONCORD_PROJECTILE_SPEED_OUTPUT");
    const std::string root = output && *output ? output : "build/smoke/feature-set-17/01-projectile-speed/checks";
    std::filesystem::create_directories(root);
    std::ofstream audit(root + "/weapons.csv");
    audit << "weapon,projectile,authoredSpeed,range,rot,invisible,oldLeptons,newLeptons\n";
    int audited = 0, guided = 0, changed = 0;
    for (const auto& weapon : live.rules.getWeaponTypes()) {
        const auto* projectile = live.rules.findProjectileType(weapon.projectile);
        if (!projectile) continue;
        const int oldSpeed = std::max(1, runtimeRuleSpeedLeptons(weapon.speed > 0 ? weapon.speed : 40));
        const int speed = runtimeProjectileSpeedLeptons(weapon, projectile,
            live.rules.getCombatDetailRules().projectileGravity);
        audit << weapon.id << ',' << projectile->id << ',' << weapon.speed << ',' << weapon.range
            << ',' << projectile->rot << ',' << projectile->inviso << ',' << oldSpeed << ',' << speed << '\n';
        ++audited;
        if (projectile->rot != 0) {
            ++guided;
            if (speed != oldSpeed) return fail(weapon.id + " guided speed changed");
        }
        if (projectile->inviso && speed != oldSpeed) return fail(weapon.id + " invisible effect cadence changed");
        if (speed != oldSpeed) ++changed;
    }
    // Independently recorded results of the retail range/gravity equation and
    // ReadSpeed, using the unchanged local rules. Do not derive goldens from
    // the production helper under test.
    struct SpeedCase { const char* weapon; int leptons; int fullRangeFrames; };
    constexpr SpeedCase goldens[] = {
        {"GrandCannonWeapon",166,24}, {"120mm",102,15}, {"105mm",95,14},
        {"155mm",121,17},
        {"HoverMissile",102,16}, {"MissileLauncher",76,27},
        {"RedEye2",255,13}, {"Medusa",255,13}, {"M60",255,5}
    };
    int timings = 0;
    for (const auto& test : goldens) {
        const auto* weapon = live.rules.findWeaponType(test.weapon);
        if (!weapon) return fail(std::string(test.weapon) + " native weapon missing");
        const auto* projectile = live.rules.findProjectileType(weapon->projectile);
        if (runtimeProjectileSpeedLeptons(*weapon, projectile,
                live.rules.getCombatDetailRules().projectileGravity) != test.leptons)
            return fail(std::string(test.weapon) + " retail speed mismatch");
        for (int hz : {15,30,60}) {
            const int ticks = runtimeProjectileTravelTicks(live.rules, *weapon, weapon->range, hz);
            if (ticks != test.fullRangeFrames * hz / 15)
                return fail(std::string(test.weapon) + " duration mismatch at " + std::to_string(hz));
            ++timings;
        }
    }
    const auto modRules = [](int gravity, bool floater) {
        IniFile ini;
        ini.loadFromString("[AudioVisual]\nGravity=" + std::to_string(gravity)
            + "\n[VehicleTypes]\n0=PROBE\n[PROBE]\nPrimary=BALLGUN\n"
              "[BALLGUN]\nRange=15\nSpeed=1\nProjectile=BALL\nWarhead=AP\n"
              "[BALL]\nArcing=yes\nFloater=" + std::string(floater ? "yes" : "no") + "\nImage=120MM\n");
        RulesDatabase rules; rules.loadFromIni(ini); return rules;
    };
    const auto normal = modRules(6,false), heavier = modRules(8,false), floating = modRules(8,true);
    const auto modSpeed = [](const RulesDatabase& rules) {
        const auto* weapon = rules.findWeaponType("BALLGUN");
        return weapon ? runtimeProjectileSpeedLeptons(*weapon, rules.findProjectileType("BALL"),
            rules.getCombatDetailRules().projectileGravity) : -1;
    };
    if (modSpeed(normal) != 166 || modSpeed(heavier) != 191 || modSpeed(floating) != 135
        || normal.getDetailRulesHash() == heavier.getDetailRulesHash()
        || heavier.getDetailRulesHash() == floating.getDetailRulesHash())
        return fail("Gravity/Floater parsing, speed or compatibility hash");

    struct FlightCase { const char* source; const char* weapon; int distance; int frames; int damage; };
    constexpr FlightCase flights[] = {
        {"GTGCAN","GrandCannonWeapon",15,24,150}, {"GTGCAN","GrandCannonWeapon",4,7,150},
        {"HTNK","120mm",5,13,90}, {"MTNK","105mm",5,14,65},
        {"DEST","155mm",8,17,36}
    };
    std::ofstream events(root + "/flight-events.csv");
    events << "source,weapon,hz,distance,spawnTick,expectedImpact,actualImpact,damage\n";
    int impacts = 0, restored = 0;
    for (int hz : {15,30,60}) for (const auto& test : flights) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = live.config; s.config.fixedTickHz = hz;
        s.config.noCooldown = s.config.computerAiEnabled = s.config.multiplayerActive = false;
        s.config.crates = s.config.superweapons = false;
        s.report = live.report; s.rules = live.rules; s.rulesLoaded = true;
        s.art = live.art; s.artLoaded = true; s.theater = live.theater; s.theaterLoaded = live.theaterLoaded;
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.runtimePreferencePersistenceEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.visionEnabled = false; s.economy.owner = "Russians"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true;
        for (int y=1;y<=28;++y) for (int x=1;x<=42;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance source;
        source.instanceId=1; source.type=test.source; source.owner="Russians";
        source.kind=std::string(test.source)=="GTGCAN" ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        source.alive=true; source.health=1000; source.x=8; source.y=10;
        source.cell=runtimeCellKey(source.x,source.y); source.mission="Sleep";
        source.facing=source.turretFacing=runtimeFacingFromDelta(1,0);
        auto target=source; target.instanceId=2; target.type="HTNK"; target.kind=WorldObjectKind::Unit;
        target.owner="Americans"; target.health=400; target.x+=test.distance;
        target.cell=runtimeCellKey(target.x,target.y);
        s.world.accessObjects()={source,target}; initializeRuntimeNavigation(s);
        if (runtimeMaxHealthForObject(s,target)!=target.health)
            return fail("native Rhino health fixture changed");
        const auto* weapon=s.rules.findWeaponType(test.weapon);
        if (!weapon) return fail("flight native weapon missing");
        RuntimeWeaponProfile profile;
        finalizeRuntimeWeaponProfile(profile,s,RuntimeWeaponSlot::Primary,*weapon,"projectile speed flight");
        spawnRuntimeProjectile(s,source,target,*weapon,profile,weapon->damage);
        if (s.projectiles.size()!=1) return fail(std::string(test.source)+" launch");
        const int expected=test.frames*hz/15;
        const int actor=s.projectiles.front().actorId;
        if (s.projectiles.front().totalTicks!=expected) return fail(std::string(test.source)+" actual launch duration");
        const int healthBefore=findObjectById(s,2)->health;
        int hit=-1;
        for (int step=0;step<=expected+1;++step) {
            ++s.tick; advanceRuntimeEffects(s);
            if (hz==30 && test.distance==15 && s.tick%12==0 && s.tick<expected) {
                const auto stats=drawRuntimeStateOffscreenAtCell(s,{15,10},960,540,error,
                    root+"/grand-cannon-"+std::to_string(s.tick)+".bmp",false);
                if (!stats || stats->projectileActorTravelDrawn==0 || stats->projectileActorTravelMissing!=0)
                    return fail("Grand Cannon original projectile rendering: "+error);
            }
            if (s.tick==std::max(1,expected/2)) {
                if (s.projectiles.empty()) return fail("flight disappeared before save");
                const auto encoded=serializeRuntimeProjectileEffect(s.projectiles.front());
                RuntimeProjectileEffect loaded;
                if (!parseRuntimeProjectileEffect(encoded,loaded)
                    || serializeRuntimeProjectileEffect(loaded)!=encoded) return fail("flight save round-trip");
                s.projectiles.front()=std::move(loaded); ++restored;
            }
            const auto effect=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),
                [&](const auto& value){return value.projectileActorId==actor && value.renderIntent=="warheadImpact";});
            if (effect!=s.impactEffects.end()) { hit=s.tick; break; }
        }
        const auto* victim=findObjectById(s,2);
        if (hit!=expected || !victim || healthBefore-victim->health!=test.damage)
            return fail(std::string(test.source)+" hit/damage at Hz="+std::to_string(hz)
                +" expected="+std::to_string(expected)+" hit="+std::to_string(hit)
                +" damage="+std::to_string(victim ? healthBefore-victim->health : -1));
        events<<test.source<<','<<test.weapon<<','<<hz<<','<<test.distance<<",0,"<<expected<<','<<hit
            <<','<<(healthBefore-victim->health)<<'\n'; ++impacts;
    }
    std::cout << "Projectile speed: audited="<<audited<<" guidedUnchanged="<<guided
        <<" visibleSpeedCorrections="<<changed<<" timingGoldens="<<timings
        <<" actualImpacts="<<impacts<<" midflightRestores="<<restored
        <<" Gravity/Floater/hashes=PASS; Grand Cannon 15 cells at 30 Hz: 3840 -> 48 ticks\n";
    return runRuntimeSpawnedMissileCompatibilitySmokeProbe(live,error);
}
