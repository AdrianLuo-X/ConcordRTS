// Stock engineer IFVs use the contextual right-click route, including targets
// which also expose transport abilities. Every observation runs full ticks.
bool runRuntimeIfvRepairTargetsSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config; assets->config.playerSlots.clear();
    assets->config.gameModeRulesOverride.clear(); assets->config.computerAiEnabled = false;
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world,
        &assets->theater, &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) { error = "IFV repair assets: " + assets->report.errorMessage; return false; }
    IniFile stock;
    if (!stock.loadFromFile(assets->report.rulesPath) || !assets->rules.loadFromIni(stock)) {
        error = "IFV repair original rules unavailable"; return false;
    }
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    const auto* repair = assets->rules.findWeaponType("RepairBullet");
    const auto* mechanical = repair ? assets->rules.findWarheadType(repair->warhead) : nullptr;
    if (!repair || repair->damage >= 0 || !mechanical || !repair->useSparkParticles
        || repair->attachedParticleSystem != "WeldingSys") {
        error = "IFV repair original negative weapon/welding rules missing"; return false;
    }
    int landTile = -1, waterTile = -1;
    const auto metadata = runtimeTerrainMetadataContext(*assets);
    const int waterSet = assets->theater.getGeneralTileSetIndex("WaterSet");
    for (int tile = 0; tile < static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(*assets, cell);
        if (!info.passable || info.shore || info.cliff || info.bridge) continue;
        if (!info.water && landTile < 0) landTile = tile;
        if (info.water && waterTile < 0 && waterSet >= 0
            && assets->theater.resolveTileIndex(tile).tileSetIndex == waterSet
            && resolveRuntimeTerrainSubTileLandType(cell, metadata, assets->renderCache) == RuntimeTerrainLandType::Water
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, assets->renderCache) == 0) waterTile = tile;
        if (landTile >= 0 && waterTile >= 0) break;
    }
    if (landTile < 0 || waterTile < 0) { error = "IFV repair original coast tiles unavailable"; return false; }
    const char* output = std::getenv("CONCORD_IFV_REPAIR_OUTPUT");
    std::string label, stage; std::vector<std::string> failures;
    int scenes = 0, shots = 0, saves = 0, snapshots = 0, captures = 0;
    std::set<std::string> repairedTypes;
    const auto fail = [&](const std::string& why) { error = "IFV repair " + label + " [" + stage + "]: " + why; return false; };
    const auto diagnostic = [](const RuntimeWindowState& s) {
        std::ostringstream out; out << "tick=" << s.tick << " status=" << s.simulationStatus
            << " command=" << s.lastCommand.acceptedCount << '/' << s.lastCommand.rejectedCount
            << " pendingEntry=" << s.pendingTransportTargets.size();
        for (int id : {1, 2, 3, 4}) {
            const auto* object = findRuntimeObjectByIdIncludingInactive(s, id);
            if (!object) continue;
            out << " object=" << id << ':' << object->type << " alive=" << object->alive
                << " hp=" << object->health << " xyz=" << runtimeObjectRenderX(*object) << ','
                << runtimeObjectRenderY(*object) << " mode=" << object->activeGunnerMode
                << " cargo=" << object->transportPassengerIds.size() << " submerged=" << object->submerged;
            if (const auto b = s.behaviors.find(id); b != s.behaviors.end())
                out << " behavior=" << static_cast<int>(b->second.kind) << '/' << static_cast<int>(b->second.state)
                    << " target=" << b->second.targetObjectId << " force=" << b->second.forceFire;
        }
        return out.str();
    };
    const auto add = [](RuntimeWindowState& s, int id, const std::string& type,
        WorldObjectKind kind, RuntimeCellPoint cell, const std::string& owner = "Americans") {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "IFVRepair" + std::to_string(id);
        object.type = type; object.kind = kind; object.owner = owner; object.x = cell.x; object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y); object.alive = object.poweredOperational = true;
        object.health = runtimeMaxHealthForObject(s, object); object.mission = "Sleep";
        object.facing = object.turretFacing = runtimeFacingFromDelta(1, 0);
        object.submerged = runtimeInitialObjectSubmergedState(s.rules, object);
        initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
        applyRuntimeInitialObjectFlightState(s.rules, object, true);
        runtimeInitializeModExtensions(object, s.rules, s.config.fixedTickHz);
        s.world.accessObjects().push_back(std::move(object));
    };
    const auto make = [&](int hz, const std::string& targetType, bool occupied = false,
        const std::string& owner = "Americans", WorldObjectKind kind = WorldObjectKind::Unit) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = assets->config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.gameMode = "Battle"; s.config.playerCountry = "Americans";
        s.config.multiplayerActive = s.config.shortGame = s.config.crates = s.config.superweapons = false;
        s.config.skirmishLoadSavePath.clear();
        s.report = assets->report; s.rules = assets->rules; s.art = assets->art; s.theater = assets->theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.world = assets->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts.theater = assets->mapFacts.theater; s.mapFacts.lighting = assets->mapFacts.lighting;
        for (int y = 64; y <= 96; ++y) for (int x = 64; x <= 96; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = x >= 78 ? waterTile : landTile; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        add(s, 1, "FV", WorldObjectKind::Unit, {76, 75});
        add(s, 2, "ENGINEER", WorldObjectKind::Infantry, {75, 75});
        const auto* type = findRuntimeUnitTypeById(s.rules, targetType);
        add(s, 3, targetType, kind, {type && (type->naval || type->waterBound) ? 78 : 77, 75}, owner);
        if (occupied) add(s, 4, targetType == "FV" ? "ENGINEER" : "E1", WorldObjectKind::Infantry, {75, 73}, owner);
        add(s, 998, "NAPOWR", WorldObjectKind::Structure, {67, 67}, "Russians");
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.triggers.bootstrapComplete = true; s.economy.skirmishStartApplied = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        return storage;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick;
        advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto board = [&](RuntimeWindowState& s, int footId, int hostId) {
        stage = "actual engineer boarding"; s.selectedObjectIds = {footId};
        const auto* host = findObjectById(s, hostId);
        if (!host || runtimeTargetCursorIntent(s, *host) != RuntimeTargetCursorIntent::Enter
            || !queueRuntimeTargetAbility(s, *host) || !s.pendingTransportTargets.contains(footId))
            return fail("entry queue rejected: " + diagnostic(s));
        for (int n = 0; n < s.config.fixedTickHz * 20 && s.pendingTransportTargets.contains(footId); ++n) step(s);
        host = findObjectById(s, hostId);
        const auto* passenger = findRuntimeObjectByIdIncludingInactive(s, footId);
        if (!host || !passenger || passenger->alive || s.pendingTransportTargets.contains(footId)
            || std::find(host->transportPassengerIds.begin(), host->transportPassengerIds.end(), footId)
                == host->transportPassengerIds.end()) return fail("full ticks did not complete boarding: " + diagnostic(s));
        if (hostId == 1 && (host->activeGunnerMode != 1
            || runtimeWeaponProfileForObject(s, *host, nullptr).weaponId != "RepairBullet"))
            return fail("boarded engineer did not select the stock repair arm");
        return true;
    };
    const auto command = [&](RuntimeWindowState& s, int sourceId, int targetId,
        RuntimeMultiplayerGameplayTargetOrderKind order = RuntimeMultiplayerGameplayTargetOrderKind::Primary) {
        s.selectedObjectIds = {sourceId}; const auto* target = findObjectById(s, targetId);
        return target && dispatchRuntimeTargetOrderFromUi(s, order, runtimeCellPointForObject(*target), target);
    };
    const auto stop = [](RuntimeWindowState& s) {
        s.selectedObjectIds = {1};
        return dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop);
    };
    const auto sourceIntact = [](const RuntimeWindowState& s) {
        const auto* fv = findObjectById(s, 1);
        const auto* engineer = findRuntimeObjectByIdIncludingInactive(s, 2);
        return fv && engineer && !engineer->alive && fv->activeGunnerMode == 1
            && fv->transportPassengerIds == std::vector<int>{2} && !s.pendingTransportTargets.contains(1);
    };
    const auto weldingDiagnostic = [](const RuntimeWindowState& s, int sourceId) {
        std::ostringstream out; out << "tick=" << s.tick << " source=" << sourceId;
        for (const auto& system : s.particles.systems()) {
            if (system.sourceObjectId != sourceId || system.typeId != "WeldingSys") continue;
            int live = 0, longest = 0;
            for (const auto& particle : s.particles.particles()) if (particle.systemInstanceId == system.id) {
                ++live; longest = std::max(longest, particle.remainingLifetime);
            }
            out << " system=" << system.id << " emitting=" << system.emitting << " age=" << system.ageTicks
                << " lifetime=" << system.remainingLifetime << " liveParticles=" << live << " maxParticleLifetime=" << longest;
        }
        int live = 0, longest = 0;
        for (const auto& particle : s.particles.particles()) if (particle.sourceObjectId == sourceId
            && particle.sourceTag == "weapon:RepairBullet") { ++live; longest = std::max(longest, particle.remainingLifetime); }
        out << " ownedParticles=" << live << " maxRemainingLifetime=" << longest;
        return out.str();
    };
    const auto waitRepairEffects = [&](RuntimeWindowState& s, int sourceId) {
        const auto* systemRules = s.rules.findParticleSystemType("WeldingSys");
        if (!systemRules) return fail("original welding system missing during drain");
        // MaxEC=500 on stock WeldingSpark is 33.333 seconds. Stopping
        // emission at ROF=80 frames does not delete the already-live sparks.
        // Bound future emission plus the complete held-particle lifetime chain.
        int tailTicks = 0; std::set<std::string> visited;
        for (std::string id = systemRules->holdsWhat; !id.empty();) {
            const auto* type = s.rules.findParticleType(id);
            if (!type || type->maxLifetime <= 0 || !visited.insert(id).second)
                return fail("welding particle chain has no finite authored expiry: " + weldingDiagnostic(s, sourceId));
            tailTicks += std::max(1, static_cast<int>(std::lround(static_cast<double>(type->maxLifetime)
                * s.config.fixedTickHz / 15.0)));
            id = type->nextParticle;
        }
        int budget = 0;
        for (const auto& system : s.particles.systems()) if (system.sourceObjectId == sourceId && system.typeId == "WeldingSys") {
            if (system.emitting && system.remainingLifetime <= 0)
                return fail("welding emitter has no finite weapon-owned lifetime: " + weldingDiagnostic(s, sourceId));
            budget = std::max(budget, std::max(0, system.remainingLifetime) + tailTicks);
        }
        for (const auto& particle : s.particles.particles()) if (particle.sourceObjectId == sourceId
            && particle.sourceTag == "weapon:RepairBullet") {
            if (particle.remainingLifetime <= 0)
                return fail("live welding spark has no finite lifetime: " + weldingDiagnostic(s, sourceId));
            const auto* type = s.rules.findParticleType(particle.typeId);
            const int fullLifetime = type ? std::max(1, static_cast<int>(std::lround(
                static_cast<double>(type->maxLifetime) * s.config.fixedTickHz / 15.0))) : 0;
            budget = std::max(budget, particle.remainingLifetime + std::max(0, tailTicks - fullLifetime));
        }
        for (const auto& p : s.projectiles) if (p.attackerObjectId == sourceId && p.weaponId == "RepairBullet")
            budget = std::max(budget, std::max(0, p.remainingTicks) + tailTicks);
        const auto clear = [&]() {
            return std::none_of(s.projectiles.begin(), s.projectiles.end(), [&](const auto& p) {
                return p.attackerObjectId == sourceId && p.weaponId == "RepairBullet";
            }) && std::none_of(s.particles.systems().begin(), s.particles.systems().end(), [&](const auto& p) {
                return p.sourceObjectId == sourceId && p.typeId == "WeldingSys";
            }) && std::none_of(s.particles.particles().begin(), s.particles.particles().end(), [&](const auto& p) {
                return p.sourceObjectId == sourceId && p.sourceTag == "weapon:RepairBullet";
            });
        };
        const int started = s.tick;
        for (int n = 0; n <= budget + s.config.fixedTickHz * 2; ++n) {
            if (clear()) return true;
            step(s);
        }
        return fail("repair effects did not finish within authored finite lifetime; started=" + std::to_string(started)
            + " budget=" + std::to_string(budget) + ' ' + weldingDiagnostic(s, sourceId));
    };
    const auto drain = [&](RuntimeWindowState& s) {
        if (!stop(s)) return fail("Stop rejected while draining repair effects");
        return waitRepairEffects(s, 1);
    };
    const auto healRestored = [&](RuntimeWindowState& s) {
        const auto* target = findObjectById(s, 3); if (!target) return fail("restored target missing");
        const int cap = runtimeMaxHealthForObject(s, *target);
        for (int n = 0; n < s.config.fixedTickHz * 25; ++n) {
            step(s); target = findObjectById(s, 3);
            if (!target || !sourceIntact(s) || target->health > cap) return fail("restored repair consumed cargo/target or exceeded cap");
            if (target->health == cap) return drain(s);
        }
        return fail("restored real repair did not finish: " + diagnostic(s));
    };
    const auto persistence = [&](RuntimeWindowState& source) {
        stage = "repair in progress save";
        std::ostringstream ordinaryText, exactText, canonical; IniFile ordinaryIni, exactIni;
        if (!writeRuntimeStateToStream(source, ordinaryText, error)
            || !writeRuntimeStateToStream(source, exactText, error)
            || !writeRuntimeMultiplayerSnapshotExtension(source, exactText, error)
            || !ordinaryIni.loadFromString(ordinaryText.str()) || !exactIni.loadFromString(exactText.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error)
            || !exactIni.loadFromString(canonical.str())) return fail("save write: " + error);
        const auto restore = [&](bool exact) {
            auto result = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*result, source, false);
            // Canonical MP output omits process-local audio/lighting hosts.
            // Load complete ordinary authority before its exact extension.
            if (!materializeRuntimeLoadStateFromIni(*result, ordinaryIni, error)
                || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*result, exactIni, error)))
                return std::unique_ptr<RuntimeWindowState>{};
            return result;
        };
        auto ordinary = restore(false), exact = restore(true);
        if (!ordinary || !exact) return fail("repair checkpoint restore: " + error);
        const auto intentValid = [&](const RuntimeWindowState& s) {
            const auto* target = findObjectById(s, 3); const auto* fv = findObjectById(s, 1);
            const auto b = s.behaviors.find(1);
            return sourceIntact(s) && target && fv && b != s.behaviors.end()
                && b->second.kind == RuntimeBehaviorKind::Attack && b->second.targetObjectId == 3
                && !b->second.forceFire && runtimeWeaponProfileForObject(s, *fv, target).weaponId == "RepairBullet"
                && target->health == findObjectById(source, 3)->health
                && target->transportPassengerIds == findObjectById(source, 3)->transportPassengerIds;
        };
        if (!intentValid(*ordinary) || !intentValid(*exact)) return fail("save changed repair intent or target cargo");
        for (int id : {1, 2, 3, 4}) {
            const auto* a = findRuntimeObjectByIdIncludingInactive(source, id);
            const auto* b = findRuntimeObjectByIdIncludingInactive(*exact, id);
            if ((!a) != (!b) || (a && (a->alive != b->alive || a->health != b->health
                || a->activeGunnerMode != b->activeGunnerMode || a->transportPassengerIds != b->transportPassengerIds
                || std::abs(runtimeObjectRenderX(*a) - runtimeObjectRenderX(*b)) > 1e-8
                || std::abs(runtimeObjectRenderY(*a) - runtimeObjectRenderY(*b)) > 1e-8)))
                return fail("exact snapshot changed active repair actor pose/cargo");
        }
        if (!healRestored(*ordinary) || !healRestored(*exact)) return false;
        ++saves; ++snapshots; return true;
    };
    const auto finish = [&](bool passed) {
        if (passed) { ++scenes; std::cout << "IFV repair " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    // Mechanical follows the authored Verses table, not Organic. Stock
    // surfaced dolphins/squids with light armor are included deliberately.
    std::vector<std::string> legalTypes;
    for (const auto& type : assets->rules.getVehicleTypes()) {
        WorldObjectInstance probe; probe.kind = WorldObjectKind::Unit; probe.type = type.id; probe.alive = true;
        if (!type.id.empty() && type.strength > 0 && !runtimeObjectIsAirTarget(*assets, probe)
            && runtimeWarheadArmorMultiplier(mechanical, type.armor) > 0) legalTypes.push_back(type.id);
    }
    for (int hz : {30, 60}) for (const auto& type : legalTypes) {
        label = type + '-' + std::to_string(hz) + "-stock-right-click";
        const bool passed = [&]() {
            stage = "startup"; auto storage = make(hz, type); auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            if (!board(s, 2, 1)) return false;
            auto* target = findMutableObjectById(s, 3); const int cap = runtimeMaxHealthForObject(s, *target);
            const bool saveScene = type == "FV" || type == "MTNK" || type == "DEST" || type == "LCRF";
            target->health = std::max(1, cap - (saveScene ? 75 : 20));
            // An authored already-damaged submarine uses the normal reveal
            // transition and its original dive timer; no targeting rule edits.
            runtimeRevealObjectAfterDamage(s, *target, "ifv-fixture-initial-damage");
            const int before = target->health; const auto manifest = target->transportPassengerIds;
            s.selectedObjectIds = {1};
            const auto* fv = findObjectById(s, 1);
            const auto profile = runtimeWeaponProfileForObject(s, *fv, target);
            if (profile.weaponId != "RepairBullet" || runtimeTargetCursorIntent(s, *target) != RuntimeTargetCursorIntent::UnitRepair)
                return fail("stock legal target did not show the repair cursor: " + diagnostic(s));
            stage = "contextual repair command";
            if (!command(s, 1, 3) || !s.behaviors.contains(1)
                || s.behaviors.at(1).kind != RuntimeBehaviorKind::Attack || s.behaviors.at(1).targetObjectId != 3
                || s.behaviors.at(1).forceFire || s.pendingTransportTargets.contains(1))
                return fail("right click was swallowed by target ability/transport: " + diagnostic(s));
            bool welded = false, saved = false, capped = false; std::set<int> actors, expired;
            for (int n = 0; n < hz * 25; ++n) {
                step(s); target = findMutableObjectById(s, 3);
                if (!target || !sourceIntact(s) || target->health > cap || target->transportPassengerIds != manifest)
                    return fail("repair consumed target/passenger or exceeded cap: " + diagnostic(s));
                for (const auto& p : s.projectiles) if (p.attackerObjectId == 1) {
                    if (p.weaponId != "RepairBullet" || p.damage != repair->damage || p.damage >= 0
                        || p.targetObjectId != 3 || !p.affectsAllies) return fail("actual negative healing projectile/target mismatch");
                    actors.insert(p.actorId);
                }
                for (const auto& p : s.recentExpiredProjectiles) if (actors.contains(p.actorId)
                    && p.weaponId == "RepairBullet" && p.damage < 0 && p.targetObjectId == 3
                    && p.lifecycle == RuntimeProjectileLifecycleState::Expired) expired.insert(p.actorId);
                for (const auto& p : s.particles.systems()) if (p.sourceObjectId == 1 && p.typeId == "WeldingSys"
                    && p.sourceTag == "weapon:RepairBullet" && p.remainingLifetime > 0) welded = true;
                if (saveScene && !saved && !actors.empty()) {
                    if (!persistence(s)) return false; saved = true; stage = "resumed live repair";
                }
                if (target->health == cap && !expired.empty() && std::none_of(s.projectiles.begin(), s.projectiles.end(),
                    [](const auto& p) { return p.attackerObjectId == 1 && p.weaponId == "RepairBullet"; })) { capped = true; break; }
            }
            if (!capped || actors.empty() || expired.size() != actors.size() || !welded || target->health <= before
                || (saveScene && !saved)) return fail("real negative impact/welding/full repair incomplete: " + diagnostic(s));
            shots += static_cast<int>(actors.size()); repairedTypes.insert(type);
            if (output && hz == 30 && (type == "FV" || type == "DEST")) {
                const auto stats = drawRuntimeStateOffscreenAtCell(s, {77, 75}, 1280, 720, error,
                    std::string(output) + '/' + label + ".bmp", false);
                if (!stats || !stats->rendered || stats->missingVoxelProxies != 0) return fail("original repair-arm render: " + error);
                ++captures;
            }
            if (!drain(s) || findObjectById(s, 3)->health != cap || !sourceIntact(s)) return false;
            return true;
        }(); finish(passed);
    }
    for (int hz : {30, 60}) for (const std::string type : {"FV", "HTK", "LCRF"}) {
        label = type + '-' + std::to_string(hz) + "-occupied-target";
        const bool passed = [&]() {
            auto storage = make(hz, type, true); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            if (!board(s, 2, 1) || !board(s, 4, 3)) return false;
            auto* target = findMutableObjectById(s, 3); const auto manifest = target->transportPassengerIds;
            const int cap = runtimeMaxHealthForObject(s, *target); target->health = cap - 10;
            s.selectedObjectIds = {1}; stage = "occupied transport repair";
            if (runtimeTargetCursorIntent(s, *target) != RuntimeTargetCursorIntent::UnitRepair || !command(s, 1, 3)
                || s.pendingTransportTargets.contains(1)) return fail("occupied target intercepted repair");
            if (!healRestored(s)) return false;
            target = findMutableObjectById(s, 3); const auto* cargo = findRuntimeObjectByIdIncludingInactive(s, 4);
            return target && target->transportPassengerIds == manifest && cargo && !cargo->alive
                ? true : fail("repair unloaded or consumed target cargo");
        }(); finish(passed);
    }
    for (int hz : {30, 60}) {
        label = "LCRF-" + std::to_string(hz) + "-mixed-selection-repair-priority";
        const bool mixedPassed = [&]() {
            auto storage = make(hz, "LCRF", true); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            // The normal FV and GI precede the repair FV after canonical ID
            // sorting. A transport relationship must not hide the later healer.
            add(s, 5, "E1", WorldObjectKind::Infantry, {75, 74});
            add(s, 10, "FV", WorldObjectKind::Unit, {76, 73});
            initializeRuntimeNavigation(s);
            if (!board(s, 4, 3) || !board(s, 2, 10)) return false;
            auto* target = findMutableObjectById(s, 3); const int cap = runtimeMaxHealthForObject(s, *target);
            const auto manifest = target->transportPassengerIds; target->health = cap - 20;
            s.selectedObjectIds = {10, 5, 1}; stage = "mixed contextual repair";
            if (runtimeTargetCursorIntent(s, *target) != RuntimeTargetCursorIntent::UnitRepair
                || !dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    runtimeCellPointForObject(*target), target) || !s.pendingTransportTargets.empty()
                || !s.behaviors.contains(10) || s.behaviors.at(10).kind != RuntimeBehaviorKind::Attack
                || s.behaviors.at(10).targetObjectId != 3 || s.behaviors.at(10).forceFire)
                return fail("leading non-healers swallowed the repair command");
            for (int id : {1, 5}) if (s.behaviors.contains(id) && s.behaviors.at(id).kind == RuntimeBehaviorKind::Attack)
                return fail("mixed repair gave an allied attack to a non-healer");
            bool shot = false, welded = false, capped = false;
            for (int n = 0; n < hz * 25; ++n) {
                step(s); target = findMutableObjectById(s, 3);
                if (!target || target->health > cap || target->transportPassengerIds != manifest
                    || !s.pendingTransportTargets.empty() || !findObjectById(s, 1) || !findObjectById(s, 5))
                    return fail("mixed repair boarded a non-healer or changed target cargo");
                for (const auto& p : s.projectiles) if (p.attackerObjectId == 10 && p.weaponId == "RepairBullet") {
                    if (p.damage >= 0 || p.targetObjectId != 3) return fail("mixed repair projectile intent mismatch");
                    shot = true;
                }
                for (const auto& p : s.particles.systems()) if (p.sourceObjectId == 10 && p.typeId == "WeldingSys") welded = true;
                if (target->health == cap && shot && std::none_of(s.projectiles.begin(), s.projectiles.end(),
                    [](const auto& p) { return p.attackerObjectId == 10 && p.weaponId == "RepairBullet"; })) { capped = true; break; }
            }
            const auto* healer = findObjectById(s, 10); const auto* engineer = findRuntimeObjectByIdIncludingInactive(s, 2);
            const auto* cargo = findRuntimeObjectByIdIncludingInactive(s, 4);
            if (!capped || !welded || !healer || healer->transportPassengerIds != std::vector<int>{2}
                || !engineer || engineer->alive || !cargo || cargo->alive)
                return fail("mixed real healing/welding/cargo preservation incomplete");
            s.selectedObjectIds = {10};
            if (!dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop))
                return fail("mixed healer Stop rejected");
            return waitRepairEffects(s, 10);
        }(); finish(mixedPassed);
        label = "MTNK-" + std::to_string(hz) + "-allied-owner";
        const bool alliedPassed = [&]() {
            auto storage = make(hz, "MTNK", false, "British"); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            s.skirmishTeamByOwner["americans"] = s.skirmishTeamByOwner["british"] = "ifv-team";
            if (!board(s, 2, 1)) return false;
            auto* target = findMutableObjectById(s, 3); target->health -= 20; s.selectedObjectIds = {1}; stage = "allied repair";
            if (!runtimeOwnersAreAllied(s, "Americans", "British")
                || runtimeTargetCursorIntent(s, *target) != RuntimeTargetCursorIntent::UnitRepair || !command(s, 1, 3))
                return fail("allied house repair was rejected");
            return healRestored(s);
        }(); finish(alliedPassed);
        for (int invalid = 0; invalid < 6; ++invalid) {
            label = "forbidden-" + std::to_string(invalid) + '-' + std::to_string(hz);
            const bool passed = [&]() {
                const std::string type = invalid == 2 ? "E1" : invalid == 3 ? "SUB" : invalid == 4 ? "ZEP" : "MTNK";
                auto storage = make(hz, type, false, invalid == 0 ? "Russians" : "Americans",
                    invalid == 2 ? WorldObjectKind::Infantry : WorldObjectKind::Unit);
                auto& s = *storage; RuntimeSnapshotActiveStateScope active(s); if (!board(s, 2, 1)) return false;
                auto* target = findMutableObjectById(s, 3); if (invalid != 1) target->health -= 10;
                const int before = target->health, started = s.tick;
                const int cap = runtimeMaxHealthForObject(s, *target);
                const auto* targetRules = findRuntimeUnitType(s.rules, *target);
                const bool passiveHealing = targetRules && targetRules->selfHealing;
                const int passiveInterval = std::max(1, static_cast<int>(std::lround(
                    s.rules.getVeterancyRules().selfHealRateMinutes * 60.0 * hz)));
                s.selectedObjectIds = {1}; stage = "stock forbidden target";
                if (invalid != 5 && runtimeTargetCursorIntent(s, *target) == RuntimeTargetCursorIntent::UnitRepair)
                    return fail("forbidden target showed a repair cursor");
                (void)command(s, 1, 3, invalid == 5 ? RuntimeMultiplayerGameplayTargetOrderKind::Attack
                    : RuntimeMultiplayerGameplayTargetOrderKind::Primary);
                if (s.pendingTransportTargets.contains(1)) return fail("forbidden target boarded the source IFV");
                for (int n = 0; n < hz * 2; ++n) {
                    step(s); target = findMutableObjectById(s, 3);
                    // A Kirov's stock SelfHealing adds one HP per RepairRate
                    // boundary even when the repair command was rejected.
                    const int expected = std::min(cap, before + (passiveHealing ? s.tick / passiveInterval - started / passiveInterval : 0));
                    if (!target || target->health != expected || std::any_of(s.projectiles.begin(), s.projectiles.end(),
                        [](const auto& p) { return p.attackerObjectId == 1 && p.weaponId == "RepairBullet"; })
                        || std::any_of(s.recentExpiredProjectiles.begin(), s.recentExpiredProjectiles.end(),
                        [](const auto& p) { return p.attackerObjectId == 1 && p.weaponId == "RepairBullet"; }))
                        return fail("forbidden command created healing damage");
                }
                return true;
            }(); finish(passed);
        }
        label = "LCRF-" + std::to_string(hz) + "-healthy-boarding-preserved";
        const bool boardingPassed = [&]() {
            auto storage = make(hz, "LCRF"); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            if (!board(s, 2, 1)) return false; s.selectedObjectIds = {1}; stage = "healthy transport entry";
            const auto* target = findObjectById(s, 3);
            if (runtimeTargetCursorIntent(s, *target) != RuntimeTargetCursorIntent::Enter)
                return fail("full-health legal transport lost its enter cursor");
            (void)command(s, 1, 3);
            if (!s.pendingTransportTargets.contains(1)) return fail("full-health transport entry was swallowed by repair priority");
            for (int n = 0; n < hz * 20 && s.pendingTransportTargets.contains(1); ++n) step(s);
            const auto* fv = findRuntimeObjectByIdIncludingInactive(s, 1);
            target = findObjectById(s, 3); const auto* engineer = findRuntimeObjectByIdIncludingInactive(s, 2);
            if (!fv || fv->alive || !target || target->transportPassengerIds != std::vector<int>{1}
                || !engineer || engineer->alive || fv->transportPassengerIds != std::vector<int>{2})
                return fail("actual healthy boarding lost nested engineer cargo");
            return true;
        }(); finish(boardingPassed);
        for (int boundary = 0; boundary < 3; ++boundary) {
            label = "MTNK-" + std::to_string(hz) + (boundary == 0 ? "-stop" : boundary == 1 ? "-engineer-unload" : "-moving-target");
            const bool passed = [&]() {
                auto storage = make(hz, "MTNK"); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
                if (!board(s, 2, 1)) return false;
                auto* target = findMutableObjectById(s, 3); target->health -= 100; stage = "repair interruption";
                if (!command(s, 1, 3)) return fail("boundary repair command rejected");
                for (int n = 0; n < hz * 3 && s.projectiles.empty(); ++n) step(s);
                if (s.projectiles.empty()) return fail("boundary fixture did not launch a real repair round");
                if (boundary == 2) {
                    s.selectedObjectIds = {3};
                    if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, {71, 80}, nullptr))
                        return fail("real moving target command rejected");
                    if (!healRestored(s)) return false;
                    target = findMutableObjectById(s, 3);
                    return target->x != 77 || target->y != 75 ? true : fail("repair target never actually moved");
                }
                if (boundary == 0) { if (!stop(s)) return fail("repair Stop rejected"); }
                else if (!dispatchRuntimeSecondaryActionFromUi(s, 1)) return fail("actual engineer unload rejected");
                // The already-fired heal may finish; no later repair round is legal.
                std::set<int> existing; for (const auto& p : s.projectiles) if (p.attackerObjectId == 1) existing.insert(p.actorId);
                for (int n = 0; n < hz * 12; ++n) {
                    step(s);
                    for (const auto& p : s.projectiles) if (p.attackerObjectId == 1 && p.weaponId == "RepairBullet" && !existing.contains(p.actorId))
                        return fail("interrupted repair continued firing");
                }
                if (boundary == 1) {
                    const auto* engineer = findObjectById(s, 2); const auto* fv = findObjectById(s, 1);
                    if (!engineer || !fv || !fv->transportPassengerIds.empty() || fv->activeGunnerMode != -1)
                        return fail("unload did not restore original engineer/empty IFV");
                }
                return true;
            }(); finish(passed);
        }
    }
    for (const std::string required : {"FV", "MTNK", "HTK", "LCRF", "DEST", "SUB"})
        if (!repairedTypes.contains(required)) failures.push_back("IFV repair missing stock target coverage: " + required);
    if (!failures.empty()) {
        std::ostringstream summary; summary << failures.size() << " IFV repair scene(s) failed:";
        for (const auto& failure : failures) summary << '\n' << failure;
        error = summary.str(); return false;
    }
    std::cout << "IFV repair passed: scenes=" << scenes << " stockVehicleTypes=" << repairedTypes.size()
        << " actualRepairShots=" << shots << " ordinarySaves=" << saves << " exactSnapshots=" << snapshots
        << " nativeCaptures=" << captures << " true-boarding/cursor/context/negative-impact/welding/cap/cargo/stock-limits=ok\n";
    return true;
}
