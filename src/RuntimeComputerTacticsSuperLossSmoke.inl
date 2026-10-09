for (bool chrono : {false, true}) for (const std::string loss : {"capture", "power", "destroy"}) {
    const std::string trigger = chrono ? "0C32599C-G" : "0D2769BC-G";
    const std::string weapon = chrono ? "ChronoSphereSpecial" : "IronCurtainSpecial";
    const std::string sourceType = chrono ? "GACSPH" : "NAIRON";
    auto storage = makeWorld(chrono ? "Americans" : "Russians", "Easy", {trigger}); auto& s = *storage;
    RuntimeSnapshotActiveStateScope scope(s);
    s.config.superweapons = true;
    auto* observer = addRuntimeObjectInstanceForOwner(s, human, RuntimeBuildKind::Unit, "SPYP", {49, 1});
    if (!observer) return fail("superweapon interruption observer unavailable");
    observer->health = 1000000; observer->mission = "Sleep"; observer->airborne = true;
    // Keep the script's destination available during the original recharge.
    // Crews escaping the destroyed power plants still fight normally.
    for (auto& enemy : s.world.accessObjects()) if (enemy.alive && enemy.owner == human) {
        if (enemy.kind == WorldObjectKind::Structure) enemy.health = 1000000;
        enemy.weaponCooldownTicksRemaining = 1000000;
    }
    auto* power = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building,
        loss == "power" ? "GAPOWR" : "NANRCT", {15, 21});
    if (!power) return fail("superweapon loss power source unavailable");
    if (loss == "power") for (int i = 0; i < 8; ++i)
        if (!addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, "GAPOWR", {2 + 4 * i, 35}))
            return fail("ordinary power-loss sources unavailable");
    std::vector<int> powerIds;
    for (const auto& object : s.world.getObjects()) if (object.alive && object.owner == owner) {
        const auto* building = findRuntimeBuildingTypeById(s.rules, object.type);
        if (building && building->power > 0) powerIds.push_back(object.instanceId);
    }
    auto* source = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, sourceType, {21, 22});
    if (!source) return fail("superweapon loss source unavailable");
    int sourceId = source->instanceId;
    initializeRuntimeNavigation(s);
    int interruptedTick = -1, restoredTick = -1, blockedChecks = 0;
    const int chargeTicks = static_cast<int>(s.rules.findSuperWeaponType(weapon)->rechargeTimeMinutes * 60 * s.config.fixedTickHz);
    bool effect = false, selected = false;
    std::vector<int> members;
    std::map<int, RuntimeCellPoint> positions;
    for (int frame = 0; frame < 40000 && !effect; ++frame) {
        // Damage clamps health to the authored maximum. Replenish the idle
        // observation targets throughout the wait, rather than only once.
        for (auto& enemy : s.world.accessObjects())
            if (enemy.alive && enemy.owner == human && enemy.kind == WorldObjectKind::Structure)
                enemy.health = 1000000;
        tick(s);
        if (s.skirmishMatchFinished) return fail("superweapon observation ended before recovery");
        const auto tactics = s.triggers.computerTacticsByOwner.find(ownerKey);
        if (tactics != s.triggers.computerTacticsByOwner.end() && !tactics->second.teams.empty()) {
            selected = true; tactics->second.nextTriggerTick = 10000000;
            for (const auto& [id, team] : tactics->second.teams) if (team.launched) members = team.objectIds;
        }
        if (selected && interruptedTick < 0) {
            interruptedTick = s.tick;
            if (loss == "destroy" || loss == "power") {
                for (int lostId : loss == "power" ? powerIds : std::vector<int>{sourceId}) {
                    RuntimeObjectExitRequest death; death.objectId = lostId; death.tick = s.tick;
                    if (!finalizeRuntimeObjectExit(s, death).accepted) return fail("superweapon/power destruction rejected");
                }
            } else {
                for (int lostId : loss == "power" ? powerIds : std::vector<int>{sourceId}) {
                    const auto result = applyRuntimeOwnerTransitionBatch(s, {{lostId, owner, human}},
                        RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits, "computer tactic source interruption");
                    if (!result.applied) return fail("superweapon source transition rejected: " + result.error);
                }
            }
        }
        if (interruptedTick >= 0 && restoredTick < 0 && s.tick > interruptedTick + 1) {
            const auto* snapshot = s.superWeapons.findSidebarSnapshot(owner, weapon);
            if (snapshot && snapshot->launchTick >= 0) return fail(weapon + " fired during " + loss);
            if (loss == "power") {
                const auto* currentSource = findObjectById(s, sourceId);
                if (currentSource && currentSource->poweredOperational)
                    return fail(weapon + " power-loss fixture did not lose power");
            }
            ++blockedChecks;
            if (s.tick - interruptedTick >= chargeTicks) {
                const auto* oldSource = findObjectById(s, sourceId);
                if (loss == "destroy" || (loss == "capture" && !oldSource)) {
                    const auto* rebuilt = addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, sourceType, {23, 25});
                    if (!rebuilt) return fail("superweapon rebuilding failed");
                    sourceId = rebuilt->instanceId;
                    refreshRuntimeObjectOccupancyAfterLocalChange(s);
                } else if (loss == "power") {
                    if (!addRuntimeObjectInstanceForOwner(s, owner, RuntimeBuildKind::Building, "NANRCT", {16, 28}))
                        return fail("power rebuilding failed");
                    refreshRuntimeObjectOccupancyAfterLocalChange(s);
                } else {
                    for (int restoredId : loss == "power" ? powerIds : std::vector<int>{sourceId}) {
                        const auto result = applyRuntimeOwnerTransitionBatch(s, {{restoredId, human, owner}},
                            RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits, "computer tactic source restored");
                        if (!result.applied) return fail("superweapon restoration rejected: " + result.error);
                    }
                }
                restoredTick = s.tick;
            }
        }
        const auto* snapshot = s.superWeapons.findSidebarSnapshot(owner, weapon);
        const bool firedAfterRecovery = restoredTick >= 0 && snapshot && snapshot->launchTick > restoredTick;
        for (int id : members) if (const auto* member = findObjectById(s, id)) {
            positions.try_emplace(id, runtimeCellPointForObject(*member));
            effect |= firedAfterRecovery && (chrono
                ? runtimeCellDistance(positions[id], runtimeCellPointForObject(*member)) > 15
                : s.superWeapons.isObjectProtected(id, s.tick));
        }
    }
    if (!effect || blockedChecks < chargeTicks - 2) {
        std::ostringstream saved; std::string saveError;
        if (writeRuntimeStateToStream(s, saved, saveError)) {
            std::ofstream out(std::filesystem::path("build/smoke/gameplay")
                / ("super-loss-" + weapon + "-" + loss + ".ini")); out << saved.str();
        }
        return fail(weapon + " did not hold and recover after " + loss + describe(s));
    }
    std::cout << "Computer superweapon interruption: " << weapon << ' ' << loss
        << " no unavailable launch=yes real source recovery=yes original team effect resumes=yes\n";
}
