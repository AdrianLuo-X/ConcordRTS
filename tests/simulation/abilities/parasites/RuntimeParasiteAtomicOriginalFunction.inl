RuntimeParasiteActionApplyResult applyRuntimeParasiteActionsAtomically(
    FixtureState& state,
    const std::vector<SpecialMechanismAction>& actions,
    const RuntimeDeathWeaponCause& inheritedDeathWeaponCause,
    int fallbackSourceObjectId,
    bool suppressUnitDeathSounds) {
    RuntimeParasiteActionApplyResult result;
    if (actions.empty()) {
        return result;
    }
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(state, actions);
    if (!validateRuntimeParasiteActionBatch(state, prepared)) {
        result.accepted = false;
        state.simulationStatus =
            "parasite action batch rejected atomically";
        return result;
    }

    FixtureCompleteState parasiteDeathCandidate =
        state.specialMechanisms;
    std::vector<SpecialMechanismAction> parasiteDeathActions;
    bool hasParasiteCausedHostDeath = false;
    const ParasiteHostDeathQueries hostDeathQueries(state);
    ParasiteHostDeathCandidate hostDeathCandidate(parasiteDeathCandidate);
    ParasiteHostDeathRejection hostDeathRejection(state, result);
    if (!concord::simulation::abilities::collectRuntimeParasiteHostDeathActions(
            prepared, state.tick, hostDeathQueries, hostDeathCandidate,
            parasiteDeathActions, hasParasiteCausedHostDeath, hostDeathRejection)) {
        return result;
    }
    if (hasParasiteCausedHostDeath) {
        parasiteDeathActions = prepareRuntimeParasiteActions(
            state,
            parasiteDeathActions
        );
        std::vector<SpecialMechanismAction> combined = prepared;
        combined.insert(
            combined.end(),
            parasiteDeathActions.begin(),
            parasiteDeathActions.end()
        );
        if (!validateRuntimeParasiteActionBatch(state, combined)) {
            result.accepted = false;
            state.simulationStatus =
                "parasite death/eject batch rejected atomically";
            return result;
        }
        state.specialMechanisms = std::move(parasiteDeathCandidate);
        const RuntimeParasiteActionApplyResult ejected =
            applyRuntimeParasiteActionsAtomically(
                state,
                parasiteDeathActions,
                inheritedDeathWeaponCause,
                fallbackSourceObjectId,
                suppressUnitDeathSounds
            );
        if (!ejected.accepted) {
            result.accepted = false;
            return result;
        }
        result.changed = ejected.changed;
    }

    for (const SpecialMechanismAction& action : prepared) {
        if (action.kind == SpecialMechanismActionKind::TimedBombDetonate) {
            continue;
        }
        WorldObjectInstance* source =
            findMutableRuntimeObjectByIdIncludingInactive(
                state,
                action.objectId
            );
        WorldObjectInstance* target =
            findMutableRuntimeObjectByIdIncludingInactive(
                state,
                action.targetObjectId
            );
        switch (action.kind) {
        case SpecialMechanismActionKind::ParasiteEnterLimbo: {
            const bool wasSelected = selectionContains(
                state,
                action.objectId
            );
            state.parasiteReselectAfterLimboObjectIds.erase(
                action.objectId
            );
            if (wasSelected
                && runtimeObjectReselectsAfterParasiteLimbo(
                    state,
                    *source
                )) {
                state.parasiteReselectAfterLimboObjectIds.insert(
                    action.objectId
                );
            }
            removeRuntimeSelection(state, action.objectId);
            if (!runtimeObjectRejoinsTeamAfterParasiteLimbo(
                    state,
                    *source
                )) {
                detachRuntimeObjectFromTeamMembership(
                    state,
                    action.objectId
                );
            }
            eraseRuntimeBehavior(state, action.objectId);
            clearRuntimeRenderPosition(*source);
            source->alive = false;
            source->mission = "ParasiteLimbo";
            source->subCell = -1;
            source->dockedToObjectId = -1;
            source->dockIndex = -1;
            result.changed = true;
            break;
        }
        case SpecialMechanismActionKind::ParasiteAttachHost:
            eraseRuntimeBehavior(state, action.targetObjectId);
            cancelRuntimePendingOccupyForTarget(
                state,
                action.targetObjectId,
                "parasite attached"
            );
            target->mission = action.paralyzes > 0
                ? "ParasiteParalyzed"
                : target->mission;
            result.changed = true;
            break;
        case SpecialMechanismActionKind::ParasiteReturnFromLimbo:
        case SpecialMechanismActionKind::ParasiteEject: {
            const bool reselect =
                state.parasiteReselectAfterLimboObjectIds.erase(
                    action.objectId
                ) > 0;
            activateRuntimeWorldObject(state, *source);
            source->health = std::max(1, source->health);
            setRuntimeObjectCell(
                state,
                *source,
                {action.cell.x, action.cell.y},
                false
            );
            source->mission = action.stunTicks > 0
                ? "ParasiteStunned"
                : "Guard";
            assignRuntimeInfantrySubcellAtCurrentLocation(state, *source);
            if (action.replacesTerminatingHost && target != nullptr
                && action.cell.x == target->x && action.cell.y == target->y) {
                // The host has not been drained yet. Preserve its last visible
                // feet, including subcells and an in-progress slope/bridge pose.
                const auto hostSubcell = worldObjectSubCellOffset(*target);
                const auto sourceSubcell = worldObjectSubCellOffset(*source);
                const bool hostHasContinuousLevel = target->renderMoveActive
                    || worldObjectHasStoppedGroundPose(*target);
                source->level = target->level;
                source->onBridge = target->onBridge;
                source->hasRenderPosition = true;
                source->renderMoveActive = false;
                source->renderX = runtimeObjectRenderX(*target)
                    + hostSubcell.x - sourceSubcell.x;
                source->renderY = runtimeObjectRenderY(*target)
                    + hostSubcell.y - sourceSubcell.y;
                source->renderLevel = runtimeDrawLevelForObject(*target);
                source->renderOnBridge = hostHasContinuousLevel
                    ? target->renderOnBridge : target->onBridge;
            }
            state.parasiteStunUntilTickByObject.applyEjectionStun(*source, state.tick, action);
            if (reselect
                && runtimeOwnersAreAllied(
                    state,
                    state.economy.owner,
                    source->owner
                )) {
                addSelectedObject(state, source->instanceId);
            }
            if (action.kind == SpecialMechanismActionKind::ParasiteEject
                && target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
            }
            result.changed = true;
            break;
        }
        case SpecialMechanismActionKind::ParasiteDamage:
            if (target != nullptr && !runtimeDamageIsTypeImmune(state, *target,
                    runtimeTypeImmuneSource(state, action.objectId), action.damage)) {
                const int appliedDamage = runtimeAuthoredDamageAgainst(
                    state,
                    *target,
                    action.damage,
                    action.warheadId,
                    0.0,
                    1.0
                );
                target->health = std::max(0, target->health - appliedDamage);
                result.changed = appliedDamage > 0 || result.changed;
                if (target->health <= 0) {
                    RuntimeObjectExitRequest targetExit;
                    targetExit.objectId = target->instanceId;
                    targetExit.kind = RuntimeObjectExitKind::Destroyed;
                    targetExit.tick = action.tick;
                    targetExit.mission = "Destroyed";
                    targetExit.reason = action.reason.empty()
                        ? "parasite bite was lethal"
                        : action.reason;
                    targetExit.sourceObjectId = action.objectId;
                    targetExit.sourceOwner = action.owner;
                    targetExit.deathWeaponCause =
                        inheritedDeathWeaponCause;
                    (void)queueRuntimeObjectExit(
                        state,
                        std::move(targetExit)
                    );
                }
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillTarget:
        case SpecialMechanismActionKind::ParasiteKillParasite: {
            const bool killSource = action.kind
                == SpecialMechanismActionKind::ParasiteKillParasite;
            if (killSource) {
                state.parasiteReselectAfterLimboObjectIds.erase(
                    action.objectId
                );
            }
            if (killSource && target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
                result.changed = true;
            }
            const int objectId = killSource
                ? action.objectId
                : action.targetObjectId;
            const WorldObjectInstance* killed =
                findRuntimeObjectByIdIncludingInactive(state, objectId);
            if (killed == nullptr || killed->health <= 0) {
                break;
            }
            RuntimeObjectExitRequest dependentExit;
            dependentExit.objectId = objectId;
            dependentExit.kind = RuntimeObjectExitKind::Destroyed;
            dependentExit.tick = action.tick;
            dependentExit.mission = "Destroyed";
            dependentExit.reason = action.reason;
            dependentExit.sourceObjectId = fallbackSourceObjectId >= 0
                ? fallbackSourceObjectId
                : action.objectId;
            dependentExit.sourceOwner = action.owner;
            dependentExit.deathWeaponCause = inheritedDeathWeaponCause;
            dependentExit.suppressDeathWeapon =
                action.suppressDeathWeapon;
            dependentExit.unitDeathSoundHandled = suppressUnitDeathSounds;
            if (killSource && action.sourceWasAttachedToHost
                && target != nullptr) {
                // Attached parasites retain their old launch cell in limbo.
                // Capture the current host before its exit clears that pose.
                dependentExit.unitDeathSoundPosition =
                    runtimeSpatialAudioPositionForObject(*target);
            } else if (killSource && !action.sourceWasAttachedToHost) {
                const auto actor = std::find_if(state.projectiles.begin(), state.projectiles.end(),
                    [&](const RuntimeProjectileEffect& projectile) {
                        return projectile.actorId == action.projectileId
                            && projectile.attackerObjectId == objectId
                            && runtimeProjectileIsParasiteLeap(state, projectile);
                    });
                if (actor != state.projectiles.end()) {
                    dependentExit.unitDeathSoundPosition = RuntimeSpatialAudioPosition{
                        actor->currentX, actor->currentY, actor->currentZ};
                }
            }
            result.changed = queueRuntimeObjectExit(
                state,
                std::move(dependentExit)
            ) == RuntimeObjectExitQueueResult::Queued || result.changed;
            break;
        }
        case SpecialMechanismActionKind::ParasiteReleaseTarget:
            if (target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
                result.changed = true;
            }
            break;
        case SpecialMechanismActionKind::TimedBombDetonate:
            break;
        }
    }
    if (result.changed) {
        invalidateRuntimeTickOccupancyCache(state);
        invalidateRuntimeObjectProjectionCache(state);
        state.navigationRefreshPending = true;
    }
    return result;
}
