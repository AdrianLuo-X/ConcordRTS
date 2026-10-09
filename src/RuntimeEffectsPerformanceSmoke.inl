// Included next to the weapon/projectile integration smoke entry point.
bool runRuntimeWarheadVersesCacheContractCheck(std::string& error) {
    const std::array<std::pair<std::string, int>, 15> armors{{
        {"none", 0}, {"flak", 1}, {"plate", 2}, {"light", 3},
        {"medium", 4}, {"heavy", 5}, {"wood", 6}, {"steel", 7},
        {"concrete", 8}, {"special_1", 9}, {"special_2", 10},
        {"NONE", 0}, {"Heavy", 5}, {"unknown", -1}, {" heavy ", -1}
    }};
    WarheadType warhead;
    warhead.id = "same-live-warhead";
    const auto matchesOriginal = [&]() {
        const std::vector<double> parsed = parseRuntimeVerses(warhead.verses);
        for (const auto& [armor, index] : armors) {
            const double expected = index < 0
                    || static_cast<std::size_t>(index) >= parsed.size()
                ? 1.0 : parsed[static_cast<std::size_t>(index)];
            const double actual = runtimeWarheadArmorMultiplier(&warhead, armor);
            if (std::bit_cast<std::uint64_t>(expected)
                != std::bit_cast<std::uint64_t>(actual)) {
                return false;
            }
        }
        return true;
    };
    const std::vector<std::string> authoredCases{
        "",
        "100%,100%,100%,75%,50%,25%,100%,100%,50%,100%,100%",
        " 25% ,invalid,, -50%,0%,23.5%,17suffix,1e309",
        "NaN,Inf,-Inf,0%,-0%,1%,100%,200%,100%,100%,100%",
        "1%,2%,3%"
    };
    for (int reload = 0; reload < 3; ++reload) {
        for (const auto& text : authoredCases) {
            // Reuse the same address/id while changing the authored text.
            warhead.verses = text;
            if (!matchesOriginal()) {
                error = "cached Verses changed armor, invalid-token or live-rule semantics";
                return false;
            }
        }
    }
    for (int index = 0; index < 192; ++index) {
        warhead.verses = std::to_string(index) + "%,0%,20%,invalid";
        if (!matchesOriginal()) {
            error = "bounded Verses cache eviction changed parsed multipliers";
            return false;
        }
    }
    for (const auto& text : authoredCases) {
        warhead.verses = text;
        if (!matchesOriginal()) {
            error = "Verses reload after cache eviction changed parsed multipliers";
            return false;
        }
    }
    if (runtimeWarheadArmorMultiplier(nullptr, "heavy") != 1.0) {
        error = "null warhead no longer uses the default armor multiplier";
        return false;
    }
    std::cout << "Verses cache: armorProfiles=15 invalidTokens=yes "
              << "liveRuleReload=yes boundedEviction=yes exactValues=yes\n";
    return true;
}

