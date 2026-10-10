// Included after the per-house selection executor. Travel intent survives the
// temporary Attack behavior, so acquiring an enemy cannot erase the destination.
bool beginRuntimePlayerTravelOrder(RuntimeWindowState& state, const std::string& owner,
    const RuntimeMultiplayerGameplayTargetOrderCommand& command
) {
    const auto* target = std::get_if<RuntimeMultiplayerGameplayCellTarget>(&command.target);
    if (!target) return false;
    std::vector<std::int32_t> actors;
    const std::string canonicalOwner = runtimeCanonicalHumanHouseOwner(owner);
    for (int id : command.objectIds) {
        const auto* actor = findObjectById(state, id);
        if (actor && actor->alive && !runtimeObjectIsEnslavedSlaveWorker(state, *actor)
            && runtimeObjectCanMove(state, *actor)
            && runtimeCanonicalHumanHouseOwner(actor->owner) == canonicalOwner
            && !runtimePlayerActorTemporarilyCommandLocked(state, *actor)) actors.push_back(id);
    }
    if (actors.empty()) return false;
    std::map<int, RuntimeCellPoint> acceptedActors;
    const bool accepted = applyRuntimePlayerSelectionCommand(state, owner, actors, [&]() {
        issueRuntimeMoveCommand(state, {target->cell.x, target->cell.y}, &acceptedActors);
        return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
    });
    if (!accepted) return false;
    for (const auto& [id, destination] : acceptedActors) {
        const auto* actor = findObjectById(state, id);
        if (!actor) continue;
        const auto* unit = findRuntimeUnitType(state.rules, *actor);
        // Retail engineers, spies and pad aircraft explicitly disable attack
        // move. They still travel; their contextual weapons must not auto-fire.
        if (unit && (unit->airportBound || (unit->preventAttackMove
                && command.order == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove))) continue;
        state.playerCommands.travelling[id] = {
            canonicalOwner,
            {destination.x, destination.y},
            command.order, false, state.tick};
    }
    state.interactionStatus = command.order == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove
        ? "attack-move command accepted" : "guard destination command accepted";
    return true;
}

bool runtimePlayerGuardDestinationAllowsTarget(const RuntimeWindowState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target
) {
    const auto found = state.playerCommands.travelling.find(actor.instanceId);
    if (found == state.playerCommands.travelling.end()
        || found->second.kind != RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination) return true;
    if (!found->second.guarding) return false;
    const auto profile = runtimeWeaponProfileForObject(state, actor, &target);
    if (!profile.weapon) return false;
    const double radius = std::max(runtimeCachedCapabilityProfileForObject(state, actor).guardScanRange,
        runtimeEffectiveWeaponRange(state, actor, target, *profile.weapon));
    return runtimeCellDistanceToObjectFootprint(state,
        {found->second.destination.x, found->second.destination.y}, target) <= radius + 0.001;
}

bool runtimePlayerTravellingCanAcquire(const RuntimeWindowState& state,
    const WorldObjectInstance& actor
) {
    const auto found = state.playerCommands.travelling.find(actor.instanceId);
    if (found == state.playerCommands.travelling.end()
        || runtimeObjectIsEnslavedSlaveWorker(state, actor)
        || runtimeCanonicalHumanHouseOwner(actor.owner) != found->second.owner
        || runtimePlayerActorTemporarilyCommandLocked(state, actor)
        || runtimeFactoryDeliveryOwnsEgress(state, actor.instanceId)) return false;
    if (found->second.kind == RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination
        && !found->second.guarding) return false;
    const auto behavior = state.behaviors.find(actor.instanceId);
    return behavior == state.behaviors.end()
        || behavior->second.kind == RuntimeBehaviorKind::Move
        || behavior->second.state == RuntimeBehaviorState::Complete
        || behavior->second.state == RuntimeBehaviorState::Idle;
}

