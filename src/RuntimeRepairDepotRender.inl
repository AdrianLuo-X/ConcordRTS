int applyRuntimeRepairDepotForegroundOrder(
    std::vector<RuntimeWorldObjectDrawRecord>& records,
    const RuntimeMapRenderContext& context
) {
    if(!context.rules||!context.art)return 0;
    struct Depot {
        const RuntimeWorldObjectDrawRecord* record;
        double left, top, right, bottom;
    };
    std::vector<Depot> depots;
    for(const auto& record:records) {
        if(!record.object||record.object->kind!=WorldObjectKind::Structure)continue;
        const auto* building=context.rules->findBuildingType(record.object->type);
        const auto* art=findArtTypeForObject(*record.object,context);
        if(building&&building->unitRepair&&!building->naval&&!building->waterBound&&art&&art->flat
            &&record.object->buildAnimTicksRemaining<=0) {
            const double x=objectRenderX(*record.object),y=objectRenderY(*record.object);
            const int blockedRows=std::clamp(building->numberImpassableRows,0,record.footprintWidth);
            depots.push_back({&record,x+blockedRows-0.5,y-0.5,
                x+record.footprintWidth-0.5,y+record.footprintHeight-0.5});
        }
    }
    if(depots.empty())return 0;
    const bool useSpatialIndex=depots.size()>16;
    RuntimeRenderSpatialIndex spatialIndex;
    std::unordered_map<int,std::vector<std::size_t>> depotsById;
    if(useSpatialIndex) {
        for(std::size_t index=0;index<depots.size();++index) {
            const auto& depot=depots[index];
            spatialIndex.add(depot.left,depot.top,depot.right,depot.bottom);
            depotsById[depot.record->instanceId].push_back(index);
        }
    }
    std::vector<std::size_t> candidates;
    int adjusted=0;
    for(auto& record:records) {
        if(!record.object||record.object->kind!=WorldObjectKind::Unit||record.aircraft
            ||record.object->airborne)continue;
        const double x=objectRenderX(*record.object),y=objectRenderY(*record.object);
        if(useSpatialIndex) {
            spatialIndex.query(x,y,candidates);
            // A docked vehicle can still have an interpolated position outside
            // its host. Include every matching ID while retaining depot order.
            const auto docked=depotsById.find(record.object->dockedToObjectId);
            if(docked!=depotsById.end()) {
                candidates.insert(candidates.end(),docked->second.begin(),docked->second.end());
                std::sort(candidates.begin(),candidates.end());
                candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
            }
        }
        const std::size_t count=useSpatialIndex?candidates.size():depots.size();
        for(std::size_t index=0;index<count;++index) {
            const auto& depot=depots[useSpatialIndex?candidates[index]:index];
            const auto* host=depot.record;
            const bool onPlatform=x>=depot.left&&x<depot.right
                &&y>=depot.top&&y<depot.bottom;
            if(!onPlatform&&record.object->dockedToObjectId!=host->instanceId)continue;
            // Flat service pads are beneath vehicles even before docking and
            // while leaving. Keep real draw coordinates; only depth changes.
            record.drawLevel=host->drawLevel;record.sortPoint=host->sortPoint;
            record.cellSum=host->cellSum;record.kindBias=host->kindBias+1;
            record.repairDepotHostId=host->instanceId;
            record.ySortOverrideLeptons=runtimeWorldObjectYSortLeptons(*host)+1.0;
            record.hasYSortOverride=true;++adjusted;break;
        }
    }
    return adjusted;
}
