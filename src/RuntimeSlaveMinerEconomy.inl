// Workers in a mobile miner retain their identity and health, but do not
// occupy a ground subcell or participate as independent active units.
void synchronizeRuntimeStoredSlaveMinerWorker(
    RuntimeWindowState& state, WorldObjectInstance& worker,
    const WorldObjectInstance& refinery
) {
    releaseRuntimeInfantrySubcellReservation(state, worker.instanceId);
    worker.x = refinery.x;
    worker.y = refinery.y;
    worker.cell = refinery.cell;
    worker.level = refinery.level;
    worker.onBridge = refinery.onBridge;
    worker.airborne = false;
    worker.flightLevel = refinery.level;
    worker.subCell = -1;
    worker.alive = false;
    worker.mission = "InsideSlaveMiner";
    std::erase(state.selectedObjectIds, worker.instanceId);
    // There is no separate body in storage; spatial audio uses the live host.
    clearRuntimeRenderPosition(worker);
}

const BuildingType* runtimeSlaveMinerParentBuilding(
    const RuntimeWindowState& state, const WorldObjectInstance& refinery
) {
    const auto* mobile = runtimeObjectIsMobileSlaveMiner(state, refinery)
        ? findRuntimeUnitType(state.rules, refinery) : nullptr;
    if (!mobile && refinery.kind != WorldObjectKind::Structure) return nullptr;
    const auto* building = findRuntimeBuildingTypeById(
        state.rules, mobile ? mobile->deploysInto : refinery.type);
    return building && building->storage > 0
        && runtimeBuildingCanGatherResources(*building) && !building->enslaves.empty()
        ? building : nullptr;
}

bool runtimeSlaveMinerWorkerIsStored(
    const RuntimeWindowState& state, const WorldObjectInstance& worker
) {
    const auto task = state.slaveMinerTasks.find(worker.instanceId);
    return task != state.slaveMinerTasks.end()
        && task->second.phase == RuntimeSlaveMinerPhase::Stored
        && !worker.alive && worker.health > 0 && worker.mission == "InsideSlaveMiner";
}

void synchronizeRuntimeStoredSlaveMinerWorkers(RuntimeWindowState& state) {
    for (auto& worker : state.world.accessObjects()) {
        if (!runtimeSlaveMinerWorkerIsStored(state, worker)) continue;
        const auto& task = state.slaveMinerTasks.at(worker.instanceId);
        const auto* refinery = findObjectById(state, task.refineryId);
        if (!refinery || !refinery->alive || refinery->health <= 0
            || !runtimeSlaveMinerParentBuilding(state, *refinery)
            || runtimeCanonicalHumanHouseOwner(worker.owner)
                != runtimeCanonicalHumanHouseOwner(refinery->owner)) continue;
        synchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
    }
}

int runtimeSlaveMinerWorkerCapacity(const UnitType& worker) {
    return std::max(1, worker.storage > 0 ? worker.storage : 4);
}

int runtimeSlaveMinerWorkerHarvestTicks(
    const RuntimeWindowState& state,
    const UnitType& worker,
    const BuildingType& building
) {
    if (worker.harvestRate > 0) {
        return runtimeRuleFramesToTicks(worker.harvestRate, state.config.fixedTickHz);
    }
    if (building.harvestRate > 0) {
        return runtimeRuleFramesToTicks(building.harvestRate, state.config.fixedTickHz);
    }
    return std::max(12, runtimeSlaveMinerGatherIntervalTicks(state, building));
}

int runtimeSlaveMinerWorkerRegenTicks(
    const RuntimeWindowState& state, const BuildingType& building
) {
    return runtimeRuleFramesToTicks(building.slaveRegenRate > 0
        ? building.slaveRegenRate : 500, state.config.fixedTickHz);
}

