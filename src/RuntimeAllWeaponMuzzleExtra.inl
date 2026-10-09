// Additional reachable slots that do not correspond one-to-one with explicit
// Rules keys: all IFV modes, all garrison windows and all five passenger ports.
int ifvModeChecks = 0, portChecks = 0, allWindowChecks = 0, gattlingRoundChecks = 0;
const auto fixture = [&](const std::string& type, WorldObjectKind kind, int direction, int rank) {
    WorldObjectInstance object;
    object.instanceId = 200000 + static_cast<int>(actors.size()); object.type = type;
    object.kind = kind; object.owner = "Russians"; object.alive = true; object.health = 1000;
    object.x = object.y = 12; object.cell = runtimeCellKey(12, 12); object.level = 2;
    object.facing = object.turretFacing = direction * 32; object.veterancy = rank;
    object.subCell = -1; object.mission = "Guard";
    return object;
};
const auto targetFor = [&](const WorldObjectInstance& source) {
    auto target = fixture("HTNK", WorldObjectKind::Unit, 0, 0);
    target.instanceId = 900001; target.owner = "Americans";
    const auto direction = directions[(source.facing / 32) & 7];
    target.x += static_cast<int>(std::lround(direction[0] * 6));
    target.y += static_cast<int>(std::lround(direction[1] * 6));
    target.cell = runtimeCellKey(target.x, target.y);
    return target;
};
const auto appendShot = [&](const WorldObjectInstance& visible, const RuntimeProjectileEffect& shot,
        const std::string& label) {
    auto actor = makeRuntimeProjectileRenderActor(shot); actor.trailerCadenceManaged = true;
    renderWorld.accessObjects().push_back(visible); actors.push_back(std::move(actor));
    labels.push_back(label); sequences.emplace_back();
    if (inventory) inventory << actors.size() - 1 << ',' << visible.type << ',' << label << ',' << shot.weaponId
        << ',' << visible.veterancy << ",extra," << visible.facing << ',' << visible.turretFacing
        << ",raw-native," << shot.sourceX << ',' << shot.sourceY << ',' << shot.sourceZ
        << ',' << shot.sourceScreenOffsetX << ',' << shot.sourceScreenOffsetY << ",,coordinates-pass\n";
};
if (const auto* ifv = probe.rules.findUnitType("UnitType", "FV")) {
    for (int mode = 0; mode < 17; ++mode) for (int rank : {0, 2}) for (int direction = 0; direction < 8; ++direction) {
        auto source = fixture("FV", WorldObjectKind::Unit, direction, rank);
        source.activeGunnerMode = mode;
        source.activeGunnerWeaponIndex = ifv->ifvWeaponIndices[mode];
        source.activeGunnerTurretIndex = ifv->ifvTurretIndices[mode];
        auto target = targetFor(source);
        const int index = source.activeGunnerWeaponIndex;
        const std::string ordinary = authoredRules.getValue("FV", "Weapon" + std::to_string(index + 1));
        const std::string expected = rank == 2
            ? authoredRules.getValue("FV", "EliteWeapon" + std::to_string(index + 1), ordinary) : ordinary;
        const auto* authoredWeapon = probe.rules.findWeaponType(expected);
        if (authoredWeapon && authoredWeapon->areaFire) target = source;
        const auto profile = runtimeWeaponProfileForObject(probe, source, &target);
        const auto label = "FV/mode" + std::to_string(mode) + "/rank" + std::to_string(rank) + "/" + std::to_string(source.facing);
        if (!profile.weapon || toLowerAsciiLocal(profile.weapon->id) != toLowerAsciiLocal(expected)) {
            failures.push_back(label + ": selected IFV mode weapon differs from raw Rules"); continue;
        }
        probe.world.accessObjects() = {source, target}; probe.projectiles.clear(); probe.impactEffects.clear();
        fireRuntimeCombatRound(probe, source, target, *profile.weapon, profile, 1.0, -1, false);
        if (probe.projectiles.empty()) { failures.push_back(label + ": no IFV discharge"); continue; }
        appendShot(source, probe.projectiles.front(), label); ++ifvModeChecks;
    }
}
for (const auto& building : probe.rules.getBuildingTypes()) {
    if (!building.canBeOccupied) continue;
    const std::string image = authoredRules.getValue(building.id, "Image", building.id);
    for (int window = 0; window < 10; ++window) {
        std::string raw = authoredArt.getValue(image, "MuzzleFlash" + std::to_string(window));
        if (raw.empty()) continue;
        std::replace(raw.begin(), raw.end(), ',', ' ');
        int dx = 0, dy = 0; std::istringstream input(raw);
        if (!(input >> dx >> dy)) { failures.push_back(building.id + ": malformed native window"); continue; }
        for (int rank : {0, 2}) {
            auto source = fixture(building.id, WorldObjectKind::Structure, 0, rank);
            source.mission = "Occupying";
            auto target = targetFor(source);
            const auto* weapon = probe.rules.findWeaponType(authoredRules.getValue("E1", rank == 2 ? "EliteOccupyWeapon" : "OccupyWeapon"));
            if (!weapon) { failures.push_back("E1: garrison weapon missing"); continue; }
            RuntimeWeaponProfile profile;
            finalizeRuntimeWeaponProfile(profile, probe, rank == 2 ? RuntimeWeaponSlot::EliteOccupy : RuntimeWeaponSlot::Occupy,
                *weapon, "all native windows");
            profile.occupied = true; profile.occupantMuzzleIndex = window;
            probe.world.accessObjects() = {source, target}; probe.projectiles.clear(); probe.impactEffects.clear();
            fireRuntimeCombatRound(probe, source, target, *weapon, profile, 1.0, -1, false);
            const auto label = building.id + "/MuzzleFlash" + std::to_string(window) + "/rank" + std::to_string(rank);
            if (probe.projectiles.empty()) { failures.push_back(label + ": no occupied discharge"); continue; }
            const auto& shot = probe.projectiles.front();
            if (!close(shot.sourceX, source.x + std::trunc((dx / 60.0 + dy / 30.0) * 256.0) / 256.0)
                || !close(shot.sourceY, source.y + std::trunc((dy / 30.0 - dx / 60.0) * 256.0) / 256.0))
                failures.push_back(label + ": window source differs from native pixel offset");
            RuntimeMapRenderContext native;
            native.art=&probe.art;native.rules=&probe.rules;
            native.theater=probe.theaterLoaded?&probe.theater:nullptr;
            native.assetRootPath=runtimeAssetRootPath(probe.config);
            native.structureRootPath=runtimeStructureRootPath(probe.config,probe.report.map);
            native.animationTick=probe.tick;native.fixedTickHz=probe.config.fixedTickHz;
            const RECT viewport{0,0,1024,768};
            for (int zoom:{48,60,72}) {
                RuntimeMapCamera camera;camera.tileWidth=zoom;camera.tileHeight=zoom/2;
                const auto placement=runtimeObjectWorldRenderPlacement(source,probe.world,viewport,native,camera,probe.renderCache);
                const POINT expected{placement.center.x+dx,placement.center.y+dy};
                const auto actual=runtimeProjectileActorScreenPoint(makeRuntimeProjectileRenderActor(shot),viewport,camera);
                if(std::abs(actual.x-expected.x)>1 || std::abs(actual.y-expected.y)>1)
                    failures.push_back(label+": native window zoom="+std::to_string(zoom)
                        +" delta="+std::to_string(actual.x-expected.x)+","+std::to_string(actual.y-expected.y));
            }
            appendShot(source, shot, label); ++allWindowChecks;
        }
    }
}
constexpr std::array<std::array<int, 3>, 5> expectedPorts{{
    {45,190,90}, {45,-190,90}, {-120,200,80}, {-120,-200,80}, {220,0,130}}};
