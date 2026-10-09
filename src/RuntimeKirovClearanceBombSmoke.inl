// Issue 22/18: real UI flight/attack with contextual ordinary-14 and canonical
// continuation. Roof lift is presentation; the original flight stays physical.
bool runRuntimeKirovClearanceBombSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* out = std::getenv("CONCORD_KIROV_CLEARANCE_BOMB_OUTPUT");
    const std::filesystem::path root = out && *out ? out : "build/issue22_18_kirov_clearance_bomb";
    std::filesystem::create_directories(root);
    const char* chosen = std::getenv("CONCORD_KIROV_CLEARANCE_BOMB_CASE");
    const std::string filter = chosen ? chosen : "";
    std::string label, stage;
    const auto fail = [&](const std::string& detail) {
        error = "Kirov clearance/bomb " + label + " [" + stage + "]: " + detail;
        return false;
    };
    std::ofstream cases(root / "cases.csv"), movement(root / "movement.csv"),
        shots(root / "shots.csv"), damage(root / "damage.csv"), checkpoints(root / "checkpoints.csv"),
        clocks(root / "clocks.csv");
    if (!cases || !movement || !shots || !damage || !checkpoints || !clocks) return fail("output unavailable");
    for (auto* stream : {&cases, &movement, &shots, &damage, &checkpoints, &clocks})
        *stream << std::setprecision(17);
    cases << "case,hz,route,axis,rank,source,target,first_tick,last_tick,rising_ticks,falling_ticks,max_lift,shots,positive_impacts,ordinary,canonical,paused_checks,stop_decay,resumed,result\n";
    movement << "case,stage,replica,tick,source,logical_x,logical_y,physical_x,physical_y,physical_z,flight_height,clearance_q16,clearance_level,clearance_tick,body_x,body_y,placed_x,placed_y,lift_pixels,health,airborne,moving,stopped,target_health\n";
    shots << "case,replica,tick,actor,spawn_tick,source,rank,logical_distance,physical_center_distance,source_x,source_y,source_z,target_x,target_y,target_z,current_x,current_y,current_z,elapsed,total,remaining,weapon,warhead,cached_damage,veteran_multiplier,vertical,acceleration,unbound_target\n";
    damage << "case,replica,tick,actor,impact_x,impact_y,footprint_distance,before,after,expected_delta,actual_delta,weapon,warhead,authored_damage,armor_multiplier,outgoing_multiplier,range_multiplier\n";
    checkpoints << "case,stage,format,tick,hz,source,target,clearance_q16,clearance_tick,flight_schema,particle_tick,particle_hz,file,result\n";
    clocks << "case,replica,tick,actor,previous_shot_tick,elapsed,minimum_rof_ticks,authored_rof,rof_multiplier\n";

    IniFile raw, rawArt;
    if (!raw.loadFromFile(live.report.rulesPath) || !rawArt.loadFromFile(live.report.artPath))
        return fail("original rules/art unavailable");
    const auto number = [&](const std::string& section, const char* key) {
        return std::stod(raw.getValue(section, key, "-999"));
    };
    const auto* nativeZep = live.rules.findUnitType("vehicle", "ZEP");
    const auto* nativeProjectile = live.rules.findProjectileType("BlimpBombP");
    if (!nativeZep || !nativeProjectile || nativeZep->strength != 2000
        || nativeZep->jumpjetSpeed != 5 || nativeZep->jumpjetClimb != 6 || nativeZep->jumpjetCrash != 12
        || number("ZEP", "Strength") != 2000 || number("ZEP", "JumpjetSpeed") != 5
        || number("ZEP", "JumpjetClimb") != 6 || number("ZEP", "JumpjetCrash") != 12
        || number("ZEP", "JumpjetHeight") != 750 || !parseRuntimeBool(raw.getValue("ZEP", "JumpjetNoWobbles", "no"))
        || rawArt.getValue("ZEP", "PrimaryFireFLH", "") != "-50,0,-140"
        || raw.getValue("BlimpBombP", "Image", "") != "ZBOMB"
        || !parseRuntimeBool(raw.getValue("BlimpBombP", "Vertical", "no"))
        || number("BlimpBombP", "Arm") != 10 || number("BlimpBombP", "Acceleration") != 1
        || number("CACHIG05", "Strength") != 1000 || raw.getValue("CACHIG05", "Armor", "") != "concrete"
        || number("GAPOWR", "Strength") != 750 || raw.getValue("GAPOWR", "Armor", "") != "wood"
        || rawArt.getValue("CACHIG05", "Foundation", "") != "4x4"
        || rawArt.getValue("CACHIG05", "Height", "") != "22"
        || rawArt.getValue("GAPOWR", "Foundation", "") != "2x2"
        || rawArt.getValue("GAPOWR", "Height", "") != "4")
        return fail("original Kirov/projectile/building declarations differ");
    for (const auto* weapon : {"BlimpBomb", "BlimpBombE"}) {
        const auto* typed = live.rules.findWeaponType(weapon);
        const std::string warhead = raw.getValue(weapon, "Warhead", "");
        const auto verses = splitRuntimeTokenList(raw.getValue(warhead, "Verses", ""));
        if (!typed || number(weapon, "Damage") != 250 || number(weapon, "ROF") != 50
            || number(weapon, "Range") != 1.5 || typed->range != 1.5
            || raw.getValue(weapon, "Projectile", "") != "BlimpBombP"
            || !parseRuntimeBool(raw.getValue(weapon, "CellRangefinding", "no"))
            || number(warhead, "CellSpread") != 2 || number(warhead, "PercentAtMax") != .5
            || verses.size() != 11 || verses[8] != "50%") return fail("original bomb range/damage/ROF/Verses differ");
    }
    const double climb = number("ZEP", "JumpjetClimb"), crash = number("ZEP", "JumpjetCrash");
    const double veteranCombat = number("General", "VeteranCombat"), veteranRof = number("General", "VeteranROF");
    if (veteranCombat != 1.1 || veteranRof != .6) return fail("original veteran combat/ROF differ");
    // Independently floor the exact binary64 raw multiplier times 50.
    // Retail's toward-zero product is 29 frames for binary64(0.6), although
    // a nearest-rounded double multiplication prints 30. No cadence helper
    // participates in this oracle; the original 0..2 jitter can only add time.
    std::uint64_t rofBits=0; std::memcpy(&rofBits,&veteranRof,sizeof(rofBits));
    const std::uint64_t rofSignificand=(rofBits & ((std::uint64_t{1}<<52)-1)) | (std::uint64_t{1}<<52);
    const int rofShift=1075-int((rofBits>>52)&2047);
    if (rofShift!=53) return fail("original binary64 ROF representation differs");
    const int eliteMinimumFrames=int((rofSignificand*50)>>rofShift);
    const auto rulesHash = live.rules.getDetailRulesHash();
    int passed = 0, ordinaryCount = 0, canonicalCount = 0;
    const auto physical = [](const WorldObjectInstance& o) {
        return std::array<double,3>{{o.hasRenderPosition ? o.renderX : double(o.x),
            o.hasRenderPosition ? o.renderY : double(o.y), o.hasRenderPosition ? o.renderLevel : double(o.level)}};
    };
    const auto nearValue = [](double a, double b) { return std::abs(a-b) <= 1e-8; };
    const auto describe = [&](const RuntimeWindowState& s, int id) {
        std::ostringstream os; os << std::setprecision(17);
        const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
        if (!o) return std::string("missing#") + std::to_string(id);
        const auto p = physical(*o);
        os << o->type << '#' << id << " hp=" << o->health << " alive=" << o->alive
           << " logical=" << o->x << ',' << o->y << ',' << o->level << " body=" << p[0] << ',' << p[1] << ',' << p[2]
           << " lift=" << o->aircraftBuildingClearanceLevel << " mission=" << o->mission
           << " render=" << o->hasRenderPosition << '/' << o->renderMoveActive;
        const auto f = s.jumpjetFlights.find(id);
        if (f != s.jumpjetFlights.end()) os << " flight=" << f->second.height << '/' << f->second.progress
            << '/' << f->second.length << " clear=" << f->second.clearanceLift << '@' << f->second.clearanceTick;
        const auto b = s.behaviors.find(id);
        if (b != s.behaviors.end()) os << " behavior=" << int(b->second.kind) << '/' << int(b->second.state)
            << " pending=" << b->second.pathRequestPending << " path=" << b->second.path.size();
        return os.str();
    };
    const auto contextFor = [&](const RuntimeWindowState& s) {
        RuntimeMapRenderContext c;
        c.theater = &s.theater; c.art = &s.art; c.rules = &s.rules;
        c.assetRootPath = runtimeAssetRootPath(s.config);
        c.tileRootPath = runtimeTileRootPath(s.config, s.report.map);
        c.palettePath = runtimePalettePath(s.config, s.report.map);
        c.structureRootPath = runtimeStructureRootPath(s.config, s.report.map);
        c.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
        c.animationTick = s.tick; c.fixedTickHz = s.config.fixedTickHz;
        c.specialMechanismSnapshots = &s.specialMechanismRenderSnapshots;
        return c;
    };
    const auto step = [&](RuntimeWindowState& s) {
        ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto fixture = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false);
        s.tick = 200; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.shortGame = false;
        s.config.superweapons = s.config.crates = false; s.config.gameMode = "Battle"; s.campaignMode = false;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.mapFacts = {}; s.mapFactsLoaded = false;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = s.report.map.height = -1;
        s.behaviors.clear(); s.projectiles.clear(); s.recentExpiredProjectiles.clear(); s.impactEffects.clear();
        s.jumpjetFlights.clear(); s.aircraftFlights.clear(); s.deathWeapons.reset();
        s.pendingObjectExits.clear(); s.pendingObjectExitIds.clear(); s.selectedObjectIds.clear(); s.playerCommands = {};
        s.humanHouseAuthorities.clear(); s.specialMechanismsInitialized = s.superWeaponsInitialized = s.subcellsInitialized = false;
        s.config.playerSlots.clear();
        for (int i=0; i<2; ++i) {
            RuntimeConfig::PlayerSlot slot; slot.enabled = slot.human = true; slot.local = i == 0;
            slot.country = i == 0 ? "Russians" : "Americans"; slot.team = std::to_string(i+1);
            s.config.playerSlots.push_back(std::move(slot));
        }
        initializeRuntimeSkirmishPlayerIdentities(s, "Russians"); s.economy.owner = runtimeSkirmishOwnerForSlot(0);
        s.economy.skirmishStartApplied = true; s.triggers.loaded = s.triggers.bootstrapComplete = true;
        s.skirmishModeAlliesAllowed = s.skirmishModeRulesLoaded = true;
        for (int y=30; y<=96; ++y) for (int x=30; x<=96; ++x) {
            TerrainCell c; c.x=x; c.y=y; c.cellIndex512=y*512+x; c.tileIndex=c.subTileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        s.particles.reset(hz); s.particlesInitialized = s.particles.alignEmptyStateTick(s.tick);
        ensureRuntimeSuperWeaponsInitialized(s); ensureRuntimeLightingInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        s.camera.tileWidth=60; s.camera.tileHeight=30;
        return box;
    };
    struct Replica { std::string name; std::unique_ptr<RuntimeWindowState> state; };
    for (int hz : {30,60}) for (int scene=0; scene<4; ++scene) {
        const bool attack = scene >= 2; const int rank = scene == 3 ? 2 : 0;
        const char axis = scene == 1 ? 'Y' : 'X';
        auto currentAbilities=splitRuntimeTokenList(raw.getValue("ZEP","VeteranAbilities",""));
        if (rank>=2) {
            const auto eliteAbilities=splitRuntimeTokenList(raw.getValue("ZEP","EliteAbilities",""));
            currentAbilities.insert(currentAbilities.end(),eliteAbilities.begin(),eliteAbilities.end());
        }
        const bool rawFaster=rank>0 && std::any_of(currentAbilities.begin(),currentAbilities.end(),
            [](const auto& ability) {return toUpperAsciiLocal(ability)=="FASTER";});
        const double originalSpeedLimit=number("ZEP","JumpjetSpeed")
            *(rawFaster ? number("General","VeteranSpeed") : 1.0);
        label = attack ? "attack-rank" + std::to_string(rank) + "-" + std::to_string(hz)
            : "flyover-" + std::string(1,axis) + "-" + std::to_string(hz);
        if (!filter.empty() && label != filter) continue;
        stage = "stock fixture";
        std::vector<Replica> replicas; replicas.push_back({"original", fixture(hz)});
        auto& initial = *replicas.front().state;
        const std::string friendly = initial.economy.owner, enemy = runtimeSkirmishOwnerForSlot(1);
        const auto point = [&](int along, int across=0) { return axis=='X'
            ? RuntimeCellPoint{along,64+across} : RuntimeCellPoint{64+across,along}; };
        const auto addBuilding = [&](const std::string& type, RuntimeCellPoint at, const std::string& owner) {
            auto* o=addRuntimeObjectInstanceForOwner(initial,owner,RuntimeBuildKind::Building,type,at);
            if (!o) return -1; o->mission="Sleep"; o->buildAnimTicksRemaining=o->buildAnimTicksTotal=0;
            clearRuntimeRenderPosition(*o); return o->instanceId;
        };
        const int low = attack ? -1 : addBuilding("GAPOWR",point(52),friendly);
        const int target = addBuilding("CACHIG05",point(70),attack ? enemy : friendly);
        // Keep both original human houses alive throughout warmup and after
        // the attack target dies. A distant passive original power plant
        // prevents the real outcome stage from cancelling the flight order.
        const int distantEnemy=addBuilding("GAPOWR",{90,90},enemy);
        auto* born=addRuntimeObjectInstanceForOwner(initial,friendly,RuntimeBuildKind::Unit,"ZEP",point(40));
        if (!born || target<0 || distantEnemy<0 || (!attack && low<0)) return fail("registered native actors unavailable");
        const int source=born->instanceId;
        born->airborne=true; born->level=born->flightLevel=0; born->veterancy=rank;
        born->mission="Guard"; born->facing=born->turretFacing=runtimeFacingFromDelta(axis=='X',axis=='Y');
        born->hasRenderPosition=true; born->renderX=born->x; born->renderY=born->y; born->renderLevel=0;
        // This is a cold, original full-HP aircraft. Its cruise and all later
        // motion are advanced by the original complete simulation.
        if (born->health != 2000 || findObjectById(initial,target)->health != 1000
            || (!attack && findObjectById(initial,low)->health != 750)) return fail("original full HP differs");
        const double cruise=number("ZEP","JumpjetHeight")/256.0;
        for (int n=0; n<hz*20; ++n) {
            step(initial);
            const auto f=initial.jumpjetFlights.find(source);
            if (f!=initial.jumpjetFlights.end() && nearValue(double(f->second.height)/runtimeJumpjetCellQ16,cruise)) break;
        }
        const auto* ready=findObjectById(initial,source);
        if (!ready || !initial.jumpjetFlights.contains(source)
            || !nearValue(physical(*ready)[2],cruise) || ready->aircraftBuildingClearanceLevel != 0
            || !findObjectById(initial,distantEnemy) || findObjectById(initial,distantEnemy)->health!=750
            || initial.skirmishMatchFinished)
            return fail("natural unobstructed cruise not reached: "+describe(initial,source));
        const int firstTick=initial.tick;
        initial.selectedObjectIds={source};
        const bool dispatched = attack
            ? dispatchRuntimeTargetOrderFromUi(initial,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*findObjectById(initial,target)),findObjectById(initial,target))
            : dispatchRuntimeTargetOrderFromUi(initial,RuntimeMultiplayerGameplayTargetOrderKind::Move,point(88));
        if (!dispatched || initial.lastCommand.acceptedCount != 1)
            return fail("real UI rejected: accepted="+std::to_string(initial.lastCommand.acceptedCount)
                +" rejected="+std::to_string(initial.lastCommand.rejectedCount)+" "+initial.interactionStatus);

        int sceneOrdinary=0, sceneCanonical=0, up=0, down=0, paused=0;
        std::int64_t lastLift=initial.jumpjetFlights.at(source).clearanceLift;
        double peak=0;
        bool risingSaved=false, fallingSaved=false, attackSaved=false, actualRoofOverlap=false;
        bool stopDecay=false, resumed=false;
        std::map<int,RuntimeProjectileEffect> previousBombs;
        std::map<std::string,std::set<int>> observedActors;
        std::map<std::string,int> shotCount, impactCount, previousShotTick;
        std::map<std::string,std::map<int,int>> beforeImpact;
        std::map<std::string,std::string> observationError;
        struct Scope {
            RuntimeWindowState& s; std::function<void(const RuntimeProjectileEffect&,bool)> prior;
            explicit Scope(RuntimeWindowState& state) : s(state), prior(std::move(state.runtimeProjectileImpactObserver)) {}
            ~Scope() { s.runtimeProjectileImpactObserver=std::move(prior); }
        };
        std::vector<std::unique_ptr<Scope>> scopes;
        const auto attachObserver = [&](Replica& replica) {
            const std::string name=replica.name; auto& s=*replica.state;
            scopes.push_back(std::make_unique<Scope>(s));
            s.runtimeProjectileImpactObserver=[&,name,state=&s](const RuntimeProjectileEffect& p,bool after) {
                if (p.attackerObjectId!=source || (p.weaponId!="BlimpBomb" && p.weaponId!="BlimpBombE")) return;
                const auto* t=findRuntimeObjectByIdIncludingInactive(*state,target);
                if (!after) { beforeImpact[name][p.actorId]=t ? t->health : 0; return; }
                const int before=beforeImpact[name][p.actorId], afterHP=t ? t->health : 0;
                // Native radial targeting uses nearest original foundation
                // grid cell. This oracle reads raw 4x4, Damage/Verses/falloff.
                const int tx=axis=='X' ? 70 : 64, ty=axis=='X' ? 64 : 70;
                const double dx=p.toCell.x-std::clamp(p.toCell.x,tx,tx+3);
                const double dy=p.toCell.y-std::clamp(p.toCell.y,ty,ty+3);
                const double d=std::hypot(dx,dy), outgoing=rank==2 ? veteranCombat : 1.0;
                const double falloff=1.0-.5*std::clamp(d,0.0,2.0)/2.0;
                const int expected=before<=0 || d>2 ? 0 : std::min(before,int(std::lround(250*.5*outgoing*falloff)));
                const int actual=before-afterHP;
                damage << label << ',' << name << ',' << state->tick << ',' << p.actorId << ','
                    << p.toCell.x << ',' << p.toCell.y << ',' << d << ',' << before << ',' << afterHP << ','
                    << expected << ',' << actual << ',' << p.weaponId << ',' << p.warheadId << ",250,0.5," << outgoing << ',' << falloff << '\n';
                if (actual!=expected) observationError[name]="authored bomb HP delta actor="+std::to_string(p.actorId)
                    +" expected="+std::to_string(expected)+" actual="+std::to_string(actual)+" before="+std::to_string(before)
                    +" after="+std::to_string(afterHP)+" distance="+std::to_string(d);
                if (actual>0) ++impactCount[name];
            };
        };
        attachObserver(replicas.front());
        const auto same = [&]() {
            const auto& a=*replicas.front().state;
            for (std::size_t i=1;i<replicas.size();++i) {
                const auto& b=*replicas[i].state;
                const auto* ao=findRuntimeObjectByIdIncludingInactive(a,source);
                const auto* bo=findRuntimeObjectByIdIncludingInactive(b,source);
                const auto* at=findRuntimeObjectByIdIncludingInactive(a,target);
                const auto* bt=findRuntimeObjectByIdIncludingInactive(b,target);
                const auto* ac=findObjectById(a,distantEnemy);
                const auto* bc=findObjectById(b,distantEnemy);
                bool exact=a.tick==b.tick && a.config.fixedTickHz==b.config.fixedTickHz && ao && bo && at && bt
                    && ac && bc && ac->health==750 && bc->health==750
                    && ao->x==bo->x && ao->y==bo->y && ao->health==bo->health && ao->mission==bo->mission
                    && ao->airborne==bo->airborne && physical(*ao)==physical(*bo)
                    && ao->aircraftBuildingClearanceLevel==bo->aircraftBuildingClearanceLevel
                    && at->health==bt->health && at->alive==bt->alive
                    && serializeRuntimeJumpjetFlights(a.jumpjetFlights)==serializeRuntimeJumpjetFlights(b.jumpjetFlights)
                    && a.projectiles.size()==b.projectiles.size();
                if (exact) for (std::size_t j=0;j<a.projectiles.size();++j)
                    if (serializeRuntimeProjectileEffect(a.projectiles[j])!=serializeRuntimeProjectileEffect(b.projectiles[j])) {exact=false;break;}
                if (!exact) return fail("strict continuation differs "+replicas[i].name+" original="+describe(a,source)
                    +" copy="+describe(b,source)+" targetHP="+std::to_string(at ? at->health : -1)+"/"+std::to_string(bt ? bt->health : -1));
            }
            return true;
        };
        const auto checkpoint = [&](const std::string& name) {
            stage=name; auto& s=*replicas.front().state;
            std::ostringstream ordinary, extended, normalized; IniFile base, canonical;
            if (!writeRuntimeStateToStream(s,ordinary,error) || !base.loadFromString(ordinary.str()))
                return fail("ordinary strict save: "+error);
            extended << ordinary.str();
            if (!writeRuntimeMultiplayerSnapshotExtension(s,extended,error) || !canonical.loadFromString(extended.str())
                || !writeRuntimeMultiplayerSnapshotCanonicalIni(canonical,normalized,error)
                || !canonical.loadFromString(normalized.str())) return fail("canonical strict save: "+error);
            // Later checkpoints replace the previous continuations, each
            // freshly rebuilt from the same real original state.
            scopes.clear(); replicas.resize(1);
            for (bool exact : {false,true}) {
                const std::string format=exact ? "canonical" : "ordinary";
                const std::string filename=label+"-"+name+"-"+format+".ini";
                std::ofstream saved(root/filename,std::ios::binary); saved << (exact ? normalized.str() : ordinary.str());
                if (!saved) return fail("checkpoint file unavailable");
                auto copy=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy,s,false);
                RuntimeSnapshotActiveStateScope active(*copy);
                if (!materializeRuntimeLoadStateFromIni(*copy,base,error)
                    || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*copy,canonical,error)))
                    return fail(format+" strict restore: "+error);
                if (copy->rules.getDetailRulesHash()!=rulesHash || copy->tick!=s.tick || copy->config.noCooldown
                    || !copy->rulesLoaded || !copy->artLoaded || !copy->theaterLoaded
                    || copy->report.rulesPath!=s.report.rulesPath || copy->report.artPath!=s.report.artPath
                    || copy->world.getTerrainCells().size()!=s.world.getTerrainCells().size()
                    || copy->particles.fixedTickHz()!=hz || copy->runtimeProjectileImpactObserver)
                    return fail("restored rules/clock/cooldown/observer differ");
                copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;
                initializeRuntimeOfflineGameplayRouting(*copy);
                const auto& f=copy->jumpjetFlights.at(source);
                checkpoints << label << ',' << name << ',' << format << ',' << copy->tick << ',' << hz << ','
                    << source << ',' << target << ',' << f.clearanceLift << ',' << f.clearanceTick
                    << ",5," << copy->particles.currentTick() << ',' << copy->particles.fixedTickHz() << ',' << filename << ",PASS\n";
                replicas.push_back({format,std::move(copy)});
                exact ? (++sceneCanonical,++canonicalCount) : (++sceneOrdinary,++ordinaryCount);
            }
            for (auto& r:replicas) attachObserver(r);
            return same();
        };
        int endTick=-1, settledTicks=0;
        for (int n=0;n<hz*240;++n) {
            stage=attack ? "real UI bombing" : stopDecay && !resumed ? "real UI stopped descent" : "real UI flyover";
            const auto priorBody=physical(*findObjectById(*replicas.front().state,source));
            for (auto& r:replicas) step(*r.state);
            if (!same()) return false;
            auto& s=*replicas.front().state; const auto* o=findObjectById(s,source);
            if (!o || !o->airborne || o->health!=2000 || !s.jumpjetFlights.contains(source))
                return fail("original aircraft identity/full HP changed: "+describe(s,source));
            const auto& f=s.jumpjetFlights.at(source); const auto p=physical(*o);
            const auto delta=f.clearanceLift-lastLift;
            const auto upLimit=std::llround(climb*65536.0*15.0/hz), downLimit=std::llround(crash*65536.0*15.0/hz);
            if (delta>upLimit || delta < -downLimit || f.clearanceTick!=s.tick
                || !nearValue(o->aircraftBuildingClearanceLevel,double(f.clearanceLift)/runtimeJumpjetCellQ16)
                || !nearValue(p[2],cruise) || f.height!=std::llround(cruise*runtimeJumpjetCellQ16)
                || std::hypot(p[0]-priorBody[0],p[1]-priorBody[1])
                    > originalSpeedLimit*15.0/hz/256.0+2.0/runtimeJumpjetCellQ16)
                return fail("original rate/physical authority changed delta="+std::to_string(delta)+" "+describe(s,source));
            if (delta>0) ++up; if (delta<0) ++down;
            peak=std::max(peak,o->aircraftBuildingClearanceLevel); lastLift=f.clearanceLift;
            const double along=axis=='X' ? p[0] : p[1], across=axis=='X' ? p[1] : p[0];
            const double priorAlong=axis=='X' ? priorBody[0] : priorBody[1];
            // The final four-cell approach and original 4x4 tall foundation
            // share one forward roof support plane. No Stop/turn/roof exit
            // has occurred here: a cell-boundary Cooldown is still travel,
            // and cannot introduce a one-tick Crash dip before rising again.
            if (!attack && !stopDecay && priorAlong>=66.0 && along<=73.5
                && across>=63.5 && across<=67.5 && delta<0)
                return fail("uninterrupted tall-roof approach dipped at a segment boundary delta="
                    +std::to_string(delta)+" "+describe(s,source));
            if (along>=69.5 && along<=73.5 && across>=63.5 && across<=67.5 && f.clearanceLift>0)
                actualRoofOverlap=true;
            for (auto& r:replicas) {
                auto& q=*r.state; const auto* actor=findObjectById(q,source); const auto body=physical(*actor);
                const auto c=contextFor(q); const RECT viewport{0,0,800,600};
                const auto unlifted=runtimeObjectBodyAnchorForRender(*actor,viewport,c,q.camera);
                const auto placed=runtimeObjectWorldRenderPlacement(*actor,q.world,viewport,c,q.camera,q.renderCache);
                const int lift=unlifted.y-placed.center.y;
                const int expectedPixels=int(std::lround(actor->aircraftBuildingClearanceLevel*15.0));
                if (placed.center.x!=unlifted.x || std::abs(lift-expectedPixels)>1)
                    return fail("renderer clearance differs from tick-owned world lift replica="+r.name
                        +" actual="+std::to_string(lift)+" expected="+std::to_string(expectedPixels));
                const auto& rf=q.jumpjetFlights.at(source);
                const auto* t=findRuntimeObjectByIdIncludingInactive(q,target);
                movement << label << ',' << stage << ',' << r.name << ',' << q.tick << ',' << source << ','
                    << actor->x << ',' << actor->y << ',' << body[0] << ',' << body[1] << ',' << body[2] << ','
                    << rf.height << ',' << rf.clearanceLift << ',' << actor->aircraftBuildingClearanceLevel << ',' << rf.clearanceTick << ','
                    << unlifted.x << ',' << unlifted.y << ',' << placed.center.x << ',' << placed.center.y << ',' << lift << ','
                    << actor->health << ',' << actor->airborne << ',' << actor->renderMoveActive << ',' << rf.stopped << ','
                    << (t ? t->health : -1) << '\n';
                for (const auto& bomb:q.projectiles) if (bomb.attackerObjectId==source && bomb.projectileId=="BlimpBombP") {
                    const double logicalDistance=std::hypot(double(actor->x-70),double(actor->y-64));
                    const double physicalDistance=std::hypot(body[0]-71.5,body[1]-65.5);
                    if (!bomb.vertical || bomb.targetObjectId!=-1 || bomb.projectileImageId!="ZBOMB"
                        || bomb.acceleration!=1 || !nearValue(bomb.sourceX,bomb.targetX) || !nearValue(bomb.sourceY,bomb.targetY)
                        || !nearValue(bomb.currentX,bomb.sourceX) || !nearValue(bomb.currentY,bomb.sourceY)
                        || bomb.currentZ>bomb.sourceZ+1e-8 || bomb.currentZ<bomb.targetZ-1e-8
                        || bomb.weaponId!=(rank==2 ? "BlimpBombE" : "BlimpBomb")
                        || bomb.warheadId!=(rank==2 ? "KTSTLEXP" : "BlimpHE")
                        || !nearValue(bomb.veteranDamageMultiplier,rank==2 ? veteranCombat : 1.0))
                        return fail("original falling bomb properties differ replica="+r.name+" actor="+std::to_string(bomb.actorId));
                    if (r.name=="original") {
                        const auto old=previousBombs.find(bomb.actorId);
                        if (old!=previousBombs.end() && (bomb.currentZ>old->second.currentZ+1e-8
                            || bomb.sourceX!=old->second.sourceX || bomb.sourceY!=old->second.sourceY))
                            return fail("falling bomb rose or changed release column");
                        previousBombs[bomb.actorId]=bomb;
                    }
                    if (observedActors[r.name].insert(bomb.actorId).second) {
                        if (r.name=="original" && shotCount[r.name]==0
                            && (!(physicalDistance>.25) || physicalDistance>number(bomb.weaponId,"Range")+1e-8))
                            return fail("first real shot outside original Range from actual body to raw foundation center logical="
                                +std::to_string(logicalDistance)+" physical="+std::to_string(physicalDistance));
                        const int prior=previousShotTick[r.name], minimum=((rank==2 ? eliteMinimumFrames : 50)*hz+14)/15;
                        if (prior && bomb.spawnTick-prior<minimum) return fail("original ROF accelerated");
                        clocks << label << ',' << r.name << ',' << q.tick << ',' << bomb.actorId << ',' << prior << ','
                            << (prior ? bomb.spawnTick-prior : 0) << ',' << minimum << ",50," << (rank==2 ? veteranRof : 1.0) << '\n';
                        previousShotTick[r.name]=bomb.spawnTick; ++shotCount[r.name];
                    }
                    shots << label << ',' << r.name << ',' << q.tick << ',' << bomb.actorId << ',' << bomb.spawnTick << ','
                        << source << ',' << rank << ',' << logicalDistance << ',' << physicalDistance << ','
                        << bomb.sourceX << ',' << bomb.sourceY << ',' << bomb.sourceZ << ',' << bomb.targetX << ',' << bomb.targetY << ','
                        << bomb.targetZ << ',' << bomb.currentX << ',' << bomb.currentY << ',' << bomb.currentZ << ','
                        << bomb.elapsedTicks << ',' << bomb.totalTicks << ',' << bomb.remainingTicks << ',' << bomb.weaponId << ','
                        << bomb.warheadId << ',' << bomb.damage << ',' << bomb.veteranDamageMultiplier << ',' << bomb.vertical << ','
                        << bomb.acceleration << ',' << bomb.targetObjectId << '\n';
                }
                if (!observationError[r.name].empty()) return fail(observationError[r.name]);
            }
            // Read-only cache/camera queries and repeated flight advancement
            // within this same tick must leave full and authority saves intact.
            if (n%hz==0) {
                const auto full=serializeRuntimeJumpjetFlights(s.jumpjetFlights), authority=serializeRuntimeJumpjetFlights(s.jumpjetFlights,false);
                auto altered=s.jumpjetFlights; altered.at(source).clearanceLift+=1; altered.at(source).clearanceTick+=1;
                if (serializeRuntimeJumpjetFlights(altered,false)!=authority) return fail("local roof presentation leaked into flight authority");
                auto c=contextFor(s); (void)runtimeKirovClearanceTargetLevel(s.world,*o,c,s.renderCache,axis=='X',axis=='Y');
                advanceRuntimeJumpjetFlights(s);
                if (serializeRuntimeJumpjetFlights(s.jumpjetFlights)!=full) return fail("same-tick/pause or renderer query advanced flight");
                ++paused;
            }
            if (!attack && !risingSaved && up>=3 && delta>0) {
                if (!checkpoint("rising")) return false; risingSaved=true;
                if (!drawRuntimeStateOffscreenAtCell(s,runtimeCellPointForObject(*findObjectById(s,source)),800,900,error,
                    (root/(label+"-rising.bmp")).string(),false)) return fail(error);
            } else if (!attack && risingSaved && !fallingSaved && actualRoofOverlap && delta<0
                && f.active && f.progress>0 && f.progress<f.length) {
                if (!checkpoint("descending")) return false; fallingSaved=true;
                if (!drawRuntimeStateOffscreenAtCell(s,point(70),800,900,error,(root/(label+"-descending.bmp")).string(),false)) return fail(error);
                for (auto& r:replicas) {
                    auto& q=*r.state; q.selectedObjectIds={source};
                    const auto prior=physical(*findObjectById(q,source));
                    const auto liftBefore=q.jumpjetFlights.at(source).clearanceLift;
                    if (!dispatchRuntimeSelectionCommandFromUi(q,RuntimeMultiplayerGameplayCommandType::Stop)
                        || q.lastCommand.acceptedCount!=1 || physical(*findObjectById(q,source))!=prior
                        || q.jumpjetFlights.at(source).clearanceLift!=liftBefore)
                        return fail("real Stop snapped physical pose or roof lift replica="+r.name);
                }
                stopDecay=true;
            } else if (attack && !attackSaved && !observedActors["original"].empty()) {
                if (!checkpoint("first-falling-bomb")) return false; attackSaved=true;
                if (!drawRuntimeStateOffscreenAtCell(s,{70,64},800,900,error,(root/(label+"-release.bmp")).string(),false)) return fail(error);
            }
            if (stopDecay && !resumed && f.clearanceLift==0) {
                for (auto& r:replicas) {
                    auto& q=*r.state; q.selectedObjectIds={source};
                    const auto prior=physical(*findObjectById(q,source));
                    if (!q.jumpjetFlights.at(source).stopped
                        || !dispatchRuntimeTargetOrderFromUi(q,RuntimeMultiplayerGameplayTargetOrderKind::Move,point(88))
                        || q.lastCommand.acceptedCount!=1 || physical(*findObjectById(q,source))!=prior)
                        return fail("natural stopped descent/real resume differs replica="+r.name);
                }
                resumed=true;
            }
            const auto* t=findRuntimeObjectByIdIncludingInactive(s,target);
            const bool ended=attack ? t && t->health<=0 && shotCount["original"]>=2 && impactCount["original"]>=2
                : std::hypot(p[0]-point(88).x,p[1]-point(88).y)<1e-6 && !s.behaviors.contains(source);
            if (ended && f.clearanceLift==0 && s.projectiles.empty()) ++settledTicks; else settledTicks=0;
            if (settledTicks>=hz) {endTick=s.tick;break;}
        }
        stage="final native evidence";
        if (endTick<0 || up<2 || down<2 || peak<=0 || paused==0
            || (!attack && (!risingSaved || !fallingSaved || !actualRoofOverlap || !stopDecay || !resumed || sceneOrdinary!=2 || sceneCanonical!=2))
            || (attack && (!attackSaved || sceneOrdinary!=1 || sceneCanonical!=1 || shotCount["original"]<2 || impactCount["original"]<2)))
            return fail("incomplete natural transition/shot/save evidence up="+std::to_string(up)+" down="+std::to_string(down)
                +" shots="+std::to_string(shotCount["original"])+" impacts="+std::to_string(impactCount["original"])
                +" ended="+std::to_string(endTick)+" "+describe(*replicas.front().state,source));
        cases << label << ',' << hz << ',' << (attack ? "Primary" : "Move") << ',' << axis << ',' << rank << ','
            << source << ',' << target << ',' << firstTick << ',' << endTick << ',' << up << ',' << down << ',' << peak << ','
            << shotCount["original"] << ',' << impactCount["original"] << ',' << sceneOrdinary << ',' << sceneCanonical << ',' << paused
            << ',' << stopDecay << ',' << resumed << ",PASS\n";
        ++passed;
        std::cout << "Kirov clearance/bomb PASS " << label << " rise=" << up << " fall=" << down
            << " shots=" << shotCount["original"] << " impacts=" << impactCount["original"]
            << " saves=" << sceneOrdinary << '+' << sceneCanonical << '\n';
    }
    if ((!filter.empty() && passed!=1) || (filter.empty() && (passed!=8 || ordinaryCount!=12 || canonicalCount!=12)))
        return fail("exact scene/save census differs");
    std::cout << "Kirov clearance/bomb complete cases=" << passed << " ordinary14=" << ordinaryCount
        << " canonicalExtensions=" << canonicalCount << '\n';
    return true;
}
