// Include after runtimeObjectCanReceiveMoveCommand and before issueRuntimeMoveCommand.
// Only the benchmark's active owned scope performs these read-only diagnostics.
void runtimeCaptureMassMovementRejection(
    const RuntimeWindowState& state, int objectId, const WorldObjectInstance* object,
    RuntimeCellPoint click, const RuntimeCellPoint* desired, const RuntimeCellPoint* candidate,
    std::string_view reason, std::string_view detail = {}, const UnitType* resolvedType = nullptr
) {
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
    const RuntimeWindowState& state, int objectId, RuntimeCellPoint click,
    const RuntimeCellPoint* desired, const RuntimeCellPoint* candidate,
    std::string_view reason, std::string_view detail = {}, const UnitType* resolvedType = nullptr
) {
    if (!RuntimeMassMovementRejectScope::matches(&state, state.tick)) return;
    // Release/chrono transactions can replace object storage. Reacquire only
    // for an active observer instead of dereferencing a pre-transaction actor.
    runtimeCaptureMassMovementRejection(state, objectId, findObjectById(state, objectId),
        click, desired, candidate, reason, detail, resolvedType);
}
