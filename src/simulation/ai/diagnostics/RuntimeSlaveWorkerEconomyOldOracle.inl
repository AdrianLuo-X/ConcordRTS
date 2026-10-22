bool s1702OldDrainRuntimeObjectExitQueue(RuntimeWindowState& state, RuntimeEconomyState* miningEconomy = nullptr);
bool s1702OldValidateRuntimePadAircraftProductionProjection(
    const RuntimeWindowState& state,
    std::string& errorMessage, const std::string* miningOwner = nullptr
) {
    if (!validateRuntimeAircraftPadSlotCapacity(state, errorMessage)) {
        return false;
    }

    std::map<std::pair<int, int>, int> liveDockUsers;
    std::set<std::pair<int, int>> occupiedSlots;
    if (state.repairDepotsInitialized) {
        for (const RuntimeRepairDepotRelation& relation :
                state.repairDepots.relations()) {
            if (state.repairDepots.claimsDock(
                    relation.hostId,
                    relation.dockIndex
                )) {
                occupiedSlots.emplace(
                    relation.hostId,
                    relation.dockIndex
                );
            }
        }
    }
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        const bool padAircraft = findRuntimePadAircraftUnitType(
            state.rules,
            object
        ) != nullptr;
        const bool unbound = object.dockedToObjectId == -1
            && object.dockIndex == -1;
        const bool bound = object.dockedToObjectId >= 0
            && object.dockIndex >= 0;
        if (padAircraft
            && runtimeObjectSuspendedByMagBeam(state, object.instanceId)
            && (!unbound
                || toLowerAsciiLocal(object.mission)
                    != "magbeamsuspended")) {
            errorMessage =
                "MagBeam PadAircraft has conflicting dock authority";
            return false;
        }
        if (padAircraft && !unbound && !bound) {
            errorMessage =
                "pad aircraft has a malformed dock authority pair";
            return false;
        }
        if (!object.alive || !bound) {
            continue;
        }

        const std::pair<int, int> slotKey{
            object.dockedToObjectId,
            object.dockIndex
        };
        const auto existing = liveDockUsers.find(slotKey);
        if (existing != liveDockUsers.end()) {
            const WorldObjectInstance* existingObject =
                findObjectById(state, existing->second);
            if (padAircraft
                || (existingObject != nullptr
                    && findRuntimePadAircraftUnitType(
                        state.rules,
                        *existingObject
                    ) != nullptr)) {
                errorMessage =
                    "aircraft pad is occupied by more than one live object";
                return false;
            }
        } else {
            liveDockUsers.emplace(slotKey, object.instanceId);
            occupiedSlots.insert(slotKey);
        }
        if (!padAircraft) {
            continue;
        }

        const WorldObjectInstance* host = findObjectById(
            state,
            object.dockedToObjectId
        );
        const BuildingType* building = host == nullptr
            ? nullptr
            : findRuntimeBuildingTypeById(state.rules, host->type);
        if (host == nullptr
            || building == nullptr
            || object.dockIndex >= building->numberOfDocks
            || !runtimeProductionOwnersMatch(
                state,
                object.owner,
                host->owner
            )
            || !runtimeAircraftDockAssignmentCompatible(
                state,
                object,
                *host
            )) {
            errorMessage =
                "pad aircraft references an invalid or incompatible host";
            return false;
        }
        if (state.repairDepotsInitialized
            && state.repairDepots.claimsDock(
                host->instanceId,
                object.dockIndex
            )) {
            errorMessage =
                "pad aircraft conflicts with repair dock authority";
            return false;
        }
    }

    std::vector<const WorldObjectInstance*> unboundPadAircraft;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.alive
            && object.dockedToObjectId == -1
            && object.dockIndex == -1
            && !runtimeObjectSuspendedByMagBeam(
                state,
                object.instanceId
            )
            && findRuntimePadAircraftUnitType(
                state.rules,
                object
            ) != nullptr) {
            unboundPadAircraft.push_back(&object);
        }
    }
    std::sort(
        unboundPadAircraft.begin(),
        unboundPadAircraft.end(),
        [](const WorldObjectInstance* left,
           const WorldObjectInstance* right) {
            return left != nullptr && right != nullptr
                ? left->instanceId < right->instanceId
                : right != nullptr;
        }
    );
    for (const WorldObjectInstance* aircraft : unboundPadAircraft) {
        if (aircraft == nullptr) {
            continue;
        }
        bool wouldBeAssigned = false;
        for (const WorldObjectInstance& host : state.world.getObjects()) {
            if (!runtimeAircraftDockAssignmentCompatible(
                    state,
                    *aircraft,
                    host
                )) {
                continue;
            }
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, host.type);
            const int dockCount = building == nullptr
                ? 0
                : std::max(0, building->numberOfDocks);
            for (int dockIndex = 0; dockIndex < dockCount; ++dockIndex) {
                if (occupiedSlots.insert({
                        host.instanceId,
                        dockIndex
                    }).second) {
                    wouldBeAssigned = true;
                    break;
                }
            }
            if (wouldBeAssigned) {
                break;
            }
        }
        if (wouldBeAssigned) {
            errorMessage =
                "unbound pad aircraft would be reassigned after load";
            return false;
        }
    }

    const RuntimeAircraftProductionMatchResult matched = [&] {
        if (!miningOwner) return runtimeAircraftProductionMatchForOwner(state, state.economy.owner);
        // The old mining facade has no production catalog or pending orders.
        auto input = buildRuntimeAircraftProductionMatchInputForOwner(
            state, *miningOwner, std::nullopt, nullptr, false);
        return input.error ? std::move(*input.error) : runtimeMatchAircraftProductionDocks(input.input);
    }();
    if (!matched.inputValid || !matched.feasible) {
        errorMessage = matched.reason.empty()
            ? "saved pad aircraft production is not feasible"
            : "pad aircraft production is not feasible: " + matched.reason;
        return false;
    }
    return true;
}

std::optional<RuntimeCellPoint> s1702OldFindNearestRuntimeStructureResourceCell(
    const RuntimeWindowState& state,
    const WorldObjectInstance& structure,
    int maxRadius,
    const WorldObjectInstance& worker
) {
    RuntimeHarvesterResourceAccess access(state, worker);
    const RuntimeCellPoint origin = runtimeCellPointForObject(structure);
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, structure);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const std::vector<RuntimeCellPoint> footprintCells =
        runtimeStructureFootprintCells(origin, footprint);
    const int radiusLimit = std::max(1, maxRadius);

    struct ResourceCandidate {
        RuntimeCellPoint cell;
        long long distanceScore = 0;
        int value = 0;
        int density = 0;
    };

    for (int radius = 0; radius <= radiusLimit; ++radius) {
        std::vector<ResourceCandidate> candidates;
        for (int y = origin.y - radius;
            y <= origin.y + height - 1 + radius;
            ++y) {
            for (int x = origin.x - radius;
                x <= origin.x + width - 1 + radius;
                ++x) {
                const bool onBorder =
                    radius == 0
                    || x == origin.x - radius
                    || x == origin.x + width - 1 + radius
                    || y == origin.y - radius
                    || y == origin.y + height - 1 + radius;
                if (!onBorder) {
                    continue;
                }

                const RuntimeCellPoint candidateCell{x, y};
                const TerrainCell* terrain =
                    findRuntimeTerrainCellAt(state, candidateCell);
                if (terrain == nullptr
                    || !terrain->hasOverlay
                    || (!terrain->overlayIsOre && !terrain->overlayIsGem)) {
                    continue;
                }

                candidates.push_back(ResourceCandidate{
                    candidateCell,
                    runtimeSquaredDistanceToCells(
                        candidateCell,
                        footprintCells
                    ),
                    runtimeResourceValueForCell(state, *terrain),
                    runtimeResourceDensityForTerrain(*terrain)
                });
            }
        }

        if (candidates.empty()) {
            continue;
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const ResourceCandidate& left,
                const ResourceCandidate& right) {
                if (left.distanceScore != right.distanceScore) {
                    return left.distanceScore < right.distanceScore;
                }
                if (left.value != right.value) {
                    return left.value > right.value;
                }
                if (left.density != right.density) {
                    return left.density > right.density;
                }
                return RuntimeCellPointLess{}(left.cell, right.cell);
            }
        );
        for (const auto& candidate : candidates)
            if (findRuntimeHarvesterResourceApproach(access, candidate.cell)) return candidate.cell;
    }

    return std::nullopt;
}

