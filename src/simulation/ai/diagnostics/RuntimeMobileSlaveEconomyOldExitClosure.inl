bool s17OldValidateRuntimePadAircraftProductionProjection(
    const RuntimeWindowState& state,
    std::string& errorMessage
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

    const RuntimeAircraftProductionMatchResult matched =
        runtimeAircraftProductionMatchForOwner(
            state,
            state.economy.owner
        );
    if (!matched.inputValid || !matched.feasible) {
        errorMessage = matched.reason.empty()
            ? "saved pad aircraft production is not feasible"
            : "pad aircraft production is not feasible: " + matched.reason;
        return false;
    }
    return true;
}

bool s17OldDrainRuntimeObjectExitQueue(RuntimeWindowState& state) {
    if (state.drainingObjectExits) {
        return false;
    }
    RuntimePerfTraceScope stageTrace("s17OldDrainRuntimeObjectExitQueue", state.tick);
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
            const RuntimeObjectExitResult result = finalizeRuntimeObjectExit(state, request);
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
                state.economy.owner
            );
        bool hasLocalPadOrders = false;
        for (const RuntimeProductionLane lane : runtimeProductionLanes) {
            const RuntimeProductionLaneState& laneState =
                state.economy.localProductionLanes.lanes[
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
            beforeLanes.emplace(state.economy.localProductionLanes);
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
                        state.economy.owner
                    );
                RuntimeAircraftProductionMatchInputBuild inputBuild =
                    buildRuntimeAircraftProductionRecoveryInputForOwner(
                        state,
                        owner,
                        localOwner
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
                                state.economy.localProductionLanes,
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
                && !s17OldValidateRuntimePadAircraftProductionProjection(
                    state,
                    normalizationError
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
                state.economy.localProductionLanes =
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
            depositRuntimeCredits(state, refund);
            state.economy.status =
                "canceled " + std::to_string(canceledCount)
                + " PadAircraft order(s) after support host loss";
        }
    }
    return changed;
}

