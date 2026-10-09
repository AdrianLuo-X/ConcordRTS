// A verified entry owns a slotless destination, while neighboring bodies
// remain ordinary collision obstacles. Unit hosts use the existing strict
// slave-worker/owner/task/phase/refinery rule; structures keep their entry marker.
int runtimeInfantryAuthorizedSlotlessEntryHostId(const RuntimeWindowState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to) {
    if(mover.kind!=WorldObjectKind::Infantry || mover.airborne) return -1;
    const auto found=state.behaviors.find(mover.instanceId);
    if(found==state.behaviors.end() || found->second.kind!=RuntimeBehaviorKind::Move
        || found->second.pendingAllowedOccupiedObjectId<0) return -1;
    const int hostId=found->second.pendingAllowedOccupiedObjectId;
    if(runtimeTransportAuthorizedBoardingHostId(state,mover,to)==hostId) return hostId;
    const auto* host=findObjectById(state,hostId);
    if(!host || !host->alive || host->health<=0
        || !runtimeCellIsAllowedOccupiedStructureFootprint(state,to,hostId,&mover)) return -1;
    return hostId;
}
bool runtimeInfantryBodyUsesAuthorizedSlotlessEntry(const RuntimeWindowState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to) {
    return runtimeInfantryAuthorizedSlotlessEntryHostId(state,mover,to)>=0;
}

// Accepted convoys can interpolate at different speeds or stop independently.
// Keep each accepted cell/timing, but wait before a visible body closes inside
// another moving/stopped body's spacing. An overlap may leave by separating.
bool runtimeStoppedGroundPoseBlocksRenderMove(const RuntimeWindowState& state,
    const WorldObjectInstance& mover,double nextBaseX,double nextBaseY) {
    if (mover.airborne || mover.kind==WorldObjectKind::Structure
        || runtimeObjectMovementMode(state,mover)==PathMovementMode::Air) return false;
    const bool foot=mover.kind==WorldObjectKind::Infantry;
    const auto offset=worldObjectSubCellOffset(mover);
    const double currentX=runtimeObjectRenderX(mover)+offset.x;
    const double currentY=runtimeObjectRenderY(mover)+offset.y;
    const double nextX=nextBaseX+offset.x,nextY=nextBaseY+offset.y;
    // Navigation commits the destination layer before the visible body arrives.
    // Preview the same half-segment layer switch as advanceRuntimeRenderMove.
    const bool currentOnBridge=worldObjectHasGroundRenderPose(mover)?mover.renderOnBridge:mover.onBridge;
    bool nextOnBridge=currentOnBridge;
    if(mover.renderMoveActive) {
        const auto found=state.behaviors.find(mover.instanceId);
        if(found!=state.behaviors.end() && found->second.renderMoveTicksTotal>0
            && found->second.renderMoveTicksRemaining>0) {
            const auto& move=found->second;
            const double progress=runtimeVehicleMoveRenderProgressAtRemaining(move,
                std::max(0,move.renderMoveTicksRemaining-1));
            nextOnBridge=progress<0.5?move.renderFromOnBridge:move.renderToOnBridge;
        }
    }
    int entryHostId=-1;
    if(mover.renderMoveActive) {
        const auto found=state.behaviors.find(mover.instanceId);
        if(found!=state.behaviors.end()) {
            entryHostId=runtimeTransportAuthorizedBoardingHostId(state,mover,found->second.renderTo);
            if(entryHostId<0 && foot) entryHostId=runtimeInfantryAuthorizedSlotlessEntryHostId(state,mover,found->second.renderTo);
        }
    }
    const auto activeMove=state.behaviors.find(mover.instanceId);
    const bool transportTransfer=entryHostId>=0
        || (activeMove!=state.behaviors.end() && activeMove->second.transportTransferHostId>=0);
    const bool completeBodyInventory=foot || transportTransfer;
    bool blocked=false;
    const auto inspect=[&](const WorldObjectInstance& other) {
        if (blocked || other.instanceId==mover.instanceId || other.instanceId==entryHostId
            || !(completeBodyInventory?runtimeAllGroundBodyIsSpatialCandidate(state,other):runtimeGroundPoseIsSpatialCandidate(state,other))
            || nextOnBridge!=(other.hasRenderPosition?other.renderOnBridge:other.onBridge)
            || other.dockedToObjectId>=0 || other.tankBunkerHostId>=0
            || other.garrisonHostObjectId>=0 || other.factoryExitHostId>=0
            || runtimeObjectOffGroundByMagBeam(state,other.instanceId)
            || runtimeObjectMovementMode(state,other)==PathMovementMode::Air) return;
        if(runtimeCrusherUsesCrushingMovement(state,mover)
            && runtimeCrusherCanCrushObject(state,mover,other,runtimeCellPointForObject(other),true)) return;
        const auto slot=worldObjectSubCellOffset(other);
        const double otherX=runtimeObjectRenderX(other)+slot.x,otherY=runtimeObjectRenderY(other)+slot.y;
        const bool otherFoot=other.kind==WorldObjectKind::Infantry;
        const double minimum=foot&&otherFoot?0.5:(!foot&&!otherFoot?1.0:0.75);
        const double nextDistance=(nextX-otherX)*(nextX-otherX)+(nextY-otherY)*(nextY-otherY);
        if (nextDistance+1e-9>=minimum*minimum) return;
        const double currentDistance=(currentX-otherX)*(currentX-otherX)+(currentY-otherY)*(currentY-otherY);
        if (currentOnBridge==(other.hasRenderPosition?other.renderOnBridge:other.onBridge)
            && currentDistance+1e-9<minimum*minimum && nextDistance>currentDistance+1e-9) return;
        blocked=true;
    };
    const auto& objects=state.world.getObjects();
    const double column=std::floor(nextX+0.5),row=std::floor(nextY+0.5);
    // Ordinary stationary infantry have no render record but still own a body.
    // Foot queries reuse the complete local buckets; vehicles retain their pose inventory.
    const bool indexCurrent=completeBodyInventory?runtimeAllGroundBodySpatialIndexIsCurrent(state):runtimeGroundPoseSpatialIndexIsCurrent(state);
    if(indexCurrent && std::isfinite(column) && std::isfinite(row)
        && column>=-1 && column<=512 && row>=-1 && row<=512) {
        for(int dy=-1;dy<=1 && !blocked;++dy) for(int dx=-1;dx<=1 && !blocked;++dx) {
            const RuntimeCellPoint cell{static_cast<int>(column)+dx,static_cast<int>(row)+dy};
            if(cell.x<0 || cell.y<0 || cell.x>=512 || cell.y>=512) continue;
            if(completeBodyInventory) forEachRuntimeAllGroundBodyCandidateAtCell(state,cell,inspect);
            else forEachRuntimeGroundPoseCandidateAtCell(state,cell,inspect);
        }
    } else {
        for(const auto& object:objects) if(completeBodyInventory || object.hasRenderPosition) inspect(object);
    }
    return blocked;
}

