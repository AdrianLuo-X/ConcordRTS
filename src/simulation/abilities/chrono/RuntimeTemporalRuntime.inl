bool advanceRuntimeTemporalAttacks(RuntimeWindowState& state) {
    if (state.specialMechanisms.temporalTargets().empty()) return false;
    // Work from IDs: completion and interruption mutate the relationship map.
    const auto targets = state.specialMechanisms.temporalTargets();
    const auto sink = runtimeSpecialEventSink(state);
    bool changed = false;
    bool playerObjectLost = false;
    for (const auto& [targetId, temporal] : targets) {
        const int previousTick = temporal.lastAdvancedTick < 0
            ? std::max(0, state.tick - 1) : temporal.lastAdvancedTick;
        if (!state.specialMechanisms.beginTemporalTick(targetId, state.tick)) continue;
        const auto frames = runtimeRuleFrameAtTick(state.tick, state.config.fixedTickHz)
            - runtimeRuleFrameAtTick(previousTick, state.config.fixedTickHz);
        for (const auto& [attackerId, unusedProgress] : temporal.progressByAttacker) {
            const auto* target = findObjectById(state, targetId);
            const auto* source = findRuntimeObjectByIdIncludingInactive(state, attackerId);
            const WorldObjectInstance* host = nullptr;
            const auto explicitOrder = state.behaviors.find(attackerId);
            const bool forceFire = explicitOrder != state.behaviors.end()
                && explicitOrder->second.kind == RuntimeBehaviorKind::Attack
                && explicitOrder->second.targetObjectId == targetId && explicitOrder->second.forceFire;
            bool valid = source && target && target->health > 0 && runtimeCombatCanAct(state, *source)
                && !runtimeCarrierAircraftHasDeckProtection(state, *target)
                && (forceFire || !runtimeOwnersAreAllied(state, source->owner, target->owner));
            if (valid && !source->alive) {
                const auto cursor = state.combatDetails.fireCursors.find(attackerId);
                if (cursor != state.combatDetails.fireCursors.end()
                    && cursor->second.lastTargetObjectId == targetId)
                    host = findObjectById(state, cursor->second.sourceAnchorObjectId);
                valid = host && runtimeCombatValidPassenger(*source, *host) && runtimeCombatCanAct(state, *host);
            } else if (valid) {
                const auto order = state.behaviors.find(attackerId);
                const auto cursor = state.combatDetails.fireCursors.find(attackerId);
                const auto* unit = findRuntimeUnitType(state.rules, *source);
                const bool independentMoveFire = unit && unit->opportunityFire && !unit->noMovingFire
                    && order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Move
                    && cursor != state.combatDetails.fireCursors.end()
                    && cursor->second.lastTargetObjectId == targetId;
                valid = order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack
                    && order->second.targetObjectId == targetId;
                valid = valid || independentMoveFire;
            }
            const auto hostOrder = host == nullptr ? state.behaviors.end()
                : state.behaviors.find(host->instanceId);
            const bool profileForceFire = forceFire
                || (hostOrder != state.behaviors.end()
                    && hostOrder->second.kind == RuntimeBehaviorKind::Attack
                    && hostOrder->second.targetObjectId == targetId
                    && hostOrder->second.forceFire);
            const auto profile = !valid ? RuntimeWeaponProfile{}
                : host ? runtimeCombatPassengerWeaponProfile(state, *source, target, profileForceFire)
                : runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
            valid = valid && profile.weapon && profile.warhead && profile.warhead->temporal;
            if (valid) {
                const auto pose = runtimeCombatFiringPose(*source, host);
                const double range = host ? state.rules.getCombatDetailRules().openToppedWarpDistance
                    : runtimeEffectiveWeaponRange(state, pose, *target, *profile.weapon);
                const double distance = host
                    ? runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(*host), *target)
                    : runtimeWeaponDistanceToTarget(state, pose, *target, *profile.weapon);
                valid = distance <= range && distance + 0.001 >= profile.weapon->minimumRange
                    && runtimeObjectVisibleToOwner(state, source->owner, *target)
                    && runtimeWeaponHasLineOfSight(state, pose, *target, *profile.weapon);
            }
            if (!valid) {
                state.specialMechanisms.clearTemporalAttacker(attackerId, targetId, state.tick,
                    sink, "temporal source or target no longer available");
                changed = true;
                continue;
            }
            if (frames <= 0) continue;
            // Temporal ignores armor verses, difficulty health and ordinary
            // firepower bonuses. ElitePrimary supplies its own authored Damage.
            const int progress = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), std::max(0, profile.weapon->damage) * frames));
            bool completed = false;
            std::string reason;
            changed = state.specialMechanisms.applyTemporalHit(state.world.accessObjects(), state.rules,
                attackerId, targetId, progress, temporal.requiredProgress, state.tick,
                [&state](const std::string& a, const std::string& b) { return runtimeOwnersAreAllied(state, a, b); },
                sink, completed, reason, forceFire) || changed;
            if (completed) {
                playerObjectLost = playerObjectLost || (target->kind != WorldObjectKind::Structure && target->owner == state.economy.owner);
                RuntimeObjectExitRequest exit;
                exit.objectId = targetId;
                exit.kind = RuntimeObjectExitKind::TemporalDestroyed;
                exit.tick = state.tick;
                exit.mission = "TemporalRemoved";
                exit.reason = "temporal continuous erasure complete";
                exit.sourceObjectId = attackerId;
                exit.sourceOwner = source->owner;
                exit.suppressDeathWeapon = true;
                queueRuntimeObjectExit(state, std::move(exit));
                break;
            }
        }
    }
    changed = drainRuntimeObjectExitQueue(state) || changed;
    if (playerObjectLost && state.tick - state.runtimeLastUnitLostSoundTick
            >= std::max(1, state.config.fixedTickHz * 2)) {
        emitRuntimeSound(state, runtimeSoundIntentUnitLost);
        state.runtimeLastUnitLostSoundTick = state.tick;
    }
    return changed;
}

