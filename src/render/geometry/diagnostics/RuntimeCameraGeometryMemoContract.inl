// Include after RuntimeCombinedCameraBoundsContract.inl and the actual camera
// helpers. Mutations are confined to a heap-owned copy of the loaded terrain.
bool runRuntimeCameraGeometryMemoActualContract(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "camera geometry memo actual: " + reason;
        return false;
    };
    if (!live.world.getSummary().loaded
        || live.world.getTerrainCells().size() < RuntimeCameraGeometryMemo::minimumEntries
        || live.world.getTerrainCells().size() >= RuntimeCameraGeometryMemo::maximumEntries)
        return fail("actual loaded terrain must contain 4096..262143 cells");
    const int entryMemoReference = runtimeCameraGeometryMemoReferenceOverride;
    const int entryBoundsReference = runtimeCameraBoundsPassReferenceOverride;
    const std::uint64_t entryWorldHash = live.world.deterministicSimulationStateHash();
    const auto saveLive = [&](std::string& bytes) {
        RuntimeSnapshotActiveStateScope active(live);
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(live, saved, error)) return false;
        bytes = saved.str();
        if (bytes.empty()) return fail("complete authoritative writer returned empty bytes");
        return true;
    };
    std::string initialSave, finalSave;
    if (!saveLive(initialSave)) return false;
    std::size_t exactPairs = 0, coldMisses = 0, hotHits = 0;
    std::size_t geometryMisses = 0, projectionMisses = 0, dimensionMisses = 0;
    std::size_t irrelevantHits = 0, copyMoveChecks = 0, referenceChecks = 0;
    const auto run = [&]() -> bool {
        RuntimeCameraGeometryMemoReferenceScope memoEnabled(false);
        RuntimeCameraBoundsPassReferenceScope combinedEnabled(false);
        // The original 5824 actual clamp/reference pairs now exercise the
        // integrated wrapper, including viewport/offset and reveal behavior.
        if (!runRuntimeCombinedCameraBoundsContract(live, error)) return false;
        auto storage = std::make_unique<RuntimeWindowState>();
        RuntimeWindowState& probe = *storage;
        probe.world = live.world;
        probe.report.map = live.report.map;
        probe.camera = live.camera;
        probe.rules = live.rules;
        probe.art = live.art;
        probe.rulesLoaded = live.rulesLoaded;
        probe.artLoaded = live.artLoaded;
        probe.camera.tileWidth = 60;
        probe.camera.tileHeight = 30;
        probe.radarProjectionCacheValid = false;
        const auto sameBounds = [](const RuntimeCameraWorldBounds& a, const RuntimeCameraWorldBounds& b) {
            return a.left == b.left && a.top == b.top && a.right == b.right
                && a.bottom == b.bottom && a.valid == b.valid;
        };
        const auto projectionKey = [](const RuntimeRadarProjection& p) {
            return RuntimeCameraGeometryProjection{p.valid,p.minX,p.minY,p.maxX,p.maxY,p.fullMapWidth};
        };
        const auto memoEnvelopes = [](const RuntimeCombinedCameraBounds& value) {
            const auto convert = [](const RuntimeCameraWorldBounds& b) {
                return RuntimeCameraGeometryBounds{b.left,b.top,b.right,b.bottom,b.valid};
            };
            return RuntimeCameraGeometryEnvelopes{convert(value.playable),convert(value.full)};
        };
        const auto compare = [&](const char* label, bool expectedHit) {
            // Use the actual builder's returned six fields. Its existing cache
            // semantics are deliberately preserved by both the oracle and hit.
            const auto projection = buildRuntimeRadarProjection(probe);
            const auto expected = runtimeCombinedCameraWorldBounds(probe, &projection);
            const auto beforeHits = probe.cameraGeometryMemo.hits();
            const auto beforeStores = probe.cameraGeometryMemo.stores();
            const auto actual = runtimeMemoizedCameraWorldBounds(probe);
            if (!sameBounds(expected.playable,actual.playable) || !sameBounds(expected.full,actual.full))
                return fail(std::string(label) + ": original ten envelope fields differ");
            const auto hitDelta = probe.cameraGeometryMemo.hits() - beforeHits;
            const auto storeDelta = probe.cameraGeometryMemo.stores() - beforeStores;
            const auto count = probe.world.getTerrainCells().size();
            const bool eligible = !runtimeCameraGeometryMemoReference()
                && count >= RuntimeCameraGeometryMemo::minimumEntries
                && count <= RuntimeCameraGeometryMemo::maximumEntries;
            if (hitDelta != (expectedHit ? 1U : 0U)
                || storeDelta != ((!expectedHit && eligible) ? 1U : 0U))
                return fail(std::string(label) + ": actual hit/store counter witness differs");
            ++exactPairs;
            if (expectedHit) ++hotHits;
            else ++coldMisses;
            return true;
        };
        const auto missThenHit = [&](const char* label) {
            return compare(label,false) && compare(label,true);
        };
        if (!missThenHit("actual loaded cold/hot")) return false;
        auto& cells = probe.world.accessTerrainCells();
        const auto* originalAddress = cells.data();
        if (cells.front().x > 1024 || cells.front().x < -1024
            || cells.front().y > 1024 || cells.front().y < -1024
            || cells.front().level > 1024 || cells.front().level < -1024)
            return fail("actual fixture exceeds the bounded mutation arithmetic domain");
        cells.front().x += 7;
        if (cells.data() != originalAddress || !missThenHit("same-address x mutation")) return false;
        ++geometryMisses;
        cells.front().y += 9;
        if (cells.data() != originalAddress || !missThenHit("same-address y mutation")) return false;
        ++geometryMisses;
        cells.front().level += 3;
        if (cells.data() != originalAddress || !missThenHit("same-address level mutation")) return false;
        ++geometryMisses;
        if (cells.front().x == cells.back().x && cells.front().y == cells.back().y
            && cells.front().level == cells.back().level)
            return fail("ordered mutation fixture has equal endpoints");
        std::reverse(cells.begin(),cells.end());
        if (cells.data() != originalAddress || !missThenHit("same-address ordered reversal")) return false;
        ++geometryMisses;
        cells.push_back(cells.front());
        if (!missThenHit("terrain count addition")) return false;
        ++geometryMisses;
        cells.pop_back();
        if (!missThenHit("terrain count removal")) return false;
        ++geometryMisses;
        const auto* beforeReplaceAddress = cells.data();
        auto replacement = cells;
        replacement.back().level += 4;
        cells = std::move(replacement);
        if (cells.data() == beforeReplaceAddress || !missThenHit("same-count owning replacement"))
            return fail("same-count owning replacement did not replace storage or match oracle");
        ++geometryMisses;
        const auto refilled = cells;
        cells.clear();
        if (!compare("clear after populated cache",false)) return false;
        cells = refilled;
        cells.back().x += 2;
        if (!missThenHit("refill former count after clear")) return false;
        ++geometryMisses;

        // These real TerrainCell fields are not mathematical envelope inputs.
        ++cells.front().tileIndex;
        if (!compare("irrelevant tile index",true)) return false;
        ++irrelevantHits;
        cells.front().hasOverlay = !cells.front().hasOverlay;
        cells.front().overlayType = "CameraGeometryMemoContract";
        if (!compare("irrelevant overlay",true)) return false;
        ++irrelevantHits;
        ++cells.front().iceGrowth;
        if (!compare("irrelevant ice state",true)) return false;
        ++irrelevantHits;

        const auto savedMap = probe.report.map;
        const auto validProjection = buildRuntimeRadarProjection(probe);
        if (!validProjection.valid || !probe.radarProjectionCacheValid)
            return fail("actual loaded map must return a valid cached radar projection");
        probe.report.map.size = "invalid";
        probe.report.map.localSize = "invalid";
        probe.report.map.width = 0;
        probe.radarProjectionCacheValid = false;
        if (buildRuntimeRadarProjection(probe).valid)
            return fail("actual invalid-projection fixture unexpectedly valid");
        if (!missThenHit("actual returned projection valid=false")) return false;
        ++projectionMisses;
        probe.report.map = savedMap;
        probe.radarProjectionCacheValid = false;
        if (!missThenHit("actual returned projection valid=true restored")) return false;
        ++projectionMisses;
        const std::array<int RuntimeWindowState::*,5> projectionFields{{
            &RuntimeWindowState::radarProjectionMinX, &RuntimeWindowState::radarProjectionMinY,
            &RuntimeWindowState::radarProjectionMaxX, &RuntimeWindowState::radarProjectionMaxY,
            &RuntimeWindowState::radarProjectionFullMapWidth
        }};
        for (const auto field : projectionFields) {
            const int prior = probe.*field;
            if (prior > 1024 || prior < -1024) return fail("projection cache fixture outside bounded domain");
            probe.*field = prior + 3;
            // Header/count remain unchanged; the real builder must read this
            // exact cached scalar, rather than reconstructing a new projection.
            const auto actualProjection = buildRuntimeRadarProjection(probe);
            const RuntimeCameraGeometryProjection changed = projectionKey(actualProjection);
            if (changed == projectionKey(validProjection))
                return fail("actual projection cache scalar mutation not observed");
            if (!missThenHit("actual returned projection cache scalar changed")) return false;
            ++projectionMisses;
            probe.*field = prior;
            if (!missThenHit("actual returned projection cache scalar restored")) return false;
            ++projectionMisses;
        }
        const std::array<std::pair<int,int>,6> dimensions{{
            {61,29},{1,1},{0,0},{-3,-3},{67,59},{60,30}
        }};
        for (const auto& [width,height] : dimensions) {
            probe.camera.tileWidth = width;
            probe.camera.tileHeight = height;
            if (!missThenHit("raw tile dimensions changed")) return false;
            ++dimensionMisses;
        }

        const auto projection = buildRuntimeRadarProjection(probe);
        const auto key = projectionKey(projection);
        const auto expected = memoEnvelopes(runtimeCombinedCameraWorldBounds(probe,&projection));
        const auto checkOwnedHit = [&](RuntimeCameraGeometryMemo& memo, const char* label) {
            const auto priorHits = memo.hits();
            const auto result = memo.find(cells,probe.camera.tileWidth,probe.camera.tileHeight,key);
            if (!result || *result != expected || memo.hits() != priorHits + 1)
                return fail(std::string(label) + ": owned copy/move full envelope or hit differs");
            ++copyMoveChecks;
            return true;
        };
        auto copied = probe.cameraGeometryMemo;
        if (!checkOwnedHit(copied,"copy construction")) return false;
        RuntimeCameraGeometryMemo copyAssigned;
        copyAssigned = copied;
        if (!checkOwnedHit(copyAssigned,"copy assignment")) return false;
        const int originalX = cells.front().x;
        ++cells.front().x;
        const auto copyHits = copied.hits();
        if (copied.find(cells,probe.camera.tileWidth,probe.camera.tileHeight,key)
            || copied.hits() != copyHits)
            return fail("copy borrowed same-address source geometry instead of owning keys");
        ++copyMoveChecks;
        cells.front().x = originalX;
        if (!checkOwnedHit(copied,"copy survives source geometry restoration")) return false;
        auto moved = std::move(copied);
        const std::vector<TerrainCell> empty;
        if (copied.find(empty,probe.camera.tileWidth,probe.camera.tileHeight,key))
            return fail("move-construction source returned populated envelope for empty cells");
        ++copyMoveChecks;
        if (!checkOwnedHit(moved,"move construction target")) return false;
        RuntimeCameraGeometryMemo moveAssigned;
        moveAssigned = std::move(moved);
        if (moved.find(empty,probe.camera.tileWidth,probe.camera.tileHeight,key))
            return fail("move-assignment source returned populated envelope for empty cells");
        ++copyMoveChecks;
        if (!checkOwnedHit(moveAssigned,"move assignment target")) return false;
        probe.cameraGeometryMemo = moveAssigned;
        if (!compare("copied/moved payload through actual wrapper",true)) return false;
        ++copyMoveChecks;
        {
            RuntimeCameraGeometryMemoReferenceScope reference(true);
            if (!compare("reference bypasses find/store",false)) return false;
            ++referenceChecks;
            {
                RuntimeCameraGeometryMemoReferenceScope nested(false);
                if (!compare("nested optimized uses actual hot payload",true)) return false;
                ++referenceChecks;
            }
            if (!runtimeCameraGeometryMemoReference()
                || !compare("nested scope restores outer reference",false)) return false;
            ++referenceChecks;
        }
        if (runtimeCameraGeometryMemoReference()
            || !compare("outer scope restores optimized hot payload",true)) return false;
        ++referenceChecks;
        probe.cameraGeometryMemo.invalidate();
        if (!missThenHit("explicit invalidation")) return false;
        if (geometryMisses != 8 || projectionMisses != 12 || dimensionMisses != 6
            || irrelevantHits != 3 || copyMoveChecks != 9 || referenceChecks != 4
            || exactPairs != 65 || coldMisses != 31 || hotHits != 34)
            return fail("actual fixture/lookup completion counts differ");
        return true;
    };
    if (!run()) return false;
    if (runtimeCameraGeometryMemoReferenceOverride != entryMemoReference
        || runtimeCameraBoundsPassReferenceOverride != entryBoundsReference)
        return fail("reference scope leaked TLS");
    if (!saveLive(finalSave)) return false;
    if (initialSave != finalSave || live.world.deterministicSimulationStateHash() != entryWorldHash)
        return fail("caller full authoritative save bytes or complete World hash changed");
    std::cout << "Camera geometry memo actual: actualLoadedTerrain=yes combinedClampPairs=5824 "
        << "envelopePairs=" << exactPairs << " envelopeFields=10 coldMisses=" << coldMisses
        << " hotHits=" << hotHits << " geometryMisses=" << geometryMisses
        << " projectionMisses=" << projectionMisses << " dimensionMisses=" << dimensionMisses
        << " irrelevantHits=" << irrelevantHits << " copyMoveChecks=" << copyMoveChecks
        << " referenceChecks=" << referenceChecks
        << " fullSave/WorldHash/TLS=exact passed=yes\n";
    error.clear();
    return true;
}
