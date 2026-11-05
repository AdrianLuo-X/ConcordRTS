#include "runtime/session/detail/RuntimeSessionCommands.h"

namespace concord::runtime::session_detail {





#include "simulation/buildings/RuntimeBuildingPlacementRange.inl"

std::string runtimeStructureBuildSlotBlockReason(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item
) {
    const std::optional<RuntimeProductionLane> lane =
        runtimeProductionLaneForCatalogItem(state, item);
    if (!lane.has_value()
        || (*lane != RuntimeProductionLane::Structures
            && *lane != RuntimeProductionLane::Armory)) {
        return "";
    }
    const RuntimeProductionLaneState& laneState =
        state.economy.localProductionLanes.lanes[
            runtimeProductionLaneIndex(*lane)
        ];
    if (laneState.ready.has_value()) {
        const std::string typeId = trimAsciiLocal(laneState.ready->itemKey);
        return "place or cancel ready "
            + (typeId.empty() ? std::string("structure") : typeId);
    }

    if (laneState.active.has_value()) {
        const std::string typeId = trimAsciiLocal(
            laneState.active->itemKey
        );
        return (laneState.onHold
                ? "resume or cancel held "
                : "under construction ")
            + (typeId.empty() ? std::string("structure") : typeId);
    }

    if (!laneState.queued.empty()) {
        const std::string typeId = trimAsciiLocal(
            laneState.queued.front().itemKey
        );
        return "already queued "
            + (typeId.empty() ? std::string("structure") : typeId);
    }

    return "";
}

RuntimeBuildAvailability runtimeBuildAvailability(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    bool respectStructureQueueLimit,
    bool respectBuildLimit
) {
    RuntimeBuildAvailability availability;
    availability.decision = evaluateRuntimeTechTreeDecision(state, item);
    availability.canQueue = availability.decision.canQueue;
    availability.canStart = availability.decision.canStart;
    availability.enoughCredits = availability.decision.enoughCredits;
    availability.lowPower = availability.decision.lowPower;
    availability.lowPowerAfterBuild = availability.decision.lowPowerAfterBuild;
    availability.powerNetAfterBuild = availability.decision.powerNetAfterBuild;
    availability.reason = availability.decision.reason;
    if (respectStructureQueueLimit
        && availability.canQueue
        && runtimeBuildKindUsesSingleStructureSlot(item.kind)) {
        const std::string blockReason =
            runtimeStructureBuildSlotBlockReason(state, item);
        if (!blockReason.empty()) {
            availability.canQueue = false;
            availability.canStart = false;
            availability.reason = blockReason;
        }
    }
    if (respectBuildLimit && availability.canQueue) {
        const std::string blockReason = runtimeBuildLimitBlockReason(
            state,
            state.economy.owner,
            item,
            false,
            true
        );
        if (!blockReason.empty()) {
            availability.canQueue = false;
            availability.canStart = false;
            availability.reason = blockReason;
        }
    }
    if (availability.canQueue) {
        const std::optional<RuntimeProductionLane> lane =
            runtimeProductionLaneForCatalogItem(state, item);
        if (!lane.has_value()) {
            availability.canQueue = false;
            availability.canStart = false;
            availability.reason = "unknown production lane";
        } else {
            const int aircraftDockCapacity =
                *lane == RuntimeProductionLane::Aircraft
                    && runtimeBuildItemUsesPadAircraft(state, item)
                    ? std::numeric_limits<int>::max()
                    : 0;
            const int capacity = runtimeProductionLaneCapacity(
                *lane,
                state.rules.getMaximumQueuedObjects(),
                aircraftDockCapacity
            );
            if (runtimeProductionLaneOccupancy(
                    state.economy.localProductionLanes,
                    *lane
                ) >= capacity) {
                availability.canQueue = false;
                availability.canStart = false;
                availability.reason =
                    "production lane full "
                    + std::string(runtimeProductionLaneName(*lane));
            }
        }
    }
    if (availability.canQueue
        && runtimeBuildItemUsesPadAircraft(state, item)) {
        RuntimeAircraftProductionOrder prospective;
        prospective.serial = std::max<std::uint64_t>(
            1,
            state.economy.localProductionLanes.nextSerial
        );
        prospective.typeId = item.typeId;
        prospective.phase =
            RuntimeAircraftProductionOrderPhase::Prospective;
        RuntimeAircraftProductionMatchResult matched;
        if (state.economy.localProductionLanes.nextSerial
                != std::numeric_limits<std::uint64_t>::max()) {
            matched = runtimeAircraftProductionMatchForOwner(
                state,
                state.economy.owner,
                prospective
            );
        }
        if (!matched.inputValid || !matched.feasible) {
            availability.canQueue = false;
            availability.canStart = false;
            availability.reason = matched.reason.empty()
                ? "no feasible aircraft pad assignment"
                : matched.reason;
        }
    }
    return availability;
}

bool runtimeCellCanPlaceSidebarBuilding(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& cell,
    const std::unordered_map<int, int>* occupiedByCell,
    const std::string& placementOwner,
    const std::unordered_set<int>* rubbleCellsOverride
) {
    return runtimeBuildingPlacementInRange(state, item, cell, placementOwner)
        && runtimeCellCanPlaceBuilding(state, item, cell, -1, occupiedByCell, placementOwner, rubbleCellsOverride);
}

WorldObjectInstance* addRuntimeObjectInstance(
    RuntimeProductionHostState& state,
    RuntimeBuildKind kind,
    const std::string& typeId,
    const RuntimeCellPoint& cell,
    bool clearPlacedSmudges
) {
    return addRuntimeObjectInstanceForOwner(
        state,
        state.economy.owner,
        kind,
        typeId,
        cell,
        std::nullopt,
        clearPlacedSmudges
    );
}

std::optional<RuntimeCellPoint> findRuntimeSpawnCellNearObject(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const WorldObjectInstance& anchor
) {
    return findRuntimeSpawnCellNearObjectForOwner(
        state,
        item,
        anchor,
        state.economy.owner
    );
}

std::vector<RuntimeCellPoint> runtimeWallConnectorCells(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& placementCell
) {
    std::vector<RuntimeCellPoint> connectors;
    if (!item.wall) {
        return connectors;
    }
    const BuildingType* wall =
        findRuntimeBuildingTypeById(state.rules, item.typeId);
    if (wall == nullptr || !wall->wall) {
        return connectors;
    }
    const int connectionRange = std::clamp(wall->guardRange, 1, 5);
    constexpr RuntimeCellPoint directions[] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };
    const std::string wallType = toUpperAsciiLocal(item.typeId);
    for (const RuntimeCellPoint& direction : directions) {
        int matchingDistance = -1;
        for (int distance = 1; distance <= connectionRange; ++distance) {
            const RuntimeCellPoint target{
                placementCell.x + direction.x * distance,
                placementCell.y + direction.y * distance
            };
            const auto matchingWall = std::find_if(
                state.world.getObjects().begin(),
                state.world.getObjects().end(),
                [&](const WorldObjectInstance& object) {
                    return object.alive
                        && object.kind == WorldObjectKind::Structure
                        && object.owner == state.economy.owner
                        && toUpperAsciiLocal(object.type) == wallType
                        && object.x == target.x
                        && object.y == target.y;
                }
            );
            if (matchingWall != state.world.getObjects().end()) {
                matchingDistance = distance;
                break;
            }
        }
        if (matchingDistance <= 1) {
            continue;
        }

        std::vector<RuntimeCellPoint> corridor;
        bool corridorClear = true;
        for (int distance = 1; distance < matchingDistance; ++distance) {
            const RuntimeCellPoint cell{
                placementCell.x + direction.x * distance,
                placementCell.y + direction.y * distance
            };
            if (!runtimeCellCanPlaceBuilding(state, item, cell)) {
                corridorClear = false;
                break;
            }
            corridor.push_back(cell);
        }
        if (!corridorClear) {
            continue;
        }
        for (const RuntimeCellPoint& cell : corridor) {
            if (std::find(connectors.begin(), connectors.end(), cell)
                == connectors.end()) {
                connectors.push_back(cell);
            }
        }
    }
    std::sort(connectors.begin(), connectors.end(), RuntimeCellPointLess{});
    return connectors;
}

