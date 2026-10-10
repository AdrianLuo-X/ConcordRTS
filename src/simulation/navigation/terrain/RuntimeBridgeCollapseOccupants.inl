// Bridge failure is a ground hazard. Use the current body pose rather than
// the destination cell committed ahead of a move, and keep aircraft aloft.
bool runtimeObjectBodyOnCollapsedBridge(
    const WorldObjectInstance& object, const std::unordered_set<int>& cells
) {
    if (!object.alive || object.kind == WorldObjectKind::Structure || object.airborne)
        return false;
    const bool groundPose = worldObjectHasGroundRenderPose(object)
        && std::isfinite(object.renderX) && std::isfinite(object.renderY);
    const bool onBridge = groundPose ? object.renderOnBridge : object.onBridge;
    if (!onBridge) return false;
    const int x = groundPose ? static_cast<int>(std::lround(object.renderX)) : object.x;
    const int y = groundPose ? static_cast<int>(std::lround(object.renderY)) : object.y;
    return cells.contains(runtimeCellKey(x, y));
}

int queueRuntimeBridgeCollapseOccupants(
    RuntimeWindowState& state, const std::unordered_set<int>& cells,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause
) {
    int queued = 0;
    for (const auto& object : state.world.getObjects()) {
        if (!runtimeObjectBodyOnCollapsedBridge(object, cells)) continue;
        RuntimeObjectExitRequest exit;
        exit.objectId = object.instanceId;
        exit.kind = RuntimeObjectExitKind::Destroyed;
        exit.tick = state.tick;
        exit.mission = "BridgeCollapsed";
        exit.reason = "bridge collapsed";
        exit.sourceObjectId = sourceObjectId;
        exit.sourceOwner = sourceOwner;
        exit.deathWeaponCause = deathWeaponCause;
        exit.suppressCrew = true;
        const bool bodyPose = worldObjectHasGroundRenderPose(object);
        exit.unitDeathSoundPosition = RuntimeSpatialAudioPosition{
            bodyPose ? object.renderX : static_cast<double>(object.x),
            bodyPose ? object.renderY : static_cast<double>(object.y),
            bodyPose ? object.renderLevel : static_cast<double>(object.level)};
        if (queueRuntimeObjectExit(state, std::move(exit))
            == RuntimeObjectExitQueueResult::Queued) ++queued;
    }
    return queued;
}

void destroyRuntimeBridgeCollapseOccupants(
    RuntimeWindowState& state, const std::unordered_set<int>& cells,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause
) {
    const auto queueOccupants = [&]() {
        return queueRuntimeBridgeCollapseOccupants(state, cells,
            sourceObjectId, sourceOwner, deathWeaponCause);
    };
    // The first drain also finalizes already queued occupants after merging
    // suppressCrew. A pending parasite can return from limbo during that drain;
    // collect any newly active body on this same footprint before nav refresh.
    (void)queueOccupants();
    if (state.drainingObjectExits) {
        state.pendingBridgeCollapses.push_back({cells, sourceObjectId,
            sourceOwner, deathWeaponCause});
        return;
    }
    (void)drainRuntimeObjectExitQueue(state);
    while (queueOccupants() > 0) {
        if (!drainRuntimeObjectExitQueue(state)) break;
    }
}
