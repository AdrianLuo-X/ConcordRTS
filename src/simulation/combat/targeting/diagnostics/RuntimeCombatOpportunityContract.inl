// Uses the real weapon/range/psychic helpers with the smoke's authored rules.
bool runRuntimeCombatOpportunityContract(const RulesDatabase& rules, std::string& error) {
    std::string checkDiagnostic;
    const auto fail = [&](const std::string& why) {
        error = "combat opportunity equivalence: " + why;
        if (!checkDiagnostic.empty()) error += " (" + checkDiagnostic + ")";
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& state = *storage;
    RuntimeSnapshotActiveStateScope scope(state);
    state.rules = rules;
    state.rulesLoaded = true;
    state.config.fixedTickHz = 15;
    state.economy.owner = "Player";
    state.structureFootprints["TARGET"] = {1, 1, true};
    state.tick = 10;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(cell);
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, const char* type, const char* owner,
                         WorldObjectKind kind, int x, int y) {
        WorldObjectInstance object;
        object.instanceId = id; object.type = type; object.owner = owner;
        object.kind = kind; object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.alive = true; object.health = 1000; object.mission = "Guard";
        state.world.accessObjects().push_back(std::move(object));
    };
    add(1, "AEGIS", "Player", WorldObjectKind::Unit, 4, 6);
    for (int id = 100; id < 228; ++id)
        add(id, "TARGET", "Player", WorldObjectKind::Structure, 5 + id % 8, 4 + id % 3);
    int cases = 0;
    RuntimeCombatOpportunityOwnerSummary ownerSummary;
    const auto check = [&](const WorldObjectInstance& pose, const WeaponType* weapon,
                           double bonus, bool distributed, int preferred, bool force,
                           bool passenger, int expectedId = -2) {
        const auto worldHash = state.world.deterministicSimulationStateHash();
        const auto psychicHash = state.psychicStatuses.stateHash();
        const auto combatHash = state.combatDetails.deterministicHash();
        const auto* scalar = selectRuntimeCombatOpportunityTarget(state, pose, weapon,
            bonus, distributed, preferred, force, passenger, true);
        const auto* optimized = selectRuntimeCombatOpportunityTarget(state, pose, weapon,
            bonus, distributed, preferred, force, passenger, false, &ownerSummary);
        const int scalarId = scalar ? scalar->instanceId : -1;
        const int optimizedId = optimized ? optimized->instanceId : -1;
        ++cases;
        const bool worldUnchanged = worldHash == state.world.deterministicSimulationStateHash();
        const bool psychicUnchanged = psychicHash == state.psychicStatuses.stateHash();
        const bool combatUnchanged = combatHash == state.combatDetails.deterministicHash();
        const bool passed = scalarId == optimizedId
            && (expectedId == -2 || scalarId == expectedId)
            && worldUnchanged && psychicUnchanged && combatUnchanged;
        checkDiagnostic.clear();
        if (!passed) {
            const auto* status = state.psychicStatuses.findByTarget(pose.instanceId);
            std::ostringstream details;
            details << "case=" << cases << " tick=" << state.tick
                << " expected=" << expectedId << " scalar=" << scalarId
                << " optimized=" << optimizedId << " actor=" << pose.instanceId
                << " preferred=" << preferred << " force=" << force
                << " distributed=" << distributed << " passenger=" << passenger
                << " fixedWeapon=" << (weapon != nullptr) << " bonus=" << bonus
                << " psychicStatus=" << (status != nullptr)
                << " expiresTick=" << (status ? status->expiresTick : -1)
                << " attackOriginalAllies=" << (status && status->attackOriginalAllies)
                << " worldUnchanged=" << worldUnchanged
                << " psychicUnchanged=" << psychicUnchanged
                << " combatUnchanged=" << combatUnchanged;
            checkDiagnostic = details.str();
        }
        return passed;
    };
    const WeaponType* radial = state.rules.findWeaponType("RADIAL");
    if (!radial) return fail("missing RADIAL fixture weapon");
    if (!check(*findObjectById(state, 1), nullptr, 0, false, -1, false, false, -1)
        || !check(*findObjectById(state, 1), radial, 0, true, -1, false, false, -1))
        return fail("self or the 128 friendly objects became targets");
    // A homogeneous moving army shares one exact owner census. Negative damage
    // (healing/repair) is already rejected by the original opportunity selector;
    // this gate changes no other weapon or treatment path.
    const auto friendlyPose = *findObjectById(state, 1);
    ownerSummary.invalidate();
    const int scansBeforeFriendly = ownerSummary.populationScans;
    for (int i = 0; i < 128; ++i) {
        if (!ownerSummary.provesNoCandidate(state, friendlyPose, false)
            || !check(friendlyPose, nullptr, 0, false, -1, false, false, -1))
            return fail("friendly owner summary changed a target");
    }
    if (ownerSummary.populationScans != scansBeforeFriendly + 1)
        return fail("friendly owner census was not reused within the pass");
    if (ownerSummary.provesNoCandidate(state, friendlyPose, true)
        || !check(friendlyPose, nullptr, 0, false, 100, true, false, 100))
        return fail("owner summary hid a forced allied target");
    for (bool actorBerserkFriendly : {false, true}) {
        state.psychicStatuses.reset();
        RuntimePsychicStatusHitRequest hit;
        hit.currentTick = 10; hit.hitEventId = actorBerserkFriendly ? 993 : 992;
        hit.durationTicks = 20;
        hit.source = runtimePsychicObjectSnapshot(state, *findObjectById(state, 100));
        hit.target = runtimePsychicObjectSnapshot(state, friendlyPose);
        hit.target.immuneToPsionics = false; hit.target.berserkFriendly = actorBerserkFriendly;
        hit.warhead = {"__OPPORTUNITY_OWNER_TEST_GAS__", true, true};
        if (!state.psychicStatuses.applyHit(hit).accepted)
            return fail("friendly-only psychic fixture hit rejected");
        if (ownerSummary.provesNoCandidate(state, friendlyPose, false)
            || !check(friendlyPose, nullptr, 0, false, 100, false, false,
                100))
            return fail("owner summary suppressed an active psychic policy");
        state.tick = 30;
        if (!ownerSummary.provesNoCandidate(state, friendlyPose, false)
            || !check(friendlyPose, nullptr, 0, false, 100, false, false, -1))
            return fail("psychic expiry did not restore ordinary relations");
        state.tick = 10;
    }
    state.psychicStatuses.reset();
    // Model an immediate fire action changing an existing object's owner,
    // retaining both vector identity and size. Production revokes this same
    // summary before each fire call, even if that call reports no projectile.
    auto* changedOwner = findMutableObjectById(state, 100);
    const auto* sameData = state.world.getObjects().data();
    const auto sameSize = state.world.getObjects().size();
    ownerSummary.invalidate();
    changedOwner->owner = "Enemy";
    if (state.world.getObjects().data() != sameData || state.world.getObjects().size() != sameSize
        || ownerSummary.provesNoCandidate(state, friendlyPose, false)
        || !check(friendlyPose, nullptr, 0, false, 100, false, false, 100))
        return fail("post-fire owner change reused the previous empty relation");
    const int scansAfterHostile = ownerSummary.populationScans;
    for (int attempt = 0; attempt < 128; ++attempt) {
        ownerSummary.invalidate();
        if (!ownerSummary.disabledForPass
            || ownerSummary.provesNoCandidate(state, friendlyPose, false)
            || ownerSummary.populationScans != scansAfterHostile)
            return fail("active battle rebuilt the owner census after a fire invalidation");
    }
    // Even if the enemy disappears, this same pass only forgoes the shortcut.
    // A newly constructed pass may resume proving empty relations.
    changedOwner->owner = "Neutral";
    ownerSummary.invalidate();
    if (ownerSummary.provesNoCandidate(state, friendlyPose, false)
        || ownerSummary.populationScans != scansAfterHostile
        || !check(friendlyPose, nullptr, 0, false, -1, false, false, -1))
        return fail("disappearing enemy incorrectly re-enabled this pass");
    ownerSummary = RuntimeCombatOpportunityOwnerSummary{};
    if (!ownerSummary.provesNoCandidate(state, friendlyPose, false)
        || ownerSummary.disabledForPass || ownerSummary.populationScans != 1
        || !check(friendlyPose, nullptr, 0, false, -1, false, false, -1)
        || !check(friendlyPose, nullptr, 0, false, 100, true, false, 100))
        return fail("neutral or forced-neutral relation changed");
    // Non-player owners are not automatically enemies in the campaign-style
    // fallback; skirmish slots make them enemies only through the exact rule.
    auto otherPose = friendlyPose; otherPose.owner = "Other";
    changedOwner->owner = "Third";
    for (auto& object : state.world.accessObjects()) {
        if (object.instanceId != 100) object.owner = "Other";
    }
    ownerSummary = RuntimeCombatOpportunityOwnerSummary{};
    if (!ownerSummary.provesNoCandidate(state, otherPose, false)
        || !check(otherPose, nullptr, 0, false, -1, false, false, -1))
        return fail("non-player fallback relation changed");
    state.skirmishCountryByOwner["Other"] = "Americans";
    state.skirmishCountryByOwner["Third"] = "Russians";
    ownerSummary.invalidate();
    if (ownerSummary.provesNoCandidate(state, otherPose, false)
        || !check(otherPose, nullptr, 0, false, 100, false, false, 100))
        return fail("skirmish hostile-owner relation changed");
    state.skirmishCountryByOwner.clear();
    for (auto& object : state.world.accessObjects()) object.owner = "Player";
    ownerSummary = RuntimeCombatOpportunityOwnerSummary{};
    // Deliberately insert equal-distance enemies in descending id order.
    add(3, "TARGET", "Enemy", WorldObjectKind::Structure, 6, 6);
    add(2, "TARGET", "Enemy", WorldObjectKind::Structure, 4, 8);
    add(4, "TARGET", "Enemy", WorldObjectKind::Structure, 4, 6);
    add(5, "TARGET", "Special", WorldObjectKind::Structure, 4, 7);
    add(6, "TARGET", "Enemy", WorldObjectKind::Structure, 4, 6);
    findMutableObjectById(state, 4)->alive = false;
    findMutableObjectById(state, 6)->health = 0;
    const auto pose = *findObjectById(state, 1);
    if (!check(pose, nullptr, 0, false, -1, false, false, 2)
        || !check(pose, nullptr, 0, false, 3, false, false, 3)
        || !check(pose, nullptr, 0, false, 100, true, false, 100)
        || !check(pose, nullptr, 0, false, 5, true, false, 5)
        || !check(pose, nullptr, 0, false, 100, false, false, 2))
        return fail("nearest, neutral, preferred or forced-friendly target changed");
    for (int cursor : {-1, 2, 3, 999}) {
        state.combatDetails.fireCursors[1].lastTargetObjectId = cursor;
        for (const WeaponType* weapon : {static_cast<const WeaponType*>(nullptr), radial})
            for (bool distributed : {false, true}) for (bool force : {false, true})
                if (!check(pose, weapon, 0, distributed, 100, force, false))
                    return fail("distributed cursor or forced preferred order changed");
    }
    // Materialize a real psychic status through its value API, including
    // both actor BerserkFriendly values and the exact expiration boundary.
    for (bool actorBerserkFriendly : {false, true}) {
        state.psychicStatuses.reset();
        RuntimePsychicStatusHitRequest hit;
        hit.currentTick = 10; hit.hitEventId = 991; hit.durationTicks = 20;
        hit.source = runtimePsychicObjectSnapshot(state, *findObjectById(state, 2));
        hit.target = runtimePsychicObjectSnapshot(state, pose);
        hit.target.immuneToPsionics = false;
        hit.target.berserkFriendly = actorBerserkFriendly;
        hit.warhead = {"__OPPORTUNITY_TEST_GAS__", true, true};
        if (!state.psychicStatuses.applyHit(hit).accepted) return fail("psychic fixture hit rejected");
        for (int tick : {10, 29, 30}) {
            state.tick = tick;
            // Active berserk ignores allegiance for either actor flag value.
            // Without a preferred target, neutral id 5 is the nearest.
            const int nearestWithoutPreferred = tick < 30 ? 5 : 2;
            for (bool distributed : {false, true}) for (bool force : {false, true})
                if (!check(pose, nullptr, 0, distributed, 100, force, false,
                        distributed ? -2 : (force || tick < 30
                            ? 100 : nearestWithoutPreferred)))
                    return fail("berserk ally override or expiry changed");
        }
    }
    state.psychicStatuses.reset();
    state.tick = 10;
    add(10, "FORTRESS", "Player", WorldObjectKind::Unit, 2, 5);
    add(11, "SOLDIER", "Player", WorldObjectKind::Infantry, 0, 0);
    auto* passenger = findMutableObjectById(state, 11);
    passenger->alive = false; passenger->mission = "InsideTransport";
    findMutableObjectById(state, 10)->transportPassengerIds = {11};
    const auto passengerPose = runtimeCombatFiringPose(*passenger, findObjectById(state, 10));
    for (bool distributed : {false, true}) for (bool force : {false, true})
        if (!check(passengerPose, nullptr, 2, distributed, 100, force, true))
            return fail("open-topped passenger targeting changed");
    // A distant hostile population used to build a complete target-dependent
    // weapon profile for every member before rejecting all of them by range.
    constexpr int distantPopulation = 256;
    for (int index = 0; index < distantPopulation; ++index)
        add(300 + index, "TARGET", "Enemy", WorldObjectKind::Structure, 64 + index % 16, 64 + index / 16);
    const RuntimeCombatOpportunityRangeBound ordinaryBound(state, pose, nullptr, 0, false);
    if (!ordinaryBound.enabled) return fail("ordinary range fixture did not enable its bound");
    const auto beforeScalar = state.capabilityCache.stats.weaponLookups;
    const auto* scalarFar = selectRuntimeCombatOpportunityTarget(state, pose, nullptr, 0, false,
        -1, false, false, true);
    const auto scalarLookups = state.capabilityCache.stats.weaponLookups - beforeScalar;
    const auto beforeBounded = state.capabilityCache.stats.weaponLookups;
    const auto* boundedFar = selectRuntimeCombatOpportunityTarget(state, pose, nullptr, 0, false,
        -1, false, false, false);
    const auto boundedLookups = state.capabilityCache.stats.weaponLookups - beforeBounded;
    const char* oldReference = std::getenv("CONCORD_MOVEMENT_REFERENCE");
    const bool expectsBound = !runtimeFrameScalingReferenceMovement()
        && !(oldReference && oldReference[0] == '1' && oldReference[1] == '\0');
    if (scalarFar != boundedFar
        || (expectsBound && (boundedLookups > 8 || scalarLookups < boundedLookups + distantPopulation)))
        return fail("far enemy filter changed selection or retained per-enemy weapon profiling");
    for (bool distributed : {false, true}) for (bool force : {false, true})
        if (!check(pose, nullptr, 0, distributed, 555, force, false))
            return fail("far preferred/distributed/forced candidate changed ordering");

    int rangeCases = 0;
    const auto mustFallBack = [&](const WorldObjectInstance& source,
        const WeaponType* fixed, double bonus, bool passengerContext) {
        ++rangeCases;
        return !RuntimeCombatOpportunityRangeBound(state, source, fixed, bonus, passengerContext).enabled;
    };
    auto specialPose = pose;
    if (!mustFallBack(pose, radial, 0, false) || !mustFallBack(pose, nullptr, 2, false)
        || !mustFallBack(pose, nullptr, 0, true)) return fail("fixed/passenger/bonus range fallback");
    specialPose.airborne = true;
    if (!mustFallBack(specialPose, nullptr, 0, false)) return fail("air range fallback");
    specialPose = pose; specialPose.tankBunkerHostId = 10;
    if (!mustFallBack(specialPose, nullptr, 0, false)) return fail("bunker range fallback");
    specialPose = pose; specialPose.activeGunnerWeaponIndex = 0;
    if (!mustFallBack(specialPose, nullptr, 0, false)) return fail("gunner range fallback");
    specialPose = pose; specialPose.kind = WorldObjectKind::Structure;
    if (!mustFallBack(specialPose, nullptr, 0, false)) return fail("building range fallback");
    const auto profileKey = runtimeCapabilityCacheKeyForObject(pose);
    const auto savedProfile = state.capabilityCache.profilesByType.at(profileKey);
    const auto cachedFallback = [&](auto alter) {
        auto& profile = state.capabilityCache.profilesByType.at(profileKey);
        alter(profile);
        const bool fallback = mustFallBack(pose, nullptr, 0, false);
        profile = savedProfile;
        return fallback;
    };
    if (!cachedFallback([](auto& profile) { profile.airRangeBonus = 4.0; })
        || !cachedFallback([](auto& profile) { profile.gattling = true; })
        || !cachedFallback([](auto& profile) { profile.indexedWeaponCandidates.emplace_back(RuntimeWeaponSlot::Indexed, "RADIAL"); })
        || !cachedFallback([](auto& profile) { profile.eliteIndexedWeaponCandidates.emplace_back(RuntimeWeaponSlot::EliteIndexed, "RADIAL"); })
        || !cachedFallback([](auto& profile) { profile.openTransportWeapon = RuntimeWeaponCandidate{RuntimeWeaponSlot::OpenTransport, "RADIAL"}; }))
        return fail("air bonus/indexed/gattling/open-transport range fallback");
    // The mutable fixture owns its rules. Restore every change before returning
    // to the caller; normal game rules and cached profiles remain untouched.
    auto* mutableRadial = const_cast<WeaponType*>(radial);
    const WeaponType savedRadial = *mutableRadial;
    mutableRadial->spawner = true; mutableRadial->range = -2.0;
    const bool infiniteFallback = mustFallBack(pose, nullptr, 0, false);
    *mutableRadial = savedRadial;
    mutableRadial->range = std::numeric_limits<double>::quiet_NaN();
    const bool nonFiniteFallback = mustFallBack(pose, nullptr, 0, false);
    *mutableRadial = savedRadial;
    if (!infiniteFallback || !nonFiniteFallback) return fail("infinite/nonfinite weapon range fallback");

    WorldObjectInstance targetProbe = *findObjectById(state, 2);
    const auto savedFootprint = state.structureFootprints.at("TARGET");
    // Test the mathematical rejection independently of selection: any target
    // satisfying the actual weapon range must survive this conservative bound.
    bool boundValid = true;
    for (double range : {0.0, 1.0, 1.999999999999, 2.0, 4.0, 10.0}) {
        mutableRadial->range = range;
        for (bool cellRange : {false, true}) {
            mutableRadial->cellRangefinding = cellRange;
            for (int width : {1, 7, 32}) {
                state.structureFootprints["TARGET"] = {width, 3, true};
                for (int level : {0, 1, 4, 20}) {
                    auto sourceProbe = pose; sourceProbe.level = level;
                    const RuntimeCombatOpportunityRangeBound bound(state, sourceProbe, nullptr, 0, false);
                    for (bool targetAir : {false, true}) for (int dx = -24; dx <= 24; ++dx) {
                        targetProbe.x = sourceProbe.x + dx; targetProbe.y = sourceProbe.y;
                        targetProbe.level = 0; targetProbe.airborne = targetAir;
                        const double distance = runtimeWeaponDistanceToTarget(state, sourceProbe, targetProbe, *mutableRadial);
                        const double actualRange = runtimeEffectiveWeaponRange(state, sourceProbe, targetProbe, *mutableRadial);
                        if (distance <= actualRange && bound.excludes(state, sourceProbe, targetProbe)) boundValid = false;
                        ++rangeCases;
                    }
                }
            }
        }
    }
    *mutableRadial = savedRadial;
    state.structureFootprints["TARGET"] = savedFootprint;
    if (!boundValid) return fail("range boundary/height/air/footprint/cell-range lower bound");
    targetProbe.x = std::numeric_limits<int>::max() - 4;
    targetProbe.y = pose.y;
    state.structureFootprints["TARGET"] = {32, 1, true};
    const bool foundationFallback = !ordinaryBound.excludes(state, pose, targetProbe);
    state.structureFootprints["TARGET"] = savedFootprint;
    auto extremePose = pose; extremePose.x = std::numeric_limits<int>::min();
    const bool coordinateFallback = !ordinaryBound.excludes(state, extremePose, targetProbe);
    rangeCases += 2;
    if (!foundationFallback || !coordinateFallback) return fail("unrepresentable range geometry did not fall back");
    if (!check(pose, nullptr, 0, false, -1, false, false, 2))
        return fail("range contract fixture did not restore ordinary targeting");
    std::cout << "Combat opportunity equivalence passed: cases=" << cases
        << " friendlyPopulation=128 ownerCensus/ownerMutation/neutral/psychic/force/distributed/passenger=yes"
        << " distantPopulation=" << distantPopulation << " rangeCases=" << rangeCases
        << " scalarWeaponLookups=" << scalarLookups << " boundedWeaponLookups=" << boundedLookups << '\n';
    error.clear();
    return true;
}
