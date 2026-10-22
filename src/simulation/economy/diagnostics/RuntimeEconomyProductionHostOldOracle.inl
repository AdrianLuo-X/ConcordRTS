// Literal pre-S19 complete callers and helper closure; only names prefixed.
int s19OldCreditBalanceAfterDeposit(
    int currentBalance,
    long long deposit
) {
    const int balance = std::clamp(
        currentBalance,
        0,
        runtimeMaximumCreditBalance
    );
    if (deposit <= 0) {
        return balance;
    }
    const long long available =
        static_cast<long long>(runtimeMaximumCreditBalance - balance);
    if (deposit >= available) {
        return runtimeMaximumCreditBalance;
    }
    return balance + static_cast<int>(deposit);
}

void s19OldDepositRuntimeCredits(
    RuntimeWindowState& state,
    long long amount
) {
    state.economy.credits = s19OldCreditBalanceAfterDeposit(
        state.economy.credits,
        amount
    );
}

int s19OldScaleResourceValue(
    int value,
    int acceptedUnits,
    int totalUnits
) {
    if (value <= 0 || acceptedUnits <= 0 || totalUnits <= 0) {
        return 0;
    }
    if (acceptedUnits >= totalUnits) {
        return value;
    }
    const long long scaled =
        (static_cast<long long>(value) * acceptedUnits + totalUnits - 1)
        / totalUnits;
    return static_cast<int>(std::max<long long>(0, scaled));
}

bool s19OldSpendRuntimeCredits(
    RuntimeWindowState& state,
    int requestedAmount
) {
    const int amount = std::max(0, requestedAmount);
    if (state.economy.credits < amount) {
        return false;
    }

    state.economy.credits -= amount;
    const int previousStoredValue =
        std::max(0, state.economy.storedResourceValue);
    const int storedSpend = std::min(amount, previousStoredValue);
    if (storedSpend <= 0) {
        return true;
    }

    const int remainingStoredValue = previousStoredValue - storedSpend;
    state.economy.storedResourceUnits = s19OldScaleResourceValue(
        std::max(0, state.economy.storedResourceUnits),
        remainingStoredValue,
        previousStoredValue
    );
    state.economy.storedResourceGemUnits = std::clamp(
        s19OldScaleResourceValue(
            std::max(0, state.economy.storedResourceGemUnits),
            remainingStoredValue,
            previousStoredValue
        ),
        0,
        state.economy.storedResourceUnits
    );
    state.economy.storedResourceValue = remainingStoredValue;
    return true;
}

