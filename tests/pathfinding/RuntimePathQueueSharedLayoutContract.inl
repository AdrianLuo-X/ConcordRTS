#include <memory>
#include <optional>

bool sharedLayoutViewChecks(bool benchmark) {
    const auto forced = [](const char* name) {
        const char* value = std::getenv(name);
        return value && value[0] == '1' && value[1] == '\0';
    };
    const bool forcedOwned = forced("CONCORD_MOVEMENT_REFERENCE")
        || forced("CONCORD_PATH_QUEUE_LAYOUT_REFERENCE");
    const int prior = runtimePathLayoutViewReferenceOverride;
    {
        RuntimePathLayoutViewReferenceScope outer(false);
        if (runtimePathLayoutViewReference()) return false;
        { RuntimePathLayoutViewReferenceScope inner(true);
          if (!runtimePathLayoutViewReference()) return false; }
        if (runtimePathLayoutViewReference()) return false;
    }
    if (runtimePathLayoutViewReferenceOverride != prior) return false;

    // Actual containers, not an implementation-only boolean: first publication
    // moves their buffers/nodes without copying, and owners survive source reset.
    {
        RuntimePathQueueLayoutStorage cache;
        auto& data = cache.ownedForConstruction();
        data.terrainIndex = {{7, 0}, {11, 1}};
        data.terrainLookup.emplace(7, 0); data.terrainLookup.emplace(11, 1);
        data.denseTerrainIndex = {0, -1, 1};
        data.denseTerrainMinX = -1; data.denseTerrainMinY = 8;
        data.denseTerrainWidth = 3; data.denseTerrainHeight = 1;
        const auto* index = data.terrainIndex.data();
        const auto* dense = data.denseTerrainIndex.data();
        const auto* hashNode = &*data.terrainLookup.find(7);
        const auto owner = cache.shareReadOnly();
        if (!cache.isShared() || owner->terrainIndex.data() != index
            || owner->denseTerrainIndex.data() != dense
            || &*owner->terrainLookup.find(7) != hashNode) return false;
        RuntimePathQueueLayoutStorage view; view.borrow(owner);
        if (view.sharedIdentityForDiagnostics() != cache.sharedIdentityForDiagnostics()) return false;
        RuntimePathQueueLayoutStorage copy(view), assigned;
        assigned = view;
        if (copy.isShared() || assigned.isShared()
            || copy.read().terrainIndex.data() == index
            || assigned.read().denseTerrainIndex.data() == dense) return false;
        copy.ownedForConstruction().terrainIndex[0].second = 99;
        assigned.ownedForConstruction().terrainLookup[7] = 99;
        if (view.read().terrainIndex[0].second != 0
            || view.read().terrainLookup.at(7) != 0) return false;
        int refused = 0;
        try { view.ownedForConstruction(); } catch (const std::logic_error&) { ++refused; }
        try { view.borrow(owner); } catch (const std::logic_error&) { ++refused; }
        RuntimePathQueueLayoutStorage invalid;
        try { invalid.borrow({}); } catch (const std::logic_error&) { ++refused; }
        if (refused != 3) return false;
        RuntimePathQueueLayoutStorage moved(std::move(view)), moveAssigned;
        moveAssigned = std::move(moved);
        moveAssigned = std::move(moveAssigned); // self-move must preserve ownership
        RuntimePathQueueLayoutStorage empty; cache.swap(empty);
        empty = RuntimePathQueueLayoutStorage{};
        if (!moveAssigned.isShared() || moveAssigned.read().terrainIndex.data() != index
            || moveAssigned.read().denseTerrainIndex.data() != dense
            || moveAssigned.read().terrainLookup.at(11) != 1
            || moveAssigned.read().denseTerrainMinX != -1
            || moveAssigned.read().denseTerrainMinY != 8
            || moveAssigned.read().denseTerrainWidth != 3
            || moveAssigned.read().denseTerrainHeight != 1) return false;
    }

    std::size_t constructions = 0, shares = 0, resultChecks = 0;
    const auto equal = [&](const PathRequestQueue& a, const PathRequestQueue& b) {
        if (!a.layoutEqualsForDiagnostics(b)
            || !sameCompleteStats(a.liveStats(), b.liveStats())
            || a.getResults().size() != b.getResults().size()) return false;
        for (std::size_t i = 0; i < a.getResults().size(); ++i) {
            if (!sameCompleteResult(a.getResults()[i], b.getResults()[i])) return false;
            ++resultChecks;
        }
        return true;
    };
    const auto exercisePair = [&](PathRequestQueue& a, PathRequestQueue& b,
            PathCell start, PathCell goal) {
        if (!equal(a, b)) return false;
        for (int i = 0; i < 12; ++i) {
            PathApproachContext context;
            context.movementMode = static_cast<PathMovementMode>(i % 4);
            context.requesterStableHash = i / 2 + 1;
            context.movementConstraintHash = i / 2 + 1;
            context.noPathCooldownTicks = 3;
            context.goalSlotCount = 2; context.shareAcrossGoalSlots = true;
            context.mapQueryApproachAvailable = i % 3 == 0;
            if (context.mapQueryApproachAvailable) context.approachCells = {goal, start};
            context.alternateGoalCells = {goal};
            if (a.submit(start, goal, context).id != b.submit(start, goal, context).id
                || !equal(a, b)) return false;
            if (i % 2 && (a.pumpWithBudget(2, i % 4 == 1 ? 1 : 256)
                    != b.pumpWithBudget(2, i % 4 == 1 ? 1 : 256)
                || !equal(a, b))) return false;
        }
        return a.pumpWithBudget(32, 4096) == b.pumpWithBudget(32, 4096)
            && !a.getResults().empty() && equal(a, b);
    };
    for (bool layoutRef : {false, true}) for (bool lookupRef : {false, true})
      for (bool terrainRef : {false, true}) for (bool sharedRef : {false, true}) {
        RuntimePathTerrainViewReferenceScope terrainScope(terrainRef);
        RuntimePathLayoutViewReferenceScope layoutScope(sharedRef);
        const auto compare = [&](const std::vector<PathTerrainCell>& cells, bool eligible) {
            PathRequestQueueTerrainLayoutCache cache;
            { PathRequestQueue cold(RuntimeReadOnlyPathTerrainTag{}, cells, cache, layoutRef, lookupRef);
              if (cold.layoutIsSharedForDiagnostics()) return false; }
            PathRequestQueue actual(RuntimeReadOnlyPathTerrainTag{}, cells, cache, layoutRef, lookupRef);
            PathRequestQueue expected = [&] {
                RuntimePathProfileHashReferenceScope literal(true);
                return PathRequestQueue(cells, lookupRef);
            }();
            constructions += 3;
            const bool expectShared = eligible && !cells.empty() && !layoutRef && !lookupRef
                && !terrainRef && !sharedRef && !forcedOwned;
            if (actual.layoutIsSharedForDiagnostics() != expectShared) return false;
            if (expectShared && (!cache.layoutIsSharedForDiagnostics()
                || actual.sharedLayoutIdentityForDiagnostics() != cache.sharedLayoutIdentityForDiagnostics()))
                return false;
            shares += expectShared;
            const auto start = cells.empty() ? PathCell{0, 0} : cells.front().cell;
            const auto goal = cells.empty() ? PathCell{1, 1} : cells[cells.size()/2].cell;
            // Mutate/destroy/rebuild the coordinate-cache only. The terrain
            // input itself remains immutable throughout the active queue.
            cache.clear();
            auto otherCells = terrain(3, false);
            for (auto& cell : otherCells) cell.cell.x += 1000;
            PathRequestQueueTerrainLayoutCache otherCache;
            { PathRequestQueue rebuild(otherCells, cache); }
            cache.swap(otherCache);
            { PathRequestQueue rebuild(otherCells, cache); }
            if (!exercisePair(actual, expected, start, goal)) return false;
            return true;
        };
        auto dense = terrain(12, true);
        for (std::size_t i = 0; i < dense.size(); ++i) {
            dense[i].level = static_cast<int>(i % 3) - 1;
            dense[i].water = i % 7 == 0; dense[i].shore = i % 11 == 0;
            dense[i].bridge = i % 13 == 0; dense[i].ramp = i % 17 == 0;
            dense[i].cliff = i % 19 == 0; dense[i].resource = i % 23 == 0;
            dense[i].cost = 1 + static_cast<int>(i % 256); dense[i].hazardCost = static_cast<int>(i % 256);
        }
        if (!compare({}, false) || !compare(dense, true)) return false;
        auto reversed = dense; std::reverse(reversed.begin(), reversed.end());
        if (!compare(reversed, true)) return false;
        auto permuted = dense;
        for (std::size_t i = 0; i < dense.size(); ++i)
            permuted[i] = dense[(i * 73U + 19U) % dense.size()];
        if (!compare(permuted, true)) return false;
        std::vector<PathTerrainCell> sparse(3);
        sparse[0].cell = {0, 1}; sparse[1].cell = {1000, 0}; sparse[2].cell = {-2000000, 2000000};
        if (!compare(sparse, true)) return false;
        auto duplicate = dense; duplicate.push_back(dense[17]);
        duplicate.back().passable = false; duplicate.back().cost = 900;
        duplicate.back().level = 9; duplicate.back().hazardCost = 1800;
        if (!compare(duplicate, false)) return false;
        dense[0].cost = 0; dense[1].hazardCost = -1;
        if (!compare(dense, false)) return false;
    }

    RuntimePathTerrainViewReferenceScope permitTerrain(false);
    RuntimePathLayoutViewReferenceScope permitLayout(false);
    auto cells = terrain(32, false);
    std::optional<PathRequestQueue> survivor;
    {
        PathRequestQueueTerrainLayoutCache cache;
        { PathRequestQueue prime(cells, cache); }
        survivor.emplace(RuntimeReadOnlyPathTerrainTag{}, cells, cache);
        if (survivor->layoutIsSharedForDiagnostics() != !forcedOwned) return false;
        PathRequestQueueTerrainLayoutCache copied(cache), assigned;
        assigned = cache;
        if (copied.layoutIsSharedForDiagnostics() || assigned.layoutIsSharedForDiagnostics()) return false;
        PathRequestQueue independent(RuntimeReadOnlyPathTerrainTag{}, cells, copied);
        if (!forcedOwned && (!independent.layoutIsSharedForDiagnostics()
            || independent.sharedLayoutIdentityForDiagnostics() == survivor->sharedLayoutIdentityForDiagnostics()))
            return false;
    } // cached source and its independently materialized copies are destroyed
    {
        PathRequestQueue expected(cells);
        if (!exercisePair(*survivor, expected, {0, 0}, {29, 30})) return false;
    }
    std::vector<PathRequestQueue> copies;
    survivor->submit({0, 0}, {29, 30});
    copies.emplace_back(*survivor);
    PathRequestQueue assigned(std::vector<PathTerrainCell>{}); assigned = *survivor;
    copies.push_back(std::move(assigned));
    PathRequestQueue moved(std::move(*survivor)); survivor.reset();
    if (moved.layoutIsSharedForDiagnostics() != !forcedOwned) return false;
    PathRequestQueue moveAssigned(std::vector<PathTerrainCell>{}); moveAssigned = std::move(moved);
    if (moveAssigned.layoutIsSharedForDiagnostics() != !forcedOwned) return false;
    copies.emplace_back(moveAssigned);
    // Move-borrowed queues must end before the terrain input ends. Copies may
    // continue after both the cache and terrain source have been released.
    moveAssigned = PathRequestQueue(std::vector<PathTerrainCell>{});
    moved = PathRequestQueue(std::vector<PathTerrainCell>{});
    for (const auto& copy : copies)
        if (copy.layoutIsSharedForDiagnostics() || copy.terrainIsBorrowedForDiagnostics()) return false;
    std::vector<PathTerrainCell>().swap(cells);
    for (std::size_t i = 0; i < copies.size(); ++i) {
        copies[i].pumpWithBudget(32, 4096);
        if (i && !equal(copies[0], copies[i])) return false;
    }

    std::vector<PathTerrainCell> native;
    native.reserve(114960);
    for (int y = 1; y <= 480; ++y) for (int x = 1; x <= 480; ++x) {
        const int u = x-y+240, v = x+y-240;
        if (u < 0 || u >= 480 || v < 1 || v >= 480) continue;
        PathTerrainCell cell; cell.cell = {x, y}; cell.passable = (x+y)%19 != 0;
        native.push_back(cell);
    }
    if (native.size() != 114960) return false;
    PathRequestQueueTerrainLayoutCache cache;
    { PathRequestQueue prime(native, cache); }
    const void* identity = nullptr;
    for (int phase = 0; phase < 2; ++phase) {
        // Dynamic values change between completed scopes; the shared layout is
        // coordinate-only. Original full hash must see the new field values.
        if (phase) { native[1].level = 7; native[2].hazardCost = 129; native[3].water = true; }
        PathRequestQueue actual(RuntimeReadOnlyPathTerrainTag{}, native, cache);
        PathRequestQueue expected = [&] {
            RuntimePathProfileHashReferenceScope literal(true);
            return PathRequestQueue(native);
        }();
        if (actual.layoutIsSharedForDiagnostics() != !forcedOwned) return false;
        if (!forcedOwned) {
            ++shares;
            if (identity && actual.sharedLayoutIdentityForDiagnostics() != identity) return false;
            identity = actual.sharedLayoutIdentityForDiagnostics();
        }
        if (!exercisePair(actual, expected, native.front().cell,
                {native.front().cell.x + 8, native.front().cell.y + 8})) return false;
    }
    if (!forcedOwned && shares == 0) return false;
    std::cout << "shared layout complete oracle passed constructions=" << constructions
        << " positiveShares=" << shares << " completeResultChecks=" << resultChecks
        << " cacheClearSwapRebuild=yes cacheDestroyed=yes copyOwns=yes moveRetainsOwner=yes"
        << " reversedUnique=yes permutedUnique=yes nativeCells=" << native.size()
        << " oldGoldUnchanged=yes benchmarkPrepared=" << (benchmark ? "yes" : "no") << '\n';
    return true;
}