// Reuse only the tick cache's sparse candidate inventory, never cached pose
// coordinates. Newly accepted segments append their envelopes during this tick.
// Dead/off-ground/changed records are checked at every read.
template<class Visitor>
void forEachRuntimeAdditionalGroundPoseOccupant(const RuntimeWindowState& state, Visitor visit) {
    const auto& objects=state.world.getObjects();
    const auto& proof=state.movementEmptyCellProof;
    const auto poseContext=runtimeGroundPoseContext(state);
    const auto inspect=[&](const WorldObjectInstance& object) {
        if (runtimeObjectOffGroundByMagBeam(state,object.instanceId)
            || runtimeObjectMovementMode(state,object)==PathMovementMode::Air) return;
        forEachRuntimeAdditionalGroundPoseOccupancy(object,[&](const auto& held) { visit(object,held); },&poseContext);
    };
    if (state.tickOccupancyCacheTick==state.tick && proof.objectData==objects.data()
        && proof.objectCount==objects.size()) {
        for (std::size_t index:proof.additionalGroundPoseObjectIndices)
            if (index<objects.size()) inspect(objects[index]);
    } else {
        for (const auto& object:objects) {
            if (!object.hasRenderPosition) continue;
            inspect(object);
        }
    }
}

// The occupied destination of an Omni approach has not committed yet, but its
// accepted segment still reserves the same corridor against new foundations
// and other committing ground movers.
bool runtimeOmniCrushPendingSegmentContainsCell(
    const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior, const RuntimeCellPoint& cell
) {
    if (!runtimeOmniCrushPendingSegment(state, object, behavior)) return false;
    return cell == behavior.renderFrom || cell == behavior.renderTo
        || (behavior.renderFrom.x != behavior.renderTo.x
            && behavior.renderFrom.y != behavior.renderTo.y
            && (cell == RuntimeCellPoint{behavior.renderFrom.x, behavior.renderTo.y}
                || cell == RuntimeCellPoint{behavior.renderTo.x, behavior.renderFrom.y}));
}

// A new foundation cannot appear under an already accepted ground segment.
// Use saved segment endpoints, including a stopped body's resumed first leg.
// Diagonal side cells were required to be clear when that segment committed, too.
std::unordered_set<int> runtimeGroundMoveSegmentPlacementCells(
    const RuntimeWindowState& state, int ignoredObjectId = -1
) {
    std::unordered_set<int> cells;
    for(const auto& [id,behavior] : state.behaviors) {
        if(id==ignoredObjectId) continue;
        const auto* object=findObjectById(state,id);
        if(!object||!object->alive||object->health<=0||object->airborne
            ||object->kind==WorldObjectKind::Structure||runtimeObjectOffGroundByMagBeam(state,id)
            ||runtimeObjectMovementMode(state,*object)==PathMovementMode::Air) continue;
        const bool pendingOmni=runtimeOmniCrushPendingSegment(state,*object,behavior);
        const auto from=pendingOmni?behavior.renderFrom:behavior.moveSegmentFrom;
        if((behavior.moveCooldownTicks<=0&&!pendingOmni)||from.x<0||from.y<0) continue;
        const auto to=pendingOmni?behavior.renderTo:runtimeCellPointForObject(*object);
        cells.insert(runtimeCellKey(from.x,from.y)); cells.insert(runtimeCellKey(to.x,to.y));
        if (runtimeInfantryMoveUsesCommittedCorner(*object, behavior)) {
            // The resumed first leg precedes moveSegmentFrom. Protect its
            // entire small corridor, including the two sides of a corner,
            // against a new foundation while that visible leg is active.
            const auto slot = worldObjectSubCellOffset(*object);
            const double startX = behavior.renderFrom.x + behavior.renderFromOffsetX + slot.x;
            const double startY = behavior.renderFrom.y + behavior.renderFromOffsetY + slot.y;
            const double pivotX = behavior.renderFrom.x + slot.x;
            const double pivotY = behavior.renderFrom.y + slot.y;
            const int left = std::clamp(static_cast<int>(std::floor(std::min(startX,pivotX)+0.5)),0,511);
            const int right = std::clamp(static_cast<int>(std::floor(std::max(startX,pivotX)+0.5)),0,511);
            const int top = std::clamp(static_cast<int>(std::floor(std::min(startY,pivotY)+0.5)),0,511);
            const int bottom = std::clamp(static_cast<int>(std::floor(std::max(startY,pivotY)+0.5)),0,511);
            for (int y=top;y<=bottom;++y) for (int x=left;x<=right;++x)
                cells.insert(runtimeCellKey(x,y));
        }
        if(from.x!=to.x&&from.y!=to.y) {
            cells.insert(runtimeCellKey(from.x,to.y)); cells.insert(runtimeCellKey(to.x,from.y));
        }
    }
    forEachRuntimeAdditionalGroundPoseOccupant(state,[&](const auto& object,const auto& held) {
        if (object.instanceId!=ignoredObjectId) cells.insert(runtimeCellKey(held.x,held.y));
    });
    return cells;
}