bool runtimeObjectIsSlaveMinerWorker(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object,
    const std::string& workerTypeId
) {
    if ((!object.alive && !runtimeSlaveMinerWorkerIsStored(state, object))
        || object.health <= 0
        || runtimeCanonicalHumanHouseOwner(object.owner)
            != runtimeCanonicalHumanHouseOwner(state.economy.owner)
        || object.kind != WorldObjectKind::Infantry
        || toUpperAsciiLocal(object.type) != toUpperAsciiLocal(workerTypeId)) {
        return false;
    }

    const UnitType* worker = findRuntimeUnitType(state.rules, object);
    return worker != nullptr && worker->slaved;
}

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerWorkerSpawnCell(
    const RuntimeWindowState& state,
    const WorldObjectInstance& refinery,
    const std::string& workerTypeId
) {
    WorldObjectInstance probe;
    probe.instanceId = -1;
    probe.kind = WorldObjectKind::Infantry;
    probe.owner = refinery.owner;
    probe.type = workerTypeId;
    probe.alive = true;
    probe.level = refinery.level;
    probe.onBridge = refinery.onBridge;
    for (const auto& cell : runtimeStructureFootprintCellsForObject(state, refinery)) {
        if (findRuntimePassableCellNear(state, probe, cell, 0, refinery.instanceId)) {
            return cell;
        }
    }
    // Occupied interior cells delay the next worker; never substitute nearby
    // ground and make it appear outside the building.
    return std::nullopt;
}

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerWorkerExitCell(
    const RuntimeWindowState& state,
    const WorldObjectInstance& refinery,
    const std::string& workerTypeId
) {
    WorldObjectInstance probe;
    probe.instanceId = -1;
    probe.kind = WorldObjectKind::Infantry;
    probe.owner = state.economy.owner;
    probe.type = workerTypeId;
    probe.alive = true;
    probe.level = refinery.level;
    probe.onBridge = refinery.onBridge;
    RuntimeHarvesterResourceAccess access(state, probe);

    const RuntimeCellPoint origin = runtimeCellPointForObject(refinery);
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, refinery);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    for (int radius = 1; radius <= 6; ++radius) {
        for (int y = origin.y - radius;
            y <= origin.y + height - 1 + radius;
            ++y) {
            for (int x = origin.x - radius;
                x <= origin.x + width - 1 + radius;
                ++x) {
                const bool onBorder =
                    x == origin.x - radius
                    || x == origin.x + width - 1 + radius
                    || y == origin.y - radius
                    || y == origin.y + height - 1 + radius;
                if (!onBorder) {
                    continue;
                }

                const std::optional<RuntimeCellPoint> passable =
                    findRuntimePassableCellNear(state, probe, {x, y}, 0);
                if (passable.has_value()) {
                    const auto neighbors = runtimeNeighborsOf(*passable);
                    if (std::any_of(neighbors.begin(), neighbors.end(), [&](const auto& next) {
                        return runtimeCellTransitionIsPassable(state, *access.terrain,
                            access.staticOccupancy, *passable, next, probe, -1, nullptr, false);
                    })) return passable;
                }
            }
        }
    }

    return std::nullopt;
}

