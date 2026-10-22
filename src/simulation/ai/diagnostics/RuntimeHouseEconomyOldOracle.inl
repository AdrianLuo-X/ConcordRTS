bool s16OldAdvanceRuntimeHarvesterEconomy(RuntimeWindowState& state) {
    RuntimePerfTraceScope trace("s16OldAdvanceRuntimeHarvesterEconomy", state.tick);
    const std::string currentOwner =
        runtimeCanonicalHumanHouseOwner(state.economy.owner);
    for (auto taskIt = state.harvesterTasks.begin();
        taskIt != state.harvesterTasks.end();) {
        const WorldObjectInstance* taskObject =
            state.objectIndex.find(state.world.getObjects(), taskIt->first);
        // Contained vehicles are hidden, but their ore remains persistent cargo.
        if (taskObject == nullptr || taskObject->health <= 0
            || (!taskObject->alive && toLowerAsciiLocal(trimAsciiLocal(taskObject->mission)) != "insidetransport")) {
            taskIt = state.harvesterTasks.erase(taskIt);
        } else if (runtimeCanonicalHumanHouseOwner(taskObject->owner)
                == currentOwner
            && !runtimeObjectIsHarvester(state, *taskObject)) {
            taskIt = state.harvesterTasks.erase(taskIt);
        } else {
            ++taskIt;
        }
    }
    if (state.economy.harvesters <= 0) {
        return false;
    }

    bool changed = false;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive
            || object.owner != state.economy.owner
            || !runtimeObjectIsHarvester(state, object)
            || object.harvestAutoSearchSuspended
            || state.behaviors.find(object.instanceId) != state.behaviors.end()) {
            continue;
        }

        RuntimeHarvesterTask& task = state.harvesterTasks[object.instanceId];
        normalizeRuntimeHarvesterTask(state, object, task);
        if (task.phase == RuntimeHarvesterPhase::Docking
            || task.phase == RuntimeHarvesterPhase::PreparingToUnload
            || task.phase == RuntimeHarvesterPhase::Unloading) {
            if (findRuntimeHarvesterRefineryById(state, object, task.targetRefineryId) == nullptr
                || !runtimeHarvesterIsAtDock(object, task)) {
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.targetRefineryId = -1;
                task.workTicksRemaining = 0;
                task.nextThinkTick = state.tick;
                task.status = "refinery lost before delivery";
                object.mission = "Harvest";
                changed = true;
            }
        }
        if ((task.phase == RuntimeHarvesterPhase::Idle
                || task.phase == RuntimeHarvesterPhase::SeekingOre)
            && task.cargoValue <= 0
            && task.nextThinkTick > state.tick) {
            continue;
        }
        if ((task.phase == RuntimeHarvesterPhase::ReturningToRefinery
                || task.phase == RuntimeHarvesterPhase::WaitingForRefineryDock)
            && task.nextThinkTick > state.tick
            && !runtimeHarvesterIsAtDock(object, task)) {
            continue;
        }

        if (task.phase == RuntimeHarvesterPhase::Loading) {
            const int intakeFacing = runtimeFacingFromDelta(
                task.targetOre.x - object.x, task.targetOre.y - object.y);
            if (intakeFacing >= 0
                && runtimeFacingAbsDifference(object.facing, intakeFacing) > 4) {
                // A restored task or another order may have changed the hull
                // facing. Realign the hull before restarting the pickup.
                task.phase = RuntimeHarvesterPhase::SeekingOre;
                task.workTicksRemaining = 0;
                task.nextThinkTick = state.tick + 1;
                task.status = "realigning intake";
                object.mission = "HarvestAlign";
                changed = true;
                continue;
            }
            if (task.workTicksRemaining > 0) {
                --task.workTicksRemaining;
                object.mission = "HarvestLoad";
                task.status = "loading ore";
                changed = true;
                if (task.workTicksRemaining > 0) continue;
            }

            // Consume only the cell that the intake was aligned to. If another
            // miner exhausted it, select and align a new target on the next tick.
            const std::optional<RuntimeCellPoint> resourceCell =
                RuntimeHarvesterResourceAccess(state, object).canGatherFrom(
                    runtimeCellPointForObject(object), task.targetOre)
                    ? std::make_optional(task.targetOre) : std::nullopt;
            if (!resourceCell.has_value()) {
                task.phase = RuntimeHarvesterPhase::SeekingOre;
                task.workTicksRemaining = 0;
                task.status = "ore cell unavailable";
                task.nextThinkTick = state.tick + 1;
                changed = true;
                continue;
            }

            const TerrainCell* beforeGatherTerrain =
                findRuntimeTerrainCellAt(state, *resourceCell);
            const int beforeGatherBails =
                beforeGatherTerrain == nullptr
                    ? 0
                    : runtimeResourceBailCountForTerrain(*beforeGatherTerrain);
            const std::string beforeGatherOverlay =
                beforeGatherTerrain == nullptr
                    ? "none"
                    : runtimeResourceOverlayDiagnosticName(
                        *beforeGatherTerrain
                    );
            bool wasGem = false;
            const int value =
                consumeRuntimeOreAtCell(state, *resourceCell, &wasGem);
            if (value <= 0) {
                task.phase =
                    task.cargoValue > 0
                        ? RuntimeHarvesterPhase::ReturningToRefinery
                        : RuntimeHarvesterPhase::SeekingOre;
                task.status = "ore disappeared";
                task.nextThinkTick =
                    state.tick + runtimeHarvesterThinkCooldownTicks(state);
                changed = true;
                continue;
            }
            const TerrainCell* afterGatherTerrain =
                findRuntimeTerrainCellAt(state, *resourceCell);
            state.economy.lastHarvesterGatherCell = *resourceCell;
            state.economy.lastHarvesterGatherBeforeBails =
                beforeGatherBails;
            state.economy.lastHarvesterGatherAfterBails =
                afterGatherTerrain == nullptr
                    ? 0
                    : runtimeResourceBailCountForTerrain(*afterGatherTerrain);
            state.economy.lastHarvesterGatherOverlayBefore =
                beforeGatherOverlay;
            state.economy.lastHarvesterGatherOverlayAfter =
                afterGatherTerrain == nullptr
                    ? "none"
                    : runtimeResourceOverlayDiagnosticName(
                        *afterGatherTerrain
                    );

            task.cargoValue += value;
            ++task.cargoUnits;
            if (wasGem) {
                ++task.cargoGemUnits;
            }
            task.cargoHasGems = task.cargoHasGems || wasGem;
            task.status =
                "loaded " + std::to_string(task.cargoUnits)
                + "/" + std::to_string(task.cargoCapacity)
                + " cargo $" + std::to_string(task.cargoValue);
            state.economy.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " " + task.status;
            changed = true;

            if (task.cargoUnits >= task.cargoCapacity) {
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.workTicksRemaining = 0;
            } else if (afterGatherTerrain != nullptr
                && runtimeResourceBailCountForTerrain(*afterGatherTerrain) > 0) {
                // Restart at the pickup boundary, without adding one/two
                // simulation ticks per bail for an unchanged intake target.
                task.phase = RuntimeHarvesterPhase::Loading;
                task.workTicksRemaining = runtimeHarvesterLoadTicks(state, object);
                spawnRuntimeHarvesterGatherEffect(state, object, *resourceCell);
            } else {
                // Keep mining this deposit until it is depleted. Re-ranking
                // neighboring cells after each bail caused needless body turns.
                task.phase = RuntimeHarvesterPhase::SeekingOre;
                task.workTicksRemaining = 0;
                task.nextThinkTick = state.tick + 1;
            }
            changed = true;
            continue;
        }

        if (task.phase == RuntimeHarvesterPhase::Docking) {
            int refineryDockFacing = runtimeFacingFromDelta(1, 0);
            if (WorldObjectInstance* refinery =
                    findMutableObjectById(state, task.targetRefineryId)) {
                if (refinery->alive
                    && refinery->kind == WorldObjectKind::Structure) {
                    refinery->mission = "Unload";
                    refineryDockFacing =
                        runtimeHarvesterRefineryDockFacing(
                            state,
                            *refinery,
                            task.dockCell
                        );
                }
            }

            const int currentFacing =
                object.facing >= 0 ? object.facing : refineryDockFacing;
            object.facing = runtimeAdvanceBodyFacing(state, object, currentFacing, refineryDockFacing);
            object.mission = "Dock";
            task.status = "docking at refinery";
            state.economy.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " docking at refinery";
            if (runtimeFacingAbsDifference(
                    object.facing,
                    refineryDockFacing
                ) <= 4) {
                object.facing = refineryDockFacing;
                task.phase = RuntimeHarvesterPhase::PreparingToUnload;
                task.workTicksRemaining = runtimeHarvesterUnloadTicks(state);
                task.status = "preparing to unload";
            }
            changed = true;
            continue;
        }

        if (task.phase == RuntimeHarvesterPhase::PreparingToUnload) {
            if (WorldObjectInstance* refinery =
                    findMutableObjectById(state, task.targetRefineryId)) {
                if (refinery->alive
                    && refinery->kind == WorldObjectKind::Structure) {
                    refinery->mission = "Unload";
                }
            }
            if (task.workTicksRemaining > 0) {
                --task.workTicksRemaining;
                object.mission = "Unload";
                task.status = "preparing to unload";
                state.economy.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " preparing to unload $"
                    + std::to_string(task.cargoValue);
                changed = true;
                if (task.workTicksRemaining > 0) continue;
            }
            task.phase = RuntimeHarvesterPhase::Unloading;
            task.status = "unloading at refinery";
            changed = true;
        }

        if (task.phase == RuntimeHarvesterPhase::Unloading) {
            // Retail TiberiumStorage unloads the first nonempty resource type
            // in full, then restarts its dump timer for the next type. Our two
            // cargo classes are Riparius (ore) followed by Cruentus (gems).
            const int oreUnits = task.cargoUnits - task.cargoGemUnits;
            const bool oreBatch = oreUnits > 0;
            const int batchUnits = oreBatch ? oreUnits : task.cargoGemUnits;
            const int batchGemUnits = oreBatch ? 0 : task.cargoGemUnits;
            const bool finalBatch = batchUnits >= task.cargoUnits;
            const int batchValue = finalBatch ? task.cargoValue : std::clamp(
                static_cast<int>(std::min<long long>(
                    static_cast<long long>(oreUnits)
                        * runtimeTiberiumBailValue(state.rules, "Riparius", 25),
                    std::numeric_limits<int>::max())),
                0, task.cargoValue);
            if (WorldObjectInstance* refinery =
                    findMutableObjectById(state, task.targetRefineryId)) {
                if (refinery->alive
                    && refinery->kind == WorldObjectKind::Structure) {
                    refinery->mission = "Unload";
                    if (task.cargoValue > 0) {
                        spawnRuntimeRefinerySmokeEffects(
                            state,
                            *refinery,
                            object
                        );
                    }
                }
            }
            if (batchValue > 0) {
                int purifierCount = 0;
                const int purifierBonus = runtimeOrePurifierBonusValue(
                    state,
                    batchValue,
                    &purifierCount
                );
                const int deliveredValue = batchValue + purifierBonus;
                depositRuntimeCredits(state, deliveredValue);
                recordRuntimeResourceDeliveryToStorage(
                    state,
                    batchUnits,
                    batchGemUnits,
                    deliveredValue
                );
                if (finalBatch) ++state.economy.resourceTrips;
                state.economy.lastResourceBaseValue = batchValue;
                state.economy.lastResourcePurifierBonus = purifierBonus;
                state.economy.lastResourcePurifierCount = purifierCount;
                state.economy.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " delivered $" + std::to_string(deliveredValue)
                    + " base=$" + std::to_string(batchValue)
                    + " purifier=$" + std::to_string(purifierBonus)
                    + " credits=" + std::to_string(state.economy.credits);
                emitRuntimeSound(state, "harvest unload");
            }
            task.cargoValue -= batchValue;
            task.cargoUnits -= batchUnits;
            task.cargoGemUnits -= batchGemUnits;
            task.cargoHasGems = task.cargoGemUnits > 0;
            if (!finalBatch && task.cargoUnits > 0 && task.cargoValue > 0) {
                task.phase = RuntimeHarvesterPhase::PreparingToUnload;
                task.workTicksRemaining = runtimeHarvesterUnloadTicks(state);
                task.status = "preparing next resource type";
                changed = true;
                continue;
            }
            task.cargoValue = 0;
            task.cargoUnits = 0;
            task.cargoGemUnits = 0;
            task.cargoHasGems = false;
            task.phase = RuntimeHarvesterPhase::SeekingOre;
            task.workTicksRemaining = 0;
            task.nextThinkTick = state.tick + 1;
            task.status = "delivered cargo";
            object.mission = "Guard";
            if (WorldObjectInstance* refinery =
                    findMutableObjectById(state, task.targetRefineryId)) {
                if (refinery->alive && refinery->mission == "Unload") {
                    refinery->mission = "Guard";
                }
            }
            changed = true;
            continue;
        }

        const bool cargoFull =
            task.cargoUnits >= task.cargoCapacity;
        if (cargoFull
            || task.phase == RuntimeHarvesterPhase::ReturningToRefinery
            || task.phase == RuntimeHarvesterPhase::WaitingForRefineryDock) {
            if (state.economy.refineries <= 0) {
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.status = "waiting for refinery";
                object.mission = "Guard";
                task.nextThinkTick =
                    state.tick + runtimeHarvesterThinkCooldownTicks(state);
                state.economy.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " has cargo but no refinery";
                changed = true;
                continue;
            }

            const WorldObjectInstance* refinery =
                findRuntimeHarvesterRefineryById(
                    state,
                    object,
                    task.targetRefineryId
                );
            if (refinery == nullptr) {
                const std::optional<int> refineryId =
                    findNearestRuntimeRefineryId(state, object);
                task.targetRefineryId = refineryId.value_or(-1);
                refinery = findRuntimeHarvesterRefineryById(
                    state,
                    object,
                    task.targetRefineryId
                );
            }
            if (refinery == nullptr) {
                task.status = "no refinery found";
                task.nextThinkTick =
                    state.tick + runtimeHarvesterThinkCooldownTicks(state);
                changed = true;
                continue;
            }

            const double tooFarThreshold =
                runtimeHarvesterTooFarDistanceForObject(state, object);
            const double refineryDistance =
                runtimeCellDistance(
                    runtimeCellPointForObject(object),
                    runtimeCellPointForObject(*refinery)
                );
            if (tooFarThreshold > 0.0
                && refineryDistance > tooFarThreshold
                && !runtimeHarvesterIsAtDock(object, task)) {
                std::optional<RuntimeCellPoint> approachTarget =
                    findRuntimeRefineryQueueCell(state, object, *refinery);
                if (!approachTarget.has_value()) {
                    approachTarget =
                        findRuntimePassableCellNear(
                            state,
                            object,
                            runtimeCellPointForObject(*refinery),
                            8,
                            -1
                        );
                }
                if (approachTarget.has_value()) {
                    std::optional<RuntimeCellPoint> actualTarget;
                    if (issueRuntimeHarvesterMoveToCell(
                            state,
                            object,
                            *approachTarget,
                            "Return",
                            &actualTarget,
                            -1,
                            true
                        )) {
                        task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                        task.dockCell = RuntimeCellPoint{};
                        task.queueCell =
                            actualTarget.value_or(*approachTarget);
                        task.status =
                            "approaching refinery #"
                            + std::to_string(task.targetRefineryId);
                        task.nextThinkTick = state.tick + 1;
                        state.economy.lastHarvesterTooFarApproachTarget =
                            task.queueCell;
                        state.economy.lastHarvesterTooFarDistance =
                            refineryDistance;
                        state.economy.lastHarvesterTooFarThreshold =
                            tooFarThreshold;
                        state.economy.lastHarvesterTooFarChrono =
                            runtimeObjectIsChronoHarvester(state, object);
                        state.economy.status =
                            "harvester #"
                            + std::to_string(object.instanceId)
                            + " approaching refinery before dock";
                        changed = true;
                        continue;
                    }
                }
            }

            const std::optional<RuntimeCellPoint> dockCell =
                findRuntimeRefineryDockCell(state, object, *refinery);
            if (dockCell.has_value()) {
                task.dockCell = *dockCell;
                task.queueCell = RuntimeCellPoint{};
            } else if (!runtimeHarvesterIsAtDock(object, task)) {
                task.dockCell = runtimeRefineryDefaultDockCell(state, *refinery);
                const std::optional<RuntimeCellPoint> queueCell =
                    findRuntimeRefineryQueueCell(state, object, *refinery);
                if (queueCell.has_value()) {
                    task.queueCell = *queueCell;
                    if (runtimeCellPointForObject(object) == *queueCell) {
                        task.phase =
                            RuntimeHarvesterPhase::WaitingForRefineryDock;
                        task.status = "waiting for refinery dock";
                        task.nextThinkTick =
                            state.tick + runtimeHarvesterThinkCooldownTicks(state);
                        object.mission = "Return";
                        state.economy.status =
                            "harvester #"
                            + std::to_string(object.instanceId)
                            + " waiting for refinery dock";
                        changed = true;
                        continue;
                    }

                    std::optional<RuntimeCellPoint> actualQueueTarget;
                    if (issueRuntimeHarvesterMoveToCell(
                            state,
                            object,
                            *queueCell,
                            "Return",
                            &actualQueueTarget,
                            -1,
                            true
                        )) {
                        if (actualQueueTarget.has_value()) {
                            task.queueCell = *actualQueueTarget;
                        }
                        task.phase =
                            RuntimeHarvesterPhase::WaitingForRefineryDock;
                        task.status =
                            "queueing for refinery #"
                            + std::to_string(task.targetRefineryId);
                        task.nextThinkTick = state.tick + 1;
                        state.economy.status =
                            "harvester #"
                            + std::to_string(object.instanceId)
                            + " queueing for refinery dock";
                        changed = true;
                        continue;
                    }
                }

                task.status = "refinery dock blocked";
                task.nextThinkTick =
                    state.tick + runtimeHarvesterThinkCooldownTicks(state);
                state.economy.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " cannot dock at " + refinery->type;
                changed = true;
                continue;
            }

            if (runtimeHarvesterIsAtDock(object, task)) {
                task.phase = RuntimeHarvesterPhase::Docking;
                task.workTicksRemaining = 0;
                task.status = "dock reached";
                object.mission = "Dock";
                if (WorldObjectInstance* mutableRefinery =
                        findMutableObjectById(state, task.targetRefineryId)) {
                    mutableRefinery->mission = "Unload";
                }
                changed = true;
                continue;
            }

            if (runtimeChronoHarvesterCanTeleportToCell(
                    state,
                    object,
                    task.dockCell,
                    task.targetRefineryId
                )) {
                if (!teleportRuntimeHarvesterToCell(
                        state,
                        object,
                        task.dockCell
                    )) {
                    task.status = "chrono parasite release rejected";
                    changed = true;
                    continue;
                }
                task.phase = RuntimeHarvesterPhase::Docking;
                task.workTicksRemaining = 0;
                task.status = "chrono teleported to refinery";
                object.mission = "Dock";
                if (WorldObjectInstance* mutableRefinery =
                        findMutableObjectById(state, task.targetRefineryId)) {
                    if (mutableRefinery->alive
                        && mutableRefinery->kind == WorldObjectKind::Structure) {
                        mutableRefinery->mission = "Unload";
                    }
                }
                state.economy.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " chrono teleported to refinery";
                changed = true;
                continue;
            }

            std::optional<RuntimeCellPoint> actualTarget;
            if (issueRuntimeHarvesterMoveToCell(
                    state,
                    object,
                    task.dockCell,
                    "Return",
                    &actualTarget,
                    task.targetRefineryId,
                    true
                )) {
                if (actualTarget.has_value()) {
                    task.dockCell = *actualTarget;
                }
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.status =
                    "returning to refinery #"
                    + std::to_string(task.targetRefineryId);
                state.economy.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " returning cargo $" + std::to_string(task.cargoValue);
                changed = true;
            }
            continue;
        }

        task.phase = RuntimeHarvesterPhase::SeekingOre;
        std::optional<RuntimeCellPoint> loadResourceCell =
            findHarvestableRuntimeResourceNear(state, object, task.targetOre);
        const std::optional<RuntimeCellPoint> scanOrigin =
            task.targetOre.x >= 0 && task.targetOre.y >= 0
                ? std::make_optional(task.targetOre)
                : std::nullopt;
        const std::optional<RuntimeCellPoint> oreCell = loadResourceCell.has_value()
            ? loadResourceCell : findNearestRuntimeOreCell(state, object, scanOrigin, true);
        if (!oreCell.has_value()) {
            if (task.cargoValue > 0) {
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.status = "returning partial load: no ore found";
                task.nextThinkTick = state.tick + 1;
                object.mission = "Return";
                state.economy.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " returning partial cargo $"
                    + std::to_string(task.cargoValue);
                changed = true;
                continue;
            }
            task.status = "no ore found";
            task.nextThinkTick =
                state.tick + runtimeHarvesterThinkCooldownTicks(state);
            object.mission = "Guard";
            state.economy.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " no ore found";
            changed = true;
            continue;
        }

        task.targetOre = *oreCell;
        if (!loadResourceCell.has_value()) {
            loadResourceCell = findHarvestableRuntimeResourceNear(state, object, task.targetOre);
        }
        if (loadResourceCell.has_value()) {
            // Store the actual adjacent resource before turning, including when
            // the original order referred to a more distant part of the field.
            task.targetOre = *loadResourceCell;
            if (!alignRuntimeHarvesterToResource(
                    state,
                    object,
                    *loadResourceCell
                )) {
                task.phase = RuntimeHarvesterPhase::SeekingOre;
                task.status = "aligning to ore";
                task.nextThinkTick = state.tick + 1;
                object.mission = "HarvestAlign";
                changed = true;
                continue;
            }
            task.phase = RuntimeHarvesterPhase::Loading;
            task.workTicksRemaining =
                runtimeHarvesterLoadTicks(state, object);
            task.status = "loading ore";
            task.nextThinkTick = state.tick + 1;
            object.mission = "HarvestLoad";
            spawnRuntimeHarvesterGatherEffect(state, object, *loadResourceCell);
            changed = true;
        } else if (issueRuntimeHarvesterMoveToCell(
                state,
                object,
                *oreCell,
                "Harvest"
            )) {
            task.phase = RuntimeHarvesterPhase::MovingToOre;
            task.workTicksRemaining = 0;
            task.status =
                "moving to ore "
                + std::to_string(oreCell->x)
                + ","
                + std::to_string(oreCell->y);
            task.nextThinkTick = state.tick + 1;
            state.economy.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " moving to ore "
                + std::to_string(oreCell->x)
                + ","
                + std::to_string(oreCell->y);
            changed = true;
        } else {
            task.status = "ore unreachable";
            task.nextThinkTick =
                state.tick + runtimeHarvesterThinkCooldownTicks(state);
            state.economy.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " ore unreachable";
            changed = true;
        }
    }

    return changed;
}

