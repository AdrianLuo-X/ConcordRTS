// Derived candidate buckets, rebuilt with the existing tick occupancy cache.
// Complete active segment envelopes cover every body position within a tick.
// Queries re-read current pose/layer/eligibility; buckets are not occupancy authority.
bool runtimeGroundPoseIsSpatialCandidate(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    return worldObjectHasGroundRenderPose(object) && object.alive && object.health>0
        && !object.authoredAircraft && std::isfinite(object.renderX) && std::isfinite(object.renderY)
        && (object.renderMoveActive || worldObjectHasStoppedGroundPose(object))
        && !runtimeObjectOffGroundByMagBeam(state,object.instanceId)
        && runtimeObjectMovementMode(state,object)!=PathMovementMode::Air;
}

bool runtimeGroundPoseSpatialIndexIsCurrent(const RuntimeWindowState& state) {
    const auto& proof=state.movementEmptyCellProof;const auto& objects=state.world.getObjects();
    return proof.additionalGroundPoseIndexReady && state.tickOccupancyCacheTick==state.tick
        && proof.objectData==objects.data() && proof.objectCount==objects.size()
        && proof.additionalGroundPoseIndexedObjects.size()==objects.size();
}

bool runtimeGroundPoseSpatialKeys(const RuntimeWindowState& state,
    const WorldObjectInstance& object,std::vector<int>& keys) {
    keys.clear();
    const auto add=[&](int x,int y) {
        if(x<0 || y<0 || x>=512 || y>=512) return false;
        keys.push_back(runtimeCellKey(x,y));return true;
    };
    if(object.renderMoveActive) {
        const auto found=state.behaviors.find(object.instanceId);
        if(found==state.behaviors.end()) return false;
        const auto& move=found->second;const auto slot=worldObjectSubCellOffset(object);
        const double x[]={object.renderX+slot.x,move.renderFrom.x+move.renderFromOffsetX+slot.x,
            move.renderFrom.x+slot.x,move.renderTo.x+move.renderToOffsetX+slot.x};
        const double y[]={object.renderY+slot.y,move.renderFrom.y+move.renderFromOffsetY+slot.y,
            move.renderFrom.y+slot.y,move.renderTo.y+move.renderToOffsetY+slot.y};
        for(int n=0;n<4;++n) if(!std::isfinite(x[n]) || !std::isfinite(y[n])) return false;
        const double minX=*std::min_element(x,x+4),maxX=*std::max_element(x,x+4);
        const double minY=*std::min_element(y,y+4),maxY=*std::max_element(y,y+4);
        const bool foot=object.kind==WorldObjectKind::Infantry;
        // Round infantry centers exactly as the scalar predicate does.
        // Expand boundary points by one ulp against interpolation roundoff.
        const double left=foot?std::floor(std::nextafter(minX+0.5,-std::numeric_limits<double>::infinity())):std::floor(minX);
        const double right=foot?std::floor(std::nextafter(maxX+0.5,std::numeric_limits<double>::infinity())):std::ceil(maxX);
        const double top=foot?std::floor(std::nextafter(minY+0.5,-std::numeric_limits<double>::infinity())):std::floor(minY);
        const double bottom=foot?std::floor(std::nextafter(maxY+0.5,std::numeric_limits<double>::infinity())):std::ceil(maxY);
        if(left<0 || top<0 || right>=512 || bottom>=512
            || (right-left+1)*(bottom-top+1)>4096) return false;
        for(int row=static_cast<int>(top);row<=static_cast<int>(bottom);++row)
            for(int column=static_cast<int>(left);column<=static_cast<int>(right);++column)
                if(!add(column,row)) return false;
    } else {
        const auto context=runtimeGroundPoseContext(state);bool valid=true;
        forEachRuntimeAdditionalGroundPoseOccupancy(object,[&](const auto& held) {
            valid=add(held.x,held.y)&&valid;
        },&context);
        const auto slot=worldObjectSubCellOffset(object);
        const double x=std::floor(object.renderX+slot.x+0.5),y=std::floor(object.renderY+slot.y+0.5);
        if(!valid || x<0 || y<0 || x>=512 || y>=512
            || !add(static_cast<int>(x),static_cast<int>(y))) return false;
        // Same-navigation-cell stopped bodies still constrain an already
        // accepted follower, although they have no additional held cell.
    }
    std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
    return true;
}