bool ensureRuntimeSlaveMinerWorkers(
    RuntimeWindowState& state,
    const WorldObjectInstance& refinery,
    const BuildingType& building
) {
    const int refineryId = refinery.instanceId;
    const int refineryFacing = refinery.facing;
    const std::string refineryOwner = refinery.owner;
    const std::string workerTypeId = runtimeSlaveMinerWorkerTypeId(building);
    const UnitType* workerType =
        findRuntimeUnitTypeById(state.rules, workerTypeId);
    if (workerType == nullptr || !workerType->slaved) {
        return false;
    }

    const int wantedWorkers = runtimeSlaveMinerGatherBatchSize(building);
    int& regenTicks = state.slaveMinerRegenTicks[refineryId];
    if (regenTicks > 0) {
        --regenTicks;
    }

    int activeWorkers = 0;
    for (const auto& [workerId, task] : state.slaveMinerTasks) {
        if (task.refineryId != refineryId) {
            continue;
        }
        const WorldObjectInstance* worker = findRuntimeObjectByIdIncludingInactive(state, workerId);
        if (worker != nullptr
            && runtimeObjectIsSlaveMinerWorker(state, *worker, workerTypeId)) {
            ++activeWorkers;
        }
    }
    bool changed = false;
    if (activeWorkers < wantedWorkers && regenTicks > 0) {
        return false;
    }
    if (!runtimeObjectIsMobileSlaveMiner(state, refinery)
        && refinery.buildAnimTicksRemaining > 0) return false;

    while (activeWorkers < wantedWorkers) {
        const WorldObjectInstance* currentRefinery =
            findObjectById(state, refineryId);
        if (currentRefinery == nullptr || !currentRefinery->alive) {
            break;
        }
        const WorldObjectInstance parentSnapshot = *currentRefinery;
        const bool mobile = runtimeObjectIsMobileSlaveMiner(state, parentSnapshot);
        const std::optional<RuntimeCellPoint> spawnCell = mobile
            ? std::optional<RuntimeCellPoint>(runtimeCellPointForObject(parentSnapshot))
            : findRuntimeSlaveMinerWorkerSpawnCell(state, parentSnapshot, workerTypeId);
        if (!spawnCell.has_value()) {
            break;
        }

        WorldObjectInstance* worker = addRuntimeObjectInstanceForOwner(
            state,
            refineryOwner,
            RuntimeBuildKind::Infantry,
            workerTypeId,
            *spawnCell,
            std::nullopt,
            true,
            mobile,
            mobile ? -1 : refineryId
        );
        if (worker == nullptr) {
            break;
        }
        worker->mission = "Harvest";
        worker->facing = refineryFacing;
        RuntimeSlaveMinerTask task;
        task.refineryId = refineryId;
        task.phase = mobile ? RuntimeSlaveMinerPhase::Stored : RuntimeSlaveMinerPhase::SeekingOre;
        task.status = mobile ? "regenerated in mobile miner" : "spawned";
        state.slaveMinerTasks[worker->instanceId] = std::move(task);
        if (mobile) synchronizeRuntimeStoredSlaveMinerWorker(state, *worker, parentSnapshot);
        ++activeWorkers;
        changed = true;
    }

    if (activeWorkers >= wantedWorkers) {
        state.slaveMinerRegenTicks.erase(refineryId);
    }
    if (changed) {
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
    }
    return changed;
}

