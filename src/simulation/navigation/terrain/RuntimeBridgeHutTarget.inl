// Type identity survives death so a failed bridge demolition cannot fall
// through to ordinary C4 destruction of the repair hut itself.
bool runtimeObjectIsBridgeRepairHut(
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
    const BuildingType* building = object.kind == WorldObjectKind::Structure
        ? findRuntimeBuildingTypeById(state.rules, object.type) : nullptr;
    return building != nullptr && building->bridgeRepairHut;
}

bool runtimeBridgeHutDemolitionAvailable(
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
    if (!runtimeObjectIsBridgeRepairHut(state, object)
        || !object.alive || object.health <= 0 || !state.destroyableBridges) {
        return false;
    }
    const auto found = state.bridgeRepairSpansByHut.find(object.instanceId);
    if (found == state.bridgeRepairSpansByHut.end()) return false;
    const RuntimeBridgeRepairSpan& span = found->second;
    if (span.hutObjectId != object.instanceId || span.cells.empty()
        || !std::any_of(span.cells.begin(), span.cells.end(),
            [](const RuntimeBridgeRepairCell& cell) { return cell.requiresOverlay; })
        || span.level != object.level || span.overlayIndex < 0
        || span.startHead.x < 0 || span.startHead.y < 0
        || span.endHead.x < 0 || span.endHead.y < 0) {
        return false;
    }
    return span.alongX
        ? span.startHead.y == span.endHead.y && span.startHead.x != span.endHead.x
        : span.startHead.x == span.endHead.x && span.startHead.y != span.endHead.y;
}

bool runtimeWeaponUsesBridgeHutDemolition(
    const WeaponType* weapon, const WarheadType* warhead
) {
    return (weapon != nullptr && (weapon->suicide || weapon->sabotageCursor))
        || (warhead != nullptr && warhead->ivanBomb);
}

bool runtimeBridgeHutDemolitionCommandLegal(
    const RuntimeWindowState& state, const WorldObjectInstance& target,
    const WeaponType* weapon, const WarheadType* warhead,
    bool sourceHasC4 = false
) {
    // Ivan plants on the building itself; only demolition needs a valid,
    // destroyable bridge. Keep C4 and other bridge weapons on that policy.
    return !runtimeObjectIsBridgeRepairHut(state, target)
        || (warhead != nullptr && warhead->ivanBomb)
        || (!sourceHasC4 && !runtimeWeaponUsesBridgeHutDemolition(weapon, warhead))
        || runtimeBridgeHutDemolitionAvailable(state, target);
}
