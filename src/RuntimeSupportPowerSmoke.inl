bool runRuntimeSupportPowerSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& reason) { error = "support powers: " + reason; return false; };
    const char* output = std::getenv("CONCORD_SUPPORT_POWERS_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    const std::set<std::string> expected{"NukeSpecial", "IronCurtainSpecial", "LightningStormSpecial",
        "ChronoSphereSpecial", "ParaDropSpecial", "AmericanParaDropSpecial", "PsychicDominatorSpecial",
        "SpyPlaneSpecial", "GeneticConverterSpecial", "ForceShieldSpecial", "PsychicRevealSpecial"};
    // all01umd supplies the terrain/resource harness but deliberately sets
    // GASPYSAT.SpySat=no for its plot. Test the stock skirmish definitions.
    IniFile stockIni; RulesDatabase stockRules;
    if (!stockIni.loadFromFile(live.report.rulesPath) || !stockRules.loadFromIni(stockIni))
        return fail("could not load original skirmish rules");
    const auto fixture = [&](const std::string& country = "Americans", int hz = 30) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config=live.config; s.config.fixedTickHz=hz; s.config.computerAiEnabled=false;
        s.config.noCooldown=false; // Ordinary recharge; the dedicated repeat fixture enables its own fast mode.
        s.config.superweapons=true; s.report=live.report; s.rules=stockRules; s.rulesLoaded=true;
        s.art=live.art; s.artLoaded=true; s.theater=live.theater; s.theaterLoaded=live.theaterLoaded;
        s.world=live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.economy.owner="Commander"; s.economy.credits=10000; s.economy.techLevel=10;
        s.economy.skirmishStartApplied=true; s.skirmishCountryByOwner["commander"]=country;
        s.triggers.bootstrapComplete=true; s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false;
        for (int y=70;y<=112;++y) for (int x=70;x<=116;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x; cell.tileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        int id=1;
        const auto addBuilding=[&](const BuildingType& building, int x, int y) {
            WorldObjectInstance object; object.instanceId=id++; object.kind=WorldObjectKind::Structure;
            object.type=building.id; object.owner="Commander"; object.alive=true; object.health=building.strength;
            object.x=x; object.y=y; object.cell=runtimeCellKey(x,y); object.mission="Guard";
            s.world.accessObjects().push_back(object);
        };
        // Actual source definitions exercise reconciliation, including the
        // American airfield and a captured neutral airport.
        std::set<std::string> sources;
        for (const auto& sw : expected) for (const auto& building : s.rules.getBuildingTypes()) {
            if (building.superWeapon==sw || building.superWeapon2==sw) {
                if (sources.insert(building.id).second) addBuilding(building,72+(id%4)*3,72+(id/4)*3);
                break;
            }
        }
        if (const auto* power=s.rules.findBuildingType("NANRCT")) {
            addBuilding(*power,72,81); addBuilding(*power,76,81);
        }
        initializeRuntimeNavigation(s); ensureRuntimeSuperWeaponsInitialized(s);
        auto context=runtimeSuperWeaponContext(s); s.superWeapons.tick(context);
        return storage;
    };
    const auto addUnit=[&](RuntimeWindowState& s, const std::string& type, WorldObjectKind kind,
        int x, int y, const std::string& owner="Enemy", int health=0) {
        auto context=runtimeSuperWeaponContext(s);
        WorldObjectInstance object; object.instanceId=context.allocateObjectId(); object.kind=kind;
        object.type=type; object.owner=owner; object.alive=true; object.x=x; object.y=y;
        object.cell=runtimeCellKey(x,y); object.mission="Guard";
        object.health=health>0 ? health : runtimeMaxHealthForObject(s,object);
        const int id=object.instanceId; s.world.accessObjects().push_back(object); return id;
    };
    const auto fire=[&](RuntimeWindowState& s, const std::string& id, SuperWeaponTarget target,
        std::optional<SuperWeaponTarget> second=std::nullopt) {
        auto context=runtimeSuperWeaponContext(s); context.emit=runtimeSuperWeaponEventSink(s);
        if (!s.superWeapons.findSidebarSnapshot("Commander",id)) { error="missing source "+id; return false; }
        if (!s.superWeapons.forceReady("Commander",id,&context)
            || !s.superWeapons.beginTargetSelection("Commander",id,context,&error)
            || !s.superWeapons.submitTarget("Commander",id,target,context,&error)) return false;
        return !second || s.superWeapons.submitTarget("Commander",id,*second,context,&error);
    };
    const auto step=[&](RuntimeWindowState& s, int ticks=1) {
        for (int i=0;i<ticks;++i) { ++s.tick; advanceRuntimeEffects(s); }
    };
    const auto capture=[&](RuntimeWindowState& s, const std::string& name) {
        refreshRuntimeSpecialMechanismRenderSnapshots(s);
        const RuntimeCellPoint center = name.find("parachute")!=std::string::npos || name=="soviet-spy-plane"
            ? RuntimeCellPoint{82,82} : RuntimeCellPoint{90,90};
        return drawRuntimeStateOffscreenAtCell(s,center,960,660,error,
            root.empty()?"":root+"/"+name+".bmp",false);
    };

    #include "RuntimeSuperWeaponDeathSoundSmoke.inl"
    #include "RuntimeChronosphereSelectionSmoke.inl"
    #include "RuntimeSupportTargetPanSmoke.inl"
    #include "RuntimeSuperWeaponRetailSmoke.inl"
    #include "RuntimeSupportRechargeSmoke.inl"
    #include "RuntimeSupportNoCooldownSmoke.inl"
    #include "RuntimeIronCurtainSmoke.inl"
    #include "RuntimeForceShieldSmoke.inl"
    #include "RuntimeSuperWeaponFriendlyDamageSmoke.inl"

    auto sidebarStorage=fixture(); auto& sidebar=*sidebarStorage;
    std::set<std::string> available;
    for (const auto& sw : sidebar.superWeapons.sidebarSnapshots()) if (sw.owner=="Commander") available.insert(sw.superWeaponId);
    if (available!=expected) {
        std::string found; for (const auto& id : available) found+=id+",";
        return fail("source/sidebar inventory: "+found);
    }
    // Exercise the runtime pacing rate against actual charging sources. The
    // same ten scheduled wall seconds must consume more charge at the six
    // finite speeds. Original speed 6 has rate 0 (unlimited); its explicitly
    // bounded simulation ticks make no uncapped wall-time/throughput claim.
    // Changing either pacing mode must not jump the displayed game time.
    for (const int hz : {30, 60}) {
        int previousConsumed = -1;
        for (int speed = 0; speed <= 6; ++speed) {
            auto storage = fixture("Russians", hz); auto& s = *storage;
            s.config.gameSpeed = speed;
            const auto* initial = s.superWeapons.findSidebarSnapshot("Commander", "NukeSpecial");
            if (!initial || initial->state != SuperWeaponRuntimeState::Charging
                    || runtimeSuperWeaponRemainingSeconds(*initial, hz) != 600)
                return fail("nuclear timer did not start at ten game minutes");
            const int before = initial->remainingTicks;
            const int initialRate = runtimeSimulationPacingTickRate(s.config);
            const bool initiallyUncapped = speed == 6;
            if (initiallyUncapped ? initialRate != 0 : initialRate <= 0)
                return fail("original finite/unlimited pacing rate contract");
            const int scheduledTicks = initiallyUncapped ? hz * 10 : initialRate * 10;
            for (int i = 0; i < scheduledTicks; ++i) { ++s.tick; advanceRuntimeSuperWeapons(s); }
            const auto* after = s.superWeapons.findSidebarSnapshot("Commander", "NukeSpecial");
            if (!after || after->state != SuperWeaponRuntimeState::Charging
                    || before - after->remainingTicks != scheduledTicks
                    || (!initiallyUncapped && scheduledTicks <= previousConsumed))
                return fail("game speed did not pace the actual nuclear charge");
            if (!initiallyUncapped) previousConsumed = scheduledTicks;
            const int beforeChange = after->remainingTicks;
            const int secondsBeforeChange = runtimeSuperWeaponRemainingSeconds(*after, hz);
            s.config.gameSpeed = (speed + 3) % 7;
            if (after->remainingTicks != beforeChange
                    || runtimeSuperWeaponRemainingSeconds(*after, s.config.fixedTickHz) != secondsBeforeChange)
                return fail("changing game speed jumped the nuclear timer");
            const int changedRate = runtimeSimulationPacingTickRate(s.config);
            const bool changedUncapped = s.config.gameSpeed == 6;
            if (changedUncapped ? changedRate != 0 : changedRate <= 0)
                return fail("changed finite/unlimited pacing rate contract");
            const int changedSpeedTicks = changedUncapped ? hz * 2 : changedRate * 2;
            for (int i = 0; i < changedSpeedTicks; ++i) { ++s.tick; advanceRuntimeSuperWeapons(s); }
            after = s.superWeapons.findSidebarSnapshot("Commander", "NukeSpecial");
            if (!after || after->state != SuperWeaponRuntimeState::Charging
                    || after->remainingTicks != beforeChange - changedSpeedTicks)
                return fail("mid-charge game speed change did not take effect");
            const int remaining = after->remainingTicks;
            const int seconds = runtimeSuperWeaponRemainingSeconds(*after, hz);

            // A low-power source stays paused at every speed, with the same
            // remaining game time surviving serialization and restoration.
            // Unlimited pacing still executes actual positive ticks; rate 0
            // must never turn the pause transition into an empty loop.
            const int lowPowerTicks = changedUncapped ? hz : changedRate;
            if (lowPowerTicks <= 0) return fail("low power requires actual simulation ticks");
            for (int i = 0; i < lowPowerTicks; ++i) {
                ++s.tick;
                auto context = runtimeSuperWeaponContext(s);
                context.ownerPowered = [](const auto&) { return false; };
                s.superWeapons.tick(context);
                if (i == 0) {
                    const auto* firstPaused = s.superWeapons.findSidebarSnapshot("Commander", "NukeSpecial");
                    if (!firstPaused || firstPaused->state != SuperWeaponRuntimeState::Suspended
                            || firstPaused->remainingTicks != remaining)
                        return fail("first actual low-power tick did not suspend unchanged charge");
                }
            }
            const auto* paused = s.superWeapons.findSidebarSnapshot("Commander", "NukeSpecial");
            if (!paused || paused->state != SuperWeaponRuntimeState::Suspended
                    || paused->remainingTicks != remaining)
                return fail("low power advanced the superweapon timer");
            std::ostringstream saved; s.superWeapons.save(saved);
            IniFile ini; SuperWeaponSystem restored; restored.reset(s.rules);
            if (!ini.loadFromString(saved.str()) || !restored.load(ini, &error))
                return fail("paused timer reload: " + error);
            const auto* loaded = restored.findSidebarSnapshot("Commander", "NukeSpecial");
            if (!loaded || loaded->state != SuperWeaponRuntimeState::Suspended
                    || loaded->remainingTicks != remaining
                    || runtimeSuperWeaponRemainingSeconds(*loaded, hz) != seconds)
                return fail("reload changed the remaining game time");
            std::cout << "Superweapon timer: hz=" << hz << " speed=" << speed
                << " actualPacingRate=" << initialRate
                << (initiallyUncapped ? " simulationTickBudget=" : " scheduledWallSeconds=10 chargeTicks=") << scheduledTicks
                << " changedSpeed=" << s.config.gameSpeed << " changedPacingRate=" << changedRate
                << (changedUncapped ? " changedSimulationTickBudget=" : " changedScheduledWallSeconds=2 chargeTicks=") << changedSpeedTicks
                << " lowPowerTicks=" << lowPowerTicks
                << " remaining=" << runtimeFormatSuperWeaponTimer(secondsBeforeChange)
                << " speedChange/lowPower/reload passed\n";
        }
    }
    // Optional full-interface captures use the same map, sidebar and tooltip
    // renderers as the live window, at both compact and wide resolutions.
    if (!root.empty()) {
        sidebar.activeBuildTab=RuntimeBuildKind::Defense;
        sidebar.ownerRemapColors["commander"] = runtimeRemapColorFromToken(
            sidebar.config, "Gold", RGB(255, 216, 0));
        auto context=runtimeSuperWeaponContext(sidebar);
        sidebar.superWeapons.forceReady("Commander","NukeSpecial",&context);
        for (const POINT size : {POINT{800,600},POINT{1280,720}}) {
            const RECT client{0,0,size.x,size.y};
            std::vector<std::uint32_t> pixels;
            if (!drawRuntimeStateOffscreenAtCell(sidebar,{80,80},size.x,size.y,
                    error,"",false,true,-1,&pixels,true)) return false;
            RuntimeBackBuffer surface;
            HDC screenDc=GetDC(nullptr);
            const bool surfaceReady=ensureRuntimeBackBuffer(
                surface,screenDc,size.x,size.y);
            if (screenDc) ReleaseDC(nullptr,screenDc);
            if (!surfaceReady) return fail("sidebar preview surface unavailable");
            std::copy(pixels.begin(),pixels.end(),
                static_cast<std::uint32_t*>(surface.pixels));
            drawRuntimeSidebar(surface.memoryDc,client,sidebar);
            const RECT sidebarRect=runtimeSidebarRect(client);
            const auto visible=runtimeVisibleSuperWeaponsOnBuildPage(sidebar,sidebarRect);
            for (std::size_t index=0;index<visible.size();++index) {
                if (visible[index]->superWeaponId!="NukeSpecial") continue;
                const RECT button=runtimeBuildButtonRect(sidebarRect,static_cast<int>(index));
                sidebar.mouseClientPoint={(button.left+button.right)/2,(button.top+button.bottom)/2};
                sidebar.mouseClientPointValid=true;
                drawRuntimeGameplayTooltip(surface.memoryDc,client,sidebar);
                break;
            }
            GdiFlush();
            const bool written=writeRuntimeSmokeBmp(root+"/sidebar-timers-"
                +std::to_string(size.x)+".bmp",surface.pixels,size.x,size.y,error);
            releaseRuntimeBackBuffer(surface);
            if (!written) return false;
        }
        sidebar.mouseClientPointValid=false;
        releaseRuntimeBackBuffer(sidebar.sidebarStaticBuffer);
        releaseRuntimeBackBuffer(sidebar.radarTerrainBuffer);

        // Capture both presentation phases without advancing simulation time.
        // Every original power, including the spy plane on later pages, is ready.
        for (const auto& id : expected)
            sidebar.superWeapons.forceReady("Commander",id,&context);
        sidebar.superWeapons.tick(context);
        for (const POINT size : {POINT{800,600},POINT{1280,720}}) {
            const RECT client{0,0,size.x,size.y};
            const RECT side=runtimeSidebarRect(client);
            std::vector<std::uint32_t> pixels;
            if (!drawRuntimeStateOffscreenAtCell(sidebar,{80,80},size.x,size.y,
                    error,"",false,true,-1,&pixels,true)) return false;
            RuntimeBackBuffer surface;
            HDC screenDc=GetDC(nullptr);
            const bool surfaceReady=ensureRuntimeBackBuffer(surface,screenDc,size.x,size.y);
            if (screenDc) ReleaseDC(nullptr,screenDc);
            if (!surfaceReady) return fail("ready label preview surface unavailable");
            SetBkMode(surface.memoryDc,TRANSPARENT);
            for (int page=0;page<runtimeBuildPageCount(sidebar,side);++page) {
                runtimeSetActiveBuildPage(sidebar,page);
                for (const std::uint64_t phase : {0ULL,500ULL}) {
                    std::copy(pixels.begin(),pixels.end(),static_cast<std::uint32_t*>(surface.pixels));
                    drawRuntimeSidebar(surface.memoryDc,client,sidebar,phase);
                    GdiFlush();
                    if (!writeRuntimeSmokeBmp(root+"/ready-"+std::to_string(size.x)
                            +"-page"+std::to_string(page)+(phase==0?"-on.bmp":"-off.bmp"),
                            surface.pixels,size.x,size.y,error)) {
                        releaseRuntimeBackBuffer(surface); return false;
                    }
                }
            }
            releaseRuntimeBackBuffer(surface);
        }
        releaseRuntimeBackBuffer(sidebar.sidebarStaticBuffer);
        releaseRuntimeBackBuffer(sidebar.radarTerrainBuffer);

        if (std::getenv("CONCORD_COMMAND_SMOKE_SUPPORT_READY_ONLY") != nullptr) {
            std::cout << "Support ready presentation: all 11 powers, 800/1280 widths, both wall-time phases captured\n";
            return true;
        }

        auto timerStorage = fixture(); auto& timers = *timerStorage;
        timers.superWeapons.reset(timers.rules);
        timers.world.accessObjects().clear();
        timers.ownerRemapColors = sidebar.ownerRemapColors;
        timers.ownerRemapColors["enemy-red"] = runtimeRemapColorFromToken(
            timers.config, "DarkRed", RGB(255, 0, 0));
        timers.ownerRemapColors["enemy-blue"] = runtimeRemapColorFromToken(
            timers.config, "DarkBlue", RGB(0, 168, 255));
        auto timerContext = runtimeSuperWeaponContext(timers);
        if (!timers.superWeapons.grant("Commander", "NukeSpecial", -1, &timerContext)
                || !timers.superWeapons.forceReady("Enemy-Red", "LightningStormSpecial", &timerContext)
                || !timers.superWeapons.grant("Enemy-Blue", "PsychicDominatorSpecial", -1, &timerContext))
            return fail("multi-owner timer preview setup");
        timerContext.ownerPowered = [](const auto& owner) { return owner != "Enemy-Blue"; };
        timers.superWeapons.tick(timerContext);
        for (const POINT size : {POINT{800,600}, POINT{1280,720}}) {
            if (!drawRuntimeStateOffscreenAtCell(timers,{80,80},size.x,size.y,error,
                    root+"/timer-owners-"+std::to_string(size.x)+".bmp",false,true,-1,nullptr,true))
                return fail("multi-owner timer capture: "+error);
        }
    }
    auto bounds=runtimeSuperWeaponContext(sidebar);
    if (90<=sidebar.report.map.width || !bounds.cellLegal(90,90,SuperWeaponTargetMode::Cell)
        || bounds.cellLegal(20,20,SuperWeaponTargetMode::Cell)) return fail("isometric map target membership");
    sidebar.activeBuildTab=RuntimeBuildKind::Defense;
    for (int i=0;i<5;++i) {
        RuntimeBuildCatalogItem item; item.kind=RuntimeBuildKind::Defense;
        item.typeId="DEFENSE_"+std::to_string(i); item.label=item.typeId; item.techTreeAllowed=true;
        item.techLevel=0; item.buildTicks=1; sidebar.economy.catalog.push_back(item);
    }
    const RECT sideRect=runtimeSidebarRect({0,0,1024,480});
    std::set<std::string> clicked;
    for (int page=0;page<runtimeBuildPageCount(sidebar,sideRect);++page) {
        runtimeSetActiveBuildPage(sidebar,page);
        const auto builds=runtimeVisibleBuildCatalogIndices(sidebar,sideRect);
        const auto powers=runtimeVisibleSuperWeaponsOnBuildPage(sidebar,sideRect);
        for (std::size_t i=0;i<powers.size();++i) {
            const auto rect=runtimeBuildButtonRect(sideRect,static_cast<int>(builds.size()+i));
            const auto* hit=runtimeSuperWeaponAtPoint(sidebar,sideRect,{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2});
            if (!hit || hit->superWeaponId!=powers[i]->superWeaponId) return fail("defense page hit mismatch");
            clicked.insert(hit->superWeaponId);
        }
    }
    if (clicked!=expected) return fail("defense pagination omitted a support power");
    std::cout<<"Support sidebar: all 11 original powers, source buildings, mixed defense pagination and diamond bounds passed\n";

    {
        auto storage=fixture(); auto& s=*storage;
        addUnit(s,"GASPYSAT",WorldObjectKind::Structure,76,94,"Commander");
        s.vision.reset(); s.vision.update(s.world,&s.rules,{{"Commander",{"Commander"}}},s.tick);
        if (!s.vision.isCellExploredBy("Commander",113,109)) {
            const auto* type=s.rules.findBuildingType("GASPYSAT"); const auto& object=s.world.getObjects().back();
            const auto* stats=s.vision.statsFor("Commander");
            return fail("Allied SpySat reveal: rule="+std::to_string(type && type->spySat)
                +" alive="+std::to_string(object.alive)+" powered="+std::to_string(object.poweredOperational)
                +" owner="+object.owner+" sources="+std::to_string(stats?stats->spySatSources:-1)
                +" explored="+std::to_string(stats?stats->exploredCells:-1));
        }
        std::cout<<"Support recon: Allied native SpySat whole-map reveal passed\n";
    }

    const auto* paraRule=live.rules.findSuperWeaponType("AmericanParaDropSpecial");
    const auto* spyRule=live.rules.findSuperWeaponType("SpyPlaneSpecial");
    if (!paraRule || paraRule->planeType!="PDPLANE" || paraRule->parachuteAnim!="PARACH"
        || !spyRule || spyRule->planeType!="SPYP") return fail("original support aircraft/parachute links");
    struct DropCase { const char* country; const char* power; const char* infantry; int count; int hz; };
    for (const auto test : {DropCase{"Americans","AmericanParaDropSpecial","E1",8,30},
            {"Americans","AmericanParaDropSpecial","E1",8,60}, {"British","ParaDropSpecial","E1",6,30},
            {"Russians","ParaDropSpecial","E2",9,30}, {"YuriCountry","ParaDropSpecial","INIT",6,30}}) {
        auto storage=fixture(test.country,test.hz); auto& s=*storage;
        if (!fire(s,test.power,{{90,90},-1})) return fail(std::string(test.power)+" target: "+error);
        bool aircraft=false, falling=false, rendered=false, roundTrip=false; int landedId=-1;
        for (int i=0;i<test.hz*45;++i) {
            step(s);
            for (const auto& object : s.world.getObjects()) {
                aircraft=aircraft || (object.type=="PDPLANE" && object.alive && object.airborne);
                if (object.kind!=WorldObjectKind::Infantry || object.owner!="Commander") continue;
                if (object.airborne) {
                    falling=true;
                    if (runtimeObjectCanMove(s,object)) return fail("paratrooper moved before landing");
                    if (!rendered && i>test.hz*5 && test.hz==30) {
                        const auto stats=capture(s,std::string(test.country)+"-parachutes");
                        if (!stats || stats->parachuteSprites==0 || stats->parachuteMissing!=0
                            || stats->missingObjectShpSprites!=0) return fail("original parachute/infantry rendering: "+error);
                        rendered=true;
                    }
                    if (!roundTrip && i>test.hz*2) {
                        std::ostringstream saved; s.superWeapons.save(saved); IniFile ini;
                        SuperWeaponSystem restored; restored.reset(s.rules);
                        if (!ini.loadFromString(saved.str()) || !restored.load(ini,&error)
                            || restored.stateHash()!=s.superWeapons.stateHash()) return fail("support flight reload: "+error);
                        s.superWeapons.swap(restored); roundTrip=true;
                        if (test.hz==30 && std::string(test.power)=="AmericanParaDropSpecial" && !root.empty()) {
                            const std::string path=root+"/american-paradrop-midair.ini";
                            if (!saveRuntimeState(s,path,error)) return fail("full midair save: "+error);
                            auto reloaded=fixture(test.country,test.hz);
                            if (!loadRuntimeState(*reloaded,path,error)) return fail("full midair load: "+error);
                            if (reloaded->superWeapons.stateHash()!=s.superWeapons.stateHash()) return fail("full save changed support flight state");
                            for (const auto& old : s.world.getObjects()) if (old.alive && old.airborne) {
                                const auto* restoredObject=findObjectById(*reloaded,old.instanceId);
                                if (!restoredObject || restoredObject->type!=old.type || restoredObject->owner!=old.owner
                                    || !restoredObject->airborne || restoredObject->mission!=old.mission
                                    || std::abs(restoredObject->renderLevel-old.renderLevel)>0.00001
                                    || std::abs(restoredObject->movementAnimationDistance-old.movementAnimationDistance)>0.00001)
                                    return fail("full save changed an aircraft/paratrooper pose");
                            }
                            step(*reloaded,2);
                        }
                    }
                } else landedId=object.instanceId;
            }
        }
        int troops=0, activePlanes=0;
        const auto* nativeInfantry=s.rules.findUnitType("InfantryType",test.infantry);
        for (const auto& object:s.world.getObjects()) {
            activePlanes += object.alive && object.type=="PDPLANE";
            if (!object.alive || object.kind!=WorldObjectKind::Infantry || object.owner!="Commander") continue;
            ++troops;
            if (object.type!=test.infantry || object.airborne || object.level!=0
                || !nativeInfantry || object.health!=nativeInfantry->strength || !runtimeObjectCanMove(s,object))
                return fail(std::string(test.country)+" infantry did not land with its native capabilities");
        }
        if (!aircraft || !falling || !roundTrip || activePlanes || troops!=test.count || landedId<0)
            return fail(std::string(test.country)+" flight/drop/landing counts "+std::to_string(troops));
        if (test.hz==30 && !rendered) return fail("parachute resource not displayed");
        s.selectedObjectIds={landedId};
        issueRuntimeMoveCommand(s,{98,99});
        if (s.lastCommand.acceptedCount!=1 || s.behaviors.find(landedId)==s.behaviors.end())
            return fail(std::string(test.country)+" landed infantry rejected a player move order");
        std::cout<<"Support paradrop: "<<test.country<<" "<<test.power<<" "<<troops<<" "<<test.infantry
            <<" at "<<test.hz<<" Hz, aircraft/descent/landing/reload passed\n";
    }

    const char* continuityOutput=std::getenv("CONCORD_SUPPORT_AIRCRAFT_CONTINUITY_OUTPUT");
    const std::string continuityRoot=continuityOutput?continuityOutput:root;
    std::ofstream spyTrace;if(!continuityRoot.empty()) {
        std::filesystem::create_directories(continuityRoot);spyTrace.open(continuityRoot+"/spy-motion.csv");
        if(!spyTrace)return fail("spy motion CSV unavailable");
        spyTrace<<"scene,event,tick,id,token,phase,x,y,z,logical_x,logical_y,delta_xy,bound,phase_start,phase_end,has_pose,active,airborne,hp,mission\n";
    }
    int spyMotionCases=0,spyMotionSamples=0,spyNavChecks=0,spySavePairs=0;
    for (const auto& test : std::array<std::pair<const char*,int>,4>{{
            {"SpyPlaneSpecial",15},{"SpyPlaneSpecial",30},{"SpyPlaneSpecial",60},{"PsychicRevealSpecial",30}}}) {
        const std::string power=test.first;const int hz=test.second;
        auto storage=fixture(power=="SpyPlaneSpecial"?"Russians":"YuriCountry",hz); auto& s=*storage;
        s.vision.reset(); s.vision.update(s.world,nullptr,{{"Commander",{"Commander"}}},s.tick); s.visionEnabled=true;
        if (s.vision.isCellExploredBy("Commander",90,90)) return fail("recon fixture already explored");
        if (!fire(s,power,{{90,90},-1})) return fail(power+" target: "+error);
        const std::string label="spy-"+std::to_string(hz);
        bool seenAircraft=false, moved=false; double lastX=0.0;
        std::unique_ptr<RuntimeWindowState> ordinary,canonical;
        auto previous=runtimeSupportAircraftSmokePoses(s);
        for (int i=0;i<450;++i) {
            step(s);const auto current=runtimeSupportAircraftSmokePoses(s);
            if(power=="SpyPlaneSpecial") {
                const auto* type=s.rules.findUnitType("AircraftType","SPYP");
                if(!type||type->speed!=15)return fail("original SPYP support Speed15 unavailable");
                const double bound=15.0/hz;
                runtimeSupportAircraftSmokeTrace(spyTrace?&spyTrace:nullptr,label,"support-flight",s.tick,current,previous,bound);
                for(const auto& [id,p]:current) {
                    const auto old=previous.find(id);++spyMotionSamples;
                    if(old!=previous.end()&&(std::abs(std::abs(p.x-old->second.x)-bound)>1e-9
                        ||std::abs(p.y-old->second.y)>1e-9||std::abs(p.z-old->second.z)>1e-9))
                        return fail(label+" actual support rate/straight height changed tick="+std::to_string(s.tick));
                }
                if(!runtimeSupportAircraftSmokeAnchors(s,error))return fail(label+" anchor: "+error);
                if(i==3) {
                    initializeRuntimeNavigation(s);const auto rebuilt=runtimeSupportAircraftSmokePoses(s);++spyNavChecks;
                    runtimeSupportAircraftSmokeTrace(spyTrace?&spyTrace:nullptr,label,"navigation-rebuild",s.tick,rebuilt,current,0.0);
                    if(current.empty()||!runtimeSupportAircraftSmokeEqual(current,rebuilt,error))return fail(label+" Nav jump: "+error);
                }
                if(ordinary) {
                    step(*ordinary);step(*canonical);
                    if(!runtimeSupportAircraftSmokeEqual(current,runtimeSupportAircraftSmokePoses(*ordinary),error)
                        ||!runtimeSupportAircraftSmokeEqual(current,runtimeSupportAircraftSmokePoses(*canonical),error))
                        return fail(label+" actual ordinary/canonical continuation: "+error);
                    if(!current.empty())++spySavePairs;
                }
                if(i==4) {
                    if(!runtimeSupportAircraftSmokeCheckpoint(s,continuityRoot,label,ordinary,canonical,error))
                        return fail(label+" actual checkpoint: "+error);
                    if(!runtimeSupportAircraftSmokeEqual(current,runtimeSupportAircraftSmokePoses(*ordinary),error)
                        ||!runtimeSupportAircraftSmokeEqual(current,runtimeSupportAircraftSmokePoses(*canonical),error))
                        return fail(label+" checkpoint pose: "+error);
                }
            }
            for (const auto& object:s.world.getObjects()) if (object.alive && object.type=="SPYP") {
                moved=moved || (seenAircraft && lastX!=object.renderX); lastX=object.renderX; seenAircraft=true;
            }
            if (i==24 && power=="SpyPlaneSpecial" && hz==30) {
                const auto stats=capture(s,"soviet-spy-plane");
                if (!stats || stats->visibleVoxelBodies<=0 || stats->missingVoxelProxies!=0)
                    return fail("original SPYP voxel render");
            }
            previous=current;
        }
        if (!s.vision.isCellExploredBy("Commander",90,90)
            || (power=="SpyPlaneSpecial" && (!seenAircraft || !moved))) return fail(power+" failed to reveal");
        if (std::any_of(s.world.getObjects().begin(),s.world.getObjects().end(),[](const auto& o){return o.alive && o.type=="SPYP";}))
            return fail("spy aircraft did not leave the map");
        if(power=="SpyPlaneSpecial")++spyMotionCases;
        std::cout<<"Support recon: "<<power<<" original aircraft/camera or psychic reveal, shroud and exit passed hz="<<hz<<"\n";
    }
    if(spyMotionCases!=3||spyNavChecks!=3||spyMotionSamples<=0||spySavePairs<36)return fail("spy continuity catalogue incomplete");
    std::cout<<"SPYP continuity: passed cases="<<spyMotionCases<<" samples="<<spyMotionSamples
        <<" nav="<<spyNavChecks<<" paired="<<spySavePairs<<'\n';

    for (const std::string power : {"NukeSpecial","LightningStormSpecial","PsychicDominatorSpecial",
            "IronCurtainSpecial","ForceShieldSpecial","ChronoSphereSpecial","GeneticConverterSpecial"}) {
        auto storage=fixture(); auto& s=*storage; int target=-1, companion=-1;
        if (power=="IronCurtainSpecial" || power=="ChronoSphereSpecial") {
            target=addUnit(s,"MTNK",WorldObjectKind::Unit,90,90,"Commander");
            companion=addUnit(s,"E1",WorldObjectKind::Infantry,91,90,"Commander");
        } else if (power=="ForceShieldSpecial") target=addUnit(s,"GAPOWR",WorldObjectKind::Structure,90,90,"Commander");
        else if (power=="GeneticConverterSpecial") target=addUnit(s,"E1",WorldObjectKind::Infantry,90,90);
        else if (power=="PsychicDominatorSpecial") {
            target=addUnit(s,"HTNK",WorldObjectKind::Unit,90,90);
            companion=addUnit(s,"NAHAND",WorldObjectKind::Structure,94,90,"Enemy",3000);
        } else target=addUnit(s,"NAHAND",WorldObjectKind::Structure,90,90,"Enemy",6000);
        initializeRuntimeNavigation(s);
        const int before=findObjectById(s,target)->health;
        if (!fire(s,power,{{90,90},target},power=="ChronoSphereSpecial"
                ? std::optional<SuperWeaponTarget>({{104,100},-1}) : std::nullopt)) return fail(power+" target: "+error);
        step(s,2);
        if (power=="IronCurtainSpecial") {
            if (!s.superWeapons.isObjectProtected(target,s.tick) || findObjectById(s,companion)) return fail("iron curtain organic/mechanical split");
        } else if (power=="ForceShieldSpecial") {
            if (!s.superWeapons.isObjectProtected(target,s.tick) || !s.superWeapons.isOwnerBlackout("Commander",s.tick))
                return fail("force shield protection/blackout");
        } else if (power=="ChronoSphereSpecial") {
            const auto* unit=findObjectById(s,target);
            if (!unit || unit->x!=104 || unit->y!=100 || unit->cell!=runtimeCellKey(104,100)
                || findObjectById(s,companion)) return fail("chronosphere transfer, cell stride or organic removal");
        } else if (power=="PsychicDominatorSpecial") {
            // The original giant-head animation fires at its authored
            // percentage; it does not capture on the second launch tick.
            for (int i=0;i<s.config.fixedTickHz*30;++i) {
                const auto* victim=findObjectById(s,target);
                if (!victim || victim->owner=="Commander") break;
                step(s);
            }
            const auto* tank=findObjectById(s,target); const auto* building=findObjectById(s,companion);
            if (!tank || tank->owner!="Commander" || tank->health!=before
                || !building || building->health>=3000 || building->owner!="Enemy")
                return fail("dominator independent building damage and tank control");
        } else if (power=="GeneticConverterSpecial") {
            const auto* mutationRule = s.rules.findSuperWeaponType(power);
            const auto* mutationType = mutationRule ? s.rules.findUnitType("infantry", mutationRule->mutationType) : nullptr;
            if (!mutationType || mutationType->strength <= 0) return fail("registered mutation full-health definition");
            if (findObjectById(s,target) || std::count_if(s.world.getObjects().begin(),s.world.getObjects().end(),
                    [&](const auto& o){return o.alive && o.type==mutationType->id && o.owner=="Commander" && o.health==mutationType->strength;})!=1)
                return fail("genetic converter transformation");
        } else {
            for (int i=0;i<1000;++i) {
                const auto* victim=findObjectById(s,target);
                if (!victim || victim->health<before) break;
                step(s);
            }
            const auto* victim=findObjectById(s,target);
            if (victim && victim->health>=before) return fail(power+" did no damage");
        }
        if (power=="PsychicDominatorSpecial") step(s,25);
        else step(s,4);
        const auto stats=capture(s,power);
        if (!stats || s.impactEffects.empty() || stats->impactAnimDrawn<=0 || stats->impactAnimMissing>0)
            return fail(power+" missing original effect animation: "+error
                +" drawn="+std::to_string(stats?stats->impactAnimDrawn:-1)
                +" missing="+std::to_string(stats?stats->impactAnimMissing:-1));
        if (power=="IronCurtainSpecial" || power=="ForceShieldSpecial") {
            step(s,2200);
            if (s.superWeapons.isObjectProtected(target,s.tick) || s.superWeapons.isOwnerBlackout("Commander",s.tick))
                return fail(power+" protection or blackout never expired");
        }
        std::cout<<"Support superweapon: "<<power<<" actual units, native effects and lifecycle passed\n";
    }
    return true;
}
