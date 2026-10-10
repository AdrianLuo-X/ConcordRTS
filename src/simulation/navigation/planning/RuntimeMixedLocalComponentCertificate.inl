// Included immediately before issueRuntimeMoveCommand, after the complete BFS.
// An owned, command-local SCC certificate, never a partial component map.
// The caller must keep the admitted graph inputs unchanged during assignment.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <new>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

inline thread_local int runtimeMixedLocalComponentReferenceOverride=-1;
inline bool runtimeMixedLocalComponentReference() noexcept {
    if(runtimeMixedLocalComponentReferenceOverride>=0)
        return runtimeMixedLocalComponentReferenceOverride!=0;
    static const bool value=[] { const char* p=std::getenv("CONCORD_MOVE_LOCAL_COMPONENT_REFERENCE");
        return p && p[0]=='1' && p[1]=='\0'; }();
    return value;
}
class RuntimeMixedLocalComponentReferenceScope {
    int previous_;
public:
    explicit RuntimeMixedLocalComponentReferenceScope(bool reference) noexcept
        : previous_(runtimeMixedLocalComponentReferenceOverride) {
        runtimeMixedLocalComponentReferenceOverride=reference?1:0;
    }
    ~RuntimeMixedLocalComponentReferenceScope() { runtimeMixedLocalComponentReferenceOverride=previous_; }
    RuntimeMixedLocalComponentReferenceScope(const RuntimeMixedLocalComponentReferenceScope&)=delete;
    RuntimeMixedLocalComponentReferenceScope& operator=(const RuntimeMixedLocalComponentReferenceScope&)=delete;
};
inline thread_local int runtimeMixedKnownUnmappedReferenceOverride=-1;
inline bool runtimeMixedKnownUnmappedReference() noexcept {
    if(runtimeMixedKnownUnmappedReferenceOverride>=0)
        return runtimeMixedKnownUnmappedReferenceOverride!=0;
    static const bool value=[] {const char* p=std::getenv("CONCORD_MOVE_KNOWN_UNMAPPED_REFERENCE");
        return p && p[0]=='1' && p[1]=='\0';}();
    return value;
}
class RuntimeMixedKnownUnmappedReferenceScope {
    int previous_;
public:
    explicit RuntimeMixedKnownUnmappedReferenceScope(bool reference) noexcept
        :previous_(runtimeMixedKnownUnmappedReferenceOverride) {
        runtimeMixedKnownUnmappedReferenceOverride=reference?1:0;
    }
    ~RuntimeMixedKnownUnmappedReferenceScope(){runtimeMixedKnownUnmappedReferenceOverride=previous_;}
    RuntimeMixedKnownUnmappedReferenceScope(const RuntimeMixedKnownUnmappedReferenceScope&)=delete;
    RuntimeMixedKnownUnmappedReferenceScope& operator=(const RuntimeMixedKnownUnmappedReferenceScope&)=delete;
};

// Semantic control: recover C38's prelisted-centre-only coverage admission.
inline thread_local int runtimeMixedBestFrontierClosureReferenceOverride=-1;
inline bool runtimeMixedBestFrontierClosureReference() noexcept {
    if(runtimeMixedBestFrontierClosureReferenceOverride>=0)
        return runtimeMixedBestFrontierClosureReferenceOverride!=0;
    static const bool value=[] {const char* p=std::getenv("CONCORD_MOVE_BEST_FRONTIER_CLOSURE_REFERENCE");
        return p && p[0]=='1' && p[1]=='\0';}();
    return value;
}
class RuntimeMixedBestFrontierClosureReferenceScope {
    int previous_;
public:
    explicit RuntimeMixedBestFrontierClosureReferenceScope(bool reference) noexcept
        :previous_(runtimeMixedBestFrontierClosureReferenceOverride) {
        runtimeMixedBestFrontierClosureReferenceOverride=reference?1:0;
    }
    ~RuntimeMixedBestFrontierClosureReferenceScope(){runtimeMixedBestFrontierClosureReferenceOverride=previous_;}
    RuntimeMixedBestFrontierClosureReferenceScope(const RuntimeMixedBestFrontierClosureReferenceScope&)=delete;
    RuntimeMixedBestFrontierClosureReferenceScope& operator=(const RuntimeMixedBestFrontierClosureReferenceScope&)=delete;
};