void resolveRuntimeTemporalBeamEndpoints(RuntimeWindowState& state) {
    for (auto& snapshot : state.specialMechanismRenderSnapshots) {
        if (snapshot.kind != "temporalBeam") continue;
        const auto* source = findRuntimeObjectByIdIncludingInactive(state, snapshot.objectId);
        const auto* target = findObjectById(state, snapshot.targetObjectId);
        if (source == nullptr || target == nullptr) continue;
        const WorldObjectInstance* host = nullptr;
        if (!source->alive) {
            const auto cursor = state.combatDetails.fireCursors.find(source->instanceId);
            if (cursor != state.combatDetails.fireCursors.end())
                host = findObjectById(state, cursor->second.sourceAnchorObjectId);
            if (!host || !runtimeCombatValidPassenger(*source, *host)) continue;
        }
        const auto sourceOrder = state.behaviors.find(snapshot.objectId);
        const auto hostOrder = host == nullptr ? state.behaviors.end()
            : state.behaviors.find(host->instanceId);
        const bool profileForceFire = (sourceOrder != state.behaviors.end()
            && sourceOrder->second.kind == RuntimeBehaviorKind::Attack
            && sourceOrder->second.targetObjectId == snapshot.targetObjectId
            && sourceOrder->second.forceFire)
            || (hostOrder != state.behaviors.end()
                && hostOrder->second.kind == RuntimeBehaviorKind::Attack
                && hostOrder->second.targetObjectId == snapshot.targetObjectId
                && hostOrder->second.forceFire);
        auto profile = host ? runtimeCombatPassengerWeaponProfile(state, *source, target, profileForceFire)
            : runtimeWeaponProfileForObject(state, *source, target, profileForceFire);
        profile.openTransport = host != nullptr;
        if (profile.weapon == nullptr || !profile.weapon->radBeam
            || profile.warhead == nullptr || !profile.warhead->temporal) continue;
        const auto pose = runtimeCombatFiringPose(*source, host);
        auto muzzle = runtimeProjectileSourcePosition(state, pose, profile,
            runtimeWeaponFiringFacing(state, pose, *target));
        alignRuntimeWeaponMuzzleToSprite(state, pose, profile, runtimeWeaponFiringFacing(state, pose, *target), muzzle);
        const auto endpoint = runtimeProjectileTargetPosition(state, *target);
        snapshot.sourceX = muzzle.x;
        snapshot.sourceY = muzzle.y;
        snapshot.sourceLevel = muzzle.z;
        snapshot.sourceScreenOffsetX = muzzle.screenOffsetX;
        snapshot.sourceNativeFixedX = muzzle.nativeFixedX;
        snapshot.sourceScreenOffsetY = muzzle.screenOffsetY;
        snapshot.sourceNativeFixedY = muzzle.nativeFixedY;
        snapshot.targetX = endpoint.x;
        snapshot.targetY = endpoint.y;
        snapshot.targetLevel = endpoint.level;
        snapshot.color = state.rules.getAudioVisualValue("ChronoBeamColor");
        if (snapshot.color.empty()) snapshot.color = "128,200,255";
        snapshot.owner = source->owner;
        snapshot.hasBeamEndpoints = true;
    }
}

void spawnRuntimeTemporalEraseEffect(
    RuntimeWindowState& state, const SpecialMechanismEvent& event
) {
    const auto* target = findObjectById(state, event.targetObjectId);
    if (target == nullptr) return;
    RuntimeImpactEffect effect;
    effect.attackerObjectId = event.objectId;
    effect.targetObjectId = target->instanceId;
    effect.owner = target->owner;
    effect.cell = {target->x, target->y};
    effect.level = target->level;
    effect.hasWorldPosition = true;
    const auto offset = worldObjectSubCellOffset(*target);
    effect.worldX = runtimeObjectRenderX(*target) + offset.x;
    effect.worldY = runtimeObjectRenderY(*target) + offset.y;
    effect.worldLevel = runtimeDrawLevelForObject(*target);
    effect.facing = target->facing;
    effect.lifecycleState = "TemporalErase";
    effect.renderIntent = "temporalErase";
    effect.animId = state.rules.getSpecialMechanismRules().warpAwayAnim;
    if (effect.animId.empty() || toLowerAsciiLocal(effect.animId) == "none") return;
    initializeRuntimeImpactAnimationTimeline(state, effect, true);
    if (const auto* art = state.art.findArtType(effect.animId)) {
        if (!art->reportSound.empty()) emitRuntimeSound(state, "sound:" + art->reportSound, runtimeSpatialAudioPositionForImpact(effect), true);
    }
    state.impactEffects.push_back(std::move(effect));
}
