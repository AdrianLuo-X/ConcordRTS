bool runRuntimeTerrainMovementSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Terrain movement: " + why; return false; };
    using Land = RuntimeTerrainLandType;
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    stock->rulesLoaded = stock->artLoaded = stock->theaterLoaded = true;
    const std::string root = "build/smoke/feature-checks/06-terrain-speed/checks";
    std::filesystem::create_directories(root);
    constexpr RuntimeCellPoint start{4,12}, finish{44,12};
    struct Fixture { std::unique_ptr<RuntimeWindowState> box; int id; };
    const auto paint = [](RuntimeWindowState& s, Land land) {
        for (auto& cell : s.world.accessTerrainCells()) {
            auto& info = s.terrainInfoByCell.at(runtimeCellKey(cell.x, cell.y));
            info.movementLandType = info.authoredLandType = land;
            info.water = land == Land::Water;
            info.shore = false; info.bridge = false; info.passable = true; info.cost = 10;
            if (land == Land::Tiberium) {
                cell.hasOverlay = cell.overlayIsOre = true; cell.overlayIndex = 102;
                cell.overlayType = cell.overlayImage = "GOLD01"; cell.overlayFrame = 11;
            }
        }
        s.pathTerrainCache.valid = s.pathTerrainCache.dynamicValid = false;
    };
    const auto make = [&](const char* type, bool infantry, int hz, int veteran, Land land) {
        Fixture f; f.box = std::make_unique<RuntimeWindowState>(); auto& s = *f.box;
        s.config = config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.computerAiEnabled = s.config.noCooldown = s.config.multiplayerActive = false;
        s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater; s.report = stock->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        RuntimeHumanHouseAuthority authority; authority.owner = "Americans";
        authority.canonicalOwner = "americans"; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 48; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        auto* object = addRuntimeObjectInstance(s, infantry ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit, type, start);
        f.id = object ? object->instanceId : -1;
        if (object) {
            object->veterancy = veteran; object->facing = runtimeFacingFromDelta(1,0);
            object->harvestAutoSearchSuspended = true;
        }
        initializeRuntimeNavigation(s); paint(s, land);
        return f;
    };
    const auto begin = [&](Fixture& f) {
        auto& s = *f.box;
        applySelection(s, {f.id}, false, false); issueRuntimeMoveCommand(s, finish);
        if (s.lastCommand.acceptedCount != 1 || !s.behaviors.contains(f.id)) return false;
        std::vector<RuntimeCellPoint> path;
        for (int x = findObjectById(s,f.id)->x + 1; x <= finish.x; ++x) path.push_back({x,finish.y});
        auto& b = s.behaviors.at(f.id); replaceRuntimeBehaviorPath(b,path); b.pathRequestPending=false;
        return true;
    };
    const auto step = [](Fixture& f) { ++f.box->tick; advanceRuntimeSimulation(*f.box); };
    std::map<std::tuple<std::string,int,int>,int> walkReferenceTicks;
    const auto cruise = [&](Fixture& f, double expectedMultiplier, const std::string& label) {
        auto& s = *f.box;
        if (!begin(f)) return fail(label + " order rejected");
        const auto* mover = findObjectById(s,f.id);
        const double base = runtimeMovementSegmentIntervalQ16(s,*mover,{8,12},{9,12}) / 65536.0;
        int first=-1, last=-1;
        for (int i=0; i<s.config.fixedTickHz*180 && last<0; ++i) {
            step(f); mover=findObjectById(s,f.id);
            if (!mover || !mover->alive) return fail(label + " mover lost");
            if (mover->x == 8 && first<0) first=s.tick;
            if (mover->x == 32 && last<0) last=s.tick;
            const auto found=s.behaviors.find(f.id);
            if (found!=s.behaviors.end() && found->second.moveCooldownTicks>0
                && found->second.renderMoveTicksRemaining!=found->second.moveCooldownTicks)
                return fail(label + " render and movement clocks diverged");
        }
        double expected=24*base/expectedMultiplier;
        if (mover->kind==WorldObjectKind::Infantry && first>=0 && last>=0) {
            // Infantry also wait for the existing tick-wide subcell batch.
            // Compare surfaces to the same Walker's measured neutral route;
            // absolute Walk cadence belongs to the separate time-base audit.
            const auto key=std::make_tuple(mover->type,s.config.fixedTickHz,mover->veterancy);
            const auto [reference,inserted]=walkReferenceTicks.try_emplace(key,last-first);
            (void)inserted;
            expected=reference->second;
        }
        if (first<0 || last<0 || std::abs((last-first)-expected)>1.1)
            return fail(label + " cruise="+std::to_string(last-first)+" expected="+std::to_string(expected)
                +" x="+std::to_string(mover->x)+" status="+s.simulationStatus);
        std::cout << label << " ticks=" << last-first << " expected=" << expected << '\n';
        return true;
    };
    int cases=0;
    // Actual simulation movement over 24 middle cells; no start/turn/arrival time.
    for (int hz : {30,60}) for (int veteran : {0,1}) {
        for (Land land : {Land::Clear,Land::Road,Land::Tiberium}) {
            for (bool wheel : {false,true}) {
                auto f=make("MTNK",false,hz,veteran,land);
                auto* type=const_cast<UnitType*>(findRuntimeUnitType(f.box->rules,*findObjectById(*f.box,f.id)));
                if (wheel) type->speedType="Wheel";
                const double multiplier=land==Land::Tiberium ? wheel ? 0.5 : 0.7 : 1.0;
                if (!cruise(f,multiplier,std::string(wheel?"Wheel":"Track")+" land="+std::to_string(int(land))
                    +" Hz="+std::to_string(hz)+" veteran="+std::to_string(veteran))) return false;
                ++cases;
            }
        }
        for (const auto& sample : {std::pair{"LCRF",false},std::pair{"GHOST",true}}) {
            for (Land land : {Land::Clear,Land::Road,Land::Water}) {
                auto f=make(sample.first,sample.second,hz,veteran,land);
                if (!cruise(f,1.0,std::string(sample.first)+" land="+std::to_string(int(land))
                    +" Hz="+std::to_string(hz)+" veteran="+std::to_string(veteran))) return false;
                ++cases;
            }
        }
        for (double percent : {100.0,40.0}) {
            auto f=make("DEST",false,hz,veteran,Land::Water);
            const_cast<LandType*>(f.box->rules.findLandType("Water"))->floatSpeed=percent;
            if (!cruise(f,percent/100.0,"Ship Float="+std::to_string(percent)+" Hz="+std::to_string(hz)
                +" veteran="+std::to_string(veteran))) return false;
            ++cases;
        }
        for (Land land : {Land::Clear,Land::Water}) {
            auto f=make("MTNK",false,hz,veteran,land);
            auto* type=const_cast<UnitType*>(findRuntimeUnitType(f.box->rules,*findObjectById(*f.box,f.id)));
            type->speedType="Amphibious"; type->movementZone="Amphibious";
            if (!cruise(f,land==Land::Clear?0.8:1.0,"Drive Amphibious land="+std::to_string(int(land))
                +" Hz="+std::to_string(hz)+" veteran="+std::to_string(veteran))) return false;
            ++cases;
        }
    }
    // Stock IFV and demolition truck omit SpeedType and default to Wheel
    // because Crusher=no; their model's appearance does not choose the type.
    for (const char* id : {"FV","DTRUCK"}) {
        auto f=make(id,false,30,0,Land::Tiberium);
        const auto* type=findRuntimeUnitType(f.box->rules,*findObjectById(*f.box,f.id));
        if (!type || type->crusher || !type->speedType.empty()) return fail("stock Wheel default fixture");
        if (!cruise(f,0.5,std::string(id)+" implicit Wheel")) return false;
        ++cases;
    }
    for (Land land : {Land::Clear,Land::Tiberium}) {
        auto f=make("MTNK",false,30,0,land);
        auto* object=findMutableObjectById(*f.box,f.id);
        object->health=runtimeMaxHealthForObject(*f.box,*object)/2;
        if (!cruise(f,(land==Land::Clear?1.0:0.7)*0.75,"Yellow health land="+std::to_string(int(land)))) return false;
        ++cases;
    }
    // Exercise actual stock TMP decoding outside the old shoreline-only path.
    {
        rebuildRuntimeTerrainInfoCache(*stock);
        const TerrainCell* road=nullptr;
        for (const auto& cell:stock->world.getTerrainCells()) {
            const auto& info=stock->terrainInfoByCell.at(runtimeCellKey(cell.x,cell.y));
            if (info.movementLandType==Land::Road && !info.bridge && !info.ramp
                && info.movementRampType==0 && info.passable) {
                road=&cell; break;
            }
        }
        if (!road) return fail("original map has no usable road TMP for metadata fixture");
        auto f=make("MTNK",false,30,0,Land::Clear);
        for (auto& cell:f.box->world.accessTerrainCells()) {
            cell.tileIndex=road->tileIndex; cell.subTileIndex=road->subTileIndex;
        }
        initializeRuntimeNavigation(*f.box);
        const_cast<LandType*>(f.box->rules.findLandType("Road"))->track=50;
        if (!cruise(f,0.5,"Original road TMP custom Track=50%")) return false;
        ++cases;
    }
    // Alternating half-height ramp centres produce a continuous long slope.
    for (bool wheel : {false,true}) for (bool down : {false,true}) for (bool custom : {false,true}) {
        auto f=make("MTNK",false,30,0,Land::Tiberium); auto& s=*f.box;
        auto* type=const_cast<UnitType*>(findRuntimeUnitType(s.rules,*findObjectById(s,f.id)));
        if (wheel) type->speedType="Wheel";
        auto& slopes=const_cast<TerrainMovementRules&>(s.rules.getTerrainMovementRules());
        if (custom) { slopes.trackedUphill=0.5; slopes.trackedDownhill=1.1;
            slopes.wheeledUphill=0.75; slopes.wheeledDownhill=1.5; }
        for (auto& cell:s.world.accessTerrainCells()) {
            const int half=down ? 48-cell.x : cell.x;
            cell.level=half/2;
            auto& info=s.terrainInfoByCell.at(runtimeCellKey(cell.x,cell.y));
            info.ramp=true; info.movementRampType=half%2?1:0;
        }
        const double slope=down ? wheel?slopes.wheeledDownhill:slopes.trackedDownhill
            : wheel?slopes.wheeledUphill:slopes.trackedUphill;
        if (!cruise(f,std::min(1.0,(wheel?0.5:0.7)*slope),"Slope wheel="+std::to_string(wheel)
            +" down="+std::to_string(down)+" custom="+std::to_string(custom))) return false;
        ++cases;
    }
    // Sample the committed destination, obey both caps, and compose damage
    // AFTER the slope but BEFORE the final SpeedPercentage cap.
    {
        auto f=make("MTNK",false,30,0,Land::Clear); auto& s=*f.box;
        auto* object=findMutableObjectById(s,f.id);
        auto& a=s.terrainInfoByCell.at(runtimeCellKey(4,12));
        auto& b=s.terrainInfoByCell.at(runtimeCellKey(5,12));
        b.movementLandType=Land::Tiberium;
        const auto check=[&](double expected) {
            return std::abs(runtimeTerrainMovementSpeedMultiplier(s,*object,{4,12},{5,12})-expected)<1e-6;
        };
        setRuntimeObjectCell(s,*object,{5,12},false);
        if (!check(0.7)) return fail("crossing used already committed object position as origin");
        a.movementRampType=1;
        if (!check(0.84)) return fail("half-height slope on equal base levels");
        b.movementLandType=Land::Clear;
        if (!check(1.0)) return fail("full-speed downhill exceeded cap");
        object->health=runtimeMaxHealthForObject(s,*object)/2;
        if (!check(0.9)) return fail("damage composed in wrong order at the speed cap");
        a.movementRampType=0;
        if (!check(0.75)) return fail("yellow health speed reduction");
        object->health=runtimeMaxHealthForObject(s,*object);
        b.movementRampType=1;
        auto& slopes=const_cast<TerrainMovementRules&>(s.rules.getTerrainMovementRules());
        slopes.trackedUphill=0.5;
        const_cast<LandType*>(s.rules.findLandType("Clear"))->track=150;
        if (!check(0.5)) return fail("land multiplier capped after rather than before slope");
        slopes.trackedUphill=0.0;
        if (!check(0.5)) return fail("zero product fallback");
        ++cases;
    }
    for (bool ship : {false,true}) {
        auto f=make(ship?"DEST":"MTNK",false,30,0,Land::Water); auto& s=*f.box;
        auto* object=findMutableObjectById(s,f.id);
        auto& info=s.terrainInfoByCell.at(runtimeCellKey(5,12)); info.bridge=true;
        const_cast<LandType*>(s.rules.findLandType("Road"))->track=80;
        const_cast<LandType*>(s.rules.findLandType("Water"))->floatSpeed=40;
        if (std::abs(runtimeTerrainMovementSpeedMultiplier(s,*object,{4,12},{5,12})-(ship?0.4:0.8))>1e-6)
            return fail("bridge deck and underlying water were mixed");
        ++cases;
    }
    // Both direct pursuit and local avoidance commit through the same clock.
    for (bool avoidance : {false,true}) {
        auto f=make("MTNK",false,30,0,Land::Tiberium); auto& s=*f.box;
        auto* object=findMutableObjectById(s,f.id);
        RuntimeObjectBehavior b; b.kind=avoidance?RuntimeBehaviorKind::Move:RuntimeBehaviorKind::Attack;
        b.moveTarget=finish;
        if (avoidance) {
            if (!tryRuntimeLocalAvoidanceMove(s,*object,b,false,"terrain-speed-smoke"))
                return fail("local avoidance did not move");
        } else {
            const int target=addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Unit,"MTNK",{40,12})->instanceId;
            applySelection(s,{f.id},false,false); issueRuntimeAttackCommand(s,*findObjectById(s,target));
            for (int i=0;i<100 && findObjectById(s,f.id)->x==start.x;++i) step(f);
            object=findMutableObjectById(s,f.id);
            if (!s.behaviors.contains(f.id) || object->x==start.x) return fail("attack approach did not advance");
            b=s.behaviors.at(f.id);
        }
        const double expected=runtimeMovementSegmentIntervalTicks(s,*object,b.renderFrom,b.renderTo)/0.7;
        if (b.moveCooldownTicks<=0 || std::abs(b.moveCooldownTicks-expected)>0.51
            || b.renderMoveTicksTotal!=b.moveCooldownTicks) return fail("pursuit/avoidance bypassed terrain timing");
        ++cases;
    }
    for (int elapsed : {0,5}) {
        auto f=make("MTNK",false,30,0,Land::Tiberium); auto& s=*f.box;
        if (!begin(f)) return fail("save route rejected");
        step(f);
        for (int i=0;i<elapsed;++i) step(f);
        if (s.behaviors.at(f.id).moveIntervalRemainderQ16==0) return fail("fractional save fixture rounded away");
        const auto before=s.behaviors.at(f.id);
        const std::string path=root+"/moving.ini";
        if (!saveRuntimeState(s,path,error)) return fail("save: "+error);
        auto restored=make("MTNK",false,30,0,Land::Clear);
        if (!loadRuntimeState(*restored.box,path,error)) return fail("load: "+error);
        const auto& loaded=restored.box->behaviors.at(f.id);
        if (loaded.moveCooldownTicks!=before.moveCooldownTicks
            || loaded.moveIntervalRemainderQ16!=before.moveIntervalRemainderQ16)
            return fail("save lost committed ticks or fractional carry");
        for (int i=0;i<180;++i) {
            step(f); step(restored);
            const auto* a=findObjectById(s,f.id); const auto* b=findObjectById(*restored.box,f.id);
            const auto ba=s.behaviors.find(f.id), bb=restored.box->behaviors.find(f.id);
            if (runtimeCellPointForObject(*a)!=runtimeCellPointForObject(*b)
                || (ba==s.behaviors.end())!=(bb==restored.box->behaviors.end())
                || (ba!=s.behaviors.end() && (ba->second.moveCooldownTicks!=bb->second.moveCooldownTicks
                    || ba->second.moveIntervalRemainderQ16!=bb->second.moveIntervalRemainderQ16))) {
                const auto describe=[&](RuntimeWindowState& state,const WorldObjectInstance& object) {
                    const auto behavior=state.behaviors.find(f.id);
                    return " pos="+std::to_string(object.x)+","+std::to_string(object.y)
                        +" hp="+std::to_string(object.health)+"/"+std::to_string(runtimeMaxHealthForObject(state,object))
                        +" cells="+std::to_string(state.terrainByCell.size())
                        +" factor="+std::to_string(runtimeTerrainMovementSpeedMultiplier(state,object,
                            {object.x,object.y},{object.x+1,object.y}))
                        +(behavior==state.behaviors.end()?" no-behavior":" cd="+std::to_string(behavior->second.moveCooldownTicks)
                            +" remainder="+std::to_string(behavior->second.moveIntervalRemainderQ16)
                            +" path="+std::to_string(behavior->second.path.size()))
                        +" status="+state.simulationStatus;
                };
                std::string diagnostic;
                saveRuntimeState(s,root+"/live-after.ini",diagnostic);
                saveRuntimeState(*restored.box,root+"/restored-after.ini",diagnostic);
                return fail("restored terrain movement diverged at tick "+std::to_string(i)
                    +" live:"+describe(s,*a)+" restored:"+describe(*restored.box,*b));
            }
        }
        ++cases;
    }
    {
        auto f=make("MTNK",false,30,0,Land::Tiberium); auto& s=*f.box;
        RuntimeMultiplayerGameplayTargetOrderCommand first;
        first.objectIds={f.id}; first.order=RuntimeMultiplayerGameplayTargetOrderKind::Move;
        first.target=RuntimeMultiplayerGameplayCellTarget{{20,12}};
        auto second=first; second.append=true;
        second.target=RuntimeMultiplayerGameplayCellTarget{{finish.x,finish.y}};
        if (!applyRuntimePlayerTargetOrder(s,"Americans",first)
            || !applyRuntimePlayerTargetOrder(s,"Americans",second)) return fail("queue setup");
        bool secondStarted=false, measured=false, completed=false;
        for (int i=0;i<2000 && !completed;++i) {
            step(f); const auto* object=findObjectById(s,f.id);
            const auto found=s.behaviors.find(f.id);
            if (found!=s.behaviors.end()) {
                const auto& b=found->second;
                if (b.moveTarget==finish && !secondStarted) {
                    if (object->x<20) return fail("queued route replaced unfinished first route");
                    secondStarted=true;
                }
                if (object->x>=23 && object->x<=36 && b.moveCooldownTicks>0
                    && b.renderMoveTicksRemaining==b.renderMoveTicksTotal) {
                    const double expected=runtimeMovementSegmentIntervalTicks(s,*object,b.renderFrom,b.renderTo)/0.7;
                    if (std::abs(b.moveCooldownTicks-expected)>1.01) return fail("queued move lost terrain multiplier");
                    measured=true;
                }
            }
            completed=runtimeCellPointForObject(*object)==finish && found==s.behaviors.end();
        }
        if (!secondStarted || !measured || !completed) return fail("queued terrain route did not finish");
        ++cases;
    }
    // Check new rule inputs independently; all affect compatibility.
    {
        const std::string base="[General]\n";
        IniFile ini; RulesDatabase defaults;
        if (!ini.loadFromString(base) || !defaults.loadFromIni(ini)) return fail("default rules");
        const auto& d=defaults.getTerrainMovementRules();
        if (d.trackedUphill!=1.0 || d.trackedDownhill!=1.0 || d.wheeledUphill!=1.0 || d.wheeledDownhill!=1.0)
            return fail("slope defaults");
        for (const char* key:{"TrackedUphill","TrackedDownhill","WheeledUphill","WheeledDownhill"}) {
            IniFile changed; RulesDatabase rules;
            if (!changed.loadFromString(base+key+"=0.65\n") || !rules.loadFromIni(changed)
                || rules.getDetailRulesHash()==defaults.getDetailRulesHash()) return fail(std::string("rule/hash ")+key);
        }
        IniFile terrain; RulesDatabase changed;
        if (!terrain.loadFromString(base+"[Road]\nTrack=65%\n") || !changed.loadFromIni(terrain)
            || changed.getDetailRulesHash()==defaults.getDetailRulesHash()) return fail("land speed compatibility hash");
        ++cases;
    }
    std::cout << "Terrain movement passed: " << cases << " cases; actual middle-route timing, 30/60 Hz, rookie/Faster, "
        "Track/Wheel/Ship/Drive-Amphibious, Hover/Walk exclusions, slope centres and caps, damage, bridges, "
        "attack/avoidance, fractional save/restore and rules compatibility\n";
    return true;
}