struct RuntimeMixedLocalComponentStats {
    std::size_t attempts=0,admitted=0,referenceFallbacks=0,guardFallbacks=0;
    std::size_t layoutFallbacks=0,walkFallbacks=0,allocationFallbacks=0;
    std::size_t layoutEntries=0,vertices=0,orientedEdges=0,coveredBest=0;
    std::size_t connectedGoalHits=0,walkSteps=0;
    std::size_t detourExpansions=0,seedVetoes=0,edgeVetoes=0,qualifiedChronoRecovery=0,idleActors=0;
    std::size_t unmappedAttempts=0,unmappedProofs=0,unmappedMemoHits=0;
    std::size_t incomingQueries=0,incomingMemoHits=0,incomingPositiveVetoes=0;
    std::size_t unmappedOriginalChecks=0,unmappedOriginalMismatches=0;
    std::size_t frontierClosureAttempts=0,frontierClosureAdmissions=0,frontierClosureMemoHits=0;
    std::size_t frontierClosureRejected=0,frontierClosurePoints=0;
    std::size_t frontierClosureReferenceVetoes=0,frontierClosureAllocationFallbacks=0;
    int rejectionReason=0,rejectionActor=-1;
    std::size_t outsideFallbacks=0,nearestFallbacks=0,wholeMaps=0,lateWholeMaps=0;
    std::size_t orderedMapsCompared=0,orderedMapMismatches=0,lateChronoWitness=0,lateAirWitness=0;
};
// Contract hooks supply owned expected output, never a callback or World writer.
inline thread_local RuntimeMixedLocalComponentStats* runtimeMixedLocalComponentStats=nullptr;
inline thread_local const std::vector<std::pair<int,int>>* runtimeMixedLocalComponentExpectedMap=nullptr;
inline thread_local std::size_t runtimeMixedLocalComponentFormationLimit=256;
inline thread_local bool runtimeMixedLocalComponentProofActive=false;
class RuntimeMixedLocalComponentStatsScope {
    RuntimeMixedLocalComponentStats* previous_;
    const std::vector<std::pair<int,int>>* previousMap_;
    std::size_t previousLimit_;
public:
    explicit RuntimeMixedLocalComponentStatsScope(RuntimeMixedLocalComponentStats& stats,
        const std::vector<std::pair<int,int>>* expected=nullptr,std::size_t formationLimit=256) noexcept
        : previous_(runtimeMixedLocalComponentStats),previousMap_(runtimeMixedLocalComponentExpectedMap),
          previousLimit_(runtimeMixedLocalComponentFormationLimit) {
        runtimeMixedLocalComponentStats=&stats; runtimeMixedLocalComponentExpectedMap=expected;
        runtimeMixedLocalComponentFormationLimit=std::min<std::size_t>(formationLimit,256);
    }
    ~RuntimeMixedLocalComponentStatsScope() {
        runtimeMixedLocalComponentStats=previous_; runtimeMixedLocalComponentExpectedMap=previousMap_;
        runtimeMixedLocalComponentFormationLimit=previousLimit_;
    }
    RuntimeMixedLocalComponentStatsScope(const RuntimeMixedLocalComponentStatsScope&)=delete;
    RuntimeMixedLocalComponentStatsScope& operator=(const RuntimeMixedLocalComponentStatsScope&)=delete;
};

