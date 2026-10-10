int superWeaponObjectCellKey(int x, int y) {
    // World objects and runtime saves use y*1000+x; IsoMapPack terrain
    // indices use 512 and must not be substituted here.
    return x < 0 || y < 0 ? -1 : y * 1000 + x;
}

// YR CellSpread::GetDistance: long axis plus half the short axis,
// rounded down. Protection/chrono 3 by 3 rectangles are separate.
int superWeaponCellSpreadDistance(int dx, int dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    return std::max(dx, dy) + std::min(dx, dy) / 2;
}

const UnitType* superWeaponUnitType(const WorldObjectInstance& object,
    const RulesDatabase* rules) {
    if (!rules || object.kind == WorldObjectKind::Structure) return nullptr;
    const auto* unit = rules->findUnitType(object.kind == WorldObjectKind::Infantry
        ? "InfantryType" : "UnitType", object.type);
    return unit ? unit : rules->findUnitType("AircraftType", object.type);
}

bool superWeaponExposed(const WorldObjectInstance& object,
    const SuperWeaponWorldContext& context) {
    if (object.garrisonHostObjectId >= 0) return false;
    if (!context.objects) return true;
    return std::none_of(context.objects->begin(), context.objects->end(), [&](const auto& host) {
        return host.alive && std::find(host.transportPassengerIds.begin(),
            host.transportPassengerIds.end(), object.instanceId) != host.transportPassengerIds.end();
    });
}

double superWeaponVerses(const WorldObjectInstance& object,
    const SuperWeaponWorldContext& context, const WarheadType* warhead, bool bypassShield = false) {
    if (!warhead || !context.rules || warhead->verses.empty()) return 1.0;
    std::string armor;
    if (const auto* unit = superWeaponUnitType(object, context.rules)) armor = unit->armor;
    else if (const auto* building = context.rules->findBuildingType(object.type)) armor = building->armor;
    if (const auto* shield = runtimeActiveShield(object, *context.rules, warhead); shield && !bypassShield && !shield->inheritArmor)
        armor = shield->armor;
    std::transform(armor.begin(), armor.end(), armor.begin(), [](unsigned char c) { return std::tolower(c); });
    const std::array<std::string, 11> armors{"none", "flak", "plate", "light", "medium",
        "heavy", "wood", "steel", "concrete", "special_1", "special_2"};
    const auto found = std::find(armors.begin(), armors.end(), armor);
    const auto values = splitValues(warhead->verses);
    const auto index = std::distance(armors.begin(), found);
    if (found == armors.end() || index >= static_cast<int>(values.size())) return 1.0;
    try {
        return std::max(0.0, std::stod(values[index]) / (values[index].find('%') != std::string::npos ? 100.0 : 1.0));
    } catch (...) { return 1.0; }
}