for (const std::string passengerType : {"E1", "GGI", "SHK", "CLEG", "BORIS"})
    for (int rank : {0, 2}) for (int seat = 0; seat < 5; ++seat) for (int direction = 0; direction < 8; ++direction) {
        auto host = fixture("BFRT", WorldObjectKind::Unit, direction, rank);
        auto passenger = fixture(passengerType, WorldObjectKind::Infantry, direction, rank);
        passenger.instanceId = 800000; passenger.alive = false; passenger.mission = "InsideTransport";
        host.transportPassengerIds = {810000, 810001, 810002, 810003, 810004};
        host.transportPassengerIds[seat] = passenger.instanceId;
        auto target = targetFor(host);
        probe.world.accessObjects() = {host, passenger, target};
        const auto profile = runtimeCombatPassengerWeaponProfile(probe, passenger, &target);
        const auto label = "BFRT/" + passengerType + "/port" + std::to_string(seat)
            + "/rank" + std::to_string(rank) + "/" + std::to_string(host.facing);
        if (!profile.weapon) { failures.push_back(label + ": passenger weapon unavailable"); continue; }
        const auto pose = runtimeCombatFiringPose(passenger, &host);
        probe.projectiles.clear(); probe.impactEffects.clear();
        fireRuntimeCombatRound(probe, pose, target, *profile.weapon, profile, 1.0, host.instanceId, false);
        if (probe.projectiles.empty()) { failures.push_back(label + ": passenger did not fire"); continue; }
        const auto& shot = probe.projectiles.front(); const auto directionVector = directions[direction];
        const auto& port = expectedPorts[seat];
        if (!close(shot.sourceX, host.x + (directionVector[0] * port[0] - directionVector[1] * port[1]) / 256.0)
            || !close(shot.sourceY, host.y + (directionVector[1] * port[0] + directionVector[0] * port[1]) / 256.0)
            || !close(shot.sourceZ, host.level + port[2] * 2.449489742783178 / 256.0))
            failures.push_back(label + ": passenger fired from its infantry pose instead of the native host port");
        appendShot(host, shot, label); ++portChecks;
    }
