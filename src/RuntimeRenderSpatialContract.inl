// Freeze the previous depot traversal here as an independent ordering oracle.
// Keep it out of the paint path: this only runs in the renderer smoke contract.
static int applyRuntimeRepairDepotForegroundOrderReference(
    std::vector<RuntimeWorldObjectDrawRecord>& records,
    const RuntimeMapRenderContext& context
) {
    if (!context.rules || !context.art) return 0;
    std::vector<const RuntimeWorldObjectDrawRecord*> depots;
    for (const auto& record : records) {
        if (!record.object || record.object->kind != WorldObjectKind::Structure) continue;
        const auto* building = context.rules->findBuildingType(record.object->type);
        const auto* art = findArtTypeForObject(*record.object, context);
        if (building && building->unitRepair && !building->naval && art && art->flat
            && record.object->buildAnimTicksRemaining <= 0) depots.push_back(&record);
    }
    int adjusted = 0;
    for (auto& record : records) {
        if (!record.object || record.object->kind != WorldObjectKind::Unit || record.aircraft
            || record.object->airborne) continue;
        const double x = objectRenderX(*record.object), y = objectRenderY(*record.object);
        for (const auto* host : depots) {
            const auto* building = context.rules->findBuildingType(host->object->type);
            const double hostX = objectRenderX(*host->object), hostY = objectRenderY(*host->object);
            const int blockedRows = std::clamp(building->numberImpassableRows, 0, host->footprintWidth);
            const bool onPlatform = x >= hostX + blockedRows - 0.5 && x < hostX + host->footprintWidth - 0.5
                && y >= hostY - 0.5 && y < hostY + host->footprintHeight - 0.5;
            if (!onPlatform && record.object->dockedToObjectId != host->instanceId) continue;
            record.drawLevel = host->drawLevel; record.sortPoint = host->sortPoint;
            record.cellSum = host->cellSum; record.kindBias = host->kindBias + 1;
            record.ySortOverrideLeptons = runtimeWorldObjectYSortLeptons(*host) + 1.0;
            record.hasYSortOverride = true; ++adjusted; break;
        }
    }
    return adjusted;
}

