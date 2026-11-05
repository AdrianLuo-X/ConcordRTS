#include "runtime/session/detail/RuntimeSessionCommands.h"

namespace concord::runtime::session_detail {

int runtimeAuthoredFramesToFixedTicks(
    const RuntimeGameSessionState& state,
    int frames) {
    return std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(std::max(1, frames))
                * static_cast<double>(std::max(1, state.config.fixedTickHz))
                / 15.0
        ))
    );
}

void runtimeCaptureMassMovementRejection(
    const RuntimeGameSessionState& state, int objectId, const WorldObjectInstance* object,
    RuntimeCellPoint click, const RuntimeCellPoint* desired, const RuntimeCellPoint* candidate,
    std::string_view reason, std::string_view detail, const UnitType* resolvedType) {
    if (!RuntimeMassMovementRejectScope::matches(&state, state.tick)) return;
    RuntimeMassMovementRejectRecord record;
    record.tick = state.tick;
    record.objectId = objectId;
    record.clickX = click.x;
    record.clickY = click.y;
    record.hasDesired = desired != nullptr;
    if (desired) { record.desiredX = desired->x; record.desiredY = desired->y; }
    record.hasCandidate = candidate != nullptr;
    if (candidate) { record.candidateX = candidate->x; record.candidateY = candidate->y; }
    record.reason.assign(reason);
    record.detail.assign(detail);
    record.visionEnabled = state.visionEnabled;
    record.objectExists = object != nullptr;
    if (object) {
        record.objectKind = static_cast<int>(object->kind);
        record.alive = object->alive;
        record.health = object->health;
        record.currentX = object->x;
        record.currentY = object->y;
        record.type.assign(object->type);
        record.owner.assign(object->owner);
        const auto* type = resolvedType ? resolvedType : findRuntimeUnitType(state.rules, *object);
        if (type) {
            record.moveToShroud = type->moveToShroud ? 1 : 0;
            record.teleporter = type->teleporter ? 1 : 0;
        }
        record.chronoLocked = state.specialMechanisms.isChronoMovementLocked(objectId, state.tick) ? 1 : 0;
        record.ownerHasObserver = state.vision.hasObserver(object->owner);
        if (record.visionEnabled && record.ownerHasObserver) {
            record.currentMapped = state.vision.isCellExploredBy(object->owner, object->x, object->y) ? 1 : 0;
            record.clickMapped = state.vision.isCellExploredBy(object->owner, click.x, click.y) ? 1 : 0;
            if (desired) record.desiredMapped = state.vision.isCellExploredBy(object->owner, desired->x, desired->y) ? 1 : 0;
            if (candidate) record.candidateMapped = state.vision.isCellExploredBy(object->owner, candidate->x, candidate->y) ? 1 : 0;
        }
    }
    // Sink copies values only; no logging, allocation, actor retention or World write.
    RuntimeMassMovementRejectScope::capture(&state, state.tick, record);
}

void runtimeCaptureMassMovementRejectionById(
    const RuntimeGameSessionState& state, int objectId, RuntimeCellPoint click,
    const RuntimeCellPoint* desired, const RuntimeCellPoint* candidate,
    std::string_view reason, std::string_view detail, const UnitType* resolvedType) {
    if (!RuntimeMassMovementRejectScope::matches(&state, state.tick)) return;
    // Release/chrono transactions can replace object storage. Reacquire only
    // for an active observer instead of dereferencing a pre-transaction actor.
    runtimeCaptureMassMovementRejection(state, objectId, findObjectById(state, objectId),
        click, desired, candidate, reason, detail, resolvedType);
}

bool advanceRuntimeCommittedTick(RuntimeProductionHostState& state, int completedTick) {
    if (completedTick < 0
        || state.tick == std::numeric_limits<int>::max()
        || completedTick != state.tick + 1) {
        throw std::logic_error(
            "multiplayer committed tick is not the next runtime tick"
        );
    }
    state.tick = completedTick;
    const auto started = std::chrono::steady_clock::now();
    const bool changed = advanceRuntimeSimulation(state, false);
    state.perf.lastSimulationMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started
        ).count();
    return changed;
}