bool queueRuntimeBuildOrder(
    RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    RuntimeProductionInsertMode insertMode
) {
    const std::optional<RuntimeProductionLane> lane =
        runtimeProductionLaneForCatalogItem(state, item);
    if (!lane.has_value()) {
        state.economy.status =
            "cannot queue " + item.typeId + ": unknown production lane";
        state.interactionStatus = state.economy.status;
        emitRuntimeSound(state, runtimeSoundIntentSidebarScold);
        return false;
    }
    RuntimeProductionLaneState& laneState =
        state.economy.localProductionLanes.lanes[
            runtimeProductionLaneIndex(*lane)
        ];
    if (laneState.active.has_value()
        && runtimeProductionItemKeysMatch(
            laneState.active->itemKey,
            item.typeId
        )
        && laneState.onHold) {
        runtimeSetProductionHold(
            state.economy.localProductionLanes,
            *lane,
            false
        );
        state.economy.status = "resumed " + item.typeId;
        state.interactionStatus = state.economy.status;
        emitRuntimeSound(state, runtimeSoundIntentSidebarButtonClick);
        return true;
    }

    const RuntimeBuildAvailability availability =
        runtimeBuildAvailability(state, item);
    if (!availability.canQueue) {
        state.economy.status =
            "cannot queue " + item.typeId + ": " + availability.reason;
        state.interactionStatus = state.economy.status;
        emitRuntimeSound(
            state,
            availability.decision.enoughCredits
                ? runtimeSoundIntentSidebarScold
                : runtimeSoundIntentInsufficientFunds
        );
        return false;
    }

    RuntimeProductionLaneOrder order;
    RuntimeProductionLanesState stagedLanes =
        state.economy.localProductionLanes;
    order.itemKey = item.typeId;
    order.cost = std::max(0, item.cost);
    order.totalTicks = std::max(1, item.buildTicks);
    const bool padAircraft = runtimeBuildItemUsesPadAircraft(state, item);
    const std::uint64_t predictedSerial = std::max<std::uint64_t>(
        1,
        state.economy.localProductionLanes.nextSerial
    );
    if (padAircraft) {
        RuntimeAircraftProductionOrder prospective;
        prospective.serial = predictedSerial;
        prospective.typeId = item.typeId;
        prospective.phase =
            RuntimeAircraftProductionOrderPhase::Prospective;
        const RuntimeAircraftProductionMatchResult matched =
            state.economy.localProductionLanes.nextSerial
                    == std::numeric_limits<std::uint64_t>::max()
                ? RuntimeAircraftProductionMatchResult{}
                : runtimeAircraftProductionMatchForOwner(
                    state,
                    state.economy.owner,
                    prospective
                );
        if (!matched.inputValid || !matched.feasible) {
            state.economy.status = "cannot queue " + item.typeId
                + ": " + (matched.reason.empty()
                    ? std::string("no feasible aircraft pad assignment")
                    : matched.reason);
            state.interactionStatus = state.economy.status;
            emitRuntimeSound(state, runtimeSoundIntentSidebarScold);
            return false;
        }
    }
    const int aircraftDockCapacity =
        *lane == RuntimeProductionLane::Aircraft && padAircraft
            ? std::numeric_limits<int>::max()
            : 0;
    const RuntimeProductionQueueResult queued =
        runtimeQueueProductionOrder(
            stagedLanes,
            *lane,
            std::move(order),
            state.rules.getMaximumQueuedObjects(),
            aircraftDockCapacity,
            insertMode
        );
    if (queued.decision != RuntimeProductionQueueDecision::Accepted
        || (padAircraft && queued.serial != predictedSerial)) {
        state.economy.status =
            "cannot queue " + item.typeId + ": production lane full";
        state.interactionStatus = state.economy.status;
        emitRuntimeSound(state, runtimeSoundIntentSidebarScold);
        return false;
    }
    state.economy.localProductionLanes = std::move(stagedLanes);
    state.economy.status =
        "queued " + item.typeId + " #" + std::to_string(queued.serial)
        + " cost " + std::to_string(item.cost);
    state.interactionStatus = state.economy.status;
    emitRuntimeSound(state, runtimeSoundIntentBuildQueued);
    return true;
}

bool queueRuntimeBuildOrderForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& commandOwner,
    const RuntimeBuildCatalogItem& item,
    RuntimeProductionInsertMode insertMode
) {
    RuntimeHumanHouseScope scope(state, commandOwner);
    return scope && queueRuntimeBuildOrder(state, item, insertMode);
}

bool cancelRuntimeBuildOrderForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& commandOwner,
    RuntimeProductionLane lane,
    std::uint64_t serial
) {
    RuntimeHumanHouseScope scope(state, commandOwner);
    if (!scope || serial == 0) {
        return false;
    }
    const RuntimeProductionCancelResult canceled =
        runtimeCancelProductionSerial(
            state.economy.localProductionLanes,
            lane,
            serial
        );
    if (!canceled.removed) {
        return false;
    }
    depositRuntimeCredits(state, canceled.refund);
    if (state.economy.selectedPlacementLane == lane
        && state.economy.selectedPlacementSerial == serial) {
        state.economy.selectedPlacementLane.reset();
        state.economy.selectedPlacementSerial = 0;
    }
    clearRuntimeInvalidPlacementSelection(state);
    state.economy.status =
        "canceled production #" + std::to_string(serial)
        + (canceled.refund > 0
            ? " refund " + std::to_string(canceled.refund)
            : "");
    state.interactionStatus = state.economy.status;
    emitRuntimeSound(state, runtimeSoundIntentBuildCanceled);
    return true;
}

