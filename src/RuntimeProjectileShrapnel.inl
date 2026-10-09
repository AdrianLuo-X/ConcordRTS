// Retail BulletClass::Shrapnel (gamemd 0x46A310): only a hit on a
// non-building occupant releases fragments. Search occupied cells in native
// CellSpread order, then aim the remaining rounds at random nearby cells.
int runtimeShrapnelCellRing(int dx, int dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

int spawnRuntimeProjectileShrapnel(
    RuntimeWindowState& state, const RuntimeProjectileEffect& parent,
    const RuntimeCellPoint& impactCell, int impactLevel
) {
    if (parent.generation >= 4) return 0;
    const auto* projectile = findRuntimeProjectileType(state.rules, parent.projectileId);
    if (!projectile || projectile->shrapnelCount <= 0
        || trimAsciiLocal(projectile->shrapnelWeapon).empty()) return 0;
    const auto* weapon = findRuntimeWeaponType(state.rules, projectile->shrapnelWeapon);
    if (!weapon || weapon->neverUse || weapon->damage <= 0 || weapon->range <= 0.0) return 0;

    // The damage path may already have marked the struck unit dead. Its saved
    // identity still describes this hit; do not turn a lethal hit into a miss.
    const auto* struck = findRuntimeObjectByIdIncludingInactive(state, parent.targetObjectId);
    if (!struck) {
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || runtimeObjectIsAirTarget(state, object)) continue;
            const auto cells = object.kind == WorldObjectKind::Structure
                ? runtimeStructureFootprintCellsForObject(state, object)
                : std::vector<RuntimeCellPoint>{runtimeCellPointForObject(object)};
            if (std::any_of(cells.begin(), cells.end(), [&](const auto& cell) {
                return cell.x == impactCell.x && cell.y == impactCell.y;
            })) { struck = &object; break; }
        }
    }
    if (!struck || struck->kind == WorldObjectKind::Structure) return 0;

    struct Target { int objectId; int ring; int dx; int dy; };
    std::vector<Target> targets;
    std::unordered_set<int> occupied;
    const int range = static_cast<int>(std::clamp(std::floor(weapon->range), 0.0, 256.0));
    for (const auto& candidate : state.world.getObjects()) {
        if (!candidate.alive || runtimeObjectIsAirTarget(state, candidate)) continue;
        const auto cells = candidate.kind == WorldObjectKind::Structure
            ? runtimeStructureFootprintCellsForObject(state, candidate)
            : std::vector<RuntimeCellPoint>{runtimeCellPointForObject(candidate)};
        for (const auto& cell : cells) {
            const int dx = cell.x - impactCell.x, dy = cell.y - impactCell.y;
            const int ring = runtimeShrapnelCellRing(dx, dy);
            if (ring == 0 || ring > range || !occupied.insert(runtimeCellKey(cell.x, cell.y)).second) continue;
            // Retail checks the first ground object in each cell, and only
            // excludes the firer and allies. A building can occupy many cells.
            if (candidate.instanceId == parent.attackerObjectId
                || runtimeOwnersAreAllied(state, candidate.owner, parent.owner)) continue;
            targets.push_back({candidate.instanceId, ring, dx, dy});
        }
    }
    std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) {
        if (a.ring != b.ring) return a.ring < b.ring;
        if (a.dy != b.dy) return a.dy < b.dy;
        // The first three entries of the retail table run right to left.
        return a.ring == 1 && a.dy == -1 ? a.dx > b.dx : a.dx < b.dx;
    });

    WorldObjectInstance source;
    source.instanceId = parent.attackerObjectId; source.kind = WorldObjectKind::Unit;
    source.owner = parent.owner; source.type = "SHRAPNEL";
    source.x = impactCell.x; source.y = impactCell.y;
    source.cell = runtimeCellKey(source.x, source.y); source.level = impactLevel;
    source.facing = source.turretFacing = parent.targetFacing; source.alive = true;
    RuntimeWeaponProfile profile;
    finalizeRuntimeWeaponProfile(profile, state, RuntimeWeaponSlot::Indexed, *weapon, "shrapnel");
    auto random = runtimeCombatRandomMix(state.config.simulationRandomSeed
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(parent.actorId)) << 32U)
        ^ static_cast<std::uint32_t>(parent.attackerObjectId) ^ 0x53485241504e454cULL);
    const int count = std::min(projectile->shrapnelCount, 256);
    for (int i = 0; i < count; ++i) {
        WorldObjectInstance ground;
        const WorldObjectInstance* target = i < static_cast<int>(targets.size())
            ? findObjectById(state, targets[i].objectId) : nullptr;
        if (!target) {
            // gamemd 0x46A958/0x46A976: independently draw X and Y from [-2,2].
            random = runtimeCombatRandomMix(random); ground.x = impactCell.x + static_cast<int>(random % 5) - 2;
            random = runtimeCombatRandomMix(random); ground.y = impactCell.y + static_cast<int>(random % 5) - 2;
            ground.instanceId = -1; ground.kind = WorldObjectKind::Unit;
            ground.owner = "Neutral"; ground.type = "SHRAPNELCELL"; ground.alive = true;
            ground.cell = runtimeCellKey(ground.x, ground.y);
            ground.level = terrainLevelAtRuntimeCell(state, ground.x, ground.y);
            target = &ground;
        }
        const int damage = runtimeAuthoredDamageAgainst(state, *target,
            static_cast<long long>(weapon->damage) * std::max(1, weapon->burst),
            weapon->warhead, 0.0, parent.veteranDamageMultiplier);
        spawnRuntimeProjectile(state, source, *target, *weapon, profile, damage,
            parent.actorId, parent.generation + 1, true, parent.targetObjectId, parent.veteranDamageMultiplier,
            parent.deathWeaponCascadeId, parent.deathWeaponRequestId, parent.deathWeaponCascadeDepth,
            true, &parent);
    }
    return count;
}
