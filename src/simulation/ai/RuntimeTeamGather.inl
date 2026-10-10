RuntimeCellPoint runtimeTeamHouseCenter(const RuntimeWindowState& state,
    const std::string& owner, RuntimeCellPoint fallback) {
    long long x = 0, y = 0; int count = 0;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure
            || toLowerAsciiLocal(object.owner) != toLowerAsciiLocal(owner)) continue;
        x += object.x; y += object.y; ++count;
    }
    return count ? RuntimeCellPoint{static_cast<int>(x / count), static_cast<int>(y / count)} : fallback;
}

RuntimeTeamStepDisposition issueRuntimeTeamGather(RuntimeWindowState& state,
    RuntimeTeamMissionState& mission, bool outsideEnemyBase) {
    const auto members = runtimeLiveTeamObjectIds(state, mission);
    if (members.empty()) return RuntimeTeamStepDisposition::Complete;
    const auto owner = runtimeTeamMissionOwner(state, mission);
    const auto center = runtimeTeamMissionCenter(state, mission);
    const bool hasOwnBase = std::any_of(state.world.getObjects().begin(), state.world.getObjects().end(), [&](const auto& object) {
        return object.alive && object.kind == WorldObjectKind::Structure
            && toLowerAsciiLocal(object.owner) == toLowerAsciiLocal(owner);
    });
    const auto ownBase = runtimeTeamHouseCenter(state, owner, center);
    const auto* enemy = runtimeTeamEnemyTarget(state, mission, 2);
    const auto enemyBase = enemy
        ? runtimeTeamHouseCenter(state, enemy->owner, runtimeCellPointForObject(*enemy)) : center;
    if (outsideEnemyBase && !enemy) return RuntimeTeamStepDisposition::Complete;
    const auto origin = outsideEnemyBase ? enemyBase : ownBase;
    const auto toward = outsideEnemyBase ? ownBase : enemyBase;
    double dx = toward.x - origin.x, dy = toward.y - origin.y;
    const double length = std::hypot(dx, dy);
    if (length > 0.0) { dx /= length; dy /= length; }
    else { dx = 1.0; dy = 0.0; }
    const int distance = state.rules.getComputerTacticsRules().safeDistance;
    RuntimeCellPoint destination{origin.x + static_cast<int>(std::lround(dx * distance)),
        origin.y + static_cast<int>(std::lround(dy * distance))};
    if (!outsideEnemyBase && !hasOwnBase) destination = center;
    const auto* leader = findObjectById(state, members.front());
    const auto passable = leader ? findRuntimePassableCellNear(state, *leader, destination, 10) : std::nullopt;
    if (!passable) return RuntimeTeamStepDisposition::Waiting;
    mission.targetCell = *passable;
    // Gathering closes ranks. The ordinary player's move intentionally
    // preserves a group's old offsets, which can straddle a whole building.
    std::unordered_set<int> reserved;
    int issued = 0;
    for (std::size_t index = 0; index < members.size(); ++index) {
        const auto* actor = findObjectById(state, members[index]);
        if (!actor) continue;
        const auto offset = formationOffsetForIndex(static_cast<int>(index));
        const auto cell = findRuntimePassableCellNear(state, *actor,
            {passable->x + offset.x, passable->y + offset.y}, 4, -1, &reserved);
        if (!cell) continue;
        reserved.insert(runtimeCellKey(cell->x, cell->y));
        issued += issueRuntimeTeamMove(state, {actor->instanceId}, *cell);
    }
    return issued > 0 ? RuntimeTeamStepDisposition::Waiting : RuntimeTeamStepDisposition::Complete;
}

RuntimeCellPoint runtimeTeamSuperWeaponCenter(const RuntimeWindowState& state,
    const RuntimeTeamMissionState& mission, const SuperWeaponRule& rule) {
    const auto average = runtimeTeamMissionCenter(state, mission);
    std::vector<RuntimeCellPoint> cells;
    for (int id : mission.objectIds)
        if (const auto* member = findObjectById(state, id); member && !member->airborne)
            cells.push_back(runtimeCellPointForObject(*member));
    const int radius = std::max(1, rule.radius > 0 ? rule.radius
        : static_cast<int>(std::ceil(std::max(1.0, rule.range))));
    const auto covered = [&](RuntimeCellPoint point) {
        return std::count_if(cells.begin(), cells.end(), [&](RuntimeCellPoint cell) {
            return std::max(std::abs(cell.x - point.x), std::abs(cell.y - point.y)) <= radius;
        });
    };
    auto best = average; auto coverage = covered(best);
    for (auto cell : cells) if (const auto count = covered(cell); count > coverage) {
        best = cell; coverage = count;
    }
    return best;
}
