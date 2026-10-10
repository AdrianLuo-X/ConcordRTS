bool advanceRuntimeOmniCrushContacts(RuntimeWindowState& state) {
    std::vector<int> sourceIds;
    for (const auto& object : state.world.getObjects())
        if (runtimeObjectUsesOmniCrushApproach(state, object))
            sourceIds.push_back(object.instanceId);
    std::sort(sourceIds.begin(), sourceIds.end());
    bool changed = false;
    for (int sourceId : sourceIds) {
        const auto* source = findObjectById(state, sourceId);
        if (!source || !source->alive) continue;
        const auto found = state.behaviors.find(sourceId);
        const bool approaching = found != state.behaviors.end()
            && runtimeOmniCrushPendingSegment(state, *source, found->second);
        const RuntimeCellPoint destination = approaching
            ? found->second.renderTo : runtimeCellPointForObject(*source);
        const auto result = resolveRuntimeCrushAtDestination(
            state, sourceId, destination, -1, true);
        changed = result == RuntimeCrushStepResult::Crushed || changed;
        // Exit callbacks can add crew, destroy the source, or erase behavior.
        // Neither world pointers nor behavior references survive that call.
        auto* current = findMutableObjectById(state, sourceId);
        auto live = state.behaviors.find(sourceId);
        if (!approaching || !current || !current->alive || live == state.behaviors.end()
            || !runtimeOmniCrushPendingSegment(state, *current, live->second)) continue;
        if (result == RuntimeCrushStepResult::Blocked) {
            changed = cancelRuntimeBlockedOmniCrushApproach(state, *current, live->second) || changed;
            continue;
        }
        const auto scan = runtimeScanCrushTargetsAtCell(state, *current, destination);
        const double dx = runtimeObjectRenderX(*current) - destination.x;
        const double dy = runtimeObjectRenderY(*current) - destination.y;
        if (!scan.blocked && scan.targetIds.empty() && dx * dx + dy * dy < 0.25
            && current->renderOnBridge == runtimeCellIsBridge(state, destination.x, destination.y)) {
            const auto departure = runtimeCellPointForObject(*current);
            setRuntimeObjectCell(state, *current, destination, false);
            updateRuntimeTickOccupancyForMove(state, *current, departure, destination);
            if (live->second.renderMoveTicksRemaining <= 0) {
                clearRuntimeRenderPosition(*current);
                live->second.renderMoveTicksTotal = live->second.renderMoveTicksRemaining = 0;
                live->second.renderArrivalDistanceAfter = -1.0;
                live->second.moveSegmentFrom = {-1, -1};
            }
            changed = true;
        } else if (live->second.moveCooldownTicks <= 0) {
            // A moving victim or newly spawned crew may still be outside the
            // contact radius. Hold the reached body, with a valid positive
            // saved clock, until the cell is genuinely clear or entry cancels.
            auto& move = live->second;
            move.renderFromOffsetX = current->renderX - move.renderFrom.x;
            move.renderFromOffsetY = current->renderY - move.renderFrom.y;
            move.renderFromLevel = current->renderLevel;
            move.renderFromOnBridge = current->renderOnBridge;
            move.renderMoveTicksTotal = move.renderMoveTicksRemaining = move.moveCooldownTicks = 1;
            move.renderArrivalDistanceAfter = -1.0;
        }
    }
    return changed;
}