int releaseRuntimeSlaveMinerWorkersForRefinery(
    RuntimeWindowState& state,
    int refineryId,
    const std::string& reason,
    const RuntimeObjectExitRequest* request = nullptr
) {
    int released = 0;
    state.slaveMinerRegenTicks.erase(refineryId);
    std::optional<WorldObjectInstance> parentSnapshot;
    if (const auto* parent = findRuntimeObjectByIdIncludingInactive(state, refineryId))
        parentSnapshot = *parent;
    std::vector<int> workerIds;
    for (const auto& [workerId, task] : state.slaveMinerTasks)
        if (task.refineryId == refineryId) workerIds.push_back(workerId);
    std::sort(workerIds.begin(), workerIds.end());
    for (const int workerId : workerIds) {
        auto* worker = findMutableRuntimeObjectByIdIncludingInactive(state, workerId);
        const bool stored = worker && runtimeSlaveMinerWorkerIsStored(state, *worker);
        if (!worker || worker->health <= 0 || (!worker->alive && !stored)) {
            state.slaveMinerTasks.erase(workerId);
            continue;
        }
        // BridgeCollapsed is the existing collapse mission carried by the
        // public exit request. A hidden occupant cannot escape that hazard.
        const bool collapsed = stored && request
            && request->kind == RuntimeObjectExitKind::Destroyed
            && request->mission == "BridgeCollapsed";
        std::optional<RuntimeCellPoint> exitCell;
        if (stored && !collapsed) exitCell = findRuntimeSlaveMinerWorkerExitCell(
            state, parentSnapshot ? *parentSnapshot : *worker, worker->type);
        if (stored && (collapsed || !exitCell)) {
            RuntimeObjectExitRequest exit;
            exit.objectId = workerId;
            exit.kind = RuntimeObjectExitKind::Destroyed;
            exit.tick = state.tick;
            exit.mission = collapsed ? "BridgeCollapsed" : "Destroyed";
            exit.reason = collapsed ? "stored slave lost with collapsed bridge"
                : "stored slave has no legal refinery escape";
            exit.wasAliveBeforeDamage = true;
            exit.suppressCrew = true;
            if (request) {
                exit.sourceObjectId = request->sourceObjectId;
                exit.sourceOwner = request->sourceOwner;
                exit.deathWeaponCause = request->deathWeaponCause;
                exit.warheadId = request->warheadId;
                exit.unitDeathSoundPosition = request->unitDeathSoundPosition;
                // A failed escape inherited from removal or sale is cleanup,
                // and keeps the existing quiet dependent-exit policy.
                exit.unitDeathSoundHandled = request->kind != RuntimeObjectExitKind::Destroyed;
                exit.suppressDeathWeapon = request->suppressDeathWeapon
                    || request->kind != RuntimeObjectExitKind::Destroyed;
            }
            if (!exit.unitDeathSoundPosition && parentSnapshot)
                exit.unitDeathSoundPosition = runtimeSpatialAudioPositionForObject(*parentSnapshot);
            // Queue before removing the relationship. The parent batch drains
            // this genuine death through the common once-only death path.
            (void)queueRuntimeObjectExit(state, std::move(exit));
            worker->health = 0;
            worker->alive = false;
            state.slaveMinerTasks.erase(workerId);
            continue;
        }

        const bool hadPose = worldObjectHasGroundRenderPose(*worker);
        const std::array<double, 3> position{worker->renderX, worker->renderY, worker->renderLevel};
        const bool onBridge = worker->renderOnBridge;
        eraseRuntimeBehavior(state, workerId);
        state.slaveMinerTasks.erase(workerId);
        worker->alive = true;
        worker->mission = "Guard";
        if (stored) {
            clearRuntimeRenderPosition(*worker);
            setRuntimeObjectCell(state, *worker, *exitCell);
            assignRuntimeInfantrySubcellAtCurrentLocation(state, *worker);
        } else if (hadPose) {
            worker->hasRenderPosition = true;
            worker->renderMoveActive = false;
            worker->renderX = position[0];
            worker->renderY = position[1];
            worker->renderLevel = position[2];
            worker->renderOnBridge = onBridge;
        }
        ++released;
    }

    if (released > 0) {
        state.economy.status =
            "released " + std::to_string(released)
            + " slave miners"
            + (reason.empty() ? "" : " " + reason);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        emitRuntimeSound(state, "SlavesFreed");
    }
    return released;
}

void cleanRuntimeSlaveMinerTasks(RuntimeWindowState& state) {
    const std::string currentOwner =
        runtimeCanonicalHumanHouseOwner(state.economy.owner);
    std::unordered_set<int> lostRefineryIds;
    for (auto taskIt = state.slaveMinerTasks.begin();
        taskIt != state.slaveMinerTasks.end();) {
        const WorldObjectInstance* worker = findRuntimeObjectByIdIncludingInactive(state, taskIt->first);
        const WorldObjectInstance* refinery =
            findObjectById(state, taskIt->second.refineryId);
        const bool workerOwned = worker != nullptr
            && runtimeCanonicalHumanHouseOwner(worker->owner) == currentOwner;
        const bool refineryOwned = refinery != nullptr
            && runtimeCanonicalHumanHouseOwner(refinery->owner) == currentOwner;
        if (!workerOwned && !refineryOwned) {
            if (worker == nullptr && refinery == nullptr) {
                taskIt = state.slaveMinerTasks.erase(taskIt);
            } else {
                ++taskIt;
            }
            continue;
        }
        if (worker == nullptr || worker->health <= 0
            || (!worker->alive && !runtimeSlaveMinerWorkerIsStored(state, *worker))) {
            if (refinery != nullptr && refinery->alive) {
                if (const BuildingType* building = runtimeSlaveMinerParentBuilding(state, *refinery)) {
                    int& regenTicks =
                        state.slaveMinerRegenTicks[refinery->instanceId];
                    if (regenTicks <= 0) {
                        regenTicks =
                            runtimeSlaveMinerWorkerRegenTicks(state, *building);
                    }
                }
            }
            taskIt = state.slaveMinerTasks.erase(taskIt);
        } else if (refinery == nullptr
            || !refinery->alive
            || !runtimeSlaveMinerParentBuilding(state, *refinery)
            || runtimeCanonicalHumanHouseOwner(worker->owner)
                != runtimeCanonicalHumanHouseOwner(refinery->owner)) {
            lostRefineryIds.insert(taskIt->second.refineryId);
            ++taskIt;
        } else {
            ++taskIt;
        }
    }
    for (int refineryId : lostRefineryIds) {
        releaseRuntimeSlaveMinerWorkersForRefinery(
            state,
            refineryId,
            "refinery gone"
        );
    }
    if (!lostRefineryIds.empty() && !state.drainingObjectExits)
        (void)drainRuntimeObjectExitQueue(state);
}

