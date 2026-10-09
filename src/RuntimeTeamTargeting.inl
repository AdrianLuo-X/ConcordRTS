// Script target selection is strategic for computer houses. The actual units
// still travel through the world and acquire vision before ordinary firing.
bool runtimeTeamCanLocateTarget(const RuntimeWindowState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target) {
    if (runtimeObjectHiddenFromOwnerByStealth(state, actor.owner, target)
        || runtimeSpyDisguiseConcealsFromObject(state, actor, target)) return false;
    return state.computerAiByOwner.contains(toLowerAsciiLocal(trimAsciiLocal(actor.owner)))
        || runtimeObjectVisibleToOwner(state, actor.owner, target);
}

std::optional<RuntimeCellPoint> runtimeTeamNavalAttackApproach(
    const RuntimeWindowState& state, const WorldObjectInstance& actor,
    const WorldObjectInstance& target, const WeaponType& weapon) {
    const auto* type = findRuntimeUnitType(state.rules, actor);
    double range = runtimeEffectiveWeaponRange(state, actor, target, weapon);
    if (!runtimeObjectVisibleToOwner(state, actor.owner, target) && type)
        range = std::min(range, static_cast<double>(std::max(1, type->sight)));
    // The destroyer's carrier aircraft uses the authored unlimited range.
    // Once the submarine is located, the parent ship can launch in place.
    if (!std::isfinite(range)) return runtimeCellPointForObject(actor);
    const int radius = std::clamp(static_cast<int>(std::ceil(range)), 1, 64);
    const auto footprint = runtimeStructureFootprintForObject(state, target);
    const auto occupied = buildRuntimeOccupancyLookup(state, actor.instanceId, nullptr, nullptr);
    std::optional<RuntimeCellPoint> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (int y = std::max(0, target.y - radius); y <= target.y + footprint.height + radius; ++y)
        for (int x = std::max(0, target.x - radius); x <= target.x + footprint.width + radius; ++x) {
            const RuntimeCellPoint cell{x, y};
            if (runtimeAttackRangeDistanceFromCell(state, cell, target, weapon.cellRangefinding) > range
                || !runtimeCellIsPassable(state, state.terrainByCell, occupied, cell, actor)) continue;
            const double distance = runtimeCellDistance(runtimeCellPointForObject(actor), cell);
            if (distance >= bestDistance) continue;
            auto firing = actor; clearRuntimeRenderPosition(firing);
            firing.x = x; firing.y = y; firing.cell = runtimeCellKey(x, y);
            if (!runtimeWeaponHasLineOfSight(state, firing, target, weapon)) continue;
            best = cell; bestDistance = distance;
        }
    return best;
}

bool runtimeTeamActorCanInteractWithTarget(const RuntimeWindowState& state,
    const WorldObjectInstance& actor, const WorldObjectInstance& target) {
    if (!actor.alive || !target.alive || actor.instanceId == target.instanceId
        || runtimeObjectDisabledByParasite(state, actor.instanceId)
        || !runtimeTeamCanLocateTarget(state, actor, target)) return false;
    if (runtimeEngineerCanCaptureStructure(state, actor, target)
        || runtimeSpyCanInfiltrateTarget(state, actor, target)) return true;
    const auto profile = runtimeWeaponProfileForObject(state, actor, &target);
    if (profile.weapon == nullptr) return false;
    if (profile.warhead != nullptr && profile.warhead->makesDisguise)
        return runtimeSpyCanApplyMakesDisguise(state, actor.instanceId,
            target.instanceId, profile.warhead->id);
    if (!runtimeWeaponHasAttackRange(*profile.weapon)
        || (profile.weapon->damage <= 0
            && (profile.warhead == nullptr || !profile.warhead->ivanBomb))) return false;
    if (runtimeObjectMovementMode(state, actor) == PathMovementMode::Naval
        && !runtimeTeamNavalAttackApproach(state, actor, target, *profile.weapon)) return false;
    return runtimePsychicCanAttackTarget(state, actor, target, true,
        runtimeOwnersAreHostileForAutoTarget(state, actor.owner, target.owner));
}

