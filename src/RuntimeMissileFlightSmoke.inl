bool runRuntimeMissileFlightSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "missile flight/exhaust: " + why; return false; };
    const char* output = std::getenv("CONCORD_MISSILE_FLIGHT_OUTPUT");
    const std::string root = output == nullptr ? "" : output;
    if (!root.empty()) std::filesystem::create_directories(root);
    for (const char* anim : {"V3TRAIL", "SMOKEY2", "NUKEPUFF"})
        if (live.art.findArtType(anim) == nullptr) return fail(std::string("original art missing: ") + anim);

    const auto fixture = [&](const std::string& type, int hz) {
        auto result = std::make_unique<RuntimeWindowState>();
        auto& s = *result;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.computerAiEnabled = false;
        s.report = live.report; s.rules = live.rules; s.rulesLoaded = true;
        s.art = live.art; s.artLoaded = true; s.theater = live.theater; s.theaterLoaded = live.theaterLoaded;
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.economy.owner = "Russians"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        for (int y=1;y<=28;++y) for (int x=1;x<=32;++x) {
            TerrainCell c; c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        WorldObjectInstance source;
        source.instanceId=1;source.kind=WorldObjectKind::Unit;source.type=type;source.owner="Russians";
        source.alive=true;source.health=s.rules.findUnitType("UnitType",type)->strength;
        source.x=8;source.y=10;source.cell=runtimeCellKey(8,10);source.mission="Attack";
        source.facing=source.turretFacing=runtimeFacingFromDelta(1,0);
        auto target=source;target.instanceId=2;target.type="CAEAST01";target.kind=WorldObjectKind::Structure;
        target.owner="Americans";target.health=3000;target.x=20;target.cell=runtimeCellKey(20,10);target.mission="Guard";
        s.world.accessObjects()={source,target};initializeRuntimeNavigation(s);
        return result;
    };
    for (const int hz : {30, 60}) for (const char* animation : {"V3TRAIL", "SMOKEY2", "NUKEPUFF"}) {
        auto sample = fixture("V3", hz); auto& s = *sample;
        const int rate = s.art.findArtType(animation)->rate;
        const int frameDelay = runtimeMissileExhaustFrameDelay(rate, hz);
        int emitted = 0;
        for (int age = 0; age < hz * 3; ++age)
            emitted += emitRuntimeMissileExhaust(s, animation, "Russians", 1, "cadence-probe", {8, 10, 6}, age) ? 1 : 0;
        const int oldCount = (hz * 3 + frameDelay - 1) / frameDelay;
        if (emitted * 3 != oldCount || s.impactEffects.empty())
            return fail(std::string(animation) + " cadence hz=" + std::to_string(hz)
                + " new=" + std::to_string(emitted) + " old=" + std::to_string(oldCount));
        for (const auto& puff : s.impactEffects)
            if (puff.animationFrameDelayTicks != frameDelay || puff.animationFrameCount != 20
                || puff.animationStartFrame != 3 || puff.animationSequenceSteps != 17
                || puff.totalTicks != 17 * frameDelay)
                return fail(std::string(animation) + " changed playback instead of emission cadence");
        std::cout << "Missile smoke cadence: " << animation << " hz=" << hz
            << " puffs/3s=" << emitted << " previous=" << oldCount
            << ", original frame rate, attached flame 0..2 / smoke 3..19 PASS\n";
    }
    for (const std::string type : {"DRED","BSUB","V3"}) for (int hz : {30,60}) {
        auto storage=fixture(type,hz);auto& s=*storage;
        RuntimeObjectBehavior attack;attack.kind=RuntimeBehaviorKind::Attack;
        attack.state=RuntimeBehaviorState::Queued;attack.targetObjectId=2;s.behaviors[1]=attack;
        advanceRuntimeSpecialMechanisms(s);
        const auto* weapon=s.rules.findWeaponType(type=="DRED"?"DredLauncher":type=="BSUB"?"CruiseLauncher":"V3Launcher");
        if (!weapon || !launchRuntimeSpawnedMissileVolley(s,*findObjectById(s,1),*findObjectById(s,2),*weapon,1.0))
            return fail(type+" could not launch");
        const int tracked=s.specialMechanisms.spawnedMissiles().begin()->first;
        bool rise=false,flat=false,dive=false,tail=false;
        double cruiseDistance=0.0;
        std::set<std::string> captured;
        std::set<int> savedStages;
        std::ofstream path;
        if (!root.empty()) {
            path.open(root+"/"+type+"-"+std::to_string(hz)+".csv");
            path<<"tick,stage,x,y,height,pitch\n";
        }
        const auto capture=[&](const std::string& name) {
            if (hz!=30 || !captured.insert(name).second) return true;
            refreshRuntimeSpecialMechanismRenderSnapshots(s);
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{14,10},880,540,error,
                root.empty()?"":root+"/"+type+"-"+name+".bmp",false);
            return stats && stats->specialMechanismRenderActors>0
                && stats->missileExhaustDrawn>0 && stats->missileExhaustHeadDrawn>0
                && stats->missileExhaustMissing==0;
        };
        for (int tick=0;tick<hz*85 && !s.specialMechanisms.spawnedMissiles().empty();++tick) {
            const auto before=s.specialMechanisms.spawnedMissiles();
            ++s.tick;advanceRuntimeEffects(s);
            const auto found=s.specialMechanisms.spawnedMissiles().find(tracked);
            if (found==s.specialMechanisms.spawnedMissiles().end()) continue;
            const auto& m=found->second;
            if (path) path<<s.tick<<','<<m.flightStage<<','<<m.x<<','<<m.y<<','<<m.z<<','<<m.pitch<<'\n';
            if (!m.released) {
                if (std::any_of(s.impactEffects.begin(),s.impactEffects.end(),[](const auto& e){return e.renderIntent=="missileExhaust";}))
                    return fail(type+" emitted exhaust before release");
                continue;
            }
            const auto& old=before.at(tracked);
            for (const auto& effect:s.impactEffects) if (effect.renderIntent=="missileExhaust") {
                if (effect.animId!="V3TRAIL" || effect.animationFrameCount!=20
                    || effect.animationStartFrame!=3 || effect.totalTicks!=34*hz/30)
                    return fail(type+" detached exhaust did not retain original V3TRAIL smoke frames/rate");
                tail=true;
            }
            const bool isRising=type=="V3" ? m.pitch>0.2 && m.z>m.originZ+1.0 : m.flightStage==0 && m.z>m.originZ+1.0;
            const bool isFlat=std::abs(m.pitch)<0.05 && (type=="V3" || m.flightStage==1);
            const bool isDiving=m.pitch<(type=="V3"?-0.35:-0.8) && m.z>m.targetZ+0.6;
            if (type!="V3" && m.flightStage==0) {
                if (std::hypot(m.x-m.originX,m.y-m.originY)>1e-8 || std::abs(m.pitch-runtimeMissileHalfPi)>1e-8)
                    return fail(type+" climbed diagonally instead of vertically");
            }
            if (type!="V3" && m.flightStage==1 && std::abs(m.pitch)<1e-8 && std::abs(old.pitch)<1e-8) {
                if (std::abs(m.z-old.z)>1e-8) return fail(type+" failed to hold horizontal cruise height");
                cruiseDistance+=std::hypot(m.x-old.x,m.y-old.y);
            }
            if (isRising) {rise=true;if (!capture("rise")) return fail(type+" rising resource render: "+error);}
            if (isFlat) {flat=true;if (!capture("cruise")) return fail(type+" cruise resource render: "+error);}
            if (isDiving) {dive=true;if (!capture("dive")) return fail(type+" dive resource render: "+error);}
            const int emissionDelay = runtimeMissileExhaustEmissionDelay(s.art.findArtType("V3TRAIL")->rate, hz);
            if ((s.tick - m.releaseTick) % emissionDelay == emissionDelay - 1) {
                if (isRising && !capture("rise-between-puffs")) return fail(type+" lost attached flame between ascent puffs");
                if (isFlat && !capture("cruise-between-puffs")) return fail(type+" lost attached flame between cruise puffs");
                if (isDiving && !capture("dive-between-puffs")) return fail(type+" lost attached flame between dive puffs");
            }
            // Restore each phase and continue the actual shot, not just a detached parser fixture.
            if ((isRising||isFlat||isDiving) && savedStages.insert(type=="V3"?(isDiving?2:isFlat?1:0):m.flightStage).second) {
                std::ostringstream saved;s.specialMechanisms.save(saved);IniFile ini;
                RuntimeSpecialMechanismState restored;restored.reset(s.rules.getSpecialMechanismRules());
                if (!ini.loadFromString(saved.str()) || !restored.load(ini,error)
                    || restored.stateHash()!=s.specialMechanisms.stateHash()
                    || restored.spawnedMissiles().at(tracked).flightStage!=m.flightStage)
                    return fail(type+" lost flight phase on reload: "+error);
                // The previous 27-field section remains readable.
                std::istringstream legacyInput(saved.str());std::ostringstream legacyText;
                bool inMissiles=false;std::string line;
                while (std::getline(legacyInput,line)) {
                    if (!line.empty() && line.front()=='[') inMissiles=line=="[SpecialSpawnedMissiles]";
                    if (inMissiles && line=="Version=4") line="Version=1";
                    else if (inMissiles && line.find('|')!=std::string::npos) for (int field=0; field<5; ++field) line.erase(line.rfind('|'));
                    legacyText<<line<<'\n';
                }
                IniFile legacy;RuntimeSpecialMechanismState older;
                older.reset(s.rules.getSpecialMechanismRules());
                if (!legacy.loadFromString(legacyText.str()) || !older.load(legacy,error)
                    || older.spawnedMissiles().size()!=s.specialMechanisms.spawnedMissiles().size())
                    return fail(type+" rejected the previous missile save version: "+error);
                s.specialMechanisms=std::move(restored);
            }
        }
        if (!rise||!flat||!dive||!tail||!s.specialMechanisms.spawnedMissiles().empty()
            || findObjectById(s,2)->health>=3000 || (type!="V3" && cruiseDistance<1.0))
            return fail(type+" incomplete staged flight / exhaust / impact: rise="+std::to_string(rise)
                +" flat="+std::to_string(flat)+" dive="+std::to_string(dive)+" tail="+std::to_string(tail)
                +" cruise="+std::to_string(cruiseDistance));
        std::cout<<"Missile flight: "<<type<<" "<<hz<<" Hz, rise/cruise/dive, original exhaust, phase reload, impact passed\n";
    }
    auto ordinary=fixture("FV",30);auto& s=*ordinary;
    const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,1),findObjectById(s,2));
    if (!profile.weapon) return fail("IFV missile weapon missing");
    spawnRuntimeProjectile(s,*findObjectById(s,1),*findObjectById(s,2),*profile.weapon,profile,1.0);
    bool smallExhaust=false;
    for (int i=0;i<180&&!s.projectiles.empty();++i) {
        ++s.tick;advanceRuntimeEffects(s);
        for (const auto& puff:s.impactEffects) if (puff.renderIntent=="missileExhaust" && puff.animId=="SMOKEY2") {
            if (!smallExhaust) {
                const auto stats=drawRuntimeStateOffscreenAtCell(s,{14,10},880,540,error,
                    root.empty()?"":root+"/IFV-flight.bmp",false);
                if (!stats || stats->projectileLineTrailSegments<=0 || stats->missileExhaustDrawn!=0 || stats->missileExhaustHeadDrawn!=0)
                    return fail("ordinary missile authored line trail render: "+error);
            }
            smallExhaust=true;
        }
    }
    if (!smallExhaust || findObjectById(s,2)->health>=3000) return fail("ordinary guided missile exhaust / damage");
    for (const auto* excluded:{"JUMP","DOGJUMP","ADOGJUMP","Torpedo","ProtonTorpedo","NormalBomb","DepthCharge","Cannon"}) {
        const auto* projectile=s.rules.findProjectileType(excluded);
        if (projectile && !runtimeMissileExhaustImage(projectile,projectile->image).empty())
            return fail(std::string("non-missile was given rocket exhaust: ")+excluded);
    }
    std::cout<<"Missile exhaust: real IFV projectile, authored line trail, non-missile exclusions passed\n";
    for (const int hz : {30,60}) for (const bool fromSilo : {true,false}) {
        if (!fromSilo && hz == 60) continue;
        auto nuclear=fixture("FV",hz); auto& n=*nuclear;
        n.config.superweapons=true;
        n.mapFacts=live.mapFacts; n.mapFactsLoaded=live.mapFactsLoaded;
        if (fromSilo) {
            auto* silo=findMutableObjectById(n,1);
            silo->type="NAMISL"; silo->kind=WorldObjectKind::Structure;
            silo->health=1000; silo->mission="Guard"; silo->level=2;
            auto reactor=*silo; reactor.instanceId=3; reactor.type="NANRCT";
            reactor.x=reactor.y=25; reactor.cell=runtimeCellKey(25,25); reactor.level=0;
            n.world.accessObjects().push_back(reactor);
            for (auto& cell:n.world.accessTerrainCells()) {
                if (cell.x>=6 && cell.x<=13 && cell.y>=8 && cell.y<=15) cell.level=2;
                if (cell.x>=18 && cell.x<=23 && cell.y>=8 && cell.y<=15) cell.level=1;
            }
            findMutableObjectById(n,2)->level=1;
            initializeRuntimeNavigation(n);
        }
        ensureRuntimeSuperWeaponsInitialized(n);
        auto context=runtimeSuperWeaponContext(n); n.superWeapons.tick(context);
        if ((!fromSilo && !n.superWeapons.grant("Russians","NukeSpecial",-1,&context))
            || !n.superWeapons.forceReady("Russians","NukeSpecial",&context)
            || !n.superWeapons.beginTargetSelection("Russians","NukeSpecial",context,&error)
            || !n.superWeapons.submitTarget("Russians","NukeSpecial",{{20,10},2},context,&error))
            return fail("nuclear missile launch: "+error);
        bool rose=false,fell=false,upExhaust=false,downExhaust=false,sky=false;
        const auto* nuclearRule=n.rules.findSuperWeaponType("NukeSpecial");
        const int flightTicks=superWeaponNuclearFlightFrames(*nuclearRule,fromSilo)*hz/15;
        const int buildupTicks=runtimeSpecialDetailAnimationDurationTicks(n,"NUKEBALL");
        int continuationTicks=0; double previousRise=-1.0,previousFall=1e9;
        SuperWeaponCell origin;
        std::unique_ptr<RuntimeWindowState> continuation;
        std::ofstream path;
        if (!root.empty()) {
            path.open(root+"/nuclear-"+std::to_string(hz)+(fromSilo?"-silo.csv":"-virtual.csv"));
            path<<"tick,phase,visible,x,y,level\n";
        }
        for (int step=0;step<flightTicks+buildupTicks+2;++step) {
            ++n.tick; advanceRuntimeEffects(n);
            if (continuation) {
                // Ordinary saves omit existing transient smoke. Verify the
                // flight itself and newly emitted exhaust after restoration.
                const auto freshExhaust=[](const auto& state) {
                    std::vector<std::array<double,3>> points;
                    for (const auto& puff:state.impactEffects)
                        if (puff.animId=="NUKEPUFF" && puff.remainingTicks==puff.totalTicks)
                            points.push_back({puff.worldX,puff.worldY,puff.worldLevel});
                    return points;
                };
                if (continuation->superWeapons.stateHash()!=n.superWeapons.stateHash()
                    || freshExhaust(*continuation)!=freshExhaust(n)) {
                    if (!root.empty()) {
                        std::ofstream original(root+"/resume-original.ini"), restored(root+"/resume-loaded.ini");
                        writeRuntimeStateToStream(n,original,error);
                        writeRuntimeStateToStream(*continuation,restored,error);
                    }
                    return fail("nuclear flight diverged after full reload tick="+std::to_string(n.tick)
                        +" core="+std::to_string(n.superWeapons.stateHash())+"/"
                        +std::to_string(continuation->superWeapons.stateHash())
                        +" puffs="+std::to_string(n.impactEffects.size())+"/"
                        +std::to_string(continuation->impactEffects.size()));
                }
                if (++continuationTicks>=2) continuation.reset();
                else { ++continuation->tick; advanceRuntimeEffects(*continuation); }
            }
            const auto& snapshots=n.superWeapons.renderSnapshots();
            const auto found=std::find_if(snapshots.begin(),snapshots.end(),[](const auto& m) {
                return m.kind=="superWeaponProjectile";
            });
            if (found==snapshots.end()) continue;
            const auto missile=*found;
            const auto pose=runtimeSuperWeaponNuclearFlightPose(n,missile);
            if (path) path<<n.tick<<','<<missile.phase<<','<<pose.visible<<','
                <<pose.x<<','<<pose.y<<','<<pose.level<<'\n';
            if (missile.requiredProgress!=flightTicks) return fail("nuclear original vertical flight timing");
            if (missile.progress==0) origin=missile.cell2;
            if (fromSilo && (missile.sourceObjectId!=1 || missile.cell2!=origin))
                return fail("missile switched launch origin after source loss");
            if (pose.visible && pose.rising) {
                if (!fromSilo || pose.x!=origin.x || pose.y!=origin.y || pose.level<previousRise)
                    return fail("nuclear ascent was not vertical above its silo");
                previousRise=pose.level; rose=true;
            } else if (pose.visible) {
                if (pose.x!=20.0 || pose.y!=10.0 || pose.level>previousFall)
                    return fail("nuclear descent was not vertical above target");
                previousFall=pose.level; fell=true;
            } else sky=true;
            for (const auto& puff:n.impactEffects) if (puff.renderIntent=="missileExhaust") {
                if (puff.animId!="NUKEPUFF" || puff.animationFrameCount!=20)
                    return fail("nuclear original exhaust resource/lifetime");
                if (puff.projectileImageId=="NKMSLUP") upExhaust=true;
                if (puff.projectileImageId=="NKMSLDN") downExhaust=true;
            }
            const int age=missile.progress;
            const bool capture=fromSilo && hz==30 && (age==0 || age==hz/6 || age==hz/3
                || age==hz/2 || age==flightTicks-hz/5 || age==flightTicks-hz/10 || age==flightTicks-1);
            if (capture) {
                refreshRuntimeSpecialMechanismRenderSnapshots(n);
                const auto stats=drawRuntimeStateOffscreenAtCell(n,pose.rising?RuntimeCellPoint{9,11}:RuntimeCellPoint{20,10},
                    880,640,error,root.empty()?"":root+"/nuclear-"+std::to_string(age)+".bmp",false);
                if (!stats || stats->projectileActorTravelMissing || stats->missileExhaustMissing
                    || stats->texturedTerrainCells<=0 || stats->projectileActorTravelDrawn<=0
                    || stats->missileExhaustHeadDrawn<=0)
                    return fail("nuclear original body/exhaust render age="+std::to_string(age)+": "+error);
            }
            if (age==hz/3 || age==flightTicks-hz/5) {
                std::ostringstream saved;
                if (!writeRuntimeStateToStream(n,saved,error)) return fail("nuclear flight save: "+error);
                IniFile ini; continuation=std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*continuation,n,false);
                if (!ini.loadFromString(saved.str()) || !materializeRuntimeLoadStateFromIni(*continuation,ini,error)
                    || continuation->superWeapons.stateHash()!=n.superWeapons.stateHash())
                    return fail("nuclear flight reload: "+error);
                continuationTicks=0;
                ++continuation->tick; advanceRuntimeEffects(*continuation);
            }
            // At 60 Hz remove the source during ascent. The launched payload
            // must keep its original column and finish even without a silo.
            if (fromSilo && hz==60 && age==hz/2) {
                n.world.accessObjects().erase(std::remove_if(n.world.accessObjects().begin(),
                    n.world.accessObjects().end(),[](const auto& o){return o.instanceId==1;}),n.world.accessObjects().end());
            }
        }
        if (rose!=fromSilo || !fell || !sky || upExhaust!=fromSilo || !downExhaust
            || findObjectById(n,2)->health>=3000)
            return fail("incomplete nuclear flight / exhaust / impact");
        std::cout<<"Nuclear flight: "<<hz<<" Hz, source="<<(fromSilo?"silo":"virtual")
            <<", vertical rise/descent, latched origin, real terrain, original frames, full reload, impact PASS\n";
    }
    return true;
}