bool runtimeSlaveMinerWorkerAtRefinery(
    const RuntimeWindowState& state,
    const WorldObjectInstance& worker,
    const WorldObjectInstance& refinery
) {
    // A committed navigation cell is ahead of the walking body. Both bodies
    // must have finished their real segments before cargo can change hands.
    if (worker.renderMoveActive || refinery.renderMoveActive
        || worker.level != refinery.level || worker.onBridge != refinery.onBridge)
        return false;
    const auto bodyCell = [](const WorldObjectInstance& object) {
        return worldObjectHasGroundRenderPose(object)
            ? RuntimeCellPoint{static_cast<int>(std::lround(object.renderX)),
                static_cast<int>(std::lround(object.renderY))}
            : RuntimeCellPoint{object.x, object.y};
    };
    if (runtimeObjectIsMobileSlaveMiner(state, refinery))
        return runtimeCellPointForObject(worker) == runtimeCellPointForObject(refinery)
            && bodyCell(worker) == bodyCell(refinery);
    return runtimeCellDistanceToObjectFootprint(
            state, runtimeCellPointForObject(worker), refinery) == 0.0
        && runtimeCellDistanceToObjectFootprint(state, bodyCell(worker), refinery) == 0.0;
}

bool issueRuntimeSlaveMinerReturn(
    RuntimeWindowState& state,
    WorldObjectInstance& worker,
    const WorldObjectInstance& refinery
) {
    worker.mission = "Return";
    const auto entry = runtimeObjectIsMobileSlaveMiner(state, refinery)
        ? std::optional<RuntimeCellPoint>(runtimeCellPointForObject(refinery))
        : findRuntimeEngineerEntryCell(state, worker, refinery, "slave-delivery", 2);
    if (entry && issueRuntimeHarvesterMoveToCell(
            state, worker, *entry, "Return", nullptr, refinery.instanceId, true)) return true;

    // A blocked return still replaces an old attack/entry order. Finish any
    // already committed walking segment instead of snapping the body forward.
    std::optional<RuntimeObjectBehavior> continuation;
    if (const auto old = state.behaviors.find(worker.instanceId);
        old != state.behaviors.end() && old->second.moveCooldownTicks > 0)
        continuation = old->second;
    eraseRuntimeBehavior(state, worker.instanceId);
    if (continuation) {
        RuntimeObjectBehavior waiting;
        waiting.kind = RuntimeBehaviorKind::Move;
        waiting.state = RuntimeBehaviorState::Queued;
        waiting.moveTarget = runtimeCellPointForObject(worker);
        waiting.weaponCooldownTicks = continuation->weaponCooldownTicks;
        inheritRuntimeVehicleMoveSegment(worker, waiting, *continuation);
        state.behaviors[worker.instanceId] = std::move(waiting);
    }
    worker.mission = "Return";
    return false;
}

