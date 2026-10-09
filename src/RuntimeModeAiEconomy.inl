std::vector<int> runtimeComputerAiNeutralOilTargets(const RuntimeWindowState& state) {
    std::vector<int> targets;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure
            || !runtimeOwnerIsNeutralForAutoTarget(object.owner)) continue;
        const auto* building = findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr && building->capturable
            && building->produceCashAmount > 0 && building->produceCashDelay > 0)
            targets.push_back(object.instanceId);
    }
    std::sort(targets.begin(), targets.end());
    return targets;
}

bool runtimeComputerAiNeedsOilEngineer(const RuntimeWindowState& state,
    const RuntimeComputerAiState& ai) {
    const auto targets = runtimeComputerAiNeutralOilTargets(state);
    if (targets.empty()) return false;
    int engineers = 0;
    for (const auto& object : state.world.getObjects())
        if (object.alive && runtimeComputerAiOwnerMatches(object, ai.owner)
            && runtimeObjectIsEngineer(state, object)) ++engineers;
    return engineers < std::min(2, static_cast<int>(targets.size()));
}

bool advanceRuntimeComputerAiOilCapture(RuntimeWindowState& state,
    const RuntimeComputerAiState& ai) {
    if (!runtimeIsMegawealthMode(state)) return false;
    auto targets = runtimeComputerAiNeutralOilTargets(state);
    if (targets.empty()) return false;
    std::set<int> reserved;
    for (const auto& [engineerId, targetId] : state.pendingEngineerCaptures) {
        const auto* engineer = findObjectById(state, engineerId);
        if (engineer != nullptr && engineer->alive
            && runtimeComputerAiOwnerMatches(*engineer, ai.owner)) reserved.insert(targetId);
    }
    std::vector<int> engineers;
    for (const auto& object : state.world.getObjects())
        if (object.alive && runtimeComputerAiOwnerMatches(object, ai.owner)
            && runtimeObjectIsEngineer(state, object) && runtimeObjectCanMove(state, object)
            && !state.pendingEngineerCaptures.contains(object.instanceId))
            engineers.push_back(object.instanceId);
    std::sort(engineers.begin(), engineers.end());
    bool changed = false;
    for (int id : engineers) {
        const auto* engineer = findObjectById(state, id);
        if (engineer == nullptr) continue;
        const auto origin = runtimeCellPointForObject(*engineer);
        std::sort(targets.begin(), targets.end(), [&](int a, int b) {
            const auto* left = findObjectById(state, a);
            const auto* right = findObjectById(state, b);
            const double da = runtimeCellDistance(origin, runtimeCellPointForObject(*left));
            const double db = runtimeCellDistance(origin, runtimeCellPointForObject(*right));
            return da != db ? da < db : a < b;
        });
        for (int targetId : targets) {
            if (reserved.contains(targetId)) continue;
            const auto* target = findObjectById(state, targetId);
            if (target != nullptr && runtimeEngineerCanCaptureStructure(state, *engineer, *target)
                && queueRuntimeEngineerCaptureForObject(state, id, *target, false)) {
                reserved.insert(targetId);
                changed = true;
                break;
            }
        }
    }
    return changed;
}
