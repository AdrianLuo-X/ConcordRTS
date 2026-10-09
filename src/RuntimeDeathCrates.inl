struct RuntimeDeathCrateSpawn {
    RuntimeCellPoint center;
    bool money = false;
    int sourceId = -1;
};

std::optional<RuntimeDeathCrateSpawn> prepareRuntimeDeathCrate(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeObjectExitRequest& request) {
    if (request.kind != RuntimeObjectExitKind::Destroyed || !object.alive
        || object.rubble || object.garrisonHostObjectId >= 0) return std::nullopt;
    RuntimeDeathCrateSpawn drop{runtimeCellPointForObject(object), false, object.instanceId};
    if (object.kind == WorldObjectKind::Structure) {
        const auto* building = state.rules.findBuildingType(object.type);
        if (!building || !building->crateBeneath) return std::nullopt;
        drop.money = building->crateBeneathIsMoney;
        const auto footprint = runtimeStructureFootprintCellsForObject(state, object);
        if (!footprint.empty()) {
            int x = 0, y = 0;
            for (const auto& cell : footprint) { x += cell.x; y += cell.y; }
            drop.center = {x / static_cast<int>(footprint.size()), y / static_cast<int>(footprint.size())};
        }
    } else {
        const auto* unit = object.kind == WorldObjectKind::Unit && !object.authoredAircraft
            ? state.rules.findUnitType("UnitType", object.type) : nullptr;
        if (!unit || !unit->carriesCrate) return std::nullopt;
        if (unit->isTrain ? !state.triggers.trainCrate : !state.triggers.truckCrate) return std::nullopt;
    }
    return drop;
}

bool spawnRuntimeDeathCrate(RuntimeWindowState& state,
    const std::optional<RuntimeDeathCrateSpawn>& drop) {
    if (!drop) return false;
    const auto& rules = state.rules.getCrateRules();
    std::unordered_set<int> reserved;
    for (const auto& crate : state.skirmishCrates)
        if (crate.active) reserved.insert(runtimeCellKey(crate.cell.x, crate.cell.y));
    if (static_cast<int>(reserved.size()) >= rules.maximum) return false;
    // The source footprint has already been released by the common exit path.
    // Prefer its center, then a nearby reachable tile; never put loot off-map,
    // in another building, on a cliff, or underneath an existing crate.
    const auto candidates = runtimeCratePlacementCandidates(state, reserved);
    const RuntimeCratePlacement* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (const auto& candidate : candidates) {
        const int dx = candidate.cell.x - drop->center.x;
        const int dy = candidate.cell.y - drop->center.y;
        if (std::abs(dx) > 6 || std::abs(dy) > 6) continue;
        const int distance = dx * dx + dy * dy;
        if (!best || distance < bestDistance || (distance == bestDistance
            && std::pair{candidate.cell.y, candidate.cell.x} < std::pair{best->cell.y, best->cell.x})) {
            best = &candidate; bestDistance = distance;
        }
    }
    if (!best) return false;
    RuntimeCrateState crate;
    crate.cell = best->cell;
    crate.water = best->water;
    crate.authored = true;
    crate.respawnTick = runtimeCrateTickAfter(state, runtimeCrateLifetimeTicks(state, crate, drop->sourceId));
    crate.imageId = runtimeCrateArtImageId(state.rules, crate.water ? rules.waterImage : rules.image);
    if (drop->money) {
        if (const auto* money = runtimeCrateRuleForType(state, "Money"))
            runtimeConfigureCratePowerup(crate, *money);
        else { crate.powerupType = "Money"; crate.powerupAnimation = "MONEY"; crate.powerupData = 2000.0; }
    } else runtimeEnsureCratePowerup(state, crate, drop->sourceId);
    state.skirmishCrates.push_back(std::move(crate));
    state.repaintRequested = true;
    return true;
}