bool advanceRuntimeSlaveMinerWorkerTask(
    RuntimeWindowState& state,
    WorldObjectInstance& worker,
    RuntimeSlaveMinerTask& task
) {
    WorldObjectInstance* refinery =
        findMutableObjectById(state, task.refineryId);
    if (refinery == nullptr || !refinery->alive) {
        return false;
    }

    const bool mobile = runtimeObjectIsMobileSlaveMiner(state, *refinery);
    const BuildingType* building = runtimeSlaveMinerParentBuilding(state, *refinery);
    const UnitType* workerType = findRuntimeUnitType(state.rules, worker);
    if (building == nullptr || workerType == nullptr || !workerType->slaved) {
        return false;
    }

    if (task.phase == RuntimeSlaveMinerPhase::Stored) {
        if (mobile) {
            synchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
            return true;
        }
        if (refinery->buildAnimTicksRemaining > 0) return false;
        const auto spawn = findRuntimeSlaveMinerWorkerSpawnCell(state, *refinery, worker.type);
        if (!spawn) return false;
        clearRuntimeRenderPosition(worker);
        worker.alive = true;
        worker.mission = "Harvest";
        setRuntimeObjectCell(state, worker, *spawn);
        assignRuntimeInfantrySubcellAtCurrentLocation(state, worker);
        task.phase = task.cargoUnits > 0
            ? RuntimeSlaveMinerPhase::Returning : RuntimeSlaveMinerPhase::SeekingOre;
        task.status = "released after deployment";
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        return true;
    }
    if (!worker.alive || worker.health <= 0 || worker.harvestAutoSearchSuspended)
        return false;

    const int cargoCapacity = runtimeSlaveMinerWorkerCapacity(*workerType);
    task.cargoUnits = std::clamp(task.cargoUnits, 0, cargoCapacity);
    task.cargoGemUnits =
        std::clamp(task.cargoGemUnits, 0, task.cargoUnits);
    task.cargoValue = std::max(0, task.cargoValue);

    if (const auto behavior = state.behaviors.find(worker.instanceId);
        behavior != state.behaviors.end()) {
        if (mobile && task.phase == RuntimeSlaveMinerPhase::Returning
            && behavior->second.kind == RuntimeBehaviorKind::Move
            && (behavior->second.pendingAllowedOccupiedObjectId != refinery->instanceId
                || behavior->second.moveTarget != runtimeCellPointForObject(*refinery))) {
            // The home can move after the worker committed its old approach.
            // Reuse the return order to retain that segment and follow the home.
            return issueRuntimeSlaveMinerReturn(state, worker, *refinery);
        }
        return false;
    }

    if (task.phase == RuntimeSlaveMinerPhase::Returning
        && runtimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)) {
        if (!mobile && refinery->buildAnimTicksRemaining > 0) return false;
        if (task.cargoValue <= 0) {
            task.cargoUnits = task.cargoGemUnits = 0;
            task.workTicksRemaining = 0;
            if (mobile) {
                eraseRuntimeBehavior(state, worker.instanceId);
                task.phase = RuntimeSlaveMinerPhase::Stored;
                task.targetOre = {-1, -1};
                task.status = "returned empty to mobile miner";
                synchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
                refreshRuntimeObjectOccupancyAfterLocalChange(state);
            } else {
                task.phase = RuntimeSlaveMinerPhase::SeekingOre;
                task.status = "returned empty";
                worker.mission = "Harvest";
            }
            return true;
        }
        int purifierCount = 0;
        const int purifierBonus = runtimeOrePurifierBonusValue(
            state,
            task.cargoValue,
            &purifierCount
        );
        const int deliveredValue = task.cargoValue + purifierBonus;
        depositRuntimeCredits(state, deliveredValue);
        recordRuntimeResourceDeliveryToStorage(
            state,
            task.cargoUnits,
            task.cargoGemUnits,
            deliveredValue
        );
        ++state.economy.resourceTrips;
        state.economy.lastResourceBaseValue = task.cargoValue;
        state.economy.lastResourcePurifierBonus = purifierBonus;
        state.economy.lastResourcePurifierCount = purifierCount;
        if (!mobile) {
            spawnRuntimeRefinerySmokeEffects(state, *refinery, worker);
            refinery->mission = "Harvest";
        }
        worker.mission = "Harvest";
        state.economy.status =
            worker.type + " delivered $"
            + std::to_string(deliveredValue)
            + " to " + refinery->type
            + " credits=" + std::to_string(state.economy.credits);
        task.cargoValue = 0;
        task.cargoUnits = 0;
        task.cargoGemUnits = 0;
        task.workTicksRemaining = 0;
        task.phase = mobile ? RuntimeSlaveMinerPhase::Stored : RuntimeSlaveMinerPhase::SeekingOre;
        task.status = mobile ? "delivered and stored" : "delivered";
        if (mobile) {
            task.targetOre = {-1, -1};
            synchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
            refreshRuntimeObjectOccupancyAfterLocalChange(state);
        }
        return true;
    }

    const bool cargoFull = task.cargoUnits >= cargoCapacity;
    if (cargoFull || task.phase == RuntimeSlaveMinerPhase::Returning) {
        task.phase = RuntimeSlaveMinerPhase::Returning;
        task.status = "returning";
        return issueRuntimeSlaveMinerReturn(state, worker, *refinery);
    }

    const auto targetStillHasOre =
        [&state](const RuntimeCellPoint& cell) {
            return cell.x >= 0
                && cell.y >= 0
                && runtimeCellHasHarvestableResource(state, cell);
        };

    bool acquiredNewTarget = false;
    if (!targetStillHasOre(task.targetOre)) {
        const std::optional<RuntimeCellPoint> resourceCell =
            findNearestRuntimeStructureResourceCell(
                state,
                *refinery,
                runtimeSlaveMinerScanRadius(state), worker
            );
        if (!resourceCell.has_value()) {
            if (task.cargoValue > 0) {
                task.phase = RuntimeSlaveMinerPhase::Returning;
                task.status = "returning partial load: no ore";
                return issueRuntimeSlaveMinerReturn(state, worker, *refinery);
            }
            worker.mission = "Guard";
            task.phase = RuntimeSlaveMinerPhase::SeekingOre;
            task.status = "no ore";
            if (runtimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)) {
                const auto exit = findRuntimeSlaveMinerWorkerExitCell(state, *refinery, worker.type);
                if (exit.has_value()) return issueRuntimeHarvesterMoveToCell(
                    state, worker, *exit, "Move", nullptr, refinery->instanceId, true);
            }
            return false;
        }
        task.targetOre = *resourceCell;
        task.phase = RuntimeSlaveMinerPhase::MovingToOre;
        acquiredNewTarget = true;
    }

    const RuntimeCellPoint workerCell = runtimeCellPointForObject(worker);
    if (runtimeCellDistanceToObjectFootprint(state, workerCell, *refinery) == 0.0) {
        // Unloading permission is not a mining position. Retain the original
        // ore goal while leaving the foundation; native path/subcell checks
        // may wait for other workers, but must not finish at the home cell or
        // replace this departure with a vehicle's adjacent intake approach.
        task.phase = RuntimeSlaveMinerPhase::MovingToOre;
        task.status = "leaving refinery for ore";
        RuntimeObjectBehavior departure;
        departure.kind = RuntimeBehaviorKind::Move;
        departure.state = RuntimeBehaviorState::Queued;
        departure.moveTarget = task.targetOre;
        eraseRuntimeBehavior(state, worker.instanceId);
        worker.mission = "Harvest";
        state.behaviors[worker.instanceId] = std::move(departure);
        auto& queued = state.behaviors.at(worker.instanceId);
        submitRuntimeMovePathRequest(state, worker, queued, workerCell,
            queued.moveTarget, refinery->instanceId, nullptr, false);
        return true;
    }
    if (!runtimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)
        && RuntimeHarvesterResourceAccess(state, worker).canGatherFrom(workerCell, task.targetOre)) {
        if (acquiredNewTarget || task.phase == RuntimeSlaveMinerPhase::MovingToOre
            || task.phase == RuntimeSlaveMinerPhase::SeekingOre) {
            task.workTicksRemaining =
                runtimeSlaveMinerWorkerHarvestTicks(
                    state,
                    *workerType,
                    *building
                );
        }
        if (!alignRuntimeHarvesterToResource(
                state,
                worker,
                task.targetOre
            )) {
            worker.mission = "HarvestAlign";
            task.phase = RuntimeSlaveMinerPhase::Harvesting;
            task.status = "aligning to ore";
            return true;
        }
        if (task.workTicksRemaining > 0) {
            --task.workTicksRemaining;
            worker.mission = "HarvestLoad";
            task.phase = RuntimeSlaveMinerPhase::Harvesting;
            task.status = "digging";
            return true;
        }

        bool wasGem = false;
        const int value =
            consumeRuntimeOreAtCell(state, task.targetOre, &wasGem);
        if (value <= 0) {
            task.targetOre = RuntimeCellPoint{};
            task.phase =
                task.cargoValue > 0
                    ? RuntimeSlaveMinerPhase::Returning
                    : RuntimeSlaveMinerPhase::SeekingOre;
            task.status =
                task.cargoValue > 0
                    ? "returning partial load: ore depleted"
                    : "ore depleted";
            return true;
        }

        task.cargoValue += value;
        ++task.cargoUnits;
        if (wasGem) {
            ++task.cargoGemUnits;
        }
        ++state.economy.slaveMinerLoads;
        worker.mission = "HarvestLoad";
        task.status =
            "loaded " + std::to_string(task.cargoUnits)
            + "/" + std::to_string(cargoCapacity);
        // The worker's HarvestLoad mission plays the original Shovel strip.
        if (task.cargoUnits >= cargoCapacity) {
            task.phase = RuntimeSlaveMinerPhase::Returning;
            task.workTicksRemaining = 0;
        } else {
            task.phase = RuntimeSlaveMinerPhase::Harvesting;
            task.workTicksRemaining =
                runtimeSlaveMinerWorkerHarvestTicks(
                    state,
                    *workerType,
                    *building
                );
        }
        return true;
    }

    task.phase = RuntimeSlaveMinerPhase::MovingToOre;
    task.status =
        "moving to ore "
        + std::to_string(task.targetOre.x)
        + ","
        + std::to_string(task.targetOre.y);
    // A worker leaving the unloading point may cross its own refinery's
    // foundation, but must walk back out to the ore before digging again.
    if (issueRuntimeHarvesterMoveToCell(
            state, worker, task.targetOre, "Harvest", nullptr, refinery->instanceId, true)) return true;
    // A depleted edge or a blocked deposit must not keep the worker attached
    // to an unreachable ore cell forever. The next search checks real access.
    task.targetOre = {-1, -1};
    task.phase = RuntimeSlaveMinerPhase::SeekingOre;
    return false;
}

