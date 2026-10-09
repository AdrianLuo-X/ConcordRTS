// Include after the existing actual planner/workset native contracts.
bool runRuntimePlannerEmptySubcellBooleanContract(const RuntimeConfig& config, std::string& error) {
    using namespace RuntimePlannerPassabilityContractDetail;
    const auto fail = [&](const std::string& why) {
        error = "planner empty subcell boolean: " + why; return false;
    };
    struct Witness {
        std::size_t calls = 0, available = 0;
        static void append(void* context, bool value) {
            auto& out = *static_cast<Witness*>(context);
            ++out.calls; out.available += value;
        }
    };
    RuntimeComponentKernelReferenceScope kernel(false);
    RuntimeComponentTerrainQueryReferenceScope terrain(false);
    RuntimeComponentColdLookupReferenceScope cold(false);
    RuntimeComponentPassabilityReferenceScope passability(false);
    RuntimeComponentDenseReferenceScope dense(false);
    RuntimeComponentQueryLeaseReferenceScope lease(false);
    RuntimeComponentEmptyCensusReferenceScope census(false);
    std::size_t nativeCases = 0, graphComparisons = 0, nativeCalls = 0;
    std::size_t nativeCommands = 0, tickPairs = 0, fullSaves = 0, observerFallbacks = 0;
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("actual stock assets");
    const auto prepare = [&](RuntimeWindowState& state, int side, int history,
        std::vector<int>& ids) {
        state.config = config; state.report = assets->report;
        state.rules = assets->rules; state.art = assets->art; state.theater = assets->theater;
        state.rulesLoaded = assets->report.rulesLoaded; state.artLoaded = assets->report.artLoaded;
        state.theaterLoaded = assets->report.theaterLoaded;
        RuntimeSnapshotActiveStateScope active(state);
        if (!prepareRuntimeMassMovementWorld(state, side == 256 ? 256 : 48, ids, error,
                false, true, false, side, side, true, true)) return false;
        state.config.defaultMapPath = state.report.map.path;
        if (history) {
            const int first = nextRuntimeObjectInstanceId(state);
            if (first <= 0 || first > std::numeric_limits<int>::max() - history) return false;
            state.world.accessObjects().reserve(state.world.getObjects().size() + history);
            for (int n = 0; n < history; ++n) {
                auto dead = object(first + n, WorldObjectKind::Unit, "MTNK", 2, 2);
                dead.owner = "Americans"; dead.alive = false; dead.health = 0; dead.mission = "Destroyed";
                state.world.accessObjects().push_back(std::move(dead));
            }
            auto metadata = std::move(state.terrainInfoByCell);
            state.objectIndex = RuntimeObjectIndex{};
            initializeRuntimeNavigation(state);
            state.terrainInfoByCell = std::move(metadata);
            invalidateRuntimeTickOccupancyCache(state);
            state.pathTerrainCache.valid = state.pathTerrainCache.dynamicValid = false;
        }
        (void)completeRuntimeWorldRefresh(state);
        return true;
    };
    const auto same = [&](const RuntimeWindowState& old, const RuntimeWindowState& now) {
        Authority before, after;
        if (!capture(old, before, error) || !capture(now, after, error)) return false;
        fullSaves += 2;
        return before == after;
    };
    for (int side : {240, 256}) for (int history : {0, 10000}) {
        auto oldBox = std::make_unique<RuntimeWindowState>();
        auto newBox = std::make_unique<RuntimeWindowState>();
        auto& old = *oldBox; auto& now = *newBox;
        std::vector<int> oldIds, newIds;
        if (!prepare(old, side, history, oldIds) || !prepare(now, side, history, newIds)
            || oldIds != newIds || !same(old, now)) return fail("identical actual native producers");
        int groundId = -1, infantryId = -1;
        std::vector<int> infantry;
        for (int id : oldIds) {
            const auto* actor = findObjectById(old, id);
            if (!actor) return fail("selected native object");
            if (runtimeObjectMovementMode(old, *actor) != PathMovementMode::Ground) continue;
            if (actor->kind == WorldObjectKind::Unit && groundId < 0) groundId = id;
            if (actor->kind == WorldObjectKind::Infantry) {
                if (infantryId < 0) infantryId = id;
                if (infantry.size() < 16) infantry.push_back(id);
            }
        }
        if (groundId < 0 || infantryId < 0 || infantry.empty()) return fail("actual native Ground/Infantry");
        for (int id : {groundId, infantryId}) {
            std::unordered_map<int, int> expected, actual;
            std::array<std::vector<std::pair<int, int>>, 2> iterationOrder;
            for (int route = 0; route < 2; ++route) {
                auto& owner = route == 0 ? old : now;
                RuntimeSnapshotActiveStateScope active(owner);
                const auto* mover = findObjectById(owner, id);
                if (!mover) return fail("native graph mover");
                const auto occupancy = buildRuntimeOccupancyLookup(owner);
                Witness witness;
                RuntimePlannerEmptySubcellBooleanReferenceScope reference(route == 0);
                RuntimePlannerEmptySubcellBooleanObserverScope observer(&Witness::append, &witness);
                auto graph = buildRuntimeMoveComponentByCell(owner, *mover, owner.terrainByCell, occupancy);
                if (graph.empty() || (route == 0 && witness.calls != 0)
                    || (id == groundId && witness.calls != 0)
                    || (route == 1 && id == infantryId && witness.calls == 0))
                    return fail("real positive/native non-infantry/reference eligibility");
                nativeCalls += witness.calls;
                iterationOrder[route].assign(graph.begin(), graph.end());
                (route == 0 ? expected : actual) = std::move(graph);
            }
            if (expected != actual || iterationOrder[0] != iterationOrder[1] || !same(old, now))
                return fail("complete original ordered directed graph/full writer");
            ++graphComparisons;
        }
        std::map<int, RuntimeCellPoint> expectedDestinations, actualDestinations;
        int expectedAccepted = -1, expectedRejected = -1;
        const RuntimeCellPoint target{side / 2 + 22, side / 2 + 48};
        for (int route = 0; route < 2; ++route) {
            auto& owner = route == 0 ? old : now;
            RuntimeSnapshotActiveStateScope active(owner);
            RuntimePlannerEmptySubcellBooleanReferenceScope reference(route == 0);
            auto& destinations = route == 0 ? expectedDestinations : actualDestinations;
            if (!issueRuntimeMassMovementOrder(owner, infantry, target, destinations, error, false))
                return fail("actual infantry group command: " + error);
            if (route == 0) {
                expectedAccepted = owner.lastCommand.acceptedCount;
                expectedRejected = owner.lastCommand.rejectedCount;
            } else if (expectedDestinations != actualDestinations
                || expectedAccepted != owner.lastCommand.acceptedCount
                || expectedRejected != owner.lastCommand.rejectedCount) {
                return fail("actual destinations and accepted/rejected counts");
            }
            ++nativeCommands;
        }
        if (!same(old, now)) return fail("actual command acceptance/paths/complete writer");
        for (int tick = 0; tick < 8; ++tick) {
            for (int route = 0; route < 2; ++route) {
                auto& owner = route == 0 ? old : now;
                RuntimeSnapshotActiveStateScope active(owner);
                RuntimePlannerEmptySubcellBooleanReferenceScope reference(route == 0);
                ++owner.tick; (void)advanceRuntimeSimulation(owner, false);
                (void)completeRuntimeWorldRefresh(owner);
            }
            if (!same(old, now)) return fail("completed tick full writer/subcells/gates/repair/mag");
            ++tickPairs;
        }
        ++nativeCases;
    }
    // Old observers keep their original query/census route. A real cold owner
    // is established by the actual >=4096-cell BFS; the new witness must be 0.
    for (int mode = 0; mode < 2; ++mode) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& owner = *box;
        std::vector<int> ids;
        if (!prepare(owner, 96, 0, ids)) return fail("observer original world");
        RuntimeSnapshotActiveStateScope active(owner);
        const WorldObjectInstance* mover = nullptr;
        for (int id : ids) {
            const auto* actor = findObjectById(owner, id);
            if (actor && actor->kind == WorldObjectKind::Infantry
                && runtimeObjectMovementMode(owner, *actor) == PathMovementMode::Ground) { mover = actor; break; }
        }
        if (!mover) return fail("observer infantry");
        const auto occupancy = buildRuntimeOccupancyLookup(owner);
        Witness witness; Observations oldQueries;
        const auto censusObserver = [](void*, bool, std::size_t, std::size_t, std::size_t) {};
        RuntimePlannerEmptySubcellBooleanReferenceScope reference(false);
        RuntimePlannerEmptySubcellBooleanObserverScope observer(&Witness::append, &witness);
        RuntimePlannerPassabilityObserverScope oldPassability(mode == 0 ? &Observations::append : nullptr, &oldQueries);
        RuntimePlannerEmptyCensusObserverScope oldCensus(mode == 1 ? +censusObserver : nullptr, nullptr);
        const auto graph = buildRuntimeMoveComponentByCell(owner, *mover, owner.terrainByCell, occupancy);
        if (graph.empty() || witness.calls != 0 || (mode == 0 && oldQueries.values.empty()))
            return fail("old observer full route");
        ++observerFallbacks;
    }
    if (nativeCases != 4 || graphComparisons != 8 || nativeCommands != 8
        || tickPairs != 32 || observerFallbacks != 2 || nativeCalls == 0)
        return fail("all native cases, real calls and completed ticks required");
    std::cout << "Planner empty subcell boolean: nativeCases=4 completeGraphs=8 nativeCommands=8"
        << " perTickFullSavePairs=32 observerFallbacks=2 nativePositiveCalls=" << nativeCalls
        << " fullSaveChecks=" << fullSaves
        << " originalDirected/order/fields/paths/fullWriter/subcells/native240/native256/H0/H10000=exact passed=yes\n";
    return true;
}
