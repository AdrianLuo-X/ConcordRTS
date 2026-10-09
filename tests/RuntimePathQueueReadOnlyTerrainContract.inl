bool readOnlyTerrainViewChecks(bool benchmark) {
    struct OriginalC27TerrainNodeShape {
        PathCell cell; int level; bool passable,water,shore,bridge,ramp,cliff; int cost,hazardCost;
    };
    static_assert(sizeof(OriginalC27TerrainNodeShape)==sizeof(PathTerrainCell));
    static_assert(alignof(OriginalC27TerrainNodeShape)==alignof(PathTerrainCell));
    static_assert(!std::is_constructible_v<PathRequestQueue, RuntimeReadOnlyPathTerrainTag,
        std::vector<PathTerrainCell>&&, PathRequestQueueTerrainLayoutCache&>);
    static_assert(!std::is_constructible_v<PathRequestQueue, RuntimeReadOnlyPathTerrainTag,
        const std::vector<PathTerrainCell>&&, PathRequestQueueTerrainLayoutCache&>);
    const auto envReference = [](const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    };
    const bool forcedOwned = envReference("CONCORD_MOVEMENT_REFERENCE")
        || envReference("CONCORD_PATH_QUEUE_LAYOUT_REFERENCE");
    const int priorOverride = runtimePathTerrainViewReferenceOverride;
    {
        RuntimePathTerrainViewReferenceScope outer(false);
        if (runtimePathTerrainViewReference()) return false;
        { RuntimePathTerrainViewReferenceScope inner(true);
          if (!runtimePathTerrainViewReference()) return false; }
        if (runtimePathTerrainViewReference()) return false;
    }
    if (runtimePathTerrainViewReferenceOverride != priorOverride) return false;
    std::size_t constructions = 0, positiveBorrows = 0, resultChecks = 0;
    const auto sameQueues = [&](const PathRequestQueue& a, const PathRequestQueue& b) {
        if (!sameCompleteStats(a.liveStats(), b.liveStats())
            || a.getResults().size() != b.getResults().size()) return false;
        for (std::size_t i = 0; i < a.getResults().size(); ++i) {
            if (!sameCompleteResult(a.getResults()[i], b.getResults()[i])) return false;
            ++resultChecks;
        }
        return true;
    };
    const auto exercise = [&](PathRequestQueue& a, PathRequestQueue& b,
            PathCell start, PathCell goal, bool simple) {
        if (!sameQueues(a, b)) return false;
        for (int i = 0; i < (simple ? 2 : 12); ++i) {
            PathApproachContext context;
            context.movementMode = simple ? PathMovementMode::Air
                : static_cast<PathMovementMode>(i % 4);
            context.requesterStableHash = i / 2 + 1;
            context.movementConstraintHash = i / 2 + 1;
            context.noPathCooldownTicks = 3;
            context.goalSlotCount = simple ? 0 : 2;
            context.shareAcrossGoalSlots = true;
            context.mapQueryApproachAvailable = !simple && i % 3 == 0;
            if (context.mapQueryApproachAvailable) context.approachCells = {goal, start};
            if (!simple) context.alternateGoalCells = {goal};
            if (a.submit(start, goal, context).id != b.submit(start, goal, context).id
                || !sameQueues(a, b)) return false;
            if (i % 2 && (a.pumpWithBudget(2, i % 4 == 1 ? 1 : 256)
                    != b.pumpWithBudget(2, i % 4 == 1 ? 1 : 256)
                || !sameQueues(a, b))) return false;
        }
        return a.pumpWithBudget(32, 4096) == b.pumpWithBudget(32, 4096)
            && !a.getResults().empty() && sameQueues(a, b);
    };
    for (bool layoutRef : {false, true}) for (bool lookupRef : {false, true})
      for (bool hashRef : {false, true}) for (bool sparseRef : {false, true})
       for (bool zeroRef : {false, true}) for (bool viewRef : {false, true}) {
        RuntimePathProfileHashReferenceScope hashScope(hashRef);
        RuntimePathProfileSparseBytesReferenceScope sparseScope(sparseRef);
        RuntimePathProfileZeroRunsReferenceScope zeroScope(zeroRef);
        RuntimePathTerrainViewReferenceScope viewScope(viewRef);
        const auto compare = [&](std::vector<PathTerrainCell> cells,
                bool uniqueNormalized, bool simple = false) {
            PathRequestQueueTerrainLayoutCache layout;
            { PathRequestQueue prime(cells, layout, layoutRef, lookupRef);
              std::vector<PathTerrainCell> originalReserve; originalReserve.reserve(cells.size());
              if (prime.terrainIsBorrowedForDiagnostics()
                  || prime.ownedTerrainBytesForDiagnostics()!=originalReserve.capacity()*sizeof(PathTerrainCell)) return false; }
            PathRequestQueue actual(RuntimeReadOnlyPathTerrainTag{}, cells, layout, layoutRef, lookupRef);
            PathRequestQueue expected = [&] {
                RuntimePathProfileHashReferenceScope original(true);
                PathRequestQueueTerrainLayoutCache fresh;
                return PathRequestQueue(cells, fresh, true, lookupRef);
            }();
            constructions += 3;
            const bool expectBorrow = uniqueNormalized && !cells.empty() && !viewRef
                && !layoutRef && !lookupRef && !forcedOwned
                && cells.size() <= PathRequestQueueTerrainLayoutCache::maxRetainedCells;
            if (actual.terrainIsBorrowedForDiagnostics() != expectBorrow) return false;
            if (expectBorrow && actual.ownedTerrainBytesForDiagnostics()!=0) return false;
            positiveBorrows += expectBorrow;
            const auto inputCopy = cells;
            const PathCell start = cells.empty() ? PathCell{0,0} : cells.front().cell;
            const PathCell goal = simple ? start : cells.empty() ? PathCell{1,1} : cells[cells.size()/2].cell;
            // Switching this reference after construction must not alter an
            // already admitted view. Both queues read the immutable live input.
            RuntimePathTerrainViewReferenceScope later(!viewRef);
            if (!exercise(actual, expected, start, goal, simple)) return false;
            for (std::size_t i = 0; i < cells.size(); ++i) {
                const auto& a = cells[i]; const auto& b = inputCopy[i];
                if (std::tie(a.cell.x,a.cell.y,a.level,a.passable,a.water,a.shore,a.bridge,
                        a.ramp,a.cliff,a.resource,a.cost,a.hazardCost)
                    != std::tie(b.cell.x,b.cell.y,b.level,b.passable,b.water,b.shore,b.bridge,
                        b.ramp,b.cliff,b.resource,b.cost,b.hazardCost)) return false;
            }
            return true;
        };
        auto mixed = terrain(16, false);
        for (std::size_t i = 0; i < mixed.size(); ++i) {
            mixed[i].level = static_cast<int>(i % 3) - 1;
            mixed[i].water = i % 7 == 0; mixed[i].shore = i % 11 == 0;
            mixed[i].bridge = i % 13 == 0; mixed[i].ramp = i % 17 == 0;
            mixed[i].cliff = i % 19 == 0; mixed[i].resource = i % 23 == 0;
            mixed[i].cost = 1 + static_cast<int>(i % 256);
            mixed[i].hazardCost = static_cast<int>(i % 256);
        }
        if (!compare({}, false) || !compare(mixed, true)) return false;
        // Original nodes keep first-authored order: only terrainIndex_ is
        // sorted. A warm reversed/permuted unique input must borrow that exact
        // order and preserve all search indices, results and live statistics.
        auto reversedUnique = mixed;
        std::reverse(reversedUnique.begin(), reversedUnique.end());
        if (!compare(reversedUnique, true)) return false;
        auto permutedUnique = mixed;
        for (std::size_t i = 0; i < mixed.size(); ++i)
            permutedUnique[i] = mixed[(i * 73U + 19U) % mixed.size()];
        if (!compare(permutedUnique, true)) return false;
        for (int field = 0; field < 12; ++field) {
            auto changed = mixed;
            switch (field) {
            case 0: changed[0].cell.x -= 1; break;
            case 1: changed[0].level = std::numeric_limits<int>::min(); break;
            case 2: changed[0].passable = !changed[0].passable; break;
            case 3: changed[0].water = !changed[0].water; break;
            case 4: changed[0].shore = !changed[0].shore; break;
            case 5: changed[0].bridge = !changed[0].bridge; break;
            case 6: changed[0].ramp = !changed[0].ramp; break;
            case 7: changed[0].cliff = !changed[0].cliff; break;
            case 8: changed[0].resource = !changed[0].resource; break;
            case 9: changed[0].cost = 0; break;
            case 10: changed[0].hazardCost = -1; break;
            case 11: changed[0].cost = changed[0].hazardCost = std::numeric_limits<int>::max(); break;
            }
            if (!compare(changed, field != 9 && field != 10, field == 1 || field == 11)) return false;
        }
        auto duplicate = mixed[17];
        duplicate.level = 9; duplicate.passable = false; duplicate.cost = 900;
        duplicate.hazardCost = 1800; mixed.push_back(duplicate);
        if (!compare(mixed, false)) return false;
        std::reverse(mixed.begin(), mixed.end());
        if (!compare(mixed, false)) return false;
        std::vector<PathTerrainCell> sparse(3);
        sparse[0].cell = {0,1}; sparse[1].cell = {1000,0}; sparse[2].cell = {-2000000,2000000};
        if (!compare(sparse, true, true)) return false;
    }
    // Copy and assignment must own data before the original view/source ends.
    RuntimePathTerrainViewReferenceScope permit(false);
    auto cells = terrain(32, false);
    PathRequestQueueTerrainLayoutCache layout;
    { PathRequestQueue prime(cells, layout); }
    std::vector<PathRequestQueue> copies;
    {
        PathRequestQueue view(RuntimeReadOnlyPathTerrainTag{}, cells, layout);
        PathRequestQueue reference(cells);
        view.submit({0,0},{29,30}); reference.submit({0,0},{29,30});
        copies.emplace_back(view); copies.emplace_back(reference);
        PathRequestQueue assigned(std::vector<PathTerrainCell>{}); assigned = view;
        copies.push_back(std::move(assigned));
        PathRequestQueue moved(std::move(view));
        if (moved.terrainIsBorrowedForDiagnostics() != !forcedOwned) return false;
        PathRequestQueue moveAssigned(std::vector<PathTerrainCell>{}); moveAssigned = std::move(moved);
        if (moveAssigned.terrainIsBorrowedForDiagnostics() != !forcedOwned) return false;
        copies.emplace_back(moveAssigned);
    }
    for (const auto& copy : copies) if (copy.terrainIsBorrowedForDiagnostics()) return false;
    for (auto& cell : cells) { cell.cost = -200; cell.hazardCost = -1; cell.passable = false; }
    std::vector<PathTerrainCell>().swap(cells);
    for (std::size_t i = 0; i < copies.size(); ++i) {
        copies[i].pumpWithBudget(2, 4096);
        if (i > 0 && !sameQueues(copies[0], copies[i])) return false;
    }
    // Same-count coordinate/ordering changes must fail old layout identity;
    // in-place metadata changes are legal only between completed view scopes.
    auto changed = terrain(8, false);
    { PathRequestQueue prime(changed, layout); }
    changed[0].cell.x = -1;
    { PathRequestQueue queue(RuntimeReadOnlyPathTerrainTag{}, changed, layout);
      if (queue.terrainIsBorrowedForDiagnostics() || layout.lastConstructionReused()) return false; }
    std::reverse(changed.begin(), changed.end());
    { PathRequestQueue queue(RuntimeReadOnlyPathTerrainTag{}, changed, layout);
      if (queue.terrainIsBorrowedForDiagnostics() || layout.lastConstructionReused()) return false; }
    changed[0].cost = 999; changed[0].hazardCost = 42; changed[0].passable = false;
    {
        PathRequestQueue actual(RuntimeReadOnlyPathTerrainTag{}, changed, layout);
        PathRequestQueue expected(changed);
        if (!exercise(actual, expected, changed.front().cell, changed.back().cell, false)) return false;
    }
    // Independent simultaneous sources with identical coordinates are not a
    // cross-queue value cache: each view uses its own full metadata and hash.
    auto foreign = changed;
    foreign[1].passable = !foreign[1].passable;
    foreign[1].cost = 145; foreign[1].hazardCost = 255;
    {
        PathRequestQueue a(RuntimeReadOnlyPathTerrainTag{}, changed, layout);
        PathRequestQueue b(RuntimeReadOnlyPathTerrainTag{}, foreign, layout);
        PathRequestQueue aOwned(changed), bOwned(foreign);
        if (!exercise(a,aOwned,changed.front().cell,changed.back().cell,false)
            || !exercise(b,bOwned,foreign.front().cell,foreign.back().cell,false)) return false;
    }
    // A borrowed storage can never expose a mutable node or silently detach
    // on a read; the three explicit writes reject before touching the source.
    {
        RuntimePathQueueTerrainStorage<PathTerrainCell> storage;
        storage.borrow(changed);
        int refused = 0;
        try { storage.reserve(100); } catch (const std::logic_error&) { ++refused; }
        try { storage.push_back(changed[0]); } catch (const std::logic_error&) { ++refused; }
        try { storage.mutableOwnedAt(0).cost = -1; } catch (const std::logic_error&) { ++refused; }
        if (refused != 3 || !storage.isBorrowed() || storage.data() != changed.data()
            || storage[0].cost != changed[0].cost) return false;
    }
    std::vector<PathTerrainCell> oversized(PathRequestQueueTerrainLayoutCache::maxRetainedCells+1);
    for (std::size_t i=0;i<oversized.size();++i) oversized[i].cell={static_cast<int>(i),0};
    {
        PathRequestQueue actual(RuntimeReadOnlyPathTerrainTag{},oversized,layout);
        PathRequestQueue expected(oversized);
        if (actual.terrainIsBorrowedForDiagnostics() || layout.lastConstructionReused()
            || layout.retainedCellCount()!=0
            || !exercise(actual,expected,oversized.front().cell,oversized.front().cell,true)) return false;
    }
    if (!forcedOwned && positiveBorrows == 0) return false;
    std::vector<PathTerrainCell> native;
    native.reserve(114960);
    for (int y = 1; y <= 480; ++y) for (int x = 1; x <= 480; ++x) {
        const int u = x-y+240, v = x+y-240;
        if (u < 0 || u >= 480 || v < 1 || v >= 480) continue;
        PathTerrainCell cell; cell.cell = {x,y}; cell.passable = (x+y)%19 != 0;
        native.push_back(cell);
    }
    if (native.size() != 114960) return false;
    PathRequestQueueTerrainLayoutCache layouts[2];
    for (auto& cache : layouts) { PathRequestQueue prime(native, cache); }
    {
        PathRequestQueue actual(RuntimeReadOnlyPathTerrainTag{},native,layouts[1]);
        PathRequestQueue expected(native,layouts[0]);
        if (actual.terrainIsBorrowedForDiagnostics()!=!forcedOwned
            || !exercise(actual,expected,native.front().cell,
                {native.front().cell.x+8,native.front().cell.y+8},false)) return false;
    }
    std::cout << "readonly terrain complete oracle passed constructions=" << constructions
        << " positiveBorrows=" << positiveBorrows << " completeResultChecks=" << resultChecks
        << " ordinaryCtorOwned=yes copyOwns=yes temporaryRejected=yes reversedUnique=yes permutedUnique=yes nativeCells=" << native.size() << '\n';
    if (!benchmark) return true;
    std::string profile;
    for (int run = 0; run < 6; ++run) {
        const bool borrowed = run == 1 || run == 2 || run == 4;
        double elapsed = 0.0;
        std::size_t retainedNodeBytes = 0;
        for (int repeat = 0; repeat < 2; ++repeat) {
            const auto start = std::chrono::steady_clock::now();
            PathRequestQueue queue = borrowed
                ? PathRequestQueue(RuntimeReadOnlyPathTerrainTag{}, native, layouts[1])
                : PathRequestQueue(native, layouts[0]);
            elapsed += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if (queue.terrainIsBorrowedForDiagnostics() != (borrowed && !forcedOwned)) return false;
            retainedNodeBytes = queue.ownedTerrainBytesForDiagnostics();
            if (borrowed && !forcedOwned && retainedNodeBytes!=0) return false;
            PathApproachContext context; context.movementMode = PathMovementMode::Air;
            queue.submit(native.front().cell,native.front().cell,context); queue.pump(1);
            if (queue.getResults().size() != 1 || !queue.getResults()[0].completed) return false;
            if (profile.empty()) profile = queue.getResults()[0].passabilityProfileHash;
            if (queue.getResults()[0].passabilityProfileHash != profile) return false;
        }
        std::cout << "readonly terrain constructor microbench route=" << (borrowed?"borrowed":"owned")
            << " cells=" << native.size() << " rounds=2 ms=" << elapsed << " profile=" << profile
            << " retainedNodeBytes=" << retainedNodeBytes
            << " coldPrimeExcluded=yes completeHash=yes gameFpsInferred=no\n";
    }
    return true;
}