bool advanceRuntimeSlaveMinerEconomy(RuntimeWindowState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeSlaveMinerEconomy", state.tick);
    cleanRuntimeSlaveMinerTasks(state);

    std::vector<int> refineryIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0
            || runtimeCanonicalHumanHouseOwner(object.owner)
                != runtimeCanonicalHumanHouseOwner(state.economy.owner)) {
            continue;
        }

        const BuildingType* building = runtimeSlaveMinerParentBuilding(state, object);
        if (building != nullptr) {
            refineryIds.push_back(object.instanceId);
        }
    }

    std::sort(refineryIds.begin(), refineryIds.end());
    bool changed = false;
    for (int refineryId : refineryIds) {
        const WorldObjectInstance* refinery = findObjectById(state, refineryId);
        const BuildingType* building =
            refinery == nullptr ? nullptr : runtimeSlaveMinerParentBuilding(state, *refinery);
        if (refinery == nullptr || building == nullptr) {
            continue;
        }
        changed =
            ensureRuntimeSlaveMinerWorkers(state, *refinery, *building)
            || changed;
    }

    std::vector<int> workerIds;
    workerIds.reserve(state.slaveMinerTasks.size());
    for (const auto& [workerId, task] : state.slaveMinerTasks) {
        workerIds.push_back(workerId);
    }
    std::sort(workerIds.begin(), workerIds.end());

    for (int workerId : workerIds) {
        WorldObjectInstance* worker = findMutableRuntimeObjectByIdIncludingInactive(state, workerId);
        auto taskIt = state.slaveMinerTasks.find(workerId);
        if (worker == nullptr
            || worker->health <= 0
            || (!worker->alive && !runtimeSlaveMinerWorkerIsStored(state, *worker))
            || runtimeCanonicalHumanHouseOwner(worker->owner)
                != runtimeCanonicalHumanHouseOwner(state.economy.owner)
            || taskIt == state.slaveMinerTasks.end()) {
            continue;
        }
        changed =
            advanceRuntimeSlaveMinerWorkerTask(state, *worker, taskIt->second)
            || changed;
    }

    return changed;
}