bool placeRuntimeReadyBuilding(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell
) {
    const std::optional<RuntimeSelectedPlacement> placement =
        runtimeSelectedPlacement(state);
    if (!placement.has_value()
        || placement->item == nullptr
        || (placement->item->kind != RuntimeBuildKind::Building
            && placement->item->kind != RuntimeBuildKind::Defense)
        || !runtimeCellCanPlaceSidebarBuilding(state, *placement->item, cell)) {
        state.economy.status = "placement blocked";
        emitRuntimeSound(state, runtimeSoundIntentCannotDeployHere);
        return false;
    }

    const int unlockedOptionsBefore =
        runtimeUnlockedBuildOptionCount(state);
    const RuntimeBuildCatalogItem item = *placement->item;
    const std::vector<RuntimeCellPoint> wallConnectors =
        runtimeWallConnectorCells(state, item, cell);

    WorldObjectInstance* placed = addRuntimeObjectInstance(
        state,
        item.kind,
        item.typeId,
        cell,
        false
    );
    if (placed == nullptr) {
        state.economy.status = "placement failed";
        return false;
    }
    const WorldObjectInstance placedAnchor = *placed;
    const std::string placedTypeId = placedAnchor.type;
    const int placedInstanceId = placedAnchor.instanceId;
    const std::optional<RuntimeProductionLaneOrder> committed =
        runtimeConsumeReadyProduction(
            state.economy.localProductionLanes,
            placement->lane,
            placement->order.serial
        );
    if (!committed.has_value()) {
        std::vector<WorldObjectInstance>& objects =
            state.world.accessObjects();
        const auto rollback = std::find_if(
            objects.begin(),
            objects.end(),
            [placedInstanceId](const WorldObjectInstance& object) {
                return object.instanceId == placedInstanceId;
            }
        );
        if (rollback != objects.end()) {
            objects.erase(rollback);
        }
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        state.economy.status = "placement commit failed";
        return false;
    }
    recordRuntimeCompletedBuild(
        state,
        placedAnchor.owner,
        item
    );
    state.economy.selectedPlacementLane.reset();
    state.economy.selectedPlacementSerial = 0;
    int placedWallConnectors = 0;
    for (const RuntimeCellPoint& connectorCell : wallConnectors) {
        WorldObjectInstance* connector = addRuntimeObjectInstance(
            state,
            item.kind,
            item.typeId,
            connectorCell
        );
        if (connector != nullptr) {
            ++placedWallConnectors;
        }
    }
    (void)clearRuntimeSmudgesForPlacedStructure(state, placedAnchor);
    state.economy.completedBuildings += 1 + placedWallConnectors;
    clearRuntimeInvalidPlacementSelection(state);
    std::string freeUnitStatus;
    if (const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, placedAnchor.type)) {
        if (!building->freeUnit.empty()
            && findRuntimeUnitTypeById(state.rules, building->freeUnit) != nullptr) {
            const UnitType* freeUnitType =
                findRuntimeUnitTypeById(state.rules, building->freeUnit);
            RuntimeBuildCatalogItem freeUnitItem;
            freeUnitItem.kind =
                runtimeBuildKindForUnitTypeId(state.rules, building->freeUnit);
            freeUnitItem.typeId = building->freeUnit;
            freeUnitItem.buildLimit = freeUnitType == nullptr
                ? std::optional<int>{}
                : freeUnitType->buildLimit;
            freeUnitItem.cost = 0;
            freeUnitItem.buildTicks = 1;
            (void)state.factoryDeliveries.deferFreeUnit({placedAnchor.instanceId,
                placedAnchor.owner, placedAnchor.type, building->freeUnit});
            const std::optional<RuntimeCellPoint> freeUnitCell =
                findRuntimeSpawnCellNearObject(
                    state,
                    freeUnitItem,
                    placedAnchor
                );
            if (freeUnitCell.has_value()) {
                WorldObjectInstance* freeUnit = addRuntimeObjectInstance(
                    state,
                    freeUnitItem.kind,
                    freeUnitItem.typeId,
                    *freeUnitCell
                );
                if (freeUnit != nullptr) {
                    applyRuntimeProducedObjectVeterancy(state, *freeUnit);
                    recordRuntimeCompletedBuild(
                        state,
                        freeUnit->owner,
                        freeUnitItem
                    );
                    queueRuntimeProductionExitMove(
                        state,
                        *freeUnit,
                        placedAnchor
                    );
                    state.factoryDeliveries.completeFreeUnit(placedAnchor.instanceId);
                    freeUnitStatus =
                        " free " + freeUnit->type + " #"
                        + std::to_string(freeUnit->instanceId);
                }
            }
        }
    }
    state.economy.status =
        "placed " + placedTypeId + " #"
        + std::to_string(placedInstanceId)
        + (placedWallConnectors > 0
            ? " connected " + std::to_string(placedWallConnectors)
            : "")
        + freeUnitStatus;
    state.interactionStatus = state.economy.status;
    emitRuntimeSound(state, runtimeSoundIntentPlacementConfirmed);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    rebuildRuntimeEconomyPower(state);
    if (runtimeUnlockedBuildOptionCount(state) > unlockedOptionsBefore) {
        emitRuntimeSound(
            state,
            runtimeSoundIntentNewConstructionOptions
        );
    }
    return true;
}

bool runtimeStructureCanBeSold(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& object
) {
    if (!object.alive
        || object.kind != WorldObjectKind::Structure
        || object.owner != state.economy.owner) {
        return false;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    return building == nullptr || !building->unsellable;
}

bool runtimeStructureCanStartRepair(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& object
) {
    const BuildingType* building = findRuntimeBuildingTypeById(state.rules, object.type);
    return object.alive
        && object.kind == WorldObjectKind::Structure
        && object.owner == state.economy.owner
        && building != nullptr && building->repairable && building->clickRepairable
        && (state.repairingStructureTicks.contains(object.instanceId)
            || object.health < runtimeMaxHealthForObject(state, object));
}

bool sellRuntimeStructure(RuntimeProductionHostState& state, int objectId) {
    WorldObjectInstance* object = findMutableObjectById(state, objectId);
    if (object == nullptr
        || object->kind != WorldObjectKind::Structure
        || object->owner != state.economy.owner) {
        state.economy.status = "sell failed: select owned structure";
        return false;
    }

    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object->type);
    if (!runtimeStructureCanBeSold(state, *object)) {
        state.economy.status = "sell failed: structure is unsellable";
        return false;
    }
    const int refund = runtimeBuildingSaleRefund(building, state.rules.getRefundPercent());
    const std::string soldType = object->type;
    RuntimeObjectExitRequest objectExit;
    objectExit.objectId = object->instanceId;
    objectExit.kind = RuntimeObjectExitKind::Sold;
    objectExit.tick = state.tick;
    objectExit.mission = "Sold";
    objectExit.reason = "sold";
    objectExit.sourceOwner = object->owner;
    objectExit.suppressDeathWeapon = true;
    if (queueRuntimeObjectExit(state, std::move(objectExit))
            != RuntimeObjectExitQueueResult::Queued
        || !drainRuntimeObjectExitQueue(state)) {
        state.economy.status = "sell failed: object exit rejected";
        return false;
    }
    depositRuntimeCredits(state, refund);
    state.economy.status =
        "sold " + soldType + " +$" + std::to_string(refund);
    emitRuntimeSound(state, runtimeSoundIntentSellStructure);
    emitRuntimeSound(state, "StructureSold");
    // The exit queue has invalidated dynamic occupancy and refreshed walls.
    // Selling leaves authored terrain and its warmed rendering/path caches intact.
    state.bridgeRepairSpansByHut.erase(objectId);
    (void)reconcileRuntimeSubcells(state, true);
    refreshRuntimeNavigationOccupancyStats(state);
    state.navigationRefreshPending = false;
    state.powerRefreshPending = false;
    rebuildRuntimeEconomyPower(state);
    return true;
}

