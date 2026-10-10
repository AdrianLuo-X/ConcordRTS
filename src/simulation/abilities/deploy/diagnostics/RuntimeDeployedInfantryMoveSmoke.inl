// Exercise the same selection D, mouse secondary and player order routes as
// gameplay. Standing up is a separate action from ordering deployed infantry.
bool runRuntimeDeployedInfantryMoveSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->config.playerSlots.clear(); assets->config.computerAiEnabled = false;
    assets->config.gameModeRulesOverride.clear();
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) { error = "Deployed infantry assets: " + assets->report.errorMessage; return false; }
    IniFile stock;
    if (!stock.loadFromFile(assets->report.rulesPath) || !assets->rules.loadFromIni(stock)) {
        error = "Deployed infantry original rules unavailable"; return false;
    }
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    int landTile = -1;
    for (int tile = 0; tile < static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(*assets, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) { landTile = tile; break; }
    }
    if (landTile < 0) { error = "Deployed infantry original ground unavailable"; return false; }
    const char* output = std::getenv("CONCORD_DEPLOYED_INFANTRY_OUTPUT");
    std::string label, stage; std::vector<std::string> failures;
    int scenes = 0, saves = 0, snapshots = 0, shots = 0, captures = 0;
    const auto fail = [&](const std::string& why) { error = "Deployed infantry " + label + " [" + stage + "]: " + why; return false; };
    const auto pose = [](const WorldObjectInstance& object) {
        return std::array<double, 3>{runtimeObjectRenderX(object), runtimeObjectRenderY(object), runtimeObjectRenderLevel(object)};
    };
    const auto samePose = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
        return std::abs(a[0] - b[0]) < 1e-8 && std::abs(a[1] - b[1]) < 1e-8 && std::abs(a[2] - b[2]) < 1e-8;
    };
    const auto diagnostic = [&](const RuntimeWindowState& s) {
        std::ostringstream out; out << "tick=" << s.tick << " status=" << s.simulationStatus;
        if (const auto* o = findObjectById(s, 1)) out << " mission=" << o->mission << " deployed=" << o->deployed
            << " xyz=" << runtimeObjectRenderX(*o) << ',' << runtimeObjectRenderY(*o) << ',' << runtimeObjectRenderLevel(*o)
            << " logical=" << o->x << ',' << o->y << " anim=" << o->buildAnimTicksRemaining << '/' << o->buildAnimTicksTotal
            << " deployTimer=" << o->deployStateTicksRemaining;
        if (const auto b = s.behaviors.find(1); b != s.behaviors.end()) out << " behavior=" << static_cast<int>(b->second.kind)
            << '/' << static_cast<int>(b->second.state) << " target=" << b->second.targetObjectId
            << " move=" << b->second.moveTarget.x << ',' << b->second.moveTarget.y << " path=" << b->second.path.size();
        return out.str();
    };
    const auto make = [&](const std::string& type, int hz, bool closeEnemy = false,
        const std::string& entryHost = {}, WorldObjectKind sourceKind = WorldObjectKind::Infantry) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = assets->config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.multiplayerActive = s.config.shortGame = s.config.crates = s.config.superweapons = false;
        s.config.gameMode = "Battle"; s.config.playerCountry = "Americans"; s.config.skirmishLoadSavePath.clear();
        s.report = assets->report; s.rules = assets->rules; s.art = assets->art; s.theater = assets->theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.world = assets->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts.theater = assets->mapFacts.theater; s.mapFacts.lighting = assets->mapFacts.lighting;
        for (int y = 64; y <= 112; ++y) for (int x = 64; x <= 112; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        const auto add = [&](int id, const std::string& unitType, WorldObjectKind kind, RuntimeCellPoint cell, const char* owner) {
            WorldObjectInstance o; o.instanceId = id; o.mapId = "DeployedInfantry" + std::to_string(id);
            o.type = unitType; o.kind = kind; o.owner = owner; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(cell.x, cell.y); o.alive = o.poweredOperational = true;
            o.health = runtimeMaxHealthForObject(s, o); o.mission = "Sleep";
            o.facing = runtimeFacingFromDelta(1, 0); o.turretFacing = -1;
            initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
            runtimeInitializeModExtensions(o, s.rules, hz); s.world.accessObjects().push_back(std::move(o));
        };
        add(1, type, sourceKind, {75, 75}, "Americans");
        // Stock unarmed car permits real hostile damage without retaliation.
        add(2, "CAR", WorldObjectKind::Unit, {closeEnemy ? 78 : 99, 75}, "Russians");
        add(3, "MTNK", WorldObjectKind::Unit, {84, 82}, "Americans");
        if (!entryHost.empty()) {
            add(4, entryHost, entryHost == "HTK" ? WorldObjectKind::Unit : WorldObjectKind::Structure,
                {80, 75}, "Americans");
            add(997, "GAPOWR", WorldObjectKind::Structure, {99, 99}, "Americans");
        }
        add(998, "NAPOWR", WorldObjectKind::Structure, {103, 103}, "Russians");
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        return storage;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick;
        advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto secondary = [](RuntimeWindowState& s, bool key) {
        s.selectedObjectIds = {1};
        return key ? dispatchRuntimeSecondaryActionForSelectionFromUi(s) : dispatchRuntimeSecondaryActionFromUi(s, 1);
    };
    const auto move = [](RuntimeWindowState& s, RuntimeMultiplayerGameplayTargetOrderKind order = RuntimeMultiplayerGameplayTargetOrderKind::Move) {
        s.selectedObjectIds = {1}; return dispatchRuntimeTargetOrderFromUi(s, order, {80, 79}, nullptr);
    };
    const auto stationaryDeployed = [&](const RuntimeWindowState& s, const std::array<double, 3>& anchor) {
        const auto* o = findObjectById(s, 1);
        if (!o || !samePose(anchor, pose(*o)) || !o->deployed || runtimeMissionIsUndeploying(*o))
            return fail("order moved or automatically folded deployed infantry: " + diagnostic(s));
        return true;
    };
    const auto deploy = [&](RuntimeWindowState& s, bool key) {
        stage = key ? "D deploy" : "mouse deploy"; const auto anchor = pose(*findObjectById(s, 1));
        if (!secondary(s, key) || !samePose(anchor, pose(*findObjectById(s, 1))))
            return fail("deployment input moved the continuous body pose: " + diagnostic(s));
        for (int n = 0; n < s.config.fixedTickHz * 5; ++n) {
            step(s); const auto* o = findObjectById(s, 1);
            if (!o || !samePose(anchor, pose(*o))) return fail("deployment animation displaced the body: " + diagnostic(s));
            if (o->deployed && !runtimeMissionIsDeploying(*o)) return true;
        }
        return fail("real deployment did not complete: " + diagnostic(s));
    };
    const auto persistence = [&](RuntimeWindowState& source, bool transitioning) {
        stage = transitioning ? "undeploy transition save" : "deployed save";
        std::ostringstream ordinaryText, exactText, canonical; IniFile ordinaryIni, exactIni;
        if (!writeRuntimeStateToStream(source, ordinaryText, error)
            || !writeRuntimeStateToStream(source, exactText, error)
            || !writeRuntimeMultiplayerSnapshotExtension(source, exactText, error)
            || !ordinaryIni.loadFromString(ordinaryText.str()) || !exactIni.loadFromString(exactText.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error)
            || !exactIni.loadFromString(canonical.str())) return fail("checkpoint write: " + error);
        const auto restore = [&](bool exact) {
            auto result = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*result, source, false);
            if (!materializeRuntimeLoadStateFromIni(*result, ordinaryIni, error)
                || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*result, exactIni, error)))
                return std::unique_ptr<RuntimeWindowState>{};
            return result;
        };
        auto ordinary = restore(false), exact = restore(true), reference = restore(true);
        if (!ordinary || !exact || !reference) return fail("checkpoint restore: " + error);
        const auto anchor = pose(*findObjectById(source, 1));
        for (auto* restored : {ordinary.get(), exact.get(), reference.get()}) {
            const auto* a = findObjectById(source, 1); const auto* b = findObjectById(*restored, 1);
            if (!b || !samePose(pose(*a), pose(*b)) || a->deployed != b->deployed
                || a->mission != b->mission || a->buildAnimTicksRemaining != b->buildAnimTicksRemaining)
                return fail("save restarted the deploy/undeploy animation or changed pose");
            const int remaining = b->buildAnimTicksRemaining;
            (void)move(*restored, RuntimeMultiplayerGameplayTargetOrderKind::Primary);
            b = findObjectById(*restored, 1);
            if (!b || (transitioning && (!runtimeMissionIsUndeploying(*b)
                || b->buildAnimTicksRemaining != remaining))) return fail("restored Move restarted standing animation");
        }
        bool walkedAfterStanding = false;
        for (int n = 0; n < source.config.fixedTickHz * 2; ++n) {
            step(*ordinary); step(*exact); step(*reference);
            for (auto* restored : {ordinary.get(), exact.get(), reference.get()}) {
                const auto* o = findObjectById(*restored, 1); const auto* ref = findObjectById(*reference, 1);
                if (!o || !ref || !samePose(pose(*ref), pose(*o)) || o->deployed != ref->deployed
                    || runtimeMissionIsUndeploying(*o) != runtimeMissionIsUndeploying(*ref)
                    || o->buildAnimTicksRemaining != ref->buildAnimTicksRemaining)
                    return fail("restored deployment state moved or diverged: " + diagnostic(*restored));
                if ((!transitioning || runtimeMissionIsUndeploying(*o)) && !samePose(anchor, pose(*o)))
                    return fail("restored deployment moved before explicit standing completed");
                if (transitioning && !o->deployed && !runtimeMissionIsUndeploying(*o) && !samePose(anchor, pose(*o)))
                    walkedAfterStanding = true;
            }
        }
        if (transitioning && !walkedAfterStanding) return fail("restored queued Move did not run after standing completed");
        ++saves; ++snapshots; return true;
    };
    const auto standAndWalk = [&](RuntimeWindowState& s, bool key, bool saveTransition) {
        stage = key ? "explicit D stand" : "explicit mouse stand";
        const auto anchor = pose(*findObjectById(s, 1));
        if (!secondary(s, key) || !runtimeMissionIsUndeploying(*findObjectById(s, 1))
            || !samePose(anchor, pose(*findObjectById(s, 1)))) return fail("explicit stand transition missing/teleported");
        if (saveTransition && !persistence(s, true)) return false;
        bool stood = false;
        for (int n = 0; n < s.config.fixedTickHz * 5; ++n) {
            const auto* o = findObjectById(s, 1);
            if (!runtimeMissionIsUndeploying(*o)) { stood = !o->deployed; break; }
            const int remaining = o->buildAnimTicksRemaining;
            (void)move(s); o = findObjectById(s, 1);
            if (!o || !runtimeMissionIsUndeploying(*o) || o->buildAnimTicksRemaining != remaining)
                return fail("repeated Move restarted or bypassed explicit standing animation");
            step(s); o = findObjectById(s, 1);
            if (!o || (runtimeMissionIsUndeploying(*o) && !samePose(anchor, pose(*o))))
                return fail("standing animation moved before completing: " + diagnostic(s));
        }
        if (!stood) return fail("explicit stand never completed");
        stage = "standing normal movement";
        if (!move(s)) return fail("standing unit rejected a normal move");
        for (int n = 0; n < s.config.fixedTickHz * 25; ++n) {
            step(s); const auto* o = findObjectById(s, 1);
            if (!o || o->deployed) return fail("normal standing movement redeployed unexpectedly");
            if (runtimeCellPointForObject(*o) == RuntimeCellPoint{80, 79} && !o->renderMoveActive)
                return !samePose(anchor, pose(*o)) ? true : fail("standing actor never moved");
        }
        return fail("standing actor did not reach real destination: " + diagnostic(s));
    };
    const auto finish = [&](bool passed) {
        if (passed) { ++scenes; std::cout << "Deployed infantry " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    const std::array<const char*, 9> commands{{"primary-move", "move", "attack-move", "guard-destination",
        "scatter", "follow", "queued-move", "stop", "far-enemy"}};
    for (const std::string type : {"E1", "DESO", "GGI"}) for (int hz : {30, 60}) for (int order = 0; order < 9; ++order) {
        label = type + '-' + std::to_string(hz) + '-' + commands[order];
        const bool passed = [&]() {
            auto storage = make(type, hz); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            const auto* rules = findRuntimeUnitTypeById(s.rules, type);
            if (!rules || !rules->deployer) return fail("original infantry deploy rules missing");
            const bool key = (order % 2) == 0;
            if (!deploy(s, key)) return false; const auto anchor = pose(*findObjectById(s, 1));
            if (order == 0 && !persistence(s, false)) return false;
            stage = std::string("blocked ") + commands[order]; s.selectedObjectIds = {1};
            if (order <= 3) (void)move(s, order == 0 ? RuntimeMultiplayerGameplayTargetOrderKind::Primary
                : order == 1 ? RuntimeMultiplayerGameplayTargetOrderKind::Move
                : order == 2 ? RuntimeMultiplayerGameplayTargetOrderKind::AttackMove : RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination);
            else if (order == 4) (void)dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Scatter);
            else if (order == 5 || order == 8) {
                const auto* target = findObjectById(s, order == 5 ? 3 : 2);
                (void)dispatchRuntimeTargetOrderFromUi(s, order == 5 ? RuntimeMultiplayerGameplayTargetOrderKind::Follow
                    : RuntimeMultiplayerGameplayTargetOrderKind::Primary, runtimeCellPointForObject(*target), target);
            } else if (order == 6) { s.playerLocalCommands.planning = true; (void)move(s); s.playerLocalCommands.planning = false; }
            else (void)dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop);
            if (!stationaryDeployed(s, anchor)) return false;
            for (int n = 0; n < hz * 3; ++n) { step(s); if (!stationaryDeployed(s, anchor)) return false; }
            if (output && hz == 30 && order == 0) {
                const auto stats = drawRuntimeStateOffscreenAtCell(s, {75, 75}, 1000, 700, error,
                    std::string(output) + '/' + label + ".bmp", false);
                if (!stats || !stats->rendered) return fail("original deployed infantry render: " + error); ++captures;
            }
            return standAndWalk(s, !key, order == 1);
        }(); finish(passed);
    }
    for (const std::string type : {"E1", "DESO", "GGI"}) for (int hz : {30, 60}) {
        label = type + '-' + std::to_string(hz) + "-near-deployed-weapon";
        const bool attackPassed = [&]() {
            auto storage = make(type, hz, true); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            const int before = findObjectById(s, 2)->health;
            if (!deploy(s, true)) return false; const auto anchor = pose(*findObjectById(s, 1));
            const auto* unit = findRuntimeUnitTypeById(s.rules, type); const auto* target = findObjectById(s, 2);
            const auto profile = runtimeWeaponProfileForObject(s, *findObjectById(s, 1), target);
            if (!profile.deployed || !profile.weapon || profile.weaponId != runtimeDeployedWeaponId(*unit))
                return fail("close enemy did not select the original deployed weapon");
            s.selectedObjectIds = {1}; stage = "close enemy attack";
            (void)dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary, {78, 75}, target);
            std::set<int> actors; bool hit = false;
            for (int n = 0; n < hz * 8; ++n) {
                step(s); if (!stationaryDeployed(s, anchor)) return false;
                for (const auto& p : s.projectiles) if (p.attackerObjectId == 1 && p.weaponId == profile.weaponId) actors.insert(p.actorId);
                const auto* victim = findRuntimeObjectByIdIncludingInactive(s, 2);
                if (victim && victim->health < before) { hit = true; break; }
            }
            // The native radiation pulse is an area effect and may have
            // applied during the deploy completion tick itself.
            if (!hit || (type != "DESO" && actors.empty())) return fail("deployed weapon did not actually damage a close enemy");
            shots += static_cast<int>(actors.size()); s.selectedObjectIds = {1};
            (void)dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop);
            return stationaryDeployed(s, anchor);
        }(); finish(attackPassed);
        label = type + '-' + std::to_string(hz) + "-deploy-mid-move-pose";
        const bool movingPassed = [&]() {
            auto storage = make(type, hz); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            stage = "real moving segment"; if (!move(s)) return fail("initial normal move rejected");
            bool found = false;
            for (int n = 0; n < hz * 3; ++n) {
                step(s); const auto* o = findObjectById(s, 1); const auto b = s.behaviors.find(1);
                if (o && o->renderMoveActive && b != s.behaviors.end() && b->second.moveCooldownTicks > 0
                    && (std::abs(runtimeObjectRenderX(*o) - o->x) > .01 || std::abs(runtimeObjectRenderY(*o) - o->y) > .01)) { found = true; break; }
            }
            if (!found) return fail("no genuine fractional infantry movement segment observed");
            if (!deploy(s, true)) return false; const auto anchor = pose(*findObjectById(s, 1));
            if (!persistence(s, false)) return false;
            for (int n = 0; n < hz; ++n) { step(s); if (!stationaryDeployed(s, anchor)) return false; }
            return standAndWalk(s, false, false);
        }(); finish(movingPassed);
    }
    for (const std::string type : {"E1", "DESO", "GGI"}) for (int hz : {30, 60})
    for (const std::string hostType : {"HTK", "NABNKR", "YAGRND"}) for (bool duringDeploy : {false, true}) {
        label = type + '-' + std::to_string(hz) + '-' + hostType + (duringDeploy ? "-deploying-entry" : "-deployed-entry");
        const bool entryPassed = [&]() {
            auto storage = make(type, hz, false, hostType); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            const auto* host = findObjectById(s, 4);
            if (!host || (hostType == "HTK" ? !findRuntimeUnitTypeById(s.rules, hostType)
                : !findRuntimeBuildingTypeById(s.rules, hostType))) return fail("original entry host rules missing");
            const auto anchor = pose(*findObjectById(s, 1));
            if (duringDeploy) {
                stage = "real deployment entry boundary";
                if (!secondary(s, true) || !runtimeMissionIsDeploying(*findObjectById(s, 1)))
                    return fail("real deployment animation not active for entry boundary");
            } else if (!deploy(s, false)) return false;
            stage = "blocked primary host entry"; s.selectedObjectIds = {1};
            host = findObjectById(s, 4);
            (void)dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*host), host);
            const auto stillOutside = [&]() {
                const auto* o = findObjectById(s, 1); const auto* h = findObjectById(s, 4);
                const auto behavior = s.behaviors.find(1);
                if (!o || !h || !o->alive || !samePose(anchor, pose(*o)) || runtimeMissionIsUndeploying(*o)
                    || (!o->deployed && !runtimeMissionIsDeploying(*o))
                    || s.pendingTransportTargets.contains(1) || s.pendingOccupyTargets.contains(1)
                    || o->garrisonHostObjectId >= 0
                    || std::find(h->transportPassengerIds.begin(), h->transportPassengerIds.end(), 1) != h->transportPassengerIds.end()
                    || std::find(h->garrisonOccupantIds.begin(), h->garrisonOccupantIds.end(), 1) != h->garrisonOccupantIds.end()
                    || (behavior != s.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Move)
                    || std::any_of(s.grinding.relations().begin(), s.grinding.relations().end(),
                        [](const auto& relation) { return relation.sourceId == 1; }))
                    return fail("host entry bypassed manual undeploy: " + diagnostic(s));
                return true;
            };
            if (!stillOutside()) return false;
            for (int n = 0; n < hz * 3; ++n) { step(s); if (!stillOutside()) return false; }
            return true;
        }(); finish(entryPassed);
    }
    for (int hz : {30, 60}) {
        label = "CAOS-" + std::to_string(hz) + "-vehicle-auto-fold";
        const bool vehiclePassed = [&]() {
            auto storage = make("CAOS", hz, false, {}, WorldObjectKind::Unit); auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            if (!findRuntimeUnitTypeById(s.rules, "CAOS")) return fail("original Chaos Drone rules missing");
            if (!deploy(s, true)) return false;
            const auto anchor = pose(*findObjectById(s, 1)); stage = "vehicle ordinary Move automatically folds";
            if (!move(s) || !runtimeMissionIsUndeploying(*findObjectById(s, 1)))
                return fail("vehicle lost its existing automatic fold-on-Move contract");
            for (int n = 0; n < hz * 25; ++n) {
                step(s); const auto* o = findObjectById(s, 1);
                if (!o) return fail("vehicle vanished during ordinary movement");
                if (runtimeMissionIsUndeploying(*o) && !samePose(anchor, pose(*o)))
                    return fail("vehicle moved before its automatic fold animation completed");
                if (!o->deployed && runtimeCellPointForObject(*o) == RuntimeCellPoint{80, 79} && !o->renderMoveActive)
                    return true;
            }
            return fail("vehicle failed to move after automatically folding: " + diagnostic(s));
        }(); finish(vehiclePassed);
    }
    if (!failures.empty()) {
        std::ostringstream summary; summary << failures.size() << " deployed infantry scene(s) failed:";
        for (const auto& failure : failures) summary << '\n' << failure;
        error = summary.str(); return false;
    }
    std::cout << "Deployed infantry passed: scenes=" << scenes << " ordinarySaves=" << saves << " exactSnapshots=" << snapshots
        << " deployedWeaponActors=" << shots << " nativeCaptures=" << captures
        << " D/mouse/blocked-orders/stop/no-chase/explicit-stand/transition/save/continuous-pose/host-entry/vehicle-auto-fold=ok\n";
    return true;
}