bool advanceRuntimePlayerTravelOrders(RuntimeWindowState& state) {
    bool changed = false;
    std::vector<int> ids;
    for (const auto& [id, travel] : state.playerCommands.travelling) ids.push_back(id);
    for (int id : ids) {
        const auto found = state.playerCommands.travelling.find(id);
        if (found == state.playerCommands.travelling.end()) continue;
        const RuntimePlayerTravelOrder travel = found->second;
        auto* actor = findMutableObjectById(state, id);
        if (!actor || !actor->alive || runtimeObjectIsEnslavedSlaveWorker(state, *actor)
            || runtimeCanonicalHumanHouseOwner(actor->owner) != travel.owner) {
            state.playerCommands.travelling.erase(id); changed = true; continue;
        }
        if (runtimePlayerActorTemporarilyCommandLocked(state, *actor)
            || runtimeFactoryDeliveryOwnsEgress(state, id)
            || state.combatDetails.hasPendingBurst(id)) continue;
        auto behavior = state.behaviors.find(id);
        if (behavior != state.behaviors.end()
            && behavior->second.kind == RuntimeBehaviorKind::Attack
            && behavior->second.state != RuntimeBehaviorState::Complete) {
            const auto* target = findObjectById(state, behavior->second.targetObjectId);
            if (target && target->alive && runtimeObjectVisibleToOwner(state, actor->owner, *target)
                && runtimeOwnersAreHostileForAutoTarget(state, actor->owner, target->owner)
                && runtimePlayerGuardDestinationAllowsTarget(state, *actor, *target)) continue;
            // The enemy died, left vision, or escaped the guarded area. Stop
            // the temporary combat action and resume the retained destination.
            cancelRuntimeCombatFire(state, id);
            eraseRuntimeBehavior(state, id);
            behavior = state.behaviors.end();
            changed = true;
        }
        if (behavior != state.behaviors.end()
            && behavior->second.state != RuntimeBehaviorState::Complete
            && behavior->second.state != RuntimeBehaviorState::Idle
            && behavior->second.kind != RuntimeBehaviorKind::None) continue;
        const bool arrived = runtimeCellPointForObject(*actor)
            == RuntimeCellPoint{travel.destination.x, travel.destination.y};
        if (arrived) {
            if (travel.kind == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove) {
                state.playerCommands.travelling.erase(id);
            } else if (travel.guarding) {
                continue;
            } else {
                state.playerCommands.travelling.at(id).guarding = true;
            }
            cancelRuntimeCombatFire(state, id);
            eraseRuntimeBehavior(state, id);
            actor->mission = runtimeMissionName(RuntimeMissionIntent::Idle);
            changed = true;
            continue;
        }
        if (state.tick < travel.nextMoveAttemptTick || !runtimeObjectCanMove(state, *actor)) continue;
        // Resuming a standing order is not another player click or voice reply.
        struct QuietResume {
            RuntimeWindowState& state;
            bool previous;
            explicit QuietResume(RuntimeWindowState& value)
                : state(value), previous(value.runtimeSuppressPlayerAudioFeedback) {
                state.runtimeSuppressPlayerAudioFeedback = true;
            }
            ~QuietResume() { state.runtimeSuppressPlayerAudioFeedback = previous; }
        } quiet(state);
        std::map<int, RuntimeCellPoint> acceptedDestinations;
        const bool accepted = applyRuntimePlayerSelectionCommand(state, travel.owner, {id}, [&]() {
            issueRuntimeMoveCommand(state, {travel.destination.x, travel.destination.y}, &acceptedDestinations);
            return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
        });
        const auto resumed = state.playerCommands.travelling.find(id);
        if (resumed != state.playerCommands.travelling.end()) {
            resumed->second.guarding = false;
            resumed->second.nextMoveAttemptTick = state.tick + std::max(1, state.config.fixedTickHz);
            const auto destination = acceptedDestinations.find(id);
            if (accepted && destination != acceptedDestinations.end())
                resumed->second.destination = {destination->second.x, destination->second.y};
        }
        changed = accepted || changed;
    }
    return changed;
}