bool repairRuntimeStructure(RuntimeProductionHostState& state, int objectId) {
    WorldObjectInstance* object = findMutableObjectById(state, objectId);
    if (object == nullptr
        || object->kind != WorldObjectKind::Structure
        || object->owner != state.economy.owner) {
        state.economy.status = "repair failed: select owned structure";
        return false;
    }

    if (!runtimeStructureCanStartRepair(state, *object)) {
        state.economy.status = "repair failed: structure cannot be repaired";
        return false;
    }
    if (state.repairingStructureTicks.erase(objectId) != 0) {
        state.economy.status = "repair cancelled: " + object->type;
        return true;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object->type);
    const int maxHealth =
        building == nullptr || building->strength <= 0
            ? std::max(256, object->health)
            : building->strength;
    if (object->health >= maxHealth) {
        state.economy.status = object->type + " already repaired";
        return false;
    }

    state.repairingStructureTicks[object->instanceId] = 0;
    state.economy.status =
        "repairing " + object->type;
    emitRuntimeSound(state, runtimeSoundIntentBuildingRepaired);
    emitRuntimeSound(state, "Repairing");
    return true;
}

bool sellRuntimeStructureForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& commandOwner,
    int objectId
) {
    bool remote = false;
    bool sold = false;
    {
        RuntimeHumanHouseScope scope(state, commandOwner);
        if (!scope) {
            return false;
        }
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object == nullptr
            || runtimeCanonicalHumanHouseOwner(object->owner)
                != runtimeCanonicalHumanHouseOwner(state.economy.owner)) {
            return false;
        }
        remote = scope.remote();
        sold = sellRuntimeStructure(state, objectId);
    }
    if (remote && sold) {
        rebuildRuntimeEconomyPower(state);
    }
    return sold;
}

bool repairRuntimeStructureForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& commandOwner,
    int objectId
) {
    RuntimeHumanHouseScope scope(state, commandOwner);
    if (!scope) {
        return false;
    }
    const WorldObjectInstance* object = findObjectById(state, objectId);
    if (object == nullptr
        || runtimeCanonicalHumanHouseOwner(object->owner)
            != runtimeCanonicalHumanHouseOwner(state.economy.owner)) {
        return false;
    }
    return repairRuntimeStructure(state, objectId);
}

void issueRuntimeGuardCommand(RuntimeProductionHostState& state) {
    cleanRuntimeSelection(state);
    resetLastRuntimeCommand(state, "guard", {-1, -1}, -1, "");
    for (int objectId : runtimeCombatSelection(state)) {
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr || !object->alive
            || runtimeObjectDisabledByParasite(state, objectId)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        const bool cancellingStructureEntry = runtimeCancelledStructureEntryNeedsRecovery(state, *object);
        const auto navalDockPose = runtimeNavalDockCommandPose(state, *object);
        if (!releaseRuntimeRepairDepotForCommand(
                state,
                objectId,
                "manual guard command"
            )) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        const bool deployed = runtimeObjectHasDeployedState(state, *object);
        const bool deployedAreaFireComplete =
            toLowerAsciiLocal(object->mission)
                == "deployedareafirecomplete";
        stopRuntimeUnstartedJumpjetSegment(state, *object);
        cancelRuntimeCombatFire(state, objectId);
        const auto oldMove = state.behaviors.find(objectId);
        const bool cancelledBoarding = oldMove != state.behaviors.end()
            && oldMove->second.transportTransferKind == RuntimeTransportTransferKind::Boarding;
        if (!eraseRuntimeBehavior(state, objectId)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        if (object->tankBunkerHostId < 0 && !cancelledBoarding && !cancellingStructureEntry) clearRuntimeRenderPosition(*object);
        applyRuntimeJumpjetPose(state, *object);
        applyRuntimeFixedWingPose(state, *object);
        restoreRuntimeNavalDockCommandStopPose(*object, navalDockPose);
        object->mission = runtimeStructureIsRepacking(*object) ? "Repacking" :
            deployedAreaFireComplete
                ? "DeployedAreaFireComplete"
                : (deployed
                ? runtimeMissionName(RuntimeMissionIntent::Deployed)
                : runtimeMissionName(RuntimeMissionIntent::Idle));
        if (object->kind == WorldObjectKind::Structure) {
            object->turretFacingClock = {};
        } else {
            object->turretFacing = deployed ? object->facing : -1;
        }
        ++state.lastCommand.acceptedCount;
    }

    state.interactionStatus =
        state.lastCommand.acceptedCount > 0
            ? "guard command accepted"
            : "guard command rejected";
    emitRuntimeSound(
        state,
        state.lastCommand.acceptedCount > 0 ? "command guard" : "blocked"
    );
    recordRuntimeCommandReplayEvent(state);
    refreshRuntimeActionSummary(state);
}

void issueRuntimeScatterCommand(RuntimeProductionHostState& state) {
    cleanRuntimeSelection(state);
    resetLastRuntimeCommand(state, "scatter", {-1, -1}, -1, "");

    std::unordered_set<int> movingGroupIds;
    for (int objectId : state.selectedObjectIds) {
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object != nullptr && runtimeObjectCanMove(state, *object)) {
            movingGroupIds.insert(objectId);
        }
    }

    std::unordered_set<int> reservedTargetCells;
    std::unordered_map<int, const TerrainCell*> generatedTerrainByCell;
    const std::unordered_map<int, const TerrainCell*>* terrainByCell =
        &state.terrainByCell;
    if (terrainByCell->empty()) {
        generatedTerrainByCell = buildRuntimeTerrainLookup(state.world);
        terrainByCell = &generatedTerrainByCell;
    }
    const std::unordered_map<int, int> groupOccupiedByCell =
        buildRuntimeOccupancyLookup(state, -1);
    int movableIndex = 0;
    for (int objectId : state.selectedObjectIds) {
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr || !runtimeObjectCanMove(state, *object)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }

        const RuntimeCellPoint current = runtimeCellPointForObject(*object);
        const auto nearby = runtimeNeighborsOf(current);
        const std::size_t first = static_cast<std::size_t>(movableIndex++) % nearby.size();
        std::optional<RuntimeCellPoint> passableTarget;
        // Manual X stays local to each actor. Neither a larger selection nor
        // an obstructed direction expands the search into a distant formation.
        // Check the actual one-step transition so a legal-looking diagonal
        // cannot require a long detour around occupied corners.
        for (std::size_t step = 0; step < nearby.size(); ++step) {
            const auto& candidate = nearby[(first + step) % nearby.size()];
            if (reservedTargetCells.contains(runtimeCellKey(candidate.x, candidate.y))
                || (runtimeObjectMovementMode(state, *object) == PathMovementMode::Air
                    && !runtimeCellIsPassable(state, *terrainByCell,
                        groupOccupiedByCell, candidate, *object, -1, nullptr, false))
                || !runtimeCellTransitionIsPassable(state, *terrainByCell,
                    groupOccupiedByCell, current, candidate, *object, -1, nullptr, false))
                continue;
            passableTarget = candidate;
            break;
        }
        if (!passableTarget.has_value()) {
            ++state.lastCommand.rejectedCount;
            continue;
        }

        if (!releaseRuntimeRepairDepotForCommand(
                state,
                objectId,
                "manual scatter command"
            )) {
            ++state.lastCommand.rejectedCount;
            continue;
        }

        departRuntimeAircraftDock(state, *object);
        if (!eraseRuntimeBehavior(state, objectId)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        RuntimeObjectBehavior behavior;
        behavior.kind = RuntimeBehaviorKind::Move;
        behavior.state = RuntimeBehaviorState::Queued;
        behavior.moveTarget = *passableTarget;
        behavior.pendingIgnoredObjectIds = movingGroupIds;
        setRuntimeMissionIntent(
            state,
            *object,
            RuntimeMissionIntent::Idle,
            "scatterQueued"
        );
        state.behaviors[objectId] = std::move(behavior);
        reservedTargetCells.insert(runtimeCellKey(passableTarget->x, passableTarget->y));
        ++state.lastCommand.acceptedCount;
    }

    state.actionSummary.formationTargets =
        static_cast<int>(reservedTargetCells.size());
    state.interactionStatus =
        state.lastCommand.acceptedCount > 0
            ? "scatter command accepted"
            : "scatter command rejected";
    emitRuntimeSound(
        state,
        state.lastCommand.acceptedCount > 0 ? "command scatter" : "blocked"
    );
    recordRuntimeCommandReplayEvent(state);
    refreshRuntimeActionSummary(state);
}