bool runRuntimeCrushCandidateIndexContractCheck(std::string& error) {
    auto storage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& state = *storage;
    IniFile rulesIni;
    if (!rulesIni.loadFromString(R"CRUSHINDEX(
[VehicleTypes]
0=CRUSHER
1=SOFT
[AircraftTypes]
0=AIR
[BuildingTypes]
0=WALL
1=BUILDING
2=HUGE
[CRUSHER]
Crusher=yes
SpeedType=Track
[SOFT]
Crushable=yes
[AIR]
SpeedType=Winged
MovementZone=Fly
[WALL]
Wall=yes
Crushable=yes
[BUILDING]
Crushable=yes
[HUGE]
Wall=yes
Crushable=yes
)CRUSHINDEX") || !state.rules.loadFromIni(rulesIni)) {
        error = "crush candidate rules fixture did not load";
        return false;
    }
    state.rulesLoaded = true;
    state.structureFootprints["WALL"] = {3, 2, true};
    state.structureFootprints["BUILDING"] = {-2, 5, true};
    state.structureFootprints["SOFT"] = {8, 8, true};
    state.structureFootprints["HUGE"] = {
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), true
    };
    for (int index = 0; index < 80; ++index) {
        WorldObjectInstance object;
        object.instanceId = 500 - index;
        object.kind = index % 4 == 0
            ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        object.type = index % 8 == 0 ? "WALL"
            : index % 4 == 0 ? "BUILDING"
            : index % 7 == 0 ? "AIR" : "SOFT";
        object.owner = index % 5 == 0 ? "Blue" : "Red";
        object.x = index * 7 % 17 - 5;
        object.y = index * 13 % 11 - 4;
        object.alive = index % 9 != 0;
        object.airborne = index % 13 == 0;
        object.submerged = index % 17 == 0;
        object.onBridge = index % 19 == 0;
        state.world.accessObjects().push_back(object);
    }
    for (const RuntimeCellPoint cell : std::array<RuntimeCellPoint, 4>{{
        {1000, 0}, {0, 1}, {-1, 0}, {0, -1}
    }}) {
        WorldObjectInstance object = state.world.getObjects()[1];
        object.instanceId = 700 + static_cast<int>(state.world.getObjects().size());
        object.x = cell.x;
        object.y = cell.y;
        state.world.accessObjects().push_back(object);
    }
    WorldObjectInstance huge = state.world.getObjects().front();
    huge.instanceId = 900;
    huge.type = "HUGE";
    huge.x = -4;
    huge.y = -4;
    state.world.accessObjects().push_back(huge);
    std::vector<PathTerrainCell> snapshot;
    for (int y = -5; y <= 12; ++y) {
        for (int x = -6; x <= 15; ++x) {
            PathTerrainCell cell;
            cell.cell = {x, y};
            snapshot.push_back(cell);
        }
    }
    for (const RuntimeCellPoint point : std::array<RuntimeCellPoint, 3>{{
        {1000, 0}, {std::numeric_limits<int>::max(), 0},
        {std::numeric_limits<int>::min(), -1}
    }}) {
        PathTerrainCell cell;
        cell.cell = {point.x, point.y};
        snapshot.push_back(cell);
    }
    const RuntimeCrushCandidateLookup lookup =
        buildRuntimeCrushCandidateLookup(state, snapshot);
    const RuntimeCrushCandidateList empty;
    WorldObjectInstance crusher = state.world.getObjects()[1];
    crusher.instanceId = 1000;
    crusher.type = "CRUSHER";
    crusher.owner = "Blue";
    for (const PathTerrainCell& terrain : snapshot) {
        const RuntimeCellPoint cell{terrain.cell.x, terrain.cell.y};
        const auto found = lookup.find(runtimeCrushCandidateCellKey(cell.x, cell.y));
        const RuntimeCrushCandidateList& candidates =
            found == lookup.end() ? empty : found->second;
        RuntimeCrushCandidateList expected;
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (runtimeObjectOccupiesCrushCell(state, object, cell)) {
                expected.push_back(&object);
            }
        }
        if (candidates != expected) {
            error = "crush candidate index changed full footprints, coordinates or world order";
            return false;
        }
        for (int variant = 0; variant < 4; ++variant) {
            const std::unordered_set<int> ignored = variant & 1
                ? std::unordered_set<int>{499, 496, 492, 488}
                : std::unordered_set<int>{};
            const int allowedId = variant & 2 ? 495 : -1;
            const RuntimeCrushTargetScan full = runtimeScanCrushTargetsAtCell(
                state, crusher, cell, allowedId, &ignored
            );
            const RuntimeCrushTargetScan indexed = runtimeScanCrushTargetsAtCell(
                state, crusher, cell, allowedId, &ignored, &candidates
            );
            if (full.blocked != indexed.blocked || full.targetIds != indexed.targetIds
                || runtimeCanEnterAndCrushCell(state, crusher, cell, &ignored)
                    != runtimeCanEnterAndCrushCell(state, crusher, cell, &ignored, &candidates)) {
                error = "indexed crush scan changed blockers, victims or enterability";
                return false;
            }
        }
    }
    std::cout << "Crush candidates: cells=" << snapshot.size()
              << " objects=" << state.world.getObjects().size()
              << " exactCoordinates=yes worldOrder=yes indexedScan=equivalent\n";
    return true;
}