class RuntimeMixedLocalComponentCertificate {
    const RuntimeWindowState* state_=nullptr;
    const std::unordered_map<int,const TerrainCell*>* terrain_=nullptr;
    const std::unordered_map<int,int>* occupancy_=nullptr;
    std::optional<WorldObjectInstance> mover_;
    std::unordered_map<int,unsigned char> vertices_;
    // A failed seed/edge is never published as a connected vertex. Only
    // successful mutual edges may extend the root SCC, including during detours.
    std::unordered_map<int,bool> seeds_;
    std::unordered_map<int,unsigned char> failedEdges_;
    // Independent ORIGINAL directed truth. A failed mutual edge is not evidence
    // that its forward direction failed. Destination+direction identifies raw
    // predecessor coordinates exactly, including the negative boundary cases.
    std::unordered_map<std::uint64_t,bool> incomingEdges_;
    std::unordered_set<int> knownUnmapped_;
    bool knownUnmappedEnabled_=false;
    friend struct RuntimeMixedLocalKnownUnmappedContractAccess;
    std::unordered_set<int> desiredKeys_;
    // Only complete membership checks may publish a new covered centre. Its
    // points were all classified BEFORE assignment; no new graph truth here.
    std::unordered_set<int> closedDesiredKeys_;
    std::size_t frontierClosureAttempts_=0,frontierClosureLimit_=0;
    std::vector<int> ids_;
    bool enabled_=false,attempted_=false;
    RuntimeMixedLocalComponentStats* stats_=nullptr;
    int tick_=0;
    std::size_t vertexLimit_=4096,edgeLimit_=32768,edgeCount_=0;
    RuntimeCellPoint lastGoal_{};
    int rejectionReason_=0,rejectionActor_=-1;
    enum Rejection { None,Reference,GlobalGuard,DuplicateSelected,CargoSelected,
        ActorIdentity,ActorTransport,ActorMission,ActorMoveEligibility,ActorShroud,
        ActorRules,ActorDeployed,ActorBehavior,Representative,Layout,Metadata,
        Limits,Departure,SeedLimit,SeedVeto,EdgeLimit,Disconnected,Allocation,Retry,MovableCount };
    void rejection(int reason,int actor=-1) noexcept {
        rejectionReason_=reason;rejectionActor_=actor;
        if(stats_) {stats_->rejectionReason=reason;stats_->rejectionActor=actor;}
    }
    static const char* rejectionName(int reason) noexcept {
        static constexpr const char* names[]{"none","reference","global-guard","duplicate-selected","cargo-selected",
            "actor-identity","actor-transport","actor-mission","actor-move-eligibility","actor-shroud",
            "actor-rules","actor-deployed","actor-behavior","representative","layout","metadata",
            "limits","departure","seed-limit","seed-veto","edge-limit","disconnected","allocation","retry","movable-count"};
        return reason>=0 && reason<static_cast<int>(std::size(names))?names[reason]:"unknown";
    }
    static bool canonical(RuntimeCellPoint p) noexcept {
        return p.x>=0 && p.x<512 && p.y>=0 && p.y<512;
    }
    static bool oldObserversOrScopes() noexcept {
        return runtimeStrictSlotVehicleProofActive || runtimeStrictSlotVehicleStats
            || runtimeActivePlannerCellPassability || runtimeActivePlannerColdCellLookups
            || runtimeActivePlannerEmptyCellCensus || runtimeActivePlannerIndexedTopology
            || runtimeActivePlannerInvisibleBuildings
            || runtimeActivePlannerLandPermission || runtimeActiveBuildingDepartureReadOnlyQueries
            || runtimePlannerPassabilityObserver || runtimePlannerColdLookupObserver
            || runtimePlannerEmptyCensusObserver || runtimePlannerIndexedTopologyObserver
            || runtimePlannerMetadataBorrowObserver || runtimePlannerKernelLeaseObserver
            || runtimePlannerCrushAliasObserver || runtimePlannerEmptySubcellBooleanObserver
            || runtimeCellGateScratchStats;
    }
    // Literal original geometric schedule. No score/passability/reservation
    // query changes its FIFO or insertion-cap behavior in Best.
    template<class Visitor> static bool probes(RuntimeCellPoint desired,Visitor&& visit) {
        std::deque<RuntimeCellPoint> frontier;
        std::unordered_set<int> visited;
        frontier.push_back(desired); visited.insert(runtimeCellKey(desired.x,desired.y));
        while(!frontier.empty() && visited.size()<320) {
            const auto current=frontier.front(); frontier.pop_front();
            if(std::abs(current.x-desired.x)<=16 && std::abs(current.y-desired.y)<=16
                && !visit(current)) return false;
            for(const auto& next:runtimeNeighborsOf(current)) {
                if(std::abs(next.x-desired.x)>16 || std::abs(next.y-desired.y)>16) continue;
                const int key=runtimeCellKey(next.x,next.y);
                if(key<0 || visited.contains(key)) continue;
                visited.insert(key); frontier.push_back(next);
            }
        }
        return true;
    }
    bool seed(RuntimeCellPoint point) {
        const int key=runtimeCellKey(point.x,point.y);
        if(vertices_.contains(key)) return true;
        if(!canonical(point))return false;
        const auto known=seeds_.find(key);if(known!=seeds_.end())return known->second;
        // Bounded failed candidates as well as connected vertices. Exhaustion
        // abandons this certificate and selects the complete original builder.
        if(seeds_.size()>=4096) {rejection(SeedLimit);return false;}
        const bool okay=runtimeCellIsPassable(*state_,*terrain_,*occupancy_,point,*mover_,-1,nullptr,false);
        seeds_.emplace(key,okay);if(!okay && stats_)++stats_->seedVetoes;
        return okay;
    }
    bool mutualEdge(RuntimeCellPoint from,RuntimeCellPoint to) {
        const int key=runtimeCellKey(from.x,from.y),nextKey=runtimeCellKey(to.x,to.y);
        const int d=(to.y-from.y+1)*3+(to.x-from.x+1);
        const unsigned char bit=static_cast<unsigned char>(1U<<(d<4?d:d-1));
        if(vertices_.at(key)&bit)return true;
        const auto failed=failedEdges_.find(key);
        if(failed!=failedEdges_.end() && (failed->second&bit))return false;
        if(!seed(to))return false;
        if(!vertices_.contains(nextKey) && vertices_.size()+knownUnmapped_.size()>=vertexLimit_) {rejection(SeedLimit);return false;}
        if(edgeCount_+2>edgeLimit_) {rejection(EdgeLimit);return false;}
        ++edgeCount_;if(stats_)++stats_->orientedEdges;
        bool okay=runtimeCellTransitionIsPassable(*state_,*terrain_,*occupancy_,from,to,*mover_);
        if(okay) {++edgeCount_;if(stats_)++stats_->orientedEdges;
            okay=runtimeCellTransitionIsPassable(*state_,*terrain_,*occupancy_,to,from,*mover_);}
        if(!okay) {failedEdges_[key]|=bit;if(stats_)++stats_->edgeVetoes;return false;}
        // Publication occurs AFTER both original directions succeed. `contains`
        // therefore always means fully connected, never just seed-tested.
        const auto inserted=vertices_.emplace(nextKey,0);if(inserted.second && stats_)++stats_->vertices;
        vertices_.at(key)|=bit;
        const int reverse=8-d;
        vertices_.at(nextKey)|=static_cast<unsigned char>(1U<<(reverse<4?reverse:reverse-1));
        return true;
    }
    bool walk(RuntimeCellPoint goal) {
        if(!canonical(goal)) return false;
        if(vertices_.contains(runtimeCellKey(goal.x,goal.y))) {
            if(stats_) ++stats_->connectedGoalHits;
            lastGoal_=goal;
            return true;
        }
        if(!seed(goal)) {if(rejectionReason_==None)rejection(SeedVeto);return false;}
        const RuntimeCellPoint root{mover_->x,mover_->y};
        if(vertices_.empty()) {
            if(!seed(root)) {if(rejectionReason_==None)rejection(SeedVeto);return false;}
            vertices_.emplace(runtimeCellKey(root.x,root.y),0);if(stats_)++stats_->vertices;
            lastGoal_=root;
        }
        if(goal==root) {lastGoal_=goal;return true;}
        // Usually the next FIFO frontier point already borders the certified
        // region. Try these edges before allocating a bounded detour frontier.
        for(const auto& from:runtimeNeighborsOf(goal)) {
            if(!canonical(from) || !vertices_.contains(runtimeCellKey(from.x,from.y)))continue;
            if(stats_)++stats_->walkSteps;
            if(mutualEdge(from,goal)) {lastGoal_=goal;return true;}
            if(rejectionReason_==SeedLimit || rejectionReason_==EdgeLimit)return false;
        }
        struct Candidate {RuntimeCellPoint point;int distance;std::size_t serial;};
        const auto later=[](const Candidate& a,const Candidate& b) {
            return a.distance!=b.distance?a.distance>b.distance:a.serial>b.serial;};
        std::priority_queue<Candidate,std::vector<Candidate>,decltype(later)> frontier(later);
        std::unordered_set<int> expanded;std::size_t serial=0;
        const auto distance=[&](RuntimeCellPoint p){return std::abs(goal.x-p.x)+std::abs(goal.y-p.y);};
        frontier.push({lastGoal_,distance(lastGoal_),serial++});
        while(!frontier.empty()) {
            const auto current=frontier.top().point;frontier.pop();
            if(!expanded.insert(runtimeCellKey(current.x,current.y)).second)continue;
            if(stats_)++stats_->detourExpansions;
            if(expanded.size()>4096) {rejection(SeedLimit);return false;}
            auto neighbors=runtimeNeighborsOf(current);
            // Fixed eight-entry stable insertion sort: no per-vertex scratch
            // allocation, and equal heuristic distances keep original order.
            for(std::size_t i=1;i<neighbors.size();++i) {
                const auto value=neighbors[i];std::size_t at=i;
                while(at && distance(value)<distance(neighbors[at-1])) {neighbors[at]=neighbors[at-1];--at;}
                neighbors[at]=value;
            }
            for(const auto& next:neighbors) {
                if(!canonical(next))continue;
                if(stats_)++stats_->walkSteps;
                if(!mutualEdge(current,next)) {
                    if(rejectionReason_==SeedLimit || rejectionReason_==EdgeLimit)return false;
                    continue;
                }
                if(next==goal) {lastGoal_=goal;return true;}
                if(!expanded.contains(runtimeCellKey(next.x,next.y)))frontier.push({next,distance(next),serial++});
            }
        }
        rejection(Disconnected);return false;
    }
    bool knownUnmapped(RuntimeCellPoint point) {
        if(!knownUnmappedEnabled_ || runtimeMixedKnownUnmappedReference() || !canonical(point))return false;
        const int key=runtimeCellKey(point.x,point.y);
        if(knownUnmapped_.contains(key)) {if(stats_)++stats_->unmappedMemoHits;return true;}
        if(vertices_.contains(key))return false;
        if(stats_)++stats_->unmappedAttempts;
        // Original seed=false alone is insufficient: crush=true or a directed
        // edge may still admit this coordinate before its failed seed is seen.
        if(seed(point))return false;
        if(rejectionReason_==SeedLimit || rejectionReason_==EdgeLimit)return false;
        if(vertices_.size()+knownUnmapped_.size()>=vertexLimit_) {rejection(SeedLimit);return false;}
        const auto predecessors=runtimeNeighborsOf(point);
        for(std::size_t direction=0;direction<predecessors.size();++direction) {
            const auto edgeKey=(static_cast<std::uint64_t>(static_cast<unsigned>(key))<<3)|direction;
            const auto found=incomingEdges_.find(edgeKey);
            bool incoming;
            if(found!=incomingEdges_.end()) {incoming=found->second;if(stats_)++stats_->incomingMemoHits;}
            else {
                if(edgeCount_>=edgeLimit_) {rejection(EdgeLimit);return false;}
                ++edgeCount_;if(stats_){++stats_->orientedEdges;++stats_->incomingQueries;}
                // Identical original non-Air complete-kernel callback: implicit
                // mover mode, allowed=-1 (original departure lookup), soft=null,
                // crush=true; original diagonal corners remain crush=false.
                incoming=runtimeCellTransitionIsPassable(*state_,*terrain_,*occupancy_,
                    predecessors[direction],point,*mover_);
                incomingEdges_.emplace(edgeKey,incoming);
            }
            if(incoming) {if(stats_)++stats_->incomingPositiveVetoes;return false;}
        }
        // The complete builder can label a key only as its canonical seed or
        // after a successful incoming edge. Both possibilities are now false.
        // Publication is separate from root-SCC vertices and happens only after
        // all eight predicates succeed as NEGATIVE proofs. No -1 map entry.
        knownUnmapped_.insert(key);
        if(stats_) {
            ++stats_->unmappedProofs;
            if(runtimeMixedLocalComponentExpectedMap) {
                ++stats_->unmappedOriginalChecks;
                if(std::any_of(runtimeMixedLocalComponentExpectedMap->begin(),runtimeMixedLocalComponentExpectedMap->end(),
                        [key](const auto& entry){return entry.first==key;}))++stats_->unmappedOriginalMismatches;
            }
        }
        return true;
    }
public:
    RuntimeMixedLocalComponentCertificate()=default;
    RuntimeMixedLocalComponentCertificate(const RuntimeMixedLocalComponentCertificate&)=delete;
    RuntimeMixedLocalComponentCertificate& operator=(const RuntimeMixedLocalComponentCertificate&)=delete;
    bool enabled() const noexcept { return enabled_; }
    void invalidate() noexcept {
        enabled_=false;knownUnmappedEnabled_=false;
        closedDesiredKeys_.clear();
        knownUnmapped_.clear();incomingEdges_.clear();
    }
    const WorldObjectInstance* originalMover() const noexcept { return mover_?&*mover_:nullptr; }
    const auto& certifiedKeys() const noexcept { return vertices_; }
    const auto& knownUnmappedKeys() const noexcept {return knownUnmapped_;}
    const std::unordered_set<int>* bestUnmappedFilter() const noexcept {
        return enabled_ && knownUnmappedEnabled_ && state_ && state_->tick==tick_
            && !runtimeMixedLocalComponentReference() && !oldObserversOrScopes()
            && !runtimeMixedKnownUnmappedReference()
            && !knownUnmapped_.empty()?&knownUnmapped_:nullptr;
    }
    bool prepare(const RuntimeWindowState& state,const std::vector<int>& ids,RuntimeCellPoint target,
        const std::unordered_map<int,const TerrainCell*>& terrain,
        const std::unordered_map<int,int>& occupancy,int representativeId,std::size_t movableCount,
        std::size_t vertexLimit=4096,std::size_t edgeLimit=32768) {
        if(attempted_) {
            enabled_=false;knownUnmappedEnabled_=false;
            closedDesiredKeys_.clear();
            knownUnmapped_.clear();incomingEdges_.clear();
            if(runtimeMixedLocalComponentStats) {
                ++runtimeMixedLocalComponentStats->attempts;
                ++runtimeMixedLocalComponentStats->guardFallbacks;
                runtimeMixedLocalComponentStats->rejectionReason=Retry;
            }
            return false;
        }
        attempted_=true;
        enabled_=false; state_=nullptr; terrain_=nullptr; occupancy_=nullptr;
        mover_.reset(); vertices_.clear(); seeds_.clear(); failedEdges_.clear(); desiredKeys_.clear(); ids_.clear(); edgeCount_=0;
        closedDesiredKeys_.clear();frontierClosureAttempts_=0;
        frontierClosureLimit_=std::min<std::size_t>(ids.size(),256);
        incomingEdges_.clear();knownUnmapped_.clear();
        knownUnmappedEnabled_=!runtimeMixedKnownUnmappedReference();
        rejectionReason_=None;rejectionActor_=-1;
        stats_=runtimeMixedLocalComponentStats; if(stats_) ++stats_->attempts;
        // One coarse diagnostic per command, only when the existing Trace is
        // requested. No callbacks, clocks, or output in the graph hot loop.
        struct Diagnostic {
            RuntimeMixedLocalComponentCertificate& owner;const RuntimeWindowState& state;
            RuntimeCellPoint target;std::size_t selected,movable;
            ~Diagnostic() noexcept {
                try {
                static const bool requested=std::getenv("CONCORD_RUNTIME_PERF_TRACE")!=nullptr;
                if(!requested)return;
                const auto* actor=owner.rejectionActor_>=0?findObjectById(state,owner.rejectionActor_):nullptr;
                std::cerr<<"Runtime local component admission: tick="<<state.tick<<" target="<<target.x<<','<<target.y
                    <<" selected="<<selected<<" movable="<<movable<<" admitted="<<owner.enabled_
                    <<" rejection="<<owner.rejectionName(owner.rejectionReason_)<<" actor="<<owner.rejectionActor_
                    <<" mission="<<(actor?actor->mission.c_str():"")
                    <<" vertices="<<owner.vertices_.size()<<" seedTests="<<owner.seeds_.size()
                    <<" orientedEdges="<<owner.edgeCount_<<'\n';
                } catch(...) { /* Optional diagnostics must not defeat fallback. */ }
            }
        } diagnostic{*this,state,target,ids.size(),movableCount};
        const auto guardFail=[&](int reason=GlobalGuard,int actor=-1) {
            rejection(reason,actor);if(stats_)++stats_->guardFallbacks;return false;};
        if(runtimeMixedLocalComponentReference()) { rejection(Reference);if(stats_) ++stats_->referenceFallbacks; return false; }
        if(runtimeMixedLocalComponentProofActive || oldObserversOrScopes()
            || runtimeFrameScalingReferenceMovement() || state.config.multiplayerActive
            || runtimeComponentPassabilityReference() || runtimeComponentKernelReference()
            || runtimeComponentTerrainQueryReference() || runtimeComponentQueryLeaseReference()
            || ids.size()<12 || ids.size()>256 || movableCount<12 || movableCount>ids.size()
            || representativeId!=ids.front() || !std::is_sorted(ids.begin(),ids.end())
            || std::adjacent_find(ids.begin(),ids.end())!=ids.end() || !canonical(target)
            || !state.specialMechanismsInitialized || !state.repairDepots.relations().empty()
            || !state.grinding.relations().empty() || !state.specialMechanisms.parasiteLinks().empty()
            || !state.specialMechanisms.mindControlLinks().empty() || !state.specialMechanisms.temporalTargets().empty()
            || !state.specialMechanisms.carrierParents().empty() || !state.specialMechanisms.carrierChildren().empty()
            || !state.combatDetails.panics.empty() || !state.combatDetails.diskDrains.sources.empty()
            || !state.pendingEngineerCaptures.empty() || !state.pendingEngineerActions.empty()
            || !state.pendingOccupyTargets.empty() || !state.pendingTransportTargets.empty()
            || !state.pendingTimedBombIntents.empty() || !state.gateIdsByCell.empty()) return guardFail();
        struct Active { bool previous=runtimeMixedLocalComponentProofActive;
            Active() { runtimeMixedLocalComponentProofActive=true; }
            ~Active() { runtimeMixedLocalComponentProofActive=previous; } } active;
        RuntimePerfTraceScope trace("moveCommand.localComponentCertificate",state.tick);
        try {
            std::unordered_set<int> selected(ids.begin(),ids.end()),seen;
            for(const auto& object:state.world.getObjects()) {
                if(selected.contains(object.instanceId) && !seen.insert(object.instanceId).second) return guardFail(DuplicateSelected,object.instanceId);
                for(int passenger:object.transportPassengerIds) if(selected.contains(passenger)) return guardFail(CargoSelected,passenger);
            }
            std::vector<const WorldObjectInstance*> actors; actors.reserve(ids.size());
            std::size_t eligible=0;
            for(int id:ids) {
                const auto* actor=findObjectById(state,id);
                if(!actor || id<0 || !actor->alive || actor->health<=0
                    || (actor->kind!=WorldObjectKind::Unit && actor->kind!=WorldObjectKind::Infantry)
                    || actor->owner!=state.economy.owner || !canonical({actor->x,actor->y})) return guardFail(ActorIdentity,id);
                if(actor->submerged || actor->dockedToObjectId>=0 || actor->dockIndex>=0
                    || actor->tankBunkerHostId>=0 || actor->garrisonHostObjectId>=0
                    || !actor->transportPassengerIds.empty() || !actor->garrisonOccupantIds.empty())return guardFail(ActorTransport,id);
                const auto* unit=findRuntimeUnitType(state.rules,*actor);
                if(!unit || unit->harvester || runtimeObjectIsMobileSlaveMiner(state,*actor))return guardFail(ActorRules,id);
                const bool canReceive=runtimeObjectCanReceiveMoveCommand(state,*actor);
                // The rejected recovery actor is left in the ORIGINAL captured
                // occupancy. It never consumes a slot or receives an assignment.
                const bool recovery=!canReceive && unit->teleporter && !unit->harvester
                    && state.specialMechanisms.isChronoMovementLocked(id,state.tick)
                    && !state.specialMechanisms.isChronoInTransit(id) && actor->mission=="ChronoFrozen";
                const bool idle=actor->mission=="Idle" || actor->mission=="Idle1" || actor->mission=="Idle2";
                if(actor->mission!="Guard" && actor->mission!="Move" && !idle && !recovery)return guardFail(ActorMission,id);
                if(!canReceive && !recovery)return guardFail(ActorMoveEligibility,id);
                if(!runtimeObjectAllowsMoveToCell(state,*actor,target))return guardFail(ActorShroud,id);
                if(runtimeObjectOffGroundByMagBeam(state,id)
                    || runtimeObjectManagedByAirstrike(state,id))return guardFail(ActorMoveEligibility,id);
                if(runtimeObjectHasDeployedState(state,*actor))return guardFail(ActorDeployed,id);
                const auto behavior=state.behaviors.find(id);
                if(behavior!=state.behaviors.end() && (behavior->second.kind!=RuntimeBehaviorKind::Move
                    || behavior->second.returnToDeployed))return guardFail(ActorBehavior,id);
                if(recovery) {if(stats_)++stats_->qualifiedChronoRecovery;}
                else ++eligible;
                if(idle && stats_)++stats_->idleActors;
                actors.push_back(actor);
            }
            if(eligible!=movableCount)return guardFail(MovableCount);
            const auto* rootUnit=findRuntimeUnitType(state.rules,*actors.front());
            const auto& rootType=actors.front()->type;
            const bool stockGroundRoot=rootType=="MTNK" || rootType=="SREF" || rootType=="TTNK" || rootType=="FV" || rootType=="HTNK";
            if(actors.front()->kind!=WorldObjectKind::Unit || !stockGroundRoot || !rootUnit || rootUnit->teleporter
                || actors.front()->airborne || runtimeObjectMovementMode(state,*actors.front())!=PathMovementMode::Ground)
                return guardFail(Representative,actors.front()->instanceId);
            // A warm wrapper table is an exact pointer index. Do not build it
            // here just to validate admission; a cold wrapper uses original find.
            const auto* metadataPointers=state.terrainInfoByCell.denseValid()
                ? &state.terrainInfoByCell.densePointers() : nullptr;
            for(const auto& [key,value]:terrain) {
                if(stats_) ++stats_->layoutEntries;
                if(!value || !canonical({value->x,value->y}) || key!=runtimeCellKey(value->x,value->y)) {
                    rejection(Layout);
                    if(stats_) ++stats_->layoutFallbacks; return false;
                }
                const bool metadataPresent=metadataPointers
                    ? (*metadataPointers)[static_cast<std::size_t>(value->y)*512+value->x]!=nullptr
                    : state.terrainInfoByCell.contains(key);
                if(!metadataPresent) { rejection(Metadata);if(stats_) ++stats_->layoutFallbacks; return false; }
            }
            state_=&state; terrain_=&terrain; occupancy_=&occupancy; tick_=state.tick;
            mover_=*actors.front(); ids_=ids;
            vertexLimit_=std::min<std::size_t>(vertexLimit,4096);
            edgeLimit_=std::min<std::size_t>(edgeLimit,32768);
            if(!vertexLimit_ || !edgeLimit_) return guardFail(Limits);
            RuntimeBuildingDepartureReadOnlyQueryScope departure(state,*mover_);
            if(runtimeStructureDepartureHostId(state,*mover_)>=0) return guardFail(Departure);
            const auto walkFail=[&] {
                knownUnmapped_.clear();incomingEdges_.clear();
                if(stats_) ++stats_->walkFallbacks; return false;
            };
            for(const auto* actor:actors) {
                const auto* unit=findRuntimeUnitType(state.rules,*actor);
                if(runtimeObjectMovementMode(state,*actor)!=PathMovementMode::Air && !unit->teleporter
                    && !walk({actor->x,actor->y})) return walkFail();
            }
            const std::size_t formations=std::min(ids.size(),runtimeMixedLocalComponentFormationLimit);
            for(std::size_t i=0;i<formations;++i) {
                const auto offset=formationOffsetForIndex(static_cast<int>(i),1);
                const RuntimeCellPoint desired{target.x+offset.x,target.y+offset.y};
                if(!canonical(desired) || !probes(desired,[&](RuntimeCellPoint p) {
                        return walk(p) || knownUnmapped(p);
                    })) return walkFail();
                desiredKeys_.insert(runtimeCellKey(desired.x,desired.y));
            }
            if(formations==0) return guardFail();
            if(knownUnmappedEnabled_)rejection(None);
            enabled_=true; if(stats_) ++stats_->admitted; return true;
        } catch(const std::bad_alloc&) {
            knownUnmapped_.clear();incomingEdges_.clear();enabled_=false;
            rejection(Allocation);if(stats_) ++stats_->allocationFallbacks; return false;
        }
    }
    // Values are already proven for this closed command. No query lease survives
    // assignment, and this does not claim pointer/count to be a content revision.
    bool coversBest(RuntimeCellPoint desired) {
        if(!enabled_ || runtimeMixedLocalComponentReference() || oldObserversOrScopes()
            || !state_ || state_->tick!=tick_
            || (!knownUnmapped_.empty() && runtimeMixedKnownUnmappedReference())) return false;
        if(!canonical(desired))return false;
        const int key=runtimeCellKey(desired.x,desired.y);
        // The original prepare-time centres keep their original exact fast path.
        if(desiredKeys_.contains(key)) {if(stats_)++stats_->coveredBest;return true;}
        if(!knownUnmappedEnabled_ || runtimeMixedKnownUnmappedReference())return false;
        if(runtimeMixedBestFrontierClosureReference()) {
            if(stats_)++stats_->frontierClosureReferenceVetoes;
            return false;
        }
        if(closedDesiredKeys_.contains(key)) {
            if(stats_) {++stats_->frontierClosureMemoHits;++stats_->coveredBest;}
            return true;
        }
        if(frontierClosureAttempts_>=frontierClosureLimit_) {
            if(stats_)++stats_->frontierClosureRejected;
            return false;
        }
        ++frontierClosureAttempts_;
        if(stats_)++stats_->frontierClosureAttempts;
        try {
            // Replay EVERY original FIFO candidate, including score-pruned ones.
            // Only frozen membership is consulted. In particular, this never
            // calls seed/walk/incoming or current World/metadata/rule queries.
            const bool complete=probes(desired,[&](RuntimeCellPoint point) {
                if(stats_)++stats_->frontierClosurePoints;
                if(!canonical(point))return false;
                const int candidateKey=runtimeCellKey(point.x,point.y);
                return vertices_.contains(candidateKey) || knownUnmapped_.contains(candidateKey);
            });
            if(!complete) {
                if(stats_)++stats_->frontierClosureRejected;
                return false;
            }
            closedDesiredKeys_.insert(key);
            if(stats_) {++stats_->frontierClosureAdmissions;++stats_->coveredBest;}
            return true;
        } catch(const std::bad_alloc&) {
            if(stats_)++stats_->frontierClosureAllocationFallbacks;
            return false;
        }
    }
    static std::vector<RuntimeCellPoint> probePointsForContract(RuntimeCellPoint desired) {
        std::vector<RuntimeCellPoint> output;
        (void)probes(desired,[&](RuntimeCellPoint p) { output.push_back(p); return true; });
        return output;
    }
    void noteWholeMap(const std::unordered_map<int,int>& map,bool nearest=false) {
        if(!stats_) return;
        ++stats_->wholeMaps;
        if(enabled_) {
            ++stats_->lateWholeMaps;
            if(nearest) ++stats_->nearestFallbacks; else ++stats_->outsideFallbacks;
            for(int id:ids_) {
                const auto* actor=findObjectById(*state_,id); if(!actor) continue;
                if(state_->specialMechanisms.isChronoInTransit(id)) { ++stats_->lateChronoWitness; break; }
            }
            for(int id:ids_) {
                const auto* actor=findObjectById(*state_,id); if(!actor) continue;
                const auto behavior=state_->behaviors.find(id);
                if(runtimeObjectMovementMode(*state_,*actor)==PathMovementMode::Air
                    && behavior!=state_->behaviors.end() && behavior->second.kind==RuntimeBehaviorKind::Move) {
                    ++stats_->lateAirWitness; break;
                }
            }
        }
        if(runtimeMixedLocalComponentExpectedMap) {
            ++stats_->orderedMapsCompared;
            const auto& expected=*runtimeMixedLocalComponentExpectedMap;
            auto it=map.begin(); bool equal=map.size()==expected.size();
            for(const auto& item:expected) {
                if(it==map.end() || it->first!=item.first || it->second!=item.second) { equal=false; break; }
                ++it;
            }
            if(!equal) ++stats_->orderedMapMismatches;
        }
    }
};