// Factory admission also needs ordinary stationary bodies, including all three
// infantry residents. This independent derived bucket set leaves the additional
// pose/empty-cell contract unchanged and is filled by the existing tick capture.
bool runtimeAllGroundBodyIsSpatialCandidate(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    return object.alive && object.health>0 && !object.airborne
        && (object.kind==WorldObjectKind::Unit || object.kind==WorldObjectKind::Infantry)
        && object.dockedToObjectId<0 && object.tankBunkerHostId<0 && object.garrisonHostObjectId<0
        && !runtimeObjectOffGroundByMagBeam(state,object.instanceId)
        && runtimeObjectMovementMode(state,object)!=PathMovementMode::Air;
}

bool runtimeAllGroundBodySpatialIndexIsCurrent(const RuntimeWindowState& state) {
    const auto& proof=state.movementEmptyCellProof;const auto& objects=state.world.getObjects();
    return proof.allGroundBodyIndexReady && state.tickOccupancyCacheTick==state.tick
        && proof.objectData==objects.data() && proof.objectCount==objects.size();
}

bool runtimeAppendAllGroundBodySpatialObject(RuntimeWindowState& state,std::size_t index) {
    const auto& objects=state.world.getObjects();
    if(index>=objects.size()) return false;
    const auto& object=objects[index];
    if(!runtimeAllGroundBodyIsSpatialCandidate(state,object)) return true;
    std::vector<int> keys;keys.reserve(16);
    if(object.hasRenderPosition && object.renderMoveActive) {
        if(!runtimeGroundPoseSpatialKeys(state,object,keys)) return false;
    } else {
        const auto slot=worldObjectSubCellOffset(object);
        const double x=std::floor(runtimeObjectRenderX(object)+slot.x+0.5);
        const double y=std::floor(runtimeObjectRenderY(object)+slot.y+0.5);
        if(!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=512 || y>=512) return false;
        keys.push_back(runtimeCellKey(static_cast<int>(x),static_cast<int>(y)));
    }
    for(int key:keys) {
        auto& bucket=state.movementEmptyCellProof.allGroundBodyIndicesByCell[key];
        if(std::find(bucket.begin(),bucket.end(),index)==bucket.end()) bucket.push_back(index);
    }
    return true;
}

template<class Visitor>
void forEachRuntimeAllGroundBodyCandidateAtCell(const RuntimeWindowState& state,
    const RuntimeCellPoint& cell,Visitor visit) {
    const auto& objects=state.world.getObjects();
    if(cell.x>=0 && cell.y>=0 && cell.x<512 && cell.y<512
        && runtimeAllGroundBodySpatialIndexIsCurrent(state)) {
        const auto& buckets=state.movementEmptyCellProof.allGroundBodyIndicesByCell;
        const auto found=buckets.find(runtimeCellKey(cell.x,cell.y));
        if(found!=buckets.end()) for(std::size_t index:found->second)
            if(index<objects.size()) visit(objects[index]);
    } else {
        // Invalid or unusual metadata retains the complete scalar safety path.
        for(const auto& object:objects) visit(object);
    }
}