// A delayed gattling round preserves the purchased slot and uses the other
// barrel. Both ranks and every native stage are sampled after the burst cursor.
for (const std::string type : {"YTNK", "YAGGUN"}) for (int rank : {0, 2})
    for (int weaponIndex = 0; weaponIndex < 6; ++weaponIndex) for (int direction = 0; direction < 8; ++direction) {
        const auto key = std::string(rank == 2 ? "EliteWeapon" : "Weapon") + std::to_string(weaponIndex + 1);
        const auto* weapon = probe.rules.findWeaponType(authoredRules.getValue(type, key));
        if (!weapon) continue;
        auto source = fixture(type, type == "YAGGUN" ? WorldObjectKind::Structure : WorldObjectKind::Unit, direction, rank);
        auto target = targetFor(source);
        RuntimeWeaponProfile profile;
        finalizeRuntimeWeaponProfile(profile, probe, rank == 2 ? RuntimeWeaponSlot::EliteIndexed : RuntimeWeaponSlot::Indexed,
            *weapon, "second gattling tube");
        profile.indexedWeaponIndex = weaponIndex;
        auto& burst = probe.combatDetails.bursts[source.instanceId];
        burst.weaponId = weapon->id; burst.nextShotIndex = 1;
        probe.world.accessObjects() = {source, target}; probe.projectiles.clear(); probe.impactEffects.clear();
        fireRuntimeCombatRound(probe, source, target, *weapon, profile, 1.0, -1, false);
        if (!probe.projectiles.empty()) {
            appendShot(source, probe.projectiles.front(), type + "/second-round/" + key + "/" + std::to_string(source.facing));
            ++gattlingRoundChecks;
        } else failures.push_back(type + ": no delayed gattling discharge");
        probe.combatDetails.bursts.clear();
    }
if (ifvModeChecks != 17 * 2 * 8 || portChecks != 5 * 2 * 5 * 8 || allWindowChecks < 20 || gattlingRoundChecks < 48)
    failures.push_back("reachable slot coverage incomplete: IFV=" + std::to_string(ifvModeChecks)
        + " ports=" + std::to_string(portChecks) + " windows=" + std::to_string(allWindowChecks)
        + " gattling=" + std::to_string(gattlingRoundChecks));
