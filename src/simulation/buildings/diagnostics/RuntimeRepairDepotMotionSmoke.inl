// Original service orders, full simulation ticks, and an independent direction
// oracle exercise the fractional repair-pad approach and natural departure.
bool runRuntimeRepairDepotMotionSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile stock, authoredArt; RulesDatabase rules; ArtDatabase art;
    if (!stock.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(stock)
        || !authoredArt.loadFromFile(live.report.artPath) || !art.loadFromIni(authoredArt)) {
        error = "Repair motion: original rules/art unavailable"; return false;
    }
    const char* output = std::getenv("CONCORD_REPAIR_DEPOT_MOTION_OUTPUT");
    const std::string outputRoot = output ? output : "";
    if (!outputRoot.empty()) std::filesystem::create_directories(outputRoot);
    const char* requested = std::getenv("CONCORD_REPAIR_DEPOT_MOTION_CASE");
    const std::string filter = requested ? requested : "";
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, services = 0, motionChecks = 0, ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, captures = 0;
    const auto fail = [&](const std::string& why) {
        error = "Repair motion " + label + " [" + stage + "]: " + why; return false;
    };
    const auto scene = [&](const std::string& name, const auto& body) {
        if (!filter.empty() && filter != name) return;
        label = name; stage = "setup"; fixtureFailure.clear();
        bool passed = body();
        if (!fixtureFailure.empty()) { fail(fixtureFailure); passed = false; }
        if (passed) { ++scenes; std::cout << "Repair motion " << name << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    int landTile = -1;
    const auto terrainMetadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, terrainMetadata, live.renderCache) == 0) {
            landTile = tile; break;
        }
    }
    if (landTile < 0) { error = "Repair motion: original flat land TMP unavailable"; return false; }
    const auto add = [&](RuntimeWindowState& s, int id, const std::string& type, WorldObjectKind kind,
            RuntimeCellPoint cell, const std::string& owner = "Commander") {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "RepairMotion" + std::to_string(id);
        object.type = type; object.kind = kind; object.owner = owner; object.x = cell.x; object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y); object.alive = object.poweredOperational = true;
        object.health = runtimeMaxHealthForObject(s, object); object.mission = "Sleep";
        object.facing = object.turretFacing = runtimeFacingFromDelta(1, 0);
        initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
        applyRuntimeInitialObjectFlightState(s.rules, object, true);
        runtimeInitializeModExtensions(object, s.rules, s.config.fixedTickHz);
        s.world.accessObjects().push_back(std::move(object));
    };
    const auto make = [&](const std::string& depot, const std::string& vehicle, int hz, bool two = false, bool blocked = false) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.playerCountry = depot == "GADEPT" ? "Americans" : "Russians";
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.noCooldown = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false; s.config.playerSlots.clear();
        s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 55; y <= 100; ++y) for (int x = 55; x <= 100; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        add(s, 1, depot, WorldObjectKind::Structure, {75, 75});
        add(s, 2, vehicle, WorldObjectKind::Unit, {70, 77});
        if (two) add(s, 3, vehicle == "MTNK" ? "HTNK" : "MTNK", WorldObjectKind::Unit, {69, 79});
        if (blocked) add(s, 4, "MTNK", WorldObjectKind::Unit, {75, 78});
        add(s, 100, depot == "GADEPT" ? "GAPOWR" : "NAPOWR", WorldObjectKind::Structure, {92, 92});
        add(s, 998, "NANRCT", WorldObjectKind::Structure, {57, 57}, "SkirmishPlayer2");
        for (int id : two ? std::vector<int>{2, 3} : std::vector<int>{2}) {
            auto* object = findMutableRuntimeObjectByIdIncludingInactive(s, id);
            object->health = std::max(1, runtimeMaxHealthForObject(s, *object) - 64);
        }
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 10000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Commander"; local.country = s.config.playerCountry; local.color = "Gold";
        RuntimeConfig::PlayerSlot hostile; hostile.enabled = true; hostile.country = "Russians"; hostile.color = "Red";
        s.config.playerSlots = {local, hostile};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["commander"] = s.config.playerCountry;
        s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house;
        house.owner = "Commander"; house.canonicalOwner = "commander"; house.country = s.config.playerCountry;
        house.localFacade = true; s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeRepairDepotsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        rebuildRuntimeEconomyPower(s); s.selectedObjectIds = {2}; return storage;
    };
    struct Pose {
        double x = 0, y = 0, z = 0; int facing = -1, health = 0, cellX = 0, cellY = 0, dock = -1, slot = -1;
        bool rendered = false, moving = false;
    };
    const auto pose = [&](const RuntimeWindowState& s, int id) {
        Pose result;
        if (const auto* object = findObjectById(s, id)) {
            result.x = object->hasRenderPosition ? object->renderX : double(object->x);
            result.y = object->hasRenderPosition ? object->renderY : double(object->y);
            result.z = object->hasRenderPosition ? object->renderLevel : double(object->level);
            result.facing = object->facing; result.health = object->health;
            result.cellX = object->x; result.cellY = object->y; result.dock = object->dockedToObjectId;
            result.slot = object->dockIndex; result.rendered = object->hasRenderPosition; result.moving = object->renderMoveActive;
        }
        return result;
    };
    const auto diagnostic = [&](const RuntimeWindowState& s, int id) {
        const auto p = pose(s, id); std::ostringstream out; out << std::setprecision(17) << "tick=" << s.tick
            << " id=" << id << " xyz=" << p.x << ',' << p.y << ',' << p.z << " cell=" << p.cellX << ',' << p.cellY
            << " facing=" << p.facing << " hp=" << p.health << " render=" << p.rendered << " moving=" << p.moving
            << " dock=" << p.dock << ':' << p.slot << " repairTimer=" << s.repairDepots.remainingServiceTicks(1);
        if (const auto* relation = findRuntimeRepairDepotRelationForUnit(s.repairDepots, id))
            out << " relation=" << int(relation->phase) << " dockCell=" << relation->dockCell.x << ',' << relation->dockCell.y;
        if (const auto it = s.behaviors.find(id); it != s.behaviors.end()) {
            const auto& b = it->second;
            out << " behavior=" << int(b.kind) << ':' << int(b.state) << " target=" << b.moveTarget.x << ',' << b.moveTarget.y
                << " path=" << b.path.size() << " cooldown=" << b.moveCooldownTicks
                << " segmentTicks=" << b.renderMoveTicksRemaining << '/' << b.renderMoveTicksTotal
                << " from=" << b.renderFrom.x + b.renderFromOffsetX << ',' << b.renderFrom.y + b.renderFromOffsetY
                << " to=" << b.renderTo.x + b.renderToOffsetX << ',' << b.renderTo.y + b.renderToOffsetY;
        }
        return out.str();
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended unexpectedly: " + s.skirmishResult;
    };
    const auto facingDifference = [](int a, int b) { const int d = std::abs(a - b) % 256; return std::min(d, 256 - d); };
    const auto actualFacing = [](double dx, double dy) {
        // Dir256 is clockwise from screen north. This uses observed body
        // displacement, never the implementation's planned integer segment.
        constexpr double pi = 3.14159265358979323846;
        double angle = std::atan2(dx - dy, -(dx + dy));
        if (angle < 0) angle += 2 * pi;
        return int(std::lround(angle * 256 / (2 * pi))) % 256;
    };
    const auto inspectMotion = [&](const RuntimeWindowState& s, int id, const Pose& previous, const Pose& current,
            const RuntimeObjectBehavior* previousMove = nullptr) {
        const auto* object = findObjectById(s, id); const auto* type = object ? findRuntimeUnitType(s.rules, *object) : nullptr;
        if (!object || !type || !object->alive || type->speed <= 0) return fail("real vehicle disappeared: " + diagnostic(s, id));
        const double dx = current.x - previous.x, dy = current.y - previous.y, distance = std::hypot(dx, dy);
        const double baseInterval = 256.0 * s.config.fixedTickHz / (15.0 * std::min(255, type->speed * 256 / 100));
        // Native per-segment whole-tick quantization can advance a short
        // fractional segment slightly faster than its exact rule interval.
        const double maxStep = 1.25 / std::max(1.0, std::floor(baseInterval)) + 1.0e-6;
        if (!std::isfinite(current.x) || !std::isfinite(current.y) || !std::isfinite(current.z)
            || distance > maxStep || std::abs(current.z - previous.z) > 1.0e-6)
            return fail("body snapped/accelerated: distance=" + std::to_string(distance) + " limit=" + std::to_string(maxStep)
                + " previousXYZ=" + std::to_string(previous.x) + ',' + std::to_string(previous.y) + " " + diagnostic(s, id));
        // Completing a committed segment recursively samples the next turn
        // in the same full tick. Its displacement still belongs to the old
        // segment, whose visible heading is the preceding frame's heading.
        const bool completedSegment = previousMove && previous.moving && previousMove->moveCooldownTicks == 1
            && previousMove->renderMoveTicksRemaining == 1;
        const int displacementFacing = completedSegment ? previous.facing : current.facing;
        if (distance > 1.0e-8 && facingDifference(displacementFacing, actualFacing(dx, dy)) > 1)
            return fail("body moved sideways: actualDirection=" + std::to_string(actualFacing(dx, dy))
                + " displacement=" + std::to_string(dx) + ',' + std::to_string(dy) + " " + diagnostic(s, id));
        if (completedSegment && distance > 1.0e-8) {
            const double oldDx = previousMove->renderTo.x + previousMove->renderToOffsetX
                - previousMove->renderFrom.x - previousMove->renderFromOffsetX;
            const double oldDy = previousMove->renderTo.y + previousMove->renderToOffsetY
                - previousMove->renderFrom.y - previousMove->renderFromOffsetY;
            if (facingDifference(actualFacing(dx, dy), actualFacing(oldDx, oldDy)) > 1)
                return fail("completed movement changed its committed direction: " + diagnostic(s, id));
        }
        if (previous.facing >= 0 && current.facing >= 0
            && facingDifference(previous.facing, current.facing) > std::max(1, 2 * type->rot + 1))
            return fail("body facing jumped past original ROT clock: " + diagnostic(s, id));
        if (const auto move = s.behaviors.find(id); move != s.behaviors.end()) {
            const auto& b = move->second;
            if (b.renderMoveTicksRemaining > 0 && b.renderArrivalDistanceAfter < 0
                && b.renderFromOffsetX == 0 && b.renderFromOffsetY == 0 && b.renderToOffsetX == 0 && b.renderToOffsetY == 0) {
                const double multiplier = runtimeTerrainMovementSpeedMultiplier(s, *object, b.renderFrom, b.renderTo);
                const double interval = baseInterval * std::max(1.0, std::hypot(double(b.renderTo.x - b.renderFrom.x),
                    double(b.renderTo.y - b.renderFrom.y))) / std::max(0.000001, multiplier);
                if (b.renderMoveTicksTotal < std::floor(interval) - 1 || b.renderMoveTicksTotal > std::ceil(interval) + 1)
                    return fail("original cruise Speed clock was reduced or accelerated: " + diagnostic(s, id));
            }
        }
        ++motionChecks; return true;
    };
    const auto orderRepair = [&](RuntimeWindowState& s, int id) {
        RuntimeSnapshotActiveStateScope active(s); s.selectedObjectIds = {id};
        const auto before = pose(s, id);
        const auto* host = findObjectById(s, 1);
        if (!host || !dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*host), host) || s.lastCommand.verb != "unitRepair" || s.lastCommand.acceptedCount != 1)
            return fail("real contextual service order rejected: " + s.interactionStatus);
        const auto after = pose(s, id);
        if (before.x != after.x || before.y != after.y || before.z != after.z || before.facing != after.facing)
            return fail("service command changed continuous pose before a tick: " + diagnostic(s, id));
        return true;
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream text, full, canonical; IniFile ordinary, snapshot;
        if (!writeRuntimeStateToStream(s, text, error) || !ordinary.loadFromString(text.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, full, error) || !writeRuntimeMultiplayerSnapshotExtension(s, full, error)
            || !snapshot.loadFromString(full.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot, canonical, error)
            || !snapshot.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
        // Canonical extension intentionally excludes v14 audiovisual authority.
        // Restore the complete ordinary base before applying that extension.
        if (!materializeRuntimeLoadStateFromIni(*restored, ordinary, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*restored, snapshot, error)))
            return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*restored); return restored;
    };
    const auto compare = [&](const RuntimeWindowState& s, const RuntimeWindowState& other, int id, bool exact) {
        const auto a = pose(s, id), b = pose(other, id);
        if (s.tick != other.tick || a.x != b.x || a.y != b.y || a.z != b.z || a.facing != b.facing
            || a.health != b.health || a.cellX != b.cellX || a.cellY != b.cellY || a.dock != b.dock || a.slot != b.slot
            || a.rendered != b.rendered || a.moving != b.moving || s.economy.credits != other.economy.credits
            || s.repairDepots.stateHash() != other.repairDepots.stateHash())
            return fail(std::string(exact ? "canonical" : "ordinary") + " saved service/pose diverged original=["
                + diagnostic(s, id) + "] restored=[" + diagnostic(other, id) + ']');
        const auto first = s.behaviors.find(id), second = other.behaviors.find(id);
        if (exact && ((first == s.behaviors.end()) != (second == other.behaviors.end()))) return fail("canonical behavior presence changed");
        if (first != s.behaviors.end() && second != other.behaviors.end()) {
            const auto& x = first->second; const auto& y = second->second;
            if (x.kind != y.kind || x.moveTarget != y.moveTarget || x.moveCooldownTicks != y.moveCooldownTicks
                || x.renderMoveTicksTotal != y.renderMoveTicksTotal || x.renderMoveTicksRemaining != y.renderMoveTicksRemaining
                || x.renderFrom != y.renderFrom || x.renderTo != y.renderTo
                || x.renderFromOffsetX != y.renderFromOffsetX || x.renderFromOffsetY != y.renderFromOffsetY
                || x.renderToOffsetX != y.renderToOffsetX || x.renderToOffsetY != y.renderToOffsetY
                || (exact && (x.state != y.state || x.path != y.path)))
                return fail("saved committed movement segment restarted: original=[" + diagnostic(s, id)
                    + "] restored=[" + diagnostic(other, id) + ']');
        }
        return true;
    };
    const auto capture = [&](RuntimeWindowState& s, const std::string& suffix) {
        if (outputRoot.empty()) return true;
        RuntimeSnapshotActiveStateScope active(s);
        const auto image = drawRuntimeStateOffscreenAtCell(s, {77, 76}, 800, 600, error,
            outputRoot + '/' + label + '-' + suffix + ".bmp", false);
        if (!image || !image->rendered || image->visibleVoxelBodies < 1) return fail("native vehicle draw missing: " + error);
        ++captures; return true;
    };
    const auto run = [&](const std::string& depot, const std::string& vehicle, int hz, bool moving, bool queued, bool blocked = false) {
        auto box = make(depot, vehicle, hz, queued, blocked); auto& s = *box;
        RuntimeSnapshotActiveStateScope active(s);
        const std::vector<int> ids = queued ? std::vector<int>{2, 3} : std::vector<int>{2};
        const auto* sourceType = rules.findUnitType("Vehicle", vehicle);
        const auto* hostType = rules.findBuildingType(depot);
        if (!sourceType || !hostType || !hostType->unitRepair || sourceType->speed != std::stoi(stock.getValue(vehicle, "Speed", "0"))
            || sourceType->rot != std::stoi(stock.getValue(vehicle, "ROT", "0"))) return fail("original Speed/ROT/service rules missing");
        if (!findObjectById(s, 1)->poweredOperational) return fail("actual power grid did not enable repair depot");
        if (moving) {
            stage = "real moving-order interruption";
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, {73, 78}))
                return fail("initial actual Move rejected");
            bool fractional = false;
            for (int i = 0; i < hz * 15; ++i) {
                const auto before = pose(s, 2); std::optional<RuntimeObjectBehavior> oldMove;
                if (const auto it = s.behaviors.find(2); it != s.behaviors.end()) oldMove = it->second;
                step(s);
                if (!inspectMotion(s, 2, before, pose(s, 2), oldMove ? &*oldMove : nullptr)) return false;
                const auto it = s.behaviors.find(2);
                if (it != s.behaviors.end() && it->second.renderMoveTicksRemaining > 2
                    && it->second.renderMoveTicksRemaining < it->second.renderMoveTicksTotal) { fractional = true; break; }
            }
            if (!fractional) return fail("initial stock movement never reached a continuous segment");
        }
        stage = "actual contextual service orders";
        for (int id : ids) if (!orderRepair(s, id)) return false;
        if (queued) {
            const auto* second = findRuntimeRepairDepotRelationForUnit(s.repairDepots, 3);
            if (!second || second->phase != RuntimeRepairDepotRelationPhase::Queued) return fail("second real vehicle did not enter repair queue");
        }
        struct Progress { bool docked = false, healed = false, released = false, departed = false; int maxHealth = 0; RuntimeCellPoint rally; };
        std::map<int, Progress> progress;
        for (int id : ids) progress[id].maxHealth = runtimeMaxHealthForObject(s, *findObjectById(s, id));
        struct Probe { std::unique_ptr<RuntimeWindowState> state; bool exact = false; int expires = 0; };
        std::vector<Probe> probes; std::array<bool, 3> saved{};
        const bool persistence = vehicle == "MTNK" && !moving && !queued && !blocked;
        const auto blockerPose = blocked ? pose(s, 4) : Pose{};
        std::ofstream csv;
        if (!outputRoot.empty()) { csv.open(outputRoot + '/' + label + ".csv"); csv << "tick,id,x,y,z,facing,hp,dock,phase,dx,dy\n"; }
        bool capturedEntry = false, capturedExit = false; int completedAt = -1;
        for (int n = 0; n < hz * (queued ? 120 : 90); ++n) {
            stage = "full approach, original paid repair, and natural rally departure";
            std::map<int, Pose> before;
            std::map<int, RuntimeObjectBehavior> previousMoves;
            for (int id : ids) {
                before[id] = pose(s, id);
                if (const auto move = s.behaviors.find(id); move != s.behaviors.end()) previousMoves.emplace(id, move->second);
            }
            step(s);
            for (int id : ids) {
                const auto current = pose(s, id);
                const auto old = previousMoves.find(id);
                if (!inspectMotion(s, id, before[id], current, old != previousMoves.end() ? &old->second : nullptr)) return false;
                auto& p = progress[id]; const auto* actor = findObjectById(s, id);
                const auto* relation = findRuntimeRepairDepotRelationForUnit(s.repairDepots, id);
                if (csv) csv << std::setprecision(17) << s.tick << ',' << id << ',' << current.x << ',' << current.y << ',' << current.z
                    << ',' << current.facing << ',' << current.health << ',' << current.dock << ',' << (relation ? int(relation->phase) : -1)
                    << ',' << current.x - before[id].x << ',' << current.y - before[id].y << '\n';
                if (current.dock == 1) {
                    p.docked = true;
                    const auto dock = runtimeAircraftDockSpawn(s, *findObjectById(s, 1), current.slot);
                    if (current.x != dock.renderX || current.y != dock.renderY || current.moving || !relation
                        || relation->phase != RuntimeRepairDepotRelationPhase::Docked) return fail("real attachment lost authored stationary dock pose");
                }
                if (current.health > before[id].health) {
                    p.healed = true;
                    if (!p.docked || current.health > p.maxHealth || before[id].dock != 1)
                        return fail("repair pulse occurred outside actual docking or exceeded stock health cap");
                }
                if (p.docked && !p.released && current.dock < 0) {
                    p.released = true;
                    const auto move = s.behaviors.find(id);
                    if (current.health != p.maxHealth || relation || move == s.behaviors.end() || move->second.kind != RuntimeBehaviorKind::Move)
                        return fail("repair did not naturally complete into actual rally movement: " + diagnostic(s, id));
                    p.rally = move->second.moveTarget;
                    // Independently transcribed retail FoundationOutside
                    // order begins (0,3), then (1,3), for both stock depots.
                    // A queued vehicle may legally occupy either candidate.
                    const RuntimeCellPoint nativeRally{75 + (blocked ? 1 : 0), 78};
                    if (!queued && p.rally != nativeRally)
                        return fail("stock default departure ignored native FoundationOutside order: actual="
                            + std::to_string(p.rally.x) + ',' + std::to_string(p.rally.y)
                            + " expected=" + std::to_string(nativeRally.x) + ',' + std::to_string(nativeRally.y));
                }
                if (p.released && !current.moving && actor->x == p.rally.x && actor->y == p.rally.y
                    && !s.behaviors.contains(id)) p.departed = true;
            }
            for (auto& probe : probes) {
                step(*probe.state); ++pairedTicks;
                if (!compare(s, *probe.state, 2, probe.exact)) return false;
            }
            if (blocked) {
                const auto current = pose(s, 4);
                if (current.x != blockerPose.x || current.y != blockerPose.y || current.health != blockerPose.health
                    || current.dock >= 0 || findRuntimeRepairDepotRelationForUnit(s.repairDepots, 4))
                    return fail("healthy real first-exit blocker was moved, repaired, or consumed");
            }
            std::erase_if(probes, [&](const Probe& probe) { return !probe.exact && s.tick >= probe.expires; });
            const auto* actor = findObjectById(s, 2); const auto b = s.behaviors.find(2);
            const auto* relation = findRuntimeRepairDepotRelationForUnit(s.repairDepots, 2);
            const bool segment = b != s.behaviors.end() && actor->renderMoveActive && b->second.renderMoveTicksRemaining > 2
                && b->second.renderMoveTicksRemaining < b->second.renderMoveTicksTotal;
            int checkpointPhase = -1;
            if (segment && relation && relation->phase == RuntimeRepairDepotRelationPhase::Reserved
                && (b->second.renderToOffsetX != 0 || b->second.renderToOffsetY != 0)) checkpointPhase = 0;
            else if (actor->dockedToObjectId == 1 && progress[2].healed && actor->health < progress[2].maxHealth) checkpointPhase = 1;
            else if (segment && progress[2].released
                && (b->second.renderFromOffsetX != 0 || b->second.renderFromOffsetY != 0)) checkpointPhase = 2;
            if (persistence && checkpointPhase >= 0 && !saved[checkpointPhase]) {
                stage = "checkpoint phase " + std::to_string(checkpointPhase);
                // Ordinary saves retain the current committed ground segment,
                // but their generic future route is reconstructed. Validate
                // every remaining interpolated frame before generic next-route
                // dispatch, without fabricating/replacing its path. Canonical
                // probes additionally cross segment completion and finish.
                const int ordinaryDuration = checkpointPhase == 1 ? std::max(1, s.repairDepots.remainingServiceTicks(1))
                    : std::max(1, b->second.renderMoveTicksRemaining - 1);
                for (bool exact : {false, true}) {
                    auto restored = checkpoint(s, exact);
                    if (!restored) return fail("checkpoint load rejected: " + error);
                    if (!compare(s, *restored, 2, exact)) return false;
                    probes.push_back({std::move(restored), exact, s.tick + ordinaryDuration});
                    if (exact) ++exactSaves; else ++ordinarySaves;
                }
                saved[checkpointPhase] = true;
            }
            if (!outputRoot.empty() && vehicle == "MTNK" && hz == 30 && !moving && !queued) {
                if (!capturedEntry && checkpointPhase == 0) { if (!capture(s, "entry")) return false; capturedEntry = true; }
                if (!capturedExit && checkpointPhase == 2) { if (!capture(s, "exit")) return false; capturedExit = true; }
            }
            const bool done = std::all_of(progress.begin(), progress.end(), [](const auto& item) { return item.second.departed; });
            if (done && completedAt < 0) completedAt = s.tick;
            if (completedAt >= 0 && s.tick - completedAt >= hz * 2) break;
        }
        for (const auto& [id, p] : progress) {
            if (!p.docked || !p.healed || !p.released || !p.departed || pose(s, id).health != p.maxHealth
                || pose(s, id).dock >= 0 || findRuntimeRepairDepotRelationForUnit(s.repairDepots, id))
                return fail("original service/departure incomplete: dock=" + std::to_string(p.docked) + " heal=" + std::to_string(p.healed)
                    + " release=" + std::to_string(p.released) + " departed=" + std::to_string(p.departed) + ' ' + diagnostic(s, id));
            ++services;
        }
        if (s.economy.credits >= 10000) return fail("original paid repair consumed no credits");
        if (persistence && (!saved[0] || !saved[1] || !saved[2]))
            return fail("actual approach/service/departure checkpoints were not all reached");
        return true;
    };
    for (const std::string depot : {"GADEPT", "NADEPT"}) for (const std::string vehicle : {"MTNK", "HTNK", "FV", "TNKD"})
        for (int hz : {30, 60}) scene(depot + '-' + vehicle + '-' + std::to_string(hz), [&]() { return run(depot, vehicle, hz, false, false); });
    for (const std::string depot : {"GADEPT", "NADEPT"}) {
        scene(depot + "-MTNK-30-moving", [&]() { return run(depot, "MTNK", 30, true, false); });
        scene(depot + "-MTNK-60-queue", [&]() { return run(depot, "MTNK", 60, false, true); });
        scene(depot + "-MTNK-30-blocked-first-exit", [&]() { return run(depot, "MTNK", 30, false, false, true); });
    }
    if (!failures.empty()) {
        std::ostringstream out; out << "Repair motion: " << failures.size() << " failed scenes";
        for (const auto& item : failures) out << '\n' << item;
        error = out.str(); return false;
    }
    if (!filter.empty() && scenes == 0) { error = "Repair motion: unknown focused case " + filter; return false; }
    std::cout << "Repair motion passed: " << scenes << " stock cases, " << services << " real services, " << motionChecks
        << " continuous/direction checks, " << ordinarySaves << '+' << exactSaves << " phase saves, " << pairedTicks
        << " paired ticks, " << captures << " native captures\n";
    return true;
}