// x/y is committed at the start of a step. Hold its departure cell until the
// authoritative cooldown finishes for mixed traffic. Infantry departures also
// retain capacity against the three-person limit. Planning keeps transient
// traffic; actual commits wait, without discarding the path or changing its goal.
inline thread_local int runtimeGroundDepartureBatchReferenceOverride = -1;
bool runtimeGroundDepartureBatchReference() {
    if (runtimeGroundDepartureBatchReferenceOverride >= 0)
        return runtimeGroundDepartureBatchReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_DEPARTURE_BATCH_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeGroundDepartureBatchReferenceScope {
    int previous_;
public:
    explicit RuntimeGroundDepartureBatchReferenceScope(bool reference)
        : previous_(runtimeGroundDepartureBatchReferenceOverride) {
        runtimeGroundDepartureBatchReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeGroundDepartureBatchReferenceScope() { runtimeGroundDepartureBatchReferenceOverride = previous_; }
    RuntimeGroundDepartureBatchReferenceScope(const RuntimeGroundDepartureBatchReferenceScope&) = delete;
    RuntimeGroundDepartureBatchReferenceScope& operator=(const RuntimeGroundDepartureBatchReferenceScope&) = delete;
};

class RuntimeGroundDepartureQueryBatch;
inline thread_local const RuntimeGroundDepartureQueryBatch* runtimeActiveGroundDepartureQueryBatch = nullptr;

// Only the current mover's read-only neighbor-candidate loop may own this scope.
// Destroy/invalidate before facing, damage, reservations or commits mutate any
// world/behavior state. No snapshot survives the loop, unit or fixed tick.
class RuntimeGroundDepartureQueryBatch {
    const RuntimeGroundDepartureQueryBatch* previous_;
    const RuntimeWindowState* state_;
    const WorldObjectInstance* mover_;
    RuntimeCellPoint origin_;
    const void* objectData_;
    std::size_t objectCount_, behaviorCount_;
    int tick_;
    bool enabled_ = false, air_ = false, moverInfantry_ = false;
    std::array<int, 9> infantryDepartures_{};
    std::array<int, 9> infantryResidents_{};
    std::array<bool, 9> nonInfantryDepartures_{};
    std::array<bool, 9> pendingOmniSegments_{};
    std::array<bool, 9> physicalInfantry_{};
    std::array<bool, 9> physicalVehicles_{};
    mutable std::uint64_t queryHits_ = 0;
    std::uint64_t behaviorVisits_ = 0, residentVisits_ = 0;

    int index(const RuntimeCellPoint& cell) const {
        const auto dx = static_cast<std::int64_t>(cell.x) - origin_.x;
        const auto dy = static_cast<std::int64_t>(cell.y) - origin_.y;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return -1;
        return static_cast<int>((dy + 1) * 3 + dx + 1);
    }
public:
    RuntimeGroundDepartureQueryBatch(const RuntimeWindowState& state,
        const WorldObjectInstance& mover, RuntimeCellPoint origin)
        : previous_(runtimeActiveGroundDepartureQueryBatch), state_(&state), mover_(&mover), origin_(origin),
          objectData_(state.world.getObjects().data()), objectCount_(state.world.getObjects().size()),
          behaviorCount_(state.behaviors.size()), tick_(state.tick) {
        enabled_ = !runtimeGroundDepartureBatchReference() && !runtimeFrameScalingReferenceMovement();
        if (enabled_) {
            air_ = runtimeObjectMovementMode(state, mover) == PathMovementMode::Air;
            moverInfantry_ = mover.kind == WorldObjectKind::Infantry;
            if (!air_) {
                for (const auto& [id, behavior] : state.behaviors) {
                    ++behaviorVisits_;
                    if (id == mover.instanceId) continue;
                    const auto* object = findObjectById(state, id);
                    if (object == nullptr || !object->alive || object->health <= 0
                        || object->kind == WorldObjectKind::Structure
                        || runtimeObjectOffGroundByMagBeam(state, id)
                        || runtimeObjectMovementMode(state, *object) == PathMovementMode::Air) continue;
                    if (runtimeOmniCrushPendingSegment(state, *object, behavior)) {
                        const auto hold = [&](RuntimeCellPoint cell) {
                            const int held = index(cell);
                            if (held >= 0) pendingOmniSegments_[held] = true;
                        };
                        hold(behavior.renderFrom); hold(behavior.renderTo);
                        if (behavior.renderFrom.x != behavior.renderTo.x
                            && behavior.renderFrom.y != behavior.renderTo.y) {
                            hold({behavior.renderFrom.x, behavior.renderTo.y});
                            hold({behavior.renderTo.x, behavior.renderFrom.y});
                        }
                        continue;
                    }
                    if (behavior.moveCooldownTicks <= 0
                        || runtimeCellPointForObject(*object) == behavior.moveSegmentFrom) continue;
                    const int destination = index(behavior.moveSegmentFrom);
                    if (destination < 0) continue;
                    if (object->kind == WorldObjectKind::Infantry) ++infantryDepartures_[destination];
                    else nonInfantryDepartures_[destination] = true;
                }
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    const RuntimeCellPoint cell{origin.x+dx,origin.y+dy};
                    if(runtimeGroundPoseSpatialIndexIsCurrent(state)
                        && (cell.x<0 || cell.y<0 || cell.x>=512 || cell.y>=512)) continue;
                    forEachRuntimeAdditionalGroundPoseOccupantAtCell(state,cell,[&](const auto& object,const auto& held) {
                        if (object.instanceId==mover.instanceId) return;
                        const int destination=index({held.x,held.y});
                        if (destination<0) return;
                        if (object.kind==WorldObjectKind::Infantry) physicalInfantry_[destination]=true;
                        else physicalVehicles_[destination]=true;
                    });
                }
                // The scalar resident scan is conditional on a departure and
                // intentionally has no mag-beam/movement-mode/owner filter.
                if (moverInfantry_ && std::any_of(infantryDepartures_.begin(), infantryDepartures_.end(),
                    [](int count) { return count > 0; })) {
                    for (const auto& other : state.world.getObjects()) {
                        ++residentVisits_;
                        if (!other.alive || other.health <= 0 || other.airborne
                            || other.kind != WorldObjectKind::Infantry || other.instanceId == mover.instanceId) continue;
                        const int destination = index(runtimeCellPointForObject(other));
                        if (destination >= 0) ++infantryResidents_[destination];
                    }
                }
            }
        }
        runtimeActiveGroundDepartureQueryBatch = this;
    }
    ~RuntimeGroundDepartureQueryBatch() { runtimeActiveGroundDepartureQueryBatch = previous_; }
    RuntimeGroundDepartureQueryBatch(const RuntimeGroundDepartureQueryBatch&) = delete;
    RuntimeGroundDepartureQueryBatch& operator=(const RuntimeGroundDepartureQueryBatch&) = delete;
    void invalidate() { enabled_ = false; }
    bool enabled() const { return enabled_; }
    std::uint64_t queryHits() const { return queryHits_; }
    std::uint64_t behaviorVisits() const { return behaviorVisits_; }
    std::uint64_t residentVisits() const { return residentVisits_; }
    std::optional<bool> query(const RuntimeWindowState& state,
        const WorldObjectInstance& mover, const RuntimeCellPoint& cell) const {
        if (!enabled_ || runtimeGroundDepartureBatchReference()
            || state_ != &state || mover_ != &mover || tick_ != state.tick
            || objectData_ != state.world.getObjects().data() || objectCount_ != state.world.getObjects().size()
            || behaviorCount_ != state.behaviors.size()) return std::nullopt;
        const int destination = index(cell);
        if (destination < 0) return std::nullopt;
        ++queryHits_;
        if (air_) return false;
        if (pendingOmniSegments_[destination]) return true;
        if (moverInfantry_) return physicalVehicles_[destination] || nonInfantryDepartures_[destination]
            || (infantryDepartures_[destination] > 0
                && infantryDepartures_[destination] + infantryResidents_[destination] >= 3);
        return physicalInfantry_[destination] || physicalVehicles_[destination]
            || infantryDepartures_[destination] > 0;
    }
};

bool runtimeGroundDepartureHoldsCell(const RuntimeWindowState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& cell) {
    if (runtimeActiveGroundDepartureQueryBatch != nullptr) {
        const auto cached = runtimeActiveGroundDepartureQueryBatch->query(state, mover, cell);
        if (cached.has_value()) return *cached;
    }
    if (runtimeObjectMovementMode(state, mover) == PathMovementMode::Air) return false;
    int departingInfantry=0;
    for (const auto& [id, behavior] : state.behaviors) {
        if (id == mover.instanceId) continue;
        const auto* object = findObjectById(state, id);
        if (object == nullptr || !object->alive || object->health <= 0
            || object->kind == WorldObjectKind::Structure
            || runtimeObjectOffGroundByMagBeam(state, id)
            || runtimeObjectMovementMode(state, *object) == PathMovementMode::Air)
            continue;
        if (runtimeOmniCrushPendingSegment(state, *object, behavior)) {
            if (runtimeOmniCrushPendingSegmentContainsCell(state, *object, behavior, cell)) return true;
            continue;
        }
        if (behavior.moveCooldownTicks <= 0 || behavior.moveSegmentFrom != cell) continue;
        if (runtimeCellPointForObject(*object) == cell) continue;
        const bool infantry=object->kind==WorldObjectKind::Infantry;
        if(infantry!=(mover.kind==WorldObjectKind::Infantry)) return true;
        if(infantry) ++departingInfantry;
    }
    bool physicalBlocked=false;
    forEachRuntimeAdditionalGroundPoseOccupantAtCell(state,cell,[&](const auto& object,const auto& held) {
        if (object.instanceId==mover.instanceId) return;
        // An infantry body keeps one slot, not the entire tile. Subcell
        // allocation/commit handles its capacity and exact held slot.
        if (object.kind!=WorldObjectKind::Infantry || mover.kind!=WorldObjectKind::Infantry)
            physicalBlocked=true;
    });
    if (physicalBlocked) return true;
    if(departingInfantry==0) return false;
    for(const auto& other : state.world.getObjects()) {
        if(other.alive && other.health>0 && !other.airborne
            && other.kind==WorldObjectKind::Infantry && other.instanceId!=mover.instanceId
            && runtimeCellPointForObject(other)==cell) ++departingInfantry;
    }
    return departingInfantry>=3;
}

// Shared infantry capacity does not reserve an entire tile. Serialize only
// strictly crossing body paths, before a second accepted segment can deadlock
// both bodies at their spacing limit. Parallel paths and shared endpoints keep
// the existing three-subcell admission rules.
bool runtimeInfantryActiveSegmentCrossesTransition(const RuntimeWindowState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::optional<int> selectedDestinationSubCell=std::nullopt) {
    if(mover.kind!=WorldObjectKind::Infantry || from==to || mover.airborne
        || runtimeObjectOffGroundByMagBeam(state,mover.instanceId)
        || runtimeObjectMovementMode(state,mover)==PathMovementMode::Air) return false;
    using Point=std::array<double,2>;using Path=std::array<Point,3>;
    const auto oldSlot=worldObjectSubCellOffset(mover);
    const bool stopped=worldObjectHasStoppedGroundPose(mover);
    const Point start{(stopped?mover.renderX:double(from.x))+oldSlot.x,
        (stopped?mover.renderY:double(from.y))+oldSlot.y};
    // The committed destination can use a different slot. Preview its actual
    // body route while retaining the source body's current physical position.
    const bool slotlessEntry=runtimeInfantryBodyUsesAuthorizedSlotlessEntry(state,mover,to);
    if(selectedDestinationSubCell && !slotlessEntry
        && !RuntimeSubcellSystemState::isActiveSubCell(*selectedDestinationSubCell)) return false;
    int nextSubCell=slotlessEntry?-1:(selectedDestinationSubCell?*selectedDestinationSubCell:mover.subCell);
    const auto reserved=state.subcells.reservations().find(mover.instanceId);
    const auto destination=runtimeSubcellLocationForCell(state,to);
    if(!selectedDestinationSubCell && !slotlessEntry && reserved!=state.subcells.reservations().end()
        && reserved->second.destination.x==destination.x
        && reserved->second.destination.y==destination.y
        && reserved->second.destination.cell==destination.cell
        && reserved->second.destination.onBridge==destination.onBridge
        && RuntimeSubcellSystemState::isActiveSubCell(reserved->second.subCell))
        nextSubCell=reserved->second.subCell;
    const auto nextSlot=worldInfantrySubCellOffset(nextSubCell);
    const Point pivot{from.x+nextSlot.x,from.y+nextSlot.y},end{to.x+nextSlot.x,to.y+nextSlot.y};
    const double fromOffsetX=start[0]-pivot[0],fromOffsetY=start[1]-pivot[1];
    const bool corner=!runtimeInfantryOffsetStartsAtSourceSubcell(nextSubCell,fromOffsetX,fromOffsetY)
        && std::abs(fromOffsetX*(to.y-from.y)-fromOffsetY*(to.x-from.x))>1e-9;
    const Path proposed{start,corner?pivot:start,end};
    const bool layer=worldObjectHasGroundRenderPose(mover)?mover.renderOnBridge:mover.onBridge;
    const auto cross=[](const Point& a,const Point& b,const Point& p) {
        return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]);
    };
    const auto opposite=[](double a,double b) {return (a>1e-9 && b<-1e-9)||(a<-1e-9 && b>1e-9);};
    const auto intersects=[&](const Point& a,const Point& b,const Point& c,const Point& d) {
        return opposite(cross(a,b,c),cross(a,b,d)) && opposite(cross(c,d,a),cross(c,d,b));
    };
    bool blocked=false;
    const auto inspect=[&](const WorldObjectInstance& other) {
        if(blocked || other.instanceId==mover.instanceId || other.kind!=WorldObjectKind::Infantry
            || !other.renderMoveActive || !runtimeGroundPoseIsSpatialCandidate(state,other)
            || layer!=other.renderOnBridge) return;
        if(runtimeCrusherUsesCrushingMovement(state,mover)
            && runtimeCrusherCanCrushObject(state,mover,other,runtimeCellPointForObject(other),true)) return;
        const auto found=state.behaviors.find(other.instanceId);
        if(found==state.behaviors.end()) return;
        const auto& move=found->second;
        if(move.renderMoveTicksTotal<=0 || move.renderMoveTicksRemaining<=0) return;
        const auto otherSlot=worldObjectSubCellOffset(other);
        const Point now{other.renderX+otherSlot.x,other.renderY+otherSlot.y};
        const Point target{move.renderTo.x+move.renderToOffsetX+otherSlot.x,
            move.renderTo.y+move.renderToOffsetY+otherSlot.y};
        Point remainingPivot=now;
        if(runtimeInfantryMoveUsesCommittedCorner(other,move)) {
            const double first=std::hypot(move.renderFromOffsetX,move.renderFromOffsetY);
            const double second=std::hypot(double(move.renderTo.x-move.renderFrom.x),
                double(move.renderTo.y-move.renderFrom.y));
            const int ticks=std::max(2,move.renderMoveTicksTotal);
            const int firstTicks=std::clamp(static_cast<int>(std::ceil(ticks*first/(first+second))),1,ticks-1);
            if(ticks*runtimeVehicleMoveRenderProgress(move)<firstTicks)
                remainingPivot={move.renderFrom.x+otherSlot.x,move.renderFrom.y+otherSlot.y};
        }
        const Path remaining{now,remainingPivot,target};
        for(int a=0;a<2 && !blocked;++a) for(int b=0;b<2 && !blocked;++b)
            blocked=intersects(proposed[a],proposed[a+1],remaining[b],remaining[b+1]);
    };
    double minX=start[0],maxX=start[0],minY=start[1],maxY=start[1];
    for(const auto& point:proposed) {
        minX=std::min(minX,point[0]);maxX=std::max(maxX,point[0]);
        minY=std::min(minY,point[1]);maxY=std::max(maxY,point[1]);
    }
    const double left=std::floor(minX+0.5),right=std::floor(maxX+0.5);
    const double top=std::floor(minY+0.5),bottom=std::floor(maxY+0.5);
    if(runtimeGroundPoseSpatialIndexIsCurrent(state) && std::isfinite(left) && std::isfinite(right)
        && std::isfinite(top) && std::isfinite(bottom) && left>=0 && top>=0 && right<512 && bottom<512
        && (right-left+1)*(bottom-top+1)<=4096) {
        for(int y=static_cast<int>(top);y<=static_cast<int>(bottom) && !blocked;++y)
            for(int x=static_cast<int>(left);x<=static_cast<int>(right) && !blocked;++x)
                forEachRuntimeGroundPoseCandidateAtCell(state,{x,y},inspect);
    } else {
        for(const auto& object:state.world.getObjects()) inspect(object);
    }
    return blocked;
}