std::optional<bool> runtimeSecondaryDeploymentState(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& object
) {
    if (!object.alive || object.health <= 0
        || runtimeObjectDisabledByParasite(state, object.instanceId)
        || state.specialMechanisms.isChronoMovementLocked(object.instanceId, state.tick)
        || object.tankBunkerHostId >= 0 || object.tankBunkerOccupantId >= 0) {
        return std::nullopt;
    }
    const RuntimeDeployProfile profile = runtimeDeployProfileForObject(state, object);
    if (object.kind == WorldObjectKind::Structure && !profile.undeploysInto.empty()) {
        // A yard is the deployed form even when its mission is Guard and its
        // deployed flag is false. A folding yard already heads toward mobile.
        if (runtimeStructureIsRepacking(object)) return false;
        return runtimeStructureCanRepack(state, object)
            ? std::optional<bool>{true} : std::nullopt;
    }
    if (!profile.transformsIntoStructure && !profile.togglesDeployedState) {
        return std::nullopt;
    }
    if (runtimeMissionIsUndeploying(object)
        || runtimeMissionIsTakingOffAfterUndeploy(object)) return false;
    if (runtimeMissionIsDeploying(object)
        || runtimeMissionIsLandingToDeploy(object)) return true;
    return profile.transformsIntoStructure
        ? false : runtimeObjectHasDeployedState(state, object);
}

bool executeRuntimeSecondaryActions(
    RuntimeProductionHostState& state,
    const std::vector<int>& objectIds,
    RuntimeMultiplayerGameplaySecondaryActionKind action
) {
    const std::optional<bool> desired = action == RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll
        ? std::optional<bool>{true}
        : action == RuntimeMultiplayerGameplaySecondaryActionKind::UndeployAll
            ? std::optional<bool>{false} : std::nullopt;
    // Freeze each decision before any transform changes selection IDs or
    // appends objects. Unloading and suicide are momentary actions, not states
    // used to choose the group's deployment direction.
    std::vector<std::pair<int, std::optional<bool>>> planned;
    bool handled = false;
    for (int id : objectIds) {
        const auto* object = findObjectById(state, id);
        if (object == nullptr || runtimeObjectDisabledByParasite(state, id)) continue;
        const RuntimeSecondaryAbility ability = runtimeSecondaryAbilityForObject(state, *object);
        const std::optional<bool> deployed = runtimeSecondaryDeploymentState(state, *object);
        if (desired.has_value() && deployed.has_value()) {
            if (*deployed == *desired) {
                handled = true; // Do not restart an already desired transition.
                continue;
            }
            // Landing, takeoff, chassis turning and structure folding keep
            // their original completion rules; a group cannot reverse them.
            if (ability == RuntimeSecondaryAbility::None
                || runtimeMissionIsMcvDeployPending(*object)
                || runtimeStructureIsRepacking(*object)) {
                handled = true;
                continue;
            }
            planned.emplace_back(id, desired);
        } else if (ability != RuntimeSecondaryAbility::None
            && (action != RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle
                || ability == RuntimeSecondaryAbility::Deploy
                || ability == RuntimeSecondaryAbility::Undeploy)) {
            planned.emplace_back(id, std::nullopt);
        }
    }
    if (desired.has_value()) {
        std::vector<int> actedIds;
        for (const auto& item : planned) actedIds.push_back(item.first);
        state.playerCommands.cancel(actedIds);
    }
    for (const auto& item : planned) {
        handled = executeRuntimeSecondaryAction(state, item.first, item.second) || handled;
    }
    return handled;
}



struct RuntimeGameplayPeerBinding {
    std::size_t slotIndex = 0;
    std::string peerId;
    std::string owner;
};

