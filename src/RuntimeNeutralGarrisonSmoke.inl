bool runRuntimeNeutralGarrisonSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "neutral garrison: " + message;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config;
    probe.config.fixedTickHz = 30;
    probe.config.defaultMapPath = "neutral-garrison-probe";
    probe.rules = live.rules;
    probe.rulesLoaded = true;
    probe.art = live.art;
    probe.artLoaded = true;
    probe.economy.owner = "Americans";
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.triggers.bootstrapComplete = true;
    probe.economy.skirmishStartApplied = true;
    for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 24; ++x) {
        TerrainCell cell;
        cell.x = x;
        cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y);
        cell.tileIndex = 0;
        cell.subTileIndex = 0;
        probe.world.accessTerrainCells().push_back(cell);
    }
    auto* host = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, "CAPRS03", {12, 12});
    if (host == nullptr) return fail("stock civilian host missing");
    const int hostId = host->instanceId;
    host->owner = "Neutral";
    host->mission = "Guard";
    host->buildAnimTicksTotal = host->buildAnimTicksRemaining = 0;
    clearRuntimeRenderPosition(*host);
    const int capacity = runtimeOccupyProfileForObject(probe, *host).maxOccupants;
    if (capacity != 10) return fail("stock Louvre capacity changed");
    std::vector<int> eligibleIds;
    const std::array<const char*, 3> types{"E1", "E2", "INIT"};
    for (int index = 0; index <= capacity; ++index) {
        auto* unit = addRuntimeObjectInstance(probe, RuntimeBuildKind::Infantry,
            types[index % types.size()], {2 + index % 4, 2 + index / 4});
        if (unit == nullptr) return fail("stock occupier missing");
        eligibleIds.push_back(unit->instanceId);
        unit->mission = "Guard";
        clearRuntimeRenderPosition(*unit);
    }
    std::vector<int> ineligibleIds;
    for (const char* type : {"GGI", "GHOST", "ENGINEER", "JUMPJET"}) {
        auto* unit = addRuntimeObjectInstance(probe, RuntimeBuildKind::Infantry,
            type, {7, 2 + static_cast<int>(ineligibleIds.size())});
        if (unit == nullptr) return fail("stock non-occupier missing");
        ineligibleIds.push_back(unit->instanceId);
        unit->mission = "Guard";
        clearRuntimeRenderPosition(*unit);
    }
    auto* tank = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, "MTNK", {9, 12});
    if (tank == nullptr) return fail("stock tank missing");
    const int tankId = tank->instanceId;
    tank->mission = "Guard";
    clearRuntimeRenderPosition(*tank);
    initializeRuntimeNavigation(probe);
    ensureRuntimeGarrisonsInitialized(probe);
    host = findMutableObjectById(probe, hostId);
    for (const int id : eligibleIds) {
        applySelection(probe, {id}, false, false);
        if (!runtimeObjectCanOccupyTarget(probe, *findObjectById(probe, id), *host)
            || runtimeTargetCursorIntent(probe, *host) != RuntimeTargetCursorIntent::Enter)
            return fail("stock occupier has no neutral building entry cursor");
    }
    for (const int id : ineligibleIds) {
        if (runtimeObjectCanOccupyTarget(probe, *findObjectById(probe, id), *host))
            return fail("non-Occupier infantry was allowed inside");
    }
    // Combat deliberately changes health and ownership. Keep those fixture
    // mutations separate from the untouched save/load and group-entry world.
    auto attackStorage = std::make_unique<RuntimeWindowState>();
    attackStorage->config = probe.config;
    attackStorage->rules = probe.rules;
    attackStorage->rulesLoaded = true;
    attackStorage->art = probe.art;
    attackStorage->artLoaded = true;
    attackStorage->world = probe.world;
    attackStorage->economy.owner = probe.economy.owner;
    attackStorage->economy.skirmishStartApplied = true;
    attackStorage->runtimeSuppressPlayerAudioFeedback = true;
    attackStorage->triggers.bootstrapComplete = true;
    initializeRuntimeNavigation(*attackStorage);
    ensureRuntimeGarrisonsInitialized(*attackStorage);
    const auto checkAttacks = [&](RuntimeWindowState& probe) {
    auto* host = findMutableObjectById(probe, hostId);
    const auto attackCursor = [&](const WorldObjectInstance& target) {
        const auto intent = runtimeTargetCursorIntent(probe, target);
        return intent == RuntimeTargetCursorIntent::Attack
            || intent == RuntimeTargetCursorIntent::AttackOutOfRange;
    };
    const int redHealth = static_cast<int>(host->health
        * probe.rules.getSpecialMechanismRules().conditionRed);
    const int fullHealth = host->health;
    applySelection(probe, {eligibleIds.front()}, false, false);
    issueRuntimeAttackCommand(probe, *host);
    if (probe.lastCommand.acceptedCount != 0)
        return fail("occupier attacked a usable empty civilian house");
    // A full reservation list must not make a usable house an attack target.
    for (int index = 0; index < capacity; ++index)
        probe.pendingOccupyTargets[eligibleIds[index]] = hostId;
    if (attackCursor(*host)) return fail("reserved house advertised an ordinary attack");
    probe.pendingOccupyTargets.clear();
    host->health = redHealth + 1;
    if (runtimeTargetCursorIntent(probe, *host) != RuntimeTargetCursorIntent::Enter)
        return fail("entry stopped above the red threshold");
    host->health = redHealth;
    if (!attackCursor(*host)) return fail("red house did not advertise an attack");
    issueRuntimeCommand(probe, runtimeCellPointForObject(*host), host);
    if (probe.lastCommand.acceptedCount != 1
        || !probe.behaviors.contains(eligibleIds.front())
        || probe.behaviors.at(eligibleIds.front()).kind != RuntimeBehaviorKind::Attack)
        return fail("red house click did not issue an attack");
    // An existing ordinary attack must stop if repairs make entry possible.
    host->health = fullHealth;
    ++probe.tick;
    advanceRuntimeSimulation(probe);
    if (probe.behaviors.contains(eligibleIds.front())
        && probe.behaviors.at(eligibleIds.front()).kind == RuntimeBehaviorKind::Attack)
        return fail("occupier kept attacking a repaired civilian house");
    eraseRuntimeBehavior(probe, eligibleIds.front());
    host = findMutableObjectById(probe, hostId);
    issueRuntimeAttackCommand(probe, *host, true);
    if (probe.lastCommand.acceptedCount != 1)
        return fail("Ctrl force attack was blocked by entry preference");
    eraseRuntimeBehavior(probe, eligibleIds.front());
    host->owner = "Russians";
    host->garrisonOccupantIds = {999999};
    if (!attackCursor(*host)) return fail("hostile occupied house blocked infantry attack");
    host->garrisonOccupantIds.clear();
    host->owner = "Neutral";
    applySelection(probe, {ineligibleIds.front()}, false, false);
    if (!attackCursor(*host)) return fail("non-occupier infantry inherited entry preference");
    applySelection(probe, {tankId}, false, false);
    for (const int health : {fullHealth, redHealth + 1, redHealth}) {
        host->health = health;
        if (!attackCursor(*host)) return fail("tank attack cursor depended on house health");
    }
    host->health = fullHealth;
    issueRuntimeCommand(probe, runtimeCellPointForObject(*host), host);
    if (probe.lastCommand.acceptedCount != 1 || probe.pendingOccupyTargets.contains(tankId))
        return fail("tank click did not issue an ordinary attack");
    for (int step = 0; step < 300 && host->health == fullHealth; ++step) {
        ++probe.tick;
        advanceRuntimeSimulation(probe);
        host = findMutableObjectById(probe, hostId);
        if (host == nullptr) return fail("tank destroyed its full-health fixture unexpectedly");
    }
    if (host->health >= fullHealth) return fail("tank ordinary attack never damaged civilian house");
    eraseRuntimeBehavior(probe, tankId);
    findMutableObjectById(probe, tankId)->mission = "Guard";
    auto* attackingInfantry = findMutableObjectById(probe, eligibleIds.front());
    setRuntimeObjectCell(probe, *attackingInfantry, {11, 12});
    clearRuntimeRenderPosition(*attackingInfantry);
    applySelection(probe, {eligibleIds.front()}, false, false);
    host->health = redHealth;
    if (runtimeTargetCursorIntent(probe, *host) != RuntimeTargetCursorIntent::Attack)
        return fail("nearby red house did not show the attack crosshair");
    issueRuntimeCommand(probe, runtimeCellPointForObject(*host), host);
    for (int step = 0; step < 300 && host->health == redHealth; ++step) {
        ++probe.tick;
        advanceRuntimeSimulation(probe);
        host = findMutableObjectById(probe, hostId);
        if (host == nullptr) return fail("infantry destroyed its threshold fixture unexpectedly");
    }
    if (host->health >= redHealth) return fail("infantry ordinary attack never damaged red house");
    eraseRuntimeBehavior(probe, eligibleIds.front());
    attackingInfantry = findMutableObjectById(probe, eligibleIds.front());
    setRuntimeObjectCell(probe, *attackingInfantry, {2, 2});
    clearRuntimeRenderPosition(*attackingInfantry);
    attackingInfantry->mission = "Guard";
    host->health = fullHealth;
    return true;
    };
    if (!checkAttacks(*attackStorage)) return false;
    const auto* first = findObjectById(probe, eligibleIds.front());
    const int normalHealth = host->health;
    host->health = 1;
    if (runtimeObjectCanOccupyTarget(probe, *first, *host))
        return fail("red health building accepted entry");
    host->health = normalHealth;
    host->owner = "Russians";
    if (runtimeObjectCanOccupyTarget(probe, *first, *host))
        return fail("enemy owned building accepted entry");
    host->owner = "Neutral";
    for (const char* alias : {"Civilian", "Civilians", "Special"}) {
        host->owner = alias;
        if (!runtimeObjectCanOccupyTarget(probe, *first, *host))
            return fail("neutral house alias rejected");
    }
    host->owner = "Neutral";
    std::vector<int> group = eligibleIds;
    group.insert(group.end(), ineligibleIds.begin(), ineligibleIds.end());
    std::reverse(group.begin(), group.end());
    applySelection(probe, group, false, false);
    if (!queueRuntimeOccupyTarget(probe, *host)
        || probe.pendingOccupyTargets.size() != static_cast<std::size_t>(capacity)
        || host->owner != "Neutral"
        || probe.pendingOccupyTargets.contains(eligibleIds.back()))
        return fail("group entry did not reserve exactly the available capacity");
    for (const int id : ineligibleIds) {
        if (probe.pendingOccupyTargets.contains(id)) return fail("mixed group queued a non-occupier");
    }
    // Reissuing the order must exclude each soldier's own reservation.
    if (!queueRuntimeOccupyTarget(probe, *host)
        || probe.pendingOccupyTargets.size() != static_cast<std::size_t>(capacity))
        return fail("reissued group entry lost its reservations");

    // Save before anybody enters: a pending neutral entry cannot be rejected
    // by the saved-intent owner validator or claim ownership during load.
    const std::string savePath = "runtime_neutral_garrison_smoke.ini";
    if (!saveRuntimeState(probe, savePath, error) || !loadRuntimeState(probe, savePath, error))
        return false;
    host = findMutableObjectById(probe, hostId);
    if (host == nullptr || host->owner != "Neutral"
        || probe.pendingOccupyTargets.size() != static_cast<std::size_t>(capacity))
        return fail("pending neutral group failed save/load");

    // Large structures have entrances along the full foundation, including
    // cells well beyond the old 1.5-cell radius from their northwest anchor.
    const auto footprint = runtimeStructureFootprintCellsForObject(probe, *host);
    const RuntimeCellPoint anchor = runtimeCellPointForObject(*host);
    const auto farEntrance = std::max_element(footprint.begin(), footprint.end(),
        [&](const auto& a, const auto& b) {
            return runtimeCellDistance(a, anchor) < runtimeCellDistance(b, anchor);
        });
    if (farEntrance == footprint.end() || runtimeCellDistance(*farEntrance, anchor) <= 1.5)
        return fail("stock foundation does not exercise the far entrance");
    auto* entrant = findMutableObjectById(probe, eligibleIds.front());
    setRuntimeObjectCell(probe, *entrant, *farEntrance);
    clearRuntimeRenderPosition(*entrant);
    if (!completeRuntimeOccupyEntry(probe, entrant->instanceId))
        return fail("first neutral entry failed: " + probe.simulationStatus);
    host = findMutableObjectById(probe, hostId);
    if (host == nullptr || host->owner != probe.economy.owner
        || host->garrisonOccupantIds.size() != 1
        || probe.pendingOccupyTargets.size() != static_cast<std::size_t>(capacity - 1))
        return fail("neutral claim lost the remaining friendly entry orders");
    // Exercise actual movement after the first claim, including vector swaps
    // in the normal simulation completion loop and subcell reservation release.
    for (int step = 0; step < 1800; ++step) {
        ++probe.tick;
        advanceRuntimeSimulation(probe);
        host = findMutableObjectById(probe, hostId);
        if (host != nullptr && host->garrisonOccupantIds.size() == static_cast<std::size_t>(capacity)) break;
    }
    if (host == nullptr || host->garrisonOccupantIds.size() != static_cast<std::size_t>(capacity)
        || !probe.pendingOccupyTargets.empty()) {
        std::ostringstream diagnostic;
        diagnostic << "group failed to arrive occupants=" << (host ? host->garrisonOccupantIds.size() : 0)
            << " pending=" << probe.pendingOccupyTargets.size() << " status=" << probe.simulationStatus;
        for (const auto& [id, targetId] : probe.pendingOccupyTargets) {
            const auto* unit = findObjectById(probe, id);
            if (unit) diagnostic << " unit=" << id << " at=" << unit->x << ',' << unit->y << " mission=" << unit->mission;
        }
        return fail(diagnostic.str());
    }
    std::set<std::string> firedTypes;
    for (int index = 0; index < capacity; ++index) {
        const auto fire = probe.garrisons.takeFirePlan(probe.rules, probe.world.accessObjects(),
            hostId, -1, probe.tick + index * 1000);
        if (!fire) return fail("occupied civilian building has no firing plan");
        const auto* gunner = findRuntimeObjectByIdIncludingInactive(probe, fire->occupantObjectId);
        const auto* rule = findRuntimeUnitType(probe.rules, *gunner);
        if (fire->weaponId != rule->occupyWeapon) return fail("garrison did not use its authored OccupyWeapon");
        firedTypes.insert(gunner->type);
    }
    if (firedTypes.size() != types.size()) return fail("mixed occupants did not fire round robin");
    if (!saveRuntimeState(probe, savePath, error) || !loadRuntimeState(probe, savePath, error))
        return false;
    host = findMutableObjectById(probe, hostId);
    if (host == nullptr || host->owner != probe.economy.owner
        || host->garrisonOccupantIds.size() != static_cast<std::size_t>(capacity))
        return fail("occupied house owner/manifest failed save/load");

    const auto exits = evacuateRuntimeGarrisonHost(probe, hostId,
        RuntimeGarrisonEvacuationReason::Manual, false);
    if (exits.size() != static_cast<std::size_t>(capacity)
        || std::any_of(exits.begin(), exits.end(), [](const auto& exit) {
            return exit.disposition != RuntimeGarrisonEvacuationDisposition::Exited;
        })) return fail("manual evacuation lost occupants");
    for (int index = 0; index < capacity; ++index) {
        const auto* unit = findObjectById(probe, eligibleIds[index]);
        if (unit == nullptr || !unit->alive || unit->garrisonHostObjectId >= 0 || unit->subCell < 0)
            return fail("evacuated infantry was not restored outside");
    }

    // A second neutral building exercises the claim from ordinary movement,
    // and an opposing player's still-pending order must be cancelled.
    auto* secondHost = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, "CAPRS03", {12, 20});
    if (secondHost == nullptr) return fail("second civilian host missing");
    const int secondHostId = secondHost->instanceId;
    secondHost->owner = "Neutral";
    secondHost->mission = "Guard";
    secondHost->buildAnimTicksTotal = secondHost->buildAnimTicksRemaining = 0;
    clearRuntimeRenderPosition(*secondHost);
    // Keep this ownership-cancellation actor away from the armed GHOST/GGI
    // fixtures at (7,2..5). Otherwise guard fire kills it before the local
    // walker can claim the second house, testing combat instead of cancellation.
    auto* opponent = addRuntimeObjectInstance(probe, RuntimeBuildKind::Infantry, "E2", {2, 23});
    if (opponent == nullptr) return fail("opposing occupier missing");
    const int opponentId = opponent->instanceId;
    opponent->owner = "Russians";
    opponent->mission = "Guard";
    clearRuntimeRenderPosition(*opponent);
    auto* walker = findMutableObjectById(probe, eligibleIds.back());
    setRuntimeObjectCell(probe, *walker, {11, 19});
    clearRuntimeRenderPosition(*walker);
    initializeRuntimeNavigation(probe);
    secondHost = findMutableObjectById(probe, secondHostId);
    // The selection below stands in for the other player's authorized command.
    probe.selectedObjectIds = {opponentId};
    if (!queueRuntimeOccupyTarget(probe, *secondHost)) return fail("opposing entry did not queue");
    applySelection(probe, {eligibleIds.back()}, false, false);
    if (!queueRuntimeTargetAbility(probe, *secondHost)
        || probe.pendingOccupyTargets.size() != 2)
        return fail("normal target command did not queue civilian entry");

    const auto pendingBefore = probe.pendingOccupyTargets;
    const auto selectedBefore = probe.selectedObjectIds;
    const RuntimeOwnerTransitionGarrisonPreparation rejected{ineligibleIds.front(), secondHostId};
    const auto rejection = applyRuntimeOwnerTransitionBatch(probe,
        {{secondHostId, "Neutral", probe.economy.owner}},
        RuntimeOwnerTransitionRepairPolicy::ReleaseRelations, "neutralGarrisonRejectionProbe",
        nullptr, nullptr, false, &rejected);
    secondHost = findMutableObjectById(probe, secondHostId);
    if (rejection.applied || secondHost == nullptr || secondHost->owner != "Neutral"
        || !secondHost->garrisonOccupantIds.empty() || probe.pendingOccupyTargets != pendingBefore
        || probe.selectedObjectIds != selectedBefore)
        return fail("rejected first entry leaked ownership or cancelled live orders");
    for (int step = 0; step < 240; ++step) {
        ++probe.tick;
        advanceRuntimeSimulation(probe);
        secondHost = findMutableObjectById(probe, secondHostId);
        if (secondHost != nullptr && !secondHost->garrisonOccupantIds.empty()) break;
    }
    opponent = findMutableObjectById(probe, opponentId);
    if (secondHost == nullptr || secondHost->owner != probe.economy.owner
        || secondHost->garrisonOccupantIds != std::vector<int>{eligibleIds.back()}
        || probe.pendingOccupyTargets.contains(opponentId)
        || opponent == nullptr || opponent->owner != "Russians" || opponent->garrisonHostObjectId >= 0)
        return fail("normal movement claim or opposing entry cancellation failed: " + probe.simulationStatus);

    // ConditionRed is inclusive and uses the same rule as entry and cursor.
    const int secondRedHealth = static_cast<int>(runtimeMaxHealthForObject(probe, *secondHost)
        * probe.rules.getSpecialMechanismRules().conditionRed);
    secondHost->health = secondRedHealth + 1;
    advanceRuntimeGarrisonState(probe);
    if (secondHost->garrisonOccupantIds.empty()) return fail("garrison evacuated above red health");
    secondHost->health = secondRedHealth;
    ++probe.tick;
    advanceRuntimeSimulation(probe);
    secondHost = findMutableObjectById(probe, secondHostId);
    walker = findMutableObjectById(probe, eligibleIds.back());
    if (secondHost == nullptr || !secondHost->garrisonOccupantIds.empty()
        || walker == nullptr || !walker->alive || walker->health <= 0
        || walker->garrisonHostObjectId >= 0 || walker->subCell < 0
        || runtimeStructureCellInsideFoundation(probe, *secondHost, runtimeCellPointForObject(*walker)))
        return fail("red health did not restore living infantry outside the foundation");
    applySelection(probe, {walker->instanceId}, false, false);
    if (runtimeObjectCanOccupyTarget(probe, *walker, *secondHost))
        return fail("evacuated infantry re-entered a red house");
    secondHost->health = runtimeMaxHealthForObject(probe, *secondHost);
    if (!probe.garrisons.tryEnter(probe.rules, probe.world.accessObjects(),
            walker->instanceId, secondHostId).entered())
        return fail("far-side evacuation fixture did not enter");
    // Leave only one exit on the east side of the real 6x4 foundation.
    // The former radius-four search could not find it and killed the soldier.
    const RuntimeCellPoint farExit{18, 20};
    std::erase_if(probe.world.accessTerrainCells(), [&](const TerrainCell& cell) {
        return !runtimeStructureCellInsideFoundation(probe, *secondHost, {cell.x, cell.y})
            && (cell.x != farExit.x || cell.y != farExit.y);
    });
    initializeRuntimeNavigation(probe);
    secondHost = findMutableObjectById(probe, secondHostId);
    secondHost->health = secondRedHealth;
    advanceRuntimeGarrisonState(probe);
    walker = findMutableObjectById(probe, eligibleIds.back());
    if (walker == nullptr || !walker->alive || walker->garrisonHostObjectId >= 0
        || walker->x != farExit.x || walker->y != farExit.y)
        return fail("large building failed to evacuate through its far-side exit");
    std::cout << "Neutral garrison: E1/E2/INIT=ok cursor=ok eligibility=ok aliases=ok"
        << " group=10 capacity=ok foundation=ok owner=ok pendingSave=ok occupiedSave=ok weapons=ok"
        << " evacuation=ok movementClaim=ok competingOwner=ok rejectionAtomic=ok"
        << " infantryAttackThreshold=ok tankDamage=ok forceAttack=ok redEvacuation=ok farExit=ok\n";
    return true;
}
