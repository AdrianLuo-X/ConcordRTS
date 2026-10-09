// Included after the existing native/warmed helpers, outside their namespace.
// Stock leaders are produced by the original map decoder's roster rotation.
// This is correctness coverage, never an FPS or command-latency claim.
#pragma once

namespace RuntimeMixedLocalComponentContractDetail {
bool stockLeaders(const RuntimeConfig& config, std::string& error, bool verifyKnownDefault=false) {
    const auto fail = [&](const std::string& message) {
        error = "mixed local stock leaders: " + message;
        return false;
    };
    if(verifyKnownDefault && runtimeMixedKnownUnmappedReference())return fail("default Known branch disabled");
    std::size_t knownFlagChecks=0,knownDefaultCommands=0;
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world,
        &assets->theater, &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("stock resources");

    constexpr std::array<int, 4> offsets{{1, 2, 3, 5}};
    constexpr std::array<const char*, 4> leaders{{"SREF", "TTNK", "FV", "HTNK"}};
    constexpr std::array<const char*, 12> types{{"MTNK", "SREF", "TTNK", "FV", "SCHP", "HTNK",
        "E1", "CLEG", "JUMPJET", "SNIPE", "GHOST", "E2"}};
    constexpr RuntimeCellPoint goal{196, 152};
    std::size_t cases = 0, actors = 0, typeReadbacks = 0, leaderReadbacks = 0;
    std::size_t warmTicks = 0, commands = 0, referenceCommands = 0, admittedCommands = 0;
    std::size_t forcedLateMaps = 0, preCommandSaveTriplets = 0, fullSaveTriplets = 0;
    std::size_t trajectoryTriplets = 0, originalSccWitnesses = 0, certifiedPoints = 0;
    std::size_t actualIdleReadbacks = 0, originalOrderedMaps = 0;
    RuntimeMixedLocalComponentStats totals;
    RuntimeStrictSlotVehicleReferenceScope keepStrictSeparate(true);

    for (std::size_t caseIndex = 0; caseIndex < leaders.size(); ++caseIndex) {
        std::array<std::unique_ptr<RuntimeWindowState>, 3> states;
        std::array<std::vector<int>, 3> ids;
        for (int branch = 0; branch < 3; ++branch) {
            states[branch] = std::make_unique<RuntimeWindowState>();
            if (!prepareNative(*states[branch], *assets, config, 240, 0, false,
                    ids[branch], error, 48, offsets[caseIndex])) return false;
            auto& state = *states[branch];
            if (state.tick != 0 || ids[branch].size() != 48 || state.world.getObjects().size() != 57)
                return fail("fresh normally hydrated native48 cardinality");
            std::array<std::size_t, 12> census{};
            for (int id : ids[branch]) {
                const auto* actor = findObjectById(state, id);
                if (!actor || !actor->alive || actor->health <= 0) return fail("actual living actor");
                const auto type = std::find(types.begin(), types.end(), actor->type);
                if (type == types.end()) return fail("unexpected original decoded stock type");
                ++census[static_cast<std::size_t>(type - types.begin())];
            }
            if (!std::all_of(census.begin(), census.end(), [](std::size_t value) { return value == 4; }))
                return fail("twelve stock types, four each, after original roster rotation");
            const auto* leader = findObjectById(state, ids[branch].front());
            const auto* unit = leader ? findRuntimeUnitType(state.rules, *leader) : nullptr;
            if (!leader || !unit || leader->instanceId < 0 || leader->type != leaders[caseIndex]
                || leader->kind != WorldObjectKind::Unit || leader->airborne || unit->teleporter
                || runtimeObjectMovementMode(state, *leader) != PathMovementMode::Ground
                || leader->x != 124 || leader->y != 140)
                return fail("actual stock nonteleporter Ground Unit leader identity/type/coordinate");
        }
        if (ids[0] != ids[1] || ids[0] != ids[2]) return fail("original decoded instance-ID order");
        ++cases; actors += 48; typeReadbacks += 12;

        for (int tick = 1; tick <= 64; ++tick) {
            std::array<Authority, 3> trajectory, full;
            std::array<std::map<int, RuntimeCellPoint>, 3> destinations;
            std::array<std::pair<int, int>, 3> acceptedRejected{};
            std::vector<std::pair<int, int>> originalOrder;
            for (int branch = 0; branch < 3; ++branch) {
                auto& state = *states[branch];
                state.tick = tick;
                RuntimeSnapshotActiveStateScope active(state);
                RuntimeMixedLocalComponentReferenceScope reference(branch == 0);
                if (tick == 61) {
                    const auto* leader = findObjectById(state, ids[branch].front());
                    const auto* unit = leader ? findRuntimeUnitType(state.rules, *leader) : nullptr;
                    if (!leader || !unit || leader->type != leaders[caseIndex] || unit->teleporter
                        || leader->kind != WorldObjectKind::Unit || leader->airborne
                        || runtimeObjectMovementMode(state, *leader) != PathMovementMode::Ground
                        || !runtimeObjectCanReceiveMoveCommand(state, *leader))
                        return fail("real 60-tick warmup changed representative eligibility");
                    ++leaderReadbacks;
                    std::size_t idle = 0;
                    for (int id : ids[branch]) {
                        const auto* actor = findObjectById(state, id);
                        if (actor && (actor->mission == "Idle1" || actor->mission == "Idle2")) ++idle;
                    }
                    if (!idle) return fail("original warmup did not produce an actual Idle1/Idle2 actor");
                    if (branch == 0) actualIdleReadbacks += idle;

                    if (branch == 0) {
                        std::unordered_set<int> moving;
                        for (int id : ids[branch]) {
                            const auto* actor = findObjectById(state, id);
                            if (actor && runtimeObjectCanReceiveMoveCommand(state, *actor)
                                && runtimeObjectAllowsMoveToCell(state, *actor, goal)) moving.insert(id);
                        }
                        const auto occupied = buildRuntimeOccupancyLookup(state, -1, nullptr, &moving);
                        const auto map = buildRuntimeMoveComponentByCell(state, *leader,
                            state.terrainByCell, occupied);
                        originalOrder.assign(map.begin(), map.end());
                        const auto root = map.find(runtimeCellKey(leader->x, leader->y));
                        if (root == map.end()) return fail("original whole directed graph omitted actual leader");
                        RuntimeMixedLocalComponentReferenceScope optimized(false);
                        RuntimeMixedLocalComponentStats proofStats;
                        RuntimeMixedLocalComponentStatsScope observe(proofStats,verifyKnownDefault?&originalOrder:nullptr);
                        RuntimeMixedLocalComponentCertificate proof;
                        if (!proof.prepare(state, ids[branch], goal, state.terrainByCell, occupied,
                                leader->instanceId, moving.size()) || !proof.enabled())
                            return fail("stock leader SCC admission type=" + leader->type
                                + " reason=" + std::to_string(proofStats.rejectionReason));
                        if (proof.certifiedKeys().empty()) return fail("no actual certified vertex");
                        for (const auto& [key, edges] : proof.certifiedKeys()) {
                            (void)edges;
                            const auto found = map.find(key);
                            if (found == map.end() || found->second != root->second)
                                return fail("stock leader certified key differs from original directed component");
                        }
                        if(verifyKnownDefault) for(int key:proof.knownUnmappedKeys())
                            if(map.contains(key))return fail("default leader direct certificate published mapped key");
                        certifiedPoints += proof.certifiedKeys().size();
                        ++originalSccWitnesses;
                    }

                    RuntimeMixedLocalComponentStats stats;
                    {
                        const std::size_t limit = branch == 2 ? 21 : 256;
                        RuntimeMixedLocalComponentStatsScope observe(stats, &originalOrder, limit);
                        if (!issueRuntimeMassMovementOrder(state, ids[branch], goal,
                                destinations[branch], error, true)) return false;
                    }
                    acceptedRejected[branch] = {state.lastCommand.acceptedCount, state.lastCommand.rejectedCount};
                    if(verifyKnownDefault) {
                        if(runtimeMixedKnownUnmappedReference() || stats.unmappedOriginalMismatches
                            || stats.unmappedOriginalChecks!=stats.unmappedProofs)
                            return fail("default leader reference/complete-map absence check");
                        ++knownFlagChecks;if(branch!=0)++knownDefaultCommands;
                    }
                    if (state.lastCommand.acceptedCount != 48 || state.lastCommand.rejectedCount != 0
                        || destinations[branch].size() != 48 || stats.attempts != 1
                        || stats.orderedMapMismatches || stats.wholeMaps != stats.orderedMapsCompared)
                        return fail("actual whole command/acceptance/ordered-map oracle");
                    if (branch == 0) {
                        if (stats.referenceFallbacks != 1 || stats.wholeMaps != 1 || stats.admitted)
                            return fail("original reference must build the full ordered graph");
                        ++referenceCommands;
                    } else {
                        if (stats.admitted != 1 || !stats.coveredBest || !stats.idleActors
                            || stats.guardFallbacks || stats.walkFallbacks || stats.allocationFallbacks)
                            return fail("stock leader positive actual command admission type=" + leader->type);
                        ++admittedCommands;
                        if (branch == 1 && stats.wholeMaps) return fail("complete certificate unexpectedly fell back");
                        if (branch == 2) {
                            if (stats.outsideFallbacks != 1 || stats.lateWholeMaps != 1 || stats.wholeMaps != 1)
                                return fail("formationLimit21 must materialize the exact original late whole graph");
                            ++forcedLateMaps;
                        }
                    }
                    originalOrderedMaps += stats.orderedMapsCompared;
                    add(totals, stats);
                    ++commands;
                }

                (void)advanceRuntimeSimulation(state);
                (void)completeRuntimeWorldRefresh(state);
                trajectory[branch] = captureTrajectory(state);
                // Capture only completed authority ticks, never fake clock synchronization.
                if (tick >= 60 && !capture(state, full[branch], error)) return false;
            }
            if (trajectory[0] != trajectory[1] || trajectory[0] != trajectory[2])
                return fail("every completed stock-leader world/path trajectory tick=" + std::to_string(tick));
            ++trajectoryTriplets;
            if (tick >= 60) {
                if (full[0] != full[1] || full[0] != full[2])
                    return fail("completed full writer/hash/system clocks type=" + std::string(leaders[caseIndex])
                        + " tick=" + std::to_string(tick));
                if (tick == 60) ++preCommandSaveTriplets; else ++fullSaveTriplets;
            }
            if (tick <= 60) ++warmTicks;
            if (tick == 61 && (destinations[0] != destinations[1] || destinations[0] != destinations[2]
                    || acceptedRejected[0] != acceptedRejected[1] || acceptedRejected[0] != acceptedRejected[2]))
                return fail("exact stock-leader command acceptance and every destination");
        }
        std::cout << "Mixed local stock leader readback: offset=" << offsets[caseIndex]
            << " type=" << leaders[caseIndex] << " id=" << ids[0].front()
            << " native48=yes warmupCompletedTick=60 commandTick=61 goal=196,152"
            << " original/fullCertificate/limitedCertificate=exact passed=yes\n";
    }

    if (cases != 4 || actors != 192 || typeReadbacks != 48 || leaderReadbacks != 12
        || warmTicks != 240 || commands != 12 || referenceCommands != 4 || admittedCommands != 8
        || forcedLateMaps != 4 || preCommandSaveTriplets != 4 || fullSaveTriplets != 16
        || trajectoryTriplets != 256 || originalSccWitnesses != 4 || originalOrderedMaps != 8
        || !certifiedPoints || !actualIdleReadbacks || totals.orderedMapMismatches)
        return fail("exact independent stock-leader coverage counts");
    std::cout << "Mixed local stock leader coverage: nativeCases=4 nativeActors=192 nativeTypeReadbacks=48"
        << " leaderReadbacks=12 warmupTicks=240 nativeCommands=12 referenceCommands=4 admittedCommands=8"
        << " forcedLateMaps=4 preCommandFullSaveTriplets=4 completedFullSaveTriplets=16"
        << " completedTrajectoryTriplets=256 originalSccWitnesses=4 originalOrderedMaps=8 certifiedPoints="
        << certifiedPoints << " idleReadbacks=" << actualIdleReadbacks
        << " leaders=SREF/TTNK/FV/HTNK originalLabels/orderedLateWhole/acceptance/destinations/paths/fullWriter=exact passed=yes\n";
    if(verifyKnownDefault) {
        if(knownFlagChecks!=12 || knownDefaultCommands!=8 || totals.unmappedOriginalMismatches
            || totals.unmappedOriginalChecks!=totals.unmappedProofs)
            return fail("default stock leader coverage/absence totals");
        std::cout<<"Known-unmapped default stock leaders: nativeCases=4 nativeActors=192 nativeTypeReadbacks=48 warmupTicks=240 nativeCommands=12 defaultCommands=8 referenceCommands=4 flagChecks=12 admittedCommands=8 forcedLateMaps=4 preCommandFullSaveTriplets=4 completedFullSaveTriplets=16 completedTrajectoryTriplets=256 actualUnmappedProofs="
            <<totals.unmappedProofs<<" actualPublishedKeysChecked="<<totals.unmappedOriginalChecks
            <<" featureReference=false SREF/TTNK/FV/HTNK/originalAbsentKeys/orderedWholeFallback/commands/paths/fullWriter=exact passed=yes\n";
    }
    return true;
}
} // namespace RuntimeMixedLocalComponentContractDetail
