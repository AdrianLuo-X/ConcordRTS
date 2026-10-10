// Native weapon inventory and presentation audit. Deliberately independent of
// runtimeWeaponFireFlh for its expected authored offsets. Include beside the
// existing projectile muzzle smoke after the combat/runtime helpers.
struct RuntimeMuzzleAuditSlot {
    std::string name;
    std::string weapon;
    RuntimeWeaponSlot slot = RuntimeWeaponSlot::None;
    int index = -1;
    bool elite = false;
    bool secondary = false;
    bool occupied = false;
};

std::optional<RuntimeMuzzleAuditSlot> runtimeMuzzleAuditSlot(
    const std::string& key, const std::string& value) {
    RuntimeMuzzleAuditSlot entry;
    entry.name = key; entry.weapon = value;
    const std::string name = toLowerAsciiLocal(key);
    if (value.empty() || toLowerAsciiLocal(value) == "none") return std::nullopt;
    entry.elite = name.starts_with("elite");
    const std::string ordinary = entry.elite ? name.substr(5) : name;
    if (ordinary == "primary") {
        entry.slot = entry.elite ? RuntimeWeaponSlot::ElitePrimary : RuntimeWeaponSlot::Primary;
    } else if (ordinary == "secondary") {
        entry.secondary = true;
        entry.slot = entry.elite ? RuntimeWeaponSlot::EliteSecondary : RuntimeWeaponSlot::Secondary;
    } else if (ordinary == "occupyweapon") {
        entry.occupied = true;
        entry.slot = entry.elite ? RuntimeWeaponSlot::EliteOccupy : RuntimeWeaponSlot::Occupy;
    } else if (ordinary.starts_with("weapon") && ordinary.size() > 6) {
        const std::string number = ordinary.substr(6);
        if (!std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        entry.index = std::atoi(number.c_str()) - 1;
        if (entry.index < 0) return std::nullopt;
        entry.secondary = entry.index == 1;
        entry.slot = entry.elite ? RuntimeWeaponSlot::EliteIndexed : RuntimeWeaponSlot::Indexed;
    } else return std::nullopt;
    return entry;
}

std::optional<std::array<int, 3>> runtimeMuzzleAuditReadFlh(
    const IniFile& art, const std::string& image, const RuntimeMuzzleAuditSlot& slot,
    std::string& selectedKey) {
    std::vector<std::string> keys;
    if (slot.index >= 0) {
        if (slot.elite) keys.push_back("EliteWeapon" + std::to_string(slot.index + 1) + "FLH");
        keys.push_back("Weapon" + std::to_string(slot.index + 1) + "FLH");
    }
    const std::string first = slot.secondary ? "Secondary" : "Primary";
    const std::string second = slot.secondary ? "Primary" : "Secondary";
    if (slot.elite) keys.push_back("Elite" + first + "FireFLH");
    keys.push_back(first + "FireFLH");
    // Stock IFV aliases Primary to Weapon1 and Secondary to Weapon2.
    if (slot.elite) keys.push_back("EliteWeapon" + std::string(slot.secondary ? "2" : "1") + "FLH");
    keys.push_back("Weapon" + std::string(slot.secondary ? "2" : "1") + "FLH");
    if (slot.elite) keys.push_back("Elite" + second + "FireFLH");
    keys.push_back(second + "FireFLH");
    for (const auto& key : keys) {
        std::string value = art.getValue(image, key);
        std::replace(value.begin(), value.end(), ',', ' ');
        std::istringstream input(value);
        std::array<int, 3> flh{};
        if (input >> flh[0] >> flh[1] >> flh[2]) {
            selectedKey = key;
            return flh;
        }
    }
    selectedKey = "fallback";
    return std::nullopt;
}

std::string runtimeMuzzleAuditExpectedSequence(const IniFile& art,
    const std::string& image, const RuntimeMuzzleAuditSlot& slot,
    const WorldObjectInstance& object) {
    const std::string sequence = art.getValue(image, "Sequence");
    std::vector<std::string> candidates;
    if (object.airborne) candidates.push_back("FireFly");
    if (object.deployed) candidates = {"DeployedFire", "DeployFire", "Fire", "SecondaryFire", "Deployed", "FireUp"};
    else if (slot.secondary) {
        if (object.prone) candidates.insert(candidates.end(), {"SecondaryProne", "FireProne"});
        candidates.insert(candidates.end(), {"SecondaryFire", "Fire", "FireUp"});
    } else {
        if (object.prone) candidates.push_back("FireProne");
        candidates.insert(candidates.end(), {"Fire", "FireUp", "SecondaryFire"});
    }
    for (const auto& key : candidates)
        if (!art.getValue(sequence, key).empty()) return key;
    return {};
}

bool runRuntimeAllWeaponMuzzleSmoke(RuntimeWindowState& live, std::string& error) {
    IniFile authoredRules, authoredArt;
    if (!authoredRules.loadFromFile(live.report.rulesPath)
        || !authoredArt.loadFromFile(live.report.artPath)) {
        error = "all weapon muzzles: native INI sources unavailable"; return false;
    }
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config; probe.report = live.report;
    probe.rules = live.rules; probe.rulesLoaded = true;
    probe.art = live.art; probe.artLoaded = true;
    probe.theater = live.theater; probe.theaterLoaded = live.theaterLoaded;
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.runtimePreferencePersistenceEnabled = false;
    probe.runtimeSoundsEnabled = false;
    for (int y = 5; y <= 24; ++y) for (int x = 5; x <= 24; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = cell.subTileIndex = 0;
        cell.level = 2;
        probe.world.accessTerrainCells().push_back(cell);
        probe.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
    }
    probe.terrainByCell = buildRuntimeTerrainLookup(probe.world);
    GameWorld renderWorld;
    std::vector<RuntimeProjectileRenderActor> actors;
    std::vector<std::string> labels, sequences, failures;
    const std::string root = std::getenv("CONCORD_ALL_MUZZLES_OUTPUT")
        ? std::getenv("CONCORD_ALL_MUZZLES_OUTPUT") : "";
    std::ofstream inventory;
    if (!root.empty()) {
        inventory.open(root + "/weapons.csv");
        if (!inventory) { error = "all weapon muzzles: cannot open inventory"; return false; }
        inventory << "case,type,slot,weapon,rank,pose,facing,turretFacing,flhKey,sourceX,sourceY,sourceZ,screenX,screenY,sequence,status\n";
    }
    // Fixed native eight-direction oracle, independent of the engine's
    // direction/FLH projection helpers. F is clockwise from screen north.
    constexpr double diagonal = 0.7071067811865475244;
    constexpr std::array<std::array<double, 2>, 8> directions{{
        {-diagonal, -diagonal}, {0.0, -1.0}, {diagonal, -diagonal}, {1.0, 0.0},
        {diagonal, diagonal}, {0.0, 1.0}, {-diagonal, diagonal}, {-1.0, 0.0}}};
    const auto close = [](double a, double b) { return std::abs(a - b) < 0.000001; };
    std::string garrisonHost;
    for (const auto& building : probe.rules.getBuildingTypes()) {
        const auto image = building.image.empty() ? building.id : building.image;
        if (building.canBeOccupied && !authoredArt.getValue(image, "MuzzleFlash0").empty()) {
            garrisonHost = building.id; break;
        }
    }
    int registeredSlots = 0, authoredChecks = 0, garrisonChecks = 0;
    for (const std::string category : {"InfantryTypes", "VehicleTypes", "AircraftTypes", "BuildingTypes"}) {
        const auto* registry = authoredRules.getSection(category);
        if (!registry) continue;
        for (const auto& [registryKey, typeId] : *registry) {
            (void)registryKey;
            const auto* section = authoredRules.getSection(typeId);
            if (!section) continue;
            const std::string image = authoredRules.getValue(typeId, "Image", typeId);
            for (const auto& [key, value] : *section) {
                const auto parsed = runtimeMuzzleAuditSlot(key, value);
                if (!parsed) continue;
                const auto& slot = *parsed;
                ++registeredSlots;
                const auto* weapon = probe.rules.findWeaponType(slot.weapon);
                if (!weapon) { failures.push_back(typeId + "/" + key + ": weapon unavailable"); continue; }
                const bool infantry = category == "InfantryTypes";
                const auto* unit = infantry ? probe.rules.findUnitType("InfantryType", typeId)
                    : probe.rules.findUnitType(category == "AircraftTypes" ? "AircraftType" : "UnitType", typeId);
                std::vector<std::string> poses{"standing"};
                if (slot.occupied) poses = {"garrison"};
                else if (infantry) {
                    if (toLowerAsciiLocal(authoredArt.getValue(image, "Crawls")) == "yes") poses.push_back("prone");
                    if (unit && unit->deployer && slot.secondary) poses.push_back("deployed");
                    if (unit && unit->jumpJet) poses = {"airborne"};
                } else if (category == "AircraftTypes" || (unit && (unit->jumpJet || unit->balloonHover))) poses = {"airborne"};
                for (const auto& poseName : poses) for (int direction = 0; direction < 8; ++direction) {
                    WorldObjectInstance source;
                    source.instanceId = static_cast<int>(actors.size()) + 1;
                    source.type = slot.occupied ? garrisonHost : typeId;
                    source.kind = slot.occupied || category == "BuildingTypes" ? WorldObjectKind::Structure
                        : infantry ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
                    source.authoredAircraft = category == "AircraftTypes";
                    source.owner = "Russians"; source.alive = true; source.health = 1000;
                    source.veterancy = slot.elite ? 2 : 0;
                    source.x = 12; source.y = 12; source.level = 2;
                    source.cell = runtimeCellKey(12, 12);
                    source.hasRenderPosition = true;
                    source.renderX = 12.25; source.renderY = 12.5; source.renderLevel = 2.0;
                    source.facing = direction * 32;
                    source.turretFacing = ((direction + 2) % 8) * 32;
                    source.subCell = -1;
                    source.prone = poseName == "prone";
                    source.deployed = poseName == "deployed";
                    source.airborne = poseName == "airborne";
                    if (source.airborne) source.level = source.flightLevel = 8, source.renderLevel = 8.0;
                    source.mission = source.deployed ? "DeployedIdle" : "Guard";
                    if (slot.occupied) source.mission = "Occupying";
                    if (unit && unit->gunner && slot.index >= 0) {
                        source.activeGunnerWeaponIndex = slot.index;
                        for (std::size_t mode = 0; mode < unit->ifvWeaponIndices.size(); ++mode)
                            if (unit->ifvWeaponIndices[mode] == slot.index) {
                                source.activeGunnerMode = static_cast<int>(mode);
                                source.activeGunnerTurretIndex = unit->ifvTurretIndices[mode]; break;
                            }
                    }
                    WorldObjectInstance target;
                    target.instanceId = 100000; target.kind = WorldObjectKind::Unit; target.type = "HTNK";
                    target.owner = "Americans"; target.alive = true; target.health = 1000;
                    const int firingDirection = unit && unit->turret ? (direction + 2) % 8 : direction;
                    target.x = source.x + static_cast<int>(std::lround(directions[firingDirection][0] * 6));
                    target.y = source.y + static_cast<int>(std::lround(directions[firingDirection][1] * 6));
                    target.cell = runtimeCellKey(target.x, target.y); target.level = 2;
                    RuntimeWeaponProfile profile;
                    finalizeRuntimeWeaponProfile(profile, probe, slot.slot, *weapon, "native muzzle audit");
                    profile.indexedWeaponIndex = slot.index;
                    profile.occupied = slot.occupied;
                    profile.occupantMuzzleIndex = slot.occupied ? direction : -1;
                    profile.deployed = source.deployed;
                    probe.projectiles.clear(); probe.impactEffects.clear();
                    probe.world.accessObjects() = {source, target};
                    fireRuntimeCombatRound(probe, source, target, *weapon, profile, 1.0, -1, false);
                    source = probe.world.getObjects().front();
                    const std::string label = typeId + "/" + key + "/" + poseName + "/" + std::to_string(direction * 32);
                    if (probe.projectiles.empty()) { failures.push_back(label + ": projectile missing"); continue; }
                    const auto& shot = probe.projectiles.front();
                    std::string flhKey;
                    const auto flh = runtimeMuzzleAuditReadFlh(authoredArt, image, slot, flhKey);
                    bool coordinates = true;
                    if (slot.occupied) {
                        const std::string hostImage = authoredRules.getValue(garrisonHost, "Image", garrisonHost);
                        auto raw = authoredArt.getValue(hostImage, "MuzzleFlash" + std::to_string(direction));
                        std::replace(raw.begin(), raw.end(), ',', ' ');
                        int x = 0, y = 0; std::istringstream offsets(raw); offsets >> x >> y;
                        const double expectedX = source.renderX + std::trunc((x / 60.0 + y / 30.0) * 256.0) / 256.0;
                        const double expectedY = source.renderY + std::trunc((y / 30.0 - x / 60.0) * 256.0) / 256.0;
                        coordinates = close(shot.sourceX, expectedX) && close(shot.sourceY, expectedY);
                        flhKey = "MuzzleFlash" + std::to_string(direction); ++garrisonChecks;
                    } else if (flh) {
                        const bool turret = source.kind == WorldObjectKind::Structure
                            ? toLowerAsciiLocal(authoredRules.getValue(typeId, "Turret")) == "yes"
                            : unit && unit->turret;
                        const int flhDirection = source.kind == WorldObjectKind::Structure && !turret
                            ? 0 : turret ? (direction + 2) % 8 : direction;
                        const auto forward = directions[flhDirection];
                        const int turretOffset = turret ? std::atoi(authoredArt.getValue(image, "TurretOffset", "0").c_str()) : 0;
                        const double expectedX = source.renderX + (forward[0] * (*flh)[0]
                            - forward[1] * (*flh)[1] + directions[direction][0] * turretOffset) / 256.0;
                        const double expectedY = source.renderY + (forward[1] * (*flh)[0]
                            + forward[0] * (*flh)[1] + directions[direction][1] * turretOffset) / 256.0;
                        coordinates = close(shot.sourceX, expectedX) && close(shot.sourceY, expectedY);
                        // Infantry has a separate native SHP pose oracle below.
                        if (!infantry) coordinates = coordinates && close(shot.sourceZ,
                            source.renderLevel + (*flh)[2] * 2.449489742783178 / 256.0);
                        ++authoredChecks;
                    }
                    if (!coordinates) failures.push_back(label + ": authored FLH projection differs");
                    for (const auto& flash : probe.impactEffects)
                        if (flash.lifecycleState == "Muzzle" && (!close(flash.worldX, shot.sourceX)
                            || !close(flash.worldY, shot.sourceY) || !close(flash.worldLevel, shot.sourceZ)
                            || flash.screenOffsetX != shot.sourceScreenOffsetX || flash.screenOffsetY != shot.sourceScreenOffsetY
                            || flash.nativeFixedX != shot.sourceNativeFixedX || flash.nativeFixedY != shot.sourceNativeFixedY))
                            failures.push_back(label + ": flash separated from projectile");
                    auto actor = makeRuntimeProjectileRenderActor(shot); actor.trailerCadenceManaged = true;
                    const std::string sequence = infantry && !slot.occupied
                        ? runtimeMuzzleAuditExpectedSequence(authoredArt, image, slot, source) : "";
                    if (inventory) inventory << actors.size() << ',' << typeId << ',' << key << ',' << weapon->id
                        << ',' << source.veterancy << ',' << poseName << ',' << source.facing << ',' << source.turretFacing
                        << ',' << flhKey << ',' << shot.sourceX << ',' << shot.sourceY << ',' << shot.sourceZ
                        << ',' << shot.sourceScreenOffsetX << ',' << shot.sourceScreenOffsetY << ',' << sequence
                        << ',' << (coordinates ? "coordinates-pass" : "coordinates-fail") << '\n';
                    renderWorld.accessObjects().push_back(source);
                    actors.push_back(actor); labels.push_back(label); sequences.push_back(sequence);
                }
            }
        }
    }
#include "simulation/combat/projectiles/diagnostics/RuntimeAllWeaponMuzzleExtra.inl"
#include "simulation/combat/projectiles/diagnostics/RuntimeAllWeaponMuzzleDeterminism.inl"
    RuntimeMapRenderContext context;
    context.theater = probe.theaterLoaded ? &probe.theater : nullptr;
    context.art = &probe.art; context.rules = &probe.rules;
    context.assetRootPath = runtimeAssetRootPath(probe.config);
    context.tileRootPath = runtimeTileRootPath(probe.config, probe.report.map);
    context.palettePath = runtimePalettePath(probe.config, probe.report.map);
    context.structureRootPath = runtimeStructureRootPath(probe.config, probe.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(probe.config, probe.report.map);
    context.fixedTickHz = probe.config.fixedTickHz;
    std::string renderError;
    if (!runRuntimeAllWeaponMuzzleRenderContract(context, renderWorld, actors, labels, sequences, root, renderError))
        failures.push_back(renderError);
    if (registeredSlots < 286) failures.push_back("native inventory under-covered: " + std::to_string(registeredSlots));
    if (!root.empty()) {
        std::ofstream report(root + "/failures.txt");
        for (const auto& failure : failures) report << failure << '\n';
    }
    std::cout << "All weapon muzzles: slots=" << registeredSlots << " poses=" << actors.size()
        << " authoredFLH=" << authoredChecks << " garrison=" << garrisonChecks
        << " IFVmodes=" << ifvModeChecks << " ports=" << portChecks << " allWindows=" << allWindowChecks
        << " gattlingRounds=" << gattlingRoundChecks << " failures=" << failures.size() << '\n';
    if (!failures.empty()) { error = "all weapon muzzles: " + failures.front(); return false; }
    return true;
}
