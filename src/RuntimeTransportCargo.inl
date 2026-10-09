// The original Passengers budget is measured in Size, while infantry
// absorbers such as the bio reactor count people independently of Size.
int runtimeTransportPassengerSpace(
    const RuntimeWindowState& state, const WorldObjectInstance& transport,
    const WorldObjectInstance& passenger
) {
    if (runtimeInfantryAbsorberType(state, transport) != nullptr) return 1;
    const auto* type = findRuntimeUnitType(state.rules, passenger);
    return type == nullptr ? 1 : std::max(1, type->size);
}

bool runtimeTransportPassengerTypeAllowed(
    const RuntimeWindowState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport
) {
    const bool absorber = runtimeInfantryAbsorberType(state, transport) != nullptr;
    if (passenger.kind != WorldObjectKind::Infantry && passenger.kind != WorldObjectKind::Unit) return false;
    const auto* type = findRuntimeUnitType(state.rules, passenger);
    if (type == nullptr) return false;
    const int limit = runtimeTransportPassengerSizeLimit(state, transport);
    if (limit > 0 && std::max(1, type->size) > limit) return false;
    if (passenger.kind == WorldObjectKind::Unit) {
        const auto movement = runtimeObjectMovementMode(state, passenger);
        return !absorber && limit > 0 && movement != PathMovementMode::Air
            && movement != PathMovementMode::Naval;
    }
    return true;
}

int runtimeTransportUsedSpace(const RuntimeWindowState& state, const WorldObjectInstance& transport) {
    std::int64_t used = 0;
    const auto& objects = state.world.getObjects();
    for (int id : transport.transportPassengerIds) {
        const auto found = std::find_if(objects.begin(), objects.end(),
            [id](const auto& object) { return object.instanceId == id; });
        used += found == objects.end() ? 1 : runtimeTransportPassengerSpace(state, transport, *found);
    }
    return static_cast<int>(std::min<std::int64_t>(used, std::numeric_limits<int>::max()));
}

bool runtimeAmphibiousTransport(const RuntimeWindowState& state, const WorldObjectInstance& transport) {
    const auto* type = findRuntimeUnitType(state.rules, transport);
    return transport.kind == WorldObjectKind::Unit && type != nullptr && type->passengers > 0
        && type->naval && runtimeObjectMovementMode(state, transport) == PathMovementMode::Amphibious;
}

bool runtimeTransportContainsCargo(
    const RuntimeWindowState& state, const WorldObjectInstance& root, int candidateId
) {
    std::vector<int> pending = root.transportPassengerIds;
    std::unordered_set<int> seen{root.instanceId};
    const auto& objects = state.world.getObjects();
    while (!pending.empty()) {
        const int id = pending.back(); pending.pop_back();
        if (id == candidateId) return true;
        if (!seen.insert(id).second) continue;
        const auto found = std::find_if(objects.begin(), objects.end(),
            [id](const auto& object) { return object.instanceId == id; });
        if (found != objects.end()) pending.insert(pending.end(),
            found->transportPassengerIds.begin(), found->transportPassengerIds.end());
    }
    return false;
}

void syncRuntimeTransportCargoPositions(RuntimeWindowState& state, const WorldObjectInstance& root) {
    if (root.transportPassengerIds.empty()) return;
    std::vector<int> pending = root.transportPassengerIds;
    std::unordered_set<int> seen{root.instanceId};
    auto& objects = state.world.accessObjects();
    while (!pending.empty()) {
        const int id = pending.back(); pending.pop_back();
        if (!seen.insert(id).second) continue;
        const auto found = std::find_if(objects.begin(), objects.end(),
            [id](const auto& object) { return object.instanceId == id; });
        if (found == objects.end() || found->alive || found->health <= 0) continue;
        found->x = root.x; found->y = root.y; found->cell = root.cell;
        found->level = root.level; found->onBridge = root.onBridge;
        found->subCell = -1; found->hasRenderPosition = found->renderMoveActive = false;
        pending.insert(pending.end(), found->transportPassengerIds.begin(), found->transportPassengerIds.end());
    }
}
