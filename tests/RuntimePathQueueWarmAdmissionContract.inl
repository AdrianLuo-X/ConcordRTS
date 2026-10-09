bool warmAdmissionFusionChecks() {
    std::size_t checks = 0;
    const auto check = [&](const std::vector<PathTerrainCell>& cells,
            const std::vector<PathCell>& coordinates, const std::vector<int>& indices,
            bool reuseEligible, bool borrowEligible) {
        const auto a = runtimeCheckPathQueueWarmAdmission(cells, coordinates, indices,
            reuseEligible, borrowEligible, false);
        const auto b = runtimeCheckPathQueueWarmAdmission(cells, coordinates, indices,
            reuseEligible, borrowEligible, true);
        ++checks;
        return a.reuseLayout == b.reuseLayout && a.borrowTerrain == b.borrowTerrain;
    };
    std::vector<PathTerrainCell> cells(257);
    std::vector<PathCell> coordinates;
    std::vector<int> indices;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        cells[i].cell = {static_cast<int>(i) - 128, static_cast<int>(i % 17) - 8};
        cells[i].cost = 1 + static_cast<int>(i);
        cells[i].hazardCost = static_cast<int>(i % 5);
        coordinates.push_back(cells[i].cell); indices.push_back(static_cast<int>(i));
    }
    for (bool reuse : {false, true}) for (bool borrow : {false, true}) {
        if (!check(cells, coordinates, indices, reuse, borrow)) return false;
        for (std::size_t position : {std::size_t{0}, std::size_t{128}, std::size_t{256}}) {
            for (int field = 0; field < 5; ++field) {
                auto changed = cells; auto changedIndices = indices;
                if (field == 0) ++changed[position].cell.x;
                if (field == 1) --changed[position].cell.y;
                if (field == 2) changedIndices[position] = -1;
                if (field == 3) changed[position].cost = 0;
                if (field == 4) changed[position].hazardCost = -1;
                if (!check(changed, coordinates, changedIndices, reuse, borrow)) return false;
            }
        }
        // Borrow rejects at the first row; coordinate mismatch is much later.
        auto changed = cells; changed.front().cost = 0; ++changed.back().cell.x;
        if (!check(changed, coordinates, indices, reuse, borrow)) return false;
        changed = cells; changed.front().hazardCost = -1; --changed.back().cell.y;
        if (!check(changed, coordinates, indices, reuse, borrow)) return false;
    }
    if (!check({}, {}, {}, false, false) || !check({}, {}, {}, true, false)) return false;
    // Rejected vector-shape gates must not touch absent coordinate/index rows.
    if (!check(cells, {}, {}, false, false) || !check(cells, coordinates, {}, true, false)) return false;
    if (checks != 76) return false;
    std::cout << "Path queue warm admission contract: equivalenceChecks=" << checks
        << " independentLayoutGate=yes completeCoordinates=yes originalTwoPassOracle=yes\n";
    return true;
}