// A dedicated factory segment keeps its committed linear clock. Wait before
// committing the cell when its complete body route cannot clear outside traffic.
bool runtimeFactoryGroundSegmentAdmissionBlocked(const RuntimeWindowState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::size_t* candidateVisits=nullptr) {
    const auto active=state.behaviors.find(mover.instanceId);
    if(state.factoryDeliveries.leases().empty()
        && (active==state.behaviors.end() || active->second.factoryExitClearanceHostId<0)) return false;
    if(from==to || !runtimeFactoryLandVehicleEligible(state,mover)
        || (!runtimeFactoryLandDeliveryOwnsEgress(state,mover)
            && (active==state.behaviors.end() || !runtimeFactoryFastClearanceMove(state,mover,active->second)))) return false;
    using Point=std::array<double,2>;
    struct Leg {Point from,to;bool onBridge;};
    const auto dot=[](const Point& a,const Point& b) {return a[0]*b[0]+a[1]*b[1];};
    const auto delta=[](const Point& a,const Point& b) {return Point{a[0]-b[0],a[1]-b[1]};};
    const auto cross=[](const Point& a,const Point& b) {return a[0]*b[1]-a[1]*b[0];};
    const auto pointDistance=[&](const Point& p,const Point& a,const Point& b) {
        const auto direction=delta(b,a);const double length=dot(direction,direction);
        const double part=length>1e-18?std::clamp(dot(delta(p,a),direction)/length,0.0,1.0):0.0;
        const Point nearest{a[0]+direction[0]*part,a[1]+direction[1]*part};
        const auto distance=delta(p,nearest);return dot(distance,distance);
    };
    const auto segmentDistance=[&](const Point& a,const Point& b,const Point& c,const Point& d) {
        const auto first=delta(b,a),second=delta(d,c),between=delta(c,a);
        const double denominator=cross(first,second);
        if(std::abs(denominator)>1e-12) {
            const double t=cross(between,second)/denominator,u=cross(between,first)/denominator;
            if(t>=0 && t<=1 && u>=0 && u<=1) return 0.0;
        }
        return std::min({pointDistance(a,c,d),pointDistance(b,c,d),
            pointDistance(c,a,b),pointDistance(d,a,b)});
    };
    const auto offset=worldObjectSubCellOffset(mover);
    const bool stopped=worldObjectHasStoppedGroundPose(mover);
    const Point start{(stopped?mover.renderX:double(from.x))+offset.x,
        (stopped?mover.renderY:double(from.y))+offset.y};
    const Point end{to.x+offset.x,to.y+offset.y},direction=delta(end,start);
    const bool fromLayer=worldObjectHasGroundRenderPose(mover)?mover.renderOnBridge:mover.onBridge;
    const bool toLayer=runtimeCellIsBridge(state,to.x,to.y);
    std::array<Leg,2> proposed;int proposedCount=1;
    proposed[0]={start,end,fromLayer};
    if(fromLayer!=toLayer) {
        const Point middle{(start[0]+end[0])*0.5,(start[1]+end[1])*0.5};
        proposed[0]={start,middle,fromLayer};proposed[1]={middle,end,toLayer};proposedCount=2;
    }
    bool blocked=false;std::unordered_set<const WorldObjectInstance*> seen;
    const auto inspect=[&](const WorldObjectInstance& other) {
        if(blocked || other.instanceId==mover.instanceId || !seen.insert(&other).second) return;
        if(candidateVisits) ++*candidateVisits;
        if(!runtimeAllGroundBodyIsSpatialCandidate(state,other)) return;
        if(runtimeCrusherUsesCrushingMovement(state,mover)
            && runtimeCrusherCanCrushObject(state,mover,other,runtimeCellPointForObject(other),true)) return;
        const auto slot=worldObjectSubCellOffset(other);
        const Point now{runtimeObjectRenderX(other)+slot.x,runtimeObjectRenderY(other)+slot.y};
        if(!std::isfinite(now[0]) || !std::isfinite(now[1])) {blocked=true;return;}
        const bool currentLayer=other.hasRenderPosition?other.renderOnBridge:other.onBridge;
        const double minimum=other.kind==WorldObjectKind::Infantry?0.75:1.0;
        const auto separation=delta(start,now);const double currentDistance=dot(separation,separation);
        // Already-overlapping bodies can depart monotonically away. This is the
        // same escape allowed by the per-tick guard, without accelerating clocks.
        const bool escaping=currentLayer==fromLayer && currentDistance+1e-9<minimum*minimum
            && dot(separation,direction)>=-1e-9 && dot(delta(end,now),delta(end,now))>currentDistance+1e-9;
        for(int n=0;n<proposedCount;++n) if(proposed[n].onBridge==currentLayer
            && pointDistance(now,proposed[n].from,proposed[n].to)+1e-9<minimum*minimum && !escaping) {
            blocked=true;return;
        }
        const auto found=state.behaviors.find(other.instanceId);
        if(!other.renderMoveActive || found==state.behaviors.end()) return;
        const auto& move=found->second;
        if(move.renderMoveTicksRemaining<=0 || move.renderMoveTicksTotal<=0) return;
        const double progress=runtimeVehicleMoveRenderProgress(move);
        std::array<double,4> cuts{progress,1.0,1.0,1.0};int count=2;
        if(move.renderFromOnBridge!=move.renderToOnBridge && progress<0.5) cuts[count++]=0.5;
        if(runtimeInfantryMoveUsesCommittedCorner(other,move)) {
            const double first=std::hypot(move.renderFromOffsetX,move.renderFromOffsetY);
            const double second=std::hypot(double(move.renderTo.x-move.renderFrom.x),double(move.renderTo.y-move.renderFrom.y));
            const int ticks=std::max(2,move.renderMoveTicksTotal);
            const int firstTicks=std::clamp(static_cast<int>(std::ceil(ticks*first/(first+second))),1,ticks-1);
            const double pivot=double(firstTicks)/ticks;if(progress<pivot) cuts[count++]=pivot;
        }
        std::sort(cuts.begin(),cuts.begin()+count);
        std::array<Leg,3> remaining;int remainingCount=0;Point previous=now;
        bool behind=proposedCount==1 && currentLayer==fromLayer
            && dot(delta(now,start),direction)<=1e-9 && std::abs(cross(direction,delta(now,start)))<=1e-9;
        for(int n=1;n<count;++n) {
            const auto base=runtimeGroundRenderMovePoint(other,move,cuts[n]);
            const Point next{base[0]+slot.x,base[1]+slot.y};
            const bool layer=(cuts[n-1]+cuts[n])*0.5<0.5?move.renderFromOnBridge:move.renderToOnBridge;
            remaining[remainingCount++]={previous,next,layer};
            behind=behind && layer==fromLayer && dot(delta(next,start),direction)<=1e-9
                && std::abs(cross(direction,delta(next,start)))<=1e-9
                && dot(delta(next,previous),direction)>=-1e-9;
            previous=next;
        }
        // A following accepted route ending behind this start cannot reserve
        // the front car's departure forever through a shared endpoint. Its own
        // body guard still prevents it from catching the departing front car.
        if(behind) return;
        for(int a=0;a<proposedCount && !blocked;++a) for(int b=0;b<remainingCount && !blocked;++b)
            if(proposed[a].onBridge==remaining[b].onBridge
                && segmentDistance(proposed[a].from,proposed[a].to,remaining[b].from,remaining[b].to)+1e-9<minimum*minimum)
                blocked=true;
    };
    const double left=std::floor(std::min(start[0],end[0]))-1,right=std::ceil(std::max(start[0],end[0]))+1;
    const double top=std::floor(std::min(start[1],end[1]))-1,bottom=std::ceil(std::max(start[1],end[1]))+1;
    if(runtimeAllGroundBodySpatialIndexIsCurrent(state) && std::isfinite(left) && std::isfinite(right)
        && std::isfinite(top) && std::isfinite(bottom) && left>=0 && top>=0 && right<512 && bottom<512
        && (right-left+1)*(bottom-top+1)<=4096) {
        for(int y=static_cast<int>(top);y<=static_cast<int>(bottom) && !blocked;++y)
            for(int x=static_cast<int>(left);x<=static_cast<int>(right) && !blocked;++x)
                forEachRuntimeAllGroundBodyCandidateAtCell(state,{x,y},inspect);
    } else {
        for(const auto& object:state.world.getObjects()) inspect(object);
    }
    return blocked;
}