static bool runRuntimeRenderSpatialContractCheck(std::string& errorMessage) {
    const bool benchmark = runtimeRendererEnvironmentValue(
        "CONCORD_RENDER_SCALABILITY_BENCHMARK") != nullptr;
    const auto fail = [&](const std::string& detail) {
        errorMessage = "render spatial candidates: " + detail;
        return false;
    };

    // Compare complete candidate-filtered sequences, including original order,
    // negative cells, bucket boundaries, enormous rectangles and invalid input.
    RuntimeRenderSpatialIndex probeIndex;
    std::vector<std::array<double, 4>> rectangles;
    for (int index = 0; index < 80; ++index) {
        const double x = (index % 10) * 8.0 - 40.5;
        const double y = (index / 10) * 8.0 - 32.5;
        rectangles.push_back({x, y, x + (index % 4) + 1.0, y + 3.0});
    }
    rectangles.insert(rectangles.end(), {
        {-1.0e15, -1.0e15, 1.0e15, 1.0e15},
        {-1.0, -1.0, 50000.0, 50000.0},
        {std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0, 1.0},
        {-std::numeric_limits<double>::infinity(), 0.0,
            std::numeric_limits<double>::infinity(), 1.0},
        {4.0, 4.0, -4.0, -4.0}
    });
    for (const auto& rect : rectangles) probeIndex.add(rect[0], rect[1], rect[2], rect[3]);
    std::vector<std::size_t> candidates, expected, actual;
    const auto checkPoint = [&](double x, double y) {
        probeIndex.query(x, y, candidates);
        expected.clear(); actual.clear();
        const auto contains = [&](std::size_t ordinal) {
            const auto& rect = rectangles[ordinal];
            return x >= rect[0] && x <= rect[2] && y >= rect[1] && y <= rect[3];
        };
        for (std::size_t index = 0; index < rectangles.size(); ++index)
            if (contains(index)) expected.push_back(index);
        for (const auto ordinal : candidates) {
            if (ordinal >= rectangles.size()) return false;
            if (contains(ordinal)) actual.push_back(ordinal);
        }
        return expected == actual && std::is_sorted(candidates.begin(), candidates.end())
            && std::adjacent_find(candidates.begin(), candidates.end()) == candidates.end();
    };
    for (int x = -96; x <= 96; ++x)
        for (int y = -80; y <= 80; y += 3)
            if (!checkPoint(x * 0.5, y * 0.5)) return fail("rectangle order/boundary mismatch");
    for (const double x : {1.0e30, -1.0e30,
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()})
        if (!checkPoint(x, 0.5)) return fail("out-of-grid fallback mismatch");

    IniFile rulesIni, artIni;
    RulesDatabase rules;
    ArtDatabase art;
    if (!rulesIni.loadFromString(
            "[BuildingTypes]\n0=__SPATIAL_DEPOT__\n1=__SPATIAL_BLOCKED__\n"
            "2=__SPATIAL_NAVAL__\n[__SPATIAL_DEPOT__]\nUnitRepair=yes\n"
            "[__SPATIAL_BLOCKED__]\nUnitRepair=yes\nNumberImpassableRows=2\n"
            "[__SPATIAL_NAVAL__]\nUnitRepair=yes\nNaval=yes\n")
        || !rules.loadFromIni(rulesIni)
        || !artIni.loadFromString(
            "[__SPATIAL_DEPOT__]\nFlat=yes\nHeight=12\nFoundation=4x3\n"
            "[__SPATIAL_BLOCKED__]\nFlat=yes\nHeight=6\nFoundation=4x3\n"
            "[__SPATIAL_NAVAL__]\nFlat=yes\nHeight=3\nFoundation=4x3\n")
        || !art.loadFromIni(artIni)) return fail("fixture load failed");
    RuntimeMapRenderContext context;
    context.rules = &rules; context.art = &art;
    RuntimeMapCamera camera;
    RuntimeMapRenderCache cache;
    const int structureCount = benchmark ? 1024 : 64;
    const int unitCount = benchmark ? 4096 : 512;
    std::vector<WorldObjectInstance> objects(structureCount + unitCount);
    std::vector<RuntimeWorldObjectDrawRecord> records;
    records.reserve(objects.size());
    for (int index = 0; index < structureCount; ++index) {
        auto& object = objects[index];
        object.kind = WorldObjectKind::Structure;
        object.type = index % 13 == 0 ? "__SPATIAL_NAVAL__"
            : index % 3 == 0 ? "__SPATIAL_BLOCKED__" : "__SPATIAL_DEPOT__";
        object.instanceId = index % 17 == 0 ? index - 1 : index;
        object.x = (index % 32) * 8 - 128;
        object.y = (index / 32) * 8 - 128;
        object.hasRenderPosition = true;
        object.renderX = object.x + (index % 2 ? 0.25 : -0.25);
        object.renderY = object.y + (index % 3 ? 0.125 : -0.125);
        object.buildAnimTicksRemaining = index % 19 == 0 ? 1 : 0;
        RuntimeWorldObjectDrawRecord record;
        record.object = &object; record.instanceId = object.instanceId;
        record.footprintWidth = 4; record.footprintHeight = 3;
        record.drawLevel = index % 7; record.sortPoint = {index * 3, index * 5};
        record.center = {index % 31, index % 47}; record.cellSum = index;
        record.kindBias = index % 4;
        records.push_back(record);
    }
    // Overlap and duplicate IDs deliberately challenge first-match ordering.
    objects[5].renderX = objects[4].renderX;
    objects[5].renderY = objects[4].renderY;
    records.back().footprintWidth = 50000;
    records.back().footprintHeight = 50000;
    for (int index = 0; index < unitCount; ++index) {
        auto& object = objects[structureCount + index];
        const auto& host = objects[(index * 17 + 4) % structureCount];
        object.kind = index % 23 == 0 ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        object.instanceId = structureCount + index;
        object.hasRenderPosition = true;
        object.renderX = objectRenderX(host) + (index % 11) * 0.5 - 1.0;
        object.renderY = objectRenderY(host) + (index % 9) * 0.5 - 1.0;
        object.subCell = index % 5;
        object.airborne = index % 29 == 0;
        object.dockedToObjectId = index % 7 == 0
            ? objects[(index * 13) % structureCount].instanceId : -1;
        RuntimeWorldObjectDrawRecord record;
        record.object = &object; record.instanceId = object.instanceId;
        record.aircraft = index % 31 == 0;
        record.center = {index % 17, index % 113};
        records.push_back(record);
    }
    auto reference = records;
    auto optimized = records;
    const auto directStart = std::chrono::steady_clock::now();
    const int directAdjusted = applyRuntimeRepairDepotForegroundOrderReference(reference, context);
    const auto indexedStart = std::chrono::steady_clock::now();
    const int indexedAdjusted = applyRuntimeRepairDepotForegroundOrder(optimized, context);
    const auto indexedEnd = std::chrono::steady_clock::now();
    if (directAdjusted != indexedAdjusted) return fail("depot adjusted count differs");
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& left = reference[index]; const auto& right = optimized[index];
        if (left.drawLevel != right.drawLevel || left.sortPoint.x != right.sortPoint.x
            || left.sortPoint.y != right.sortPoint.y || left.cellSum != right.cellSum
            || left.kindBias != right.kindBias || left.hasYSortOverride != right.hasYSortOverride
            || left.ySortOverrideLeptons != right.ySortOverrideLeptons)
            return fail("depot draw record differs at " + std::to_string(index));
    }

    auto structures = runtimeAircraftClearanceStructures(records);
    const auto structureIndex = buildRuntimeStructureRenderSpatialIndex(structures);
    std::uint64_t candidateVisits = 0;
    for (int index = 0; index < unitCount; ++index) {
        auto aircraft = records[structureCount + index];
        aircraft.airborneForeground = true;
        auto& object = objects[structureCount + index];
        object.factoryExitHostId = index % 3 == 0 ? structures[index % structures.size()].instanceId : -1;
        // Probe the inclusive aircraft epsilon on both sides of exact edges.
        if (index % 4 == 0) {
            const auto& host = *structures[index % structures.size()].object;
            object.renderX = objectRenderX(host) - 0.5 + (index % 3 - 1) * 0.000001;
        }
        const auto subCell = worldObjectSubCellOffset(object);
        structureIndex.query(objectRenderX(object) + subCell.x,
            objectRenderY(object) + subCell.y, candidates);
        candidateVisits += candidates.size();
        expected.clear(); actual.clear();
        for (std::size_t candidate = 0; candidate < structures.size(); ++candidate)
            if (runtimeAircraftOverStructureFoundation(aircraft, structures[candidate]))
                expected.push_back(candidate);
        for (const auto candidate : candidates)
            if (runtimeAircraftOverStructureFoundation(aircraft, structures[candidate]))
                actual.push_back(candidate);
        if (expected != actual) return fail("aircraft precise overlap/order differs");
        if (runtimeAircraftBuildingClearanceLiftPixels(aircraft, structures, context, camera, cache)
            != runtimeAircraftBuildingClearanceLiftPixels(aircraft, structures, context, camera, cache,
                &candidates)) return fail("aircraft clearance pixels differ");
    }
    if (candidateVisits >= static_cast<std::uint64_t>(structureCount) * unitCount / 4)
        return fail("sparse-scene candidate work did not decrease");
    if (benchmark) {
        std::cout << "Renderer spatial benchmark: buildings=" << structureCount
            << " units=" << unitCount << " directDepotMs=" << runtimeElapsedMs(directStart, indexedStart)
            << " indexedDepotMs=" << runtimeElapsedMs(indexedStart, indexedEnd)
            << " directAircraftCandidates=" << static_cast<std::uint64_t>(structureCount) * unitCount
            << " indexedAircraftCandidates=" << candidateVisits << " equal=1\n";
    }
    return true;
}