bool advanceRuntimeSimulation(
    RuntimeProductionHostState& state,
    bool inputDragLightweightMode) {
    RuntimePerfTraceScope trace("advanceRuntimeSimulation", state.tick);
    struct StoredSlaveWorkerSync {
        RuntimeProductionHostState& state;
        ~StoredSlaveWorkerSync() { synchronizeRuntimeStoredSlaveMinerWorkers(state); }
    } storedSlaveWorkerSync{state};
    RuntimeVisionTerrainCoordinateInvocationScope visionTerrainCoordinates(state.world);
    mergeRuntimeGrandCannonPrewarm(state);
    processRuntimeEvaQueue(state);
    bool evaSubtitleChanged = false;
    if (!state.runtimeEvaSubtitleText.empty()
        && state.runtimeEvaSubtitleUntilTick > 0
        && state.tick >= state.runtimeEvaSubtitleUntilTick) {
        clearRuntimeEvaSubtitle(state);
        evaSubtitleChanged = true;
    }
    runtimeResetLastTickCapabilityStats(state);
    state.actionSummary.movedThisTick = 0;
    state.actionSummary.localAvoidanceAttemptsThisTick = 0;
    state.actionSummary.localAvoidanceMovesThisTick = 0;
    state.actionSummary.attacksThisTick = 0;
    state.actionSummary.impactsThisTick = 0;
    state.actionSummary.destroyedObjects = 0;
    state.actionSummary.completedCommands = 0;
    state.actionSummary.repathsThisTick = 0;
    state.actionSummary.turretTurnsThisTick = 0;
    state.perf.lastSimEffectsMs = 0.0;
    state.perf.lastSimEconomyMs = 0.0;
    state.perf.lastSimTriggersMs = 0.0;
    state.perf.lastSimBuildMs = 0.0;
    state.perf.lastSimGuardMs = 0.0;
    state.perf.lastSimMoveMs = 0.0;
    state.perf.lastSimAttackMs = 0.0;
    state.perf.lastSimPathMs = 0.0;
    state.perf.lastSimVisionMs = 0.0;
    state.perf.lastSimRepathCount = 0;
    state.perf.lastSimPathRequests = 0;

    auto measureSimulationStep = [](auto&& step, double& outputMs) {
        const auto start = std::chrono::steady_clock::now();
        const bool changed = step();
        outputMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start
        ).count();
        return changed;
    };

    bool changed = advanceRuntimeModExtensions(state);
    changed = advanceRuntimeJumpjetFlights(state) || changed;
    changed = advanceRuntimeTransportUnloading(state) || changed;
    reconcileRuntimeAircraftFlights(state);
    changed = updateRuntimeVisionState(state) || evaSubtitleChanged || changed;
    changed = measureSimulationStep(
        [&state]() { return advanceRuntimeEffects(state); },
        state.perf.lastSimEffectsMs
    ) || changed;
    changed = syncRuntimeLightingSuperWeaponProfiles(state) || changed;
    changed = advanceRuntimeLighting(state) || changed;
    changed = advanceRuntimeParticles(state) || changed;
    // Psychic status expiry and target identity reconciliation must observe
    // impacts from this tick before AI, guard, team, or active behaviors make
    // another actor-specific targeting decision.
    changed = advanceRuntimePsychicStatuses(state) || changed;
    // Spy blackout expiry, newly produced default disguises, and detector
    // intelligence must advance even when there are no active behaviors.
    changed = advanceRuntimeSpies(state) || changed;
    changed = advanceRuntimeDiskDrains(state, true) || changed;
    // Expiry and temporary activity cleanup must precede queued orders:
    // a finished attack may leave camouflage bookkeeping until effects tick.
    changed = advanceRuntimePlayerCommands(state) || changed;
    const std::size_t selectionSizeBeforeClean =
        state.selectedObjectIds.size();
    cleanRuntimeSelection(state);
    cleanRuntimeLocalPlayerSelection(state);
    changed = state.selectedObjectIds.size() != selectionSizeBeforeClean
        || changed;
    changed = measureSimulationStep(
        [&state]() { return advanceRuntimeEconomy(state); },
        state.perf.lastSimEconomyMs
    ) || changed;
    changed = measureSimulationStep(
        [&state]() { return advanceRuntimeTriggers(state); },
        state.perf.lastSimTriggersMs
    ) || changed;
    changed = completeRuntimeWorldRefresh(state) || changed;
    changed = advanceRuntimeWorldAudio(state) || changed;
    changed = measureSimulationStep(
        [&state]() { return advanceRuntimeBuildAnimations(state); },
        state.perf.lastSimBuildMs
    ) || changed;
    changed = advanceRuntimeStructureRepacks(state) || changed;
    changed = advanceRuntimePanics(state) || changed;
    changed = advanceRuntimeInfantryProneStates(state) || changed;
    changed = advanceRuntimeWeaponCooldowns(state) || changed;
    changed = advanceRuntimeCombatBursts(state) || changed;
    changed = advanceRuntimeCombatIndependentFire(state) || changed;
    changed = advanceRuntimeMcvDeployTurns(state) || changed;
    changed = advanceRuntimeDeployTransitions(state) || changed;
    changed = ensureRuntimeAreaFireBehaviors(state) || changed;
    changed = advanceRuntimeIdleVariants(state) || changed;
    changed = advanceRuntimeFactoryExitAnimations(state) || changed;
    changed = advanceRuntimeIdleWeaponFacings(state) || changed;
    changed = advanceRuntimeTankBunkers(state) || changed;
    changed = advanceRuntimeGarrisonState(state) || changed;
    changed = advanceRuntimeGattlingWeaponTimers(state) || changed;
    if (!inputDragLightweightMode) {
        changed = advanceRuntimeComputerAi(state) || changed;
        changed = measureSimulationStep(
            [&state]() { return advanceRuntimeGuardBehavior(state); },
            state.perf.lastSimGuardMs
        ) || changed;
    }
    changed = advanceRuntimeSkirmishCrates(state) || changed;
    changed = runtimeSidebarPowerAnimationActive(state) || changed;
    changed = completeRuntimeWorldRefresh(state) || changed;
    changed = advanceRuntimeSkirmishOutcome(state) || changed;
    if (state.skirmishMatchFinished) {
        updateRuntimeVisionState(state);
        refreshRuntimeActionSummary(state);
        finishRuntimeTurretSoundActivities(state, state.world);
        return true;
    }

    // Gate phases advance before path processing and before the empty-
    // behavior early return.  A gate with no active mover may still finish
    // opening or close after its authored delay.
    changed = advanceRuntimeGates(state) || changed;
    // Repair timers, queue promotion and invalid-relation release must also
    // advance when no ordinary Move/Attack behaviors remain.
    // Release completed factory egress claims first so a naval repair order
    // may reserve the newly free shared dock in this same fixed tick.
    changed = advanceRuntimeFactoryDeliveries(state) || changed;
    changed = advanceRuntimeRepairDepots(state) || changed;
    // Landed aircraft reload and repair from the shared dock mirror. Keep the
    // service clock ahead of path processing and the empty-behavior return.
    changed = advanceRuntimeAircraftServices(state) || changed;
    changed = completeRuntimeWorldRefresh(state) || changed;

    if (!inputDragLightweightMode) {
        const auto pathQueueStart = std::chrono::steady_clock::now();
        changed = processRuntimePathRequests(state) || changed;
        state.perf.lastSimMoveMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - pathQueueStart
            ).count();
    }

    if (state.behaviors.empty()) {
        changed = advanceRuntimeOmniCrushContacts(state) || changed;
        changed = updateRuntimeVisionState(state) || changed;
        refreshRuntimeActionSummary(state);
        finishRuntimeTurretSoundActivities(state, state.world);
        return changed;
    }

    if (inputDragLightweightMode) {
        changed = updateRuntimeVisionState(state) || changed;
        refreshRuntimeActionSummary(state);
        finishRuntimeTurretSoundActivities(state, state.world);
        return changed;
    }

    std::vector<int> completedObjectIds;
    std::vector<int> behaviorIds;
    behaviorIds.reserve(state.behaviors.size());
    for (const auto& [objectId, behavior] : state.behaviors) {
        (void)behavior;
        behaviorIds.push_back(objectId);
    }
    std::sort(behaviorIds.begin(), behaviorIds.end());
    prepareRuntimeInfantryStepReservations(state);

    std::vector<WorldObjectInstance>& behaviorObjects =
        state.world.accessObjects();
    const std::vector<std::size_t> initialBehaviorObjectOffsets =
        buildRuntimeBehaviorObjectOffsets(
            behaviorObjects, behaviorIds, state.objectIndex);

    {
    RuntimeInfantryWaterBehaviorContextScope waterMetadataContext(state);
    for (std::size_t behaviorIndex = 0; behaviorIndex < behaviorIds.size(); ++behaviorIndex) {
        const int objectId = behaviorIds[behaviorIndex];
        auto behaviorIt = state.behaviors.find(objectId);
        if (behaviorIt == state.behaviors.end()) {
            continue;
        }

        WorldObjectInstance* object = runtimeBehaviorObjectAtInitialOffset(
            behaviorObjects, objectId, initialBehaviorObjectOffsets[behaviorIndex]);
        if (object == nullptr) {
            completedObjectIds.push_back(objectId);
            changed = true;
            continue;
        }
        if (runtimeComputerAiPausedForOwner(state, object->owner)) {
            clearRuntimeRenderPosition(*object);
            continue;
        }
        if (runtimeObjectManagedByAirstrike(state, objectId)) {
            clearRuntimeRenderPosition(*object);
            completedObjectIds.push_back(objectId);
            changed = true;
            continue;
        }
        if (runtimeObjectDisabledByParasite(state, objectId)) {
            object->mission = state.specialMechanisms.isParasiteHostParalyzed(
                objectId
            ) ? "ParasiteParalyzed" : "ParasiteStunned";
            clearRuntimeRenderPosition(*object);
            completedObjectIds.push_back(objectId);
            changed = true;
            continue;
        }

        [[maybe_unused]] RuntimeObjectBehavior& behavior = behaviorIt->second;
        if (runtimeStructureIsRepacking(*object)) continue;
        const auto chronoTransport = state.pendingTransportTargets.find(objectId);
        const auto* chronoHost = chronoTransport != state.pendingTransportTargets.end()
            ? findObjectById(state, chronoTransport->second) : nullptr;
        if (chronoHost && runtimeChronoTransportArrivalReady(state, *object, *chronoHost)
            && completeRuntimeTransportEntry(state, objectId)) {
            completedObjectIds.push_back(objectId);
            changed = true;
            continue;
        }
        if (holdRuntimeChronoRecovery(state, *object, behavior)) continue;
        if (behavior.kind == RuntimeBehaviorKind::Move) {
            // Recheck entry before taking another step. Another engineer or
            // automatic repair can fill the target while this one is walking.
            const auto engineerIntent = state.pendingEngineerCaptures.find(
                objectId
            );
            if (engineerIntent != state.pendingEngineerCaptures.end()) {
                const WorldObjectInstance* target = findObjectById(
                    state, engineerIntent->second
                );
                const auto actionIt = state.pendingEngineerActions.find(objectId);
                const RuntimeEngineerAction queuedAction = actionIt
                        == state.pendingEngineerActions.end()
                    ? RuntimeEngineerAction::Capture : actionIt->second;
                if (target == nullptr || queuedAction == RuntimeEngineerAction::None
                    || runtimeEngineerActionForStructure(state, *object, *target)
                        != queuedAction) {
                    if (!eraseRuntimeBehavior(state, objectId)) continue;
                    clearRuntimeRenderPosition(*object);
                    setRuntimeMissionIntent(
                        state, *object, RuntimeMissionIntent::Idle,
                        "engineerEntryUnavailable"
                    );
                    changed = true;
                    continue;
                }
            }
            if (behavior.weaponCooldownTicks > 0) {
                --behavior.weaponCooldownTicks;
            }
            const auto moveStart = std::chrono::steady_clock::now();
            changed = advanceRuntimeMoveBehavior(state, *object, behavior) || changed;
            state.perf.lastSimMoveMs +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - moveStart
                ).count();
            const bool physicallyDockedAircraft =
                !object->airborne
                && object->dockedToObjectId >= 0
                && object->dockIndex >= 0
                && toLowerAsciiLocal(object->mission) == "dock";
            const bool boardingBodyArrived = runtimeTransportBoardingReadyForManifest(state, *object, behavior);
            const auto pendingC4 = state.pendingTimedBombIntents.find(objectId);
            const bool settledC4Body = !object->renderMoveActive
                && pendingC4 != state.pendingTimedBombIntents.end()
                && pendingC4->second.kind == RuntimeTimedBombIntentKind::C4
                && pendingC4->second.targetObjectId == behavior.targetObjectId;
            if (behavior.state == RuntimeBehaviorState::Complete
                && (runtimeCellPointForObject(*object) == behavior.moveTarget || boardingBodyArrived)
                && behavior.moveCooldownTicks <= 0
                && (!object->hasRenderPosition || physicallyDockedAircraft || boardingBodyArrived || settledC4Body
                    || (!object->renderMoveActive && findRuntimeRepairDepotRelationForUnit(state.repairDepots, objectId))
                    || (runtimeJumpjetProfile(state, *object) && !object->renderMoveActive)
                    || (!object->renderMoveActive
                        && runtimeTransportUnitBoardingPending(state, objectId)
                        && behavior.pendingAllowedOccupiedObjectId
                            == state.pendingTransportTargets.at(objectId)))) {
                if (!physicallyDockedAircraft) {
                    const RuntimeRepairDepotArrivalResult repairArrival =
                        completeRuntimeRepairDepotArrival(state, objectId);
                    if (repairArrival
                        != RuntimeRepairDepotArrivalResult::NotPending) {
                        changed = true;
                        if (repairArrival
                            == RuntimeRepairDepotArrivalResult::Attached) {
                            completedObjectIds.push_back(objectId);
                        }
                        continue;
                    }
                    const RuntimeTimedBombArrivalResult timedBombArrival =
                        completeRuntimeTimedBombIntent(state, objectId);
                    if (timedBombArrival
                            != RuntimeTimedBombArrivalResult::NotPending) {
                        changed = true;
                        if (timedBombArrival
                                == RuntimeTimedBombArrivalResult::Retargeted) {
                            continue;
                        }
                        // A legal stopped outside pose also owns its exact foot.
                        if (object->renderMoveActive) clearRuntimeRenderPosition(*object);
                        completedObjectIds.push_back(objectId);
                        continue;
                    }
                    const bool transportEntryPending =
                        state.pendingTransportTargets.find(objectId)
                            != state.pendingTransportTargets.end();
                    if (runtimeGrindingRelationForSource(
                            state.grinding,
                            objectId
                        ) != nullptr) {
                        const bool grindingEntryCompleted =
                            completeRuntimeGrindingEntry(state, objectId);
                        changed = grindingEntryCompleted || changed;
                        if (grindingEntryCompleted) {
                            completedObjectIds.push_back(objectId);
                        }
                        // A rejected detached completion leaves the queued
                        // relation and its completed path intact for retry.
                        // Do not settle the actor to Idle or erase the only
                        // entry authority in the generic completion tail.
                        continue;
                    }
                    if (completeRuntimeInfantryService(state, objectId)) {
                        completedObjectIds.push_back(objectId);
                        changed = true;
                        continue;
                    }
                    if (completeRuntimeSpyInfiltration(state, objectId)) {
                        completedObjectIds.push_back(objectId);
                        changed = true;
                        continue;
                    }
                    if (!runtimeTransportUnitBoardingPending(state, objectId)
                        && !runtimeTransportUnloadingPending(state, *object)) {
                        setRuntimeMissionIntent(
                            state,
                            *object,
                            RuntimeMissionIntent::Idle,
                            "moveCompleteSettled"
                        );
                        clearRuntimeRenderPosition(*object);
                    }
                    const bool engineerEntryPending =
                        state.pendingEngineerCaptures.contains(objectId);
                    if (engineerEntryPending) {
                        const bool engineerEntryCompleted =
                            completeRuntimeEngineerCapture(state, objectId);
                        changed = engineerEntryCompleted || changed;
                        if (engineerEntryCompleted) {
                            // The owner transaction swaps world and behavior
                            // storage.  Do not touch the pre-commit object or
                            // behavior references after a successful entry.
                            completedObjectIds.push_back(objectId);
                            continue;
                        }
                        if (state.pendingEngineerCaptures.contains(objectId)) {
                            WorldObjectInstance* retryEngineer =
                                findMutableObjectById(state, objectId);
                            if (retryEngineer != nullptr
                                && retryEngineer->alive) {
                                setRuntimeMissionIntent(
                                    state,
                                    *retryEngineer,
                                    RuntimeMissionIntent::Entering,
                                    "engineerEntryRetry"
                                );
                            }
                            continue;
                        }
                    }
                    if (completeRuntimeOccupyEntry(state, objectId)) {
                        // Claiming a neutral garrison swaps object/behavior
                        // storage just like an engineer capture.
                        completedObjectIds.push_back(objectId);
                        changed = true;
                        continue;
                    }
                    if (state.pendingOccupyTargets.contains(objectId)) {
                        if (auto* retryOccupier = findMutableObjectById(state, objectId)) {
                            setRuntimeMissionIntent(state, *retryOccupier,
                                RuntimeMissionIntent::Entering, "occupyEntryRetry");
                        }
                        continue;
                    }
                    const bool transportEntryCompleted =
                        completeRuntimeTransportEntry(state, objectId);
                    changed = transportEntryCompleted || changed;
                    if (!transportEntryCompleted && object->renderMoveActive
                        && behavior.moveCooldownTicks > 0) { changed = true; continue; }
                    if (transportEntryPending
                        && !transportEntryCompleted
                        && state.pendingTransportTargets.find(objectId)
                            != state.pendingTransportTargets.end()) {
                        const int transportId =
                            state.pendingTransportTargets.at(objectId);
                        WorldObjectInstance* transport =
                            findMutableObjectById(state, transportId);
                        if (transport == nullptr
                            || !runtimeObjectCanEnterTransport(
                                state,
                                *object,
                                *transport,
                                false
                            )) {
                            state.pendingTransportTargets.erase(objectId);
                        } else {
                            const std::optional<RuntimeCellPoint> entryCell =
                                runtimeTransportEntryCell(state, *object, *transport);
                            if (entryCell.has_value()) {
                                const RuntimeCellPoint currentCell =
                                    runtimeCellPointForObject(*object);
                                const std::vector<RuntimeCellPoint> entryPath =
                                    findRuntimePath(
                                        state,
                                        *object,
                                        currentCell,
                                        *entryCell,
                                        transportId
                                    );
                                if (currentCell != *entryCell
                                    && entryPath.empty()) {
                                    state.pendingTransportTargets.erase(
                                        objectId
                                    );
                                    setRuntimeMissionIntent(
                                        state,
                                        *object,
                                        RuntimeMissionIntent::Idle,
                                        "transportRetargetBlocked"
                                    );
                                } else {
                                    behavior.moveTarget = *entryCell;
                                    behavior.pendingAllowedOccupiedObjectId =
                                        transportId;
                                    replaceRuntimeBehaviorPath(
                                        behavior,
                                        entryPath
                                    );
                                    behavior.state =
                                        RuntimeBehaviorState::Queued;
                                    behavior.blockedTicks = 0;
                                    setRuntimeMissionIntent(
                                        state,
                                        *object,
                                        RuntimeMissionIntent::Entering,
                                        "transportRetarget"
                                    );
                                    changed = true;
                                    continue;
                                }
                            } else {
                                const bool peerBoardingActive = transport->kind == WorldObjectKind::Unit
                                    && !transport->airborne && !transport->renderMoveActive
                                    && object->alive && object->health > 0 && !object->airborne
                                    && behavior.kind == RuntimeBehaviorKind::Move
                                    && behavior.pendingAllowedOccupiedObjectId == transportId
                                    && std::any_of(state.pendingTransportTargets.begin(),
                                        state.pendingTransportTargets.end(), [&](const auto& entry) {
                                            if (entry.first == objectId || entry.second != transportId) return false;
                                            const auto peerMove = state.behaviors.find(entry.first);
                                            const auto* peer = findObjectById(state, entry.first);
                                            return peer && peer->alive && peer->health > 0 && !peer->airborne
                                                && peer->renderMoveActive && peerMove != state.behaviors.end()
                                                && peerMove->second.kind == RuntimeBehaviorKind::Move
                                                && peerMove->second.pendingAllowedOccupiedObjectId == transportId
                                                && peerMove->second.transportTransferKind == RuntimeTransportTransferKind::Boarding
                                                && peerMove->second.transportTransferHostId == transportId
                                                && peerMove->second.renderTo == runtimeCellPointForObject(*transport)
                                                && peerMove->second.renderMoveTicksRemaining > 0
                                                && peerMove->second.moveCooldownTicks > 0;
                                        });
                                if (peerBoardingActive) {
                                    setRuntimeMissionIntent(state, *object, RuntimeMissionIntent::Entering,
                                        "transportBoardingPeerBusy");
                                    changed = true;
                                    continue;
                                }
                                state.pendingTransportTargets.erase(objectId);
                                setRuntimeMissionIntent(
                                    state,
                                    *object,
                                    RuntimeMissionIntent::Idle,
                                    "transportRetargetUnavailable"
                                );
                            }
                            changed = true;
                        }
                    }
                }
                completedObjectIds.push_back(objectId);
            }
        } else if (behavior.kind == RuntimeBehaviorKind::Attack) {
            const auto attackStart = std::chrono::steady_clock::now();
            const bool behaviorChanged =
                advanceRuntimeAttackBehavior(state, *object, behavior);
            state.perf.lastSimAttackMs +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - attackStart
                ).count();
            changed = behaviorChanged || changed;
            if (behavior.state == RuntimeBehaviorState::Complete) {
                const UnitType* ammoRules = runtimeAmmoRulesForObject(
                    state.rules,
                    *object
                );
                const bool emptyWithoutManualReload =
                    ammoRules != nullptr
                    && ammoRules->ammo >= 0
                    && object->currentAmmo <= 0
                    && !ammoRules->manualReload;
                const RuntimeAircraftDockReturnResult dockReturn =
                    emptyWithoutManualReload
                    ? RuntimeAircraftDockReturnResult::NotApplicable
                    : queueRuntimeAircraftReturnToDock(
                        state,
                        *object,
                        behavior,
                        "aircraftAttackReturnQueued"
                    );
                changed =
                    dockReturn != RuntimeAircraftDockReturnResult::NotApplicable
                    || changed;
                if (dockReturn
                        == RuntimeAircraftDockReturnResult::NotApplicable
                    || dockReturn
                        == RuntimeAircraftDockReturnResult::Docked) {
                    completedObjectIds.push_back(objectId);
                }
            }
        } else {
            completedObjectIds.push_back(objectId);
        }
    }
    }

    // Exit finalization may erase the behavior being advanced.  Drain only
    // after the iteration has released every object/behavior reference.
    changed = drainRuntimeObjectExitQueue(state) || changed;
    for (int objectId : completedObjectIds) {
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        const auto behaviorIt = state.behaviors.find(objectId);
        if (object && behaviorIt != state.behaviors.end()
            && runtimeCancelledStructureEntryNeedsRecovery(state, *object, behaviorIt->second)
            && !recoverRuntimeCancelledStructureEntry(state, *object, behaviorIt->second)) continue;

        if (behaviorIt != state.behaviors.end()
            && behaviorIt->second.kind == RuntimeBehaviorKind::Move
            && behaviorIt->second.weaponCooldownTicks > 0
            && object != nullptr
            && object->alive) {
            RuntimeObjectBehavior cooldown;
            cooldown.kind = RuntimeBehaviorKind::Attack;
            cooldown.state = RuntimeBehaviorState::Cooldown;
            cooldown.targetObjectId = -1;
            cooldown.weaponCooldownTicks =
                behaviorIt->second.weaponCooldownTicks;
            state.behaviors[objectId] = std::move(cooldown);
            continue;
        }
        const auto sortie = state.specialMechanisms.carrierChildren().find(objectId);
        if (auto* child = findMutableObjectById(state, objectId);
            child && child->airborne && sortie != state.specialMechanisms.carrierChildren().end()
            && sortie->second.phase != CarrierChildPhase::Docked
            && sortie->second.phase != CarrierChildPhase::Destroyed
            && !state.rules.findSpawnedMissileProfile(child->type)) {
            (void)state.specialMechanisms.requestCarrierChildReturn(
                objectId, state.tick, runtimeSpecialEventSink(state), "aircraft attack completed");
            child->mission = "CarrierReturning";
            clearRuntimeCarrierAttackPreservingPose(state, *child);
        } else {
            finishRuntimePanicMovementLeg(state, objectId);
        }
    }
    // A delivery-owned Move may reach its rally during the behavior pass
    // above. Release that lease before exposing this tick boundary; otherwise
    // the completed behavior is erased while the saved authority still
    // requires it until the next tick.
    changed = advanceRuntimeFactoryDeliveries(state) || changed;

    changed = advanceRuntimeOmniCrushContacts(state) || changed;

    const RuntimeSubcellReconcileReport subcellReconcile =
        reconcileRuntimeSubcells(state, true);
    changed = subcellReconcile.reassignedInfantry > 0
        || subcellReconcile.needsMoveOut > 0
        || subcellReconcile.droppedReservations > 0
        || changed;

    cleanRuntimeSelection(state);
    changed = cleanRuntimeLocalPlayerSelection(state) || changed;
    changed = completeRuntimeWorldRefresh(state) || changed;
    // Movement can enter or leave a powered detector radius in this pass.
    // Refresh the owner-shared detection projection before exposing the tick.
    changed = advanceRuntimeSpies(state) || changed;
    changed = updateRuntimeVisionState(state) || changed;
    refreshRuntimeActionSummary(state);
    if (state.behaviors.empty() && !changed) {
        state.simulationStatus = "simulation idle";
    }
    for (auto& object : state.world.accessObjects()) {
        applyRuntimeJumpjetPose(state, object);
        applyRuntimeFixedWingPose(state, object);
    }
    finishRuntimeTurretSoundActivities(state, state.world);
    return changed;
}

void mergeRuntimeGrandCannonPrewarm(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("mergeRuntimeGrandCannonPrewarm",state.tick);
    if (state.prewarmMerge) state.prewarmMerge->merge(state.renderCache,state.tick);
}

}