// Match reserveBatch's actual current-or-first-available slot without mutating leases.
int runtimeInfantryBodyPreviewDestinationSubcell(const RuntimeWindowState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& to) {
    if(runtimeInfantryBodyUsesAuthorizedSlotlessEntry(state,mover,to)) return -1;
    const auto destination=runtimeSubcellLocationForCell(state,to);
    const auto reserved=state.subcells.reservations().find(mover.instanceId);
    if(reserved!=state.subcells.reservations().end()
        && reserved->second.destination.x==destination.x && reserved->second.destination.y==destination.y
        && reserved->second.destination.cell==destination.cell && reserved->second.destination.onBridge==destination.onBridge
        && RuntimeSubcellSystemState::isActiveSubCell(reserved->second.subCell)) return reserved->second.subCell;
    const auto available=runtimeAvailableInfantrySubcells(state,mover,to);
    return RuntimeSubcellSystemState::selectAvailableSubCell(available,mover.subCell);
}

// Reject a body route before its logical cell/subcell commits. The late guard
// remains a floor, but cannot reroute an already committed segment through an idle resident.
bool runtimeInfantryBodySegmentAdmissionBlocked(const RuntimeWindowState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    std::size_t* candidateVisits=nullptr,int forcedSubCell=-1,std::vector<int>* blockingObjectIds=nullptr,
    std::optional<int> selectedDestinationSubCell=std::nullopt) {
    if(blockingObjectIds) blockingObjectIds->clear();
    if(mover.kind!=WorldObjectKind::Infantry || mover.airborne || from==to
        || runtimeObjectOffGroundByMagBeam(state,mover.instanceId)
        || runtimeObjectMovementMode(state,mover)==PathMovementMode::Air) return false;
    const int entryHostId=runtimeInfantryAuthorizedSlotlessEntryHostId(state,mover,to);
    const bool slotlessEntry=entryHostId>=0;
    const int nextSubCell=slotlessEntry?-1:(selectedDestinationSubCell?*selectedDestinationSubCell
        :(forcedSubCell>=0?forcedSubCell:runtimeInfantryBodyPreviewDestinationSubcell(state,mover,to)));
    if(!RuntimeSubcellSystemState::isActiveSubCell(nextSubCell) && !slotlessEntry) return true;
    using Point=std::array<double,2>;struct Leg {Point from,to;bool onBridge;};
    const auto dot=[](const Point& a,const Point& b) {return a[0]*b[0]+a[1]*b[1];};
    const auto delta=[](const Point& a,const Point& b) {return Point{a[0]-b[0],a[1]-b[1]};};
    const auto cross=[](const Point& a,const Point& b) {return a[0]*b[1]-a[1]*b[0];};
    const auto pointDistance=[&](const Point& p,const Point& a,const Point& b) {
        const auto direction=delta(b,a);const double length=dot(direction,direction);
        const double part=length>1e-18?std::clamp(dot(delta(p,a),direction)/length,0.0,1.0):0.0;
        const Point nearest{a[0]+direction[0]*part,a[1]+direction[1]*part};
        const auto distance=delta(p,nearest);return dot(distance,distance);
    };
    const auto segmentDistance=[&](const Point& a,const Point& b,const Point& c,const Point& d) {
        const auto first=delta(b,a),second=delta(d,c),between=delta(c,a);
        const double denominator=cross(first,second);
        if(std::abs(denominator)>1e-12) {
            const double t=cross(between,second)/denominator,u=cross(between,first)/denominator;
            if(t>=0 && t<=1 && u>=0 && u<=1) return 0.0;
        }
        return std::min({pointDistance(a,c,d),pointDistance(b,c,d),pointDistance(c,a,b),pointDistance(d,a,b)});
    };
    const auto oldSlot=worldObjectSubCellOffset(mover),slot=worldInfantrySubCellOffset(nextSubCell);
    const bool stopped=worldObjectHasStoppedGroundPose(mover);
    const Point start{(stopped?mover.renderX:double(from.x))+oldSlot.x,
        (stopped?mover.renderY:double(from.y))+oldSlot.y};
    const Point pivot{from.x+slot.x,from.y+slot.y},end{to.x+slot.x,to.y+slot.y};
    const auto offset=delta(start,pivot),direction=delta(end,start);
    const bool corner=!runtimeInfantryOffsetStartsAtSourceSubcell(nextSubCell,offset[0],offset[1])
        && std::abs(offset[0]*(to.y-from.y)-offset[1]*(to.x-from.x))>1e-9;
    const bool fromLayer=worldObjectHasGroundRenderPose(mover)?mover.renderOnBridge:mover.onBridge;
    const bool toLayer=runtimeCellIsBridge(state,to.x,to.y);
    // Foot clocks have no vehicle arrival or docking scale. Read, never consume,
    // their interval remainder to split the same corner/layer curve as begin/advance.
    const auto behavior=state.behaviors.find(mover.instanceId);
    const std::int64_t remainder=behavior==state.behaviors.end()?0:behavior->second.moveIntervalRemainderQ16;
    const double q16=std::clamp(runtimeMovementSegmentIntervalQ16(state,mover,from,to)
        /std::max(1e-9,runtimeTerrainMovementSpeedMultiplier(state,mover,from,to)),65536.0,
        double(std::numeric_limits<int>::max()-1)*65536.0);
    int total=static_cast<int>(std::clamp((std::llround(q16)+remainder+32768)/65536,
        std::int64_t(1),std::int64_t(std::numeric_limits<int>::max()-1)));
    const double first=std::hypot(offset[0],offset[1]);
    const double second=std::hypot(double(to.x-from.x),double(to.y-from.y));
    double firstPart=0.0;
    if(corner) {
        total=static_cast<int>(std::clamp(std::ceil(total*(first+second)/second),1.0,
            double(std::numeric_limits<int>::max()-1)));
        const int ticks=std::max(2,total);
        firstPart=double(std::clamp(static_cast<int>(std::ceil(ticks*first/(first+second))),1,ticks-1))/ticks;
    }
    const auto proposedPoint=[&](double progress) {
        if(!corner) return Point{start[0]+direction[0]*progress,start[1]+direction[1]*progress};
        if(progress<firstPart) {const double part=progress/firstPart;
            return Point{start[0]+(pivot[0]-start[0])*part,start[1]+(pivot[1]-start[1])*part};}
        const double part=(progress-firstPart)/(1.0-firstPart);
        return Point{pivot[0]+(end[0]-pivot[0])*part,pivot[1]+(end[1]-pivot[1])*part};
    };
    std::array<double,4> cuts{0.0,1.0,1.0,1.0};int cutCount=2;
    if(corner) cuts[cutCount++]=firstPart;
    if(fromLayer!=toLayer) cuts[cutCount++]=0.5;
    std::sort(cuts.begin(),cuts.begin()+cutCount);
    std::array<Leg,3> proposed;int proposedCount=0;
    for(int n=1;n<cutCount;++n) if(cuts[n]>cuts[n-1])
        proposed[proposedCount++]={proposedPoint(cuts[n-1]),proposedPoint(cuts[n]),
            (cuts[n-1]+cuts[n])*0.5<0.5?fromLayer:toLayer};
    bool blocked=false;std::unordered_set<const WorldObjectInstance*> seen;
    const auto recordBlock=[&](const WorldObjectInstance& other) {
        blocked=true;if(blockingObjectIds) blockingObjectIds->push_back(other.instanceId);
    };
    const auto inspect=[&](const WorldObjectInstance& other) {
        if((blocked && !blockingObjectIds) || other.instanceId==mover.instanceId || other.instanceId==entryHostId
            || !seen.insert(&other).second) return;
        if(candidateVisits) ++*candidateVisits;
        if(!runtimeAllGroundBodyIsSpatialCandidate(state,other) || other.factoryExitHostId>=0) return;
        if(runtimeCrusherUsesCrushingMovement(state,mover)
            && runtimeCrusherCanCrushObject(state,mover,other,runtimeCellPointForObject(other),true)) return;
        const auto otherSlot=worldObjectSubCellOffset(other);
        const Point now{runtimeObjectRenderX(other)+otherSlot.x,runtimeObjectRenderY(other)+otherSlot.y};
        if(!std::isfinite(now[0]) || !std::isfinite(now[1])) {recordBlock(other);return;}
        const bool currentLayer=other.hasRenderPosition?other.renderOnBridge:other.onBridge;
        const double minimum=other.kind==WorldObjectKind::Infantry?0.5:0.75;
        const auto separation=delta(start,now);const double currentDistance=dot(separation,separation);
        // An accepted straight front segment keeps moving away from a normal
        // follower. Its final body must clear this follower's complete route.
        // Validate before checking its current point, which can equal our end.
        const auto front=state.behaviors.find(other.instanceId);
        if(proposedCount==1 && other.renderMoveActive && front!=state.behaviors.end()
            && front->second.renderMoveTicksRemaining>0 && front->second.renderMoveTicksTotal>0
            && currentLayer==fromLayer && currentDistance+1e-9>=minimum*minimum
            && dot(delta(now,start),direction)>0.0 && std::abs(cross(direction,delta(now,start)))<=1e-9) {
            const auto& ahead=front->second;
            const Point final{ahead.renderTo.x+ahead.renderToOffsetX+otherSlot.x,
                ahead.renderTo.y+ahead.renderToOffsetY+otherSlot.y};
            if(!runtimeInfantryMoveUsesCommittedCorner(other,ahead)
                && ahead.renderFromOnBridge==fromLayer && ahead.renderToOnBridge==fromLayer
                && std::abs(cross(direction,delta(final,start)))<=1e-9
                && dot(delta(final,now),direction)>=0.0
                && dot(delta(final,end),direction)+1e-9>=minimum*std::hypot(direction[0],direction[1])) return;
        }
        bool escaping=currentLayer==fromLayer && currentDistance+1e-9<minimum*minimum;
        Point escapePrevious=start;
        for(int n=0;n<proposedCount && escaping;++n) {
            const auto away=delta(escapePrevious,now),travel=delta(proposed[n].to,escapePrevious);
            escaping=proposed[n].onBridge==currentLayer && dot(away,travel)>=-1e-9;
            escapePrevious=proposed[n].to;
        }
        escaping=escaping && dot(delta(end,now),delta(end,now))>currentDistance+1e-9;
        for(int n=0;n<proposedCount;++n) if(proposed[n].onBridge==currentLayer
            && pointDistance(now,proposed[n].from,proposed[n].to)+1e-9<minimum*minimum && !escaping) {
            recordBlock(other);return;
        }
        const auto found=state.behaviors.find(other.instanceId);
        if(!other.renderMoveActive || found==state.behaviors.end()) return;
        const auto& move=found->second;
        if(move.renderMoveTicksRemaining<=0 || move.renderMoveTicksTotal<=0) return;
        const double progress=runtimeVehicleMoveRenderProgress(move);
        std::array<double,4> remainingCuts{progress,1.0,1.0,1.0};int count=2;
        if(move.renderFromOnBridge!=move.renderToOnBridge && progress<0.5) remainingCuts[count++]=0.5;
        if(runtimeInfantryMoveUsesCommittedCorner(other,move)) {
            const double a=std::hypot(move.renderFromOffsetX,move.renderFromOffsetY);
            const double b=std::hypot(double(move.renderTo.x-move.renderFrom.x),double(move.renderTo.y-move.renderFrom.y));
            const int ticks=std::max(2,move.renderMoveTicksTotal);
            const double part=double(std::clamp(static_cast<int>(std::ceil(ticks*a/(a+b))),1,ticks-1))/ticks;
            if(progress<part) remainingCuts[count++]=part;
        }
        std::sort(remainingCuts.begin(),remainingCuts.begin()+count);
        std::array<Leg,3> remaining;int remainingCount=0;Point previous=now;
        // A straight departure moves monotonically away from every point in
        // this rear half-plane. Let it clear a follower's shared endpoint.
        bool behind=proposedCount==1 && currentLayer==fromLayer
            && currentDistance+1e-9>=minimum*minimum && dot(delta(now,start),direction)<=1e-9;
        for(int n=1;n<count;++n) if(remainingCuts[n]>remainingCuts[n-1]) {
            const auto base=runtimeGroundRenderMovePoint(other,move,remainingCuts[n]);
            const Point next{base[0]+otherSlot.x,base[1]+otherSlot.y};
            const bool layer=(remainingCuts[n-1]+remainingCuts[n])*0.5<0.5?move.renderFromOnBridge:move.renderToOnBridge;
            remaining[remainingCount++]={previous,next,layer};
            behind=behind && layer==fromLayer && dot(delta(next,start),direction)<=1e-9;
            previous=next;
        }
        if(behind) return;
        bool objectBlocked=false;
        for(int a=0;a<proposedCount && !objectBlocked;++a) for(int b=0;b<remainingCount && !objectBlocked;++b)
            if(proposed[a].onBridge==remaining[b].onBridge
                && segmentDistance(proposed[a].from,proposed[a].to,remaining[b].from,remaining[b].to)+1e-9<minimum*minimum)
                objectBlocked=true;
        if(objectBlocked) recordBlock(other);
    };
    double minX=start[0],maxX=start[0],minY=start[1],maxY=start[1];
    for(int n=0;n<proposedCount;++n) for(const auto& point:{proposed[n].from,proposed[n].to}) {
        minX=std::min(minX,point[0]);maxX=std::max(maxX,point[0]);minY=std::min(minY,point[1]);maxY=std::max(maxY,point[1]);
    }
    const double left=std::floor(minX)-1,right=std::ceil(maxX)+1,top=std::floor(minY)-1,bottom=std::ceil(maxY)+1;
    if(runtimeAllGroundBodySpatialIndexIsCurrent(state) && std::isfinite(left) && std::isfinite(right)
        && std::isfinite(top) && std::isfinite(bottom) && left>=0 && top>=0 && right<512 && bottom<512
        && (right-left+1)*(bottom-top+1)<=4096) {
        for(int y=static_cast<int>(top);y<=static_cast<int>(bottom) && (!blocked || blockingObjectIds);++y)
            for(int x=static_cast<int>(left);x<=static_cast<int>(right) && (!blocked || blockingObjectIds);++x)
                forEachRuntimeAllGroundBodyCandidateAtCell(state,{x,y},inspect);
    } else {
        for(const auto& object:state.world.getObjects()) inspect(object);
    }
    return blocked;
}
// Virtual-cell searches have already selected the current-or-first-available slot. Do not reuse
// a real object's old reservation as if it survived an earlier virtual step.
bool runtimeInfantryBodySegmentAdmissionBlockedAtSubcell(const RuntimeWindowState& state,
    const WorldObjectInstance& mover,const RuntimeCellPoint& from,const RuntimeCellPoint& to,
    int targetSubCell,std::vector<int>* blockingObjectIds=nullptr) {
    return runtimeInfantryBodySegmentAdmissionBlocked(state,mover,from,to,nullptr,targetSubCell,blockingObjectIds);
}

bool runtimeGroundDepartureHoldsTransition(const RuntimeWindowState& state,
    const WorldObjectInstance& mover, const RuntimeCellPoint& from, const RuntimeCellPoint& to,
    std::optional<int> selectedDestinationSubCell=std::nullopt) {
    return runtimeFactoryGroundSegmentAdmissionBlocked(state,mover,from,to)
        || runtimeInfantryActiveSegmentCrossesTransition(state,mover,from,to,selectedDestinationSubCell)
        || runtimeGroundDepartureHoldsCell(state,mover,to)
        || (from.x!=to.x && from.y!=to.y
            && (runtimeGroundDepartureHoldsCell(state,mover,{from.x,to.y})
                || runtimeGroundDepartureHoldsCell(state,mover,{to.x,from.y})));
}