// Conservative departure candidates. Read authoritative behavior at query time;
// completed/dead segments can remain in a bucket until the next tick rebuild.
void appendRuntimeDepartureSpatialBehavior(RuntimeWindowState& state, int id,
    const RuntimeObjectBehavior& move) {
    const RuntimeCellPoint cells[] = {move.moveSegmentFrom, move.renderFrom, move.renderTo,
        {move.renderFrom.x, move.renderTo.y}, {move.renderTo.x, move.renderFrom.y}};
    for (const auto& cell : cells) {
        if (cell.x < 0 || cell.y < 0 || cell.x >= 512 || cell.y >= 512) continue;
        auto& ids = state.movementEmptyCellProof.departureBehaviorIdsByCell[runtimeCellKey(cell.x,cell.y)];
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
}
bool buildRuntimeGroundPoseSpatialIndex(RuntimeWindowState& state) {
    state.movementEmptyCellProof.departureBehaviorIdsByCell.clear();
    state.movementEmptyCellProof.departureBehaviorCount = state.behaviors.size();
    for (const auto& [id, move] : state.behaviors) appendRuntimeDepartureSpatialBehavior(state, id, move);
    auto& proof=state.movementEmptyCellProof;proof.additionalGroundPoseIndexReady=false;
    auto& buckets=proof.additionalGroundPoseIndicesByCell;buckets.clear();
    const auto& objects=state.world.getObjects();
    proof.additionalGroundPoseIndexedObjects.assign(objects.size(),0);
    std::vector<int> keys;keys.reserve(16);
    for(std::size_t index:proof.additionalGroundPoseObjectIndices) {
        if(index>=objects.size()) return false;
        const auto& object=objects[index];
        if(!runtimeGroundPoseIsSpatialCandidate(state,object)) continue;
        if(!runtimeGroundPoseSpatialKeys(state,object,keys)) return false;
        for(int key:keys) buckets[key].push_back(index);
        proof.additionalGroundPoseIndexedObjects[index]=1;
    }
    proof.additionalGroundPoseIndexReady=true;return true;
}

// Append only the new segment's small envelope. Old same-tick buckets remain
// conservative candidates; membership flags avoid rescanning the full inventory.
bool runtimeTrackGroundPoseSpatialMove(RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    const auto departure = state.behaviors.find(object.instanceId);
    if (departure != state.behaviors.end()) appendRuntimeDepartureSpatialBehavior(state, object.instanceId, departure->second);
    if(runtimeAllGroundBodyIsSpatialCandidate(state,object)) {
        if(!runtimeAllGroundBodySpatialIndexIsCurrent(state)) return false;
        const auto& objects=state.world.getObjects();
        const auto base=reinterpret_cast<std::uintptr_t>(objects.data());
        const auto address=reinterpret_cast<std::uintptr_t>(&object);
        if(address<base || (address-base)%sizeof(WorldObjectInstance)!=0) return false;
        const auto index=(address-base)/sizeof(WorldObjectInstance);
        if(!runtimeAppendAllGroundBodySpatialObject(state,index)) return false;
    }
    if(!runtimeGroundPoseIsSpatialCandidate(state,object)) return true;
    if(!runtimeGroundPoseSpatialIndexIsCurrent(state)) return false;
    auto& proof=state.movementEmptyCellProof;const auto& objects=state.world.getObjects();
    const auto base=reinterpret_cast<std::uintptr_t>(objects.data());
    const auto address=reinterpret_cast<std::uintptr_t>(&object);
    if(address<base || (address-base)%sizeof(WorldObjectInstance)!=0) return false;
    const auto index=(address-base)/sizeof(WorldObjectInstance);
    if(index>=objects.size()) return false;
    std::vector<int> keys;keys.reserve(16);
    if(!runtimeGroundPoseSpatialKeys(state,object,keys)) return false;
    if(!proof.additionalGroundPoseIndexedObjects[index]) {
        proof.additionalGroundPoseIndexedObjects[index]=1;
        proof.additionalGroundPoseObjectIndices.push_back(index);
    }
    for(int key:keys) {
        auto& bucket=proof.additionalGroundPoseIndicesByCell[key];
        if(std::find(bucket.begin(),bucket.end(),index)==bucket.end()) bucket.push_back(index);
    }
    return true;
}

template<class Visitor>
void forEachRuntimeGroundPoseCandidateAtCell(const RuntimeWindowState& state,
    const RuntimeCellPoint& cell,Visitor visit,std::size_t* candidateVisits=nullptr) {
    const auto& objects=state.world.getObjects();
    const auto inspect=[&](const WorldObjectInstance& object) {
        if(candidateVisits) ++*candidateVisits;visit(object);
    };
    if(cell.x>=0 && cell.y>=0 && cell.x<512 && cell.y<512
        && runtimeGroundPoseSpatialIndexIsCurrent(state)) {
        const auto& buckets=state.movementEmptyCellProof.additionalGroundPoseIndicesByCell;
        const auto found=buckets.find(runtimeCellKey(cell.x,cell.y));
        if(found!=buckets.end()) for(std::size_t index:found->second)
            if(index<objects.size()) inspect(objects[index]);
    } else {
        // Preserve the scalar safety path when metadata is absent or unusual.
        for(const auto& object:objects) if(object.hasRenderPosition) inspect(object);
    }
}

template<class Visitor>
void forEachRuntimeAdditionalGroundPoseOccupantAtCell(const RuntimeWindowState& state,
    const RuntimeCellPoint& cell,Visitor visit,std::size_t* candidateVisits=nullptr) {
    const auto context=runtimeGroundPoseContext(state);
    forEachRuntimeGroundPoseCandidateAtCell(state,cell,[&](const auto& object) {
        if(runtimeObjectOffGroundByMagBeam(state,object.instanceId)
            || runtimeObjectMovementMode(state,object)==PathMovementMode::Air) return;
        forEachRuntimeAdditionalGroundPoseOccupancy(object,[&](const auto& held) {
            if(held.x==cell.x && held.y==cell.y) visit(object,held);
        },&context);
    },candidateVisits);
}
