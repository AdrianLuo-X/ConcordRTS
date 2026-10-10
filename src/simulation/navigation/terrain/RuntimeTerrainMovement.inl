int runtimeMaxHealthForObject(const RuntimeWindowState&, const WorldObjectInstance&);

// Only Drive/Ship locomotors feed the retail land table into actual speed.
// Hover and Walk use it for reachability but set SpeedPercentage to 0 or 1.
bool runtimeObjectUsesTerrainMovementSpeed(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    if (object.kind != WorldObjectKind::Unit || object.airborne) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!type) return false;
    const auto loco = toLowerAsciiLocal(trimAsciiLocal(type->locomotor));
    if (!loco.empty()) return loco == "{4a582741-9839-11d1-b709-00a024ddafd1}"
        || loco == "{2bea74e1-7cca-11d3-be14-00104b62a16c}";
    // Sparse runtime fixtures and old custom rules omit the default Drive GUID.
    return !type->organic && !type->teleporter && !runtimeUnitUsesAirMovement(*type)
        && toLowerAsciiLocal(type->speedType) != "hover";
}

double runtimeMovementLandPercent(const LandType& land, const UnitType& type) {
    const auto speed = toLowerAsciiLocal(trimAsciiLocal(type.speedType));
    if (speed == "track") return land.track;
    if (speed == "foot") return land.foot;
    if (speed == "wheel") return land.wheel;
    if (speed == "hover") return land.hover;
    if (speed == "float") return land.floatSpeed;
    if (speed == "floatbeach") return land.floatBeach;
    if (speed == "amphibious") return land.amphibious;
    if (speed == "winged") return 100.0;
    // UnitTypeClass::Read_INI (0x7476D3): absent SpeedType defaults to
    // Track for Crusher=yes, otherwise Wheel (e.g. the stock IFV).
    return type.crusher ? land.track : land.wheel;
}

double runtimeMovementCellFloorLevel(const TerrainCell& cell,
    const RuntimeTerrainInfo& info) {
    // CellClass::GetFloorHeight (0x47B3A0), evaluated at the cell centre.
    // The raw ground height is queried even beneath a high bridge.
    const unsigned ramp = info.movementRampType;
    const double centre = (ramp >= 1 && ramp <= 4) || (ramp >= 17 && ramp <= 20)
        ? 0.5 : ramp >= 9 && ramp <= 16 ? 1.0 : 0.0;
    return cell.level + centre;
}

double runtimeTerrainMovementSpeedMultiplier(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeCellPoint& from,
    const RuntimeCellPoint& to) {
    if (!runtimeObjectUsesTerrainMovementSpeed(state, object)) return 1.0;
    const auto* type = findRuntimeUnitType(state.rules, object);
    const auto destination = state.terrainByCell.find(runtimeCellKey(to.x, to.y));
    if (destination == state.terrainByCell.end() || !destination->second) return 1.0;
    const auto& cell = *destination->second;
    const auto info = runtimeCachedTerrainInfoForCell(state, cell);
    auto land = info.movementLandType;
    if (land == RuntimeTerrainLandType::Unknown) land = info.authoredLandType;
    const bool ship = toLowerAsciiLocal(trimAsciiLocal(type->locomotor))
        == "{2bea74e1-7cca-11d3-be14-00104b62a16c}"
        || runtimeObjectMovementMode(state, object) == PathMovementMode::Naval;
    if (info.bridge && !ship) land = RuntimeTerrainLandType::Road;
    else if (cell.hasOverlay && (cell.overlayIsOre || cell.overlayIsGem))
        land = RuntimeTerrainLandType::Tiberium;
    if (land == RuntimeTerrainLandType::Unknown) {
        land = info.water || (info.bridge && ship) ? RuntimeTerrainLandType::Water
            : info.shore ? RuntimeTerrainLandType::Beach : RuntimeTerrainLandType::Clear;
    }
    const char* id = runtimeTerrainLandTypeRulesId(land);
    const auto* authored = id ? state.rules.findLandType(id) : nullptr;
    double multiplier = authored ? runtimeMovementLandPercent(*authored, *type) / 100.0 : 1.0;
    multiplier = std::min(1.0, multiplier);
    const auto source = state.terrainByCell.find(runtimeCellKey(from.x, from.y));
    if (source != state.terrainByCell.end() && source->second && from != to) {
        const double difference = runtimeMovementCellFloorLevel(cell, info)
            - runtimeMovementCellFloorLevel(*source->second,
                runtimeCachedTerrainInfoForCell(state, *source->second));
        const auto& slopes = state.rules.getTerrainMovementRules();
        const auto speed = toLowerAsciiLocal(trimAsciiLocal(type->speedType));
        const bool tracked = speed.empty() ? type->crusher : speed == "track";
        if (difference > 0.0) multiplier *= tracked ? slopes.trackedUphill : slopes.wheeledUphill;
        else if (difference < 0.0) multiplier *= tracked ? slopes.trackedDownhill : slopes.wheeledDownhill;
    }
    // Normally a zero land speed is rejected by navigation. Retail still uses
    // 0.5 for a zero product on a committed/forced step (including a zero slope).
    if (multiplier == 0.0) multiplier = 0.5;
    if (object.health <= runtimeMaxHealthForObject(state, object)
            * state.rules.getSpecialMechanismRules().conditionYellow) multiplier *= 0.75;
    // Drive/Ship store their target percentage before passing it through
    // FootClass::SetSpeedPercentage, which caps the final value at 1.0.
    return std::isfinite(multiplier) ? std::clamp(multiplier, 0.01, 1.0) : 1.0;
}
