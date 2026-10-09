// Issue25/10: original Wall=yes overlays remain terrain-owned values.
// Included after projectile-terrain damage helpers; no WorldObject is promoted.
static const BuildingType* runtimeOverlayWallRuleForCell(
    const RuntimeWindowState& state, const TerrainCell& cell) {
    const auto& wall = cell.overlayWall;
    if (wall.health < 0 || wall.originalIndex < 0 || wall.originalType.empty()) return nullptr;
    const auto& overlays = state.rules.getOverlayTypes();
    if (wall.originalIndex >= static_cast<int>(overlays.size())) return nullptr;
    const auto& overlay = overlays[static_cast<std::size_t>(wall.originalIndex)];
    if (!overlay.wall || toUpperAsciiLocal(overlay.id) != toUpperAsciiLocal(wall.originalType)) return nullptr;
    const auto* building = findRuntimeBuildingTypeById(state.rules, wall.originalType);
    return building && building->wall && building->strength > 0
        && wall.health <= building->strength ? building : nullptr;
}

static bool runtimeOverlayWallUsesCurrentOverlay(const TerrainCell& cell) {
    return cell.hasOverlay && cell.overlayIndex == cell.overlayWall.originalIndex
        && (cell.overlayType.empty()
            || toUpperAsciiLocal(cell.overlayType) == toUpperAsciiLocal(cell.overlayWall.originalType));
}

bool runtimeIsOverlayWallTargetId(int id) {
    constexpr int first = std::numeric_limits<int>::min();
    return id >= first && id <= first + 512 * 512 - 1;
}


bool runtimeOverlayWallBehaviorTargetIsValid(
    const RuntimeWindowState& state, const RuntimeObjectBehavior& behavior) {
    if (!runtimeIsOverlayWallTargetId(behavior.targetObjectId)) return true;
    if (behavior.kind != RuntimeBehaviorKind::Attack || !behavior.forceFire
        || !behavior.attackGround || behavior.attackBridge) return false;
    const auto point = behavior.attackBridgeCell;
    if (point.x < 0 || point.x >= 512 || point.y < 0 || point.y >= 512
        || behavior.targetObjectId - std::numeric_limits<int>::min() != point.y * 512 + point.x)
        return false;
    for (const auto& cell : state.world.getTerrainCells())
        if (cell.x == point.x && cell.y == point.y)
            // A wall can die after firing while the command is still awaiting its next tick.
            return runtimeOverlayWallRuleForCell(state, cell) != nullptr
                && (cell.overlayWall.health == 0 || runtimeOverlayWallUsesCurrentOverlay(cell));
    return false;
}
std::optional<WorldObjectInstance> runtimeOverlayWallTargetAtCell(
    const RuntimeWindowState& state, const RuntimeCellPoint& point) {
    if (point.x < 0 || point.x >= 512 || point.y < 0 || point.y >= 512) return std::nullopt;
    const int key = runtimeCellKey(point.x, point.y);
    const TerrainCell* cell = nullptr;
    const auto found = state.terrainByCell.find(key);
    if (found != state.terrainByCell.end()) cell = found->second;
    if (!cell) for (const auto& candidate : state.world.getTerrainCells())
        if (candidate.x == point.x && candidate.y == point.y) { cell = &candidate; break; }
    if (!cell || cell->overlayWall.health <= 0 || !runtimeOverlayWallRuleForCell(state, *cell)
        || !runtimeOverlayWallUsesCurrentOverlay(*cell)) return std::nullopt;
    WorldObjectInstance target;
    target.instanceId = std::numeric_limits<int>::min() + point.y * 512 + point.x;
    target.kind = WorldObjectKind::Structure;
    target.mapId = "OverlayWall:" + std::to_string(key);
    target.type = cell->overlayWall.originalType;
    target.owner = "RuntimeMapWall";
    target.x = point.x; target.y = point.y; target.cell = key; target.level = cell->level;
    target.health = cell->overlayWall.health;
    target.alive = target.poweredOperational = true;
    return target;
}

std::optional<WorldObjectInstance> runtimeAttackGroundTargetObject(
    const RuntimeWindowState& state, const RuntimeObjectBehavior& behavior) {
    if (!behavior.attackGround) return std::nullopt;
    if (runtimeIsOverlayWallTargetId(behavior.targetObjectId)) {
        const int key = behavior.targetObjectId - std::numeric_limits<int>::min();
        if (behavior.attackBridgeCell.x < 0 || behavior.attackBridgeCell.x >= 512 || behavior.attackBridgeCell.y < 0 || behavior.attackBridgeCell.y >= 512 || key != behavior.attackBridgeCell.y * 512 + behavior.attackBridgeCell.x)
            return std::nullopt;
        return runtimeOverlayWallTargetAtCell(state, behavior.attackBridgeCell);
    }
    return runtimeGroundAttackTargetObject(state, behavior.attackBridgeCell);
}