// The mining state machines operate on a house economy facade. Computers use
// the same machines, but their actual credit account remains in AI/campaign
// state. Keep local production, selection feedback and technology out of this
// short-lived facade, including when a slave miner deploys or repacks.
class S16OldRuntimeComputerMiningScope {
public:
    S16OldRuntimeComputerMiningScope(RuntimeWindowState& state, const std::string& owner)
        : state_(state), owner_(runtimeRepairDepotCanonicalOwnerId(state, owner)),
          startingCredits_(runtimeRepairDepotCreditBalance(state, owner)),
          ledger_(state.computerMiningByOwner[owner_]),
          interaction_(state.interactionStatus), simulation_(state.simulationStatus),
          lastCommand_(state.lastCommand),
          economy_(std::move(state.economy)), techTree_(std::move(state.techTree)),
          techTreeLoaded_(state.techTreeLoaded),
          suppressAudio_(state.runtimeSuppressPlayerAudioFeedback) {
        state.economy = RuntimeEconomyState{};
        state.economy.owner = owner;
        state.economy.credits = startingCredits_;
        state.economy.storedResourceUnits = ledger_.storedResourceUnits;
        state.economy.storedResourceValue = ledger_.storedResourceValue;
        state.economy.storedResourceGemUnits = ledger_.storedResourceGemUnits;
        state.economy.resourceTrips = ledger_.resourceTrips;
        state.economy.harvesterTeleports = ledger_.harvesterTeleports;
        state.economy.slaveMinerLoads = ledger_.slaveMinerLoads;
        state.economy.skirmishStartApplied = economy_.skirmishStartApplied;
        state.techTree = TechTreeReport{};
        state.techTreeLoaded = false;
        state.humanHouseScopeActive = true;
        state.remoteHumanHouseScopeActive = true;
        state.computerMiningScopeActive = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
        refreshRuntimeEconomyMetricsForCurrentOwner(state);
    }
    S16OldRuntimeComputerMiningScope(const S16OldRuntimeComputerMiningScope&) = delete;
    S16OldRuntimeComputerMiningScope& operator=(const S16OldRuntimeComputerMiningScope&) = delete;
    ~S16OldRuntimeComputerMiningScope() {
        const int income = std::max(0, state_.economy.credits - startingCredits_);
        ledger_.storedResourceUnits = state_.economy.storedResourceUnits;
        ledger_.storedResourceValue = state_.economy.storedResourceValue;
        ledger_.storedResourceGemUnits = state_.economy.storedResourceGemUnits;
        ledger_.resourceTrips = state_.economy.resourceTrips;
        ledger_.harvesterTeleports = state_.economy.harvesterTeleports;
        ledger_.slaveMinerLoads = state_.economy.slaveMinerLoads;
        state_.economy = std::move(economy_);
        state_.techTree = std::move(techTree_);
        state_.techTreeLoaded = techTreeLoaded_;
        state_.interactionStatus = std::move(interaction_);
        state_.simulationStatus = std::move(simulation_);
        state_.lastCommand = std::move(lastCommand_);
        state_.humanHouseScopeActive = false;
        state_.remoteHumanHouseScopeActive = false;
        state_.computerMiningScopeActive = false;
        state_.runtimeSuppressPlayerAudioFeedback = suppressAudio_;
        // Restore the facade first: this routes the deposit to its sole real
        // account, rather than back into the temporary harvesting balance.
        depositRuntimeRepairDepotCredits(state_, owner_, income);
        const auto ai = state_.computerAiByOwner.find(owner_);
        if (!state_.campaignMode && ai != state_.computerAiByOwner.end()) {
            ai->second.incomeEarned = static_cast<int>(std::min<long long>(
                std::numeric_limits<int>::max(),
                static_cast<long long>(ai->second.incomeEarned) + income));
        }
    }
private:
    RuntimeWindowState& state_;
    std::string owner_;
    int startingCredits_;
    RuntimeComputerMiningState& ledger_;
    std::string interaction_, simulation_;
    decltype(RuntimeWindowState::lastCommand) lastCommand_;
    RuntimeEconomyState economy_;
    TechTreeReport techTree_;
    bool techTreeLoaded_, suppressAudio_;
};

