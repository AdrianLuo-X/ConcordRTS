bool runRuntimeMcvRepackSmokeProbe(
    const RuntimeWindowState& live, std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "MCV repack: " + message;
        return false;
    };
    const auto makeProbe = [&](const std::string& owner, int size = 30) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.config.fixedTickHz = 30;
        probe.config.mcvRepacks = true;
        probe.config.shortGame = true;
        probe.config.computerAiEnabled = false;
        probe.rules = live.rules;
        probe.rulesLoaded = true;
        probe.art = live.art;
        probe.artLoaded = true;
        probe.economy.owner = owner;
        probe.economy.skirmishStartApplied = true;
        probe.triggers.bootstrapComplete = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 1; y <= size; ++y) {
            for (int x = 1; x <= size; ++x) {
                TerrainCell cell;
                cell.x = x;
                cell.y = y;
                cell.cellIndex512 = runtimeCellKey(x, y);
                cell.tileIndex = 0;
                cell.subTileIndex = 0;
                probe.world.accessTerrainCells().push_back(cell);
            }
        }
        initializeRuntimeNavigation(probe);
        return storage;
    };
    const auto selected = [](RuntimeWindowState& probe) {
        return probe.selectedObjectIds.size() == 1
            ? findMutableObjectById(probe, probe.selectedObjectIds.front())
            : nullptr;
    };
    const auto finishConstruction = [](WorldObjectInstance& object) {
        object.buildAnimTicksTotal = object.buildAnimTicksRemaining = 0;
        object.mission = "Guard";
    };
    const auto finishRepacking = [&](RuntimeWindowState& probe, const std::vector<int>& yardIds) {
        std::map<int, int> remainingById;
        int maxRemaining = 0;
        int liveBefore = 0;
        for (const auto& object : probe.world.getObjects()) liveBefore += object.alive;
        for (int yardId : yardIds) {
            const auto* yard = findObjectById(probe, yardId);
            if (yard == nullptr || yard->kind != WorldObjectKind::Structure
                || yard->mission != "Repacking" || yard->buildAnimTicksRemaining <= 0
                || yard->buildAnimTicksTotal < yard->buildAnimTicksRemaining)
                return fail("repack did not retain a live yard for its complete animation");
            remainingById.emplace(yardId, yard->buildAnimTicksRemaining);
            maxRemaining = std::max(maxRemaining, yard->buildAnimTicksRemaining);
        }
        for (int elapsed = 1; elapsed <= maxRemaining; ++elapsed) {
            ++probe.tick;
            (void)advanceRuntimeBuildAnimations(probe);
            (void)advanceRuntimeStructureRepacks(probe);
            int liveNow = 0;
            for (const auto& object : probe.world.getObjects()) liveNow += object.alive;
            if (liveNow != liveBefore) return fail("repack duplicated or lost a live object");
            for (const auto& [yardId, total] : remainingById) {
                const auto* yard = findObjectById(probe, yardId);
                if (elapsed < total) {
                    if (yard == nullptr || yard->kind != WorldObjectKind::Structure
                        || yard->mission != "Repacking"
                        || yard->buildAnimTicksRemaining != total - elapsed)
                        return fail("yard converted before the final reverse-animation boundary");
                    if (elapsed == 1) {
                        const int remaining = yard->buildAnimTicksRemaining;
                        (void)undeployRuntimeStructure(probe, yardId);
                        yard = findObjectById(probe, yardId);
                        if (yard == nullptr || yard->buildAnimTicksRemaining != remaining)
                            return fail("repeated repack command restarted or skipped the animation");
                    }
                } else if (yard != nullptr) {
                    return fail("yard was retained after the final reverse-animation boundary");
                }
            }
        }
        const auto selectionAfter = probe.selectedObjectIds;
        (void)advanceRuntimeStructureRepacks(probe);
        int liveAfter = 0;
        for (const auto& object : probe.world.getObjects()) liveAfter += object.alive;
        if (liveAfter != liveBefore || selectionAfter != probe.selectedObjectIds)
            return fail("completed repack was applied more than once");
        return true;
    };
    const auto finishDeployment = [&](RuntimeWindowState& probe, const std::vector<int>& vehicleIds) {
        for(int tick=0;tick<probe.config.fixedTickHz*6;++tick) {
            bool mobile=false;for(int id:vehicleIds)mobile=mobile||findObjectById(probe,id)!=nullptr;
            if(!mobile)return true;
            RuntimeSnapshotActiveStateScope scope(probe);++probe.tick;advanceRuntimeSimulation(probe,false);
        }
        return fail("MCV turn/deployment did not finish");
    };
    struct Faction {
        const char* owner;
        const char* yard;
        const char* vehicle;
    };
    const Faction factions[] = {
        {"Americans", "GACNST", "AMCV"},
        {"Russians", "NACNST", "SMCV"},
        {"YuriCountry", "YACNST", "PCV"},
        // A captured yard must keep its current house through both forms.
        {"Russians", "GACNST", "AMCV"}
    };
    for (const auto& faction : factions) {
        auto storage = makeProbe(faction.owner);
        auto& probe = *storage;
        auto* yard = addRuntimeObjectInstance(
            probe, RuntimeBuildKind::Building, faction.yard, {6, 6});
        if (yard == nullptr) return fail(std::string(faction.yard) + " is missing");
        finishConstruction(*yard);
        const int yardId = yard->instanceId;
        const int yardHealth = runtimeMaxHealthForObject(probe, *yard) / 2;
        yard->health = yardHealth;
        yard->veterancy = 1;
        yard->veterancyExperience = 55;
        yard->crateArmorMultiplier = 1.25;
        yard->crateFirepowerMultiplier = 1.5;
        yard->crateSpeedMultiplier = 1.75;
        yard->crateCloakable = true;
        applySelection(probe, {yardId}, false, false);
        if (yard->deployed || runtimeSecondaryAbilityForObject(probe, *yard)
                != RuntimeSecondaryAbility::Undeploy) {
            return fail(std::string(faction.yard)
                + " in Guard is not offered Undeploy by the click/D command path");
        }
        if (!dispatchRuntimeSecondaryActionFromUi(probe, yardId))
            return fail("click command was not handled");
        if (!finishRepacking(probe, {yardId})) return false;
        auto* vehicle = selected(probe);
        if (findObjectById(probe, yardId) != nullptr || vehicle == nullptr
            || vehicle->type != faction.vehicle || vehicle->owner != faction.owner
            || !runtimeObjectCanMove(probe, *vehicle)
            || runtimeCellPointForObject(*vehicle) != RuntimeCellPoint{6, 6})
            return fail("click did not create a selected movable vehicle on the old foundation");
        const int vehicleId = vehicle->instanceId;
        if (runtimeSkirmishOwnerDefeated(probe, faction.owner))
            return fail("repacking the last construction yard incorrectly defeated its owner");
        const int vehicleHealth = runtimeMaxHealthForObject(probe, *vehicle) / 2;
        if (vehicle->health != vehicleHealth || vehicle->veterancy != 1
            || vehicle->veterancyExperience != 55
            || vehicle->crateArmorMultiplier != 1.25
            || vehicle->crateFirepowerMultiplier != 1.5
            || vehicle->crateSpeedMultiplier != 1.75 || !vehicle->crateCloakable)
            return fail("repacking lost damage, veterancy or crate modifiers");
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(probe))
            return fail("D command could not redeploy the vehicle");
        if (!finishDeployment(probe, {vehicleId})) return false;
        yard = selected(probe);
        if (findObjectById(probe, vehicleId) != nullptr || yard == nullptr
            || yard->type != faction.yard || yard->health != yardHealth
            || yard->owner != faction.owner || yard->veterancyExperience != 55
            || yard->crateArmorMultiplier != 1.25 || !yard->crateCloakable)
            return fail("redeployment lost the selected yard or its state");
        finishConstruction(*yard);
        const int redeployedId = yard->instanceId;
        if (runtimeSkirmishOwnerDefeated(probe, faction.owner))
            return fail("redeploying the surviving MCV incorrectly defeated its owner");
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(probe))
            return fail("D command could not start repacking the deployed yard");
        if (!finishRepacking(probe, {redeployedId})) return false;
        if (findObjectById(probe, redeployedId) != nullptr
            || selected(probe) == nullptr || selected(probe)->type != faction.vehicle)
            return fail("D command could not repack the deployed yard");
        const int secondVehicleId = selected(probe)->instanceId;
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(probe))
            return fail("second deployment was not handled");
        if (!finishDeployment(probe, {secondVehicleId})) return false;
        yard = selected(probe);
        if (yard == nullptr || yard->type != faction.yard)
            return fail("second deployment did not produce a yard");
        finishConstruction(*yard);
        const int movingYardId = yard->instanceId;
        const RuntimeCellPoint destination{20, 20};
        if (selectedMovableObjectCount(probe) != 1
            || !dispatchRuntimeTargetOrderFromUi(probe,
                RuntimeMultiplayerGameplayTargetOrderKind::Primary, destination)
            || probe.lastCommand.acceptedCount != 1
            || probe.lastCommand.rejectedCount != 0
            || findObjectById(probe, movingYardId) == nullptr)
            return fail("ground click did not begin repacking and queue one move order");
        const auto pendingMove = probe.behaviors.find(movingYardId);
        if (pendingMove == probe.behaviors.end() || pendingMove->second.kind != RuntimeBehaviorKind::Move
            || pendingMove->second.moveTarget != destination)
            return fail("repacking yard did not retain its pending move destination");
        if (!finishRepacking(probe, {movingYardId})) return false;
        vehicle = selected(probe);
        if (vehicle == nullptr || vehicle->type != faction.vehicle)
            return fail("move order lost the vehicle selection");
        const int movingVehicleId = vehicle->instanceId;
        bool arrived = false;
        for (int tick = 0; tick < 900; ++tick) {
            ++probe.tick;
            advanceRuntimeSimulation(probe);
            vehicle = findMutableObjectById(probe, movingVehicleId);
            if (vehicle != nullptr && runtimeCellPointForObject(*vehicle) == destination) {
                arrived = true;
                break;
            }
        }
        if (!arrived) return fail("repacked vehicle did not reach the move destination");
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(probe))
            return fail("vehicle could not deploy after moving");
        if (!finishDeployment(probe, {movingVehicleId})) return false;
        yard = selected(probe);
        if (yard == nullptr || yard->type != faction.yard
            || yard->owner != faction.owner || yard->health != yardHealth)
            return fail("relocation did not preserve the yard and its damage");
        int liveObjects = 0;
        for (const auto& object : probe.world.getObjects()) liveObjects += object.alive;
        if (liveObjects != 1) return fail("repeated transformations duplicated an object");

        finishConstruction(*yard);
        const int finalYardId = yard->instanceId;
        probe.config.mcvRepacks = false;
        if (runtimeObjectHasSecondaryAction(probe, *yard)
            || selectedMovableObjectCount(probe) != 0
            || undeployRuntimeStructure(probe, finalYardId))
            return fail("MCV Repacks=no did not disable repacking");
        dispatchRuntimeTargetOrderFromUi(probe,
            RuntimeMultiplayerGameplayTargetOrderKind::Move, {5, 5});
        if (findObjectById(probe, finalYardId) == nullptr
            || probe.lastCommand.acceptedCount != 0)
            return fail("move bypassed MCV Repacks=no");
        probe.config.mcvRepacks = true;
        yard = findMutableObjectById(probe, finalYardId);
        yard->buildAnimTicksRemaining = 10;
        if (runtimeObjectHasSecondaryAction(probe, *yard)
            || selectedMovableObjectCount(probe) != 0
            || undeployRuntimeStructure(probe, finalYardId))
            return fail("unfinished construction could repack");
    }

    {
        auto storage = makeProbe("Americans", 40);
        auto& probe = *storage;
        std::vector<int> vehicleIds;
        for (const RuntimeCellPoint cell : {RuntimeCellPoint{6, 6}, RuntimeCellPoint{26, 26}}) {
            auto* vehicle = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, "AMCV", cell);
            if (vehicle == nullptr) return fail("group MCV fixture could not create a vehicle");
            vehicleIds.push_back(vehicle->instanceId);
        }
        applySelection(probe, vehicleIds, false, false);
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(probe)
            || probe.selectedObjectIds.size() != vehicleIds.size())
            return fail("group deployment lost a transformed actor's selection");
        if (!finishDeployment(probe, vehicleIds)) return false;
        const auto yardIds = probe.selectedObjectIds;
        for (int vehicleId : vehicleIds) {
            if (findObjectById(probe, vehicleId) != nullptr)
                return fail("group deployment stopped after transforming the first MCV");
        }
        for (int yardId : yardIds) {
            auto* deployedYard = findMutableObjectById(probe, yardId);
            if (deployedYard == nullptr || deployedYard->type != "GACNST")
                return fail("group deployment did not leave both construction yards selected");
            finishConstruction(*deployedYard);
        }
        if (!dispatchRuntimeSecondaryActionForSelectionFromUi(probe)
            || probe.selectedObjectIds.size() != yardIds.size())
            return fail("group repacking lost a transformed actor's selection");
        if (!finishRepacking(probe, yardIds)) return false;
        for (int yardId : yardIds) {
            if (findObjectById(probe, yardId) != nullptr)
                return fail("group repacking stopped after transforming the first yard");
        }
        for (int vehicleId : probe.selectedObjectIds) {
            const auto* vehicle = findObjectById(probe, vehicleId);
            if (vehicle == nullptr || vehicle->type != "AMCV")
                return fail("group repacking did not leave both MCVs selected");
        }
    }

    // No exterior cell is available: the collapsing yard itself frees the cell.
    auto enclosedStorage = makeProbe("Americans", 1);
    auto& enclosed = *enclosedStorage;
    auto* yard = addRuntimeObjectInstance(
        enclosed, RuntimeBuildKind::Building, "GACNST", {1, 1});
    if (yard == nullptr) return fail("enclosed yard could not be created");
    finishConstruction(*yard);
    const int enclosedYardId = yard->instanceId;
    applySelection(enclosed, {enclosedYardId}, false, false);
    int enclosedVehicleId = -1;
    if (!undeployRuntimeStructure(enclosed, enclosedYardId, &enclosedVehicleId))
        return fail("the yard blocked its own repack cell");
    if (enclosedVehicleId != enclosedYardId)
        return fail("repack returned a vehicle before its reverse animation completed");
    if (!finishRepacking(enclosed, {enclosedYardId})) return false;
    if (selected(enclosed) == nullptr) return fail("enclosed repack lost its selection");
    enclosedVehicleId = selected(enclosed)->instanceId;
    const auto* vehicle = findObjectById(enclosed, enclosedVehicleId);
    if (vehicle == nullptr || runtimeCellPointForObject(*vehicle) != RuntimeCellPoint{1, 1})
        return fail("enclosed repack did not use the vacated foundation");

    auto blockedStorage = makeProbe("Americans", 1);
    auto& blocked = *blockedStorage;
    yard = addRuntimeObjectInstance(blocked, RuntimeBuildKind::Building, "GACNST", {1, 1});
    if (yard == nullptr) return fail("blocked yard could not be created");
    finishConstruction(*yard);
    const int blockedYardId = yard->instanceId;
    if (addRuntimeObjectInstance(blocked, RuntimeBuildKind::Unit, "MTNK", {1, 1}) == nullptr)
        return fail("blocking unit could not be created");
    if (undeployRuntimeStructure(blocked, blockedYardId)
        || findObjectById(blocked, blockedYardId) == nullptr)
        return fail("repacking overlapped another unit or removed a blocked yard");

    for (int hz : {15, 30, 60}) {
        auto storage = makeProbe("Americans");
        auto& probe = *storage;
        probe.config.fixedTickHz = hz;
        yard = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, "GACNST", {6, 6});
        if (yard == nullptr) return fail("repack clock fixture is missing");
        finishConstruction(*yard);
        const int yardId = yard->instanceId;
        const int expectedTicks = runtimeStructureBuildupTiming(probe, *yard).fixedTicks;
        applySelection(probe, {yardId}, false, false);
        if (!undeployRuntimeStructure(probe, yardId)) return fail("repack clock fixture rejected");
        yard = findMutableObjectById(probe, yardId);
        if (yard == nullptr || yard->buildAnimTicksTotal != expectedTicks)
            return fail("repack duration did not use the rule-frame buildup clock");
        if (!finishRepacking(probe, {yardId})) return false;
    }

    {
        auto storage = makeProbe("Americans");
        auto& probe = *storage;
        yard = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, "GACNST", {6, 6});
        if (yard == nullptr) return fail("selection-change fixture yard is missing");
        finishConstruction(*yard);
        const int yardId = yard->instanceId;
        auto* tank = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, "MTNK", {20, 20});
        if (tank == nullptr) return fail("selection-change fixture tank is missing");
        const int tankId = tank->instanceId;
        applySelection(probe, {yardId}, false, false);
        if (!undeployRuntimeStructure(probe, yardId)) return fail("selection-change repack rejected");
        applySelection(probe, {tankId}, false, false);
        if (!finishRepacking(probe, {yardId})) return false;
        if (probe.selectedObjectIds != std::vector<int>{tankId})
            return fail("finished repack stole selection from a later player choice");
    }

    {
        auto storage = makeProbe("Americans");
        auto& probe = *storage;
        yard = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, "GACNST", {6, 6});
        if (yard == nullptr) return fail("retarget fixture yard is missing");
        finishConstruction(*yard);
        const int yardId = yard->instanceId;
        applySelection(probe, {yardId}, false, false);
        if (!dispatchRuntimeTargetOrderFromUi(probe,
                RuntimeMultiplayerGameplayTargetOrderKind::Primary, {20, 20}))
            return fail("retarget fixture did not begin repacking");
        const auto* startingYard = findObjectById(probe, yardId);
        if (startingYard == nullptr || startingYard->buildAnimTicksRemaining <= 1)
            return fail("retarget fixture did not begin its animation");
        const int startingTicks = startingYard->buildAnimTicksRemaining;
        ++probe.tick;
        (void)advanceRuntimeSimulation(probe);
        yard = findMutableObjectById(probe, yardId);
        if (yard == nullptr || yard->mission != "Repacking"
            || yard->buildAnimTicksRemaining != startingTicks - 1
            || runtimeCellPointForObject(*yard) != RuntimeCellPoint{6, 6})
            return fail("normal simulation moved the yard or skipped its repack timer");
        const RuntimeCellPoint retarget{22, 18};
        if (selectedMovableObjectCount(probe) != 1
            || !dispatchRuntimeTargetOrderFromUi(probe,
                RuntimeMultiplayerGameplayTargetOrderKind::Primary, retarget)
            || probe.lastCommand.acceptedCount != 1)
            return fail("ground click could not update a repacking yard's destination");
        yard = findMutableObjectById(probe, yardId);
        const auto pending = probe.behaviors.find(yardId);
        if (yard == nullptr || yard->buildAnimTicksRemaining != startingTicks - 1
            || pending == probe.behaviors.end() || pending->second.moveTarget != retarget)
            return fail("retarget restarted the animation or lost the new destination");
        if (!finishRepacking(probe, {yardId})) return false;
        const auto* vehicle = selected(probe);
        if (vehicle == nullptr || vehicle->type != "AMCV")
            return fail("retargeted repack lost its vehicle");
        const auto move = probe.behaviors.find(vehicle->instanceId);
        if (move == probe.behaviors.end() || move->second.kind != RuntimeBehaviorKind::Move
            || move->second.moveTarget != retarget)
            return fail("retargeted vehicle did not inherit the latest destination");
    }

    std::cout << "MCV repack: Allied/Soviet/Yuri/captured=ok click/D=ok "
        "move/redeploy=ok selection/owner/health/veterancy/crates=ok "
        "disabled/unfinished=ok ownFoundation/blocked=ok noDuplicates=ok "
        "reverseAnimation/exactBoundary/repeatedCommand=ok clocks=15/30/60 "
        "selectionChange/normalTick/retarget=ok\n";
    return true;
}