int issueRuntimeTeamAttack(RuntimeWindowState& state,
    const std::vector<int>& objectIds, const WorldObjectInstance& targetObject) {
    RuntimeAutonomousOrderScope orderScope(state.runtimeIssuingAutonomousOrder);
    const WorldObjectInstance target = targetObject;
    const auto savedSelection = state.selectedObjectIds;
    const bool savedAudio = state.runtimeSuppressPlayerAudioFeedback;
    state.runtimeSuppressPlayerAudioFeedback = true;
    int accepted = 0;
    for (int objectId : objectIds) {
        const auto* actor = findObjectById(state, objectId);
        if (actor == nullptr || !runtimeTeamActorCanInteractWithTarget(state, *actor, target))
            continue;
        applySelection(state, {objectId}, false, false);
        if (runtimeEngineerCanCaptureStructure(state, *actor, target)) {
            (void)queueRuntimeEngineerCaptureForObject(state, objectId, target, false);
            if (state.pendingEngineerCaptures.contains(objectId)) {
                ++accepted; ++state.triggers.ordersIssued;
            }
        } else if (runtimeSpyCanInfiltrateTarget(state, *actor, target)) {
            (void)queueRuntimeSpyInfiltration(state, target);
            const auto behavior = state.behaviors.find(objectId);
            if (behavior != state.behaviors.end()
                && behavior->second.targetObjectId == target.instanceId) {
                ++accepted; ++state.triggers.ordersIssued;
            }
        } else if (!runtimeObjectVisibleToOwner(state, actor->owner, target)) {
            const auto profile = runtimeWeaponProfileForObject(state, *actor, &target);
            const auto destination = runtimeObjectMovementMode(state, *actor) == PathMovementMode::Naval && profile.weapon
                ? runtimeTeamNavalAttackApproach(state, *actor, target, *profile.weapon)
                : findRuntimePassableCellNear(state, *actor, runtimeCellPointForObject(target), 4);
            if (destination) accepted += issueRuntimeTeamMove(state, {objectId}, *destination);
        } else {
            issueRuntimeAttackCommand(state, target);
            const int count = std::max(0, state.lastCommand.acceptedCount);
            accepted += count; state.triggers.ordersIssued += count;
            const auto behavior = state.behaviors.find(objectId);
            if (count > 0 && behavior != state.behaviors.end()
                && behavior->second.kind == RuntimeBehaviorKind::Attack) {
                const auto profile = runtimeWeaponProfileForObject(state, *actor, &target);
                // A Spy deliberately copies an infantry identity; this is
                // not an automatically acquired hostile weapon target.
                behavior->second.autoAcquired = profile.warhead == nullptr
                    || !profile.warhead->makesDisguise;
            }
        }
    }
    state.selectedObjectIds = savedSelection;
    state.runtimeSuppressPlayerAudioFeedback = savedAudio;
    return accepted;
}

bool runtimeTeamHasActorSpecificAttackTarget(const RuntimeWindowState& state,
    const RuntimeTeamMissionState& mission, const WorldObjectInstance& target) {
    for (int objectId : mission.objectIds) {
        const auto* actor = findObjectById(state, objectId);
        if (actor != nullptr && runtimeTeamActorCanInteractWithTarget(state, *actor, target))
            return true;
    }
    return false;
}

