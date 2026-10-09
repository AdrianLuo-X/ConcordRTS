bool runRuntimeTankBunkerSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "tank bunker: " + message; return false;
    };
    const auto* stock = live.rules.findBuildingType("NATBNK");
    const auto& rules = live.rules.getSpecialMechanismRules();
    if (stock == nullptr || !stock->bunker || stock->numberOfDocks != 1
        || std::abs(rules.bunkerDamageMultiplier - 1.3) > 0.0001
        || std::abs(rules.bunkerRofMultiplier - 1.3) > 0.0001
        || rules.bunkerWeaponRangeBonus != 2.0) return fail("stock rules missing");
    for (const auto* id : {"NATBNK_A", "NATBNK_B", "NATBNK_AD", "NATBNK_BD",
            "NATBNK_A2", "NATBNK_B2", "NATBNK_A2D", "NATBNK_B2D"}) {
        if (live.art.findArtType(id) == nullptr) return fail(std::string("original art missing: ") + id);
    }
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config; probe.report = live.report;
    probe.rules = live.rules; probe.rulesLoaded = true;
    probe.art = live.art; probe.artLoaded = true;
    probe.theater = live.theater; probe.theaterLoaded = live.theaterLoaded;
    probe.world = live.world;
    probe.world.accessObjects().clear(); probe.world.accessTerrainCells().clear();
    probe.world.accessTerrainObjects().clear(); probe.world.accessSmudges().clear();
    probe.economy.owner = "YuriCountry";
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.triggers.bootstrapComplete = true;
    probe.economy.skirmishStartApplied = true;
    for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 28; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = 0;
        probe.world.accessTerrainCells().push_back(cell);
    }
    WorldObjectInstance host;
    host.instanceId = 1; host.kind = WorldObjectKind::Structure; host.type = "NATBNK";
    host.owner = probe.economy.owner; host.health = stock->strength;
    host.alive = true; host.x = host.y = 12; host.cell = runtimeCellKey(12, 12); host.mission = "Guard";
    auto tank = host;
    tank.instanceId = 2; tank.kind = WorldObjectKind::Unit; tank.type = "MTNK";
    tank.health = probe.rules.findUnitType("UnitType", "MTNK")->strength;
    tank.x = 5; tank.y = 12; tank.cell = runtimeCellKey(tank.x, tank.y);
    auto second = tank; second.instanceId = 3; second.y = 15; second.cell = runtimeCellKey(second.x, second.y);
    probe.world.accessObjects() = {host, tank, second};
    initializeRuntimeNavigation(probe);
    int allowedTypes = 0;
    for (const auto& type : live.rules.getVehicleTypes()) {
        auto* unit = findMutableObjectById(probe, 2);
        unit->type = type.id;
        const bool expected = type.bunkerable && !type.naval;
        if (runtimeObjectCanEnterTransport(probe, *unit, *findObjectById(probe, 1), true) != expected) {
            return fail("wrong eligibility for " + type.id);
        }
        allowedTypes += expected ? 1 : 0;
    }
    *findMutableObjectById(probe, 2) = tank;
    auto* unit = findMutableObjectById(probe, 2);
    unit->owner = "Americans";
    if (runtimeObjectCanEnterTransport(probe, *unit, *findObjectById(probe, 1), true)) return fail("enemy admitted");
    *unit = tank;
    findMutableObjectById(probe, 1)->buildAnimTicksRemaining = 5;
    if (runtimeObjectCanEnterTransport(probe, *unit, *findObjectById(probe, 1), true)) return fail("unfinished bunker admitted a tank");
    findMutableObjectById(probe, 1)->buildAnimTicksRemaining = 0;

    const char* output = std::getenv("CONCORD_TANK_BUNKER_OUTPUT");
    if (output != nullptr) std::filesystem::create_directories(output);
    const auto savePath = std::filesystem::temp_directory_path()
        / ("concord_tank_bunker_" + std::to_string(GetCurrentProcessId()) + ".ini");
    const auto render = [&](const char* name, int expectedFrame, bool occupied) {
        const auto result = drawRuntimeStateOffscreenAtCell(probe, {12, 12}, 640, 420,
            error, output == nullptr ? "" : std::string(output) + "/" + name + ".bmp", false);
        if (!result) return false;
        error = std::string(name) + " missingBase=" + std::to_string(result->missingStructureSprites)
            + " missingAnim=" + std::to_string(result->buildingArtAnimationMissing)
            + " layers=" + std::to_string(result->buildingSpecialAnimationLayers)
            + " frame=" + std::to_string(result->firstBuildingArtAnimationFrame)
            + " front=" + std::to_string(result->tankBunkerFrontLayers)
            + " frontFrame=" + std::to_string(result->tankBunkerFrontFrame);
        if (result->missingStructureSprites != 0 || result->buildingArtAnimationMissing != 0) return false;
        if (expectedFrame < 0) return result->buildingSpecialAnimationLayers == 0;
        return result->buildingSpecialAnimationLayers == 2
            && result->firstBuildingArtAnimationFrame == expectedFrame
            && (!occupied || (result->visibleVoxelBodies >= 1 && result->tankBunkerFrontLayers == 1
                && result->tankBunkerFrontFrame == expectedFrame));
    };
    if (!render("00-empty", -1, false)) return fail("empty original building render: " + error);
    probe.selectedObjectIds = {2, 3};
    if (runtimeTargetCursorIntent(probe, *findObjectById(probe, 1)) != RuntimeTargetCursorIntent::Enter
        || !queueRuntimeTargetAbility(probe, *findObjectById(probe, 1))
        || probe.pendingTransportTargets.size() != 1 || !probe.pendingTransportTargets.contains(2)) {
        return fail("cursor / single-place reservation");
    }
    if (completeRuntimeTransportEntry(probe, 2)) return fail("tank entered from a distance");
    for (int i = 0; i < 800 && !probe.pendingTransportTargets.empty(); ++i) {
        ++probe.tick; advanceRuntimeSimulation(probe);
    }
    unit = findMutableObjectById(probe, 2);
    auto* bunker = findMutableObjectById(probe, 1);
    if (!unit || !unit->alive || unit->tankBunkerHostId != 1 || bunker->tankBunkerOccupantId != 2
        || runtimeObjectCanMove(probe, *unit)) return fail("real arrival / armed immobilized tank");
    const auto facesBunkerOpening = [](const WorldObjectInstance& object) {
        // Runtime compass: 0 = up, 64 = right, 128 = down, 160 = lower-left.
        return object.facing == 160
            && (object.turretFacing < 0 || object.turretFacing == 160);
    };
    if (facesBunkerOpening(*unit) || !runtimeTankBunkerWaitingForFacing(*bunker))
        return fail("arrival snapped to the opening or raised walls before parking");
    if (!render("01-parking-arrival", -1, true)) return fail("walls rose during parking: " + error);
    const int arrivalFacing = unit->facing;
    // Rendering/repeated sampling at a paused tick must not advance rotation.
    advanceRuntimeTankBunkers(probe);
    const auto pausedBodyClock = unit->bodyFacingClock;
    const auto pausedTurretClock = unit->turretFacingClock;
    for (int i = 0; i < 3; ++i) advanceRuntimeTankBunkers(probe);
    if (unit->facing != arrivalFacing || unit->bodyFacingClock != pausedBodyClock
        || unit->turretFacingClock != pausedTurretClock || !runtimeTankBunkerWaitingForFacing(*bunker))
        return fail("parking advanced while the simulation tick was paused");
    int parkingTurns = 0;
    bool savedDuringParking = false;
    for (int i = 0; i < probe.config.fixedTickHz * 20
            && runtimeTankBunkerWaitingForFacing(*findObjectById(probe, 1)); ++i) {
        unit = findMutableObjectById(probe, 2);
        const int previousFacing = unit->facing;
        auto expectedTank = *unit;
        ++probe.tick;
        const int expectedFacing = runtimeAdvanceBodyFacing(probe, expectedTank,
            expectedTank.facing, 160);
        advanceRuntimeSimulation(probe);
        unit = findMutableObjectById(probe, 2); bunker = findMutableObjectById(probe, 1);
        if (unit->facing != expectedFacing) return fail("parking did not use normal vehicle ROT");
        parkingTurns += unit->facing != previousFacing ? 1 : 0;
        if (runtimeTankBunkerWaitingForFacing(*bunker)) {
            if (runtimeTankBunkerLayerVisible(*bunker, "SpecialAnim")
                || runtimeTankBunkerLayerVisible(*bunker, "SpecialAnimTwo"))
                return fail("walls became visible before turning finished");
            if (parkingTurns >= 2 && !savedDuringParking) {
                if (!render("01-parking-middle", -1, true)) return fail("parking middle render: " + error);
                const int savedFacing = unit->facing, savedTurret = unit->turretFacing;
                const auto bodyClock = unit->bodyFacingClock, turretClock = unit->turretFacingClock;
                if (!saveRuntimeState(probe, pathString(savePath), error)
                    || !loadRuntimeState(probe, pathString(savePath), error))
                    return fail("parking save/load: " + error);
                unit = findMutableObjectById(probe, 2); bunker = findMutableObjectById(probe, 1);
                if (!runtimeTankBunkerWaitingForFacing(*bunker) || unit->facing != savedFacing
                    || unit->turretFacing != savedTurret || unit->bodyFacingClock != bodyClock
                    || unit->turretFacingClock != turretClock)
                    return fail("save/load lost the partially turned parking state");
                savedDuringParking = true;
            }
        } else if (!facesBunkerOpening(*unit) || bunker->tankBunkerAnimationStartTick != probe.tick) {
            return fail("rise did not start exactly when parking finished");
        }
    }
    if (parkingTurns < 3 || !savedDuringParking || !facesBunkerOpening(*unit)
        || runtimeTankBunkerWaitingForFacing(*bunker)) return fail("parking did not visibly complete across ticks");
    // Original NATBNK's floor center is (0, 30) from its SHP anchor at 60x30.
    // Verify the real occupied pose against the art, rather than only counting
    // front-wall draws (which also passed when the tank sat above the walls).
    RuntimeMapCamera floorCamera;
    floorCamera.tileWidth = 60; floorCamera.tileHeight = 30;
    const auto floorPoint = runtimeProjectCellXY(unit->renderX, unit->renderY,
        unit->renderLevel, floorCamera);
    const auto bunkerPoint = runtimeProjectCellXY(bunker->x, bunker->y,
        bunker->level, floorCamera);
    if (floorPoint.x - bunkerPoint.x != 0 || floorPoint.y - bunkerPoint.y != 30)
        return fail("tank hull is not centered inside the authored bunker floor");
    probe.selectedObjectIds = {3};
    if (runtimeTargetCursorIntent(probe, *bunker) != RuntimeTargetCursorIntent::NoEnter) return fail("occupied cursor");
    const int start = bunker->tankBunkerAnimationStartTick;
    if (!render("01-rising-first", 0, true)) return fail("first original rise frame: " + error);
    probe.tick = start + runtimeTankBunkerFrameDelay(*probe.art.findArtType("NATBNK_A"), probe.config.fixedTickHz) * 5;
    if (!render("02-rising-middle", 5, true)) return fail("middle original rise frame: " + error);
    probe.tick = start + 90;
    for (int i = 0; i < 30; ++i) { ++probe.tick; advanceRuntimeSimulation(probe); }
    unit = findMutableObjectById(probe, 2); bunker = findMutableObjectById(probe, 1);
    if (!facesBunkerOpening(*unit)) return fail("idle tank lost its lower-left parking direction");
    if (!render("03-occupied", 9, true)) return fail("held original walls: " + error);
    if (output != nullptr) {
        const auto originalTank = *unit;
        for (const char* type : {"LTNK", "MTNK", "HTNK"}) {
            for (const int facing : {0, 64, 128, 160, 192}) {
                unit->type = type;
                unit->health = probe.rules.findUnitType("UnitType", type)->strength;
                unit->facing = unit->turretFacing = facing;
                const std::string name = std::string("occupied-") + type + "-" + std::to_string(facing);
                if (!render(name.c_str(), 9, true)) return fail("occupied hull/wall facing: " + error);
            }
        }
        *unit = originalTank;
    }
    bunker->health = stock->strength / 2 - 1;
    if (!render("04-damaged-occupied", 19, true)) return fail("damaged original wall bank: " + error);
    bunker->health = stock->strength;
    auto enemy = tank; enemy.instanceId = 4; enemy.type = "HTNK"; enemy.owner = "Americans";
    enemy.x = 19; enemy.y = 13; enemy.cell = runtimeCellKey(19, 13);
    enemy.health = probe.rules.findUnitType("UnitType", "HTNK")->strength;
    const int enemyHealth = enemy.health;
    probe.world.accessObjects().push_back(enemy);
    refreshRuntimeObjectOccupancyAfterLocalChange(probe);
    unit = findMutableObjectById(probe, 2); bunker = findMutableObjectById(probe, 1);
    const auto profile = runtimeWeaponProfileForObject(probe, *unit, &enemy);
    if (profile.weapon == nullptr) return fail("tank lost its own weapon");
    const int plainCooldown = runtimeWeaponCooldownTicks(probe, tank, *profile.weapon);
    const int bunkerCooldown = runtimeWeaponCooldownTicks(probe, *unit, *profile.weapon);
    if (bunkerCooldown >= plainCooldown || runtimeEffectiveWeaponRange(probe, *unit, enemy, *profile.weapon)
            != profile.weapon->range + 2.0) return fail("range / divided ROF bonus");
    probe.selectedObjectIds = {1}; issueRuntimeAttackCommand(probe, *findObjectById(probe, 4));
    if (probe.lastCommand.acceptedCount != 1) return fail("manual attack beyond base weapon range rejected");
    const auto heldCell = runtimeCellPointForObject(*unit);
    for (int i = 0; i < 220 && findObjectById(probe, 4)->health == enemyHealth; ++i) {
        ++probe.tick; advanceRuntimeSimulation(probe);
    }
    unit = findMutableObjectById(probe, 2);
    if (findObjectById(probe, 4)->health >= enemyHealth || runtimeCellPointForObject(*unit) != heldCell) {
        return fail("garrisoned tank did not actually fire in its extended range");
    }
    const auto* aimedTarget = findObjectById(probe, 4);
    if (unit->facing != 160 || unit->turretFacing != runtimeFacingFromDelta(
            aimedTarget->x - unit->x, aimedTarget->y - unit->y)) {
        return fail("parked hull turned or turret could not aim away from the opening");
    }
    if (!render("occupied-aiming", 9, true)) return fail("aimed turret / parked hull: " + error);
    const int damage = enemyHealth - findObjectById(probe, 4)->health;
    const int plainDamage = runtimeWeaponDamageAgainst(probe, enemy, *profile.weapon, 0.0, 1.0);
    if (damage != static_cast<int>(std::lround(plainDamage * 1.3))) return fail("shot damage not multiplied once: " + std::to_string(damage));
    eraseRuntimeBehavior(probe, 2); eraseRuntimeBehavior(probe, 4);
    probe.projectiles.clear();
    probe.selectedObjectIds = {1};
    issueRuntimeStopCommand(probe);
    if (probe.lastCommand.acceptedCount != 1 || probe.behaviors.contains(2)
        || findObjectById(probe, 2)->tankBunkerHostId != 1
        || !findObjectById(probe, 2)->hasRenderPosition) return fail("selected bunker stop control");
    issueRuntimeGuardCommand(probe);
    if (probe.lastCommand.acceptedCount != 1 || findObjectById(probe, 2)->tankBunkerHostId != 1)
        return fail("selected bunker guard control");
    const int savedBodyFacing = unit->facing, savedTurretFacing = unit->turretFacing;
    if (!saveRuntimeState(probe, pathString(savePath), error)
        || !loadRuntimeState(probe, pathString(savePath), error)) return fail("save/load: " + error);
    bunker = findMutableObjectById(probe, 1); unit = findMutableObjectById(probe, 2);
    if (!unit || unit->tankBunkerHostId != 1 || bunker->tankBunkerOccupantId != 2
        || bunker->tankBunkerAnimationStartTick != start) return fail("save lost bunker relation/timestamp");
    if (unit->facing != savedBodyFacing || unit->turretFacing != savedTurretFacing)
        return fail("save/load changed the parked body or aimed turret");
    // Attack the vehicle directly: ordinary shells damage only its cover.
    auto* hostile = findMutableObjectById(probe, 4);
    const auto hostileWeapon = runtimeWeaponProfileForObject(probe, *hostile, unit);
    const int tankHealth = unit->health, bunkerHealth = bunker->health;
    spawnRuntimeProjectile(probe, *hostile, *unit, *hostileWeapon.weapon, hostileWeapon, 1);
    for (int i = 0; i < 180 && findObjectById(probe, 1)->health == bunkerHealth; ++i) {
        ++probe.tick; advanceRuntimeEffects(probe);
    }
    if (findObjectById(probe, 2)->health != tankHealth || findObjectById(probe, 1)->health >= bunkerHealth) {
        return fail("ordinary weapon did not hit the bunker instead of the vehicle");
    }
    probe.projectiles.clear();
    // DiskWH is an original penetrating warhead: it must hit the live tank.
    hostile = findMutableObjectById(probe, 4);
    hostile->type = "DISK";
    unit = findMutableObjectById(probe, 2);
    const auto penetrating = runtimeWeaponProfileForObject(probe, *hostile, unit);
    if (penetrating.weapon == nullptr || penetrating.warhead == nullptr
        || !penetrating.warhead->penetratesBunker) return fail("stock DiskWH penetration missing");
    const int coveredTankHealth = unit->health;
    spawnRuntimeProjectile(probe, *hostile, *unit, *penetrating.weapon, penetrating, 1);
    for (int i = 0; i < 180 && findObjectById(probe, 2)->health == coveredTankHealth; ++i) {
        ++probe.tick; advanceRuntimeEffects(probe);
    }
    if (findObjectById(probe, 2)->health >= coveredTankHealth) return fail("penetrating warhead was blocked");
    findMutableObjectById(probe, 4)->type = "HTNK";
    probe.projectiles.clear();
    bunker = findMutableObjectById(probe, 1);
    bunker->health = stock->strength;
    if (!executeRuntimeSecondaryAction(probe, 1) || bunker->tankBunkerOccupantId >= 0)
        return fail("secondary unload could not find a real exit");
    if (!render("05-lowering-first", 9, false)) return fail("original reverse frame: " + error);
    probe.tick += runtimeTankBunkerFrameDelay(*probe.art.findArtType("NATBNK_A2"), probe.config.fixedTickHz) * 5;
    if (!render("06-lowering-middle", 4, false)) return fail("reverse progression: " + error);
    for (int i = 0; i < 80; ++i) { ++probe.tick; advanceRuntimeSimulation(probe); }
    unit = findMutableObjectById(probe, 2);
    if (unit->tankBunkerHostId >= 0 || !runtimeObjectCanMove(probe, *unit)
        || runtimeVeterancyAbilityMultiplier(probe, *unit, VeteranAbility::Firepower) != 1.0
        || !render("07-empty-again", -1, false)) return fail("unload did not restore ordinary tank/building");
    // The AI script must use the same path and retain the same live vehicle.
    findMutableObjectById(probe, 4)->alive = false;
    findMutableRuntimeObjectByIdIncludingInactive(probe, 4)->health = 0;
    eraseRuntimeBehavior(probe, 2); eraseRuntimeBehavior(probe, 4);
    RuntimeTeamMissionState team; team.objectIds = {2};
    const RuntimeScriptStep step{61, 0};
    if (issueRuntimeTeamScriptStep(probe, team, step) != RuntimeTeamStepDisposition::Waiting) return fail("AI could not enter");
    for (int i = 0; i < 600 && !probe.pendingTransportTargets.empty(); ++i) { ++probe.tick; advanceRuntimeSimulation(probe); }
    unit = findMutableObjectById(probe, 2); bunker = findMutableObjectById(probe, 1);
    if (unit == nullptr || unit->tankBunkerHostId != 1
        || runtimeTeamScriptStepCompletion(probe, team, step) != RuntimeTeamStepCompletion::Complete) return fail("AI arrival failed");
    for (int i = 0; i < probe.config.fixedTickHz * 20
            && runtimeTankBunkerWaitingForFacing(*findObjectById(probe, 1)); ++i) {
        ++probe.tick; advanceRuntimeSimulation(probe);
    }
    if (!facesBunkerOpening(*unit)) return fail("AI re-entry did not face lower-left");
    bunker->health = stock->strength / 2 - 1;
    probe.selectedObjectIds = {2};
    issueRuntimeMoveCommand(probe, {8, 13});
    if (bunker->tankBunkerOccupantId >= 0 || !render("08-damaged-lowering", 19, false))
        return fail("move exit / damaged reverse artwork");
    for (int i = 0; i < probe.config.fixedTickHz * 30
        && runtimeCellPointForObject(*findObjectById(probe, 2)) != RuntimeCellPoint{8, 13}; ++i) {
        ++probe.tick; advanceRuntimeSimulation(probe);
    }
    if (runtimeCellPointForObject(*findObjectById(probe, 2)) != RuntimeCellPoint{8, 13})
        return fail("manual move could not leave the foundation");
    if (issueRuntimeTeamScriptStep(probe, team, step) != RuntimeTeamStepDisposition::Waiting) return fail("AI refill failed");
    for (int i = 0; i < 600 && !probe.pendingTransportTargets.empty(); ++i) { ++probe.tick; advanceRuntimeSimulation(probe); }
    for (int i = 0; i < probe.config.fixedTickHz * 20
            && runtimeTankBunkerWaitingForFacing(*findObjectById(probe, 1)); ++i) {
        ++probe.tick; advanceRuntimeSimulation(probe);
    }
    unit = findMutableObjectById(probe, 2);
    if (!unit || unit->tankBunkerHostId != 1 || !facesBunkerOpening(*unit))
        return fail("entry after moving away did not restore the lower-left direction");
    RuntimeObjectExitRequest death;
    death.objectId = 1; death.kind = RuntimeObjectExitKind::Destroyed; death.tick = probe.tick;
    death.mission = "Destroyed"; death.reason = "tank bunker smoke";
    queueRuntimeObjectExit(probe, std::move(death)); drainRuntimeObjectExitQueue(probe);
    unit = findMutableObjectById(probe, 2);
    if (unit == nullptr || !unit->alive || unit->tankBunkerHostId >= 0) return fail("bunker destruction lost the living tank");
    // Cancel and destruction before raising must leave an empty, lowered
    // bunker; a never-started rise must not play its reverse animation/sound.
    const auto prepareTurn = [&](int tickHz) {
        probe.config.fixedTickHz = tickHz;
        probe.tick = 0;
        probe.behaviors.clear(); probe.pendingTransportTargets.clear();
        auto* turnHost = findMutableRuntimeObjectByIdIncludingInactive(probe, 1);
        *turnHost = host;
        auto* turnTank = findMutableObjectById(probe, 2);
        *turnTank = tank;
        const auto cells = runtimeStructureFootprintCellsForObject(probe, *turnHost);
        turnTank->x = cells.back().x; turnTank->y = cells.back().y;
        turnTank->cell = runtimeCellKey(turnTank->x, turnTank->y);
        turnTank->facing = turnTank->turretFacing = 32;
        refreshRuntimeObjectOccupancyAfterLocalChange(probe);
        const int sounds = probe.runtimeSoundEvents;
        return completeRuntimeTankBunkerEntry(probe, 2, 1)
            && turnTank->facing == 32 && turnTank->turretFacing == 32
            && probe.runtimeSoundEvents == sounds;
    };
    std::vector<int> durationAtTickRates;
    for (int tickHz : {15, 30, 60}) {
        if (!prepareTurn(tickHz)) return fail("parking setup snapped facing or played rise sound");
        const int sounds = probe.runtimeSoundEvents;
        while (probe.tick < tickHz * 20
                && runtimeTankBunkerWaitingForFacing(*findObjectById(probe, 1))) {
            advanceRuntimeTankBunkers(probe);
            if (runtimeTankBunkerWaitingForFacing(*findObjectById(probe, 1))) ++probe.tick;
        }
        if (!facesBunkerOpening(*findObjectById(probe, 2)) || probe.runtimeSoundEvents != sounds + 1)
            return fail("timed parking failed or rise sound did not play once");
        durationAtTickRates.push_back(probe.tick);
        advanceRuntimeTankBunkers(probe);
        if (probe.runtimeSoundEvents != sounds + 1) return fail("rise sound repeated");
    }
    if (durationAtTickRates[1] != durationAtTickRates[0] * 2
        || durationAtTickRates[2] != durationAtTickRates[0] * 4)
        return fail("parking rotation duration changed with fixed tick rate");
    if (!prepareTurn(30)) return fail("cancel parking setup");
    const int cancelSounds = probe.runtimeSoundEvents;
    if (!unloadRuntimeTankBunker(probe, *findMutableObjectById(probe, 1))) return fail("cancel parking had no exit");
    bunker = findMutableObjectById(probe, 1); unit = findMutableObjectById(probe, 2);
    if (bunker->tankBunkerOccupantId >= 0 || bunker->tankBunkerLowering
        || bunker->tankBunkerAnimationStartTick >= 0 || unit->tankBunkerHostId >= 0
        || probe.runtimeSoundEvents != cancelSounds) return fail("cancel parking raised/lowered untouched walls");
    if (!prepareTurn(30)) return fail("parking destruction setup");
    findMutableObjectById(probe, 1)->health = 0;
    advanceRuntimeTankBunkers(probe);
    unit = findMutableObjectById(probe, 2);
    bunker = findMutableRuntimeObjectByIdIncludingInactive(probe, 1);
    if (!unit->alive || unit->tankBunkerHostId >= 0 || bunker->tankBunkerLowering)
        return fail("destruction during parking kept a relation or lowered untouched walls");
    std::cout << "Tank bunker smoke passed: " << allowedTypes
        << " eligible stock vehicles, real entry, gradual parking before rise, paused/15/30/60-Hz rotation, "
        << "mid-turn save/load and cancel/destruction, lower-left parking and free turret aim, "
        << "original rise/hold/reverse/damaged layers, "
        << "extended-range shot damage=" << damage << ", ROF " << plainCooldown << "->" << bunkerCooldown
        << ", protection/penetration, save/load, AI, secondary/move exit and destruction.\n";
    return true;
}