void refreshRuntimeOverlayWallTopology(RuntimeWindowState& state) {
    std::unordered_map<int, TerrainCell*> tracked;
    std::unordered_set<int> playable;
    for (auto& cell : state.world.accessTerrainCells()) {
        const int key = runtimeCellKey(cell.x, cell.y);
        playable.insert(key);
        if (runtimeOverlayWallRuleForCell(state, cell)) tracked.emplace(key, &cell);
    }
    constexpr std::array<std::array<int, 3>, 4> directions{{
        {{0, -1, 1}}, {{1, 0, 2}}, {{0, 1, 4}}, {{-1, 0, 8}}
    }};
    for (const auto& [key, pointer] : tracked) {
        (void)key;
        auto& cell = *pointer;
        auto& wall = cell.overlayWall;
        if (wall.health == 0) {
            if (runtimeOverlayWallUsesCurrentOverlay(cell)) {
                cell.hasOverlay = false; cell.overlayIndex = -1; cell.overlayFrame = 0;
                cell.overlayType.clear(); cell.overlayImage.clear();
                cell.overlayIsOre = cell.overlayIsGem = false;
            }
            continue;
        }
        if (!runtimeOverlayWallUsesCurrentOverlay(cell)) continue;
        // A border neighbor absent from the playable terrain retains its authored bit.
        int mask = wall.originalFrame & 15;
        for (const auto& direction : directions) {
            const int x = cell.x + direction[0], y = cell.y + direction[1];
            if (x < 0 || x >= 512 || y < 0 || y >= 512) continue;
            const int neighborKey = runtimeCellKey(x, y);
            const auto neighbor = tracked.find(neighborKey);
            if (neighbor == tracked.end()) {
                if (playable.contains(neighborKey)) mask &= ~direction[2];
                continue;
            }
            const auto& adjacent = *neighbor->second;
            const bool connected = adjacent.overlayWall.health > 0
                && runtimeOverlayWallUsesCurrentOverlay(adjacent)
                && toUpperAsciiLocal(adjacent.overlayWall.originalType) == toUpperAsciiLocal(wall.originalType);
            mask = connected ? mask | direction[2] : mask & ~direction[2];
        }
        WorldObjectInstance artwork;
        artwork.kind = WorldObjectKind::Structure; artwork.type = wall.originalType;
        const auto* art = runtimeArtTypeForObject(state, artwork);
        const int levels = std::max(1, art ? art->damageLevels : 1);
        const int strength = runtimeOverlayWallRuleForCell(state, cell)->strength;
        int bank = 0;
        if (static_cast<long long>(wall.health) * 4 <= strength) bank = std::min(2, levels - 1);
        else if (static_cast<long long>(wall.health) * 2 <= strength) bank = std::min(1, levels - 1);
        // Extra authored banks are not classified by DamageLevels. Preserve their
        // original artwork while HP is positive; representative bootstrap HP is
        // an engine policy, not evidence of retail exact wall health.
        const int authoredBank = wall.originalFrame / 16;
        if (authoredBank >= levels) bank = authoredBank;
        cell.overlayFrame = mask + bank * 16;
    }
}

static bool runtimeOverlayWallSelectionCanAttack(
    RuntimeWindowState& state, const WorldObjectInstance& target) {
    for (int id : runtimeCombatSelection(state)) {
        const auto* source = findObjectById(state, id);
        if (!source || !source->alive || source->health <= 0 || source->owner != state.economy.owner
            || runtimeObjectDisabledByParasite(state, id) || !runtimeObjectHasAttackAmmo(state.rules, *source)) continue;
        const auto profile = runtimeWeaponProfileForObject(state, *source, &target, state.forceAttackHeld);
        if (profile.weapon && profile.weapon->damage > 0 && profile.weapon->range > 0
            && (!profile.projectile || profile.projectile->ag)) return true;
    }
    return false;
}