bool s19OldAdvanceRuntimeProductionEconomy(RuntimeWindowState& state) {
    RuntimePerfTraceScope trace("s19OldAdvanceRuntimeProductionEconomy", state.tick);
    RuntimeProductionLanesState staged =
        state.economy.localProductionLanes;
    const std::uint64_t previousHash =
        runtimeProductionLanesStateHash(staged);
    const int creditsBefore = std::max(0, state.economy.credits);
    int stagedCredits = creditsBefore;

    const auto resolveCatalog = [&state](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) -> std::optional<RuntimeProductionCatalogAdapter> {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            runtimeProductionCatalogLookup(state, order.itemKey);
        if (!catalog.has_value()
            || !runtimeProductionOrderMatchesCatalogSnapshot(
                state,
                lane,
                order,
                *catalog
            )) {
            return std::nullopt;
        }
        return catalog;
    };
    const auto poweredCompatibleFactoryCount = [&state, &resolveCatalog](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            resolveCatalog(lane, order);
        if (!catalog.has_value()) {
            return 0;
        }
        if (runtimeBuildItemUsesPadAircraft(state, *catalog->item)) {
            const std::optional<RuntimeAircraftProductionAssignment>
                assignment = runtimeAircraftProductionAssignmentForSerial(
                    state,
                    state.economy.owner,
                    order.serial
                );
            const WorldObjectInstance* host = assignment.has_value()
                ? findObjectById(state, assignment->hostId)
                : nullptr;
            const BuildingType* building = host == nullptr
                ? nullptr
                : findRuntimeBuildingTypeById(state.rules, host->type);
            if (!assignment.has_value()
                || !runtimeProductionItemKeysMatch(
                    assignment->typeId,
                    catalog->item->typeId
                )
                || host == nullptr
                || building == nullptr
                || (building->powered && !host->poweredOperational)) {
                return 0;
            }

            // The exact assigned host gates progress, but every additional
            // compatible powered aircraft factory still contributes the
            // authored MultipleFactory speed multiplier. Count structures,
            // not docks: NumberOfDocks is physical capacity, not a factory
            // multiplier.
            int compatiblePoweredHosts = 0;
            for (const WorldObjectInstance& candidate :
                    state.world.getObjects()) {
                const BuildingType* candidateBuilding =
                    findRuntimeBuildingTypeById(
                        state.rules,
                        candidate.type
                    );
                if (candidateBuilding == nullptr
                    || (candidateBuilding->powered
                        && !candidate.poweredOperational)
                    || !runtimeAircraftCanDockAtStructureForOwner(
                        state,
                        *catalog->item,
                        candidate,
                        state.economy.owner
                    )) {
                    continue;
                }
                ++compatiblePoweredHosts;
                if (compatiblePoweredHosts
                    == runtimeProductionTimingMaximumFactories) {
                    break;
                }
            }
            return compatiblePoweredHosts;
        }
        return static_cast<int>(findRuntimeFactoryAnchorCandidatesForOwner(
            state,
            *catalog->item,
            state.economy.owner,
            false
        ).size());
    };
    RuntimeProductionAdvanceHooks hooks;
    hooks.canStart = [&state, &resolveCatalog, &poweredCompatibleFactoryCount](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            resolveCatalog(lane, order);
        return catalog.has_value()
            && evaluateRuntimeTechTreeDecision(
                state,
                *catalog->item
            ).canQueue
            && poweredCompatibleFactoryCount(lane, order) > 0;
    };
    hooks.canContinue = [&resolveCatalog, &poweredCompatibleFactoryCount](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) {
        return resolveCatalog(lane, order).has_value()
            && poweredCompatibleFactoryCount(lane, order) > 0;
    };
    hooks.productionRate = [
        &state,
        &resolveCatalog,
        &poweredCompatibleFactoryCount
    ](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            resolveCatalog(lane, order);
        if (!catalog.has_value()) {
            return RuntimeProductionRate{0, 0};
        }
        RuntimeBuildCatalogItem pricedItem = *catalog->item;
        pricedItem.cost = order.cost;
        const RuntimeProductionTimingResult timing =
            runtimeProductionTimingForItem(
                state,
                pricedItem,
                poweredCompatibleFactoryCount(lane, order),
                static_cast<double>(state.economy.powerProduced),
                static_cast<double>(state.economy.powerUsed)
            );
        if (!timing.valid) {
            return RuntimeProductionRate{0, 0};
        }
        return runtimeSidebarProductionRate(pricedItem.kind, timing.productionRate);
    };
    hooks.completeImmediately = [&state](RuntimeProductionLane, const RuntimeProductionLaneOrder&) {
        return runtimeNoCooldownForOwner(state, state.economy.owner);
    };
    const std::vector<RuntimeProductionAdvanceEvent> events =
        runtimeAdvanceProductionLanes(staged, stagedCredits, hooks);
    const int creditsDebited = creditsBefore - stagedCredits;
    const int eventDebits = std::accumulate(
        events.begin(),
        events.end(),
        0,
        [](int total, const RuntimeProductionAdvanceEvent& event) {
            return total + std::max(0, event.creditsDebited);
        }
    );
    if (creditsDebited < 0
        || eventDebits != creditsDebited
        || (creditsDebited > 0
            && !s19OldSpendRuntimeCredits(state, creditsDebited))) {
        state.economy.status = "production ledger transaction rejected";
        return false;
    }
    state.economy.localProductionLanes = std::move(staged);
    bool changed = previousHash != runtimeProductionLanesStateHash(
        state.economy.localProductionLanes
    );

    for (const RuntimeProductionAdvanceEvent& event : events) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            runtimeProductionCatalogLookup(state, event.itemKey);
        const RuntimeBuildCatalogItem* item =
            catalog.has_value() ? catalog->item : nullptr;
        switch (event.kind) {
        case RuntimeProductionAdvanceKind::Started:
            state.economy.status =
                "building " + event.itemKey
                + " ticks=" + std::to_string(event.remainingTicks);
            emitRuntimeSound(
                state,
                item != nullptr
                    && item->kind == RuntimeBuildKind::Infantry
                    ? runtimeSoundIntentTrainingStarted
                    : runtimeSoundIntentBuildStarted
            );
            break;
        case RuntimeProductionAdvanceKind::WaitingForCredits:
            state.economy.status =
                "waiting credits for " + event.itemKey;
            break;
        case RuntimeProductionAdvanceKind::Blocked:
            state.economy.status =
                "production paused " + event.itemKey;
            break;
        case RuntimeProductionAdvanceKind::Held:
            state.economy.status = "on hold " + event.itemKey;
            break;
        case RuntimeProductionAdvanceKind::PlacementReady:
            state.economy.status =
                event.itemKey
                + " ready: click sidebar icon to place; sidebar right-click cancels production";
            state.interactionStatus = state.economy.status;
            emitRuntimeSound(state, runtimeSoundIntentBuildComplete);
            break;
        default:
            break;
        }
    }

    clearRuntimeInvalidPlacementSelection(state);
    const std::vector<RuntimeProductionCompletedOrder> completed =
        runtimeEnumerateCompletedProduction(
            state.economy.localProductionLanes
        );
    for (const RuntimeProductionCompletedOrder& pending : completed) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            runtimeProductionCatalogLookup(state, pending.order.itemKey);
        if (!catalog.has_value()
            || catalog->item == nullptr
            || catalog->lane != pending.lane) {
            state.economy.status =
                "production completion catalog mismatch "
                + pending.order.itemKey;
            continue;
        }
        const std::optional<RuntimeProductionLaneOrder> preflight =
            runtimePeekCompletedProduction(
                state.economy.localProductionLanes,
                pending.lane
            );
        if (!preflight.has_value()
            || preflight->serial != pending.order.serial) {
            state.economy.status =
                "production completion serial changed before commit";
            continue;
        }
        if (!completeRuntimeProductionOrder(
                state,
                *catalog->item,
                false,
                pending.lane,
                pending.order.serial
            )) {
            continue;
        }
        const std::optional<RuntimeProductionLaneOrder> committed =
            runtimeCommitCompletedProduction(
                state.economy.localProductionLanes,
                pending.lane,
                pending.order.serial
            );
        changed = committed.has_value() || changed;
    }
    return changed;
}

