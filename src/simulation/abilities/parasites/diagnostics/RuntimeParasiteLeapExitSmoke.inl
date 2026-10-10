// Real UI DroneJump launches, native DRONP pixels and natural host deaths.
// Fixtures use full stock health and rules. No projectile is removed, impact
// invoked, damage injected or continuous object pose fabricated by this probe.
bool runRuntimeParasiteLeapExitSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Parasite leap/exit " + label + " [" + stage + "]: " + why; return false;
    };
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->config.playerSlots.clear(); stock->config.gameModeRulesOverride.clear();
    stock->config.computerAiEnabled = false;
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    IniFile native;
    if (!stock->report.success || !native.loadFromFile(stock->report.rulesPath)
        || !stock->rules.loadFromIni(native)) return fail("original resources/rules unavailable");
    stock->rulesLoaded = stock->artLoaded = stock->theaterLoaded = true;
    const auto* jump = stock->rules.findWeaponType("DroneJump");
    const auto* dron = stock->rules.findUnitType("Vehicle", "DRON");
    const auto* tank = stock->rules.findUnitType("Vehicle", "HTNK");
    const auto* bodyArt = stock->art.findArtType("DRONP");
    const auto* jumpType = jump ? stock->rules.findProjectileType(jump->projectile) : nullptr;
    const auto* parasite = jump ? stock->rules.findWarheadType(jump->warhead) : nullptr;
    if (!jump || !dron || !tank || !bodyArt || !jumpType || !parasite
        || jump->damage != 50 || jump->rof != 60 || dron->strength != 100 || tank->strength != 400
        || jumpType->image != "DRONP" || !bodyArt->rotates || !parasite->parasite)
        return fail("stock DRON/HTNK/DroneJump/DRONP identity changed");
    const auto rulesHash = stock->rules.getDetailRulesHash();
    int landTile = -1, waterTile = -1;
    for (int index = 0; index < static_cast<int>(stock->theater.getTotalTileSlots()); ++index) {
        TerrainCell cell; cell.tileIndex = index; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(*stock, cell);
        if (landTile < 0 && info.passable && !info.water && !info.shore && !info.cliff && !info.bridge && !info.ramp)
            landTile = index;
        if (waterTile < 0 && info.water && !info.shore && !info.cliff && !info.bridge && !info.ramp)
            waterTile = index;
        if (landTile >= 0 && waterTile >= 0) break;
    }
    const auto* overlays = native.getSection("OverlayTypes"); int bridgeOrdinal = -1;
    if (overlays) for (std::size_t i = 0; i < overlays->size(); ++i)
        if ((*overlays)[i].second == "BRIDGE1") bridgeOrdinal = static_cast<int>(i);
    if (landTile < 0 || waterTile < 0 || bridgeOrdinal < 0 || native.getValue("BRIDGE1", "Image", "") != "BRIDGE")
        return fail("stock clear/water TMP / BRIDGE1 registry missing");
    const char* focus = std::getenv("CONCORD_PARASITE_LEAP_EXIT_CASE");
    const char* output = std::getenv("CONCORD_PARASITE_LEAP_EXIT_OUTPUT");
    const std::filesystem::path root = output && *output ? output
        : "build/smoke/feature-set-22/02-parasite-leap-exit";
    std::filesystem::create_directories(root);
    std::ofstream positions(root / "trajectories.csv"), cases(root / "cases.csv");
    positions << std::setprecision(17) << "case,replica,tick,actor,phase,x,y,z,targetX,targetY,targetZ,speed,nativeFrame,pixels\n";
    cases << "case,hz,launchTick,contactTick,deathTick,travelFrames,sourceFractional,hostMoved,exitX,exitY,exitZ,bridge,result\n";
    constexpr int sourceId = 101, hostId = 102, width = 384, height = 256;
    constexpr std::uint32_t background = 0x001d2935U, alternateBackground = 0x00d1bd8bU;
    constexpr double tau = 6.28318530717958647692, levelsPerCell = 2.449489742783178;
    const RECT viewport{0, 0, width, height};
    const std::array<RuntimeCellPoint, 8> directions{{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}}};
    int attempted = 0, passed = 0, flightFrames = 0, nativeChecks = 0, coldChecks = 0;
    int saves = 0, snapshots = 0, continuedTicks = 0, captures = 0, bridgeCases = 0;
    int postExitSaves = 0, postExitSnapshots = 0, fractionalDeathCases = 0;
    std::set<int> directionFrames; std::vector<std::string> failures;
    const auto facade = [](RuntimeWindowState& s, const std::string& owner) {
        s.economy.owner = owner; s.config.playerCountry = owner; s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority authority; authority.owner = owner; authority.country = owner;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(owner); authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick;
        advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto contextFor = [&](const RuntimeWindowState& s) {
        RuntimeMapRenderContext c; c.rules = &s.rules; c.art = &s.art; c.theater = &s.theater;
        c.assetRootPath = runtimeAssetRootPath(s.config); c.palettePath = runtimePalettePath(s.config, s.report.map);
        c.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
        c.ownerRemapColors = &s.ownerRemapColors; c.animationTick = s.tick; c.fixedTickHz = s.config.fixedTickHz;
        c.surfaceWidth = width; c.surfaceHeight = height; c.effectOnlyProbe = true;
        c.shadowsEnabled = false; c.useTileTextures = false; c.vision = nullptr; c.lightingTint = nullptr;
        return c;
    };
    const auto describe = [](const WorldObjectInstance* o) {
        if (!o) return std::string("absent");
        std::ostringstream out; out << std::setprecision(17) << o->type << " alive=" << o->alive << " hp=" << o->health
            << " cell=" << o->x << ',' << o->y << " xyz=" << runtimeObjectRenderX(*o) << ',' << runtimeObjectRenderY(*o)
            << ',' << runtimeDrawLevelForObject(*o) << " level=" << o->level << " bridge=" << o->onBridge
            << " renderPosition=" << o->hasRenderPosition << " moving=" << o->renderMoveActive
            << " renderBridge=" << o->renderOnBridge << " facing=" << o->facing
            << " mission=" << o->mission; return out.str();
    };
    const auto fractional = [](const WorldObjectInstance& o) {
        return std::abs(runtimeObjectRenderX(o) - std::round(runtimeObjectRenderX(o))) > 1e-5
            || std::abs(runtimeObjectRenderY(o) - std::round(runtimeObjectRenderY(o))) > 1e-5;
    };
    const auto hostPosition = [](const WorldObjectInstance& o) {
        const auto offset = worldObjectSubCellOffset(o);
        return std::array<double, 3>{runtimeObjectRenderX(o) + offset.x,
            runtimeObjectRenderY(o) + offset.y, runtimeDrawLevelForObject(o)};
    };
    const auto make = [&](const std::string& targetType, int hz, int direction, bool bridge, int elevation) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, *stock, false); s.config = stock->config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.skirmishLoadSavePath.clear();
        s.config.noCooldown = s.config.shortGame = s.config.crates = s.config.superweapons = s.config.multiplayerActive = false;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        const int bankLevel = elevation + (bridge ? 4 : 0);
        for (int y = 20; y <= 60; ++y) for (int x = 20; x <= 60; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = landTile; cell.subTileIndex = 0; cell.level = bankLevel;
            if (bridge && x >= 30 && x <= 49) {
                // The stock high bridge adds four levels to its underlying
                // tile. Its banks must meet that deck so native paths can enter.
                // Original water below and beside the deck keeps a real ground
                // path on the bridge instead of taking a cheaper land detour.
                cell.tileIndex = waterTile; cell.level = elevation;
                if (y == 32) {
                    cell.hasOverlay = true; cell.overlayIndex = bridgeOrdinal; cell.overlayFrame = 0;
                    cell.overlayType = "BRIDGE1"; cell.overlayImage = "BRIDGE";
                }
            }
            s.world.accessTerrainCells().push_back(std::move(cell));
        }
        const auto add = [&](int id, const std::string& type, WorldObjectKind kind, RuntimeCellPoint cell, const std::string& owner) {
            WorldObjectInstance o; o.instanceId = id; o.mapId = "ParasiteLeapExit" + std::to_string(id);
            o.type = type; o.kind = kind; o.owner = owner; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(o.x, o.y); o.level = bankLevel; o.alive = o.poweredOperational = true;
            o.health = runtimeMaxHealthForObject(s, o); o.mission = "Sleep";
            o.facing = o.turretFacing = runtimeFacingFromDelta(directions[direction].x, directions[direction].y);
            initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
            runtimeInitializeModExtensions(o, s.rules, hz); s.world.accessObjects().push_back(std::move(o));
        };
        add(sourceId, "DRON", WorldObjectKind::Unit, bridge ? RuntimeCellPoint{29,32} : RuntimeCellPoint{32,32}, "Americans");
        add(hostId, targetType, targetType == "E1" ? WorldObjectKind::Infantry : WorldObjectKind::Unit,
            bridge ? RuntimeCellPoint{28,32} : RuntimeCellPoint{32 + directions[direction].x,32 + directions[direction].y}, "Russians");
        add(998, "NAPOWR", WorldObjectKind::Structure, {54,54}, "Russians");
        add(999, "GAPOWR", WorldObjectKind::Structure, {58,54}, "Americans");
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        facade(s, "Americans"); return box;
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream text, canonical; IniFile base, snapshot;
        if (!writeRuntimeStateToStream(s, text, error) || !base.loadFromString(text.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeMultiplayerSnapshotExtension(s, text, error)
            || !snapshot.loadFromString(text.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot, canonical, error)
            || !snapshot.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto copy = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy, s, false);
        RuntimeSnapshotActiveStateScope active(*copy);
        if (!materializeRuntimeLoadStateFromIni(*copy, base, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*copy, snapshot, error))) return std::unique_ptr<RuntimeWindowState>{};
        return copy;
    };
    struct Probe {
        std::string name; std::unique_ptr<RuntimeMapRenderCache> cache = std::make_unique<RuntimeMapRenderCache>();
        RuntimeProjectileEffect previous; bool havePrevious = false, contacted = false, exited = false, cold = false;
        int actor = -1, launchTick = -1, contactTick = -1, deathTick = -1, frames = 0, bites = 0;
        int nextBite = -1; WorldObjectInstance launchPose, exitPose;
        double initialHostX = 0, initialHostY = 0; bool moved = false;
        bool reselectExpected = false, hostMoveResumed = false, lethalRoutePrepared = false;
        RuntimeCellPoint hostRouteGoal; int hostMoveCommands = 0;
        std::vector<std::vector<std::uint32_t>> captures;
    };
    // Expected direction and opacity come from the native SHP table plus the
    // observed physical displacement. No renderer anchor/frame helper is an oracle.
    const auto pixelsFor = [&](RuntimeWindowState& s, Probe& probe, const RuntimeProjectileEffect& p, int tile,
        std::vector<std::uint32_t>& pixels, bool auditCold) {
        auto c = contextFor(s); RuntimeMapCamera camera; camera.initialized = true;
        camera.tileWidth = tile; camera.tileHeight = tile / 2;
        const auto projection = [&](double x, double y, double z) {
            return POINT{static_cast<LONG>(std::llround((x-y)*tile/2.0)),
                static_cast<LONG>(std::llround((x+y-z)*camera.tileHeight/2.0))};
        };
        const auto start = projection(p.sourceX, p.sourceY, p.sourceZ);
        camera.worldOffsetX = start.x - width / 2; camera.worldOffsetY = start.y - height / 2;
        const auto center = projection(p.currentX, p.currentY, p.currentZ);
        const double weight=1.0-std::clamp(p.progress,0.0,1.0);
        // The launch's native bitmap correction fades with actual travel.
        // Paint against its independently projected physical XYZ at each zoom.
        const POINT anchor{center.x-camera.worldOffsetX+static_cast<LONG>(std::lround(weight
                *(p.sourceNativeFixedX+(p.sourceScreenOffsetX-p.sourceNativeFixedX)*tile/60.0))),
            center.y-camera.worldOffsetY+static_cast<LONG>(std::lround(weight
                *(p.sourceNativeFixedY+(p.sourceScreenOffsetY-p.sourceNativeFixedY)*camera.tileHeight/30.0)))};
        RuntimeShpFrameImage first, image; std::string resolved, path, detail; int frameCount = 0;
        if (!loadRuntimeAuxiliaryShpFrameImage(c, *probe.cache, "DRONP", 0, first, resolved, path, detail, false, &frameCount)
            || frameCount <= 0 || path.empty()) return fail("native DRONP source decode: " + detail);
        const int count = std::max(1, std::min(frameCount, bodyArt->facings > 0 ? bodyArt->facings : 32));
        double dx = p.targetX-p.currentX, dy = p.targetY-p.currentY, dz = p.targetZ-p.currentZ;
        if (probe.havePrevious && p.elapsedTicks > probe.previous.elapsedTicks) {
            dx = p.currentX-probe.previous.currentX; dy = p.currentY-probe.previous.currentY; dz = p.currentZ-probe.previous.currentZ;
        }
        // Native rotations quantize each projected endpoint before selecting
        // a frame. Reconstruct that quantization from the observed motion.
        const double length=std::hypot(std::hypot(dx,dy),dz/levelsPerCell);
        if (length<=1e-10) return fail("native DRONP frame has no real travel direction");
        const double divisor=p.homingInitialized?length:1.0;
        const auto next=projection(p.currentX+dx/divisor,p.currentY+dy/divisor,p.currentZ+dz/divisor);
        double turns = std::atan2(static_cast<double>(next.x-center.x),static_cast<double>(center.y-next.y)) / tau;
        if (turns < 0) turns += 1;
        const int frame = (count-static_cast<int>(std::lround(turns*count))%count)%count;
        if (!loadRuntimeAuxiliaryShpFrameImage(c, *probe.cache, "DRONP", frame, image, resolved, path, detail))
            return fail("native DRONP directional frame: " + detail);
        const int left = anchor.x-image.globalWidth/2+image.offsetX, top = anchor.y-image.globalHeight/2+image.offsetY;
        if (left < 0 || top < 0 || left+image.width >= width || top+image.height >= height)
            return fail("native DRONP flight left its fixed camera");
        std::vector<RuntimeProjectileRenderActor> actors{makeRuntimeProjectileRenderActor(p, &s.rules, &s.art)};
        c.projectileActors = &actors;
        // The isolated projectile pass still requires a loaded map summary.
        GameWorld empty = s.world; empty.accessObjects().clear();
        pixels.assign(width*height, background); c.surfacePixels = pixels.data(); auto fixedCamera = camera;
        const auto drawn = drawRuntimeMap(nullptr, viewport, empty, c, fixedCamera, *probe.cache);
        std::vector<std::uint32_t> other(width*height, alternateBackground); c.surfacePixels = other.data(); fixedCamera = camera;
        const auto second = drawRuntimeMap(nullptr, viewport, empty, c, fixedCamera, *probe.cache);
        if (!drawn.rendered || !second.rendered || drawn.projectileActorTravelDrawn != 1
            || drawn.projectileDirectionalSprites != 1 || drawn.projectileActorTravelMissing) {
            std::ostringstream stats;
            stats << "real native DRONP body/direction missing: replica=" << probe.name
                << " tile=" << tile << " worldLoaded=" << empty.getSummary().loaded
                << " rendered=" << drawn.rendered << '/' << second.rendered
                << " snapshots=" << drawn.projectileActorSnapshots
                << " travel=" << drawn.projectileActorTravelDrawn
                << " directional=" << drawn.projectileDirectionalSprites
                << " missing=" << drawn.projectileActorTravelMissing
                << " paletteMissing=" << drawn.projectileActorPaletteMissing
                << " decodeMissing=" << drawn.projectileActorDecodeMissing
                << " expired=" << drawn.projectileActorExpired
                << " culled=" << drawn.projectileActorCulled
                << " image=" << p.projectileImageId
                << " lifecycle=" << actors.front().lifecycleState
                << " intent=" << actors.front().renderIntent
                << " resolved=" << probe.cache->lastProjectileImageId
                << " palette=" << probe.cache->lastProjectilePaletteSource;
            return fail(stats.str());
        }
        int visible = 0;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const bool actual = (pixels[y*width+x]&0xffffffU) != background
                || (other[y*width+x]&0xffffffU) != alternateBackground;
            const bool expected = x>=left && y>=top && x<left+image.width && y<top+image.height
                && image.pixels[(y-top)*image.width+x-left] != 0;
            if (actual != expected) return fail("native DRONP opacity/anchor/direction differs from raw frame: replica="
                + probe.name + " tile=" + std::to_string(tile) + " frame=" + std::to_string(frame));
            visible += actual ? 1 : 0;
        }
        if (visible < 8) return fail("native DRONP invisible travel frame");
        if (auditCold) {
            auto cold = std::make_unique<RuntimeMapRenderCache>(); std::vector<std::uint32_t> fresh(width*height, background);
            c.surfacePixels = fresh.data(); fixedCamera = camera;
            const auto coldDrawn = drawRuntimeMap(nullptr, viewport, empty, c, fixedCamera, *cold);
            if (!coldDrawn.rendered || fresh != pixels) return fail("cold native DRONP pixels differ");
            ++coldChecks;
        }
        if (tile == 60) {
            directionFrames.insert(frame);
            positions << label << ',' << probe.name << ',' << s.tick << ',' << p.actorId << ",flight,"
                << p.currentX << ',' << p.currentY << ',' << p.currentZ << ',' << p.targetX << ',' << p.targetY << ',' << p.targetZ
                << ',' << p.speedCellsPerTick << ',' << frame << ',' << visible << '\n';
        }
        ++nativeChecks; return true;
    };
    const auto run = [&](const std::string& targetType, int hz, int direction,
        const std::string& motion, bool bridge, int elevation, bool save) {
        label = targetType + '-' + std::to_string(hz) + "-d" + std::to_string(direction) + '-' + motion
            + (bridge ? "-bridge" : elevation ? "-high" : "");
        if (focus && *focus && label.find(focus) == std::string::npos) return;
        ++attempted; stage = "original fixture / UI Move";
        const bool ok = [&]() {
            auto box = make(targetType, hz, direction, bridge, elevation); auto& s = *box;
            RuntimeSnapshotActiveStateScope active(s);
            const auto originalAlliances=s.playerCommands.alliances;
            if (bridge) {
                // Real Stop enters Guard. Use the existing mutual house
                // alliance while both actors warm up so Guard cannot launch
                // DroneJump before the explicit UI Primary under test.
                if (runtimeOwnersAreAllied(s,"Americans","Russians"))
                    return fail("original houses unexpectedly allied before bridge warm-up");
                s.playerCommands.alliances[{"americans","russians"}]=true;
                s.playerCommands.alliances[{"russians","americans"}]=true;
                if (!runtimeOwnersAreAllied(s,"Americans","Russians"))
                    return fail("native mutual bridge warm-up alliance did not apply");
            }
            const auto move = [&](RuntimeWindowState& q, int id, const std::string& owner, RuntimeCellPoint goal) {
                RuntimeSnapshotActiveStateScope active(q); facade(q, owner); q.selectedObjectIds = {id};
                const auto* before = findObjectById(q, id); if (!before) return false;
                const double x = runtimeObjectRenderX(*before), y = runtimeObjectRenderY(*before), z = runtimeDrawLevelForObject(*before);
                const bool accepted = dispatchRuntimeTargetOrderFromUi(q, RuntimeMultiplayerGameplayTargetOrderKind::Move, goal)
                    && q.lastCommand.acceptedCount == 1;
                const auto* after = findObjectById(q, id);
                return accepted && after && runtimeObjectRenderX(*after)==x && runtimeObjectRenderY(*after)==y && runtimeDrawLevelForObject(*after)==z;
            };
            const auto stopSource=[&]() {
                facade(s,"Americans");s.selectedObjectIds={sourceId};
                const auto before=*findObjectById(s,sourceId);
                if (!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop)
                    || s.lastCommand.acceptedCount!=1) return false;
                const auto* after=findObjectById(s,sourceId);
                return after && runtimeObjectRenderX(*after)==runtimeObjectRenderX(before)
                    && runtimeObjectRenderY(*after)==runtimeObjectRenderY(before)
                    && runtimeDrawLevelForObject(*after)==runtimeDrawLevelForObject(before);
            };
            const auto fixtureHost=*findObjectById(s,hostId);
            RuntimeCellPoint hostGoal{33-directions[direction].y*8,33+directions[direction].x*8};
            if (bridge) {
                // Both actors enter the stock overlay through their real path
                // authorities. Neither onBridge nor XYZ is assigned by the test.
                // Stage the source first so it cannot block the host's long
                // path on the single-cell deck. Stop inside a real segment,
                // then let the host walk to the free cell just behind it.
                if (!move(s,sourceId,"Americans",{34,32})) return fail("real bridge source Move rejected");
                bool sourceEntered=false;
                for (int n=0;n<hz*8;++n) {
                    step(s);const auto* a=findObjectById(s,sourceId);
                    if (a && a->x==33 && a->onBridge && a->renderOnBridge && fractional(*a)
                        && runtimeObjectRenderX(*a)>32.8 && runtimeObjectRenderX(*a)<33.0) { sourceEntered=true;break; }
                }
                if (!sourceEntered) return fail("bridge source Move never supplied its real fractional deck pose: "+describe(findObjectById(s,sourceId)));
                if (!stopSource()) return fail("real bridge source Stop rejected or snapped its actual pose");
                hostGoal={32,32};
                if (!move(s,hostId,"Russians",hostGoal)) return fail("real bridge host Move rejected");
                bool entered = false;
                int sourceDeckTicks=0,hostDeckTicks=0,jointFractionalDeckTicks=0,legalRangeDeckTicks=0;
                for (int n=0; n<hz*20; ++n) {
                    step(s); const auto* a=findObjectById(s,sourceId); const auto* b=findObjectById(s,hostId);
                    const double separation=a && b ? std::hypot(runtimeObjectRenderX(*a)-runtimeObjectRenderX(*b),
                        runtimeObjectRenderY(*a)-runtimeObjectRenderY(*b)) : 0.0;
                    const bool sourceDeck=a && a->onBridge && a->renderOnBridge;
                    const bool hostDeck=b && b->onBridge && b->renderOnBridge;
                    const bool jointFractional=sourceDeck && hostDeck && fractional(*a) && fractional(*b);
                    const bool legalRange=jointFractional && runtimeWeaponDistanceToTarget(s,*a,*b,*jump)<=jump->range;
                    sourceDeckTicks+=sourceDeck?1:0;hostDeckTicks+=hostDeck?1:0;
                    jointFractionalDeckTicks+=jointFractional?1:0;legalRangeDeckTicks+=legalRange?1:0;
                    if (legalRange
                        && separation>.75 && separation<1.6) { entered=true; break; }
                }
                if (!entered) return fail("stock bridge paths never supplied both live fractional deck poses: sourceDeckTicks="
                    +std::to_string(sourceDeckTicks)+" hostDeckTicks="+std::to_string(hostDeckTicks)
                    +" jointFractionalDeckTicks="+std::to_string(jointFractionalDeckTicks)
                    +" legalRangeDeckTicks="+std::to_string(legalRangeDeckTicks)
                    +" source={"+describe(findObjectById(s,sourceId))+"} host={"+describe(findObjectById(s,hostId))+"}");
            } else if (motion == "source") {
                // A single-axis warm-up leaves the committed navigation cell
                // within the original jump range of both cardinal/diagonal
                // targets. A diagonal sidestep commits two cells away and
                // would require a new attack approach before launching.
                if (!move(s,sourceId,"Americans",{32,39}))
                    return fail("source Move rejected");
                bool moved=false;
                for (int n=0;n<hz*4;++n) { step(s); const auto* a=findObjectById(s,sourceId);
                    if (a && fractional(*a) && std::hypot(runtimeObjectRenderX(*a)-32,runtimeObjectRenderY(*a)-32)>.08) { moved=true;break; } }
                if (!moved) return fail("source Move produced no continuous fractional pose");
            } else if (motion == "host") {
                if (!move(s,hostId,"Russians",hostGoal)) return fail("host Move rejected");
                bool moved=false; const auto* initial=findObjectById(s,hostId);
                const double x=runtimeObjectRenderX(*initial),y=runtimeObjectRenderY(*initial);
                for (int n=0;n<hz*4;++n) { step(s); const auto* b=findObjectById(s,hostId);
                    if (b && fractional(*b) && std::hypot(runtimeObjectRenderX(*b)-x,runtimeObjectRenderY(*b)-y)>.08) { moved=true;break; } }
                if (!moved) return fail("host Move produced no continuous fractional pose");
            }
            if (bridge) {
                s.playerCommands.alliances=originalAlliances;
                if (runtimeOwnersAreAllied(s,"Americans","Russians"))
                    return fail("bridge warm-up failed to restore original hostile houses");
            }
            facade(s,"Americans"); s.selectedObjectIds={sourceId};
            if (motion == "source" || bridge) {
                if (!stopSource()) return fail("real source Stop rejected or snapped its actual pose");
            }
            const auto* source=findObjectById(s,sourceId); const auto* host=findObjectById(s,hostId);
            const auto* sourceArt=source?runtimeArtTypeForObject(s,*source):nullptr;
            int forward=-1,lateral=-1,flhHeight=-1; char comma1=0,comma2=0;
            std::istringstream flh(sourceArt?sourceArt->primaryFireFLH:""); flh>>forward>>comma1>>lateral>>comma2>>flhHeight;
            if (!source || !host || source->health!=dron->strength || host->health!=runtimeMaxHealthForObject(s,*host)
                || forward!=0 || lateral!=0 || flhHeight!=30 || comma1!=',' || comma2!=',')
                return fail("original full health / DRON FLH=0,0,30 oracle unavailable: source={"+describe(source)
                    +"} host={"+describe(host)+"} flh="+(sourceArt?sourceArt->primaryFireFLH:"absent"));
            if (source->owner!="Americans" || host->owner!="Russians"
                || !runtimeOrdinaryAttackRelationLegal(s,*source,*host,false)
                || !runtimeWeaponCanTargetObject(s,*jump,*host)
                || !s.specialMechanisms.pendingParasiteLaunches().empty() || !s.specialMechanisms.parasiteLinks().empty())
                return fail("original enemy target/weapon eligibility or clean pre-Primary parasite state changed");
            if (runtimeWeaponDistanceToTarget(s,*source,*host,*jump)>jump->range)
                return fail("warm-up left the original DroneJump range: source={"+describe(source)
                    +"} host={"+describe(host)+"}");
            stage="real UI Primary / uninterrupted native flight";
            if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*host),host)
                || s.lastCommand.acceptedCount!=1) return fail("real DRON Primary rejected");
            Probe original; original.name="original";
            original.initialHostX=runtimeObjectRenderX(bridge?fixtureHost:*host);
            original.initialHostY=runtimeObjectRenderY(bridge?fixtureHost:*host);
            original.reselectExpected=dron->reselectIfLimboed;
            Probe ordinaryProbe; ordinaryProbe.name="ordinary"; Probe exactProbe; exactProbe.name="canonical";
            std::unique_ptr<RuntimeWindowState> ordinary, exact;
            bool saved=false;
            const auto inspect = [&](RuntimeWindowState& q, Probe& probe, const WorldObjectInstance& preSource, const WorldObjectInstance& preHost) {
                const auto* a=findRuntimeObjectByIdIncludingInactive(q,sourceId);
                const auto* b=findRuntimeObjectByIdIncludingInactive(q,hostId);
                if (!a || !b) return fail(probe.name+" source/host identity vanished");
                probe.moved=probe.moved || std::hypot(runtimeObjectRenderX(preHost)-probe.initialHostX,runtimeObjectRenderY(preHost)-probe.initialHostY)>.04;
                const auto shot=std::find_if(q.projectiles.begin(),q.projectiles.end(),[](const auto& p){return p.attackerObjectId==sourceId && p.weaponId=="DroneJump";});
                if (shot!=q.projectiles.end()) {
                    const auto& p=*shot;
                    if (probe.actor<0) {
                        probe.actor=p.actorId;probe.launchTick=p.spawnTick;probe.launchPose=preSource;
                        const double expectedZ=runtimeDrawLevelForObject(preSource)+flhHeight*levelsPerCell/256.0;
                        if (!p.hasSourcePosition || std::abs(p.sourceX-runtimeObjectRenderX(preSource))>1e-8
                            || std::abs(p.sourceY-runtimeObjectRenderY(preSource))>1e-8 || std::abs(p.sourceZ-expectedZ)>1e-8)
                            return fail(probe.name+" launch did not retain actual source XYZ plus original FLH");
                    }
                    const double nativeSpeed=std::min(255,std::clamp(jump->speed,0,100)*256/100)/256.0*15.0/hz;
                    if (p.actorId!=probe.actor || p.projectileId!=jump->projectile || p.projectileImageId!="DRONP" || p.damage!=50
                        || p.targetObjectId!=hostId || p.owner!="Americans" || p.speedCellsPerTick<=0
                        || p.speedCellsPerTick>nativeSpeed+1e-8 || a->alive || a->health!=100 || a->mission!="ParasiteLimbo"
                        || selectionContains(q,sourceId) || !q.specialMechanisms.pendingParasiteLaunches().contains(p.actorId))
                        return fail(probe.name+" original launch identity/speed/limbo authority changed");
                    if (probe.havePrevious) {
                        const double dx=p.currentX-probe.previous.currentX,dy=p.currentY-probe.previous.currentY,dz=(p.currentZ-probe.previous.currentZ)/levelsPerCell;
                        const double travelled=std::hypot(std::hypot(dx,dy),dz);
                        if (p.elapsedTicks!=probe.previous.elapsedTicks+1 || travelled>p.speedCellsPerTick+1e-8
                            || std::abs(travelled-p.speedCellsPerTick)>1e-7
                            || std::abs(p.speedCellsPerTick-probe.previous.speedCellsPerTick)>1e-9
                            || p.sourceScreenOffsetX!=probe.previous.sourceScreenOffsetX || p.sourceScreenOffsetY!=probe.previous.sourceScreenOffsetY
                            || p.sourceNativeFixedX!=probe.previous.sourceNativeFixedX || p.sourceNativeFixedY!=probe.previous.sourceNativeFixedY)
                            return fail(probe.name+" flight failed to advance continuously at its original per-tick speed");
                        const auto actor=makeRuntimeProjectileRenderActor(p,&q.rules,&q.art);
                        if (!actor.hasFlightDirection || std::abs(actor.flightDirectionX-dx/travelled)>1e-7
                            || std::abs(actor.flightDirectionY-dy/travelled)>1e-7 || std::abs(actor.flightDirectionZ/levelsPerCell-dz/travelled)>1e-7)
                            return fail(probe.name+" DRONP direction differs from actual travel");
                    }
                    std::vector<std::uint32_t> pixels;
                    for (int tile : probe.name=="original" ? std::vector<int>{48,60,120} : std::vector<int>{60}) {
                        if (!pixelsFor(q,probe,p,tile,pixels,!probe.cold && tile==60)) return false;
                        if (tile==60) { probe.cold=true; if (output && *output && probe.name=="original") probe.captures.push_back(pixels); }
                    }
                    probe.previous=p;probe.havePrevious=true;++probe.frames;++flightFrames;
                } else if (probe.actor>=0 && !probe.contacted) {
                    const auto terminal=std::find_if(q.recentExpiredProjectiles.begin(),q.recentExpiredProjectiles.end(),[&](const auto& p){return p.actorId==probe.actor;});
                    const auto target=hostPosition(preHost);
                    if (!probe.havePrevious || terminal==q.recentExpiredProjectiles.end()) return fail(probe.name+" real terminal projectile missing");
                    const double moved=std::hypot(std::hypot(terminal->currentX-probe.previous.currentX,terminal->currentY-probe.previous.currentY),
                        (terminal->currentZ-probe.previous.currentZ)/levelsPerCell);
                    if (moved>probe.previous.speedCellsPerTick+1e-8 || std::abs(terminal->currentX-target[0])>1e-7
                        || std::abs(terminal->currentY-target[1])>1e-7 || std::abs(terminal->currentZ-target[2])>1e-7
                        || q.specialMechanisms.pendingParasiteLaunches().contains(probe.actor))
                        return fail(probe.name+" leap terminated before actual body contact or teleported its final step");
                    const auto terminalActor=makeRuntimeProjectileRenderActor(*terminal,&q.rules,&q.art);
                    if (moved>1e-8 && (!terminalActor.hasFlightDirection
                        || std::abs(terminalActor.flightDirectionX-(terminal->currentX-probe.previous.currentX)/moved)>1e-7
                        || std::abs(terminalActor.flightDirectionY-(terminal->currentY-probe.previous.currentY)/moved)>1e-7
                        || std::abs(terminalActor.flightDirectionZ-(terminal->currentZ-probe.previous.currentZ)/moved)>1e-7))
                        return fail(probe.name+" terminal DRONP lost its actual last-step heading");
                    probe.contacted=true;probe.contactTick=q.tick;
                    positions<<label<<','<<probe.name<<','<<q.tick<<','<<probe.actor<<",contact,"<<terminal->currentX<<','<<terminal->currentY<<','<<terminal->currentZ
                        <<','<<target[0]<<','<<target[1]<<','<<target[2]<<','<<terminal->speedCellsPerTick<<",-1,0\n";
                    if (targetType=="HTNK") {
                        if (!b->alive || !q.specialMechanisms.parasiteLinks().contains(hostId)) return fail(probe.name+" natural HTNK attachment missing");
                        const auto& link=q.specialMechanisms.parasiteLinks().at(hostId);
                        if (link.sourceObjectId!=sourceId || link.damage!=50 || link.damageIntervalTicks!=hz*4)
                            return fail(probe.name+" original parasite damage/timing changed");
                        probe.nextBite=link.nextDamageTick;
                    }
                }
                if (probe.contacted && targetType=="HTNK" && b->alive && q.specialMechanisms.parasiteLinks().contains(hostId)) {
                    const auto& link=q.specialMechanisms.parasiteLinks().at(hostId);
                    if (b->health<preHost.health && preHost.health-b->health!=50) return fail(probe.name+" native bite damage budget changed");
                    if (link.nextDamageTick>probe.nextBite) {
                        if (q.tick!=probe.nextBite || link.nextDamageTick!=probe.nextBite+hz*4) return fail(probe.name+" native bite clock changed");
                        probe.nextBite=link.nextDamageTick;++probe.bites;
                    }
                }
                if (probe.contacted && !b->alive && !probe.exited) {
                    probe.deathTick=q.tick;probe.exitPose=preHost;probe.exited=true;
                    const bool selected=selectionContains(q,sourceId);
                    const auto feet=hostPosition(preHost);
                    // Bridge actors must both have walked to fractional deck
                    // poses before Primary. The infantry may finish that short
                    // segment while the source turns; ordinary moving-infantry
                    // scenes separately require fractional contact/death.
                    const bool requireMovingDeath=(motion=="host" || bridge)
                        && (targetType=="HTNK" || !bridge);
                    if (requireMovingDeath && (!fractional(preHost)
                        || (targetType=="HTNK" && (!probe.hostMoveResumed || !preHost.renderMoveActive))))
                        return fail(probe.name+" natural moving-host death lacked an active fractional pose: "+describe(&preHost));
                    if (!a->alive || a->health!=100 || a->x!=preHost.x || a->y!=preHost.y || a->level!=preHost.level || a->onBridge!=preHost.onBridge
                        || std::abs(runtimeObjectRenderX(*a)-feet[0])>1e-8
                        || std::abs(runtimeObjectRenderY(*a)-feet[1])>1e-8
                        || std::abs(runtimeDrawLevelForObject(*a)-feet[2])>1e-8
                        || selected!=probe.reselectExpected || q.parasiteReselectAfterLimboObjectIds.contains(sourceId)
                        || !q.specialMechanisms.parasiteLinks().empty() || !q.specialMechanisms.pendingParasiteLaunches().empty()
                        || q.specialMechanisms.isParasiteSourceLimboed(sourceId) || q.behaviors.contains(sourceId))
                        return fail(probe.name+" natural death failed exact victim-cell/XYZ/bridge replacement or selection cleanup: victim={"
                            +describe(&preHost)+"} source={"+describe(a)+"}");
                    positions<<label<<','<<probe.name<<','<<q.tick<<','<<probe.actor<<",exit,"<<runtimeObjectRenderX(*a)<<','<<runtimeObjectRenderY(*a)<<','<<runtimeDrawLevelForObject(*a)
                        <<','<<feet[0]<<','<<feet[1]<<','<<feet[2]<<",0,-1,0\n";
                }
                return q.rules.getDetailRulesHash()==rulesHash || fail("stock rule hash changed");
            };
            const auto maintainHostRoute = [&](RuntimeWindowState& q, Probe& probe) {
                if (targetType!="HTNK" || (motion!="host" && !bridge) || !probe.contacted || probe.exited) return true;
                const auto* moving=findObjectById(q,hostId);
                const auto relation=q.specialMechanisms.parasiteLinks().find(hostId);
                if (!moving || relation==q.specialMechanisms.parasiteLinks().end()) return fail(probe.name+" attached moving host disappeared");
                const auto& link=relation->second;
                const bool lethal= moving->health<=50 || (link.culling && link.cullingThresholdPerMyriad>0
                    && static_cast<long long>(moving->health)*10000<=static_cast<long long>(link.targetMaximumHealth)*link.cullingThresholdPerMyriad);
                const bool prepareLethal=lethal && !probe.lethalRoutePrepared && link.nextDamageTick-q.tick<=hz*3;
                const bool nearGoal=probe.hostMoveResumed
                    && std::hypot(runtimeObjectRenderX(*moving)-probe.hostRouteGoal.x,runtimeObjectRenderY(*moving)-probe.hostRouteGoal.y)<1.8;
                if (!probe.hostMoveResumed || nearGoal || prepareLethal) {
                    // Native attachment stops the preceding Move. Resume via
                    // the real UI, then turn before arrival; the last native
                    // bite observes an unfinished physical walking segment.
                    probe.hostRouteGoal=bridge ? RuntimeCellPoint{runtimeObjectRenderX(*moving)<=39?47:31,32}
                        : RuntimeCellPoint{runtimeObjectRenderX(*moving)<=36?48:24,moving->y};
                    if (!move(q,hostId,"Russians",probe.hostRouteGoal)) return fail(probe.name+" attached host real Move rejected or changed its continuous pose");
                    facade(q,"Americans");q.selectedObjectIds.clear();
                    probe.hostMoveResumed=true;++probe.hostMoveCommands;
                    if (prepareLethal) probe.lethalRoutePrepared=true;
                }
                return true;
            };
            const auto compare = [&](RuntimeWindowState& copy, const std::string& replica) {
                for (int id : {sourceId,hostId}) {
                    const auto* a=findRuntimeObjectByIdIncludingInactive(s,id); const auto* b=findRuntimeObjectByIdIncludingInactive(copy,id);
                    if (!a || !b || a->alive!=b->alive || a->health!=b->health || a->x!=b->x || a->y!=b->y || a->level!=b->level || a->onBridge!=b->onBridge
                        || runtimeObjectRenderX(*a)!=runtimeObjectRenderX(*b) || runtimeObjectRenderY(*a)!=runtimeObjectRenderY(*b)
                        || runtimeDrawLevelForObject(*a)!=runtimeDrawLevelForObject(*b))
                        return fail(replica+" source/host authority diverged: actual={"+describe(a)+"} copy={"+describe(b)+"}");
                }
                const auto nativeShots=[](const RuntimeWindowState& q) {
                    std::vector<std::string> records;
                    for (const auto& p:q.projectiles) if (p.attackerObjectId==sourceId && p.weaponId=="DroneJump")
                        records.push_back(serializeRuntimeProjectileEffect(p));
                    return records;
                };
                if (s.tick!=copy.tick || nativeShots(s)!=nativeShots(copy)
                    || s.specialMechanisms.stateHash()!=copy.specialMechanisms.stateHash())
                    return fail(replica+" native parasite clock/relationship/selection restore diverged");
                return true;
            };
            for (int n=0;n<hz*45;++n) {
                const auto preSource=*findRuntimeObjectByIdIncludingInactive(s,sourceId),preHost=*findRuntimeObjectByIdIncludingInactive(s,hostId);
                step(s); if (!inspect(s,original,preSource,preHost) || !maintainHostRoute(s,original)) return false;
                for (auto pair : {std::pair{ordinary.get(),&ordinaryProbe},std::pair{exact.get(),&exactProbe}}) if (pair.first) {
                    const auto a=*findRuntimeObjectByIdIncludingInactive(*pair.first,sourceId),b=*findRuntimeObjectByIdIncludingInactive(*pair.first,hostId);
                    step(*pair.first);++continuedTicks;
                    if (!inspect(*pair.first,*pair.second,a,b) || !maintainHostRoute(*pair.first,*pair.second)
                        || !compare(*pair.first,pair.second->name)) return false;
                }
                if (save && !saved && original.havePrevious && original.previous.elapsedTicks>=1
                    && !original.contacted && std::hypot(original.previous.targetX-original.previous.currentX,original.previous.targetY-original.previous.currentY)>.35) {
                    stage="ordinary / canonical in-flight restore and continuation";
                    ordinary=checkpoint(s,false);exact=checkpoint(s,true);
                    if (!ordinary || !exact || !compare(*ordinary,"ordinary") || !compare(*exact,"canonical")) return fail("in-flight checkpoint: "+error);
                    const auto initializeProbe=[&](Probe& probe) {
                        probe.actor=original.actor;probe.launchTick=original.launchTick;probe.launchPose=original.launchPose;
                        probe.previous=original.previous;probe.havePrevious=true;probe.initialHostX=original.initialHostX;probe.initialHostY=original.initialHostY;
                    };
                    initializeProbe(ordinaryProbe);initializeProbe(exactProbe);
                    // The loader deliberately resets local selection. Native
                    // source/host/shot authority is persisted; deferred local
                    // reselect follows each replica's actual UI context.
                    ordinaryProbe.reselectExpected=ordinary->parasiteReselectAfterLimboObjectIds.contains(sourceId);
                    exactProbe.reselectExpected=exact->parasiteReselectAfterLimboObjectIds.contains(sourceId);
                    if (motion=="host" || bridge) {
                        for (auto* q : {&s,ordinary.get(),exact.get()}) {
                            if (!move(*q,hostId,"Russians",hostGoal)) return fail("continued real host Move rejected or snapped after restore");
                            facade(*q,"Americans");q->selectedObjectIds.clear();
                        }
                    }
                    for (auto pair : {std::pair{ordinary.get(),&ordinaryProbe},std::pair{exact.get(),&exactProbe}}) {
                        std::vector<std::uint32_t> originalPixels,copyPixels;
                        if (!pixelsFor(s,original,original.previous,60,originalPixels,false)
                            || !pixelsFor(*pair.first,*pair.second,pair.second->previous,60,copyPixels,true) || originalPixels!=copyPixels)
                            return fail("restored native DRONP pixels differ at checkpoint");
                        pair.second->cold=true;
                    }
                    saved=true;++saves;++snapshots;
                }
                if (original.exited && (!ordinary || ordinaryProbe.exited) && (!exact || exactProbe.exited)) break;
            }
            stage="natural completion / preserved original rules";
            const auto remainingShots=std::count_if(s.projectiles.begin(),s.projectiles.end(),[](const auto& p){
                return p.attackerObjectId==sourceId && p.weaponId=="DroneJump";
            });
            if (!original.contacted || !original.exited || original.frames<2 || (save && !saved)
                || ((motion=="source" || bridge) && !fractional(original.launchPose))
                || ((motion=="host" || bridge) && !original.moved)
                || (bridge && (!original.launchPose.onBridge || !original.exitPose.onBridge))
                || (targetType=="HTNK" && original.bites<1)
                || remainingShots>0 || s.rules.getDetailRulesHash()!=rulesHash) {
                std::ostringstream coverage;
                coverage<<"missing native flight/real movement/save/natural death coverage: contacted="<<original.contacted
                    <<" exited="<<original.exited<<" frames="<<original.frames<<" saveRequired="<<save<<" saved="<<saved
                    <<" launchFractional="<<fractional(original.launchPose)<<" moved="<<original.moved
                    <<" launchBridge="<<original.launchPose.onBridge<<" exitBridge="<<original.exitPose.onBridge
                    <<" bites="<<original.bites<<" remainingShots="<<remainingShots
                    <<" rulesSame="<<(s.rules.getDetailRulesHash()==rulesHash)
                    <<" launch={"<<describe(&original.launchPose)<<"} exit={"<<describe(&original.exitPose)<<'}';
                return fail(coverage.str());
            }
            const auto* returned=findObjectById(s,sourceId);
            if (save) {
                stage="ordinary / canonical natural-exit pose roundtrip";
                const auto expectedFeet=hostPosition(original.exitPose);
                for (bool canonical : {false,true}) {
                    auto restored=checkpoint(s,canonical);
                    if (!restored) return fail("post-exit checkpoint: "+error);
                    const auto* copy=findObjectById(*restored,sourceId);
                    if (!copy || !copy->alive || copy->health!=100 || copy->x!=returned->x || copy->y!=returned->y
                        || copy->level!=returned->level || copy->onBridge!=returned->onBridge
                        || std::abs(runtimeObjectRenderX(*copy)-expectedFeet[0])>1e-8
                        || std::abs(runtimeObjectRenderY(*copy)-expectedFeet[1])>1e-8
                        || std::abs(runtimeDrawLevelForObject(*copy)-expectedFeet[2])>1e-8
                        || restored->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash()
                        || !restored->specialMechanisms.pendingParasiteLaunches().empty() || !restored->specialMechanisms.parasiteLinks().empty()
                        || restored->specialMechanisms.isParasiteSourceLimboed(sourceId)
                        || restored->parasiteReselectAfterLimboObjectIds.contains(sourceId))
                        return fail("post-exit codec changed actual source feet XYZ/level/bridge or parasite cleanup");
                    if (canonical) ++postExitSnapshots; else ++postExitSaves;
                }
            }
            if (targetType=="HTNK" && (motion=="host" || bridge)) ++fractionalDeathCases;
            cases<<label<<','<<hz<<','<<original.launchTick<<','<<original.contactTick<<','<<original.deathTick<<','<<original.frames<<','
                <<fractional(original.launchPose)<<','<<original.moved<<','<<runtimeObjectRenderX(*returned)<<','<<runtimeObjectRenderY(*returned)<<','
                <<runtimeDrawLevelForObject(*returned)<<','<<returned->onBridge<<",PASS\n";
            if (!original.captures.empty()) {
                constexpr int columns=8;const int rows=static_cast<int>((original.captures.size()+columns-1)/columns);
                std::vector<std::uint32_t> sheet(width*columns*height*rows,background);
                for (std::size_t i=0;i<original.captures.size();++i) for (int y=0;y<height;++y)
                    std::copy_n(original.captures[i].begin()+y*width,width,sheet.begin()+((i/columns)*height+y)*width*columns+(i%columns)*width);
                if (!writeRuntimeSmokeBmp(pathString(root/(label+"-native-flight.bmp")),sheet.data(),width*columns,height*rows,error)) return fail("native DRONP filmstrip write");
                ++captures;
                const auto drawn=drawRuntimeStateOffscreenAtCell(s,{returned->x,returned->y},640,480,error,
                    pathString(root/(label+"-natural-exit.bmp")),false);
                if (!drawn || !drawn->rendered) return fail("natural exit native scene image: "+error);
                ++captures;
            }
            if (bridge) ++bridgeCases;
            std::cout<<"Parasite leap/exit "<<label<<" PASS launch="<<original.launchTick<<" contact="<<original.contactTick<<" death="<<original.deathTick
                <<" nativeFrames="<<original.frames<<" sourceFractional="<<fractional(original.launchPose)<<" hostMoved="<<original.moved
                <<" fractionalDeath="<<fractional(original.exitPose)<<" attachedUiMoves="<<original.hostMoveCommands
                <<" warmAllianceRestored="<<(s.playerCommands.alliances==originalAlliances)<<'\n';
            return true;
        }();
        if (ok) ++passed; else { failures.push_back(error);std::cout<<error<<'\n';error.clear(); }
    };
    for (int hz : {30,60}) for (const std::string type : {"HTNK","E1"}) for (int direction=0;direction<8;++direction)
        run(type,hz,direction,"static",false,0,false);
    for (int hz : {30,60}) for (const std::string type : {"HTNK","E1"}) {
        run(type,hz,0,"source",false,0,true);
        run(type,hz,2,"host",false,0,true);
        run(type,hz,0,"host",true,0,true);
        run(type,hz,1,"source",false,3,true);
    }
    if (!attempted) failures.push_back("Parasite leap/exit: focus did not match a real case");
    if (!(focus && *focus) && (directionFrames.size()<8 || bridgeCases!=4 || saves!=16 || snapshots!=16
        || postExitSaves!=16 || postExitSnapshots!=16 || fractionalDeathCases!=4)) {
        std::ostringstream coverage;
        coverage<<"Parasite leap/exit: missing all-direction/native bridge/save matrix coverage: directions="<<directionFrames.size()
            <<" bridgeCases="<<bridgeCases<<" saves="<<saves<<" snapshots="<<snapshots
            <<" postExitSaves="<<postExitSaves<<" postExitSnapshots="<<postExitSnapshots
            <<" fractionalDeathCases="<<fractionalDeathCases;
        failures.push_back(coverage.str());
    }
    positions.close();cases.close();
    if (!positions.good() || !cases.good()) failures.push_back("Parasite leap/exit: native evidence report write failed");
    if (!failures.empty()) {
        std::ostringstream result;result<<failures.size()<<" parasite leap/exit scene(s) failed:";
        for (const auto& failure : failures) result<<'\n'<<failure;error=result.str();return false;
    }
    std::cout<<"Parasite leap/exit passed: cases="<<passed<<" realTravelFrames="<<flightFrames<<" nativePixelChecks="<<nativeChecks
        <<" coldChecks="<<coldChecks<<" nativeDirections="<<directionFrames.size()<<" ordinarySaves="<<saves<<" canonicalSnapshots="<<snapshots
        <<" postExitSaves="<<postExitSaves<<" postExitSnapshots="<<postExitSnapshots<<" fractionalHostDeaths="<<fractionalDeathCases
        <<" continuedTicks="<<continuedTicks<<" bridgeCases="<<bridgeCases<<" nativeCaptures="<<captures
        <<" UI/originalHealthSpeedFLH/actualContact/victimXYZ/selection/relationshipCleanup=ok\n";
    error.clear();return true;
}