std::optional<WorldObjectInstance> visibleTerrainWallTargetAtPoint(
    RuntimeWindowState& state, const RECT& viewport, POINT point) {
    if (state.selectedObjectIds.empty() || !PtInRect(&viewport, point)) return std::nullopt;
    const auto& projected = runtimeProjectedObjectsForViewport(state, viewport);
    const auto* normal = findProjectedObjectAtPoint(projected, point);
    // Only an empty foundation fallback can yield to a real visible wall pixel.
    // Opaque structures, mobiles and final foreground aircraft keep their hit priority.
    if (normal && (!normal->usesFootprintHit
        || runtimeSpriteHitSpansContain(normal->structureSpriteHitSpans, point))) return std::nullopt;
    RuntimeMapRenderContext context;
    context.stableRenderConfiguration = true;
    context.theater = state.theaterLoaded ? &state.theater : nullptr;
    context.art = state.artLoaded ? &state.art : nullptr;
    context.rules = state.rulesLoaded ? &state.rules : nullptr;
    context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
    context.ownerRemapColors = state.ownerRemapColors.empty() ? nullptr : &state.ownerRemapColors;
    context.neutralRemapOwners = state.neutralRemapOwners.empty() ? nullptr : &state.neutralRemapOwners;
    const auto* paint = runtimeGeometryBatchRenderContext();
    if (!paint) {
        refreshRuntimeGateRenderSnapshots(state);
        refreshRuntimeSpecialMechanismRenderSnapshots(state);
    }
    context.gateSnapshots = &state.gateRenderSnapshots;
    context.specialMechanismSnapshots = &state.specialMechanismRenderSnapshots;
    if (runtimeLocalVisionRestricted(state)) {
        context.vision = &state.vision;
        context.visionObserverOwner = state.economy.owner;
    }
    context.tileRootPath = runtimeTileRootPath(state.config, state.report.map);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    context.animationTick = state.tick; context.fixedTickHz = state.config.fixedTickHz;
    context.impactActors = paint ? paint->impactActors : runtimeImpactRenderActorsForRender(state);
    context.selectedObjectIds = &state.selectedObjectIds;
    context.playerCheerUntilTick = &state.playerCommunication.cheerUntilTick;
    if (paint) context = *paint;
    RuntimeGeometryQueryScope geometry(state.world, viewport, context, state.camera, state.renderCache, true);
    std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>> spans;
    if (!resolveRuntimeBuildingVisibleHitSpans(state.world, viewport, context, state.camera, state.renderCache, spans))
        return std::nullopt;
    for (const auto& [id, pixels] : spans) {
        if (!runtimeIsOverlayWallTargetId(id) || !runtimeSpriteHitSpansContain(pixels, point)) continue;
        const int key = id - std::numeric_limits<int>::min();
        auto target = runtimeOverlayWallTargetAtCell(state, {key % 512, key / 512});
        if (target && runtimeObjectVisibleToOwner(state, state.economy.owner, *target)
            && runtimeOverlayWallSelectionCanAttack(state, *target)) return target;
    }
    return std::nullopt;
}

RuntimeTerrainProjectileDamageResult applyRuntimeProjectileOverlayWallDamage(
    RuntimeWindowState& state, const RuntimeProjectileEffect& projectile,
    const RuntimeCellPoint& impactCell, const WeaponType* weapon, const WarheadType* warhead) {
    RuntimeTerrainProjectileDamageResult result;
    if (!projectile.ag || !warhead || (!warhead->wall && !warhead->wood)
        || warhead->mindControl || warhead->electricAssault || warhead->temporal
        || warhead->ivanBomb || warhead->bombDisarm) return result;
    const double radius = std::max(0.0, projectile.cellSpread);
    std::vector<int> changed;
    for (auto& cell : state.world.accessTerrainCells()) {
        if (cell.overlayWall.health <= 0 || !runtimeOverlayWallRuleForCell(state, cell)
            || !runtimeOverlayWallUsesCurrentOverlay(cell)) continue;
        const double distance = runtimeCellDistance(impactCell, {cell.x, cell.y});
        if ((radius <= 0 && distance > 0) || (radius > 0 && distance > radius)) continue;
        auto target = runtimeOverlayWallTargetAtCell(state, {cell.x, cell.y});
        if (!target || !runtimeProjectileCanHitTarget(state, projectile, *target)) continue;
        long long baseDamage = projectile.damage;
        std::string damageWarhead = projectile.warheadId;
        if (projectile.spawnedMissileDamage) baseDamage = projectile.spawnedMissileBaseDamage;
        else if (!projectile.particleDamagePulse && !runtimeProjectileHasLockedDeathWeaponDamage(projectile) && weapon) {
            baseDamage = runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon);
            damageWarhead = weapon->warhead;
        }
        if (baseDamage <= 0) continue;
        const int damage = runtimeAuthoredDamageAgainst(state, *target, baseDamage, damageWarhead,
            distance, projectile.veteranDamageMultiplier);
        if (damage <= 0) continue;
        const int before = cell.overlayWall.health;
        cell.overlayWall.health = static_cast<int>(std::max(0LL, static_cast<long long>(before) - damage));
        ++result.objectsDamaged;
        result.totalDamage += before - cell.overlayWall.health;
        if (cell.overlayWall.health == 0) ++result.objectsDestroyed;
        changed.push_back(runtimeCellKey(cell.x, cell.y));
    }
    if (changed.empty()) return result;
    refreshRuntimeOverlayWallTopology(state);
    for (int key : changed) {
        const auto found = state.terrainByCell.find(key);
        if (found != state.terrainByCell.end() && found->second)
            state.terrainInfoByCell[key] = runtimeNavigationTerrainInfoForCell(state, *found->second);
    }
    state.attackApproachCache.clear(); state.pathFailureCache.clear();
    state.pathTerrainCache.valid = false; state.pathTerrainCache.terrainCellCount = 0;
    state.pathTerrainCache.staticCells.clear(); state.pathTerrainCache.dynamicValid = false;
    state.pathTerrainCache.dynamicCells.clear();
    invalidateRuntimeTickOccupancyCache(state); invalidateRuntimeObjectProjectionCache(state);
    state.renderCache.terrainLayer = RuntimeTerrainLayerCache{};
    state.placementChecksCacheTick = -1;
    state.radarTerrainKey.clear(); state.radarProjectionCacheValid = false;
    state.navigationRefreshPending = true;
    state.repaintRequested = true;
    return result;
}
