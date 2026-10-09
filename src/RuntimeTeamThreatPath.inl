struct RuntimePathThreatArea {
    int objectId = -1;
    int x = 0, y = 0, width = 1, height = 1;
    double range = 0.0;
    int cost = 0;
};

double runtimePathThreatAvoidance(const RuntimeWindowState& state,
    const WorldObjectInstance& mover) {
    for (const auto& [id, mission] : state.triggers.teamMissions) {
        if (mission.completed || std::find(mission.objectIds.begin(), mission.objectIds.end(),
                mover.instanceId) == mission.objectIds.end()) continue;
        const auto* team = findRuntimeTriggerTeamById(state, mission.teamTypeId);
        if (team && team->avoidThreats) return 1.0;
    }
    for (const auto& [owner, tactics] : state.triggers.computerTacticsByOwner)
        for (const auto& [id, squad] : tactics.teams) {
            if (std::find(squad.objectIds.begin(), squad.objectIds.end(), mover.instanceId) == squad.objectIds.end()) continue;
            const auto* team = findRuntimeTriggerTeamById(state, squad.teamTypeId);
            if (team && team->avoidThreats) return 1.0;
        }
    const auto* unit = findRuntimeUnitType(state.rules, mover);
    return unit ? unit->threatAvoidanceCoefficient : 0.0;
}

std::vector<RuntimePathThreatArea> runtimePathThreatAreas(const RuntimeWindowState& state,
    const WorldObjectInstance& mover, double coefficient) {
    std::vector<RuntimePathThreatArea> result;
    if (coefficient <= 0.0) return result;
    for (const auto& enemy : state.world.getObjects()) {
        if (!enemy.alive || enemy.rubble || enemy.buildAnimTicksRemaining > 0
            || !enemy.poweredOperational || !runtimeOwnersAreHostileForAutoTarget(state, mover.owner, enemy.owner)
            || !runtimeObjectVisibleToOwner(state, mover.owner, enemy)) continue;
        const auto* unit = findRuntimeUnitType(state.rules, enemy);
        const auto* building = enemy.kind == WorldObjectKind::Structure
            ? findRuntimeBuildingType(state.rules, enemy) : nullptr;
        const int posed = building ? building->threatPosed : unit ? unit->threatPosed : 0;
        if (posed <= 0) continue;
        const auto profile = runtimeWeaponProfileForObject(state, enemy, &mover);
        if (!profile.weapon || profile.weapon->damage <= 0 || !runtimeWeaponHasAttackRange(*profile.weapon)) continue;
        const double range = runtimeEffectiveWeaponRange(state, enemy, mover, *profile.weapon);
        // Unlimited carrier targeting does not make every cell impassable.
        if (!std::isfinite(range)) continue;
        const auto footprint = runtimeStructureFootprintForObject(state, enemy);
        result.push_back({enemy.instanceId, enemy.x, enemy.y, footprint.width, footprint.height,
            range, static_cast<int>(std::lround(std::clamp(posed * coefficient, 1.0, 1000.0)))});
    }
    return result;
}

int runtimePathThreatCost(const std::vector<RuntimePathThreatArea>& threats, const PathCell& cell) {
    int cost = 0;
    for (const auto& threat : threats) {
        const int dx = std::max({threat.x - cell.x, 0, cell.x - (threat.x + threat.width - 1)});
        const int dy = std::max({threat.y - cell.y, 0, cell.y - (threat.y + threat.height - 1)});
        if (static_cast<double>(dx) * dx + static_cast<double>(dy) * dy <= threat.range * threat.range)
            cost = std::min(4000, cost + threat.cost);
    }
    return cost;
}
