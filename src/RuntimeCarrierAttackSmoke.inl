bool runRuntimeCarrierAttackSmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* output = std::getenv("CONCORD_CARRIER_ATTACK_OUTPUT");
    const bool diagnostic = std::getenv("CONCORD_CARRIER_ATTACK_DIAGNOSTIC") != nullptr;
    const std::string root = output ? output : "build/carrier-attack";
    std::filesystem::create_directories(root);
    std::string label;
    const auto fail = [&](const std::string& why) { error = "carrier attack " + label + ": " + why; return false; };
    IniFile native; RulesDatabase rules;
    if (!native.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(native)) return fail("stock rules");
    const auto* carrierRules = findRuntimeUnitTypeById(rules, "CARRIER");
    if (!carrierRules || carrierRules->spawns != "HORNET" || carrierRules->spawnsNumber != 3)
        return fail("stock carrier must carry three Hornets");
    constexpr int carrierSortieTimeoutSeconds = 30;
    int cases = 0, saves = 0, paired = 0, captures = 0;
    for (const std::string targetType : {"NAWEAP", "DRED"})
    for (int hz : {15, 30, 60}) for (bool noCooldown : {false, true}) {
        label = targetType + "-" + std::to_string(hz) + (noCooldown ? "-fast" : "-normal");
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.noCooldown = noCooldown;
        s.config.playerSlots.clear(); s.config.playerCountry = "Americans";
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.crates = s.config.shortGame = false;
        s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.rules = rules; s.art = live.art; s.theater = live.theater; s.report = live.report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.economy.owner = "Americans"; s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; initializeRuntimeLocalProductionLanes(s.economy);
        s.world = live.world; s.world.accessObjects().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        WorldObjectInstance parent; parent.instanceId = 1; parent.kind = WorldObjectKind::Unit;
        parent.type = "CARRIER"; parent.owner = "Americans"; parent.alive = true;
        parent.x = parent.y = 75; parent.cell = runtimeCellKey(75, 75);
        parent.health = carrierRules->strength; parent.mission = "Guard";
        parent.facing = parent.turretFacing = runtimeFacingFromDelta(1, 0);
        auto target = parent; target.instanceId = 2; target.owner = "Russians"; target.type = targetType;
        target.kind = targetType == "NAWEAP" ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        target.x = 90; target.cell = runtimeCellKey(target.x, target.y); target.mission = "Sleep";
        target.health = runtimeMaxHealthForObject(s, target); target.poweredOperational = true;
        const int originalHealth = target.health;
        s.world.accessObjects() = {parent, target}; s.selectedObjectIds = {1};
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        s.structureFootprints = buildRuntimeStructureFootprints(s);
        RuntimeSnapshotActiveStateScope active(s);
        advanceRuntimeSpecialMechanisms(s);
        const auto slots = s.specialMechanisms.carrierParents().at(1).childBySlot;
        std::set<int> ids;
        for (const auto& [slot, id] : slots) {
            const auto* child = findObjectById(s, id);
            if (!child || child->type != "HORNET" || child->currentAmmo != 1 || child->airborne)
                return fail("deck aircraft state");
            ids.insert(id);
        }
        if (ids.size() != 3) return fail("not exactly three distinct aircraft");
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {target.x, target.y}, findObjectById(s, 2)) || s.lastCommand.acceptedCount != 1)
            return fail("actual attack command rejected");
        const auto step = [](RuntimeWindowState& state) {
            RuntimeSnapshotActiveStateScope scope(state); ++state.tick; advanceRuntimeSimulation(state, false);
        };
        std::map<int, int> launches, shots; std::set<int> projectiles;
        std::map<int, std::set<int>> shotsByChild;
        const auto fullSortie = [&](const std::map<int, std::set<int>>& byChild) {
            return byChild.size() == ids.size() && std::all_of(ids.begin(), ids.end(), [&](int id) {
                const auto found = byChild.find(id);
                return found != byChild.end() && found->second.size() == 3;
            });
        };
        int firstDamage = -1, maxDistinct = 0, maxOutbound = 0, allFlyingTicks = 0;
        bool capturedFlight = false, capturedShot = false, stopped = false, returned = false;
        const auto save = [&](const std::map<int, std::set<int>>& alreadyFired) {
            std::ostringstream text; IniFile ini; auto restored = std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*restored, s, false);
            if (!writeRuntimeStateToStream(s, text, error) || !ini.loadFromString(text.str())
                || !materializeRuntimeLoadStateFromIni(*restored, ini, error)) return false;
            ++saves;
            // Seed already discharged actor IDs, including expired actors.
            // Observing a live saved projectile again cannot add another shot.
            auto restoredShots = alreadyFired;
            bool recalled = false;
            for (int n = 0; n < hz * carrierSortieTimeoutSeconds; ++n) {
                step(*restored); ++paired;
                const auto observeRestored = [&](const auto& actors) {
                    for (const auto& p : actors)
                        if (ids.contains(p.attackerObjectId) && p.weaponId == "HornetBomb")
                            restoredShots[p.attackerObjectId].insert(p.actorId);
                };
                observeRestored(restored->projectiles);
                observeRestored(restored->recentExpiredProjectiles);
                if (!recalled && fullSortie(restoredShots)) {
                    restored->selectedObjectIds = {1}; issueRuntimeStopCommand(*restored); recalled = true;
                }
                if (recalled && fullSortie(restoredShots) && originalHealth - findObjectById(*restored, 2)->health == 360
                    && std::none_of(restored->projectiles.begin(), restored->projectiles.end(), [&](const auto& p) {
                        return ids.contains(p.attackerObjectId) && p.weaponId == "HornetBomb";
                    })
                    && std::all_of(ids.begin(), ids.end(), [&](int id) {
                        const auto* child = findObjectById(*restored, id);
                        return child && !child->airborne && child->dockedToObjectId == 1
                            && child->currentAmmo == 1;
                    })) return true;
            }
            // Observation only: retain every original timeout and success predicate.
            const auto diagnose = [&](const RuntimeWindowState& q, const char* branch,
                    const std::map<int, std::set<int>>& observed, bool stoppedAtNine) {
                std::cout << std::setprecision(17) << "Carrier timeout " << label
                    << " branch=" << branch << " tick=" << q.tick
                    << " timeoutSeconds=" << carrierSortieTimeoutSeconds
                    << " matchFinished=" << q.skirmishMatchFinished
                    << " originalHP=" << originalHealth << " stoppedAtNine=" << stoppedAtNine
                    << " fullSortie=" << fullSortie(observed)
                    << " live=" << q.projectiles.size() << " expired=" << q.recentExpiredProjectiles.size() << '\n';
                for (int id : {1, 2}) {
                    const auto* object = findObjectById(q, id);
                    std::cout << "Carrier timeout body branch=" << branch << " id=" << id
                        << " exists=" << (object != nullptr);
                    if (object) std::cout << " type=" << object->type << " owner=" << object->owner
                        << " hp=" << object->health << " alive=" << object->alive
                        << " logical=" << object->x << ',' << object->y << ',' << object->level
                        << " physical=" << runtimeObjectRenderX(*object) << ',' << runtimeObjectRenderY(*object)
                        << ',' << runtimeObjectRenderLevel(*object) << " mission=" << object->mission;
                    std::cout << '\n';
                }
                bool allInside = true;
                for (int id : ids) {
                    const auto* child = findObjectById(q, id);
                    const auto state = q.specialMechanisms.carrierChildren().find(id);
                    const auto order = q.behaviors.find(id);
                    const auto actors = observed.find(id);
                    std::cout << "Carrier timeout child branch=" << branch << " id=" << id
                        << " exists=" << (child != nullptr);
                    if (child) std::cout << " hp=" << child->health << " alive=" << child->alive
                        << " ammo=" << child->currentAmmo << " air=" << child->airborne
                        << " dock=" << child->dockedToObjectId << " slot=" << child->dockIndex
                        << " logical=" << child->x << ',' << child->y << ',' << child->level
                        << " physical=" << runtimeObjectRenderX(*child) << ',' << runtimeObjectRenderY(*child)
                        << ',' << runtimeObjectRenderLevel(*child) << " hasPose=" << child->hasRenderPosition
                        << " moving=" << child->renderMoveActive << " mission=" << child->mission;
                    allInside = allInside && child && !child->airborne && child->dockedToObjectId == 1
                        && child->currentAmmo == 1;
                    if (state != q.specialMechanisms.carrierChildren().end())
                        std::cout << " phase=" << int(state->second.phase) << " budget=" << state->second.sortieShotsRemaining
                            << " magazine=" << state->second.ammoRemaining << " target=" << state->second.targetObjectId
                            << " actionTick=" << state->second.actionTick;
                    if (order != q.behaviors.end()) std::cout << " behavior=" << int(order->second.kind)
                        << " state=" << int(order->second.state) << " target=" << order->second.targetObjectId
                        << " weaponCooldown=" << order->second.weaponCooldownTicks
                        << " moveCooldown=" << order->second.moveCooldownTicks
                        << " pendingPath=" << order->second.pathRequestPending << " pathNodes=" << order->second.path.size()
                        << " renderRemaining=" << order->second.renderMoveTicksRemaining
                        << " fullOrder=" << runtimeMultiplayerSnapshotBehaviorRecord(id, order->second);
                    else std::cout << " behavior=absent";
                    std::cout << " actors=" << (actors == observed.end() ? 0 : actors->second.size()) << " actorIDs=";
                    if (actors != observed.end()) for (int actor : actors->second) std::cout << actor << ';';
                    std::cout << '\n';
                }
                bool hasLiveBomb = false;
                for (const auto& p : q.projectiles) {
                    const bool childBomb = ids.contains(p.attackerObjectId) && p.weaponId == "HornetBomb";
                    hasLiveBomb = hasLiveBomb || childBomb;
                    std::cout << "Carrier timeout projectile branch=" << branch << " actor=" << p.actorId
                        << " attacker=" << p.attackerObjectId << " target=" << p.targetObjectId
                        << " weapon=" << p.weaponId << " remaining=" << p.remainingTicks
                        << " childBomb=" << childBomb << '\n';
                }
                const auto* victim = findObjectById(q, 2);
                std::cout << "Carrier timeout predicates branch=" << branch << " stopped=" << stoppedAtNine
                    << " fullSortie=" << fullSortie(observed) << " damage=" << (victim ? originalHealth - victim->health : -1)
                    << " expectedDamage=360 liveChildBomb=" << hasLiveBomb << " allInsideAmmo1=" << allInside << '\n';
            };
            diagnose(s, "source-at-checkpoint", alreadyFired, false);
            diagnose(*restored, "restored-after-timeout", restoredShots, recalled);
            error = "restored attack did not finish three real bombs per aircraft";
            return false;
        };
        bool savedOutbound = false;
        const auto capture = [&](const std::string& phase) {
            const auto stats = drawRuntimeStateOffscreenAtCell(s, {82, 75}, 1280, 720, error,
                root + "/" + label + "-" + phase + ".bmp", false);
            ++captures;
            return stats && stats->rendered && stats->missingVoxelProxies == 0
                && stats->visibleVoxelBodies >= 4;
        };
        for (int n = 0; n < hz * carrierSortieTimeoutSeconds; ++n) {
            step(s);
            std::vector<std::pair<double, double>> poses; int airborne = 0;
            for (int id : ids) {
                const auto* child = findObjectById(s, id);
                if (!child || !child->alive) return fail("aircraft disappeared");
                if (child->airborne) {
                    ++airborne; launches.try_emplace(id, s.tick);
                    const std::pair<double, double> pose{runtimeObjectRenderX(*child), runtimeObjectRenderY(*child)};
                    if (std::none_of(poses.begin(), poses.end(), [&](const auto& old) {
                        return std::hypot(old.first - pose.first, old.second - pose.second) < .75;
                    })) poses.push_back(pose);
                }
            }
            if (airborne == 3) {
                ++allFlyingTicks; maxDistinct = std::max(maxDistinct, static_cast<int>(poses.size()));
                const bool allArmed = std::all_of(ids.begin(), ids.end(), [&](int id) {
                    return findObjectById(s, id)->currentAmmo == 1;
                });
                if (allArmed) maxOutbound = std::max(maxOutbound, static_cast<int>(poses.size()));
                const auto* first = findObjectById(s, *ids.begin());
                const bool allEmerged = std::all_of(ids.begin(), ids.end(), [&](int id) {
                    return s.specialMechanisms.carrierChildren().at(id).phase == CarrierChildPhase::Attacking;
                });
                if (allArmed && allEmerged && !capturedFlight && hz == 30 && !noCooldown
                    && std::hypot(runtimeObjectRenderX(*first) - parent.x, runtimeObjectRenderY(*first) - parent.y) > 3.0
                    && (diagnostic || poses.size() == 3)) {
                    if (!capture("flight")) return fail("native carrier and aircraft rendering");
                    capturedFlight = true;
                }
            }
            for (const auto& p : s.projectiles) {
                if (p.attackerObjectId == 1 && p.weaponId == "HornetLauncher")
                    return fail("carrier launched a fake direct weapon projectile");
                if (ids.contains(p.attackerObjectId) && p.weaponId == "HornetBomb") {
                    const bool firstShot = shots.try_emplace(p.attackerObjectId, s.tick).second; projectiles.insert(p.actorId);
                    shotsByChild[p.attackerObjectId].insert(p.actorId);
                    const auto* child = findObjectById(s, p.attackerObjectId);
                    if (firstShot && std::hypot(runtimeObjectRenderX(*child) - parent.x,
                            runtimeObjectRenderY(*child) - parent.y) < 3.0)
                        return fail("aircraft fired without flying toward target");
                    if (!capturedShot && hz == 30 && !noCooldown && airborne == 3) {
                        if (!capture("bomb")) return fail("native bomb rendering"); capturedShot = true;
                    }
                }
            }
            if (findObjectById(s, 2)->health < originalHealth && firstDamage < 0) firstDamage = s.tick;
            if (!savedOutbound && !launches.empty()) { if (!save(shotsByChild)) return fail("outbound save: " + error); savedOutbound = true; }
            if (!stopped && projectiles.size() == 9 && fullSortie(shotsByChild)) {
                issueRuntimeStopCommand(s);
                if (!save(shotsByChild)) return fail("return save: " + error);
                stopped = true;
            }
            if (stopped && airborne == 0 && firstDamage >= 0
                && std::none_of(s.projectiles.begin(), s.projectiles.end(), [&](const auto& p) {
                    return ids.contains(p.attackerObjectId) && p.weaponId == "HornetBomb";
                })) { returned = true; break; }
        }
        std::set<int> launchTimes; for (const auto& [id, tick] : launches) launchTimes.insert(tick);
        std::cout << "Carrier " << label << " planes=" << ids.size() << " launches=" << launches.size()
            << " distinctLaunchTimes=" << launchTimes.size() << " visibleSeparated=" << maxDistinct
            << " outboundSeparated=" << maxOutbound << " allFlyingTicks=" << allFlyingTicks << " firingPlanes=" << shots.size()
            << " projectiles=" << projectiles.size() << " damage=" << originalHealth - findObjectById(s, 2)->health
            << " firstDamage=" << firstDamage << " returned=" << returned << '\n';
        if (launches.size() != 3 || shots.size() != 3 || projectiles.size() != 9 || !fullSortie(shotsByChild) || !returned
            || originalHealth - findObjectById(s, 2)->health != 360)
            return fail("three aircraft with three distinct real bombs each, impact and return");
        if (hz == 30 && !noCooldown && (!capturedFlight || !capturedShot)) return fail("flight or bombing capture missing");
        if (!diagnostic && maxOutbound != 3)
            return fail("three aircraft visually overlap");
        ++cases;
    }
    std::cout << "Carrier attack " << (diagnostic ? "diagnostic" : "passed") << ": cases=" << cases
        << " fullSaves=" << saves << " restoredTicks=" << paired << " nativeCaptures=" << captures
        << " three-real-aircraft/flight/nine-individual-bombs/damage360/return=ok\n";
    return true;
}
