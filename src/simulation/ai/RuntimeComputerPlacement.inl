bool runtimeComputerPlacementPreservesExits(const RuntimeWindowState& state,
    const RuntimeBuildCatalogItem& item, RuntimeCellPoint cell, const std::string& owner) {
    WorldObjectInstance candidate;
    candidate.kind = WorldObjectKind::Structure; candidate.type = item.typeId;
    candidate.owner = owner; candidate.x = cell.x; candidate.y = cell.y;
    const auto footprint = runtimeStructureFootprintForObject(state, candidate);
    const auto overlaps = [&](RuntimeCellPoint point) {
        return point.x >= cell.x && point.y >= cell.y
            && point.x < cell.x + footprint.width && point.y < cell.y + footprint.height;
    };
    for (const auto& factory : state.world.getObjects()) {
        if (!factory.alive || factory.kind != WorldObjectKind::Structure
            || toLowerAsciiLocal(factory.owner) != toLowerAsciiLocal(owner)) continue;
        const auto* type = findRuntimeBuildingType(state.rules, factory);
        // Shipyards also have WeaponsFactory=yes. They use the naval spawn
        // search around the foundation, not the land factory's east corridor.
        if (!type || !type->weaponsFactory || type->naval || type->waterBound
            || toUpperAsciiLocal(type->factory) != "UNITTYPE") continue;
        const auto shape = runtimeStructureFootprintForObject(state, factory);
        const RuntimeCellPoint door{factory.x + shape.width, factory.y + shape.height / 2};
        for (int outward = 0; outward < 3; ++outward)
            if (overlaps({door.x + outward, door.y})) return false;
    }
    const auto* type = findRuntimeBuildingTypeById(state.rules, item.typeId);
    if (type && type->weaponsFactory && !type->naval && !type->waterBound
        && toUpperAsciiLocal(type->factory) == "UNITTYPE") {
        const RuntimeCellPoint door{cell.x + footprint.width, cell.y + footprint.height / 2};
        for (int outward = 0; outward < 3; ++outward) {
            const RuntimeCellPoint point{door.x + outward, door.y};
            const auto* terrain = runtimeProductionTerrainCellAt(state, point);
            if (!terrain) return false;
            const auto info = runtimeCachedTerrainInfoForCell(state, *terrain);
            if (!info.passable || (info.water && !info.bridge && !info.shore)) return false;
            for (const auto& other : state.world.getObjects()) {
                if (!other.alive || other.kind != WorldObjectKind::Structure) continue;
                const auto shape = runtimeStructureFootprintForObject(state, other);
                if (point.x >= other.x && point.y >= other.y
                    && point.x < other.x + shape.width && point.y < other.y + shape.height) return false;
            }
        }
    }
    return true;
}
