int applyRuntimeRepairDepotForegroundOrder(
    std::vector<RuntimeWorldObjectDrawRecord>& records,
    const RuntimeMapRenderContext& context
) {
    if (!context.rules || !context.art) return 0;
    std::vector<concord::render::RuntimeRepairDepotPlatform> platforms;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        if (!record.object || record.object->kind != WorldObjectKind::Structure) continue;
        const auto* building = context.rules->findBuildingType(record.object->type);
        const auto* art = findArtTypeForObject(*record.object, context);
        if (building && building->unitRepair && !building->naval && !building->waterBound && art && art->flat
            && record.object->buildAnimTicksRemaining <= 0) {
            const double x = objectRenderX(*record.object);
            const double y = objectRenderY(*record.object);
            platforms.push_back(concord::render::makeRuntimeRepairDepotPlatform(i, record.instanceId,
                x, y, record.footprintWidth, record.footprintHeight, building->numberImpassableRows));
        }
    }
    if (platforms.empty()) return 0;
    concord::render::RuntimeRepairDepotForegroundOrder order(platforms);
    int adjusted = 0;
    for (auto& record : records) {
        if (!record.object || record.object->kind != WorldObjectKind::Unit || record.aircraft
            || record.object->airborne) continue;
        const double x = objectRenderX(*record.object);
        const double y = objectRenderY(*record.object);
        const auto hostIndex = order.firstMatchingHost(x, y, record.object->dockedToObjectId);
        if (!hostIndex) continue;
        // Only copy ordering. The vehicle keeps its original drawing coordinates.
        const auto& host = records[*hostIndex];
        record.drawLevel = host.drawLevel;
        record.sortPoint = host.sortPoint;
        record.cellSum = host.cellSum;
        record.kindBias = host.kindBias + 1;
        record.repairDepotHostId = host.instanceId;
        record.ySortOverrideLeptons = runtimeWorldObjectYSortLeptons(host) + 1.0;
        record.hasYSortOverride = true;
        ++adjusted;
    }
    return adjusted;
}