// Workers in a mobile miner retain their identity and health, but do not
// occupy a ground subcell or participate as independent active units.
void s1702OldSynchronizeRuntimeStoredSlaveMinerWorker(
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

const BuildingType* s1702OldRuntimeSlaveMinerParentBuilding(
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

bool s1702OldRuntimeSlaveMinerWorkerIsStored(
    const RuntimeWindowState& state, const WorldObjectInstance& worker
) {
    const auto task = state.slaveMinerTasks.find(worker.instanceId);
    return task != state.slaveMinerTasks.end()
        && task->second.phase == RuntimeSlaveMinerPhase::Stored
        && !worker.alive && worker.health > 0 && worker.mission == "InsideSlaveMiner";
}

void s1702OldSynchronizeRuntimeStoredSlaveMinerWorkers(RuntimeWindowState& state) {
    for (auto& worker : state.world.accessObjects()) {
        if (!s1702OldRuntimeSlaveMinerWorkerIsStored(state, worker)) continue;
        const auto& task = state.slaveMinerTasks.at(worker.instanceId);
        const auto* refinery = findObjectById(state, task.refineryId);
        if (!refinery || !refinery->alive || refinery->health <= 0
            || !s1702OldRuntimeSlaveMinerParentBuilding(state, *refinery)
            || runtimeCanonicalHumanHouseOwner(worker.owner)
                != runtimeCanonicalHumanHouseOwner(refinery->owner)) continue;
        s1702OldSynchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
    }
}

int s1702OldRuntimeSlaveMinerWorkerCapacity(const UnitType& worker) {
    return std::max(1, worker.storage > 0 ? worker.storage : 4);
}

int s1702OldRuntimeSlaveMinerWorkerHarvestTicks(
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

int s1702OldRuntimeSlaveMinerWorkerRegenTicks(
    const RuntimeWindowState& state, const BuildingType& building
) {
    return runtimeRuleFramesToTicks(building.slaveRegenRate > 0
        ? building.slaveRegenRate : 500, state.config.fixedTickHz);
}

bool s1702OldRuntimeObjectIsSlaveMinerWorker(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object,
    const std::string& workerTypeId
) {
    if ((!object.alive && !s1702OldRuntimeSlaveMinerWorkerIsStored(state, object))
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

std::optional<RuntimeCellPoint> s1702OldFindRuntimeSlaveMinerWorkerSpawnCell(
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

std::optional<RuntimeCellPoint> s1702OldFindRuntimeSlaveMinerWorkerExitCell(
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

bool s1702OldEnsureRuntimeSlaveMinerWorkers(
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
            && s1702OldRuntimeObjectIsSlaveMinerWorker(state, *worker, workerTypeId)) {
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
            : s1702OldFindRuntimeSlaveMinerWorkerSpawnCell(state, parentSnapshot, workerTypeId);
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
        if (mobile) s1702OldSynchronizeRuntimeStoredSlaveMinerWorker(state, *worker, parentSnapshot);
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

int s1702OldReleaseRuntimeSlaveMinerWorkersForRefinery(
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
        const bool stored = worker && s1702OldRuntimeSlaveMinerWorkerIsStored(state, *worker);
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
        if (stored && !collapsed) exitCell = s1702OldFindRuntimeSlaveMinerWorkerExitCell(
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

void s1702OldCleanRuntimeSlaveMinerTasks(RuntimeWindowState& state) {
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
            || (!worker->alive && !s1702OldRuntimeSlaveMinerWorkerIsStored(state, *worker))) {
            if (refinery != nullptr && refinery->alive) {
                if (const BuildingType* building = s1702OldRuntimeSlaveMinerParentBuilding(state, *refinery)) {
                    int& regenTicks =
                        state.slaveMinerRegenTicks[refinery->instanceId];
                    if (regenTicks <= 0) {
                        regenTicks =
                            s1702OldRuntimeSlaveMinerWorkerRegenTicks(state, *building);
                    }
                }
            }
            taskIt = state.slaveMinerTasks.erase(taskIt);
        } else if (refinery == nullptr
            || !refinery->alive
            || !s1702OldRuntimeSlaveMinerParentBuilding(state, *refinery)
            || runtimeCanonicalHumanHouseOwner(worker->owner)
                != runtimeCanonicalHumanHouseOwner(refinery->owner)) {
            lostRefineryIds.insert(taskIt->second.refineryId);
            ++taskIt;
        } else {
            ++taskIt;
        }
    }
    for (int refineryId : lostRefineryIds) {
        s1702OldReleaseRuntimeSlaveMinerWorkersForRefinery(
            state,
            refineryId,
            "refinery gone"
        );
    }
    if (!lostRefineryIds.empty() && !state.drainingObjectExits)
        (void)s1702OldDrainRuntimeObjectExitQueue(state);
}

bool s1702OldRuntimeSlaveMinerWorkerAtRefinery(
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

bool s1702OldIssueRuntimeSlaveMinerReturn(
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

bool s1702OldAdvanceRuntimeSlaveMinerWorkerTask(
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
    const BuildingType* building = s1702OldRuntimeSlaveMinerParentBuilding(state, *refinery);
    const UnitType* workerType = findRuntimeUnitType(state.rules, worker);
    if (building == nullptr || workerType == nullptr || !workerType->slaved) {
        return false;
    }

    if (task.phase == RuntimeSlaveMinerPhase::Stored) {
        if (mobile) {
            s1702OldSynchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
            return true;
        }
        if (refinery->buildAnimTicksRemaining > 0) return false;
        const auto spawn = s1702OldFindRuntimeSlaveMinerWorkerSpawnCell(state, *refinery, worker.type);
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

    const int cargoCapacity = s1702OldRuntimeSlaveMinerWorkerCapacity(*workerType);
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
            return s1702OldIssueRuntimeSlaveMinerReturn(state, worker, *refinery);
        }
        return false;
    }

    if (task.phase == RuntimeSlaveMinerPhase::Returning
        && s1702OldRuntimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)) {
        if (!mobile && refinery->buildAnimTicksRemaining > 0) return false;
        if (task.cargoValue <= 0) {
            task.cargoUnits = task.cargoGemUnits = 0;
            task.workTicksRemaining = 0;
            if (mobile) {
                eraseRuntimeBehavior(state, worker.instanceId);
                task.phase = RuntimeSlaveMinerPhase::Stored;
                task.targetOre = {-1, -1};
                task.status = "returned empty to mobile miner";
                s1702OldSynchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
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
            s1702OldSynchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
            refreshRuntimeObjectOccupancyAfterLocalChange(state);
        }
        return true;
    }

    const bool cargoFull = task.cargoUnits >= cargoCapacity;
    if (cargoFull || task.phase == RuntimeSlaveMinerPhase::Returning) {
        task.phase = RuntimeSlaveMinerPhase::Returning;
        task.status = "returning";
        return s1702OldIssueRuntimeSlaveMinerReturn(state, worker, *refinery);
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
            s1702OldFindNearestRuntimeStructureResourceCell(
                state,
                *refinery,
                runtimeSlaveMinerScanRadius(state), worker
            );
        if (!resourceCell.has_value()) {
            if (task.cargoValue > 0) {
                task.phase = RuntimeSlaveMinerPhase::Returning;
                task.status = "returning partial load: no ore";
                return s1702OldIssueRuntimeSlaveMinerReturn(state, worker, *refinery);
            }
            worker.mission = "Guard";
            task.phase = RuntimeSlaveMinerPhase::SeekingOre;
            task.status = "no ore";
            if (s1702OldRuntimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)) {
                const auto exit = s1702OldFindRuntimeSlaveMinerWorkerExitCell(state, *refinery, worker.type);
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
    if (!s1702OldRuntimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)
        && RuntimeHarvesterResourceAccess(state, worker).canGatherFrom(workerCell, task.targetOre)) {
        if (acquiredNewTarget || task.phase == RuntimeSlaveMinerPhase::MovingToOre
            || task.phase == RuntimeSlaveMinerPhase::SeekingOre) {
            task.workTicksRemaining =
                s1702OldRuntimeSlaveMinerWorkerHarvestTicks(
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
                s1702OldRuntimeSlaveMinerWorkerHarvestTicks(
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

bool s1702OldAdvanceRuntimeSlaveMinerEconomy(RuntimeWindowState& state) {
    RuntimePerfTraceScope trace("s1702OldAdvanceRuntimeSlaveMinerEconomy", state.tick);
    s1702OldCleanRuntimeSlaveMinerTasks(state);

    std::vector<int> refineryIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0
            || runtimeCanonicalHumanHouseOwner(object.owner)
                != runtimeCanonicalHumanHouseOwner(state.economy.owner)) {
            continue;
        }

        const BuildingType* building = s1702OldRuntimeSlaveMinerParentBuilding(state, object);
        if (building != nullptr) {
            refineryIds.push_back(object.instanceId);
        }
    }

    std::sort(refineryIds.begin(), refineryIds.end());
    bool changed = false;
    for (int refineryId : refineryIds) {
        const WorldObjectInstance* refinery = findObjectById(state, refineryId);
        const BuildingType* building =
            refinery == nullptr ? nullptr : s1702OldRuntimeSlaveMinerParentBuilding(state, *refinery);
        if (refinery == nullptr || building == nullptr) {
            continue;
        }
        changed =
            s1702OldEnsureRuntimeSlaveMinerWorkers(state, *refinery, *building)
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
            || (!worker->alive && !s1702OldRuntimeSlaveMinerWorkerIsStored(state, *worker))
            || runtimeCanonicalHumanHouseOwner(worker->owner)
                != runtimeCanonicalHumanHouseOwner(state.economy.owner)
            || taskIt == state.slaveMinerTasks.end()) {
            continue;
        }
        changed =
            s1702OldAdvanceRuntimeSlaveMinerWorkerTask(state, *worker, taskIt->second)
            || changed;
    }

    return changed;
}
RuntimeObjectExitResult s1702OldFinalizeRuntimeObjectExit(
    RuntimeWindowState& state,
    const RuntimeObjectExitRequest& request
) {
    RuntimeObjectExitResult result;
    if (state.deathWeapons.hasTriggered(request.objectId)) {
        result.duplicate = true;
        result.deathWeapon.veto =
            RuntimeDeathWeaponVeto::ObjectAlreadyTriggered;
        return result;
    }

    WorldObjectInstance* object =
        findMutableRuntimeObjectByIdIncludingInactive(
            state,
            request.objectId
        );
    if (object == nullptr) {
        result.deathWeapon.veto = RuntimeDeathWeaponVeto::ObjectMissing;
        return result;
    }

    std::optional<RuntimeSpecialMechanismState> parasiteExitCandidate;
    std::vector<SpecialMechanismAction> parasiteExitActions;
    int parasiteExitSourceObjectId = -1;
    ensureRuntimeSpecialMechanismsInitialized(state);
    auto unitDeathSoundPosition = request.unitDeathSoundPosition;
    if (request.kind == RuntimeObjectExitKind::Destroyed && !unitDeathSoundPosition) {
        // A limbo launch retains its old object cell; its live actor is the
        // actual body until contact, including a script killing it in flight.
        for (const auto& [actorId, pending] : state.specialMechanisms.pendingParasiteLaunches()) {
            if (pending.sourceObjectId != request.objectId) continue;
            const auto actor = std::find_if(state.projectiles.begin(), state.projectiles.end(),
                [&](const RuntimeProjectileEffect& projectile) {
                    return projectile.actorId == actorId
                        && projectile.attackerObjectId == request.objectId
                        && runtimeProjectileIsParasiteLeap(state, projectile);
                });
            if (actor != state.projectiles.end()) {
                unitDeathSoundPosition = RuntimeSpatialAudioPosition{
                    actor->currentX, actor->currentY, actor->currentZ};
            }
            break;
        }
    }
    const auto parasiteLink = state.specialMechanisms.parasiteLinks().find(
        request.objectId
    );
    if (parasiteLink != state.specialMechanisms.parasiteLinks().end()) {
        parasiteExitSourceObjectId = parasiteLink->second.sourceObjectId;
        parasiteExitCandidate = state.specialMechanisms;
        ParasiteHostEventRequest hostExit;
        hostExit.currentTick = std::max(0, request.tick);
        hostExit.targetObjectId = request.objectId;
        hostExit.event = ParasiteHostEventKind::DestroyedExternally;
        const ParasiteOperationResult handled =
            parasiteExitCandidate->handleParasiteHostEvent(
                hostExit,
                SpecialEventSink{}
            );
        parasiteExitActions = prepareRuntimeParasiteActions(
            state,
            handled.actions
        );
        if (!handled.accepted
            || !validateRuntimeParasiteActionBatch(
                state,
                parasiteExitActions
            )) {
            result.deathWeapon.veto = RuntimeDeathWeaponVeto::InvalidContext;
            return result;
        }
    }

    if (object->kind == WorldObjectKind::Structure
        && (request.kind == RuntimeObjectExitKind::Destroyed
            || request.kind == RuntimeObjectExitKind::TemporalDestroyed
            || request.kind == RuntimeObjectExitKind::Sold)) {
        const RuntimeRepairDepotHostTransitionKind transitionKind =
            request.kind == RuntimeObjectExitKind::Sold
            ? RuntimeRepairDepotHostTransitionKind::Sold
            : RuntimeRepairDepotHostTransitionKind::Destroyed;
        if (!transitionRuntimeRepairDepotHost(
                state,
                request.objectId,
                transitionKind,
                request.kind == RuntimeObjectExitKind::TemporalDestroyed,
                object->owner
            )) {
            result.deathWeapon.veto = RuntimeDeathWeaponVeto::InvalidContext;
            return result;
        }
    }

    RuntimeDeathWeaponDeathEvent deathEvent;
    deathEvent.objectId = request.objectId;
    deathEvent.currentTick = std::max(0, request.tick);
    deathEvent.cause = request.deathWeaponCause;
    deathEvent.suppressFire = request.suppressDeathWeapon
        || request.kind != RuntimeObjectExitKind::Destroyed;
    result.deathWeapon = state.deathWeapons.notifyDeath(
        makeRuntimeDeathWeaponContext(state),
        deathEvent
    );
    if (!result.deathWeapon.accepted) {
        result.duplicate = result.deathWeapon.veto
            == RuntimeDeathWeaponVeto::ObjectAlreadyTriggered;
        return result;
    }
    result.accepted = true;
    const auto crewSurvivors = prepareRuntimeCrewSurvivors(state, *object, request);
    const auto deathCrate = prepareRuntimeDeathCrate(state, *object, request);

    if (parasiteExitCandidate.has_value()) {
        state.specialMechanisms = std::move(*parasiteExitCandidate);
        const RuntimeParasiteActionApplyResult parasiteExitApplied =
            applyRuntimeParasiteActionsAtomically(
                state,
                parasiteExitActions,
                request.deathWeaponCause,
                parasiteExitSourceObjectId,
                request.kind != RuntimeObjectExitKind::Destroyed
            );
        if (!parasiteExitApplied.accepted) {
            result.accepted = false;
            result.deathWeapon.veto = RuntimeDeathWeaponVeto::InvalidContext;
            return result;
        }
    }

    cancelRuntimeCombatFire(state, request.objectId);
    int incomingTransportAuthorities = 0;
    for (WorldObjectInstance& transport : state.world.accessObjects()) {
        const auto passenger = std::find(
            transport.transportPassengerIds.begin(),
            transport.transportPassengerIds.end(),
            request.objectId
        );
        if (passenger == transport.transportPassengerIds.end()) {
            continue;
        }
        ++incomingTransportAuthorities;
        transport.transportPassengerIds.erase(passenger);
        if (runtimeInfantryAbsorberType(state, transport) != nullptr)
            runtimeRestartBioReactorAnimation(transport, state.tick);
        refreshRuntimeTransportGunnerState(state, transport);
    }
    if (incomingTransportAuthorities > 1) {
        state.simulationStatus =
            "object exit repaired duplicate transport authority";
    }

    (void)state.factoryDeliveries.releaseByObject(request.objectId);
    (void)state.factoryDeliveries.releaseByHost(request.objectId);
    std::erase_if(state.pendingCloneDeliveries, [&request](const auto& pending) {
        return pending.sourceObjectId == request.objectId;
    });

    const bool destroyed = request.kind == RuntimeObjectExitKind::Destroyed
        || request.kind == RuntimeObjectExitKind::TemporalDestroyed;
    const bool structure = object->kind == WorldObjectKind::Structure;
    // A duplicate request may merge flags while the first exit kind wins.
    // That must not turn a sale/consumption/temporal cleanup into a death cue.
    const bool unitDeathSoundRequested = request.kind == RuntimeObjectExitKind::Destroyed;
    const auto containedDeathSoundPosition = unitDeathSoundPosition
        .value_or(runtimeSpatialAudioPositionForObject(*object));
    const std::vector<int> transportPassengers =
        object->transportPassengerIds;
    const std::vector<int> garrisonOccupants =
        object->garrisonOccupantIds;
    std::vector<int> carrierChildren;
    ensureRuntimeSpecialMechanismsInitialized(state);
    const auto carrierParent =
        state.specialMechanisms.carrierParents().find(request.objectId);
    if (carrierParent != state.specialMechanisms.carrierParents().end()) {
        for (const auto& [slot, childId] :
                carrierParent->second.childBySlot) {
            (void)slot;
            if (childId >= 0) {
                carrierChildren.push_back(childId);
            }
        }
    }

    std::unordered_set<int> livingDependents;
    std::unordered_set<int> hiddenDependents;
    if (unitDeathSoundRequested) {
        const auto rememberLiving = [&](const std::vector<int>& ids) {
            for (int id : ids) {
                const auto* dependent = findRuntimeObjectByIdIncludingInactive(state, id);
                // Radial damage can queue both the parent and its children
                // before any exit drains. Keep those real, newly lethal
                // callbacks distinct from cleanup of an already dead record.
                const bool pendingLethalDeath = std::any_of(
                    state.pendingObjectExits.begin(), state.pendingObjectExits.end(),
                    [&](const RuntimeObjectExitRequest& pending) {
                        return pending.objectId == id
                            && pending.kind == RuntimeObjectExitKind::Destroyed
                            && pending.wasAliveBeforeDamage;
                    });
                if (dependent != nullptr && (dependent->health > 0 || pendingLethalDeath)) {
                    livingDependents.insert(id);
                    const auto child = state.specialMechanisms.carrierChildren().find(id);
                    if (child != state.specialMechanisms.carrierChildren().end()
                        && child->second.parentObjectId == request.objectId
                        && child->second.phase == CarrierChildPhase::Docked
                        && !dependent->airborne) {
                        hiddenDependents.insert(id);
                    }
                }
            }
        };
        rememberLiving(transportPassengers);
        rememberLiving(garrisonOccupants);
        rememberLiving(carrierChildren);
    }

    if (destroyed) recordRuntimeMatchObjectDestruction(state, *object, request.sourceOwner);
    const bool recordCampaignDestruction = (state.campaignMode || state.cooperativeSession.active())
        && (request.kind == RuntimeObjectExitKind::Destroyed
            || request.kind == RuntimeObjectExitKind::TemporalDestroyed);
    if (recordCampaignDestruction) {
        state.triggers.campaignDestroyedTickByObject[request.objectId] =
            deathEvent.currentTick;
        if (!request.sourceOwner.empty()) {
            state.triggers.campaignDestroyerHouseByObject[request.objectId] =
                request.sourceOwner;
        }
        recordRuntimeCampaignObjectDestruction(
            state,
            *object,
            request.sourceOwner
        );
    }

    // Capture the visible pose before interpolation and subcells are cleared.
    // The once-only tombstone above also guards non-projectile deaths.
    if (request.kind == RuntimeObjectExitKind::Destroyed
        && !request.destructionEffectsSpawned && !structure
        && (object->alive || request.wasAliveBeforeDamage)
        && object->garrisonHostObjectId < 0) {
        if (const UnitType* unit = findRuntimeUnitTypeById(state.rules, object->type)) {
            RuntimeProjectileEffect cause;
            cause.attackerObjectId = request.sourceObjectId;
            cause.targetObjectId = object->instanceId;
            cause.owner = request.sourceOwner;
            cause.warheadId = request.warheadId;
            spawnRuntimeUnitDestructionEffects(state, *object, *unit, cause,
                runtimeProjectileTargetPosition(state, *object), 0, request.wasAliveBeforeDamage);
        }
    }
    if (structure && request.kind == RuntimeObjectExitKind::Destroyed) {
        if (const auto* building = state.rules.findBuildingType(object->type)) {
            if (!request.destructionEffectsSpawned
                && (object->alive || request.wasAliveBeforeDamage)) {
                RuntimeProjectileEffect cause;
                cause.attackerObjectId = request.sourceObjectId; cause.owner = request.sourceOwner;
                cause.warheadId = request.warheadId;
                spawnRuntimeBuildingDestructionEffects(state, *object, *building, cause,
                    runtimeProjectileTargetPosition(state, *object), 0);
                if (!building->dieSound.empty()) emitRuntimeSound(state, "sound:" + building->dieSound, runtimeSpatialAudioPositionForObject(*object), true);
            }
            object->rubble = building->leaveRubble;
        }
    }
    if (structure && request.kind == RuntimeObjectExitKind::Sold) {
        beginRuntimeStructureSaleAnimation(state, *object);
    }
    if (!structure && request.kind == RuntimeObjectExitKind::Destroyed
        && unitDeathSoundRequested && !request.unitDeathSoundHandled) {
        emitRuntimeUnitDeathSound(state, *object, unitDeathSoundPosition, true);
    }
    if (destroyed) {
        object->health = 0;
    }
    object->alive = false;
    if (structure) for (auto& cycle : state.impactEffects) {
        if (cycle.targetObjectId == request.objectId
            && (cycle.lifecycleState == "GrindingAnimation"
                || cycle.lifecycleState == "GrindingAnimationQueued")) {
            cycle.remainingTicks = 0;
        }
    }
    stopRuntimeVehicleDamageSmokeForSource(state, request.objectId);
    if (!request.mission.empty()) {
        object->mission = request.mission;
    } else {
        switch (request.kind) {
        case RuntimeObjectExitKind::Destroyed:
            object->mission = "Destroyed";
            break;
        case RuntimeObjectExitKind::TemporalDestroyed:
            object->mission = "TemporalRemoved";
            break;
        case RuntimeObjectExitKind::Sold:
            object->mission = "Sold";
            break;
        case RuntimeObjectExitKind::Consumed:
            object->mission = "Entered";
            break;
        case RuntimeObjectExitKind::TransformSource:
            object->mission = "Transformed";
            break;
        case RuntimeObjectExitKind::ScriptRemoved:
            object->mission = "Removed";
            break;
        }
    }
    (void)releaseRuntimeRepairDepotRelationsForObject(
        state,
        request.objectId,
        request.reason.empty() ? "object exited" : request.reason
    );
    s1702OldReleaseRuntimeSlaveMinerWorkersForRefinery(
        state,
        request.objectId,
        request.reason.empty() ? "refinery exited" : request.reason,
        &request
    );
    if (object->tankBunkerOccupantId >= 0) releaseRuntimeTankBunker(state, *object);
    if (object->tankBunkerHostId >= 0) {
        if (auto* host = findMutableObjectById(state, object->tankBunkerHostId)) {
            releaseRuntimeTankBunker(state, *host);
        }
        object->tankBunkerHostId = -1;
    }
    destroyRuntimeTransportPassengers(state, *object, "Destroyed");
    handleRuntimeGarrisonHostDestruction(state, request.objectId);
    releaseRuntimeAircraftDock(*object);
    const BuildingType* exitedAirport = structure
        ? findRuntimeBuildingTypeById(state.rules, object->type) : nullptr;
    const bool destroyParkedAircraft = destroyed && exitedAirport != nullptr
        && exitedAirport->numberOfDocks > 0
        && runtimeProductionFactorySupportsLane(*exitedAirport, RuntimeProductionLane::Aircraft);
    for (WorldObjectInstance& candidate : state.world.accessObjects()) {
        if (candidate.dockedToObjectId == request.objectId) {
            // A support reservation survives a sortie. Only aircraft still
            // on the ground perish with a destroyed airport; surviving
            // flyers enter the existing post-drain support recovery.
            if (destroyParkedAircraft && candidate.alive && !candidate.airborne
                && runtimeObjectMovementMode(state, candidate) == PathMovementMode::Air) {
                RuntimeObjectExitRequest parkedExit;
                parkedExit.objectId = candidate.instanceId;
                parkedExit.kind = request.kind;
                parkedExit.tick = deathEvent.currentTick;
                parkedExit.reason = "support airport destroyed";
                parkedExit.sourceObjectId = request.sourceObjectId;
                parkedExit.sourceOwner = request.sourceOwner;
                parkedExit.deathWeaponCause = request.deathWeaponCause;
                parkedExit.warheadId = request.warheadId;
                parkedExit.suppressDeathWeapon = deathEvent.suppressFire;
                parkedExit.suppressCrew = request.suppressCrew;
                parkedExit.wasAliveBeforeDamage = true;
                parkedExit.unitDeathSoundRequested = unitDeathSoundRequested
                    && candidate.health > 0;
                parkedExit.unitDeathSoundHandled = !parkedExit.unitDeathSoundRequested;
                (void)queueRuntimeObjectExit(state, std::move(parkedExit));
            }
            releaseRuntimeAircraftSupportAuthority(candidate);
        }
    }

    std::vector<int> pendingActors;
    for (const auto& [actorId, behavior] : state.behaviors) {
        if (behavior.kind != RuntimeBehaviorKind::Move
            || behavior.targetObjectId != request.objectId) {
            continue;
        }
        const WorldObjectInstance* actor = findObjectById(state, actorId);
        const UnitType* actorRules = actor == nullptr
            ? nullptr
            : state.rules.findUnitType("InfantryType", actor->type);
        if (actorRules != nullptr && actorRules->infiltrate) {
            pendingActors.push_back(actorId);
        }
    }
    for (const auto& [actorId, targetId] : state.pendingEngineerCaptures) {
        if (targetId == request.objectId) {
            pendingActors.push_back(actorId);
        }
    }
    for (const auto& [actorId, targetId] : state.pendingTransportTargets) {
        if (targetId == request.objectId) {
            pendingActors.push_back(actorId);
        }
    }
    for (const auto& [actorId, intent] : state.pendingTimedBombIntents) {
        if (intent.targetObjectId == request.objectId) {
            pendingActors.push_back(actorId);
        }
    }
    std::sort(pendingActors.begin(), pendingActors.end());
    pendingActors.erase(
        std::unique(pendingActors.begin(), pendingActors.end()),
        pendingActors.end()
    );
    for (int actorId : pendingActors) {
        WorldObjectInstance* pendingActor = findMutableObjectById(
            state,
            actorId
        );
        eraseRuntimeBehavior(state, actorId);
        if (pendingActor != nullptr && pendingActor->alive) {
            setRuntimeMissionIntent(
                state,
                *pendingActor,
                RuntimeMissionIntent::Idle,
                "entryTargetExited"
            );
            clearRuntimeRenderPosition(*pendingActor);
        }
    }
    cancelRuntimePendingOccupyForTarget(
        state,
        request.objectId,
        "objectExited"
    );
    clearRuntimeRenderPosition(*object);
    object->subCell = -1;
    eraseRuntimeBehavior(state, request.objectId);
    state.jumpjetFlights.erase(request.objectId);
    state.aircraftFlights.erase(request.objectId);
    removeRuntimeSelection(state, request.objectId);
    state.parasiteReselectAfterLimboObjectIds.erase(request.objectId);
    state.repairingStructureTicks.erase(request.objectId);
    state.harvesterTasks.erase(request.objectId);
    if (destroyed) {
        const auto workerTask = state.slaveMinerTasks.find(request.objectId);
        if (workerTask != state.slaveMinerTasks.end()) {
            const auto* parent = findObjectById(state, workerTask->second.refineryId);
            const auto* mobile = parent && parent->kind == WorldObjectKind::Unit
                ? findRuntimeUnitType(state.rules, *parent) : nullptr;
            const auto* parentRules = parent ? findRuntimeBuildingTypeById(state.rules,
                mobile ? mobile->deploysInto : parent->type) : nullptr;
            if (parentRules && parent && parent->health > 0
                && parent->owner == object->owner
                && toUpperAsciiLocal(parentRules->enslaves) == toUpperAsciiLocal(object->type)) {
                auto& wait = state.slaveMinerRegenTicks[parent->instanceId];
                if (wait <= 0) wait = s1702OldRuntimeSlaveMinerWorkerRegenTicks(state, *parentRules);
            }
        }
    }
    state.slaveMinerTasks.erase(request.objectId);
    state.slaveMinerRegenTicks.erase(request.objectId);
    state.gattlingWeaponTimers.erase(request.objectId);
    state.cloakObservedHealthByObject.erase(request.objectId);
    state.runtimeUnitFeedbackCooldownUntilTick.erase(request.objectId);

    std::vector<SpecialMechanismAction> specialActions;
    state.specialMechanisms.onObjectRemoved(
        state.world.accessObjects(),
        request.objectId,
        deathEvent.currentTick,
        runtimeSpecialEventSink(state),
        request.reason.empty() ? "object exited" : request.reason,
        &specialActions
    );
    if (state.spiesInitialized) {
        const RuntimeSpyObjectExitResult spyExit =
            state.spies.onObjectExited(
                request.objectId,
                deathEvent.currentTick
            );
        (void)applyRuntimeSpyActions(state, spyExit.actions);
    }

    std::vector<int> dependentExitIds;
    dependentExitIds.reserve(
        transportPassengers.size()
        + garrisonOccupants.size()
        + carrierChildren.size()
    );
    dependentExitIds.insert(
        dependentExitIds.end(),
        transportPassengers.begin(),
        transportPassengers.end()
    );
    dependentExitIds.insert(
        dependentExitIds.end(),
        garrisonOccupants.begin(),
        garrisonOccupants.end()
    );
    dependentExitIds.insert(
        dependentExitIds.end(),
        carrierChildren.begin(),
        carrierChildren.end()
    );
    std::sort(dependentExitIds.begin(), dependentExitIds.end());
    dependentExitIds.erase(
        std::unique(dependentExitIds.begin(), dependentExitIds.end()),
        dependentExitIds.end()
    );
    for (int dependentId : dependentExitIds) {
        const WorldObjectInstance* dependent =
            findRuntimeObjectByIdIncludingInactive(state, dependentId);
        if (dependent == nullptr || dependent->alive) {
            continue;
        }
        RuntimeObjectExitRequest dependentExit;
        dependentExit.objectId = dependentId;
        dependentExit.kind = RuntimeObjectExitKind::Destroyed;
        dependentExit.tick = deathEvent.currentTick;
        dependentExit.mission = dependent->mission;
        dependentExit.reason = "container or carrier exited";
        dependentExit.sourceObjectId = request.objectId;
        dependentExit.sourceOwner = request.sourceOwner;
        dependentExit.deathWeaponCause = request.deathWeaponCause;
        dependentExit.suppressDeathWeapon = deathEvent.suppressFire;
        dependentExit.unitDeathSoundRequested = unitDeathSoundRequested
            && livingDependents.contains(dependentId);
        // A consumed/replaced/removed host also cleans up its child records;
        // that bookkeeping keeps the original non-death sound policy.
        dependentExit.unitDeathSoundHandled = !dependentExit.unitDeathSoundRequested;
        if (dependentExit.unitDeathSoundRequested
            && (std::find(transportPassengers.begin(), transportPassengers.end(), dependentId)
                    != transportPassengers.end()
                || std::find(garrisonOccupants.begin(), garrisonOccupants.end(), dependentId)
                    != garrisonOccupants.end()
                || hiddenDependents.contains(dependentId))) {
            dependentExit.unitDeathSoundPosition = containedDeathSoundPosition;
        }
        (void)queueRuntimeObjectExit(state, std::move(dependentExit));
    }
    (void)applyRuntimeSpecialMechanismActions(
        state,
        specialActions,
        request.deathWeaponCause,
        request.objectId,
        !unitDeathSoundRequested
    );

    if (destroyed) {
        ++state.actionSummary.destroyedObjects;
    }
    state.attackApproachCache.clear();
    invalidateRuntimeTickOccupancyCache(state);
    invalidateRuntimeObjectProjectionCache(state);
    state.navigationRefreshPending = true;
    state.powerRefreshPending = state.powerRefreshPending || structure;
    if (structure) {
        refreshRuntimeWallConnectionMasks(state);
    }

    spawnRuntimeDeathCrate(state, deathCrate);
    spawnRuntimeCrewSurvivors(state, crewSurvivors);

    if (result.deathWeapon.fireRequest.has_value()) {
        (void)spawnRuntimeDeathWeaponProjectile(
            state,
            *result.deathWeapon.fireRequest
        );
    }
    return result;
}

bool s1702OldDrainRuntimeObjectExitQueue(RuntimeWindowState& state, RuntimeEconomyState* miningEconomy) {
    auto& economy = miningEconomy ? *miningEconomy : state.economy;
    if (state.drainingObjectExits) {
        return false;
    }
    RuntimePerfTraceScope stageTrace("s1702OldDrainRuntimeObjectExitQueue", state.tick);
    struct RuntimeAircraftSupportRecoverySnapshot {
        int aircraftId = -1;
        std::string canonicalOwner;
        std::string typeId;
        bool airborne = false;
        bool wasPhysicallyDocked = false;
        int priorHostId = -1;
        int priorDockIndex = -1;
    };
    std::vector<RuntimeAircraftSupportRecoverySnapshot>
        aircraftSupportBeforeExit;
    std::set<std::pair<std::string, int>> padHostsBeforeExit;
    // These snapshots are consumed only after an exit was finalized. An
    // empty input queue cannot finalize an exit during the drain below.
    // Grinding and tractor reconciliation must still run after that drain.
    if (!state.pendingObjectExits.empty() || runtimeHistoryQueryReference()) {
        aircraftSupportBeforeExit.reserve(state.world.getObjects().size());
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || object.dockedToObjectId < 0
                || object.dockIndex < 0
                || runtimeObjectSuspendedByMagBeam(state, object.instanceId)
                || findRuntimePadAircraftUnitType(state.rules, object) == nullptr) {
                continue;
            }
            aircraftSupportBeforeExit.push_back({
                object.instanceId,
                runtimeRepairDepotCanonicalOwnerId(state, object.owner),
                object.type,
                object.airborne,
                toLowerAsciiLocal(object.mission) == "dock",
                object.dockedToObjectId,
                object.dockIndex
            });
        }
        for (const WorldObjectInstance& host : state.world.getObjects()) {
            const BuildingType* building = host.kind
                    == WorldObjectKind::Structure
                ? findRuntimeBuildingTypeById(state.rules, host.type)
                : nullptr;
            // A lethal impact clamps health to zero before it queues ObjectExit.
            // Snapshot the authored pad-host identity while the object is still
            // alive instead of calling the live eligibility predicate, otherwise
            // an order-only host loss is invisible to post-drain recovery.
            if (host.alive
                && building != nullptr
                && building->numberOfDocks > 0
                && runtimeProductionFactorySupportsLane(
                    *building,
                    RuntimeProductionLane::Aircraft
                )) {
                padHostsBeforeExit.emplace(
                    runtimeRepairDepotCanonicalOwnerId(state, host.owner),
                    host.instanceId
                );
            }
        }
    }
    state.drainingObjectExits = true;
    bool changed = false;
    bool finalizedExit = false;
    for (;;) {
        bool acceptedInWave = false;
        while (!state.pendingObjectExits.empty()) {
            RuntimeObjectExitRequest request = std::move(state.pendingObjectExits.front());
            state.pendingObjectExits.pop_front();
            state.pendingObjectExitIds.erase(request.objectId);
            const RuntimeObjectExitResult result = s1702OldFinalizeRuntimeObjectExit(state, request);
            acceptedInWave = result.accepted || acceptedInWave;
            finalizedExit = result.accepted || finalizedExit;
            changed = result.accepted || changed;
        }
        if (!acceptedInWave) break;
        bool queuedBridgeBody = false;
        for (const auto& collapse : state.pendingBridgeCollapses) {
            queuedBridgeBody = queueRuntimeBridgeCollapseOccupants(state, collapse.cells,
                collapse.sourceObjectId, collapse.sourceOwner, collapse.deathWeaponCause) > 0
                || queuedBridgeBody;
        }
        if (!queuedBridgeBody) break;
    }
    state.drainingObjectExits = false;
    state.pendingBridgeCollapses.clear();
    if (!state.deferredBridgeMutationCells.empty()) {
        auto bridgeCells = std::move(state.deferredBridgeMutationCells);
        state.deferredBridgeMutationCells.clear();
        refreshRuntimeBridgeMutationState(state, bridgeCells);
        changed = true;
    }
    if (!state.applyingGrindingActions) {
        changed = reconcileRuntimeGrindingWithWorld(state) || changed;
    }
    if (!state.reconcilingMagBeams) {
        changed = reconcileRuntimeMagBeamsWithWorld(state) || changed;
    }
    if (finalizedExit) {
        // Host loss is a joint capacity transition. Fixed live occupancies
        // remain hard reservations, while only aircraft whose former host
        // disappeared in this drain may move. Match those survivors before
        // unfinished orders so a flexible aircraft can take its alternative
        // pad instead of needlessly canceling a constrained order.
        std::map<
            std::string,
            std::vector<RuntimeAircraftSupportRecoverySnapshot>
        > flexibleAircraftByOwner;
        bool padSupportAuthorityChanged = false;
        std::set<std::string> lostPadHostOwners;
        for (const auto& [owner, hostId] : padHostsBeforeExit) {
            if (findObjectById(state, hostId) == nullptr) {
                lostPadHostOwners.insert(owner);
            }
        }
        for (const RuntimeAircraftSupportRecoverySnapshot& before :
                aircraftSupportBeforeExit) {
            const WorldObjectInstance* aircraft =
                findObjectById(state, before.aircraftId);
            if (aircraft == nullptr
                || aircraft->dockedToObjectId != before.priorHostId
                || aircraft->dockIndex != before.priorDockIndex) {
                padSupportAuthorityChanged = true;
            }
            if (aircraft == nullptr
                || aircraft->dockedToObjectId != -1
                || aircraft->dockIndex != -1
                || runtimeObjectSuspendedByMagBeam(
                    state,
                    before.aircraftId
                )
                || findObjectById(state, before.priorHostId) != nullptr) {
                continue;
            }
            flexibleAircraftByOwner[before.canonicalOwner].push_back(before);
        }
        std::set<int> flexibleAircraftIds;
        for (const auto& [owner, aircraft] : flexibleAircraftByOwner) {
            (void)owner;
            for (const RuntimeAircraftSupportRecoverySnapshot& snapshot :
                    aircraft) {
                flexibleAircraftIds.insert(snapshot.aircraftId);
            }
        }
        // Capacity can also open because a repair/non-Pad dock claimant or a
        // bound aircraft exited. Include every already-unbound PadAircraft so
        // the post-drain state is canonical even when no support pair itself
        // changed during this batch.
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!object.alive
                || object.dockedToObjectId != -1
                || object.dockIndex != -1
                || runtimeObjectSuspendedByMagBeam(state, object.instanceId)
                || findRuntimePadAircraftUnitType(state.rules, object)
                    == nullptr
                || !flexibleAircraftIds.insert(object.instanceId).second) {
                continue;
            }
            RuntimeAircraftSupportRecoverySnapshot unbound;
            unbound.aircraftId = object.instanceId;
            unbound.canonicalOwner = runtimeRepairDepotCanonicalOwnerId(
                state,
                object.owner
            );
            unbound.typeId = object.type;
            unbound.airborne = object.airborne;
            flexibleAircraftByOwner[unbound.canonicalOwner].push_back(
                std::move(unbound)
            );
        }
        const std::string localCanonicalOwner =
            runtimeRepairDepotCanonicalOwnerId(
                state,
                economy.owner
            );
        bool hasLocalPadOrders = false;
        for (const RuntimeProductionLane lane : runtimeProductionLanes) {
            const RuntimeProductionLaneState& laneState =
                economy.localProductionLanes.lanes[
                    runtimeProductionLaneIndex(lane)
                ];
            hasLocalPadOrders = hasLocalPadOrders
                || std::any_of(
                    laneState.queued.begin(),
                    laneState.queued.end(),
                    [&state](const RuntimeProductionLaneOrder& order) {
                        return state.rules.isPadAircraft(order.itemKey);
                    }
                )
                || (laneState.active.has_value()
                    && state.rules.isPadAircraft(
                        laneState.active->itemKey
                    ))
                || (laneState.ready.has_value()
                    && state.rules.isPadAircraft(
                        laneState.ready->itemKey
                    ));
        }
        if (hasLocalPadOrders
            && lostPadHostOwners.contains(localCanonicalOwner)) {
            flexibleAircraftByOwner.try_emplace(localCanonicalOwner);
        }
        if (!padSupportAuthorityChanged && flexibleAircraftByOwner.empty()) {
            return changed;
        }
        std::optional<RuntimeProductionLanesState> beforeLanes;
        std::vector<std::pair<int, WorldObjectInstance>> beforeAircraft;
        std::optional<RuntimeAircraftServiceSystemState> beforeService;
        long long refund = 0;
        std::size_t canceledCount = 0U;
        bool normalized = false;
        std::string normalizationError;
        try {
            beforeLanes.emplace(economy.localProductionLanes);
            std::size_t flexibleAircraftCount = 0U;
            for (const auto& [owner, aircraft] : flexibleAircraftByOwner) {
                (void)owner;
                flexibleAircraftCount += aircraft.size();
            }
            beforeAircraft.reserve(flexibleAircraftCount);
            for (const auto& [owner, aircraft] : flexibleAircraftByOwner) {
                (void)owner;
                for (const RuntimeAircraftSupportRecoverySnapshot& snapshot :
                        aircraft) {
                    const WorldObjectInstance* current =
                        findObjectById(state, snapshot.aircraftId);
                    if (current != nullptr) {
                        beforeAircraft.emplace_back(
                            snapshot.aircraftId,
                            *current
                        );
                    }
                }
            }
            if (state.aircraftServicesInitialized) {
                beforeService.emplace(state.aircraftServices);
            }

            for (const auto& [owner, flexibleAircraft] :
                    flexibleAircraftByOwner) {
                const bool localOwner = owner
                    == runtimeRepairDepotCanonicalOwnerId(
                        state,
                        economy.owner
                    );
                RuntimeAircraftProductionMatchInputBuild inputBuild =
                    buildRuntimeAircraftProductionRecoveryInputForOwner(
                        state,
                        owner,
                        localOwner && miningEconomy == nullptr
                    );
                if (inputBuild.error.has_value()) {
                    normalizationError = inputBuild.error->reason.empty()
                        ? "invalid production authority"
                        : inputBuild.error->reason;
                    break;
                }
                RuntimeAircraftProductionRecoveryInput recoveryInput;
                recoveryInput.canonicalOwner =
                    inputBuild.input.canonicalOwner;
                recoveryInput.slots = std::move(inputBuild.input.slots);
                recoveryInput.liveOccupancies =
                    std::move(inputBuild.input.liveOccupancies);
                recoveryInput.orders = std::move(inputBuild.input.orders);
                for (const RuntimeAircraftSupportRecoverySnapshot& snapshot :
                        flexibleAircraft) {
                    RuntimeAircraftProductionFlexibleLiveAircraft aircraft;
                    aircraft.canonicalOwner = snapshot.canonicalOwner;
                    aircraft.aircraftId = snapshot.aircraftId;
                    aircraft.typeId = snapshot.typeId;
                    aircraft.airborne = snapshot.airborne;
                    if (snapshot.priorHostId >= 0
                        && snapshot.priorDockIndex >= 0) {
                        aircraft.priorSlot =
                            RuntimeAircraftProductionSlotReference{
                                snapshot.priorHostId,
                                snapshot.priorDockIndex
                            };
                    }
                    recoveryInput.flexibleLiveAircraft.push_back(
                        std::move(aircraft)
                    );
                }
                const RuntimeAircraftProductionRecoveryResult recovered =
                    runtimeRecoverAircraftProductionDocks(recoveryInput);
                if (!recovered.inputValid) {
                    normalizationError = recovered.reason.empty()
                        ? "invalid recovery authority"
                        : recovered.reason;
                    break;
                }
                if (!localOwner
                    && !recovered.rejectedOrderSerials.empty()) {
                    normalizationError =
                        "foreign recovery unexpectedly selected local orders";
                    break;
                }
                if (localOwner) {
                    for (const std::uint64_t serial :
                            recovered.rejectedOrderSerials) {
                        RuntimeProductionCancelResult canceled;
                        for (const RuntimeProductionLane lane :
                                runtimeProductionLanes) {
                            canceled = runtimeCancelProductionSerial(
                                economy.localProductionLanes,
                                lane,
                                serial
                            );
                            if (canceled.removed) {
                                break;
                            }
                        }
                        if (!canceled.removed
                            || canceled.order.serial != serial
                            || !state.rules.isPadAircraft(
                                canceled.order.itemKey
                            )) {
                            normalizationError =
                                "could not cancel the matcher-selected order #"
                                + std::to_string(serial);
                            break;
                        }
                        refund += std::max(0, canceled.refund);
                        ++canceledCount;
                    }
                }

                if (normalizationError.empty()) {
                        const auto assignmentFor = [&recovered](
                            int aircraftId
                        ) -> const RuntimeAircraftProductionLiveAssignment* {
                            const auto found = std::lower_bound(
                                recovered.liveAssignments.begin(),
                                recovered.liveAssignments.end(),
                                aircraftId,
                                [](const auto& assignment, int id) {
                                    return assignment.aircraftId < id;
                                }
                            );
                            return found != recovered.liveAssignments.end()
                                    && found->aircraftId == aircraftId
                                ? &*found
                                : nullptr;
                        };
                        bool physicallyRelocated = false;
                        for (const RuntimeAircraftSupportRecoverySnapshot&
                                snapshot : flexibleAircraft) {
                            WorldObjectInstance* aircraft =
                                findMutableObjectById(
                                    state,
                                    snapshot.aircraftId
                                );
                            const RuntimeAircraftProductionLiveAssignment*
                                assignment = assignmentFor(
                                    snapshot.aircraftId
                                );
                            if (aircraft == nullptr) {
                                normalizationError =
                                    "recovery aircraft disappeared";
                                break;
                            }
                            if (assignment == nullptr) {
                                releaseRuntimeAircraftSupportAuthority(
                                    *aircraft
                                );
                                if (snapshot.wasPhysicallyDocked) {
                                    clearRuntimeRenderPosition(*aircraft);
                                    aircraft->mission = "Guard";
                                }
                                continue;
                            }
                            const WorldObjectInstance* host = findObjectById(
                                state,
                                assignment->hostId
                            );
                            if (host == nullptr) {
                                normalizationError =
                                    "recovery assignment host disappeared";
                                break;
                            }
                            if (snapshot.wasPhysicallyDocked) {
                                physicallyRelocated =
                                    applyRuntimeAircraftDockPose(
                                        state,
                                        *aircraft,
                                        *host,
                                        assignment->dockIndex
                                    ) || physicallyRelocated;
                            } else {
                                aircraft->dockedToObjectId =
                                    assignment->hostId;
                                aircraft->dockIndex = assignment->dockIndex;
                            }
                        }
                        if (physicallyRelocated) {
                            refreshRuntimeObjectOccupancyAfterLocalChange(
                                state
                            );
                        }
                }

                if (!normalizationError.empty()) {
                    break;
                }
            }
            if (normalizationError.empty()
                && state.aircraftServicesInitialized) {
                const RuntimeAircraftServiceReconcileReport report =
                    state.aircraftServices.reconcile(
                        makeRuntimeAircraftServiceContext(state)
                    );
                if (!report.contextValid
                    || report.skippedHostsByLimit != 0U
                    || report.skippedBindingsByLimit != 0U
                    || report.duplicateDockBindings != 0U
                    || report.duplicateAircraftBindings != 0U) {
                    normalizationError =
                        "aircraft service recovery was invalid";
                }
            }
            if (normalizationError.empty()
                && !s1702OldValidateRuntimePadAircraftProductionProjection(
                    state,
                    normalizationError, miningEconomy ? &economy.owner : nullptr
                )) {
                normalizationError =
                    "recovered projection is invalid: "
                    + normalizationError;
            }
            normalized = normalizationError.empty();
        } catch (...) {
            normalizationError =
                "exception while reconciling host-loss production";
        }
        if (!normalized) {
            if (beforeLanes.has_value()) {
                economy.localProductionLanes =
                    std::move(*beforeLanes);
            }
            for (auto& [aircraftId, before] : beforeAircraft) {
                WorldObjectInstance* aircraft = findMutableObjectById(
                    state,
                    aircraftId
                );
                if (aircraft != nullptr) {
                    *aircraft = std::move(before);
                }
            }
            if (beforeService.has_value()) {
                state.aircraftServices = std::move(*beforeService);
            }
            refreshRuntimeObjectOccupancyAfterLocalChange(state);
            state.simulationStatus =
                "PadAircraft host-loss normalization failed: "
                + normalizationError;
        } else if (canceledCount > 0U) {
            economy.credits = runtimeCreditBalanceAfterDeposit(economy.credits, refund);
            economy.status =
                "canceled " + std::to_string(canceledCount)
                + " PadAircraft order(s) after support host loss";
        }
    }
    return changed;
}

// Legacy facade for the remaining slave worker consumer. Ordinary computer
// harvesters and mobile slave miners run first with HouseEconomyContext. A work
// record already has the original pre-mining metrics and must not be refreshed
// again here; its net balance is committed after both slave paths finish.
class s1702OldRuntimeComputerMiningScope {
public:
    s1702OldRuntimeComputerMiningScope(RuntimeWindowState& state, const std::string& owner,
        const RuntimeEconomyState* ordinaryWork = nullptr)
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
        if (ordinaryWork) state.economy = *ordinaryWork;
        state.techTree = TechTreeReport{};
        state.techTreeLoaded = false;
        state.humanHouseScopeActive = true;
        state.remoteHumanHouseScopeActive = true;
        state.computerMiningScopeActive = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
        if (!ordinaryWork) refreshRuntimeEconomyMetricsForCurrentOwner(state);
    }
    s1702OldRuntimeComputerMiningScope(const s1702OldRuntimeComputerMiningScope&) = delete;
    s1702OldRuntimeComputerMiningScope& operator=(const s1702OldRuntimeComputerMiningScope&) = delete;
    ~s1702OldRuntimeComputerMiningScope() {
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

// The old enclosing Scope committed a partial explicit balance during stack
// unwinding too. Preserve that synchronous owner-account boundary on failure.
void s1702OldCommitRuntimeComputerMiningWorkAfterExplicitFailure(RuntimeWindowState& state,
    const std::string& owner, const RuntimeEconomyState& work, int startingCredits) {
    const std::string key = runtimeRepairDepotCanonicalOwnerId(state, owner);
    auto& ledger = state.computerMiningByOwner[key];
    ledger.storedResourceUnits = work.storedResourceUnits;
    ledger.storedResourceValue = work.storedResourceValue;
    ledger.storedResourceGemUnits = work.storedResourceGemUnits;
    ledger.resourceTrips = work.resourceTrips;
    ledger.harvesterTeleports = work.harvesterTeleports;
    ledger.slaveMinerLoads = work.slaveMinerLoads;
    const int income = std::max(0, work.credits - startingCredits);
    depositRuntimeRepairDepotCredits(state, key, income);
    const auto ai = state.computerAiByOwner.find(key);
    if (!state.campaignMode && ai != state.computerAiByOwner.end()) {
        ai->second.incomeEarned = static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),
            static_cast<long long>(ai->second.incomeEarned) + income));
    }
}

// Preserve the old synchronous feedback policy without moving either the
// player's economy or technology tree during explicit computer harvesting.
class s1702OldRuntimeComputerMiningFeedbackScope {
public:
    explicit s1702OldRuntimeComputerMiningFeedbackScope(RuntimeWindowState& state)
        : state_(state), interaction_(state.interactionStatus), simulation_(state.simulationStatus),
          command_(state.lastCommand), human_(state.humanHouseScopeActive),
          remote_(state.remoteHumanHouseScopeActive), computer_(state.computerMiningScopeActive),
          suppress_(state.runtimeSuppressPlayerAudioFeedback) {
        state.humanHouseScopeActive = state.remoteHumanHouseScopeActive = state.computerMiningScopeActive = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
    }
    ~s1702OldRuntimeComputerMiningFeedbackScope() {
        state_.interactionStatus = std::move(interaction_); state_.simulationStatus = std::move(simulation_);
        state_.lastCommand = std::move(command_); state_.humanHouseScopeActive = human_;
        state_.remoteHumanHouseScopeActive = remote_; state_.computerMiningScopeActive = computer_;
        state_.runtimeSuppressPlayerAudioFeedback = suppress_;
    }
    s1702OldRuntimeComputerMiningFeedbackScope(const s1702OldRuntimeComputerMiningFeedbackScope&) = delete;
    s1702OldRuntimeComputerMiningFeedbackScope& operator=(const s1702OldRuntimeComputerMiningFeedbackScope&) = delete;
private:
    RuntimeWindowState& state_;
    std::string interaction_, simulation_;
    decltype(RuntimeWindowState::lastCommand) command_;
    bool human_, remote_, computer_, suppress_;
};

bool s1702OldAdvanceRuntimeComputerMining(RuntimeWindowState& state) {
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
        RuntimeEconomyState ordinaryWork;
        ordinaryWork.owner = owner;
        ordinaryWork.credits = runtimeRepairDepotCreditBalance(state, owner);
        const auto& ledger = state.computerMiningByOwner[runtimeRepairDepotCanonicalOwnerId(state, owner)];
        ordinaryWork.storedResourceUnits = ledger.storedResourceUnits;
        ordinaryWork.storedResourceValue = ledger.storedResourceValue;
        ordinaryWork.storedResourceGemUnits = ledger.storedResourceGemUnits;
        ordinaryWork.resourceTrips = ledger.resourceTrips;
        ordinaryWork.harvesterTeleports = ledger.harvesterTeleports;
        ordinaryWork.slaveMinerLoads = ledger.slaveMinerLoads;
        ordinaryWork.skirmishStartApplied = state.economy.skirmishStartApplied;
        refreshRuntimeEconomyMetricsForOwner(state, ordinaryWork);
        const int ordinaryStartingCredits = ordinaryWork.credits;
        try {
            s1702OldRuntimeComputerMiningFeedbackScope feedback(state);
            auto context = makeRuntimeHouseEconomyContext(ordinaryWork, true);
            changed = advanceRuntimeHarvesterEconomy(state, context, ordinaryWork) || changed;
            changed = advanceRuntimeMobileSlaveMinerEconomy(state, context, ordinaryWork) || changed;
        } catch (...) {
            s1702OldCommitRuntimeComputerMiningWorkAfterExplicitFailure(state, owner, ordinaryWork, ordinaryStartingCredits);
            throw;
        }
        // Only the slave worker consumer borrows the legacy facade. Carry the
        // explicit balance into that scope without depositing early: one net
        // delta is still committed after slave workers, in the original order.
        s1702OldRuntimeComputerMiningScope scope(state, owner, &ordinaryWork);
        changed = s1702OldAdvanceRuntimeSlaveMinerEconomy(state) || changed;
    }
    return changed;
}
