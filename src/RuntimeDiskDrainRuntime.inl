bool runtimeDiskDrainRecordActive(const RuntimeWindowState& state, int sourceId,
    const RuntimeDiskDrain& drain) {
    const auto* source = findObjectById(state, sourceId);
    const auto* target = findObjectById(state, drain.targetId);
    const auto* weapon = state.rules.findWeaponType(drain.weaponId);
    const auto* building = target ? state.rules.findBuildingType(target->type) : nullptr;
    const auto order = state.behaviors.find(sourceId);
    if (!source || !source->alive || source->health <= 0 || !target || !target->alive
        || target->health <= 0 || target->kind != WorldObjectKind::Structure
        || !building || !building->drainable || !weapon || !weapon->drainWeapon
        || source->owner != drain.owner || source->type != drain.sourceType
        || target->owner != drain.targetOwner || target->type != drain.targetType
        || runtimeOwnersAreAllied(state, source->owner, target->owner)
        || order == state.behaviors.end() || order->second.kind != RuntimeBehaviorKind::Attack
        || order->second.targetObjectId != drain.targetId
        || order->second.state == RuntimeBehaviorState::Complete
        || runtimeObjectDisabledByParasite(state, sourceId)
        || runtimeObjectSuspendedByMagBeam(state, sourceId)
        || (state.specialMechanismsInitialized
            && (state.specialMechanisms.isTemporalFrozen(sourceId)
                || state.specialMechanisms.isTemporalFrozen(drain.targetId)))
        || runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(*source), *target) > 0.001)
        return false;
    return runtimeWeaponHasLineOfSight(state, *source, *target, *weapon);
}

bool runtimeDiskDrainOwnerBlackout(const RuntimeWindowState& state, const std::string& owner) {
    const auto key = runtimeRepairDepotCanonicalOwnerId(state, owner);
    for (const auto& [id, drain] : state.combatDetails.diskDrains.sources) {
        if (runtimeRepairDepotCanonicalOwnerId(state, drain.targetOwner) != key
            || !runtimeDiskDrainRecordActive(state, id, drain)) continue;
        const auto* target = findObjectById(state, drain.targetId);
        const auto* building = state.rules.findBuildingType(drain.targetType);
        if (target && building && runtimeBuildingPowerOutput(*building, *target) > 0) return true;
    }
    return false;
}

bool runtimeDiskDrainDisablesObject(const RuntimeWindowState& state, int objectId) {
    const auto& drains = state.combatDetails.diskDrains;
    const int source = drains.sourceForTarget(objectId);
    const auto found = drains.sources.find(source);
    return found != drains.sources.end() && runtimeDiskDrainRecordActive(state, source, found->second);
}

void cancelRuntimeDiskDrainSource(RuntimeWindowState& state, int sourceId) {
    if (state.combatDetails.diskDrains.sources.erase(sourceId)) state.powerRefreshPending = true;
}

bool beginRuntimeDiskDrain(RuntimeWindowState& state, WorldObjectInstance& source,
    const WorldObjectInstance& target, const WeaponType& weapon, RuntimeObjectBehavior& behavior) {
    auto& drains = state.combatDetails.diskDrains;
    const auto* building = state.rules.findBuildingType(target.type);
    if (!weapon.drainWeapon || target.kind != WorldObjectKind::Structure || !building
        || !building->drainable || runtimeOwnersAreAllied(state, source.owner, target.owner)
        || runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(source), target) > 0.001)
        return false;
    const int competingSource = drains.sourceForTarget(target.instanceId);
    if (competingSource >= 0 && competingSource != source.instanceId) {
        const auto& other = drains.sources.at(competingSource);
        if (runtimeDiskDrainRecordActive(state, competingSource, other)) {
            behavior.state = RuntimeBehaviorState::Cooldown;
            return false;
        }
        cancelRuntimeDiskDrainSource(state, competingSource);
    }
    auto existing = drains.sources.find(source.instanceId);
    if (existing == drains.sources.end() || existing->second.targetId != target.instanceId
        || existing->second.targetOwner != target.owner || existing->second.owner != source.owner) {
        RuntimeDiskDrain drain;
        drain.targetId = target.instanceId;
        drain.startedTick = drain.lastAdvancedTick = state.tick;
        // Retail 0x6FA167 checks CurrentFrame % DrainMoneyFrameDelay.
        drain.nextMoneyTick = runtimeDiskDrainNextMoneyTick(state.tick,
            state.rules.getSpecialMechanismRules().drainMoneyFrameDelay, state.config.fixedTickHz);
        drain.owner = source.owner; drain.sourceType = source.type;
        drain.targetOwner = target.owner; drain.targetType = target.type; drain.weaponId = weapon.id;
        drains.sources.insert_or_assign(source.instanceId, std::move(drain));
        state.powerRefreshPending = true;
        if (weapon.decloakToFire) runtimeDecloakObject(state, source, "drain");
        notifyRuntimeWeaponFired(state, source, target, weapon);
        ++state.actionSummary.attacksThisTick;
    }
    // DrainWeapon establishes a link, not a damaging projectile or a paid burst.
    // Its authored FireOnce must not clear that persistent attack target.
    behavior.path.clear();
    behavior.pathRequestPending = false;
    behavior.state = RuntimeBehaviorState::Cooldown;
    source.mission = "Attacking";
    return true;
}

bool advanceRuntimeDiskDrains(RuntimeWindowState& state, bool transferMoney) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeDiskDrains", state.tick);
    auto& drains = state.combatDetails.diskDrains.sources;
    bool changed = false;
    const auto& rules = state.rules.getSpecialMechanismRules();
    for (auto it = drains.begin(); it != drains.end();) {
        if (!runtimeDiskDrainRecordActive(state, it->first, it->second)) {
            it = drains.erase(it);
            state.powerRefreshPending = true;
            changed = true;
            continue;
        }
        auto& drain = it->second;
        if (transferMoney && state.tick > drain.lastAdvancedTick) {
            drain.lastAdvancedTick = state.tick;
            const auto* building = state.rules.findBuildingType(drain.targetType);
            if (state.tick >= drain.nextMoneyTick) {
                if (building && building->resourceDestination) {
                    const int available = std::max(0, runtimeRepairDepotCreditBalance(state, drain.targetOwner));
                    const int receiver = std::max(0, runtimeRepairDepotCreditBalance(state, drain.owner));
                    const int amount = std::min({rules.drainMoneyAmount, available,
                        std::numeric_limits<int>::max() - receiver});
                    if (amount > 0) {
                        debitRuntimeRepairDepotCredits(state, drain.targetOwner, amount);
                        depositRuntimeRepairDepotCredits(state, drain.owner, amount);
                    }
                }
                drain.nextMoneyTick = runtimeDiskDrainNextMoneyTick(
                    state.tick, rules.drainMoneyFrameDelay, state.config.fixedTickHz);
            }
            changed = true;
        }
        ++it;
    }
    return changed;
}