bool s16OldAdvanceRuntimeComputerMining(RuntimeWindowState& state) {
    if (!state.rulesLoaded || state.humanHouseScopeActive
        || state.skirmishMatchFinished) return false;
    std::vector<std::string> owners;
    if (state.campaignMode) {
        for (const auto& [key, house] : state.triggers.campaignHouseProduction) {
            if (!house.playerControlled && house.side >= 0
                && runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner) != key)
                owners.push_back(house.owner);
        }
    } else {
        if (!runtimeComputerAiEnabledForSession(state)
            || !state.economy.skirmishStartApplied) return false;
        ensureRuntimeComputerAiStates(state);
        for (const auto& [key, ai] : state.computerAiByOwner) {
            if (ai.initialized && !findRuntimeHumanHouseAuthority(state, ai.owner)
                && runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner) != key)
                owners.push_back(ai.owner);
        }
    }
    std::sort(owners.begin(), owners.end());
    bool changed = false;
    for (const std::string& owner : owners) {
        S16OldRuntimeComputerMiningScope scope(state, owner);
        changed = s16OldAdvanceRuntimeHarvesterEconomy(state) || changed;
        changed = advanceRuntimeMobileSlaveMinerEconomy(state) || changed;
        changed = advanceRuntimeSlaveMinerEconomy(state) || changed;
    }
    return changed;
}
