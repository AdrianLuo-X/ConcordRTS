bool runRuntimeJumpjetKinematicsSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const char* section = std::getenv("CONCORD_JUMPJET_SMOKE_SECTION");
    const auto run = [&](const char* name) { return !section || std::string(section)==name; };
    const auto fail = [&](const std::string& why) { error = "Jumpjet kinematics: " + why; return false; };
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original rules unavailable");
    for (const auto& expected : std::array<std::tuple<const char*, double, double, int>, 4>{
            {{"JUMPJET",30,20,10}, {"ZEP",5,6,2}, {"SHAD",30,10,6}, {"DISK",16,8,100}}}) {
        const auto* unit = findRuntimeUnitTypeById(stock->rules, std::get<0>(expected));
        if (!unit || !runtimeUnitUsesJumpjetMovement(*unit) || unit->jumpjetSpeed != std::get<1>(expected)
            || unit->jumpjetClimb != std::get<2>(expected) || unit->jumpjetTurnRate != std::get<3>(expected))
            return fail(std::string("stock parameters: ")+std::get<0>(expected));
    }
    const auto makeWorld = [&](const RulesDatabase* rules = nullptr, bool originalTerrain = false) {
        auto result = std::make_unique<RuntimeWindowState>(); auto& s = *result;
        s.config = config; s.config.fixedTickHz = 30; s.config.gameMode = "Battle";
        s.config.computerAiEnabled = false; s.config.shortGame = false;
        s.rules = rules ? *rules : stock->rules; s.art = stock->art; s.theater = stock->theater;
        s.report = stock->report; s.mapFacts = stock->mapFacts;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.economy.owner = "Americans"; s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimeSoundsEnabled = false;
        s.runtimePreferencePersistenceEnabled = false; s.triggers.loaded = s.triggers.bootstrapComplete = true;
        if (originalTerrain) {
            s.world = stock->world; s.world.accessObjects().clear();
            s.config.playerSlots.resize(1);
            auto& slot=s.config.playerSlots[0]; slot.enabled=slot.human=slot.local=true;
            slot.country="Americans";slot.name="Jumpjet probe";slot.peerId="jumpjet-probe-peer";slot.team="-";slot.startPosition=1;
            s.economy.owner=runtimeSkirmishOwnerForSlot(0);
            s.skirmishCountryByOwner[runtimeCanonicalHumanHouseOwner(s.economy.owner)]="Americans";
        }
        else for (int y=0; y<64; ++y) for (int x=0; x<64; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s); return result;
    };
    const auto custom = [&](int ordinarySpeed, double speed, double acceleration, double climb,
            int turn, double wobbles = 0.0, bool noWobbles = true) {
        std::ostringstream text;
        text << "[InfantryTypes]\n0=TEST\n[TEST]\nStrength=100\nSpeed=" << ordinarySpeed
            << "\nJumpJet=yes\nBalloonHover=yes\nJumpjetHeight=500\nJumpjetSpeed=" << speed
            << "\nJumpjetAccel=" << acceleration << "\nJumpjetClimb=" << climb
            << "\nJumpjetTurnRate=" << turn << "\nJumpjetWobbles=" << wobbles
            << "\nJumpjetDeviation=64\nJumpjetNoWobbles=" << (noWobbles ? "yes" : "no") << '\n';
        IniFile ini; ini.loadFromString(text.str()); RulesDatabase rules; rules.loadFromIni(ini); return rules;
    };
    const auto add = [&](RuntimeWindowState& s, const std::string& type, RuntimeCellPoint cell = {4,4}) {
        auto* object = addRuntimeObjectInstance(s, runtimeBuildKindForUnitTypeId(s.rules,type), type, cell);
        if (!object) return -1;
        object->facing=runtimeFacingFromDelta(1,0); s.selectedObjectIds={object->instanceId}; return object->instanceId;
    };
    const auto step = [&](RuntimeWindowState& s) {
        ++s.tick; advanceRuntimeJumpjetFlights(s); advanceRuntimeBuildAnimations(s); advanceRuntimeDeployTransitions(s);
        processRuntimePathRequests(s); prepareRuntimeInfantryStepReservations(s);
        std::vector<int> ids; for (const auto& [id,b] : s.behaviors) { (void)b; ids.push_back(id); }
        std::sort(ids.begin(),ids.end());
        for (int id : ids) {
            auto* o=findMutableObjectById(s,id); auto found=s.behaviors.find(id);
            if (!o || !o->alive || found==s.behaviors.end()) continue;
            if (found->second.kind==RuntimeBehaviorKind::Move) advanceRuntimeMoveBehavior(s,*o,found->second);
            else if (found->second.kind==RuntimeBehaviorKind::Attack) advanceRuntimeAttackBehavior(s,*o,found->second);
        }
        for (auto& object : s.world.accessObjects()) applyRuntimeJumpjetPose(s,object);
    };
    if (run("speed")) {
        const auto slowRules=custom(99,12,2,5,128), fastRules=custom(1,30,10,20,128);
        auto slow=makeWorld(&slowRules), fast=makeWorld(&fastRules);
        const int a=add(*slow,"TEST"), b=add(*fast,"TEST");
        if (a<0 || b<0) return fail("custom flyer's creation");
        issueRuntimeMoveCommand(*slow,{40,4}); issueRuntimeMoveCommand(*fast,{40,4});
        for (int n=0; n<20; ++n) {
            step(*slow); step(*fast);
            if (n==4 && (slow->jumpjetFlights.at(a).speed!=4*runtimeJumpjetQ16
                || fast->jumpjetFlights.at(b).speed!=20*runtimeJumpjetQ16))
                return fail("initial acceleration was skipped");
        }
        const auto& sf=slow->jumpjetFlights.at(a); const auto& ff=fast->jumpjetFlights.at(b);
        if (sf.height != 50*runtimeJumpjetQ16 || ff.height != 200*runtimeJumpjetQ16
            || sf.speed > 12*runtimeJumpjetQ16 || ff.speed != 30*runtimeJumpjetQ16
            || findObjectById(*slow,a)->renderX >= findObjectById(*fast,b)->renderX)
            return fail("speed/acceleration/climb did not replace ordinary Speed");
        const auto paused=serializeRuntimeJumpjetFlights(fast->jumpjetFlights);
        for (int n=0; n<5; ++n) advanceRuntimeJumpjetFlights(*fast);
        if (paused!=serializeRuntimeJumpjetFlights(fast->jumpjetFlights)) return fail("same-tick/pause advanced flight");
        // Stop during a real segment: no jump at acceptance and no new cells.
        while (!fast->jumpjetFlights.at(b).active || fast->jumpjetFlights.at(b).progress==0) step(*fast);
        const auto* before=findObjectById(*fast,b); const double x=before->renderX, y=before->renderY;
        const int cell=before->cell;
        issueRuntimeStopCommand(*fast);
        if (findObjectById(*fast,b)->renderX!=x || findObjectById(*fast,b)->renderY!=y) return fail("stop snapped position");
        const auto stoppedHeight=fast->jumpjetFlights.at(b).height;
        for (int n=0; n<120; ++n) {
            step(*fast); const auto& f=fast->jumpjetFlights.at(b); const auto* o=findObjectById(*fast,b);
            if (f.speed!=0 || f.active || !f.stopped || o->renderMoveActive
                || o->renderX!=x || o->renderY!=y || o->cell!=cell)
                return fail("stopped flight moved horizontally");
        }
        if (fast->jumpjetFlights.at(b).height<=stoppedHeight) return fail("stopped flight halted normal climb");
        issueRuntimeMoveCommand(*fast,{40,4});
        if (findObjectById(*fast,b)->renderX!=x || findObjectById(*fast,b)->renderY!=y)
            return fail("resuming stopped flight snapped position");
        bool resumed=false; double previousX=x;
        for (int n=0;n<10;++n) {
            step(*fast); const auto* o=findObjectById(*fast,b);
            if(o->renderX+1e-9<previousX || o->renderX-previousX>0.061)
                return fail("resumed stopped flight was discontinuous");
            resumed=resumed || o->renderX>x;
            previousX=o->renderX;
        }
        if(!resumed) return fail("stopped flight did not resume");
        auto immediate=makeWorld(&fastRules); const int z=add(*immediate,"TEST");
        issueRuntimeMoveCommand(*immediate,{40,4});step(*immediate);
        issueRuntimeStopCommand(*immediate);
        for(int n=0;n<10;++n)step(*immediate);
        if(findObjectById(*immediate,z)->x!=4 || findObjectById(*immediate,z)->renderX!=4.0)
            return fail("a stopped, unstarted segment accelerated away");
        auto departing=makeWorld(&fastRules);const int d=add(*departing,"TEST");
        auto* grounded=findMutableObjectById(*departing,d);
        grounded->airborne=false;grounded->flightLevel=grounded->level=0;grounded->hasRenderPosition=false;
        departRuntimeAircraftDock(*departing,*grounded);
        if(!grounded->airborne || grounded->flightLevel!=0)return fail("ground departure skipped the climb");
        step(*departing);
        if(departing->jumpjetFlights.at(d).height!=10*runtimeJumpjetQ16)return fail("ground departure climb rate");
        std::cout << "Jumpjet speed/climb: stock=4 profiles speed overrides=yes acceleration=yes braking=yes pause=yes\n";
    }
    if (run("hover")) {
        const auto slowRules=custom(9,30,10,20,2), fastRules=custom(9,30,10,20,20);
        auto slow=makeWorld(&slowRules), fast=makeWorld(&fastRules);
        const int a=add(*slow,"TEST"),b=add(*fast,"TEST");
        findMutableObjectById(*slow,a)->facing=0; findMutableObjectById(*fast,b)->facing=0;
        issueRuntimeMoveCommand(*slow,{20,4}); issueRuntimeMoveCommand(*fast,{20,4});
        for(int n=0;n<8;++n){step(*slow);step(*fast);}
        if (findObjectById(*slow,a)->facing==findObjectById(*fast,b)->facing
            || findObjectById(*slow,a)->facing>8) return fail("infantry/locomotor turn rate ignored");
        const auto wobbleRules=custom(9,30,10,1000,10,1.0,false), steadyRules=custom(9,30,10,1000,10,1.0,true);
        auto wobble=makeWorld(&wobbleRules),steady=makeWorld(&steadyRules);
        const int w=add(*wobble,"TEST"),q=add(*steady,"TEST");
        bool varied=false;
        for(int n=0;n<60;++n){
            step(*wobble);step(*steady);
            const double z=findObjectById(*wobble,w)->renderLevel;
            varied=varied||std::abs(z-500.0/256.0)>0.1;
            if (!std::isfinite(z) || std::abs(z-500.0/256.0)>0.25001
                || findObjectById(*steady,q)->renderLevel!=500.0/256.0) return fail("hover amplitude/NoWobbles");
        }
        if(!varied) return fail("wobble never changed rendered altitude");
        if (slowRules.getDetailRulesHash()==fastRules.getDetailRulesHash()
            || wobbleRules.getDetailRulesHash()==steadyRules.getDetailRulesHash()) return fail("flight rules fingerprint");
        std::cout << "Jumpjet turn/hover: TurnRate=yes Wobbles+Deviation=yes NoWobbles=yes rules fingerprint=yes\n";
    }
    if (run("clock")) {
        const auto rules=custom(9,30,10,10,10);
        for (const int hz : {15,30,60}) {
            auto s=makeWorld(&rules); s->config.fixedTickHz=hz;
            const int id=add(*s,"TEST");
            for(int n=0;n<hz;++n)step(*s);
            if(s->jumpjetFlights.at(id).height!=150*runtimeJumpjetQ16)
                return fail("climb depends on simulation tick rate");
        }
        std::cout << "Jumpjet clock: 15/30/60 Hz vertical conversion=yes\n";
    }
    if (run("cadence")) {
        const auto rules=custom(9,30,0,1000,128);
        for (const int hz : {15,30,60}) {
            auto s=makeWorld(&rules); s->config.fixedTickHz=hz;
            const int id=add(*s,"TEST");
            auto* object=findMutableObjectById(*s,id);
            object->airborne=true; object->hasRenderPosition=true;
            object->renderX=object->x; object->renderY=object->y;
            object->renderLevel=500.0/256.0; object->flightLevel=object->level=2;
            issueRuntimeMoveCommand(*s,{40,4});
            for(int n=0;n<hz;++n)step(*s);
            const double start=findObjectById(*s,id)->renderX;
            double previous=start;
            for(int n=0;n<2*hz;++n) {
                step(*s); const double current=findObjectById(*s,id)->renderX;
                const double expected=30.0*15.0/(hz*256.0);
                if(std::abs((current-previous)-expected)>1.0/runtimeJumpjetCellQ16)
                    return fail("cell boundary discarded distance at hz="+std::to_string(hz));
                previous=current;
            }
            if(std::abs(previous-start-900.0/256.0)>1.0/runtimeJumpjetCellQ16)
                return fail("cross-cell cruise duration depends on Hz");
        }
        RuntimeJumpjetFlight completed;
        completed.length=completed.progress=runtimeJumpjetCellQ16;
        completed.toX=1; completed.lastTick=7; completed.segmentCarry=4*runtimeJumpjetQ16;
        RuntimeJumpjetFlights sample{{1,completed}},loaded;
        const auto saved=serializeRuntimeJumpjetFlights(sample);
        if(!deserializeRuntimeJumpjetFlights(saved,loaded)
            || serializeRuntimeJumpjetFlights(loaded)!=saved)
            return fail("boundary carry save roundtrip");
        const auto reject=[&](std::string value) {
            auto staged=loaded;
            return !deserializeRuntimeJumpjetFlights(value,staged)
                && serializeRuntimeJumpjetFlights(staged)==saved;
        };
        sample.at(1).segmentCarry=-1;
        if(!reject(serializeRuntimeJumpjetFlights(sample)))return fail("negative carry accepted");
        sample.at(1)=completed; sample.at(1).active=true; --sample.at(1).progress;
        if(!reject(serializeRuntimeJumpjetFlights(sample)))return fail("active segment carry accepted");
        for(const int schema:{1,2}) {
            const std::string legacy="JumpjetFlights "+std::to_string(schema)
                +" 1\n1 0 0 0 0 4 4 4 4 -1 -1 0 0"
                +(schema==2?" -1 -1 -1 -1 8":"")+"\n";
            if(!deserializeRuntimeJumpjetFlights(legacy,loaded) || loaded.at(1).segmentCarry!=0)
                return fail("legacy flight did not default to zero carry");
        }
        std::cout<<"Jumpjet cadence: 15/30/60Hz continuous cell crossing; carry save/legacy/malformed atomic PASS\n";
    }
    if (run("save")) {
        auto s=makeWorld(nullptr,true);
        RuntimeCellPoint start{-1,-1};
        for(const auto& cell:s->world.getTerrainCells()){
            bool valid=cell.x>=4 && cell.x<480 && cell.y>=4 && cell.y<480;
            for(int dx=0;valid && dx<20;++dx){
                const auto found=s->terrainByCell.find(runtimeCellKey(cell.x+dx,cell.y));
                valid=found!=s->terrainByCell.end() && found->second && found->second->level==cell.level;
            }
            if(valid){start={cell.x,cell.y};break;}
        }
        if(start.x<0) return fail("stock map has no save probe route");
        const int id=add(*s,"SHAD",start);
        if(id<0) return fail("stock save flyer");
        issueRuntimeMoveCommand(*s,{start.x+16,start.y});
        for(int n=0;n<13;++n)step(*s);
        if(!s->jumpjetFlights.at(id).active) return fail("save was not taken during flight");
        for (bool coasting : {false,true}) {
        if(coasting)issueRuntimeStopCommand(*s);
        std::ostringstream saved; if(!writeRuntimeStateToStream(*s,saved,error))return false;
        IniFile ini;if(!ini.loadFromString(saved.str()))return fail("save INI parse");
        auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,*s,false);
        if(!materializeRuntimeLoadStateFromIni(*restored,ini,error))return false;
        if(serializeRuntimeJumpjetFlights(s->jumpjetFlights)!=serializeRuntimeJumpjetFlights(restored->jumpjetFlights))
            return fail("full save lost velocity/height/segment/phase");
        initializeRuntimeNavigation(*restored);
        for(int n=0;n<4;++n){
            const auto* a=findObjectById(*s,id);const auto* b=findObjectById(*restored,id);
            if(a->renderX!=b->renderX || a->renderY!=b->renderY || a->renderLevel!=b->renderLevel
                || serializeRuntimeJumpjetFlights(s->jumpjetFlights)!=serializeRuntimeJumpjetFlights(restored->jumpjetFlights))
                return fail("saved airborne pose or continuation diverged");
            step(*s);step(*restored);
        }
        }
        const auto unchanged=serializeRuntimeJumpjetFlights(s->jumpjetFlights);
        RuntimeJumpjetFlights rejected=s->jumpjetFlights;
        if(deserializeRuntimeJumpjetFlights(unchanged+" trailing",rejected)
            || serializeRuntimeJumpjetFlights(rejected)!=unchanged) return fail("malformed state was not atomic");
        // Multiplayer snapshots prohibit the offline no-cooldown option.
        // Match the normal multiplayer launch configuration before hashing.
        s->config.noCooldown=false;
        s->config.multiplayerActive=true;s->config.multiplayerSetupHash=1;
        s->config.multiplayerCanonicalMapId="jumpjet-probe";s->config.multiplayerMapContentFingerprint=2;
        s->config.multiplayerTimelineEpoch=1;s->config.simulationRandomSeed=1;
        RuntimeHumanHouseAuthority local;local.owner=s->economy.owner;
        local.canonicalOwner=runtimeCanonicalHumanHouseOwner(local.owner);local.country="Americans";local.localFacade=true;
        s->humanHouseAuthorities={local};completeRuntimeWorldRefresh(*s);
        initializeRuntimeLocalProductionLanes(s->economy);
        s->particles.reset(s->config.fixedTickHz);s->particlesInitialized=true;
        s->superWeapons.reset(std::vector<SuperWeaponRule>{});s->superWeaponsInitialized=true;
        if(!s->particles.alignEmptyStateTick(s->tick) || !s->superWeapons.alignEmptyStateTick(s->tick))
            return fail("snapshot subsystem clocks");
        std::string hashError;const auto hash=runtimeMultiplayerAuthoritativeStateHash(*s,hashError);
        if(hash==0)return fail("authority fixture: "+hashError);
        ++s->jumpjetFlights.at(id).speed;
        if(hash==runtimeMultiplayerAuthoritativeStateHash(*s,hashError))return fail("velocity absent from authority hash");
        --s->jumpjetFlights.at(id).speed;
        RuntimeMultiplayerSnapshotIdentity identity;identity.proposalId=1;
        identity.sourceTimelineEpoch=1;identity.snapshotTick=identity.barrierTick=s->tick;
        identity.snapshotToken="jumpjet-flight";identity.authoritativeStateHash=hash;identity.gameplaySetupFingerprint=1;
        RuntimeMultiplayerSnapshotMemoryImage image;RuntimeMultiplayerSnapshotStagedState staged;
        if(!exportRuntimeMultiplayerSnapshotV15(*s,identity,image,hashError)
            || !stageRuntimeMultiplayerSnapshot(*s,image.bytes,image.descriptor,staged,hashError))
            return fail("live flight snapshot: "+hashError);
        commitRuntimeMultiplayerSnapshotAuthority(*s,std::move(staged));
        if(runtimeMultiplayerAuthoritativeStateHash(*s,hashError)!=hash
            || serializeRuntimeJumpjetFlights(s->jumpjetFlights)!=unchanged)
            return fail("committed flight snapshot changed authority");
        std::cout << "Jumpjet save: moving+coasting full save+navigation rebuild=yes snapshot stage+commit=yes malformed atomic=yes authority=yes\n";
    }
    if (run("deploy")) {
        auto s=makeWorld();const int id=add(*s,"SCHP");if(id<0)return fail("stock siege chopper");
        for(int n=0;n<300;++n)step(*s);
        auto* o=findMutableObjectById(*s,id);
        const double cruise=double(s->jumpjetFlights.at(id).height)/runtimeJumpjetCellQ16;
        if(!executeRuntimeSecondaryAction(*s,id))return fail("chopper deployment rejected");
        step(*s);o=findMutableObjectById(*s,id);
        if(!o->airborne || o->renderLevel>=cruise || o->renderLevel<=0)return fail("deployment jumped to ground");
        for(int n=0;n<800 && !findObjectById(*s,id)->deployed;++n)step(*s);
        o=findMutableObjectById(*s,id);
        if(!o->deployed || o->airborne)return fail("landing deployment did not complete");
        if(!executeRuntimeSecondaryAction(*s,id))return fail("chopper undeployment rejected");
        bool intermediate=false;
        for(int n=0;n<800;++n){
            step(*s);o=findMutableObjectById(*s,id);
            intermediate=intermediate||(o->airborne && o->renderLevel>0 && o->renderLevel<cruise);
            if(o->airborne && o->mission==runtimeMissionName(RuntimeMissionIntent::Idle) && !o->deployed)break;
        }
        o=findMutableObjectById(*s,id);
        if(!intermediate || o->deployed || !o->airborne
            || s->jumpjetFlights.at(id).height!=std::llround(cruise*runtimeJumpjetCellQ16))
            return fail("undeploy did not climb continuously back to cruise");
        issueRuntimeMoveCommand(*s,{24,4});
        for(int n=0;n<100;++n){step(*s);if(s->jumpjetFlights.at(id).active && s->jumpjetFlights.at(id).progress>0)break;}
        o=findMutableObjectById(*s,id);const double movingX=o->renderX,movingZ=o->renderLevel;
        if(!s->jumpjetFlights.at(id).active || !executeRuntimeSecondaryAction(*s,id))
            return fail("moving chopper deployment rejected");
        o=findMutableObjectById(*s,id);
        if(o->renderX!=movingX || o->renderLevel!=movingZ)return fail("moving deployment snapped pose");
        double lastX=movingX,lastZ=movingZ;bool landed=false;
        for(int n=0;n<800;++n){
            step(*s);o=findMutableObjectById(*s,id);
            if(std::abs(o->renderX-lastX)>0.1 || std::abs(o->renderLevel-lastZ)>0.1)
                return fail("moving deployment jumped horizontally or vertically");
            lastX=o->renderX;lastZ=o->renderLevel;
            if(o->deployed && !o->airborne){landed=true;break;}
        }
        if(!landed)return fail("moving deployment never completed");
        std::cout << "Jumpjet deployment: stock SCHP landing+deployment+takeoff=yes\n";
    }
    if (run("ground-targets")) {
        int scenes=0, domainChecks=0, descendingFrames=0, saves=0;
        const auto* nativeTank=stock->rules.findUnitType("UnitType","HTNK");
        if (!nativeTank || nativeTank->primaryWeapon!="120mm") return fail("ground targets: original HTNK primary");
        const auto checkDomain=[&](RuntimeWindowState& s, int id, bool air, const std::string& phase) {
            const auto* body=findObjectById(s,id);
            if (!body || !body->alive || body->airborne!=air || runtimeObjectIsAirTarget(s,*body)!=air)
                return fail(phase+": current flight target domain");
            for (const auto* weaponId : {"M60","120mm","FlakWeapon"}) {
                const auto* weapon=s.rules.findWeaponType(weaponId);
                const auto* projectile=weapon ? findRuntimeProjectileType(s.rules,weapon->projectile) : nullptr;
                const bool aa=std::string(weaponId)=="FlakWeapon";
                if (!weapon || !projectile || projectile->aa!=aa || projectile->ag==aa)
                    return fail(phase+": original AA/AG weapon definition "+weaponId);
                RuntimeProjectileEffect shot; shot.aa=projectile->aa; shot.ag=projectile->ag;
                if (runtimeWeaponCanTargetObject(s,*weapon,*body)!=(air==aa)
                    || runtimeProjectileCanHitTarget(s,shot,*body)!=(air==aa))
                    return fail(phase+": original weapon/projectile target gate "+weaponId);
            }
            ++domainChecks; return true;
        };
        const auto checkpoint=[&](RuntimeWindowState& s, bool canonical) {
            std::ostringstream base, full, normalized; IniFile ordinary, exact;
            if (!s.particles.alignEmptyStateTick(s.tick) || !s.superWeapons.alignEmptyStateTick(s.tick)) {
                error="ground-target fixture empty authority clocks";
                return std::unique_ptr<RuntimeWindowState>{};
            }
            if (!writeRuntimeStateToStream(s,base,error) || !ordinary.loadFromString(base.str()))
                return std::unique_ptr<RuntimeWindowState>{};
            if (canonical && (!writeRuntimeStateToStream(s,full,error)
                || !writeRuntimeMultiplayerSnapshotExtension(s,full,error) || !exact.loadFromString(full.str())
                || !writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error)
                || !exact.loadFromString(normalized.str()))) return std::unique_ptr<RuntimeWindowState>{};
            auto restored=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored,s,false);
            RuntimeSnapshotActiveStateScope active(*restored);
            if (!materializeRuntimeLoadStateFromIni(*restored,ordinary,error)
                || (canonical && !materializeRuntimeMultiplayerSnapshotExtension(*restored,exact,error)))
                return std::unique_ptr<RuntimeWindowState>{};
            ++saves; return restored;
        };
        for (int hz : {30,60}) for (const std::string type : {"SHAD","HIND","SCHP"}) {
            const auto* native=stock->rules.findUnitType("UnitType",type);
            if (!native) {
                if (type=="HIND") { std::cout<<"Jumpjet ground targets HIND: native registration absent; raw-rule contract required\n"; continue; }
                return fail("ground targets: unregistered stock "+type);
            }
            auto s=makeWorld(nullptr,true); s->config.fixedTickHz=hz;
            s->config.noCooldown=s->config.multiplayerActive=s->config.superweapons=false;
            s->particles.reset(hz); s->particlesInitialized=true;
            s->superWeapons.reset(std::vector<SuperWeaponRule>{}); s->superWeaponsInitialized=true;
            RuntimeSnapshotActiveStateScope active(*s);
            RuntimeCellPoint start{-1,-1}; WorldObjectInstance probe;
            probe.kind=WorldObjectKind::Unit; probe.type=type; probe.owner=s->economy.owner;
            for (const auto& cell:s->world.getTerrainCells()) {
                bool usable=cell.x>=4 && cell.x<480 && cell.y>=4 && cell.y<480;
                for (int dx=0;usable && dx<=3;++dx) {
                    const RuntimeCellPoint p{cell.x+dx,cell.y}; const auto found=s->terrainByCell.find(runtimeCellKey(p.x,p.y));
                    if (found==s->terrainByCell.end() || !found->second || found->second->level!=cell.level) { usable=false; break; }
                    const auto info=runtimeCachedTerrainInfoForCell(*s,*found->second);
                    usable=info.passable && !info.water && !info.bridge && !info.shore && !info.cliff && !info.ramp
                        && findRuntimePassableCellNear(*s,probe,p,0,-1,nullptr,nullptr,nullptr,PathMovementMode::Ground).has_value();
                }
                if (usable) { start={cell.x,cell.y}; break; }
            }
            if (start.x<0) return fail("ground targets: no native dry landing route for "+type);
            const int id=add(*s,type,start); if (id<0) return fail("ground targets: stock "+type);
            const std::string scene=type+"-"+std::to_string(hz);
            if (!checkDomain(*s,id,true,scene+"-initial-flight")) return false;
            if (type=="SCHP") {
                for (int n=0;n<hz*2;++n) step(*s);
                if (!executeRuntimeSecondaryAction(*s,id)) return fail(scene+": native deploy rejected");
            } else {
                issueRuntimeMoveCommand(*s,{start.x+3,start.y});
                if (s->lastCommand.acceptedCount!=1) return fail(scene+": initial native Move rejected");
                bool rose=false;
                for (int n=0;n<hz*10;++n) {
                    step(*s); const auto* body=findObjectById(*s,id);
                    if (!checkDomain(*s,id,true,scene+"-initial-takeoff")) return false;
                    if (runtimeObjectRenderLevel(*body)>terrainLevelAtRuntimeCell(*s,body->x,body->y)+0.25) {
                        rose=true; break;
                    }
                }
                if (!rose) return fail(scene+": initial native takeoff height");
            }
            const double initialZ=runtimeObjectRenderLevel(*findObjectById(*s,id)); bool descended=false, landed=false;
            for (int n=0;n<hz*30;++n) {
                step(*s); const auto* body=findObjectById(*s,id); if (!body) return fail(scene+": missing descent body");
                if (body->airborne) {
                    if (!checkDomain(*s,id,true,scene+"-descending")) return false;
                    if (runtimeObjectRenderLevel(*body)<initialZ && runtimeObjectRenderLevel(*body)>terrainLevelAtRuntimeCell(*s,body->x,body->y)) {
                        descended=true; ++descendingFrames;
                    }
                } else if (type!="SCHP" || body->deployed) { landed=true; break; }
            }
            const auto* grounded=findObjectById(*s,id);
            if (!descended || !landed || !grounded || grounded->health!=native->strength
                || grounded->level!=terrainLevelAtRuntimeCell(*s,grounded->x,grounded->y)
                || !checkDomain(*s,id,false,scene+"-grounded")) return fail(scene+": native touchdown domain");
            const auto groundedBefore=*grounded;
            auto ordinary=checkpoint(*s,false), canonical=checkpoint(*s,true);
            if (!ordinary || !canonical) return fail(scene+": strict grounded save: "+error);
            for (const auto& replica : std::array<std::pair<const char*,RuntimeWindowState*>,3>{{
                    {"original",s.get()},{"ordinary",ordinary.get()},{"canonical",canonical.get()}}}) {
                auto& q=*replica.second; RuntimeSnapshotActiveStateScope replicaActive(q);
                const auto* body=findObjectById(q,id); const std::string label=scene+"-"+replica.first;
                if (!body || body->health!=groundedBefore.health || body->cell!=groundedBefore.cell || body->level!=groundedBefore.level
                    || body->onBridge!=groundedBefore.onBridge || body->deployed!=groundedBefore.deployed
                    || !checkDomain(q,id,false,label+"-restored-ground")) return fail(label+": grounded identity/domain restore");
                q.selectedObjectIds={id};
                if (type=="SCHP") {
                    if (!executeRuntimeSecondaryAction(q,id)) return fail(label+": native undeploy rejected");
                } else {
                    issueRuntimeMoveCommand(q,start);
                    if (q.lastCommand.acceptedCount!=1) return fail(label+": native takeoff Move rejected");
                }
                bool tookOff=false;
                for (int n=0;n<hz*20;++n) {
                    step(q); body=findObjectById(q,id); if (!body) return fail(label+": missing takeoff body");
                    if (body->airborne && runtimeObjectRenderLevel(*body)>terrainLevelAtRuntimeCell(q,body->x,body->y)) {
                        tookOff=true; break;
                    }
                }
                if (!tookOff || !checkDomain(q,id,true,label+"-takeoff")) return fail(label+": native takeoff target domain");
            }
            ++scenes; std::cout<<"Jumpjet ground targets "<<scene<<" PASS native descent/ground/takeoff M60/120mm/FlakWeapon ordinary/canonical\n";
        }
        std::cout<<"Jumpjet ground targets summary PASS scenes="<<scenes<<" domainChecks="<<domainChecks
            <<" descendingFrames="<<descendingFrames<<" saves="<<saves<<'\n';
    }
    if(run("navigation") && !runRuntimeJumpJetMovementSmokeProbe(*stock,error))return false;
    std::cout << "Jumpjet kinematics: passed\n";
    return true;
}
