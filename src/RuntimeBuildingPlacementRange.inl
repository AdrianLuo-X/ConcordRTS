// Sidebar construction only. MCV deployment, scripted spawns and initial-base
// setup use the physical foundation check without requiring a pre-existing base.
bool runtimeBuildingPlacementAnchorEligible(
    const RuntimeWindowState& state,
    const WorldObjectInstance& anchor,
    const std::string& placementOwner,
    bool placingWall = false
) {
    if (!anchor.alive || anchor.health <= 0 || anchor.rubble
        || anchor.kind != WorldObjectKind::Structure
        || runtimeGateObjectIsLimbo(anchor)) {
        return false;
    }
    const BuildingType* type = findRuntimeBuildingTypeById(state.rules, anchor.type);
    if (!type || type->invisibleInGame) return false;
    const auto ownerKey = [&](const std::string& owner) {
        std::string key = runtimeCanonicalHumanHouseOwner(owner);
        if (state.campaignMode) {
            const auto found = state.triggers.campaignCanonicalHouseByAlias.find(key);
            if (found != state.triggers.campaignCanonicalHouseByAlias.end())
                key = runtimeCanonicalHumanHouseOwner(found->second);
        }
        return key;
    };
    const std::string wanted = ownerKey(placementOwner);
    if (wanted.empty()) return false;
    if (ownerKey(anchor.owner) == wanted) {
        // Owned walls can extend a wall chain, but never extend the base for
        // ordinary buildings. A buildup already occupies its foundation.
        return type->baseNormal || (placingWall && type->wall);
    }
    return state.config.buildOffAlly && type->baseNormal
        && type->eligibleForAllyBuilding
        && runtimeOwnersAreAllied(state, placementOwner, anchor.owner);
}

bool runtimeBuildingPlacementInRange(
    const RuntimeWindowState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& cell,
    const std::string& placementOwner = {}
) {
    if (item.kind != RuntimeBuildKind::Building && item.kind != RuntimeBuildKind::Defense)
        return false;
    const BuildingType* type = findRuntimeBuildingTypeById(state.rules, item.typeId);
    const auto footprint = runtimeStructureFootprintForBuildItem(state, item);
    // Adjacent counts the empty cells between foundations: zero still allows
    // touching foundations. Use 64-bit bounds for authored large INI values.
    const std::int64_t padding = static_cast<std::int64_t>(type ? type->adjacent : 1) + 1;
    const std::int64_t minX = static_cast<std::int64_t>(cell.x) - padding;
    const std::int64_t minY = static_cast<std::int64_t>(cell.y) - padding;
    const std::int64_t maxX = static_cast<std::int64_t>(cell.x) + footprint.width - 1 + padding;
    const std::int64_t maxY = static_cast<std::int64_t>(cell.y) + footprint.height - 1 + padding;
    const std::string& owner = placementOwner.empty() ? state.economy.owner : placementOwner;
    for (const auto& anchor : state.world.getObjects()) {
        if (!runtimeBuildingPlacementAnchorEligible(state, anchor, owner, type && type->wall))
            continue;
        const auto anchorCell = runtimeCellPointForObject(anchor);
        const auto anchorFootprint = runtimeStructureFootprintForObject(state, anchor);
        if (static_cast<std::int64_t>(anchorCell.x) <= maxX
            && static_cast<std::int64_t>(anchorCell.y) <= maxY
            && static_cast<std::int64_t>(anchorCell.x) + anchorFootprint.width - 1 >= minX
            && static_cast<std::int64_t>(anchorCell.y) + anchorFootprint.height - 1 >= minY) {
            return true;
        }
    }
    return false;
}
