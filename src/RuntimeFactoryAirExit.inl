bool runtimeFactoryUsesAirExit(const RuntimeWindowState& state,
    const WorldObjectInstance& factory, const WorldObjectInstance& produced) {
    const auto* type = findRuntimeBuildingTypeById(state.rules, factory.type);
    return factory.kind == WorldObjectKind::Structure && type && type->weaponsFactory
        && toUpperAsciiLocal(type->factory) == "UNITTYPE"
        && runtimeObjectMovementMode(state, produced) == PathMovementMode::Air;
}

std::optional<RuntimeCellPoint> runtimeFactoryAirExitSpawnCell(const RuntimeWindowState& state,
    const WorldObjectInstance& factory) {
    const auto footprint = runtimeStructureFootprintForObject(state, factory);
    const RuntimeCellPoint cell{factory.x + (std::max(1, footprint.width) - 1) / 2,
        factory.y + (std::max(1, footprint.height) - 1) / 2};
    if (state.terrainByCell.find(runtimeCellKey(cell.x, cell.y)) == state.terrainByCell.end())
        return std::nullopt;
    for (const auto& object : state.world.getObjects())
        if (object.alive && object.factoryExitHostId == factory.instanceId) return std::nullopt;
    return cell;
}

const WorldObjectInstance* runtimeFactoryAirExitContainingHost(const RuntimeWindowState& state,
    const WorldObjectInstance& produced) {
    if (produced.factoryExitHostId < 0) return nullptr;
    const auto* host = findObjectById(state, produced.factoryExitHostId);
    if (!host || !host->alive || !host->factoryExitAirborne) return nullptr;
    const auto footprint = runtimeStructureFootprintForObject(state, *host);
    const double x = runtimeObjectRenderX(produced), y = runtimeObjectRenderY(produced);
    return x >= host->x && x < host->x + std::max(1, footprint.width)
        && y >= host->y && y < host->y + std::max(1, footprint.height) ? host : nullptr;
}

bool runtimeFactoryAirExitWaitingForRoof(const RuntimeWindowState& state,
    const WorldObjectInstance& produced) {
    const auto* host = runtimeFactoryAirExitContainingHost(state, produced);
    return host && state.tick - host->factoryExitAnimationStartTick
        < runtimeFactoryAirExitCloseTicks(state, *host);
}

bool runtimeFactoryAirExitReadyToMove(const RuntimeWindowState& state,
    const WorldObjectInstance& produced) {
    const auto* host = runtimeFactoryAirExitContainingHost(state, produced);
    if (!host) return true;
    if (runtimeFactoryAirExitWaitingForRoof(state, produced)) return false;
    const auto* unit = findRuntimeUnitType(state.rules, produced);
    if (!unit || !runtimeUnitUsesJumpjetMovement(*unit)) return true;
    const auto flight = state.jumpjetFlights.find(produced.instanceId);
    const auto height = flight == state.jumpjetFlights.end()
        ? produced.flightLevel * runtimeJumpjetCellQ16 : flight->second.height;
    return height >= terrainLevelAtRuntimeCell(state, produced.x, produced.y) * runtimeJumpjetCellQ16
        + unit->jumpJetHeight * runtimeJumpjetQ16;
}
