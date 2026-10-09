// Frozen pre-optimization search: square scans and a fresh occupancy snapshot
// per candidate. Used only by the harvester smoke to check ranking/reachability.
std::optional<RuntimeCellPoint> referenceRuntimeOreSearch(
    const RuntimeWindowState& state,
    const WorldObjectInstance& harvester,
    std::optional<RuntimeCellPoint> scanOrigin,
    bool farScan
) {
    const RuntimeCellPoint from = runtimeCellPointForObject(harvester);
    const RuntimeCellPoint origin =
        (scanOrigin.has_value() && scanOrigin->x >= 0 && scanOrigin->y >= 0)
            ? *scanOrigin
            : from;
    const int maxRadius =
        std::max(1, farScan
            ? state.rules.getTiberiumFarScan()
            : state.rules.getTiberiumNearScan());
    const int startRadius = farScan ? 1 : 0;

    struct OreCandidate {
        RuntimeCellPoint cell;
        int value = 0;
        int priority = 0;
        long long distanceScore = 0;
    };

    const auto candidateIfHarvestable =
        [&](const RuntimeCellPoint& cell) -> std::optional<OreCandidate> {
            const TerrainCell* terrain = findRuntimeTerrainCellAt(state, cell);
            if (terrain == nullptr
                || !terrain->hasOverlay
                || (!terrain->overlayIsOre && !terrain->overlayIsGem)) {
                return std::nullopt;
            }

            const long long dxFromHarvester =
                static_cast<long long>(cell.x - from.x);
            const long long dyFromHarvester =
                static_cast<long long>(cell.y - from.y);
            int priority = 4;
            const int dxFromOrigin = cell.x - origin.x;
            const int dyFromOrigin = cell.y - origin.y;
            if (std::abs(dxFromOrigin) <= 1
                && std::abs(dyFromOrigin) <= 1) {
                constexpr int priorityMatrix[3][3] = {
                    {8, 5, 6},
                    {3, 0, 2},
                    {7, 4, 1},
                };
                priority = priorityMatrix[dyFromOrigin + 1][dxFromOrigin + 1];
            }

            return OreCandidate{
                cell,
                runtimeResourceValueForCell(state, *terrain),
                priority,
                (dxFromHarvester * dxFromHarvester)
                    + (dyFromHarvester * dyFromHarvester)
            };
        };

    const auto reachable = [&](const RuntimeCellPoint& cell) {
        return findRuntimeHarvesterResourceApproach(state, harvester, cell).has_value();
    };

    if (const std::optional<OreCandidate> originCandidate =
        candidateIfHarvestable(origin)) {
        if (reachable(originCandidate->cell)) {
            return originCandidate->cell;
        }
    }

    for (int radius = startRadius; radius <= maxRadius; ++radius) {
        std::vector<OreCandidate> candidates;
        candidates.reserve(static_cast<std::size_t>(std::max(8, radius * 8)));

        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius == 0) {
                    continue;
                }
                if (std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }

                const RuntimeCellPoint candidateCell{
                    origin.x + dx,
                    origin.y + dy
                };
                if (const std::optional<OreCandidate> candidate =
                    candidateIfHarvestable(candidateCell)) {
                    candidates.push_back(*candidate);
                }
            }
        }

        if (candidates.empty()) {
            continue;
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [farScan](const OreCandidate& left, const OreCandidate& right) {
                if (!farScan && left.value != right.value) {
                    return left.value > right.value;
                }
                if (left.priority != right.priority) {
                    return left.priority < right.priority;
                }
                if (left.distanceScore != right.distanceScore) {
                    return left.distanceScore < right.distanceScore;
                }
                return RuntimeCellPointLess{}(left.cell, right.cell);
            }
        );

        for (const OreCandidate& candidate : candidates) {
            if (reachable(candidate.cell)) {
                return candidate.cell;
            }
        }
    }
    return std::nullopt;
}

bool checkRuntimeOreSearchContract(const RuntimeWindowState& state,
                                   const WorldObjectInstance& miner,
                                   std::string& error) {
    int checked = 0;
    for (const RuntimeCellPoint origin : {
            RuntimeCellPoint{-1, -1}, RuntimeCellPoint{0, 0},
            RuntimeCellPoint{5, 5}, RuntimeCellPoint{6, 6},
            RuntimeCellPoint{9, 9}, RuntimeCellPoint{12, 12}}) {
        for (bool farScan : {false, true}) {
            const auto expected = referenceRuntimeOreSearch(state, miner, origin, farScan);
            const auto actual = findNearestRuntimeOreCell(state, miner, origin, farScan);
            if (expected != actual) {
                error = "ore search changed target near " + std::to_string(origin.x)
                    + "," + std::to_string(origin.y);
                return false;
            }
            ++checked;
        }
    }
    std::cout << "Ore search contract: queries=" << checked << " targets=identical\n";
    return true;
}