bool runRuntimeEffectBufferContractCheck(std::string& error) {
    if (!runRuntimeWarheadVersesCacheContractCheck(error)) {
        return false;
    }
    if (!runRuntimeCrushCandidateIndexContractCheck(error)) {
        return false;
    }
    auto storage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& state = *storage;
    state.runtimeSuppressPlayerAudioFeedback = true;
    constexpr int projectileCount = 1024;
    constexpr int impactCount = 4096;
    const std::string payload = "effect-buffer-regression-long-authored-identity";
    for (int id = 0; id < projectileCount; ++id) {
        RuntimeProjectileEffect projectile;
        projectile.actorId = id;
        projectile.fromCell = {0, 0};
        projectile.toCell = {10, 10};
        projectile.totalTicks = projectile.remainingTicks = 1 + id % 5;
        // Visual support links exercise the real expiration/replay path
        // without generating additional damage in this buffer-only fixture.
        projectile.renderIntent = "prismsupportbeam";
        projectile.weaponId = payload;
        state.projectiles.push_back(std::move(projectile));
    }
    for (int id = 0; id < impactCount; ++id) {
        RuntimeImpactEffect impact;
        impact.projectileActorId = id;
        impact.totalTicks = impact.remainingTicks = 1 + id % 5;
        impact.animId = payload;
        state.impactEffects.push_back(std::move(impact));
    }
    const std::size_t impactCapacity = state.impactEffects.capacity();
    for (int tick = 1; tick <= 6; ++tick) {
        state.tick = tick;
        (void)advanceRuntimeEffects(state);
        std::size_t position = 0;
        for (int id = 0; id < projectileCount; ++id) {
            if (1 + id % 5 <= tick) continue;
            if (position >= state.projectiles.size()) {
                error = "surviving projectile missing";
                return false;
            }
            const auto& projectile = state.projectiles[position++];
            if (projectile.actorId != id || projectile.weaponId != payload
                || projectile.remainingTicks != 1 + id % 5 - tick) {
                error = "projectile order, identity or age changed";
                return false;
            }
        }
        if (position != state.projectiles.size()
            || !state.projectileTickScratch.empty()) {
            error = "projectile lifetime or scratch drain mismatch";
            return false;
        }
        position = 0;
        for (int id = 0; id < impactCount; ++id) {
            if (1 + id % 5 <= tick) continue;
            if (position >= state.impactEffects.size()) {
                error = "surviving impact missing";
                return false;
            }
            const auto& impact = state.impactEffects[position++];
            if (impact.projectileActorId != id || impact.animId != payload
                || impact.remainingTicks != 1 + id % 5 - tick) {
                error = "impact order, identity or age changed";
                return false;
            }
        }
        if (position != state.impactEffects.size()
            || state.impactEffects.capacity() != impactCapacity) {
            error = "impact lifetime or retained capacity mismatch";
            return false;
        }
    }
    if (state.recentExpiredProjectiles.size() != 32) {
        error = "expired projectile history lost its bounded retention";
        return false;
    }

    WorldObjectInstance building;
    building.type = "FootprintProbe";
    building.kind = WorldObjectKind::Structure;
    building.x = 3;
    building.y = 4;
    for (int width : {-2, 1, 2, 4, 8}) {
        for (int height : {-2, 1, 2, 4, 8}) {
            state.structureFootprints[building.type] = {width, height, true};
            const auto cells = runtimeStructureFootprintCellsForObject(state, building);
            for (int x = -5; x <= 14; ++x) {
                for (int y = -5; y <= 14; ++y) {
                    const RuntimeCellPoint point{x, y};
                    double expected = std::numeric_limits<double>::max();
                    for (const auto& cell : cells) {
                        expected = std::min(expected, runtimeCellDistance(point, cell));
                    }
                    if (runtimeCellDistanceToObjectFootprint(state, point, building)
                        != expected) {
                        error = "nearest foundation cell differs from exhaustive search";
                        return false;
                    }
                    const bool occupied = std::find(cells.begin(), cells.end(), point)
                        != cells.end();
                    if (runtimeObjectOccupiesCrushCell(state, building, point) != occupied) {
                        error = "crush foundation membership differs from exhaustive search";
                        return false;
                    }
                }
            }
        }
    }
    WorldObjectInstance mobile = building;
    mobile.kind = WorldObjectKind::Unit;
    for (int x = 0; x <= 12; ++x) {
        for (int y = 0; y <= 12; ++y) {
            if (runtimeObjectOccupiesCrushCell(state, mobile, {x, y})
                != (x == mobile.x && y == mobile.y)) {
                error = "mobile crush occupancy inherited a structure footprint";
                return false;
            }
        }
    }
    // The wrapper answers enterability; a non-crusher must still report
    // blocked from the detailed scan when another object occupies the cell.
    state.world.accessObjects().push_back(mobile);
    state.world.accessObjects().back().instanceId = 8001;
    WorldObjectInstance nonCrusher = mobile;
    nonCrusher.instanceId = 8002;
    const RuntimeCellPoint occupiedCell{mobile.x, mobile.y};
    const RuntimeCrushTargetScan detailed = runtimeScanCrushTargetsAtCell(
        state, nonCrusher, occupiedCell
    );
    if (!detailed.blocked || !detailed.targetIds.empty()
        || runtimeCanEnterAndCrushCell(state, nonCrusher, occupiedCell)
        || runtimeCanEnterAndCrushCell(state, nonCrusher, {30, 30})) {
        error = "non-crusher fast path changed enterability or detailed blocking";
        return false;
    }
    std::cout << "Effect buffers: projectiles=1024 impacts=4096 stableOrder=yes "
              << "expiry=yes capacityReused=yes foundationDistance=exact "
              << "crushMembership=exact nonCrusherBlocking=preserved\n";
    return true;
}