bool s19OldAdvanceRuntimeEconomyForCurrentHumanHouse(
    RuntimeWindowState& state,
    bool refreshSecretLabCatalog
) {
    if (state.remoteHumanHouseScopeActive) {
        refreshRuntimeEconomyMetricsForCurrentOwner(state);
    }
    bool changed = false;
    changed = advanceRuntimeHarvesterEconomy(state) || changed;
    changed = advanceRuntimeMobileSlaveMinerEconomy(state) || changed;
    changed = advanceRuntimeSlaveMinerEconomy(state) || changed;
    changed = advanceRuntimeStructureRepairs(state) || changed;
    if (refreshSecretLabCatalog
        && addRuntimeSecretLabBuildCatalog(state) > 0) {
        emitRuntimeSound(state, runtimeSoundIntentNewConstructionOptions);
        changed = true;
    }

    if (state.economy.simplifiedHarvesterIncomeEnabled
        && state.economy.harvesters > 0
        && state.economy.refineries > 0) {
        if (state.economy.incomeCooldownTicks > 0) {
            --state.economy.incomeCooldownTicks;
        } else {
            const int income =
                std::min(420, 140 * std::max(1, state.economy.harvesters));
            s19OldDepositRuntimeCredits(state, income);
            state.economy.incomeCooldownTicks = 180;
            ++state.economy.resourceTrips;
            state.economy.status =
                "harvester income +" + std::to_string(income)
                + " credits="
                + std::to_string(state.economy.credits);
            changed = true;
        }
    }

    changed = s19OldAdvanceRuntimeProductionEconomy(state) || changed;
    return changed;
}

bool s19OldAdvanceRuntimeEconomy(RuntimeWindowState& state) {
    bool changed = false;
    // Resource growth and object healing are world-global authored effects.
    // Advance them once before entering any per-house facade scope.
    changed = advanceRuntimeOreGrowth(state) || changed;
    changed = advanceRuntimeTiberiumHealing(state) || changed;
    changed = advanceRuntimeSelfHealing(state) || changed;
    changed = advanceRuntimeSpecialBuildings(state) || changed;

    if (state.humanHouseAuthorities.empty()) {
        changed = s19OldAdvanceRuntimeEconomyForCurrentHumanHouse(state, false) || changed;
        return advanceRuntimeComputerMining(state) || changed;
    }

    // This is the only full grid/UI refresh. Remote scopes use a metric-only
    // path, so their low-power state cannot drive the local sidebar or audio.
    rebuildRuntimeEconomyPower(state);
    bool remoteChanged = false;
    std::vector<std::string> owners;
    owners.reserve(state.humanHouseAuthorities.size());
    for (const RuntimeHumanHouseAuthority& authority :
            state.humanHouseAuthorities) {
        owners.push_back(authority.owner);
    }
    for (const std::string& owner : owners) {
        RuntimeHumanHouseScope scope(state, owner);
        if (!scope) {
            continue;
        }
        const bool ownerChanged = s19OldAdvanceRuntimeEconomyForCurrentHumanHouse(
            state,
            scope.remote()
        );
        remoteChanged = (scope.remote() && ownerChanged) || remoteChanged;
        changed = ownerChanged || changed;
    }
    if (remoteChanged) {
        rebuildRuntimeEconomyPower(state);
    }
    return advanceRuntimeComputerMining(state) || changed;
}

bool s19OldAdvanceRuntimeSimulation(
    RuntimeWindowState& state,
    bool inputDragLightweightMode = false
) {
    RuntimePerfTraceScope trace("s19OldAdvanceRuntimeSimulation", state.tick);
    struct StoredSlaveWorkerSync {
        RuntimeWindowState& state;
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
        [&state]() { return s19OldAdvanceRuntimeEconomy(state); },
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

        RuntimeObjectBehavior& behavior = behaviorIt->second;
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

