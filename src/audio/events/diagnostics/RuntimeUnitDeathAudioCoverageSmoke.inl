// Native rules, real exit producers and real sample resolution.  The oracle
// reads the original INI declarations, never the production death-audio helper.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeUnitDeathAudioCoverageSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string label = "startup";
    const char* selected = std::getenv("CONCORD_UNIT_DEATH_AUDIO_CASE");
    const auto fail = [&](const std::string& why) {
        error = "Unit death audio coverage: " + label + ": " + why;
        return false;
    };
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config; assets->config.playerSlots.clear();
    assets->config.gameModeRulesOverride.clear();
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world,
        &assets->theater, &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("original resources unavailable");
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    IniFile raw, rawArt;
    if (!raw.loadFromFile(assets->report.rulesPath)
        || !rawArt.loadFromFile(assets->report.artPath)) return fail("original INIs unavailable");
    const auto tokens = [](const std::string& declaration) {
        std::vector<std::string> result;
        std::istringstream input(declaration); std::string token;
        while (std::getline(input, token, ',')) {
            const auto first = token.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            token = token.substr(first, token.find_last_not_of(" \t\r\n") - first + 1);
            std::string lower = token;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower != "none" && lower != "<none>" && lower != "-1") result.push_back(token);
        }
        return result;
    };
    const auto flag = [&](const std::string& id, const char* key, bool fallback = false) {
        auto value = raw.getValue(id, key, fallback ? "yes" : "no");
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value == "yes" || value == "true" || value == "1";
    };
    const auto number = [&](const std::string& id, const char* key, double fallback) {
        const auto value = raw.getValue(id, key, "");
        if (value.empty()) return fallback;
        try { return std::stod(value); } catch (...) { return fallback; }
    };
    struct NativeType { std::string id; WorldObjectKind kind; bool aircraft; };
    std::vector<NativeType> types;
    std::set<std::string> deathSections{"GenVehicleDie"};
    int infantry = 0, vehicles = 0, aircraft = 0;
    bool rawDeclarationsMatch = true;
    std::string rawMismatchId;
    const auto collectTypes = [&](const std::vector<UnitType>& definitions, WorldObjectKind kind, bool air) {
        for (const auto& definition : definitions) {
            if (!raw.getSection(definition.id)) {
                // A name such as APACHE/YDUM in the original list has no type
                // declaration.  Report it explicitly; it is not a stock unit.
                std::cout << "Unit death audio inventory: unconfigured list entry " << definition.id << '\n';
                continue;
            }
            types.push_back({definition.id, kind, air});
            const auto& id = definition.id;
            const bool declarationMatches = definition.dieSound == raw.getValue(id, "DieSound", "")
                && definition.voiceCrashing == raw.getValue(id, "VoiceCrashing", "")
                && definition.crashingSound == raw.getValue(id, "CrashingSound", "")
                && definition.sinkingSound == raw.getValue(id, "SinkingSound", raw.getValue("AudioVisual", "SinkingSound", ""))
                && definition.impactLandSound == raw.getValue(id, "ImpactLandSound", raw.getValue("AudioVisual", "ImpactLandSound", ""))
                && definition.impactWaterSound == raw.getValue(id, "ImpactWaterSound", raw.getValue("AudioVisual", "ImpactWaterSound", ""))
                && std::abs(definition.weight - number(id, "Weight", 1.0)) < 1e-8
                && std::abs(definition.shipSinkingWeight - number("General", "ShipSinkingWeight", 3.0)) < 1e-8
                && definition.naval == flag(id, "Naval") && definition.underwater == flag(id, "Underwater")
                && definition.organic == flag(id, "Organic", kind == WorldObjectKind::Infantry)
                && definition.crashable == flag(id, "Crashable") && definition.missileSpawn == flag(id, "MissileSpawn");
            if (!declarationMatches && rawMismatchId.empty()) rawMismatchId = id;
            rawDeclarationsMatch = declarationMatches && rawDeclarationsMatch;
            if (kind == WorldObjectKind::Infantry) ++infantry; else if (air) ++aircraft; else ++vehicles;
            for (const char* field : {"DieSound", "VoiceCrashing", "CrashingSound", "SinkingSound", "CrushSound"})
                for (const auto& sound : tokens(raw.getValue(definition.id, field, ""))) deathSections.insert(sound);
        }
    };
    collectTypes(assets->rules.getInfantryTypes(), WorldObjectKind::Infantry, false);
    collectTypes(assets->rules.getVehicleTypes(), WorldObjectKind::Unit, false);
    collectTypes(assets->rules.getAircraftTypes(), WorldObjectKind::Unit, true);
    if (!rawDeclarationsMatch) return fail("raw death/movement/default declarations differ from parsed native rules: " + rawMismatchId);
    if (infantry < 60 || vehicles < 75 || aircraft < 10) return fail("incomplete native type inventory");
    for (const std::string id : {"120mm", "Medusa"}) {
        const auto* weapon = assets->rules.findWeaponType(id);
        if (!weapon || weapon->damage != static_cast<int>(number(id, "Damage", -1))
            || weapon->warhead != raw.getValue(id, "Warhead", "") || weapon->projectile != raw.getValue(id, "Projectile", ""))
            return fail("native ordinary-hit weapon oracle changed: " + id);
    }
    int waterTile = -1;
    for (int tile = 0; tile < 65535; ++tile) {
        TerrainCell probe; probe.tileIndex = tile;
        const auto info = runtimeTerrainInfoForCell(*assets, probe);
        if (info.water && !info.bridge && !info.shore && !info.cliff) { waterTile = tile; break; }
    }
    if (waterTile < 0) return fail("original water tile unavailable");
    const auto fixture = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.gameModeRulesOverride.clear(); s.config.playerCountry = "Americans";
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.crates = s.config.shortGame = s.config.noCooldown = false;
        s.rules = assets->rules; s.art = assets->art; s.theater = assets->theater; s.report = assets->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.mapFacts = {}; s.mapFactsLoaded = false;
        s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimeSoundEffectsEnabled = true; s.runtimeSoundVolume = 100;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true; s.economy.credits = 10000;
        s.triggers.loaded = s.triggers.bootstrapComplete = true; s.tick = 100;
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = x >= 16 ? waterTile : 0; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeGarrisonsInitialized(s); ensureRuntimeSubcellsInitialized(s);
        return box;
    };
    const auto add = [&](RuntimeWindowState& s, const std::string& type, int id, int x = 8, int y = 8,
                         const std::string& owner = "Russians") {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "DeathAudio" + std::to_string(id);
        object.type = type; object.owner = owner;
        object.kind = s.rules.findBuildingType(type) ? WorldObjectKind::Structure
            : s.rules.findUnitType("InfantryType", type) ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        object.authoredAircraft = s.rules.findUnitType("AircraftType", type) != nullptr;
        object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.alive = object.poweredOperational = true; object.health = std::max(1, runtimeMaxHealthForObject(s, object));
        object.currentAmmo = -1; object.mission = "Sleep"; object.facing = object.turretFacing = 64;
        object.hasRenderPosition = true; object.renderX = x + .125; object.renderY = y + .25; object.renderLevel = 0;
        s.world.accessObjects().push_back(object);
        invalidateRuntimeObjectProjectionCache(s); invalidateRuntimeTickOccupancyCache(s);
        return id;
    };
    const auto listen = [&](RuntimeWindowState& s, const RuntimeSpatialAudioPosition& position) {
        if (!loadRuntimeSoundDatabase(s)) return false;
        s.cameraClampViewport = {0, 0, 640, 400}; s.camera.initialized = true;
        const auto center = runtimeProjectCellXY(position.x, position.y, position.level, s.camera);
        s.camera.worldOffsetX = center.x - 320; s.camera.worldOffsetY = center.y - 200;
        s.runtimeSoundIntentHistory.clear(); s.runtimeSoundPlaybackHistory.clear();
        return true;
    };
    using Records = std::vector<RuntimeSoundPlaybackRecord>;
    const auto gather = [](RuntimeWindowState& s, Records& records) {
        records.insert(records.end(), s.runtimeSoundPlaybackHistory.begin(), s.runtimeSoundPlaybackHistory.end());
        s.runtimeSoundPlaybackHistory.clear();
    };
    const auto samePosition = [](const auto& record, const RuntimeSpatialAudioPosition& position) {
        return record.spatial && std::abs(record.sourcePosition.x - position.x) < 1e-8
            && std::abs(record.sourcePosition.y - position.y) < 1e-8
            && std::abs(record.sourcePosition.level - position.level) < 1e-8;
    };
    const auto objectPosition = [](const WorldObjectInstance& object) {
        // Independent expected input from the saved object pose, rather than
        // the production spatial-audio adapter that the emitter also calls.
        return RuntimeSpatialAudioPosition{object.hasRenderPosition ? object.renderX : double(object.x),
            object.hasRenderPosition ? object.renderY : double(object.y),
            object.renderMoveActive ? object.renderLevel : double(object.level)};
    };
    struct Cue { std::vector<std::string> sections; bool trackCamera = false; };
    const auto nativeCues = [&](const NativeType& type, bool airborne, bool water) {
        std::vector<Cue> result;
        const bool missile = flag(type.id, "MissileSpawn");
        if (missile) return result;
        const bool sinking = water && flag(type.id, "Naval") && !flag(type.id, "Underwater")
            && !flag(type.id, "Organic", type.kind == WorldObjectKind::Infantry)
            && number(type.id, "Weight", 1.0) >= number("General", "ShipSinkingWeight", 3.0) && !airborne;
        const bool crashing = airborne && (type.aircraft || flag(type.id, "Crashable"));
        const std::string locomotor = raw.getValue(type.id, "Locomotor", "");
        const bool airMovement = type.aircraft || flag(type.id, "JumpJet")
            || locomotor.find("92612") != std::string::npos
            || raw.getValue(type.id, "MovementZone", "") == "Fly";
        const auto cue = [&](const char* field, bool track) {
            auto choices = tokens(raw.getValue(type.id, field, ""));
            if (!choices.empty()) result.push_back({std::move(choices), track});
        };
        if (sinking) {
            auto choices = tokens(raw.getValue(type.id, "SinkingSound", raw.getValue("AudioVisual", "SinkingSound", "")));
            if (!choices.empty()) result.push_back({std::move(choices), true});
            return result;
        }
        if (crashing) { cue("CrashingSound", true); cue("VoiceCrashing", true); return result; }
        if (airMovement) cue("VoiceCrashing", true);
        const auto declaration = raw.getValue(type.id, "DieSound", "");
        cue("DieSound", false);
        // Keep the existing vehicle default, but no infantry or MissileSpawn
        // default. Explicit sentinels also remain intentionally silent.
        if (declaration.empty() && type.kind == WorldObjectKind::Unit) result.push_back({{"GenVehicleDie"}, false});
        return result;
    };
    const auto nativeType = [&](const std::string& id) {
        for (const auto& type : types) if (type.id == id) return type;
        return NativeType{id, WorldObjectKind::Structure, false};
    };
    int cases = 0, queueDeaths = 0, projectileDeaths = 0, causes = 0, samples = 0, duplicates = 0, wreckPhases = 0, impactCues = 0;
    const auto check = [&](const Records& records, const RuntimeSpatialAudioPosition& position,
                           const std::vector<Cue>& cues) {
        std::set<std::string> allowed;
        for (const auto& cue : cues) {
            allowed.insert(cue.sections.begin(), cue.sections.end());
            int count = 0;
            for (const auto& record : records) if (samePosition(record, position)
                && std::find(cue.sections.begin(), cue.sections.end(), record.section) != cue.sections.end()) {
                ++count;
                if (!record.playable || record.sample.empty() || record.path.empty() || record.spatialGain16384 <= 0)
                    return fail("unplayable death sample " + record.section + " status=" + record.status);
                if (record.trackCamera != cue.trackCamera) return fail("wrong camera policy " + record.section);
            }
            if (count != 1) {
                std::ostringstream why; why << "expected one [";
                for (const auto& choice : cue.sections) why << choice << ',';
                why << "] at " << position.x << ',' << position.y << ',' << position.level << " observed=" << count;
                for (const auto& record : records) why << " {" << record.section << ':' << record.sourcePosition.x
                    << ',' << record.sourcePosition.y << ',' << record.sourcePosition.level << ':' << record.status << '}';
                return fail(why.str());
            }
            ++samples;
        }
        for (const auto& record : records) if (samePosition(record, position) && deathSections.contains(record.section)
            && !allowed.contains(record.section)) return fail("unexpected ordinary death cue " + record.section);
        return true;
    };
    std::string lastImpactDiagnostic;
    const auto lethalProjectile = [&](RuntimeWindowState& s, int id, const std::string& warhead = "AP") {
        const auto* target = findObjectById(s, id);
        if (!target) { lastImpactDiagnostic = "missing target #" + std::to_string(id); return false; }
        // The current native hit gate classifies authored Fly/Air movement
        // as an air target even when its body is on the ground. Choose an
        // actual stock AA projectile for that domain, without changing it.
        const bool airTarget = runtimeObjectIsAirTarget(s, *target);
        const auto* nativeWeapon = s.rules.findWeaponType(airTarget ? "Medusa" : "120mm");
        const auto* nativeProjectile = nativeWeapon ? s.rules.findProjectileType(nativeWeapon->projectile) : nullptr;
        if (!nativeWeapon || !nativeProjectile) { lastImpactDiagnostic = "original weapon/projectile missing"; return false; }
        const int healthBefore = target->health;
        const auto position = runtimeProjectileTargetPosition(s, *target);
        RuntimeProjectileEffect hit; hit.actorId = nextRuntimeProjectileActorId(s); hit.targetObjectId = id;
        hit.owner = "Americans"; hit.toCell = hit.currentCell = position.cell; hit.toLevel = position.level;
        hit.currentX = position.x; hit.currentY = position.y; hit.currentZ = position.level;
        hit.hasTargetPosition = true; hit.targetX = position.x; hit.targetY = position.y; hit.targetZ = position.level;
        hit.weaponId = nativeWeapon->id; hit.projectileId = nativeProjectile->id;
        hit.damage = nativeWeapon->damage; hit.warheadId = warhead == "AP" ? nativeWeapon->warhead : warhead;
        hit.ag = nativeProjectile->ag; hit.aa = nativeProjectile->aa;
        hit.affectsAllies = hit.affectsEnemies = true;
        const bool canHit = runtimeProjectileCanHitTarget(s, hit, *target);
        std::ostringstream diagnostic;
        diagnostic << " weapon=" << hit.weaponId << " projectile=" << hit.projectileId << " warhead=" << hit.warheadId
            << " damage=" << hit.damage << " target=" << target->type << " airborne=" << target->airborne
            << " airDomain=" << airTarget << " MovementZone=" << raw.getValue(target->type, "MovementZone", "")
            << " SpeedType=" << raw.getValue(target->type, "SpeedType", "") << " Armor=" << raw.getValue(target->type, "Armor", "")
            << " AA=" << hit.aa << " AG=" << hit.ag << " nativeCanHit=" << canHit << " hpBefore=" << healthBefore;
        const bool applied = canHit && applyRuntimeProjectileImpact(s, hit);
        const auto* after = findRuntimeObjectByIdIncludingInactive(s, id);
        diagnostic << " applied=" << applied << " hpAfter=" << (after ? after->health : -1);
        lastImpactDiagnostic = diagnostic.str();
        return applied;
    };
    const auto repeated = [&](RuntimeWindowState& s, int id) {
        const auto events = s.runtimeSoundEvents;
        RuntimeObjectExitRequest again; again.objectId = id; again.tick = s.tick;
        if (queueRuntimeObjectExit(s, again) != RuntimeObjectExitQueueResult::AlreadyFinalized
            || !finalizeRuntimeObjectExit(s, again).duplicate || s.runtimeSoundEvents != events)
            return fail("repeated queue/direct exit replayed a cue");
        ++duplicates; return true;
    };
    const auto scene = [&](const std::string& name, const auto& run) {
        label = name;
        if (selected && name != selected) return true;
        if (!run()) return false;
        ++cases; std::cout << "Unit death audio scene: " << name << " PASS\n"; return true;
    };
    for (int hz : {30, 60}) for (const auto& type : types) {
        const auto locomotor = raw.getValue(type.id, "Locomotor", "");
        const bool air = type.aircraft || flag(type.id, "JumpJet") || locomotor.find("92612") != std::string::npos;
        for (bool grounded : {false, true}) {
            if (grounded && !air) continue;
            for (bool projectile : {false, true}) {
                const auto name = type.id + "/" + std::to_string(hz) + "/" + (grounded ? "grounded" : "native")
                    + "/" + (projectile ? "projectile" : "queue");
                if (!scene(name, [&]() {
                    auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
                    const bool water = flag(type.id, "Naval");
                    add(s, type.id, 1, water ? 18 : 8);
                    auto* victim = findMutableObjectById(s, 1);
                    victim->airborne = air && !grounded; victim->flightLevel = victim->level = victim->airborne ? 4 : 0;
                    victim->renderLevel = victim->level;
                    if (projectile) victim->health = 1; // Wounded native unit; rules and damage are unchanged.
                    const auto position = objectPosition(*victim);
                    if (!listen(s, position)) return fail("original sound bank");
                    Records records;
                    if (projectile) {
                        if (!lethalProjectile(s, 1)) return fail("actual lethal impact rejected" + lastImpactDiagnostic);
                        ++projectileDeaths;
                    } else {
                        RuntimeObjectExitRequest exit; exit.objectId = 1; exit.tick = s.tick;
                        exit.reason = "death-audio coverage queued lethal exit"; exit.suppressCrew = true;
                        if (queueRuntimeObjectExit(s, exit) != RuntimeObjectExitQueueResult::Queued
                            || !drainRuntimeObjectExitQueue(s)) return fail("queued Destroyed lifecycle rejected");
                        ++queueDeaths;
                    }
                    gather(s, records);
                    const auto* dead = findRuntimeObjectByIdIncludingInactive(s, 1);
                    if (!dead || dead->alive || dead->health != 0) return fail("native unit survived lethal route" + (projectile ? lastImpactDiagnostic : " queued Destroyed"));
                    const auto expected = nativeCues(type, air && !grounded, water);
                    if (!check(records, position, expected) || !repeated(s, 1)) return false;
                    // Preserve and advance the real wreck to its natural impact
                    // or sinking completion. Collect every tick before history's
                    // 32-record cap; birth cues must still be exactly once.
                    if (!s.wrecks.empty()) {
                        int ticks = 0;
                        const bool sinking = s.wrecks.front().sinking;
                        RuntimeSpatialAudioPosition landingPosition;
                        bool landingWater = false, landingHasGround = false;
                        while (!s.wrecks.empty() && ticks++ < hz * 45) {
                            auto nextPose = runtimeWreckObjectPose(s.wrecks.front(), s.tick + 1, hz);
                            const auto ground = s.terrainByCell.find(runtimeCellKey(nextPose.x, nextPose.y));
                            landingHasGround = ground != s.terrainByCell.end() && ground->second;
                            if (landingHasGround) {
                                const auto info = runtimeTerrainInfoForCell(s, *ground->second);
                                landingWater = info.water && !info.bridge;
                                nextPose.level = static_cast<int>(runtimeEffectiveTerrainLevel(*ground->second));
                                nextPose.renderLevel = nextPose.level;
                            }
                            landingPosition = objectPosition(nextPose);
                            ++s.tick; advanceRuntimeEffects(s); gather(s, records);
                        }
                        if (!s.wrecks.empty()) return fail("native wreck never completed");
                        if (!check(records, position, expected)) return false;
                        if (!sinking && landingHasGround) {
                            const char* field = landingWater ? "ImpactWaterSound" : "ImpactLandSound";
                            const auto choices = tokens(raw.getValue(type.id, field, raw.getValue("AudioVisual", field, "")));
                            if (!choices.empty()) {
                                if (!check(records, landingPosition, {{choices, false}})) return false;
                                ++impactCues;
                            }
                        }
                        ++wreckPhases;
                    }
                    return true;
                })) return false;
            }
        }
    }

    // Real producers below feed queue/drain themselves. The lowered victim HP
    // is fixture setup only, never a change to native weapons or warheads.
    for (int hz : {30, 60}) {
        const auto ordinaryCause = [&](const std::string& reason, const auto& produce,
                                       const std::function<void(RuntimeWindowState&)>& prepare = {}) {
            return scene("cause/" + reason + "/" + std::to_string(hz), [&]() {
                auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
                add(s, "MTNK", 1); findMutableObjectById(s, 1)->health = 1;
                if (prepare) prepare(s);
                const auto position = objectPosition(*findObjectById(s, 1));
                if (!listen(s, position)) return fail("original sound bank");
                if (!produce(s)) return fail("real producer rejected");
                (void)drainRuntimeObjectExitQueue(s); Records records; gather(s, records);
                if (findObjectById(s, 1)) return fail("producer did not kill target");
                if (!check(records, position, nativeCues(nativeType("MTNK"), false, false)) || !repeated(s, 1)) return false;
                ++causes; return true;
            });
        };
        if (!ordinaryCause("magbeam-fall", [&](RuntimeWindowState& s) {
            RuntimeMagBeamAction fall; fall.sourceObjectId = 99; fall.sourceOwner = "Americans";
            return applyRuntimeMagBeamFallDamage(s, *findMutableObjectById(s, 1), 1000, fall, "mag beam falling impact");
        })) return false;
        if (!ordinaryCause("campaign-destroy", [&](RuntimeWindowState& s) {
            destroyRuntimeCampaignObject(s, *findMutableObjectById(s, 1), "Americans", "campaign scripted destroy"); return true;
        })) return false;
        if (!ordinaryCause("bridge-collapse", [&](RuntimeWindowState& s) {
            const auto result = damageRuntimeBridgeAtCell(s, {8, 8}, 100000, 0, 99, "Americans");
            return result.hit && result.destroyed;
        }, [&](RuntimeWindowState& s) {
            auto* cell = const_cast<TerrainCell*>(s.terrainByCell.at(runtimeCellKey(8, 8)));
            cell->hasOverlay = true; cell->overlayIndex = 24; cell->overlayFrame = 0;
            cell->overlayType = cell->overlayImage = "BRIDGE1";
            auto* victim = findMutableObjectById(s, 1); victim->onBridge = victim->renderOnBridge = true;
            victim->level = 4; victim->renderLevel = 4;
            s.destroyableBridges = true;
        })) return false;
        if (!scene("cause/crush-exclusive/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "E1", 1); add(s, "MTNK", 2, 7, 8, "Americans");
            const auto position = objectPosition(*findObjectById(s, 1));
            if (!listen(s, position)) return fail("original sound bank");
            RuntimeVeterancyKillCredit credit;
            if (!destroyRuntimeObjectByCrush(s, 1, 2, "Americans", credit)) return fail("real crush rejected");
            Records records; gather(s, records);
            if (!check(records, position, {{tokens(raw.getValue("E1", "CrushSound", "")), false}})
                || !repeated(s, 1)) return false;
            ++causes; return true;
        })) return false;
        if (!scene("cause/radiation-site/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "E1", 1); findMutableObjectById(s, 1)->health = 1;
            const auto position = objectPosition(*findObjectById(s, 1));
            if (!listen(s, position)) return fail("original sound bank");
            if (s.specialMechanisms.spawnRadiationArea(99, "Americans", {8, 8}, 3, 500, hz * 10,
                    "RadSite", s.tick, true, nullptr) < 0) return fail("native radiation producer");
            Records records;
            for (int tick = 0; tick < hz * 12 && findObjectById(s, 1); ++tick) {
                ++s.tick; advanceRuntimeSpecialMechanisms(s); gather(s, records);
            }
            if (findObjectById(s, 1)) return fail("native radiation never lethal");
            if (!check(records, position, nativeCues(nativeType("E1"), false, false)) || !repeated(s, 1)) return false;
            ++causes; return true;
        })) return false;
        if (!scene("cause/mastermind-overload/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "MIND", 1, 8, 8, "Americans"); findMutableObjectById(s, 1)->health = 1;
            const auto position = objectPosition(*findObjectById(s, 1));
            if (!listen(s, position)) return fail("original sound bank");
            const auto* weapon = s.rules.findWeaponType("MultipleMindControlTank");
            if (!weapon || !weapon->infiniteMindControl) return fail("native controller weapon");
            for (int n = 0; n < 4; ++n) {
                add(s, "E2", 10 + n, 6 + n, 6);
                std::string reason;
                if (!s.specialMechanisms.tryMindControl(s.world.accessObjects(), s.rules, 1, 10 + n,
                        weapon->damage, false, true, false, s.tick, nullptr, nullptr, reason))
                    return fail("native fourth control " + reason);
            }
            Records records;
            for (int tick = 0; tick < hz * 12 && findObjectById(s, 1); ++tick) {
                ++s.tick; advanceRuntimeSpecialMechanisms(s); gather(s, records);
            }
            if (findObjectById(s, 1)) return fail("native overload never lethal");
            auto expected = nativeCues(nativeType("MIND"), false, false);
            const auto voice = tokens(raw.getValue("AudioVisual", "MasterMindOverloadDeathSound", ""));
            if (voice.empty()) return fail("original overload voice missing");
            expected.push_back({voice, false});
            if (!check(records, position, expected) || !repeated(s, 1)) return false;
            for (int n = 0; n < 4; ++n) if (!findObjectById(s, 10 + n)) return fail("overload killed a controlled survivor");
            ++causes; return true;
        })) return false;
        if (!scene("cause/repair-depot-host-loss/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "NADEPT", 1); add(s, "MTNK", 2, 7, 8);
            findMutableObjectById(s, 1)->health = 1; findMutableObjectById(s, 2)->health = 1;
            s.structureFootprints = buildRuntimeStructureFootprints(s); ensureRuntimeRepairDepotsInitialized(s);
            const auto reservation = s.repairDepots.reserveDock(makeRuntimeRepairDepotContext(s), 1, 2, s.tick);
            if (!reservation.accepted || !applyRuntimeRepairDepotActions(s, reservation.actions))
                return fail("native repair dock reservation");
            const auto* relation = findRuntimeRepairDepotRelationForUnit(s.repairDepots, 2);
            if (!relation) return fail("repair reservation relation missing");
            auto* vehicle = findMutableObjectById(s, 2);
            vehicle->x = relation->dockCell.x; vehicle->y = relation->dockCell.y;
            vehicle->cell = runtimeCellKey(vehicle->x, vehicle->y); clearRuntimeRenderPosition(*vehicle);
            if (completeRuntimeRepairDepotArrival(s, 2) != RuntimeRepairDepotArrivalResult::Attached)
                return fail("native repair dock arrival");
            const auto position = objectPosition(*findObjectById(s, 2));
            if (!listen(s, position)) return fail("original sound bank");
            if (!lethalProjectile(s, 1)) return fail("real depot lethal impact");
            Records records; gather(s, records);
            if (findObjectById(s, 2)) return fail("destroyed depot retained attached unit");
            if (!check(records, position, nativeCues(nativeType("MTNK"), false, false)) || !repeated(s, 2)) return false;
            ++causes; return true;
        })) return false;
        if (!scene("cause/temporal-continuous/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "E1", 1); add(s, "CLEG", 2, 7, 8, "Americans");
            if (!listen(s, objectPosition(*findObjectById(s, 1)))) return fail("original sound bank");
            initializeRuntimeOfflineGameplayRouting(s); s.selectedObjectIds = {2};
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary, {8, 8}, findObjectById(s, 1)))
                return fail("actual Chrono Legionnaire attack command");
            Records records; gather(s, records); bool sawContinuous = false;
            RuntimeSpatialAudioPosition position;
            for (int tick = 0; tick < hz * 12 && findObjectById(s, 1); ++tick) {
                const auto* target = findObjectById(s, 1);
                position = objectPosition(*target);
                const auto subCell = worldObjectSubCellOffset(*target);
                position.x += subCell.x; position.y += subCell.y;
                ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); gather(s, records);
                sawContinuous = sawContinuous || s.specialMechanisms.isTemporalFrozen(1);
            }
            if (findObjectById(s, 1) || !sawContinuous) return fail("native continuous Temporal erasure never completed");
            const auto animation = raw.getValue("General", "WarpAway", "WARPAWAY");
            const auto report = tokens(rawArt.getValue(animation, "Report", ""));
            if (report.empty()) return fail("original WarpAway Report missing");
            if (!check(records, position, {{report, true}}) || !repeated(s, 1)) return false;
            for (const auto& record : records) if (deathSections.contains(record.section))
                return fail("Temporal erasure emitted an ordinary death cue " + record.section);
            ++causes; return true;
        })) return false;
        if (!scene("cause/moving-victim/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "MTNK", 1, 8, 8, "Americans");
            // Start from the original logical cell; the following fractional
            // pose must come from real native movement, not manual assignment.
            clearRuntimeRenderPosition(*findMutableObjectById(s, 1));
            initializeRuntimeOfflineGameplayRouting(s); s.selectedObjectIds = {1};
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, {12, 10}))
                return fail("actual native Move command rejected");
            bool moving = false;
            for (int tick = 0; tick < hz * 4 && !moving; ++tick) {
                ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
                const auto* target = findObjectById(s, 1);
                if (!target) return fail("moving victim vanished before lethal hit");
                moving = target->renderMoveActive && target->hasRenderPosition
                    && (std::abs(target->renderX - target->x) > 1e-4 || std::abs(target->renderY - target->y) > 1e-4);
            }
            if (!moving) return fail("actual movement did not expose a continuous pose");
            const auto position = objectPosition(*findObjectById(s, 1));
            findMutableObjectById(s, 1)->health = 1;
            if (!listen(s, position) || !lethalProjectile(s, 1)) return fail("moving victim actual lethal impact");
            Records records; gather(s, records);
            if (!check(records, position, nativeCues(nativeType("MTNK"), false, false)) || !repeated(s, 1)) return false;
            ++causes; return true;
        })) return false;
        if (!scene("silent/nonlethal-hit/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "MTNK", 1); const auto before = findObjectById(s, 1)->health;
            const auto position = objectPosition(*findObjectById(s, 1));
            if (!listen(s, position) || !lethalProjectile(s, 1)) return fail("native nonlethal control impact");
            const auto* after = findObjectById(s, 1);
            if (!after || after->health <= 0 || after->health >= before) return fail("native AP hit was not a surviving damage control");
            Records records; gather(s, records);
            if (!check(records, position, {})) return false;
            ++causes; return true;
        })) return false;
        for (const std::string mechanism : {"parasite-bite", "parasite-cull", "parasite-cleanse"}) {
            if (!scene("cause/" + mechanism + "/" + std::to_string(hz), [&]() {
                auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
                const bool culling = mechanism == "parasite-cull";
                const std::string hostType = culling ? "HYD" : "MTNK";
                const std::string sourceType = culling ? "SQD" : "DRON";
                add(s, hostType, 1, culling ? 18 : 8); add(s, sourceType, 2, culling ? 17 : 7, 8, "Americans");
                auto* host = findMutableObjectById(s, 1);
                // DRON's original Parasite has no Culling. SQD's original
                // ParasitePlus does; use the two real authored weapons.
                // An inorganic DRON applies the original 50-damage first
                // bite in the same effects tick as attachment. A one-HP host
                // can therefore lose its real link before the observer sees
                // it. Keep stock health for the bite and let every bite run.
                host->health = mechanism == "parasite-cleanse" ? 200 : culling ? 10 : runtimeMaxHealthForObject(s, *host);
                const auto position = objectPosition(*host);
                if (!listen(s, position)) return fail("original sound bank");
                const auto* source = findObjectById(s, 2);
                const auto profile = runtimeWeaponProfileForObject(s, *source, host);
                if (!profile.weapon || !launchRuntimeParasiteProjectile(s, *source, *host, *profile.weapon, profile))
                    return fail("native DroneJump launch");
                Records records; gather(s, records);
                for (int tick = 0; tick < hz * 10 && !s.specialMechanisms.parasiteLinks().contains(1); ++tick) {
                    ++s.tick; advanceRuntimeEffects(s); gather(s, records);
                }
                if (!s.specialMechanisms.parasiteLinks().contains(1)) {
                    std::ostringstream why; why << "natural parasite attachment never completed tick=" << s.tick
                        << " status=" << s.simulationStatus << " pending=" << s.specialMechanisms.pendingParasiteLaunches().size();
                    for (int id : {1, 2}) {
                        const auto* object = findRuntimeObjectByIdIncludingInactive(s, id);
                        why << " object{" << id << ":hp=" << (object ? object->health : -1) << ",alive=" << (object && object->alive)
                            << ",mission=" << (object ? object->mission : "missing") << ",tombstone=" << s.deathWeapons.hasTriggered(id) << '}';
                    }
                    for (const auto& actor : s.projectiles) why << " actor{" << actor.actorId << ':' << actor.weaponId
                        << ",elapsed=" << actor.elapsedTicks << ",xyz=" << actor.currentX << ',' << actor.currentY << ',' << actor.currentZ << '}';
                    return fail(why.str());
                }
                if (profile.weapon->warhead != (culling ? "ParasitePlus" : "Parasite"))
                    return fail("native bite/cull selected the wrong authored warhead");
                if (mechanism == "parasite-cleanse") {
                    // The native healed-host operation emits KillParasite;
                    // consume its real value actions through the atomic host.
                    if (!handleRuntimeParasiteHostEvent(s, 1, ParasiteHostEventKind::Healed, 0))
                        return fail("native healed-host cleanse rejected");
                    (void)drainRuntimeObjectExitQueue(s); gather(s, records);
                    if (findRuntimeObjectByIdIncludingInactive(s, 2)->health != 0)
                        return fail("cleanse did not kill limbo drone");
                    if (!check(records, position, nativeCues(nativeType("DRON"), false, false)) || !repeated(s, 2)) return false;
                } else {
                    for (int tick = 0; tick < hz * 45 && findObjectById(s, 1); ++tick) {
                        ++s.tick; advanceRuntimeSpecialMechanisms(s); gather(s, records);
                    }
                    if (findObjectById(s, 1)) return fail("native parasite never killed host");
                    if (!check(records, position, nativeCues(nativeType(hostType), false, culling)) || !repeated(s, 1)) return false;
                }
                ++causes; return true;
            })) return false;
        }
        if (!scene("cause/ivan-bomb/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "MTNK", 1); add(s, "IVAN", 2, 7, 8, "Americans");
            findMutableObjectById(s, 1)->health = 1;
            const auto position = objectPosition(*findObjectById(s, 1));
            if (!listen(s, position)) return fail("original sound bank");
            std::string reason;
            if (!s.specialMechanisms.tryAttachIvanBomb(s.world.getObjects(), s.rules, 2, 1, s.tick, hz,
                    runtimeSpecialEventSink(s), reason)) return fail("native Ivan plant " + reason);
            Records records;
            for (int tick = 0; tick < hz * 40 && findObjectById(s, 1); ++tick) {
                ++s.tick; advanceRuntimeSpecialMechanisms(s); gather(s, records);
            }
            if (findObjectById(s, 1)) return fail("native timed bomb never detonated lethally");
            if (!check(records, position, nativeCues(nativeType("MTNK"), false, false)) || !repeated(s, 1)) return false;
            ++causes; return true;
        })) return false;
        for (const std::string type : {"DTRUCK"}) {
            if (!scene("cause/suicide-deploy/" + type + "/" + std::to_string(hz), [&]() {
                auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
                add(s, type, 1, 8, 8, "Americans");
                const auto position = objectPosition(*findObjectById(s, 1));
                if (!listen(s, position)) return fail("original sound bank");
                if (!dispatchRuntimeSecondaryActionFromUi(s, 1, RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle))
                    return fail("actual suicide deploy rejected");
                Records records; gather(s, records);
                if (findObjectById(s, 1)) return fail("suicide deploy retained live authority");
                if (!check(records, position, nativeCues(nativeType(type), false, false)) || !repeated(s, 1)) return false;
                ++causes; return true;
            })) return false;
        }
        for (const std::string type : {"TERROR", "DTRUCK"}) {
            if (!scene("cause/suicide-weapon/" + type + "/" + std::to_string(hz), [&]() {
                auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
                add(s, type, 1, 8, 8, "Americans"); add(s, "MTNK", 2, 9, 8);
                // Ordinary fixed body poses keep the original Range=1
                // Demobomb target in range; firing still follows its native
                // turn/prefire/weapon/Suicide stages through the full host.
                clearRuntimeRenderPosition(*findMutableObjectById(s, 1));
                clearRuntimeRenderPosition(*findMutableObjectById(s, 2));
                findMutableObjectById(s, 1)->facing = findMutableObjectById(s, 1)->turretFacing = runtimeFacingFromDelta(1, 0);
                const auto position = objectPosition(*findObjectById(s, 1));
                if (!listen(s, position)) return fail("original sound bank");
                initializeRuntimeOfflineGameplayRouting(s); s.selectedObjectIds = {1};
                if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary, {9, 8}, findObjectById(s, 2)))
                    return fail("actual suicide primary command");
                const auto* source = findObjectById(s, 1);
                const auto profile = source ? runtimeWeaponProfileForObject(s, *source, findObjectById(s, 2)) : RuntimeWeaponProfile{};
                if (!profile.weapon || !profile.weapon->suicide) return fail("native weapon is not authored Suicide");
                if (!s.behaviors.contains(1)) return fail("primary command did not install native attack behavior");
                Records records; gather(s, records);
                for (int tick = 0; tick < hz * 10 && findObjectById(s, 1); ++tick) {
                    ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); gather(s, records);
                }
                if (const auto* survivor = findObjectById(s, 1)) return fail("weapon Suicide failed to destroy the actual shooter: hp="
                    + std::to_string(survivor->health) + " mission=" + survivor->mission + " status=" + s.simulationStatus);
                // The stock Demobomb Report uses the same sample as DTRUCK's
                // DieSound. Its authored weapon report is a separate event;
                // count the source's lifecycle cue independently, once.
                Records deathRecords;
                for (const auto& record : records) if (record.intent.rfind("weapon:", 0) != 0) deathRecords.push_back(record);
                if (!check(deathRecords, position, nativeCues(nativeType(type), false, false)) || !repeated(s, 1)) return false;
                ++causes; return true;
            })) return false;
        }
        for (const std::string container : {"HTK", "GAAIRC", "NABNKR", "CARRIER"}) {
            if (!scene("cause/contained/" + container + "/" + std::to_string(hz), [&]() {
                auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
                add(s, container, 1, container == "CARRIER" ? 18 : 8);
                auto* host = findMutableObjectById(s, 1); host->health = 1;
                const auto position = objectPosition(*host);
                if (!listen(s, position)) return fail("original sound bank");
                const std::string childType = container == "CARRIER" ? "HORNET" : container == "GAAIRC" ? "ORCA" : "E1";
                std::vector<int> children;
                if (container == "CARRIER") {
                    advanceRuntimeSpecialMechanisms(s);
                    const auto parent = s.specialMechanisms.carrierParents().find(1);
                    if (parent == s.specialMechanisms.carrierParents().end()) return fail("native carrier spawning");
                    for (const auto& [slot, id] : parent->second.childBySlot) { (void)slot; if (id >= 0) children.push_back(id); }
                    if (children.size() != 3) return fail("native carrier did not create all three aircraft");
                } else {
                    add(s, childType, 2, 11, 8); children.push_back(2);
                    auto* child = findMutableObjectById(s, 2); host = findMutableObjectById(s, 1);
                    if (container == "GAAIRC") { child->dockedToObjectId = 1; child->dockIndex = 0; child->airborne = false; }
                    else if (container == "NABNKR") {
                        host->garrisonOccupantIds = {2}; child->garrisonHostObjectId = 1; child->alive = false; child->mission = "Garrisoned";
                        ensureRuntimeGarrisonsInitialized(s);
                    } else { host->transportPassengerIds = {2}; child->alive = false; child->mission = "Passenger"; }
                }
                std::vector<RuntimeSpatialAudioPosition> childPositions;
                for (int id : children) childPositions.push_back(container == "HTK" || container == "NABNKR" || container == "CARRIER"
                    ? position : objectPosition(*findRuntimeObjectByIdIncludingInactive(s, id)));
                s.runtimeSoundPlaybackHistory.clear();
                if (!lethalProjectile(s, 1)) return fail("real container lethal impact rejected");
                Records records; gather(s, records);
                for (std::size_t n = 0; n < children.size(); ++n) {
                    const auto* child = findRuntimeObjectByIdIncludingInactive(s, children[n]);
                    if (!child || child->alive || child->health != 0) return fail("contained child survived host destruction");
                }
                // Carrier children share one position and cue family. Count
                // their individual once-only records instead of calling the
                // single-victim assertion three times.
                const auto expected = nativeCues(nativeType(childType), false, false);
                for (const auto& cue : expected) {
                    int count = 0;
                    for (const auto& record : records) if (std::find(cue.sections.begin(), cue.sections.end(), record.section) != cue.sections.end()) {
                        ++count;
                        if (!record.playable || record.sample.empty() || !record.spatial || record.spatialGain16384 <= 0)
                            return fail("contained child sample unplayable " + record.section);
                        if (std::none_of(childPositions.begin(), childPositions.end(), [&](const auto& p) { return samePosition(record, p); }))
                            return fail("contained death remained at unrelated position " + record.section);
                    }
                    if (count != static_cast<int>(children.size())) return fail("contained death cue count " + cue.sections.front()
                        + " expected=" + std::to_string(children.size()) + " actual=" + std::to_string(count));
                    samples += count;
                }
                for (int id : children) if (!repeated(s, id)) return false;
                ++causes; return true;
            })) return false;
        }
        if (!scene("cause/contained/CARRIER-flight/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "CARRIER", 1, 18, 8, "Americans"); add(s, "MTNK", 2, 12, 8);
            advanceRuntimeSpecialMechanisms(s);
            const auto parent = s.specialMechanisms.carrierParents().find(1);
            if (parent == s.specialMechanisms.carrierParents().end()) return fail("native carrier spawning");
            std::vector<int> children;
            for (const auto& [slot, id] : parent->second.childBySlot) { (void)slot; if (id >= 0) children.push_back(id); }
            if (children.size() != 3) return fail("native carrier flight requires three authored children");
            initializeRuntimeOfflineGameplayRouting(s); s.selectedObjectIds = {1};
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    {12, 8}, findObjectById(s, 2))) return fail("native carrier attack command");
            bool flew = false;
            for (int tick = 0; tick < hz * 12 && !flew; ++tick) {
                ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
                const auto motherPosition = objectPosition(*findObjectById(s, 1));
                flew = std::any_of(children.begin(), children.end(), [&](int id) {
                    const auto* child = findObjectById(s, id);
                    if (!child || !child->airborne) return false;
                    const auto p = objectPosition(*child);
                    return std::hypot(p.x - motherPosition.x, p.y - motherPosition.y) > .25;
                });
            }
            if (!flew) return fail("actual spawner never produced a spatially distinct flying child");
            const auto motherPosition = objectPosition(*findObjectById(s, 1));
            struct ExpectedChildCue { Cue cue; RuntimeSpatialAudioPosition position; };
            std::vector<ExpectedChildCue> expected;
            for (int id : children) {
                const auto* child = findRuntimeObjectByIdIncludingInactive(s, id);
                if (!child || child->health <= 0) return fail("carrier child died before parent fixture");
                const auto position = child->alive && child->airborne ? objectPosition(*child) : motherPosition;
                for (const auto& cue : nativeCues(nativeType("HORNET"), child->airborne, false))
                    expected.push_back({cue, position});
            }
            findMutableObjectById(s, 1)->health = 1;
            if (!listen(s, motherPosition) || !lethalProjectile(s, 1)) return fail("native flying-carrier lethal impact");
            Records records; gather(s, records); std::set<std::size_t> claimed;
            for (const auto& wanted : expected) {
                bool found = false;
                for (std::size_t n = 0; n < records.size(); ++n) {
                    const auto& record = records[n];
                    if (claimed.contains(n) || !samePosition(record, wanted.position)
                        || std::find(wanted.cue.sections.begin(), wanted.cue.sections.end(), record.section) == wanted.cue.sections.end()) continue;
                    if (!record.playable || record.sample.empty() || record.path.empty()
                        || record.spatialGain16384 <= 0 || record.trackCamera != wanted.cue.trackCamera)
                        return fail("flying carrier child sample/track invalid " + record.section);
                    claimed.insert(n); ++samples; found = true; break;
                }
                if (!found) return fail("flying child cue lost physical position " + wanted.cue.sections.front());
            }
            for (std::size_t n = 0; n < records.size(); ++n) if (!claimed.contains(n))
                for (const auto& wanted : expected)
                    if (std::find(wanted.cue.sections.begin(), wanted.cue.sections.end(), records[n].section) != wanted.cue.sections.end())
                        return fail("flying child repeated cue " + records[n].section);
            for (int id : children) {
                const auto* child = findRuntimeObjectByIdIncludingInactive(s, id);
                if (!child || child->alive || child->health != 0 || !repeated(s, id)) return fail("flying child survived/repeated parent exit");
            }
            if (!repeated(s, 1)) return false;
            ++causes; return true;
        })) return false;
        if (!scene("silent/contained-already-dead/" + std::to_string(hz), [&]() {
            auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "HTK", 1); add(s, "E1", 2, 11, 8);
            auto* passenger = findMutableObjectById(s, 2);
            passenger->alive = false; passenger->health = 0; passenger->mission = "Passenger";
            findMutableObjectById(s, 1)->transportPassengerIds = {2};
            findMutableObjectById(s, 1)->health = 1;
            const auto position = objectPosition(*findObjectById(s, 1));
            if (!listen(s, position) || !lethalProjectile(s, 1)) return fail("dead residual passenger host impact");
            Records records; gather(s, records);
            if (!check(records, position, nativeCues(nativeType("HTK"), false, false))) return false;
            const auto passengerDeath = tokens(raw.getValue("E1", "DieSound", ""));
            for (const auto& record : records) if (std::find(passengerDeath.begin(),
                    passengerDeath.end(), record.section) != passengerDeath.end())
                return fail("already dead residual passenger replayed DieSound");
            if (!repeated(s, 1) || !repeated(s, 2)) return false;
            ++causes; return true;
        })) return false;
        for (const auto kind : {RuntimeObjectExitKind::TemporalDestroyed, RuntimeObjectExitKind::Sold,
                RuntimeObjectExitKind::Consumed, RuntimeObjectExitKind::TransformSource, RuntimeObjectExitKind::ScriptRemoved}) {
            if (!scene("silent/exit-kind" + std::to_string(static_cast<int>(kind)) + "/" + std::to_string(hz), [&]() {
                auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
                add(s, "E1", 1); const auto position = objectPosition(*findObjectById(s, 1));
                if (!listen(s, position)) return fail("original sound bank");
                RuntimeObjectExitRequest exit; exit.objectId = 1; exit.kind = kind; exit.tick = s.tick;
                exit.suppressCrew = exit.suppressDeathWeapon = true;
                if (queueRuntimeObjectExit(s, exit) != RuntimeObjectExitQueueResult::Queued || !drainRuntimeObjectExitQueue(s))
                    return fail("non-death exit rejected");
                Records records; gather(s, records);
                if (!check(records, position, {}) || !repeated(s, 1)) return false;
                ++causes; return true;
            })) return false;
            if (!scene("silent/contained-exit-kind" + std::to_string(static_cast<int>(kind)) + "/" + std::to_string(hz), [&]() {
                auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
                add(s, "HTK", 1); add(s, "E1", 2, 11, 8);
                auto* passenger = findMutableObjectById(s, 2);
                passenger->alive = false; passenger->mission = "Passenger";
                findMutableObjectById(s, 1)->transportPassengerIds = {2};
                const auto position = objectPosition(*findObjectById(s, 1));
                if (!listen(s, position)) return fail("original sound bank");
                RuntimeObjectExitRequest exit; exit.objectId = 1; exit.kind = kind; exit.tick = s.tick;
                exit.suppressCrew = exit.suppressDeathWeapon = true;
                if (queueRuntimeObjectExit(s, exit) != RuntimeObjectExitQueueResult::Queued || !drainRuntimeObjectExitQueue(s))
                    return fail("non-death container exit rejected");
                Records records; gather(s, records);
                for (const auto& record : records) if (deathSections.contains(record.section))
                    return fail("non-death/Temporal container cleanup emitted ordinary death cue " + record.section);
                if (!repeated(s, 1) || !repeated(s, 2)) return false;
                ++causes; return true;
            })) return false;
        }
    }
    if (!scene("silent/contained-exit-merge/30", [&]() {
        auto box = fixture(30); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
        add(s, "HTK", 1); add(s, "E1", 2, 11, 8);
        auto* passenger = findMutableObjectById(s, 2); passenger->alive = false; passenger->mission = "Passenger";
        findMutableObjectById(s, 1)->transportPassengerIds = {2};
        if (!listen(s, objectPosition(*findObjectById(s, 1)))) return fail("original sound bank");
        RuntimeObjectExitRequest removal; removal.objectId = 1; removal.tick = s.tick;
        removal.kind = RuntimeObjectExitKind::ScriptRemoved; removal.suppressCrew = removal.suppressDeathWeapon = true;
        if (queueRuntimeObjectExit(s, removal) != RuntimeObjectExitQueueResult::Queued) return fail("first ScriptRemoved rejected");
        removal.kind = RuntimeObjectExitKind::Destroyed;
        (void)queueRuntimeObjectExit(s, removal);
        if (!drainRuntimeObjectExitQueue(s)) return fail("merged container exit rejected");
        Records records; gather(s, records);
        for (const auto& record : records) if (deathSections.contains(record.section))
            return fail("later Destroyed duplicate polluted first ScriptRemoved with " + record.section);
        if (!repeated(s, 1) || !repeated(s, 2)) return false;
        ++causes; return true;
    })) return false;
    for (const auto kind : {RuntimeObjectExitKind::ScriptRemoved, RuntimeObjectExitKind::TemporalDestroyed}) {
        if (!scene("silent/parasite-host-exit-kind" + std::to_string(static_cast<int>(kind)) + "/30", [&]() {
            auto box = fixture(30); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            add(s, "MTNK", 1); add(s, "DRON", 2, 7, 8, "Americans");
            if (!listen(s, objectPosition(*findObjectById(s, 1)))) return fail("original sound bank");
            const auto* source = findObjectById(s, 2); const auto* host = findObjectById(s, 1);
            const auto profile = runtimeWeaponProfileForObject(s, *source, host);
            if (!profile.weapon || !launchRuntimeParasiteProjectile(s, *source, *host, *profile.weapon, profile))
                return fail("native parasite launch");
            for (int tick = 0; tick < 300 && !s.specialMechanisms.parasiteLinks().contains(1); ++tick) {
                ++s.tick; advanceRuntimeEffects(s);
            }
            if (!s.specialMechanisms.parasiteLinks().contains(1)) return fail("native attachment never completed");
            s.runtimeSoundPlaybackHistory.clear();
            RuntimeObjectExitRequest removal; removal.objectId = 1; removal.tick = s.tick; removal.kind = kind;
            removal.suppressCrew = removal.suppressDeathWeapon = true;
            if (queueRuntimeObjectExit(s, removal) != RuntimeObjectExitQueueResult::Queued || !drainRuntimeObjectExitQueue(s))
                return fail("attached host non-death exit rejected");
            Records records; gather(s, records);
            for (const auto& record : records) if (deathSections.contains(record.section))
                return fail("non-death host cleanup killed limbo drone audibly " + record.section);
            if (!s.specialMechanisms.parasiteLinks().empty() || !s.specialMechanisms.pendingParasiteLaunches().empty())
                return fail("non-death host retained parasite relation");
            if (!repeated(s, 1) || !repeated(s, 2)) return false;
            ++causes; return true;
        })) return false;
    }
    if (!scene("cause/parasite-pending-source-destroyed/30", [&]() {
        auto box = fixture(30); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
        add(s, "MTNK", 1, 9, 8); add(s, "DRON", 2, 7, 8, "Americans");
        const auto* source = findObjectById(s, 2); const auto* host = findObjectById(s, 1);
        const auto profile = runtimeWeaponProfileForObject(s, *source, host);
        if (!profile.weapon || !launchRuntimeParasiteProjectile(s, *source, *host, *profile.weapon, profile))
            return fail("native parasite launch");
        ++s.tick; advanceRuntimeEffects(s);
        const auto actor = std::find_if(s.projectiles.begin(), s.projectiles.end(), [](const auto& projectile) {
            return projectile.attackerObjectId == 2 && projectile.weaponId == "DroneJump" && projectile.elapsedTicks >= 1;
        });
        if (actor == s.projectiles.end() || !s.specialMechanisms.pendingParasiteLaunches().contains(actor->actorId))
            return fail("real flight actor missing at elapsed tick");
        const RuntimeSpatialAudioPosition position{actor->currentX, actor->currentY, actor->currentZ};
        const auto launchPosition = objectPosition(*findRuntimeObjectByIdIncludingInactive(s, 2));
        if (std::hypot(position.x - launchPosition.x, position.y - launchPosition.y) < 1e-4
            && std::abs(position.level - launchPosition.level) < 1e-4) return fail("flight body never left launch pose");
        if (!listen(s, position)) return fail("original sound bank");
        RuntimeObjectExitRequest death; death.objectId = 2; death.tick = s.tick; death.kind = RuntimeObjectExitKind::Destroyed;
        death.suppressCrew = true;
        if (queueRuntimeObjectExit(s, death) != RuntimeObjectExitQueueResult::Queued || !drainRuntimeObjectExitQueue(s))
            return fail("public inactive-source Destroyed exit rejected");
        Records records; gather(s, records);
        for (int tick = 0; tick < 300 && (!s.specialMechanisms.pendingParasiteLaunches().empty() || !s.projectiles.empty()); ++tick) {
            ++s.tick; advanceRuntimeEffects(s); advanceRuntimeSpecialMechanisms(s); gather(s, records);
        }
        if (!s.specialMechanisms.pendingParasiteLaunches().empty() || !s.specialMechanisms.parasiteLinks().empty())
            return fail("dead airborne parasite reattached or retained pending identity");
        if (!findObjectById(s, 1)) return fail("dead launch unexpectedly destroyed host");
        if (!check(records, position, nativeCues(nativeType("DRON"), false, false)) || !repeated(s, 2)) return false;
        ++causes; return true;
    })) return false;
    for (const std::string shipType : {"DEST", "CARRIER"}) {
        if (!scene("cause/nuke-water/" + shipType + "/30", [&]() {
            auto box = fixture(30); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            s.config.superweapons = true;
            add(s, shipType, 1, 18, 8); add(s, "NAMISL", 2, 3, 3, "Americans");
            add(s, "NANRCT", 3, 7, 3, "Americans");
            findMutableObjectById(s, 1)->health = 1;
            const auto position = objectPosition(*findObjectById(s, 1));
            if (raw.getValue("NAMISL", "SuperWeapon", "") != "NukeSpecial")
                return fail("original silo no longer grants NukeSpecial");
            s.structureFootprints = buildRuntimeStructureFootprints(s);
            const std::string childType = raw.getValue(shipType, "Spawns", "");
            const int childCount = static_cast<int>(number(shipType, "SpawnsNumber", 0));
            std::vector<int> children;
            if (!childType.empty() && childCount > 0) {
                advanceRuntimeSpecialMechanisms(s);
                const auto parent = s.specialMechanisms.carrierParents().find(1);
                if (parent == s.specialMechanisms.carrierParents().end()) return fail("native ship spawning " + childType);
                for (const auto& [slot, id] : parent->second.childBySlot) { (void)slot; if (id >= 0) children.push_back(id); }
                if (static_cast<int>(children.size()) != childCount) return fail("original ship child count " + childType);
                for (int id : children) {
                    const auto* child = findRuntimeObjectByIdIncludingInactive(s, id);
                    if (!child || child->type != childType || child->airborne || child->health <= 0)
                        return fail("original ship child type/grounded state " + childType);
                }
            }
            if (!listen(s, position)) return fail("original sound bank");
            ensureRuntimeSuperWeaponsInitialized(s);
            auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
            s.superWeapons.tick(context); std::string reason;
            if (!s.superWeapons.findSidebarSnapshot("Americans", "NukeSpecial")
                || !s.superWeapons.forceReady("Americans", "NukeSpecial", &context)
                || !s.superWeapons.beginTargetSelection("Americans", "NukeSpecial", context, &reason)
                || !s.superWeapons.submitTarget("Americans", "NukeSpecial", SuperWeaponTarget{{18, 8}, -1}, context, &reason))
                return fail("native silo nuclear targeting " + reason);
            Records records; gather(s, records); bool sawFlight = false;
            std::string lethalStageDiagnostic;
            for (int tick = 0; tick < 30 * 45 && findObjectById(s, 1); ++tick) {
                for (const auto& fx : s.superWeapons.renderSnapshots())
                    sawFlight = sawFlight || (fx.superWeaponId == "NukeSpecial" && fx.kind == "superWeaponProjectile");
                ++s.tick; advanceRuntimeEffects(s);
                if (!findObjectById(s, 1)) {
                    std::ostringstream stage; stage << " radialStage tick=" << s.tick;
                    for (const auto& pending : s.pendingObjectExits)
                        stage << " pending{" << pending.objectId << ":kind=" << static_cast<int>(pending.kind)
                            << ",wasAlive=" << pending.wasAliveBeforeDamage << ",requested=" << pending.unitDeathSoundRequested
                            << ",handled=" << pending.unitDeathSoundHandled << '}';
                    for (int id : children) {
                        const auto* child = findRuntimeObjectByIdIncludingInactive(s, id);
                        const auto phase = s.specialMechanisms.carrierChildren().find(id);
                        stage << " child{" << id << ":hp=" << (child ? child->health : -1) << ",alive=" << (child && child->alive)
                            << ",airborne=" << (child && child->airborne) << ",tombstone=" << s.deathWeapons.hasTriggered(id)
                            << ",phase=" << (phase == s.specialMechanisms.carrierChildren().end() ? -1 : static_cast<int>(phase->second.phase)) << '}';
                    }
                    lethalStageDiagnostic = stage.str();
                }
                // The enclosing native simulation drains exits after its
                // effects/behavior stages in this same tick (Shell's final
                // drain), before the next carrier reconciliation.
                (void)drainRuntimeObjectExitQueue(s); gather(s, records);
            }
            if (!sawFlight || findObjectById(s, 1)) return fail("native nuclear flight/radial death never completed");
            for (int tick = 0; tick < 30; ++tick) {
                ++s.tick; advanceRuntimeEffects(s); (void)drainRuntimeObjectExitQueue(s); gather(s, records);
            }
            const auto expected = nativeCues(nativeType(shipType), false, true);
            if (expected.empty()) return fail("original water ship lacks a sinking declaration");
            const auto childCues = children.empty() ? std::vector<Cue>{} : nativeCues(nativeType(childType), false, false);
            for (const auto& cue : childCues) {
                int count = 0;
                for (const auto& record : records)
                    if (std::find(cue.sections.begin(), cue.sections.end(), record.section) != cue.sections.end()) {
                        ++count;
                        if (!samePosition(record, position) || !record.playable || record.sample.empty() || record.path.empty()
                            || record.spatialGain16384 <= 0 || record.trackCamera != cue.trackCamera)
                            return fail("nuclear deck child sample/position " + record.section);
                    }
                if (count != static_cast<int>(children.size())) {
                    std::ostringstream why; why << "nuclear deck death count " << cue.sections.front()
                        << " expected=" << children.size() << " actual=" << count << lethalStageDiagnostic;
                    for (const auto& record : records) why << " sound{" << record.section << ':' << record.sourcePosition.x
                        << ',' << record.sourcePosition.y << ',' << record.sourcePosition.level << ':' << record.status << '}';
                    return fail(why.str());
                }
                samples += count;
            }
            // Child families have already been counted exactly; retain the
            // mother's independent sinking oracle and unexpected-cue check.
            Records motherRecords;
            for (const auto& record : records) if (std::none_of(childCues.begin(), childCues.end(), [&](const auto& cue) {
                return std::find(cue.sections.begin(), cue.sections.end(), record.section) != cue.sections.end();
            })) motherRecords.push_back(record);
            if (!check(motherRecords, position, expected) || !repeated(s, 1)) return false;
            for (int id : children) {
                const auto* child = findRuntimeObjectByIdIncludingInactive(s, id);
                if (!child || child->health != 0 || child->alive || !repeated(s, id)) return fail("nuclear deck child survived/repeated exit");
            }
            ++causes; return true;
        })) return false;
    }
    if (!cases) return fail("case filter matched no scene");
    std::cout << "Unit death audio coverage PASS: native infantry=" << infantry << " vehicles=" << vehicles
        << " aircraft=" << aircraft << " scenes=" << cases << " queuedDeaths=" << queueDeaths
        << " projectileDeaths=" << projectileDeaths << " causeScenes=" << causes << " cueChecks=" << samples
        << " duplicateExits=" << duplicates << " naturalWreckCompletions=" << wreckPhases << " nativeImpactCues=" << impactCues << '\n';
    error.clear(); return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