bool runtimeTeamQuarryMatches(const RuntimeWindowState& state,
    const WorldObjectInstance& object, int quarry) {
    const auto* building = object.kind == WorldObjectKind::Structure
        ? findRuntimeBuildingTypeById(state.rules, object.type) : nullptr;
    const auto* unit = findRuntimeUnitType(state.rules, object);
    switch (quarry) {
    case 0: case 1: case 8: return true;
    case 2: return building != nullptr;
    case 3: return (building != nullptr && (building->refinery || building->resourceGatherer))
        || (unit != nullptr && (unit->harvester || unit->resourceGatherer));
    case 4: return object.kind == WorldObjectKind::Infantry;
    case 5: return object.kind == WorldObjectKind::Unit && unit != nullptr
        && !unit->airportBound && !unit->harvester && !unit->resourceGatherer;
    case 6: return building != nullptr && !trimAsciiLocal(building->factory).empty();
    case 7: return building != nullptr && building->isBaseDefense;
    case 9: return building != nullptr && building->power > 0;
    case 10: return building != nullptr && building->canBeOccupied;
    case 11: return building != nullptr && building->needsEngineer;
    default: return false;
    }
}

const WorldObjectInstance* runtimeTeamEnemyTarget(const RuntimeWindowState& state,
    const RuntimeTeamMissionState& mission, int quarry) {
    const auto center = runtimeTeamMissionCenter(state, mission);
    const auto intendedEnemy = runtimeComputerTacticTargetOwner(state, mission);
    const bool enemyStillPresent = !intendedEnemy.empty() && std::any_of(
        state.world.getObjects().begin(), state.world.getObjects().end(), [&](const auto& object) {
            return object.alive && toLowerAsciiLocal(object.owner) == toLowerAsciiLocal(intendedEnemy);
        });
    const WorldObjectInstance* best = nullptr;
    long long bestScore = std::numeric_limits<long long>::min();
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || !runtimeTeamQuarryMatches(state, object, quarry)
            || (enemyStillPresent && !runtimeOwnerIsNeutralForAutoTarget(object.owner)
                && toLowerAsciiLocal(object.owner) != toLowerAsciiLocal(intendedEnemy))
            || std::find(mission.objectIds.begin(), mission.objectIds.end(), object.instanceId)
                != mission.objectIds.end()
            || (runtimeOwnerIsNeutralForAutoTarget(object.owner) && quarry != 10 && quarry != 11)
            || !runtimeTeamHasActorSpecificAttackTarget(state, mission, object)) continue;
        long long score = object.kind == WorldObjectKind::Structure ? 5000LL : 1000LL;
        if (const auto* building = object.kind == WorldObjectKind::Structure
                ? findRuntimeBuildingTypeById(state.rules, object.type) : nullptr)
            score += building->cost;
        score -= static_cast<long long>(std::lround(
            runtimeCellDistance(center, runtimeCellPointForObject(object)) * 10.0));
        if (best == nullptr || score > bestScore
            || (score == bestScore && object.instanceId < best->instanceId)) {
            best = &object; bestScore = score;
        }
    }
    return best;
}

// Entry consumes the specialist. Retain the completed interaction long enough
// for its script to register success, rather than treating entry as a casualty.
void recordRuntimeTeamTargetInteraction(RuntimeWindowState& state, int actorId, int targetId) {
    for (auto& [id, mission] : state.triggers.teamMissions) {
        if (!mission.completed && mission.stepIssued && mission.targetObjectId == targetId
            && std::find(mission.objectIds.begin(), mission.objectIds.end(), actorId)
                != mission.objectIds.end()) mission.targetInteractionCompleted = true;
    }
}

bool runtimeTeamAircraftNeedReload(const RuntimeWindowState& state,
    const std::vector<int>& objectIds) {
    bool emptyAircraft = false;
    for (int id : objectIds) {
        const auto* actor = findObjectById(state, id);
        const auto* unit = actor == nullptr ? nullptr : findRuntimeUnitType(state.rules, *actor);
        if (unit == nullptr || !unit->airportBound) return false;
        emptyAircraft = emptyAircraft || !runtimeObjectHasAttackAmmo(state.rules, *actor);
    }
    return emptyAircraft;
}