bool buildRuntimeGameplayPeerBindings(
    const RuntimeProductionHostState& state,
    std::unordered_map<std::string, RuntimeGameplayPeerBinding>& bindings,
    std::string& errorMessage
) {
    std::unordered_map<std::string, RuntimeGameplayPeerBinding> staged;
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (!slot.enabled || !slot.human) {
            continue;
        }
        if (slot.peerId.empty()) {
            errorMessage =
                "enabled human slot has no authenticated peer binding";
            return false;
        }
        RuntimeGameplayPeerBinding binding;
        binding.slotIndex = slotIndex;
        binding.peerId = slot.peerId;
        binding.owner = runtimeSkirmishOwnerForSlot(slotIndex);
        if (findRuntimeHumanHouseAuthority(state, binding.owner) == nullptr) {
            errorMessage = "human peer binding has no house authority";
            return false;
        }
        if (!staged.emplace(binding.peerId, std::move(binding)).second) {
            errorMessage = "authenticated peer is bound to multiple slots";
            return false;
        }
    }
    bindings = std::move(staged);
    errorMessage.clear();
    return true;
}

std::optional<RuntimeProductionLane> runtimeGameplayProductionLane(
    RuntimeMultiplayerGameplayProductionLane lane
) {
    switch (lane) {
    case RuntimeMultiplayerGameplayProductionLane::Structures:
        return RuntimeProductionLane::Structures;
    case RuntimeMultiplayerGameplayProductionLane::Armory:
        return RuntimeProductionLane::Armory;
    case RuntimeMultiplayerGameplayProductionLane::Infantry:
        return RuntimeProductionLane::Infantry;
    case RuntimeMultiplayerGameplayProductionLane::Vehicles:
        return RuntimeProductionLane::Vehicles;
    case RuntimeMultiplayerGameplayProductionLane::Aircraft:
        return RuntimeProductionLane::Aircraft;
    case RuntimeMultiplayerGameplayProductionLane::Ships:
        return RuntimeProductionLane::Ships;
    }
    return std::nullopt;
}

RuntimeProductionInsertMode runtimeGameplayProductionInsertMode(
    RuntimeMultiplayerGameplayProductionInsertMode mode
) {
    return mode
            == RuntimeMultiplayerGameplayProductionInsertMode::AfterActive
        ? RuntimeProductionInsertMode::AfterActive
        : RuntimeProductionInsertMode::Back;
}

const RuntimeBuildCatalogItem* findRuntimeGameplayCatalogItem(
    const RuntimeProductionHostState& state,
    const std::string& owner,
    const std::string& typeId
) {
    const RuntimeEconomyState* economy =
        runtimeHumanHouseEconomy(state, owner);
    if (economy == nullptr) {
        return nullptr;
    }
    const std::string wanted =
        toUpperAsciiLocal(trimAsciiLocal(typeId));
    const auto found = std::find_if(
        economy->catalog.begin(),
        economy->catalog.end(),
        [&wanted](const RuntimeBuildCatalogItem& item) {
            return toUpperAsciiLocal(trimAsciiLocal(item.typeId)) == wanted;
        }
    );
    return found == economy->catalog.end() ? nullptr : &*found;
}





bool validateRuntimeGameplayVisibleTarget(
    const RuntimeProductionHostState& state,
    const std::string& owner,
    int objectId,
    std::string& errorMessage
) {
    const WorldObjectInstance* target = findObjectById(state, objectId);
    if (target == nullptr) {
        errorMessage = "gameplay target is missing or dead";
        return false;
    }
    if (!runtimeGameplayObjectVisibleToOwner(state, owner, *target)) {
        errorMessage = "gameplay target is not visible to authenticated peer";
        return false;
    }
    return true;
}

#include "ui/input/RuntimePlayerCommunication.inl"



