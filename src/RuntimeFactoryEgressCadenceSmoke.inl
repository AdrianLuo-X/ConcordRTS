// Real sidebar queue -> paid original production -> original factory exit.
// Speed expectations come from the retail 15-Hz lepton clock, not the new
// factory-egress duration helper. No unit, route or completion is fabricated.
bool runRuntimeFactoryEgressCadenceSmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile stock, stockArt; RulesDatabase rules; ArtDatabase art;
    if (!stock.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(stock)
        || !stockArt.loadFromFile(live.report.artPath) || !art.loadFromIni(stockArt)) {
        error = "Factory egress: original rules/art unavailable"; return false;
    }
    const char* directory = std::getenv("CONCORD_FACTORY_EGRESS_OUTPUT");
    const char* requested = std::getenv("CONCORD_FACTORY_EGRESS_CASE");
    const std::string outputRoot = directory ? directory : "", filter = requested ? requested : "";
    if (!outputRoot.empty()) std::filesystem::create_directories(outputRoot);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int sceneCount = 0, producedCount = 0, linearChecks = 0, motionChecks = 0, cadenceChecks = 0;
    int sidebarChecks = 0, clearanceSegments = 0, ordinarySaves = 0, exactSaves = 0, pairedTicks = 0;
    int outsideChecks = 0, terminalBrakes = 0, captures = 0, codecChecks = 0, gateChecks = 0;
    const auto fail = [&](const std::string& why) {
        error = "Factory egress " + label + " [" + stage + "]: " + why; return false;
    };
    const auto scene = [&](const std::string& name, const auto& body) {
        if (!filter.empty() && filter != name) return;
        label = name; stage = "original fixture"; fixtureFailure.clear();
        bool passed = body();
        if (!fixtureFailure.empty()) { fail(fixtureFailure); passed = false; }
        if (passed) { ++sceneCount; std::cout << "Factory egress " << name << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    int landTile = -1; auto metadataCache = std::make_unique<RuntimeMapRenderCache>();
    const auto metadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, *metadataCache) == 0) { landTile = tile; break; }
    }
    if (landTile < 0) { error = "Factory egress: original flat land TMP unavailable"; return false; }
    struct Case { const char* factory; const char* unit; const char* country; const char* power; int leptons; };
    const std::array<Case, 3> cases{{{"GAWEAP", "MTNK", "Americans", "GAPOWR", 17},
        {"NAWEAP", "HTNK", "Russians", "NAPOWR", 15}, {"YAWEAP", "LTNK", "YuriCountry", "YAPOWR", 17}}};
    const auto add = [&](RuntimeWindowState& s, int id, const std::string& type, WorldObjectKind kind,
            RuntimeCellPoint cell, const std::string& owner = "Commander") {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "Egress" + std::to_string(id);
        object.type = type; object.kind = kind; object.owner = owner; object.x = cell.x; object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y); object.alive = object.poweredOperational = true;
        object.health = runtimeMaxHealthForObject(s, object); object.mission = "Sleep";
        object.buildAnimTicksRemaining = 0;
        initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
        runtimeInitializeModExtensions(object, s.rules, s.config.fixedTickHz);
        s.world.accessObjects().push_back(std::move(object));
    };
    const auto make = [&](const Case& c, int hz, bool accelerated, bool compact = false) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.playerCountry = c.country; s.config.techLevel = 10;
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false; s.config.noCooldown = accelerated;
        s.config.startingUnits = 0; s.config.playerSlots.clear();
        s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = compact ? 65 : 55; y <= (compact ? 99 : 103); ++y)
            for (int x = 55; x <= (compact ? 110 : 205); ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        add(s, 1, c.factory, WorldObjectKind::Structure, {75, 75});
        for (int n = 0; n < 4; ++n) add(s, 100 + n, c.power, WorldObjectKind::Structure, {64 + n * 5, 96});
        const std::string yard = c.country == std::string("Americans") ? "GACNST"
            : c.country == std::string("Russians") ? "NACNST" : "YACNST";
        add(s, 2, yard, WorldObjectKind::Structure, {64, 65});
        add(s, 900, c.unit, WorldObjectKind::Unit, {85, 91});
        // IDs cross 999/1000, also exercising real canonical row ordering.
        add(s, 998, "NANRCT", WorldObjectKind::Structure, {57, compact ? 66 : 57}, "SkirmishPlayer2");
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 50000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Commander"; local.country = c.country; local.color = "Gold";
        RuntimeConfig::PlayerSlot hostile; hostile.enabled = true; hostile.country = "Russians"; hostile.color = "Red";
        s.config.playerSlots = {local, hostile};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["commander"] = c.country; s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house;
        house.owner = "Commander"; house.canonicalOwner = "commander"; house.country = c.country;
        house.localFacade = true; s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeRepairDepotsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        rebuildRuntimeEconomyPower(s); return box;
    };
    struct Pose {
        double x = 0, y = 0, z = 0; int facing = -1, hp = 0, cellX = 0, cellY = 0;
        bool rendered = false, moving = false;
        bool operator==(const Pose&) const = default;
    };
    const auto pose = [](const WorldObjectInstance& object) {
        return Pose{object.hasRenderPosition ? object.renderX : double(object.x),
            object.hasRenderPosition ? object.renderY : double(object.y),
            object.hasRenderPosition ? object.renderLevel : double(object.level),
            object.facing, object.health, object.x, object.y, object.hasRenderPosition, object.renderMoveActive};
    };
    const auto diagnostic = [&](const RuntimeWindowState& s, int id) {
        std::ostringstream out; out << std::setprecision(17) << "tick=" << s.tick << " id=" << id;
        if (const auto* object = findObjectById(s, id)) {
            const auto p = pose(*object); out << " XYZ=" << p.x << ',' << p.y << ',' << p.z << " cell="
                << p.cellX << ',' << p.cellY << " moving=" << p.moving << " rendered=" << p.rendered
                << " facing=" << p.facing << " hp=" << p.hp << " mission=" << object->mission;
        } else out << " missing";
        if (const auto it = s.behaviors.find(id); it != s.behaviors.end()) {
            const auto& b = it->second; out << " kind/state=" << int(b.kind) << '/' << int(b.state)
                << " target=" << b.moveTarget.x << ',' << b.moveTarget.y << " from=" << b.renderFrom.x << ',' << b.renderFrom.y
                << " to=" << b.renderTo.x << ',' << b.renderTo.y << " clocks=" << b.moveCooldownTicks << '/'
                << b.renderMoveTicksRemaining << '/' << b.renderMoveTicksTotal << " arrival=" << b.renderArrivalDistanceAfter
                << " clearanceHost=" << b.factoryExitClearanceHostId << " path=" << b.path.size();
        }
        const auto& lane = s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Vehicles)];
        out << " credits=" << s.economy.credits << " queue=" << lane.queued.size() << " leases=" << s.factoryDeliveries.leases().size();
        if (lane.active) out << " active=" << lane.active->serial << ':' << lane.active->remainingTicks << ':' << lane.active->creditsSpent;
        return out.str();
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended: " + s.skirmishResult;
    };
    const auto facingDifference = [](int a, int b) { const int d = std::abs(a - b) % 256; return std::min(d, 256 - d); };
    const auto displacementFacing = [](double dx, double dy) {
        constexpr double pi = 3.14159265358979323846;
        double angle = std::atan2(dx - dy, -(dx + dy)); if (angle < 0) angle += 2 * pi;
        return int(std::lround(angle * 256 / (2 * pi))) % 256;
    };
    const auto ownsLease = [](const RuntimeWindowState& s, int id) {
        return std::any_of(s.factoryDeliveries.leases().begin(), s.factoryDeliveries.leases().end(),
            [&](const auto& lease) { return lease.producedObjectId == id && lease.domain == RuntimeFactoryDeliveryDomain::Land
                && lease.lane == RuntimeProductionLane::Vehicles && lease.hostId == 1; });
    };
    const auto inspect = [&](const RuntimeWindowState& s, int id, const Case& c, const Pose& before,
            const RuntimeObjectBehavior* old, bool wasLeased, bool accelerated) {
        const auto* object = findObjectById(s, id); const auto* type = object ? findRuntimeUnitType(s.rules, *object) : nullptr;
        if (!object || !type || !object->alive || object->health != type->strength)
            return fail("real vehicle lost original health/type: " + diagnostic(s, id));
        const auto now = pose(*object); const double dx = now.x - before.x, dy = now.y - before.y, moved = std::hypot(dx, dy);
        const bool generated = old && old->factoryExitClearanceHostId == 1;
        const bool fastSegment = accelerated && (wasLeased || generated);
        const double segmentDistance = old ? std::hypot(old->renderTo.x + old->renderToOffsetX - old->renderFrom.x - old->renderFromOffsetX,
            old->renderTo.y + old->renderToOffsetY - old->renderFrom.y - old->renderFromOffsetY) : 1.0;
        const double maximumStep = fastSegment ? segmentDistance / std::max(2, int(std::floor(s.config.fixedTickHz * 0.2 * segmentDistance)))
            : 1.25 * c.leptons * 15.0 / (256.0 * s.config.fixedTickHz);
        if (!std::isfinite(now.x) || !std::isfinite(now.y) || !std::isfinite(now.z)
            || moved > maximumStep + 1.0e-6 || std::abs(now.z - before.z) > 1.0e-6)
            return fail("noncontinuous / accelerated physical body: delta=" + std::to_string(moved)
                + " allowed=" + std::to_string(maximumStep) + " " + diagnostic(s, id));
        const bool finishedSegment = old && old->moveCooldownTicks == 1 && old->renderMoveTicksRemaining == 1;
        const int visibleFacing = finishedSegment ? before.facing : now.facing;
        if (moved > 1.0e-8 && facingDifference(visibleFacing, displacementFacing(dx, dy)) > 1)
            return fail("body drifts sideways from actual displacement: " + diagnostic(s, id));
        if (before.facing >= 0 && now.facing >= 0 && facingDifference(before.facing, now.facing) > 2 * type->rot + 1)
            return fail("existing body turned instantly beyond original ROT: " + diagnostic(s, id));
        if (old && old->renderMoveTicksRemaining > 0 && (wasLeased || fastSegment)) {
            const double fromX = old->renderFrom.x + old->renderFromOffsetX, fromY = old->renderFrom.y + old->renderFromOffsetY;
            const double toX = old->renderTo.x + old->renderToOffsetX, toY = old->renderTo.y + old->renderToOffsetY;
            const double fraction = double(old->renderMoveTicksTotal - old->renderMoveTicksRemaining + 1) / old->renderMoveTicksTotal;
            if (std::abs(now.x - (fromX + (toX - fromX) * fraction)) > 1.0e-9
                || std::abs(now.y - (fromY + (toY - fromY) * fraction)) > 1.0e-9)
                return fail("factory exit is not the committed linear segment: " + diagnostic(s, id));
            ++linearChecks;
        }
        if (const auto it = s.behaviors.find(id); it != s.behaviors.end()) {
            const auto& b = it->second;
            if (b.renderMoveTicksRemaining > 0 && b.renderMoveTicksTotal > 0) {
                const double distance = std::hypot(b.renderTo.x + b.renderToOffsetX - b.renderFrom.x - b.renderFromOffsetX,
                    b.renderTo.y + b.renderToOffsetY - b.renderFrom.y - b.renderFromOffsetY);
                const bool egress = ownsLease(s, id), autoClear = b.factoryExitClearanceHostId == 1;
                const bool capped = accelerated && (egress || autoClear);
                if (capped && (b.renderMoveTicksTotal > std::ceil(0.2 * s.config.fixedTickHz * distance) + 1
                    || b.renderMoveTicksTotal < 2 || b.renderArrivalDistanceAfter >= 0.0))
                    return fail("fast factory segment retained ordinary slow arrival or lacks interpolation: " + diagnostic(s, id));
                if (!capped && (egress || b.renderArrivalDistanceAfter < 0.0)) {
                    const double interval = distance * 256.0 * s.config.fixedTickHz / (15.0 * c.leptons);
                    if (b.renderMoveTicksTotal < std::floor(interval) - 1 || b.renderMoveTicksTotal > std::ceil(interval) + 1
                        || (egress && b.renderArrivalDistanceAfter >= 0.0))
                        return fail("original Speed changed / factory exit inherits terminal braking: " + diagnostic(s, id));
                    ++cadenceChecks; if (!egress && !autoClear) ++outsideChecks;
                }
                if (!egress && !autoClear && b.renderArrivalDistanceAfter == 0.0) {
                    const double cruise = distance * 256.0 * s.config.fixedTickHz / (15.0 * c.leptons);
                    if (b.renderMoveTicksTotal <= std::floor(cruise)) return fail("ordinary destination lost native arrival braking: " + diagnostic(s, id));
                    ++terminalBrakes;
                }
                if (autoClear) ++clearanceSegments;
            }
            if (id == 900 && b.factoryExitClearanceHostId >= 0)
                return fail("existing player Move was tagged/accelerated as automatic clearance: " + diagnostic(s, id));
        }
        ++motionChecks; return true;
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream ordinaryText, fullText, canonical; IniFile base, snapshot;
        if (!writeRuntimeStateToStream(s, ordinaryText, error) || !base.loadFromString(ordinaryText.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, fullText, error) || !writeRuntimeMultiplayerSnapshotExtension(s, fullText, error)
            || !snapshot.loadFromString(fullText.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot, canonical, error)
            || !snapshot.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
        // Canonical filtering omits v14 AV-host authority. Load the valid
        // ordinary base, then apply its real canonical extension, unchanged.
        if (!materializeRuntimeLoadStateFromIni(*restored, base, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*restored, snapshot, error))) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*restored); return restored;
    };
    const auto compare = [&](const RuntimeWindowState& s, const RuntimeWindowState& other, int id, bool exact) {
        const auto* a = findObjectById(s, id); const auto* b = findObjectById(other, id);
        if (s.tick != other.tick || !a || !b || pose(*a) != pose(*b)
            || s.economy.credits != other.economy.credits
            || s.factoryDeliveries.stateHash() != other.factoryDeliveries.stateHash()
            || runtimeProductionLanesStateHash(s.economy.localProductionLanes) != runtimeProductionLanesStateHash(other.economy.localProductionLanes))
            return fail(std::string(exact ? "canonical" : "ordinary") + " pose/paid queue/lease diverged: original=["
                + diagnostic(s, id) + "] restored=[" + diagnostic(other, id) + ']');
        const auto x = s.behaviors.find(id), y = other.behaviors.find(id);
        if ((x == s.behaviors.end()) != (y == other.behaviors.end())) return fail("saved committed behavior presence changed");
        if (x != s.behaviors.end()) {
            const auto& m = x->second; const auto& n = y->second;
            if (m.kind != n.kind || m.moveTarget != n.moveTarget || m.moveCooldownTicks != n.moveCooldownTicks
                || m.renderMoveTicksTotal != n.renderMoveTicksTotal || m.renderMoveTicksRemaining != n.renderMoveTicksRemaining
                || m.renderFrom != n.renderFrom || m.renderTo != n.renderTo
                || m.renderFromOffsetX != n.renderFromOffsetX || m.renderFromOffsetY != n.renderFromOffsetY
                || m.renderToOffsetX != n.renderToOffsetX || m.renderToOffsetY != n.renderToOffsetY
                || m.renderArrivalDistanceAfter != n.renderArrivalDistanceAfter || m.factoryExitClearanceHostId != n.factoryExitClearanceHostId
                || (exact && (m.state != n.state || m.path != n.path)))
                return fail("saved egress/auto-clearance segment restarted or lost its source: original=["
                    + diagnostic(s, id) + "] restored=[" + diagnostic(other, id) + ']');
        }
        if (exact) for (const auto& object : s.world.getObjects()) {
            if (!object.alive || object.kind != WorldObjectKind::Unit) continue;
            const auto* peer = findObjectById(other, object.instanceId);
            if (!peer || pose(object) != pose(*peer)) return fail("canonical other moving unit/spawn changed: " + diagnostic(s, object.instanceId));
        }
        ++pairedTicks; return true;
    };
    struct Replica { std::unique_ptr<RuntimeWindowState> state; int id = -1, until = 0; bool exact = false; };
    const auto auditMarker = [&](RuntimeWindowState& s, int id) {
        const auto* object = findObjectById(s, id); const auto source = s.behaviors.find(id);
        if (!object || source == s.behaviors.end() || source->second.factoryExitClearanceHostId != 1)
            return fail("marker audit was not captured from a real automatic clearance");
        std::ostringstream text; IniFile base;
        if (!writeRuntimeStateToStream(s, text, error) || !base.loadFromString(text.str())) return fail("marker ordinary save failed: " + error);
        const auto* section = base.getSection("Behaviors"); std::string key, raw; std::vector<std::string> fields;
        if (section) for (const auto& entry : *section) {
            const auto tokens = splitRuntimeSaveFields(entry.second);
            if (tokens.size() == 17 && tokens[0] == std::to_string(id)) { key = entry.first; raw = entry.second; fields = tokens; break; }
        }
        const auto join = [](const std::vector<std::string>& values) {
            std::ostringstream out; for (std::size_t n = 0; n < values.size(); ++n) { if (n) out << '|'; out << values[n]; } return out.str();
        };
        if (fields.size() != 17 || fields[16] != "1") return fail("real ordinary automatic clearance did not write the optional 17th host field");
        int parsedId = -1; RuntimeObjectBehavior parsed;
        if (!parseRuntimeSavedBehaviorRecord(fields, *object, 14, parsedId, parsed)
            || parsedId != id || parsed.factoryExitClearanceHostId != 1) return fail("valid 17-field ordinary source record rejected");
        auto legacy = fields; legacy.pop_back();
        if (!parseRuntimeSavedBehaviorRecord(legacy, *object, 14, parsedId, parsed) || parsed.factoryExitClearanceHostId != -1)
            return fail("legacy 16-field ordinary behavior inherited fast-clearance authority");
        ++codecChecks;
        for (const std::size_t width : {12U, 14U}) {
            legacy = fields; legacy.resize(width);
            if (!parseRuntimeSavedBehaviorRecord(legacy, *object, 14, parsedId, parsed) || parsed.factoryExitClearanceHostId != -1)
                return fail("legacy ordinary prefix rejected or invented clearance source");
            ++codecChecks;
        }
        auto queued = fields; queued[6] = "0"; queued[14] = queued[15] = "-1";
        if (!parseRuntimeSavedBehaviorRecord(queued, *object, 14, parsedId, parsed) || parsed.factoryExitClearanceHostId != 1)
            return fail("new queued 17-field double-sentinel record rejected");
        auto invalidQueued = queued; invalidQueued[6] = "1";
        if (parseRuntimeSavedBehaviorRecord(invalidQueued, *object, 14, parsedId, parsed))
            return fail("ordinary17 accepted positive cooldown without a committed FROM");
        ++codecChecks;
        queued.pop_back();
        if (parseRuntimeSavedBehaviorRecord(queued, *object, 14, parsedId, parsed)) return fail("legacy16 wrongly accepts new double-sentinel coordinates");
        ++codecChecks;
        auto mixed = fields; mixed[14] = "-1";
        if (parseRuntimeSavedBehaviorRecord(mixed, *object, 14, parsedId, parsed)) return fail("ordinary17 accepted mixed sentinel FROM coordinates");
        ++codecChecks;
        const auto ordinaryReject = [&](const std::vector<std::string>& bad) {
            std::string modified = text.str(); const std::string original = key + '=' + raw;
            const auto at = modified.find(original);
            if (at == std::string::npos) return fail("ordinary bad-marker fixture did not find its exact saved row");
            modified.replace(at, original.size(), key + '=' + join(bad)); IniFile broken;
            if (!broken.loadFromString(modified)) return fail("bad-marker INI fixture malformed");
            auto candidate = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*candidate, s, false); std::string rejected;
            if (materializeRuntimeLoadStateFromIni(*candidate, broken, rejected)) return fail("ordinary illegal automatic-clearance marker accepted");
            // Ordinary file materialization is a detached candidate operation;
            // rejecting it must never alter the live producer or its queue.
            if (source->second.factoryExitClearanceHostId != 1 || !findObjectById(s, id)) return fail("ordinary rejection changed live authority");
            ++codecChecks; return true;
        };
        auto bad = fields; bad[16] = "-2"; if (!ordinaryReject(bad)) return false;
        bad = fields; bad[1] = "Attack"; if (!ordinaryReject(bad)) return false;
        const auto exactRaw = runtimeMultiplayerSnapshotBehaviorRecord(id, source->second);
        auto exactFields = splitRuntimeSaveFields(exactRaw);
        if (exactFields.size() != 38 || !parseRuntimeMultiplayerSnapshotBehaviorRecord(exactRaw, parsedId, parsed)
            || parsed.factoryExitClearanceHostId != 1) return fail("real canonical behavior did not preserve its optional 38th source field");
        legacy = exactFields; legacy.pop_back();
        if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(join(legacy), parsedId, parsed) || parsed.factoryExitClearanceHostId != -1)
            return fail("legacy 37-field canonical behavior inherited a clearance source");
        ++codecChecks;
        for (const std::size_t width : {33U, 35U}) {
            legacy = exactFields; legacy.resize(width);
            if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(join(legacy), parsedId, parsed) || parsed.factoryExitClearanceHostId != -1)
                return fail("legacy canonical prefix rejected or invented clearance source");
            ++codecChecks;
        }
        queued = exactFields; queued[2] = std::to_string(int(RuntimeBehaviorState::Queued));
        queued[25] = "0"; queued[35] = queued[36] = "-1";
        if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(join(queued), parsedId, parsed) || parsed.factoryExitClearanceHostId != 1)
            return fail("new queued 38-field double-sentinel record rejected");
        invalidQueued = queued; invalidQueued[25] = "1";
        if (parseRuntimeMultiplayerSnapshotBehaviorRecord(join(invalidQueued), parsedId, parsed))
            return fail("canonical38 accepted positive cooldown without a committed FROM");
        ++codecChecks;
        invalidQueued = queued; invalidQueued[25] = "-1";
        if (parseRuntimeMultiplayerSnapshotBehaviorRecord(join(invalidQueued), parsedId, parsed))
            return fail("canonical38 accepted negative movement cooldown");
        ++codecChecks;
        queued.pop_back();
        if (parseRuntimeMultiplayerSnapshotBehaviorRecord(join(queued), parsedId, parsed)) return fail("legacy37 wrongly accepts new double-sentinel coordinates");
        ++codecChecks;
        mixed = exactFields; mixed[35] = "-1";
        if (parseRuntimeMultiplayerSnapshotBehaviorRecord(join(mixed), parsedId, parsed)) return fail("canonical38 accepted mixed sentinel FROM coordinates");
        ++codecChecks;
        for (const int forbiddenField : {1, 5, 33}) {
            bad = exactFields;
            bad[forbiddenField] = forbiddenField == 1 ? std::to_string(int(RuntimeBehaviorKind::Attack)) : "1";
            if (parseRuntimeMultiplayerSnapshotBehaviorRecord(join(bad), parsedId, parsed))
                return fail("canonical38 accepted source marker on attack/target/force-fire");
            ++codecChecks;
        }
        for (const auto& kind : std::array<std::pair<const char*, WorldObjectKind>, 4>{{
                {"E1", WorldObjectKind::Infantry}, {"SHAD", WorldObjectKind::Unit},
                {"DEST", WorldObjectKind::Unit}, {"DLPH", WorldObjectKind::Unit}}}) {
            WorldObjectInstance excluded = *object; excluded.type = kind.first; excluded.kind = kind.second;
            excluded.airborne = excluded.type == "SHAD";
            if (!findRuntimeUnitType(s.rules, excluded) || runtimeFactoryClearanceBehaviorIsValid(s, excluded, source->second))
                return fail("rules-aware source marker wrongly accepts stock infantry/air/naval/organic: " + excluded.type);
            ++codecChecks;
        }
        std::ostringstream full, canonical; IniFile snapshot;
        if (!writeRuntimeStateToStream(s, full, error) || !writeRuntimeMultiplayerSnapshotExtension(s, full, error)
            || !snapshot.loadFromString(full.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot, canonical, error))
            return fail("marker canonical fixture export failed: " + error);
        const auto exactReject = [&](const std::vector<std::string>& brokenFields) {
            std::string modified = canonical.str(); const auto at = modified.find(exactRaw);
            if (at == std::string::npos) return fail("canonical bad-marker fixture did not find its exact behavior row");
            modified.replace(at, exactRaw.size(), join(brokenFields)); IniFile broken;
            if (!broken.loadFromString(modified)) return fail("bad canonical marker INI malformed");
            auto candidate = checkpoint(s, true); if (!candidate) return fail("atomic marker checkpoint failed");
            std::string rejected;
            if (materializeRuntimeMultiplayerSnapshotExtension(*candidate, broken, rejected)) return fail("canonical illegal/mismatched clearance source accepted");
            if (!compare(s, *candidate, id, true)) return fail("rejected canonical source mutated committed pose/queue/lease");
            ++codecChecks; return true;
        };
        bad = exactFields; bad[37] = "-2"; if (!exactReject(bad)) return false;
        bad = exactFields; bad[37] = "2"; if (!exactReject(bad)) return false;

        // These isolated configuration/ownership views test the gate using a
        // genuinely generated object and behavior. They create no fake Move,
        // spawn, capture event or future motion in the live simulation.
        auto view = checkpoint(s, true); if (!view) return fail("clearance gate checkpoint failed");
        const auto fast = [&]() {
            const auto* unit = findObjectById(*view, id); const auto it = view->behaviors.find(id);
            return unit && it != view->behaviors.end() && runtimeFactoryEgressUsesFastClock(*view, *unit, it->second);
        };
        if (!fast()) return fail("real local human automatic clearance lacked fast eligibility"); ++gateChecks;
        view->config.noCooldown = false; if (fast()) return fail("normal game inherited fast-clearance clock"); ++gateChecks; view->config.noCooldown = true;
        view->config.multiplayerActive = true; if (fast()) return fail("network inherited local-only fast clearance"); ++gateChecks; view->config.multiplayerActive = false;
        view->campaignMode = true; if (fast()) return fail("campaign inherited local-only fast clearance"); ++gateChecks; view->campaignMode = false;
        auto* authority = findRuntimeHumanHouseAuthority(*view, "Commander");
        if (!authority) return fail("local human gate fixture lost its actual house authority");
        authority->localFacade = false; if (fast()) return fail("remote/AI house inherited local fast clearance"); ++gateChecks; authority->localFacade = true;
        auto* host = findMutableObjectById(*view, 1); if (!host) return fail("source factory missing before ownership-view gate");
        host->owner = "SkirmishPlayer2"; if (fast()) return fail("captured/foreign source still accelerated its former car"); ++gateChecks;
        host->owner = "Commander";
        // The source-loss case uses the real UI sale on the disposable branch.
        // Already committed timing/continuous pose must survive source loss.
        const Pose beforeSale = pose(*findObjectById(*view, id)); const auto committed = view->behaviors.at(id);
        { RuntimeSnapshotActiveStateScope selling(*view);
            if (!dispatchRuntimeStructureToolFromUi(*view, RuntimeSidebarTool::Sell, 1)) return fail("actual source sale rejected"); }
        if (pose(*findObjectById(*view, id)) != beforeSale || fast()
            || view->behaviors.at(id).moveCooldownTicks != committed.moveCooldownTicks
            || view->behaviors.at(id).renderMoveTicksRemaining != committed.renderMoveTicksRemaining)
            return fail("real source sale reset a committed automatic-clearance segment or retained acceleration");
        ++gateChecks;
        auto stale = checkpoint(*view, true); if (!stale) return fail("legitimate sold-source marker was not save-compatible: " + error);
        if (!compare(*view, *stale, id, true)) return false;
        return true;
    };
    const auto capture = [&](RuntimeWindowState& s, RuntimeCellPoint door, const std::string& suffix) {
        if (outputRoot.empty()) return true;
        RuntimeSnapshotActiveStateScope active(s);
        const auto image = drawRuntimeStateOffscreenAtCell(s, door, 800, 600, error,
            outputRoot + '/' + label + '-' + suffix + ".bmp", false);
        if (!image || !image->rendered || image->visibleVoxelBodies < 1) return fail("real factory/vehicle raster missing: " + error);
        ++captures; return true;
    };
    const auto run = [&](const Case& c, int hz, bool accelerated, bool longRally) {
        auto box = make(c, hz, accelerated); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
        const auto* unitType = rules.findUnitType("Vehicle", c.unit);
        if (!unitType || unitType->speed != std::stoi(stock.getValue(c.unit, "Speed", "0"))
            || unitType->rot != std::stoi(stock.getValue(c.unit, "ROT", "0"))
            || std::min(255, unitType->speed * 256 / 100) != c.leptons)
            return fail("original stock Speed/ROT/lepton golden missing");
        auto catalog = std::find_if(s.economy.catalog.begin(), s.economy.catalog.end(),
            [&](const auto& item) { return item.typeId == c.unit && item.kind == RuntimeBuildKind::Unit; });
        if (catalog == s.economy.catalog.end()) return fail("original tech-tree vehicle cameo unavailable");
        const RuntimeBuildCatalogItem item = *catalog;
        if (item.cost != std::stoi(stock.getValue(c.unit, "Cost", "0")) || runtimeNoCooldownForOwner(s, "Commander") != accelerated
            || !runtimeBuildAvailability(s, item).canQueue || !findObjectById(s, 1)->poweredOperational)
            return fail("genuine owner/power/paid catalog queue unavailable: " + runtimeBuildAvailability(s, item).reason);
        const auto spawn = findRuntimeProductionSpawnForOwner(s, item, "Commander", false);
        if (!spawn || !spawn->anchor || spawn->anchor->instanceId != 1) return fail("original stock weapon-factory doorway missing");
        const RuntimeCellPoint door = spawn->cell; const auto outward = runtimeProductionOutwardCell(s, *spawn->anchor, door);
        if (!outward) return fail("original doorway has no outward egress");
        const RuntimeCellPoint rally{outward->x + 100, outward->y};
        if (longRally) {
            stage = "real factory rally UI"; applySelection(s, {1}, false, false);
            if (!dispatchRuntimePlayerFactoryFromUi(s, 1, RuntimeMultiplayerGameplayCell{rally.x, rally.y}))
                return fail("actual rally UI rejected: " + s.interactionStatus);
            const auto it = s.playerCommands.rallyPoints.find(1);
            if (it == s.playerCommands.rallyPoints.end() || it->second.cell != RuntimeMultiplayerGameplayCell{rally.x, rally.y})
                return fail("actual factory rally command not committed");
        } else if (!s.playerCommands.rallyPoints.empty()) return fail("default fixture inherited a user rally");
        stage = "existing player Move keeps original Speed"; applySelection(s, {900}, false, false);
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, {103, 91}))
            return fail("actual independent player Move rejected");
        const int targetCount = accelerated ? 8 : 1, creditsBefore = s.economy.credits;
        int requestedCount = 0, maxPendingTicks = 0, pendingStart = -1; std::uint64_t pendingSerial = 0;
        std::map<int, int> born, released; std::vector<int> order;
        std::vector<Replica> replicas; bool egressSaved = false, clearanceSaved = false, externalMove = false;
        bool stoppedDefault = false, followingBeforeFarArrival = false, sawOutside = false, sawBrake = false;
        bool firstCapture = false, lastCapture = false; const int outsideBefore = outsideChecks, brakeBefore = terminalBrakes;
        const auto requestMore = [&]() {
            while (requestedCount < targetCount) {
                const auto availability = runtimeBuildAvailability(s, item);
                if (!availability.canQueue) {
                    const int capacity = runtimeProductionLaneCapacity(RuntimeProductionLane::Vehicles, s.rules.getMaximumQueuedObjects(), 0);
                    if (runtimeProductionLaneOccupancy(s.economy.localProductionLanes, RuntimeProductionLane::Vehicles) >= capacity) break;
                    return fail("actual repeated sidebar became unavailable: " + availability.reason);
                }
                if (!dispatchRuntimeQueueProductionFromUi(s, item)) return fail("actual repeated production UI rejected: " + s.interactionStatus);
                ++requestedCount;
            }
            return true;
        };
        const auto savePair = [&](int id) {
            const auto it = s.behaviors.find(id);
            if (it == s.behaviors.end() || it->second.renderMoveTicksRemaining <= 2) return fail("save did not occur inside a genuine committed segment");
            for (const bool exact : {false, true}) {
                auto restored = checkpoint(s, exact);
                if (!restored) return fail("mid-exit checkpoint rejected: " + error);
                if (!compare(s, *restored, id, exact)) return false;
                const int duration = exact ? hz * 2 : it->second.renderMoveTicksRemaining - 1;
                // Ordinary v14 rebuilds remaining paths. Compare its preserved
                // committed segment before that boundary; canonical preserves
                // the actual full path and continues across release/next unit.
                replicas.push_back({std::move(restored), id, s.tick + duration, exact});
                if (exact) ++exactSaves; else ++ordinarySaves;
            }
            return true;
        };
        std::ofstream csv;
        if (!outputRoot.empty()) { csv.open(outputRoot + '/' + label + ".csv"); csv << "tick,id,x,y,z,facing,lease,clearanceHost,total,remaining,queue,activeRemaining,credits\n"; }
        stage = "actual paid sidebar production";
        if (!requestMore()) return false;
        const int originalManufactureBudget = item.buildTicks * targetCount + hz * 20;
        const int stockTravelBudget = int(std::ceil(125.0 * 256.0 * hz / (15.0 * c.leptons))) + hz * 20;
        const int deadline = accelerated ? stockTravelBudget + hz * 25 : originalManufactureBudget + hz * 35;
        bool finished = false;
        for (int iteration = 0; iteration < deadline; ++iteration) {
            stage = "continuous production / physical egress / sidebar";
            struct Prior { Pose pose; std::optional<RuntimeObjectBehavior> move; bool lease = false; };
            std::map<int, Prior> previous;
            for (const auto& object : s.world.getObjects()) if (object.alive && object.kind == WorldObjectKind::Unit) {
                Prior p; p.pose = pose(object); p.lease = ownsLease(s, object.instanceId);
                if (const auto it = s.behaviors.find(object.instanceId); it != s.behaviors.end()) p.move = it->second;
                previous.emplace(object.instanceId, std::move(p));
            }
            step(s); if (!fixtureFailure.empty()) return fail(fixtureFailure);
            for (auto it = replicas.begin(); it != replicas.end();) {
                step(*it->state);
                if (!compare(s, *it->state, it->id, it->exact)) return false;
                if (s.tick >= it->until) it = replicas.erase(it); else ++it;
            }
            std::vector<const WorldObjectInstance*> units;
            for (const auto& object : s.world.getObjects()) {
                if (!object.alive || object.kind != WorldObjectKind::Unit || object.owner != "Commander") continue;
                units.push_back(&object);
                const auto old = previous.find(object.instanceId);
                if (old != previous.end() && !inspect(s, object.instanceId, c, old->second.pose,
                        old->second.move ? &*old->second.move : nullptr, old->second.lease, accelerated)) return false;
                if (object.instanceId == 900) continue;
                if (object.type != c.unit || object.health != unitType->strength) return fail("production created non-stock type/health");
                if (!born.contains(object.instanceId)) {
                    if (born.size() >= static_cast<std::size_t>(targetCount)) return fail("one actual order produced duplicate vehicles");
                    born[object.instanceId] = s.tick; order.push_back(object.instanceId); ++producedCount;
                    if (!ownsLease(s, object.instanceId)) return fail("real produced vehicle was not delivered through a factory lease");
                    // The full tick may already commit the destination cell
                    // before beginning interpolation. Actual body position,
                    // not that ahead-of-render navigation cell, proves birth.
                    const auto birthPose = pose(object);
                    if (birthPose.x != door.x || birthPose.y != door.y)
                        return fail("vehicle body spawned away from its original doorway: " + diagnostic(s, object.instanceId));
                    if (const auto b = s.behaviors.find(object.instanceId); b != s.behaviors.end()
                        && b->second.renderMoveTicksTotal > 0
                        && (b->second.renderFrom != door || b->second.renderTo != *outward))
                        return fail("first committed factory segment does not join actual door to outward cell");
                    const int expectedFacing = displacementFacing(outward->x - door.x, outward->y - door.y);
                    if (facingDifference(object.facing, expectedFacing) > 1) return fail("new stock tank did not face its physical exit before movement");
                    if (order.size() > 1 && longRally) {
                        const auto* first = findObjectById(s, order.front());
                        followingBeforeFarArrival = followingBeforeFarArrival || (first && released.contains(first->instanceId)
                            && std::hypot(pose(*first).x - rally.x, pose(*first).y - rally.y) > 10);
                    }
                }
                if (!ownsLease(s, object.instanceId) && !released.contains(object.instanceId)) {
                    released[object.instanceId] = s.tick;
                    if (pose(object).x != outward->x || pose(object).y != outward->y)
                        return fail("door lease released before actual body reached original outward cell: " + diagnostic(s, object.instanceId));
                }
                const auto move = s.behaviors.find(object.instanceId);
                if (!object.renderMoveActive && !ownsLease(s, object.instanceId)) stoppedDefault = stoppedDefault || !longRally;
                if (accelerated && move != s.behaviors.end() && move->second.renderMoveTicksRemaining > 2
                    && move->second.renderMoveTicksRemaining < move->second.renderMoveTicksTotal) {
                    if (!egressSaved && ownsLease(s, object.instanceId)) { stage = "committed egress save"; if (!savePair(object.instanceId)) return false; egressSaved = true; }
                    if (!clearanceSaved && move->second.factoryExitClearanceHostId == 1) {
                        stage = "committed automatic clear-door save";
                        if (!savePair(object.instanceId) || !auditMarker(s, object.instanceId)) return false; clearanceSaved = true;
                    }
                }
                if (csv && (iteration % 5 == 0 || old == previous.end())) {
                    const auto p = pose(object); const auto& lane = s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Vehicles)];
                    csv << s.tick << ',' << object.instanceId << ',' << std::setprecision(17) << p.x << ',' << p.y << ',' << p.z << ',' << p.facing
                        << ',' << ownsLease(s, object.instanceId) << ',' << (move == s.behaviors.end() ? -1 : move->second.factoryExitClearanceHostId)
                        << ',' << (move == s.behaviors.end() ? 0 : move->second.renderMoveTicksTotal)
                        << ',' << (move == s.behaviors.end() ? 0 : move->second.renderMoveTicksRemaining) << ',' << lane.queued.size()
                        << ',' << (lane.active ? lane.active->remainingTicks : -1) << ',' << s.economy.credits << '\n';
                }
            }
            for (std::size_t a = 0; a < units.size(); ++a) for (std::size_t b = a + 1; b < units.size(); ++b) {
                const auto x = pose(*units[a]), y = pose(*units[b]);
                if ((units[a]->x == units[b]->x && units[a]->y == units[b]->y)
                    || std::hypot(x.x - y.x, x.y - y.y) < 0.1)
                    return fail("genuine produced/clearing units overlap: " + std::to_string(units[a]->instanceId) + '/' + std::to_string(units[b]->instanceId));
            }
            const auto& lane = s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Vehicles)];
            const auto* sidebarActive = runtimeActiveProductionForItem(s, item);
            if ((sidebarActive != nullptr) != lane.active.has_value() || runtimeReadyProductionForItem(s, item) != nullptr
                || runtimeQueuedProductionCountForItem(s, item) != int(lane.queued.size()) || lane.onHold)
                return fail("actual vehicle cameo active/ready/count became stale");
            ++sidebarChecks;
            if (lane.active && lane.active->remainingTicks == 0) {
                if (pendingSerial != lane.active->serial) { pendingSerial = lane.active->serial; pendingStart = s.tick; }
                const int pending = s.tick - pendingStart + 1; maxPendingTicks = std::max(maxPendingTicks, pending);
                const int allowance = accelerated ? hz * 2 : int(std::ceil(256.0 * hz / (15.0 * c.leptons))) + hz * 2;
                if (pending > allowance) return fail("paid completed sidebar waits excessively behind physical exit: " + diagnostic(s, order.empty() ? 1 : order.back()));
            } else { pendingSerial = 0; pendingStart = -1; }
            int spent = int(born.size()) * item.cost;
            if (lane.active) spent += lane.active->creditsSpent;
            if (s.economy.credits != creditsBefore - spent) return fail("original queue did not debit each actual cost once");
            const bool captureScene = accelerated && !longRally && hz == 30;
            if (captureScene && !firstCapture && !order.empty() && ownsLease(s, order.front())
                && findObjectById(s, order.front())->renderMoveActive) {
                if (!capture(s, door, "first-egress")) return false; firstCapture = true;
            }
            // Keep every existing exact snapshot's queue identical by issuing
            // the same real UI enqueue if capacity required replenishment.
            const int previouslyRequested = requestedCount;
            if (!requestMore()) return false;
            for (auto& replica : replicas) if (replica.exact) for (int n = previouslyRequested; n < requestedCount; ++n) {
                RuntimeSnapshotActiveStateScope restoring(*replica.state);
                if (!dispatchRuntimeQueueProductionFromUi(*replica.state, item)) return fail("canonical repeated real queue UI rejected");
            }
            const bool allDelivered = int(born.size()) == targetCount && released.size() == born.size()
                && !lane.active && !lane.ready && lane.queued.empty() && s.factoryDeliveries.leases().empty();
            bool parkedProduced = true;
            for (const auto& [id, birthTick] : born) {
                (void)birthTick; const auto* object = findObjectById(s, id);
                parkedProduced = parkedProduced && object && !object->renderMoveActive;
            }
            if (allDelivered && !longRally && !externalMove && parkedProduced) {
                stage = "original player Move after automatic factory clearance";
                const int id = order.front(); applySelection(s, {id}, false, false);
                const auto p = pose(*findObjectById(s, id));
                const RuntimeCellPoint goal{std::max(90, int(std::lround(p.x)) + 12), std::max(84, int(std::lround(p.y)) + 4)};
                if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, goal)) return fail("real post-egress player Move rejected");
                if (s.behaviors.at(id).factoryExitClearanceHostId >= 0) return fail("new player order inherited automatic clearance speed");
                for (auto& replica : replicas) if (replica.exact) {
                    RuntimeSnapshotActiveStateScope restoring(*replica.state); applySelection(*replica.state, {id}, false, false);
                    if (!dispatchRuntimeTargetOrderFromUi(*replica.state, RuntimeMultiplayerGameplayTargetOrderKind::Move, goal)) return fail("saved real player Move rejected");
                }
                externalMove = true;
            }
            bool allStopped = true;
            for (const auto& [id, birthTick] : born) {
                (void)birthTick; const auto* object = findObjectById(s, id); const auto behavior = s.behaviors.find(id);
                allStopped = allStopped && object && !object->renderMoveActive
                    && (behavior == s.behaviors.end() || behavior->second.state == RuntimeBehaviorState::Complete);
            }
            sawOutside = outsideChecks > outsideBefore; sawBrake = terminalBrakes > brakeBefore;
            if (allDelivered && allStopped && replicas.empty() && sawOutside && sawBrake) {
                if (captureScene && !lastCapture) { if (!capture(s, door, "eight-released")) return false; lastCapture = true; }
                finished = true; break;
            }
        }
        stage = "natural final factory / sidebar / native travel contract";
        if (!finished || requestedCount != targetCount || int(born.size()) != targetCount || released.size() != born.size()
            || !s.factoryDeliveries.leases().empty() || s.economy.credits != creditsBefore - targetCount * item.cost
            || (accelerated && (!egressSaved || !clearanceSaved || (!longRally && !stoppedDefault)))
            || (longRally && !followingBeforeFarArrival) || !sawOutside || !sawBrake)
            return fail("incomplete genuine flow: delivered=" + std::to_string(born.size()) + '/' + std::to_string(targetCount)
                + " released=" + std::to_string(released.size()) + " saved=" + std::to_string(egressSaved) + '/' + std::to_string(clearanceSaved)
                + " defaultStopped=" + std::to_string(stoppedDefault) + " nextBeforeFar=" + std::to_string(followingBeforeFarArrival)
                + " outside/brake=" + std::to_string(sawOutside) + '/' + std::to_string(sawBrake)
                + " " + diagnostic(s, order.empty() ? 1 : order.front()));
        if (longRally) {
            const auto* first = findObjectById(s, order.front());
            if (!first || std::hypot(pose(*first).x - rally.x, pose(*first).y - rally.y) > 1.5)
                return fail("first actual long-rally unit never reached its real destination");
        }
        if (runtimeActiveProductionForItem(s, item) || runtimeReadyProductionForItem(s, item)
            || runtimeQueuedProductionCountForItem(s, item) != 0 || !runtimeBuildAvailability(s, item).canQueue)
            return fail("actual vehicle cameo remained completed/busy after queue drained");
        std::cout << "Factory egress metrics " << label << " units=" << born.size() << " paid=" << targetCount * item.cost
            << " maxCompletedWaitTicks=" << maxPendingTicks << " originalLeptons=" << c.leptons
            << " firstRelease=" << released.at(order.front()) - born.at(order.front()) << " ticks=" << s.tick << '\n';
        return true;
    };
    const auto runBlockedRally = [&](int hz) {
        const auto& c = cases.front(); auto box = make(c, hz, true, true); auto& s = *box;
        RuntimeSnapshotActiveStateScope active(s);
        const RuntimeCellPoint goal{104, 76}, gateCell{103, 76};
        // A genuine no-route search must exhaust this small origin component
        // within the unchanged retail runtime's 2048-node search budget.
        // The large 100-cell rally fixture intentionally permits partial paths
        // at that budget; it remains unchanged in the eighteen main scenes.
        if (s.world.getTerrainCells().size() != 1960) return fail("compact real-TMP courtyard dimensions changed");
        // Original 1x1 foundations make a courtyard whose destination remains
        // empty and passable. Its west opening is closed by a genuinely paid
        // sidebar building only after the ordinary Move has been accepted.
        add(s, 200, "GAPILE", WorldObjectKind::Structure, {99, 80});
        int ringId = 210;
        for (int y = 75; y <= 77; ++y) for (int x = 103; x <= 105; ++x) {
            if (RuntimeCellPoint{x, y} != goal && RuntimeCellPoint{x, y} != gateCell)
                add(s, ringId++, "GAPILL", WorldObjectKind::Structure, {x, y});
        }
        // A real fixed foundation stops the first failed route's eastward
        // avoidance step. Native ROT then gives a full-tick failed/Cooldown
        // boundary; no behavior state or navigation route is injected.
        add(s, 220, "GAPILL", WorldObjectKind::Structure, {82, 76});
        initializeRuntimeNavigation(s); completeRuntimeWorldRefresh(s); rebuildRuntimeEconomyPower(s);
        const auto catalogItem = [&](const char* id) -> std::optional<RuntimeBuildCatalogItem> {
            const auto it = std::find_if(s.economy.catalog.begin(), s.economy.catalog.end(),
                [&](const auto& item) { return item.typeId == id; });
            return it == s.economy.catalog.end() ? std::nullopt : std::optional<RuntimeBuildCatalogItem>(*it);
        };
        const auto car = catalogItem("MTNK"), pillar = catalogItem("GAPILL");
        const auto* pillarType = findRuntimeBuildingTypeById(s.rules, "GAPILL");
        const auto* barracks = findObjectById(s, 200);
        if (!car || !pillar || !pillarType || !barracks
            || runtimeStructureFootprintForBuildItem(s, *pillar).width != 1
            || runtimeStructureFootprintForBuildItem(s, *pillar).height != 1
            || pillarType->adjacent != std::stoi(stock.getValue("GAPILL", "Adjacent", "0"))
            || !runtimeBuildingPlacementAnchorEligible(s, *barracks, "Commander")
            || !runtimeBuildingPlacementInRange(s, *pillar, gateCell)
            || !runtimeCellCanPlaceBuilding(s, *pillar, gateCell)
            || !runtimeBuildAvailability(s, *pillar).canQueue)
            return fail("original GAPILE prerequisite / GAPILL foundation or adjacency unavailable");
        const int creditsAtStart = s.economy.credits;
        stage = "genuine paid Ready GAPILL";
        if (!dispatchRuntimeQueueProductionFromUi(s, *pillar)) return fail("real defense queue rejected: " + s.interactionStatus);
        for (int n = 0; n < hz * 3 && !runtimeReadyProductionForItem(s, *pillar); ++n) {
            step(s); if (!fixtureFailure.empty()) return fail(fixtureFailure);
        }
        if (!runtimeReadyProductionForItem(s, *pillar) || s.economy.credits != creditsAtStart - pillar->cost)
            return fail("original one paid defense order did not become Ready");
        const auto spawn = findRuntimeProductionSpawnForOwner(s, *car, "Commander", false);
        if (!spawn || !spawn->anchor || spawn->anchor->instanceId != 1) return fail("blocked fixture original factory door missing");
        const RuntimeCellPoint door = spawn->cell; const auto outward = runtimeProductionOutwardCell(s, *spawn->anchor, door);
        if (!outward || *outward != RuntimeCellPoint{81, 76}) return fail("blocked fixture original outward geometry changed");
        applySelection(s, {1}, false, false);
        if (!dispatchRuntimePlayerFactoryFromUi(s, 1, RuntimeMultiplayerGameplayCell{goal.x, goal.y})
            || !dispatchRuntimeQueueProductionFromUi(s, *car)) return fail("real first rally / vehicle queue rejected");
        std::set<int> produced, released; int firstId = -1, gateId = -1;
        int pendingStart = -1, maxPending = 0; std::uint64_t pendingSerial = 0;
        const auto advanceAndInspect = [&]() {
            struct Previous { Pose p; std::optional<RuntimeObjectBehavior> move; bool lease; };
            std::map<int, Previous> before;
            for (const auto& object : s.world.getObjects()) if (object.alive && object.kind == WorldObjectKind::Unit) {
                const auto b = s.behaviors.find(object.instanceId);
                before.emplace(object.instanceId, Previous{pose(object), b == s.behaviors.end()
                    ? std::nullopt : std::optional<RuntimeObjectBehavior>(b->second), ownsLease(s, object.instanceId)});
            }
            step(s); if (!fixtureFailure.empty()) return fail(fixtureFailure);
            for (const auto& object : s.world.getObjects()) {
                if (!object.alive || object.kind != WorldObjectKind::Unit || object.owner != "Commander") continue;
                if (const auto old = before.find(object.instanceId); old != before.end()
                    && !inspect(s, object.instanceId, c, old->second.p,
                        old->second.move ? &*old->second.move : nullptr, old->second.lease, true)) return false;
                if (object.instanceId == 900) continue;
                if (object.type != "MTNK" || object.health != runtimeMaxHealthForObject(s, object))
                    return fail("blocked flow created a non-stock or damaged vehicle");
                if (produced.insert(object.instanceId).second) {
                    if (produced.size() > 4) return fail("one real blocked-flow production order spawned duplicate vehicles");
                    ++producedCount; if (firstId < 0) firstId = object.instanceId;
                    if (!ownsLease(s, object.instanceId) || pose(object).x != door.x || pose(object).y != door.y)
                        return fail("blocked flow did not originate at its genuine leased doorway");
                }
                if (!ownsLease(s, object.instanceId)) released.insert(object.instanceId);
            }
            for (int a : produced) for (int b : produced) if (a < b) {
                const auto* x = findObjectById(s, a); const auto* y = findObjectById(s, b);
                if (!x || !y || (x->x == y->x && x->y == y->y)
                    || std::hypot(pose(*x).x - pose(*y).x, pose(*x).y - pose(*y).y) < 0.1)
                    return fail("blocked-flow real vehicles overlap");
            }
            const auto& lane = s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Vehicles)];
            if ((runtimeActiveProductionForItem(s, *car) != nullptr) != lane.active.has_value()
                || runtimeReadyProductionForItem(s, *car) || runtimeQueuedProductionCountForItem(s, *car) != int(lane.queued.size()))
                return fail("blocked-flow actual vehicle cameo state stale");
            ++sidebarChecks;
            if (lane.active && lane.active->remainingTicks == 0) {
                if (pendingSerial != lane.active->serial) { pendingSerial = lane.active->serial; pendingStart = s.tick; }
                maxPending = std::max(maxPending, s.tick - pendingStart + 1);
                if (maxPending > hz * 2) return fail("blocked-flow completed vehicle exceeds original two-second egress gate: "
                    + diagnostic(s, firstId));
            } else { pendingSerial = 0; pendingStart = -1; }
            return true;
        };
        stage = "first physical release then actual Stop / Move";
        for (int n = 0; n < hz * 4 && (firstId < 0 || !released.contains(firstId)); ++n)
            if (!advanceAndInspect()) return false;
        const auto* first = findObjectById(s, firstId);
        if (!first || first->renderMoveActive || pose(*first).x != outward->x || pose(*first).y != outward->y)
            return fail("first real lease did not release at its continuous outward endpoint");
        applySelection(s, {firstId}, false, false);
        if (!dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop)
            || s.playerCommands.queues.contains(firstId)
            || !dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, goal))
            return fail("real Stop did not cancel old automatic route or new player Move rejected");
        if (!s.behaviors.contains(firstId) || s.behaviors.at(firstId).moveTarget != goal)
            return fail("accepted open-courtyard Move changed its actual destination");
        // At 23 cells this original command queues its path in the movement
        // stage, rather than the short-command greedy branch. All stages still
        // run through advanceRuntimeSimulation; there is no path-only probe.
        if (!advanceAndInspect() || !s.behaviors.at(firstId).pathRequestPending
            || findObjectById(s, firstId)->renderMoveActive) return fail("long Move did not naturally queue before closure");

        stage = "actual paid sidebar placement closes reachable courtyard";
        HWND window = CreateWindowExW(0, L"STATIC", L"Factory blocked rally", WS_POPUP,
            0, 0, 1280, 720, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) return fail("actual placement input window unavailable");
        struct Cleanup { HWND window; ~Cleanup() { SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (GetCapture() == window) ReleaseCapture(); DestroyWindow(window); } } cleanup{window};
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        const RECT client{0, 0, 1280, 720}; const RECT viewport = runtimeMapViewport(client), sidebar = runtimeSidebarRect(client);
        s.activeBuildTab = RuntimeBuildKind::Defense; std::optional<POINT> readyButton;
        const auto click = [&](POINT p) {
            runtimeWindowProc(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
            runtimeWindowProc(window, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
        };
        for (int page = 0; page < runtimeBuildPageCount(s, sidebar) && !readyButton; ++page) {
            runtimeSetActiveBuildPage(s, page); const auto visible = runtimeVisibleBuildCatalogIndices(s, sidebar);
            for (std::size_t n = 0; n < visible.size(); ++n) if (s.economy.catalog[visible[n]].typeId == "GAPILL") {
                const auto rect = runtimeBuildButtonRect(sidebar, int(n));
                readyButton = POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}; break;
            }
        }
        if (!readyButton) return fail("original paid Ready GAPILL cameo missing");
        click(*readyButton);
        if (!runtimeSelectedPlacement(s)) return fail("real Ready cameo did not enter placement: " + s.interactionStatus);
        centerRuntimeCameraOnCell(s, client, gateCell); ensureRuntimeCameraForViewport(s, viewport);
        POINT gatePoint = runtimeScreenPointForCell(s, viewport, gateCell, terrainLevelAtRuntimeCell(s, gateCell.x, gateCell.y));
        bool pickedGate = false;
        for (int dy = -8; dy <= 8 && !pickedGate; ++dy) for (int dx = -8; dx <= 8 && !pickedGate; ++dx) {
            const POINT p{gatePoint.x + dx, gatePoint.y + dy}; const POINT picked = approximateCellFromScreenPoint(s, viewport, p);
            if (pointInRectInclusive(viewport, p) && picked.x == gateCell.x && picked.y == gateCell.y) { gatePoint = p; pickedGate = true; }
        }
        if (!pickedGate) return fail("actual map placement cannot pick original gate cell");
        click(gatePoint);
        for (const auto& object : s.world.getObjects()) if (object.alive && object.type == "GAPILL"
            && object.x == gateCell.x && object.y == gateCell.y) gateId = object.instanceId;
        if (gateId < 0 || runtimeReadyProductionForItem(s, *pillar) || runtimeSelectedPlacement(s))
            return fail("actual gate placement rejected: " + s.interactionStatus + " / " + s.economy.status);
        int sealedFoundations = 0;
        for (int y = 75; y <= 77; ++y) for (int x = 103; x <= 105; ++x) {
            if (RuntimeCellPoint{x, y} == goal) continue;
            if (!runtimeCellHasHardWholeCellBlocker(s, {x, y}, firstId, -1, {}))
                return fail("actual built courtyard foundation left an open navigation cell: "
                    + std::to_string(x) + ',' + std::to_string(y));
            ++sealedFoundations;
        }
        if (sealedFoundations != 8 || runtimeCellHasHardWholeCellBlocker(s, goal, firstId, -1, {}))
            return fail("real courtyard target must stay empty inside eight solid original foundations");

        stage = "full-tick genuine failed path / native avoidance turn";
        bool failedPath = false;
        for (int n = 0; n < hz * 2 && !failedPath; ++n) {
            if (!advanceAndInspect()) return false;
            auto b = s.behaviors.find(firstId); const auto* object = findObjectById(s, firstId);
            failedPath = b != s.behaviors.end() && b->second.moveTarget == goal && b->second.blockedTicks > 0
                && b->second.path.empty() && !b->second.pathRequestPending && b->second.moveCooldownTicks == 0
                && (b->second.state == RuntimeBehaviorState::Blocked || b->second.state == RuntimeBehaviorState::Cooldown)
                && s.lastPathQueueStats.realNoPathResults > 0 && s.lastPathQueueStats.budgetExhaustedResults == 0
                && object && !object->renderMoveActive && pose(*object).x == outward->x && pose(*object).y == outward->y;
        }
        if (!failedPath) return fail("real closure did not produce a stationary failed ordinary Move: "
            + diagnostic(s, firstId) + " status=" + s.simulationStatus + " expanded="
            + std::to_string(s.lastPathQueueStats.lastPumpExpandedNodes) + " budgetResults="
            + std::to_string(s.lastPathQueueStats.budgetExhaustedResults) + " realNoPath="
            + std::to_string(s.lastPathQueueStats.realNoPathResults) + " pathBack="
            + (s.behaviors.at(firstId).path.empty() ? std::string("empty")
                : std::to_string(s.behaviors.at(firstId).path.back().x) + ',' + std::to_string(s.behaviors.at(firstId).path.back().y)));
        std::cout << "Factory blocked native failure " << label << ' ' << diagnostic(s, firstId)
            << " blockedTicks=" << s.behaviors.at(firstId).blockedTicks << " status=" << s.simulationStatus
            << " terrain=" << s.world.getTerrainCells().size() << " solidRing=" << sealedFoundations
            << " expanded=" << s.lastPathQueueStats.lastPumpExpandedNodes
            << " budgetResults=" << s.lastPathQueueStats.budgetExhaustedResults
            << " realNoPath=" << s.lastPathQueueStats.realNoPathResults << '\n';
        for (int n = 0; n < 3; ++n) if (!dispatchRuntimeQueueProductionFromUi(s, *car))
            return fail("real subsequent production pressure queue rejected");
        bool yielded = false, opened = false, restoredGoal = false, saved = false, finished = false;
        std::vector<Replica> replicas;
        const auto preservedHead = [&](const RuntimeWindowState& state) {
            const auto q = state.playerCommands.queues.find(firstId);
            if (q == state.playerCommands.queues.end() || q->second.owner != "commander" || q->second.orders.size() != 1) return false;
            const auto& order = q->second.orders.front(); const auto* target = std::get_if<RuntimeMultiplayerGameplayCellTarget>(&order.target);
            return order.order == RuntimeMultiplayerGameplayTargetOrderKind::Move && order.objectIds == std::vector<std::int32_t>{firstId}
                && target && target->cell == RuntimeMultiplayerGameplayCell{goal.x, goal.y};
        };
        stage = "real production pressure preserves failed target before short yield";
        for (int n = 0; n < hz * 40; ++n) {
            if (!advanceAndInspect()) return false;
            for (auto it = replicas.begin(); it != replicas.end();) {
                step(*it->state);
                if (!compare(s, *it->state, firstId, it->exact)
                    || s.playerCommands.serialize() != it->state->playerCommands.serialize())
                    return fail("saved automatic yield lost its original front Move or exact continuation");
                if (s.tick >= it->until) it = replicas.erase(it); else ++it;
            }
            auto b = s.behaviors.find(firstId); const auto* object = findObjectById(s, firstId);
            if (!object) return fail("original failed car disappeared");
            if (b != s.behaviors.end() && b->second.factoryExitClearanceHostId == 1) {
                if (!preservedHead(s) || b->second.moveTarget == goal)
                    return fail("short automatic yield consumed, duplicated or replaced the real original destination");
                yielded = true;
                if (!opened) {
                    const Pose beforeSale = pose(*object); const int beforeRefund = s.economy.credits;
                    if (!dispatchRuntimeStructureToolFromUi(s, RuntimeSidebarTool::Sell, gateId)
                        || findObjectById(s, gateId) || pose(*findObjectById(s, firstId)) != beforeSale)
                        return fail("real gate sale did not reopen without teleporting the preserved car");
                    const int refund = runtimeBuildingSaleRefund(pillarType, s.rules.getRefundPercent());
                    if (s.economy.credits != beforeRefund + refund) return fail("real gate sale refund changed original paid accounting");
                    opened = true;
                    // Exit finalization/navigation rebuilding can replace
                    // transient containers. Reacquire after the real sale.
                    b = s.behaviors.find(firstId); object = findObjectById(s, firstId);
                    if (b == s.behaviors.end() || !object || b->second.factoryExitClearanceHostId != 1)
                        return fail("actual sale erased the active preserved-target yield");
                }
                if (!saved && b->second.renderMoveTicksRemaining > 2
                    && b->second.renderMoveTicksRemaining < b->second.renderMoveTicksTotal) {
                    for (const bool exact : {false, true}) {
                        auto restored = checkpoint(s, exact);
                        if (!restored || !compare(s, *restored, firstId, exact) || !preservedHead(*restored))
                            return fail("real preserved-front automatic yield checkpoint rejected or changed route: " + error);
                        replicas.push_back({std::move(restored), firstId, s.tick + (exact ? hz * 2 : b->second.renderMoveTicksRemaining - 1), exact});
                        if (exact) ++exactSaves; else ++ordinarySaves;
                    }
                    saved = true;
                }
            } else if (yielded && b != s.behaviors.end() && b->second.kind == RuntimeBehaviorKind::Move
                && b->second.moveTarget == goal && b->second.factoryExitClearanceHostId == -1) {
                restoredGoal = true;
                if (s.playerCommands.queues.contains(firstId)) return fail("restored ordinary target remained duplicated in queue");
            }
            const auto& lane = s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Vehicles)];
            if (produced.size() == 4 && released.size() == 4 && lane.queued.empty() && !lane.active && !lane.ready
                && s.factoryDeliveries.leases().empty() && replicas.empty() && restoredGoal && !object->renderMoveActive
                && (b == s.behaviors.end() || b->second.state == RuntimeBehaviorState::Complete)
                && std::hypot(pose(*object).x - goal.x, pose(*object).y - goal.y) <= 1.5) { finished = true; break; }
        }
        const int expectedCredits = creditsAtStart - pillar->cost - car->cost * 4
            + runtimeBuildingSaleRefund(pillarType, s.rules.getRefundPercent());
        const auto* fixedFoundation = findObjectById(s, 220);
        if (!finished || !failedPath || !yielded || !opened || !restoredGoal || !saved
            || s.economy.credits != expectedCredits || !fixedFoundation || fixedFoundation->health != pillarType->strength)
            return fail("incomplete real failed target / yield / preserved head / paid natural arrival: " + diagnostic(s, firstId));
        std::cout << "Factory blocked metrics " << label << " vehicles=" << produced.size()
            << " maxCompletedWaitTicks=" << maxPending << " originalGoal=" << goal.x << ',' << goal.y
            << " genuineFullTickFailure/preservedHead/ordinary+canonical/realSale/nativeArrival=yes\n";
        return true;
    };
    for (const auto& c : cases) for (const int hz : {30, 60}) {
        for (const bool longRally : {false, true}) scene(std::string(c.factory) + '-' + c.unit + '-' + std::to_string(hz)
            + "-fast-" + (longRally ? "rally100" : "default"), [&] { return run(c, hz, true, longRally); });
        scene(std::string(c.factory) + '-' + c.unit + '-' + std::to_string(hz) + "-normal-default", [&] { return run(c, hz, false, false); });
    }
    for (const int hz : {30, 60}) scene("GAWEAP-MTNK-" + std::to_string(hz) + "-fast-blocked-rally",
        [&] { return runBlockedRally(hz); });
    if (!filter.empty() && sceneCount == 0 && failures.empty()) { error = "Factory egress: requested case not found: " + filter; return false; }
    std::cout << "Factory egress: " << sceneCount << " scenes, " << producedCount << " genuine vehicles, " << motionChecks
        << " continuous poses, " << linearChecks << " independent linear steps, " << cadenceChecks << " original-Speed clocks, "
        << clearanceSegments << " automatic clearance frames, " << outsideChecks << " outside original-Speed clocks, " << terminalBrakes
        << " native terminal-brake frames, " << sidebarChecks << " real cameo-state checks, " << ordinarySaves << '+' << exactSaves
        << " saves, " << pairedTicks << " strict paired ticks, " << codecChecks << " marker codec checks, "
        << gateChecks << " local-only/source-loss gates, " << captures << " native BMPs\n";
    if (!failures.empty()) { std::ostringstream out; out << failures.size() << " factory egress scenes failed";
        for (const auto& item : failures) out << '\n' << item; error = out.str(); return false; }
    error.clear(); return true;
}