bool prevalidateRuntimeMultiplayerGameplayCommand(
    const RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
) {
    if (state.multiplayerDefeatedOwners.contains(
            runtimeCanonicalHumanHouseOwner(owner)
        )) {
        errorMessage = "defeated multiplayer house cannot issue commands";
        return false;
    }
    std::string codecError;
    if (!validateRuntimeMultiplayerGameplayCommand(command, codecError)) {
        errorMessage = "invalid gameplay command: " + codecError;
        return false;
    }
    return std::visit(
        [&](const auto& body) {
            using Body = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayTargetOrderCommand>) {
                if (!validateRuntimeGameplayOwnedObjects(
                        state,
                        owner,
                        body.objectIds,
                        errorMessage
                    )) {
                    return false;
                }
                if (const auto* objectTarget = std::get_if<
                        RuntimeMultiplayerGameplayObjectTarget>(
                            &body.target
                        )) {
                    return validateRuntimeGameplayVisibleTarget(
                        state,
                        owner,
                        objectTarget->objectId,
                        errorMessage
                    );
                }
                return true;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayStopCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayGuardCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayScatterCommand>) {
                return validateRuntimeGameplayOwnedObjects(
                    state,
                    owner,
                    body.objectIds,
                    errorMessage
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySecondaryActionCommand>) {
                if (!validateRuntimeGameplayOwnedObjects(
                        state,
                        owner,
                        body.objectIds,
                        errorMessage
                    )) {
                    return false;
                }
                for (int objectId : body.objectIds) {
                    const WorldObjectInstance* object = findObjectById(state, objectId);
                    if (object == nullptr || runtimeObjectDisabledByParasite(state, objectId)) {
                        continue;
                    }
                    const RuntimeSecondaryAbility ability =
                        runtimeSecondaryAbilityForObject(state, *object);
                    if (body.action == RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll
                        || body.action == RuntimeMultiplayerGameplaySecondaryActionKind::UndeployAll) {
                        if (runtimeSecondaryDeploymentState(state, *object).has_value()
                            || ability != RuntimeSecondaryAbility::None) return true;
                    } else if (body.action == RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle) {
                        if (ability == RuntimeSecondaryAbility::Deploy
                            || ability == RuntimeSecondaryAbility::Undeploy) {
                            return true;
                        }
                    } else if (ability != RuntimeSecondaryAbility::None) {
                        return true;
                    }
                }
                errorMessage = "secondary selection has no available action";
                return false;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayQueueProductionCommand>) {
                const RuntimeBuildCatalogItem* item =
                    findRuntimeGameplayCatalogItem(state, owner, body.typeId);
                return item != nullptr
                    && runtimeProductionLaneForCatalogItem(state, *item)
                        .has_value()
                    ? true
                    : (errorMessage =
                        "production item is not in issuing house catalog",
                       false);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayCancelProductionCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySetProductionHoldCommand>) {
                if (!runtimeGameplayProductionLane(body.lane).has_value()) {
                    errorMessage = "production lane is invalid";
                    return false;
                }
                return true;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayPlaceBuildingCommand>) {
                const std::optional<RuntimeProductionLane> lane =
                    runtimeGameplayProductionLane(body.lane);
                const RuntimeBuildCatalogItem* item =
                    findRuntimeGameplayCatalogItem(
                        state,
                        owner,
                        body.buildingTypeId
                    );
                if (!lane.has_value()
                    || item == nullptr
                    || (item->kind != RuntimeBuildKind::Building
                        && item->kind != RuntimeBuildKind::Defense)
                    || runtimeProductionLaneForCatalogItem(state, *item)
                        != lane) {
                    errorMessage = "placement item/lane is not authorized";
                    return false;
                }
                return true;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySellCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayRepairCommand>) {
                const WorldObjectInstance* object =
                    findObjectById(state, body.objectId);
                if (object == nullptr
                    || object->kind != WorldObjectKind::Structure
                    || runtimeCanonicalHumanHouseOwner(object->owner)
                        != runtimeCanonicalHumanHouseOwner(owner)) {
                    errorMessage =
                        "structure action target is not owned by issuing house";
                    return false;
                }
                return true;
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetPrimaryFactoryCommand>
                || std::is_same_v<Body, RuntimeMultiplayerGameplaySetRallyPointCommand>) {
                if (!validateRuntimePlayerFactoryCommand(state, owner, body.objectId, errorMessage)) return false;
                if constexpr (std::is_same_v<Body, RuntimeMultiplayerGameplaySetRallyPointCommand>) {
                    if (!findRuntimeTerrainCellAt(state, {body.cell.x, body.cell.y})) {
                        errorMessage = "factory rally cell is outside the map"; return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplayPlayerCommunicationCommand>) {
                return validateRuntimePlayerCommunication(state, owner, body, errorMessage);
            } else if constexpr (std::is_same_v<Body, RuntimeMultiplayerGameplayWaypointCommand>) {
                return validateRuntimePlayerWaypointCommand(state, owner, body, errorMessage);
            } else {
                static_assert(std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayFireSuperWeaponCommand>);
                if (!state.superWeaponsInitialized
                    || state.superWeapons.findSidebarSnapshot(
                        owner,
                        body.superWeaponId
                    ) == nullptr) {
                    errorMessage =
                        "superweapon is not owned by issuing house";
                    return false;
                }
                for (const RuntimeMultiplayerGameplaySuperWeaponTarget& target :
                        body.targets) {
                    if (target.objectId.has_value()
                        && !validateRuntimeGameplayVisibleTarget(
                            state,
                            owner,
                            *target.objectId,
                            errorMessage
                        )) {
                        return false;
                    }
                }
                return true;
            }
        },
        command.body
    );
}

bool prevalidateRuntimeMultiplayerGameplayBatch(
    const RuntimeProductionHostState& state,
    std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
    std::string& errorMessage
) {
    std::unordered_map<std::string, RuntimeGameplayPeerBinding> bindings;
    if (!buildRuntimeGameplayPeerBindings(state, bindings, errorMessage)) {
        return false;
    }
    for (const RuntimeMultiplayerLockstepApplyCommand& item : batch) {
        const auto binding = bindings.find(item.peerId);
        if (binding == bindings.end()) {
            errorMessage = "gameplay batch contains an unknown peer";
            return false;
        }
        std::string codecError;
        if (!validateRuntimeMultiplayerGameplayCommand(
                item.command,
                codecError
            )) {
            errorMessage = "invalid gameplay command: " + codecError;
            return false;
        }
    }
    errorMessage.clear();
    return true;
}







bool applyRuntimeGameplaySuperWeaponCommand(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayFireSuperWeaponCommand& command
) {
    RuntimeHumanHouseScope houseScope(state, owner);
    if (!houseScope
        || !runtimeGameplaySuperWeaponReadyForCommand(
            state,
            owner,
            command
        )) {
        return false;
    }
    SuperWeaponWorldContext context = runtimeSuperWeaponContext(state);
    context.emit = runtimeSuperWeaponEventSink(state);
    std::string reason;
    const auto parasiteHostPoses=runtimeParasiteLinkedHostPoses(state);
    bool accepted = false;
    if (command.targets.size() == 2U) {
        accepted = state.superWeapons.submitTwoStageTarget(
            owner,
            command.superWeaponId,
            runtimeGameplaySuperWeaponTarget(command.targets[0]),
            runtimeGameplaySuperWeaponTarget(command.targets[1]),
            context,
            &reason
        );
    } else {
        accepted = state.superWeapons.beginTargetSelection(
            owner,
            command.superWeaponId,
            context,
            &reason
        );
        if (accepted) {
            const SuperWeaponTarget target = command.targets.empty()
                ? SuperWeaponTarget{}
                : runtimeGameplaySuperWeaponTarget(command.targets.front());
            accepted = state.superWeapons.submitTarget(
                owner,
                command.superWeaponId,
                target,
                context,
                &reason
            );
        }
    }
    (void)notifyRuntimeParasiteHostsRelocated(state,parasiteHostPoses);
    (void)drainRuntimeObjectExitQueue(state);
    return accepted;
}

bool placeRuntimeGameplayBuildingForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayPlaceBuildingCommand& command
) {
    const std::optional<RuntimeProductionLane> lane =
        runtimeGameplayProductionLane(command.lane);
    if (!lane.has_value()) {
        return false;
    }
    bool remote = false;
    bool placed = false;
    {
        RuntimeHumanHouseScope scope(state, owner);
        if (!scope) {
            return false;
        }
        RuntimeProductionLaneState& laneState =
            state.economy.localProductionLanes.lanes[
                runtimeProductionLaneIndex(*lane)
            ];
        if (!laneState.ready.has_value()
            || laneState.ready->serial != command.productionSerial
            || !runtimeProductionItemKeysMatch(
                laneState.ready->itemKey,
                command.buildingTypeId
            )) {
            return false;
        }
        state.economy.selectedPlacementLane = *lane;
        state.economy.selectedPlacementSerial = command.productionSerial;
        remote = scope.remote();
        placed = placeRuntimeReadyBuilding(
            state,
            {command.cell.x, command.cell.y}
        );
    }
    if (remote && placed) {
        rebuildRuntimeEconomyPower(state);
    }
    return placed;
}

#include "ui/input/RuntimePlayerCommands.inl"

bool applyRuntimeMultiplayerGameplayCommand(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayCommand& command
) {
    return std::visit(
        [&](const auto& body) {
            using Body = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayTargetOrderCommand>) {
                return applyRuntimePlayerTargetOrder(state, owner, body, !state.config.multiplayerActive && runtimeUsesGameplayIntentRouter(state));
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayStopCommand>) {
                state.playerCommands.cancel(body.objectIds);
                return applyRuntimeGameplaySelectionCommand(
                    state,
                    owner,
                    body.objectIds,
                    [&]() {
                        issueRuntimeStopCommand(state);
                        return state.lastCommand.valid
                            && state.lastCommand.acceptedCount > 0;
                    }
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayGuardCommand>) {
                state.playerCommands.cancel(body.objectIds);
                return applyRuntimeGameplaySelectionCommand(
                    state,
                    owner,
                    body.objectIds,
                    [&]() {
                        issueRuntimeGuardCommand(state);
                        return state.lastCommand.valid
                            && state.lastCommand.acceptedCount > 0;
                    }
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayScatterCommand>) {
                state.playerCommands.cancel(body.objectIds);
                return applyRuntimeGameplaySelectionCommand(
                    state,
                    owner,
                    body.objectIds,
                    [&]() {
                        issueRuntimeScatterCommand(state);
                        return state.lastCommand.valid
                            && state.lastCommand.acceptedCount > 0;
                    }
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySecondaryActionCommand>) {
                if (body.action != RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll
                    && body.action != RuntimeMultiplayerGameplaySecondaryActionKind::UndeployAll)
                    state.playerCommands.cancel(body.objectIds);
                return applyRuntimeGameplaySelectionCommand(
                    state,
                    owner,
                    body.objectIds,
                    [&]() {
                        return executeRuntimeSecondaryActions(state, body.objectIds, body.action);
                    }
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayQueueProductionCommand>) {
                const RuntimeBuildCatalogItem* catalog =
                    findRuntimeGameplayCatalogItem(state, owner, body.typeId);
                if (catalog == nullptr) {
                    return false;
                }
                const RuntimeBuildCatalogItem item = *catalog;
                return queueRuntimeBuildOrderForHumanOwner(
                    state,
                    owner,
                    item,
                    runtimeGameplayProductionInsertMode(body.insertMode)
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayCancelProductionCommand>) {
                const std::optional<RuntimeProductionLane> lane =
                    runtimeGameplayProductionLane(body.lane);
                return lane.has_value()
                    && cancelRuntimeBuildOrderForHumanOwner(
                        state,
                        owner,
                        *lane,
                        body.productionSerial
                    );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySetProductionHoldCommand>) {
                const std::optional<RuntimeProductionLane> lane =
                    runtimeGameplayProductionLane(body.lane);
                RuntimeHumanHouseScope scope(state, owner);
                if (!lane.has_value() || !scope) {
                    return false;
                }
                RuntimeProductionLaneState& laneState =
                    state.economy.localProductionLanes.lanes[
                        runtimeProductionLaneIndex(*lane)
                    ];
                if (!laneState.active.has_value()
                    || laneState.active->serial
                        != body.activeProductionSerial) {
                    return false;
                }
                return runtimeSetProductionHold(
                    state.economy.localProductionLanes,
                    *lane,
                    body.onHold
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayPlaceBuildingCommand>) {
                return placeRuntimeGameplayBuildingForHumanOwner(
                    state,
                    owner,
                    body
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySellCommand>) {
                return sellRuntimeStructureForHumanOwner(
                    state,
                    owner,
                    body.objectId
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayRepairCommand>) {
                return repairRuntimeStructureForHumanOwner(
                    state,
                    owner,
                    body.objectId
                );
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetPrimaryFactoryCommand>) {
                return applyRuntimePlayerPrimaryFactory(state, owner, body.objectId);
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetRallyPointCommand>) {
                return applyRuntimePlayerRallyPoint(state, owner, body.objectId, body.cell);
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplayPlayerCommunicationCommand>) {
                return applyRuntimePlayerCommunication(state, owner, body);
            } else if constexpr (std::is_same_v<Body, RuntimeMultiplayerGameplayWaypointCommand>) {
                return applyRuntimePlayerWaypointCommand(state, owner, body);
            } else {
                static_assert(std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayFireSuperWeaponCommand>);
                return applyRuntimeGameplaySuperWeaponCommand(
                    state,
                    owner,
                    body
                );
            }
        },
        command.body
    );
}

void recordRuntimeMultiplayerGameplayAuthorityResult(
    RuntimeProductionHostState& state,
    const RuntimeMultiplayerLockstepApplyCommand& item,
    const std::string& owner,
    bool applied
) {
    int& counter = applied
        ? state.multiplayerGameplayCommandsApplied
        : state.multiplayerGameplayCommandsNoOp;
    if (counter < std::numeric_limits<int>::max()) {
        ++counter;
    }
    state.multiplayerGameplayAuthorityLog.push_back(
        std::string(applied ? "applied " : "no-op ")
        + item.peerId + " owner=" + owner
        + " command=" + std::to_string(item.commandIndex)
    );
    constexpr std::size_t maximumLogEntries = 64;
    while (state.multiplayerGameplayAuthorityLog.size()
        > maximumLogEntries) {
        state.multiplayerGameplayAuthorityLog.pop_front();
    }
}

bool applyRuntimeMultiplayerGameplayBatch(
    RuntimeProductionHostState& state,
    std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
    std::string& errorMessage
) {
    if (!prevalidateRuntimeMultiplayerGameplayBatch(
            state,
            batch,
            errorMessage
        )) {
        return false;
    }
    std::unordered_map<std::string, RuntimeGameplayPeerBinding> bindings;
    if (!buildRuntimeGameplayPeerBindings(state, bindings, errorMessage)) {
        return false;
    }
    const std::vector<int> savedSelection = state.selectedObjectIds;
    const std::string savedInteractionStatus = state.interactionStatus;
    const std::string savedSimulationStatus = state.simulationStatus;
    const std::string savedLocalEconomyStatus = state.economy.status;
    const RuntimeSidebarTool savedSidebarTool = state.activeSidebarTool;
    const bool savedAudioSuppression =
        state.runtimeSuppressPlayerAudioFeedback;
    const auto restorePresentation = [&]() {
        state.selectedObjectIds.clear();
        for (int objectId : savedSelection) {
            if (findObjectById(state, objectId) != nullptr) {
                state.selectedObjectIds.push_back(objectId);
            }
        }
        state.interactionStatus = savedInteractionStatus;
        state.simulationStatus = savedSimulationStatus;
        state.economy.status = savedLocalEconomyStatus;
        state.activeSidebarTool = savedSidebarTool;
        state.runtimeSuppressPlayerAudioFeedback = savedAudioSuppression;
    };
    try {
        for (const RuntimeMultiplayerLockstepApplyCommand& item : batch) {
            const RuntimeGameplayPeerBinding& binding =
                bindings.at(item.peerId);
            std::string stateDependentReason;
            const bool stateDependentValid =
                prevalidateRuntimeMultiplayerGameplayCommand(
                    state,
                    binding.owner,
                    item.command,
                    stateDependentReason
                );
            const bool applied = stateDependentValid
                && applyRuntimeMultiplayerGameplayCommand(
                    state,
                    binding.owner,
                    item.command
                );
            // A command can become ordinarily illegal because a preceding
            // command in this canonical batch consumed/moved its target, or
            // because the command was authored against an older simulation
            // view.  Those state-dependent failures (including an ownership
            // mismatch) are deterministic no-ops, not a frame rejection.
            recordRuntimeMultiplayerGameplayAuthorityResult(
                state,
                item,
                binding.owner,
                applied
            );
        }
        restorePresentation();
    } catch (...) {
        restorePresentation();
        throw;
    }
    errorMessage.clear();
    return true;
}

}
