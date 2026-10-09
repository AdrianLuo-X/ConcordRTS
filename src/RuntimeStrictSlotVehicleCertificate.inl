// Command-local proof only. The complete BFS and assignment implementation stay
// unchanged. This is not a partial component map or a persistent value cache.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <vector>

inline thread_local int runtimeStrictSlotVehicleReferenceOverride = -1;
inline bool runtimeStrictSlotVehicleReference() noexcept {
    if (runtimeStrictSlotVehicleReferenceOverride >= 0)
        return runtimeStrictSlotVehicleReferenceOverride != 0;
    static const bool reference = [] {
        const char* v = std::getenv("CONCORD_MOVE_STRICT_SLOT_CERTIFICATE_REFERENCE");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return reference;
}
class RuntimeStrictSlotVehicleReferenceScope {
    int previous_;
public:
    explicit RuntimeStrictSlotVehicleReferenceScope(bool reference) noexcept
        : previous_(runtimeStrictSlotVehicleReferenceOverride) {
        runtimeStrictSlotVehicleReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeStrictSlotVehicleReferenceScope() { runtimeStrictSlotVehicleReferenceOverride = previous_; }
    RuntimeStrictSlotVehicleReferenceScope(const RuntimeStrictSlotVehicleReferenceScope&) = delete;
    RuntimeStrictSlotVehicleReferenceScope& operator=(const RuntimeStrictSlotVehicleReferenceScope&) = delete;
};
struct RuntimeStrictSlotVehicleCertificateStats {
    std::uint64_t attempts=0, admitted=0, referenceFallbacks=0, guardFallbacks=0;
    std::uint64_t layoutFallbacks=0, slotFallbacks=0, walkFallbacks=0, allocationFallbacks=0;
    std::uint64_t layoutEntries=0, vertices=0, orientedEdges=0, strictSlots=0;
    void add(const RuntimeStrictSlotVehicleCertificateStats& s) noexcept {
        attempts+=s.attempts; admitted+=s.admitted; referenceFallbacks+=s.referenceFallbacks;
        guardFallbacks+=s.guardFallbacks; layoutFallbacks+=s.layoutFallbacks;
        slotFallbacks+=s.slotFallbacks; walkFallbacks+=s.walkFallbacks;
        allocationFallbacks+=s.allocationFallbacks; layoutEntries+=s.layoutEntries;
        vertices+=s.vertices; orientedEdges+=s.orientedEdges; strictSlots+=s.strictSlots;
    }
};
inline thread_local RuntimeStrictSlotVehicleCertificateStats* runtimeStrictSlotVehicleStats=nullptr;
inline thread_local bool runtimeStrictSlotVehicleProofActive=false;
class RuntimeStrictSlotVehicleStatsScope {
    RuntimeStrictSlotVehicleCertificateStats* previous_;
public:
    explicit RuntimeStrictSlotVehicleStatsScope(RuntimeStrictSlotVehicleCertificateStats& stats) noexcept
        : previous_(runtimeStrictSlotVehicleStats) { runtimeStrictSlotVehicleStats=&stats; }
    ~RuntimeStrictSlotVehicleStatsScope() { runtimeStrictSlotVehicleStats=previous_; }
    RuntimeStrictSlotVehicleStatsScope(const RuntimeStrictSlotVehicleStatsScope&)=delete;
    RuntimeStrictSlotVehicleStatsScope& operator=(const RuntimeStrictSlotVehicleStatsScope&)=delete;
};
inline bool runtimeStrictSlotVehicleCanonical(const RuntimeCellPoint& p) noexcept {
    return p.x>=0 && p.x<512 && p.y>=0 && p.y<512;
}
inline bool runtimeStrictSlotVehicleStrictWinner(int softPenalty,std::uint64_t tie) noexcept {
    return softPenalty>=0 && softPenalty < 530-static_cast<int>(tie&31U);
}

// Every successful proof is completed before any command assignment. The only
// diagnostic sink is POD aggregation; there is no callback/reentry in the proof.
// provenPoints is contract-only and receives an owned copy AFTER proof completion.
bool tryRuntimeStrictSlotVehicleCertificate(
    const RuntimeWindowState& state,const std::vector<int>& ids,const RuntimeCellPoint& target,
    const std::unordered_map<int,const TerrainCell*>& terrain,
    const std::unordered_map<int,int>& occupancy,int representativeId,std::size_t movableCount,
    std::vector<RuntimeCellPoint>* provenPoints=nullptr,
    std::size_t vertexLimit=4096,std::size_t edgeLimit=8192) {
    RuntimeStrictSlotVehicleCertificateStats stats; stats.attempts=1;
    struct Finish {
        RuntimeStrictSlotVehicleCertificateStats& stats;
        bool previous=runtimeStrictSlotVehicleProofActive;
        ~Finish() { runtimeStrictSlotVehicleProofActive=previous;
            if(runtimeStrictSlotVehicleStats) runtimeStrictSlotVehicleStats->add(stats); }
    } finish{stats};
    if(runtimeStrictSlotVehicleReference()) { ++stats.referenceFallbacks; return false; }
    const auto guardFail=[&] { ++stats.guardFallbacks; return false; };
    if(runtimeStrictSlotVehicleProofActive || ids.size()<12 || ids.size()>256
        || movableCount!=ids.size() || representativeId!=ids.front()
        || !std::is_sorted(ids.begin(),ids.end())
        || std::adjacent_find(ids.begin(),ids.end())!=ids.end()
        || !runtimeStrictSlotVehicleCanonical(target)
        || !state.specialMechanismsInitialized || state.config.multiplayerActive
        || !state.repairDepots.relations().empty() || !state.subcells.reservations().empty()
        || !state.grinding.relations().empty() || !state.specialMechanisms.parasiteLinks().empty()
        || !state.combatDetails.panics.empty() || !state.combatDetails.diskDrains.sources.empty()
        || !state.pendingEngineerCaptures.empty() || !state.pendingEngineerActions.empty()
        || !state.pendingOccupyTargets.empty() || !state.pendingTransportTargets.empty()
        || !state.pendingTimedBombIntents.empty()
        || runtimeActivePlannerCellPassability || runtimeActivePlannerColdCellLookups
        || runtimeActivePlannerEmptyCellCensus || runtimeActivePlannerIndexedTopology
        || runtimeActivePlannerLandPermission || runtimeActiveBuildingDepartureReadOnlyQueries
        || runtimePlannerPassabilityObserver || runtimePlannerColdLookupObserver
        || runtimePlannerEmptyCensusObserver || runtimePlannerIndexedTopologyObserver
        || runtimePlannerMetadataBorrowObserver || runtimePlannerKernelLeaseObserver
        || runtimePlannerCrushAliasObserver || runtimePlannerEmptySubcellBooleanObserver
        || runtimeCellGateScratchStats || runtimeFrameScalingReferenceMovement()) return guardFail();
    runtimeStrictSlotVehicleProofActive=true;
    vertexLimit=std::min<std::size_t>(vertexLimit,4096);
    edgeLimit=std::min<std::size_t>(edgeLimit,8192);
    if(!vertexLimit || !edgeLimit) return guardFail();
    RuntimePerfTraceScope trace("moveCommand.strictSlotCertificate",state.tick);
    try {
        std::vector<const WorldObjectInstance*> actors; actors.reserve(ids.size());
        std::unordered_set<int> selected(ids.begin(),ids.end()),seen;
        // Duplicate IDs (including dead history) and hidden cargo are not admitted.
        for(const auto& object:state.world.getObjects()) {
            if(selected.contains(object.instanceId) && !seen.insert(object.instanceId).second) return guardFail();
            for(int passenger:object.transportPassengerIds) if(selected.contains(passenger)) return guardFail();
        }
        for(int id:ids) {
            const auto* actor=findObjectById(state,id);
            if(!actor || id<0 || !actor->alive || actor->health<=0 || actor->kind!=WorldObjectKind::Unit
                || (actor->type!="MTNK" && actor->type!="HTNK") || actor->owner!=state.economy.owner
                || actor->airborne || actor->submerged || actor->dockedToObjectId>=0 || actor->dockIndex>=0
                || actor->tankBunkerHostId>=0 || actor->garrisonHostObjectId>=0
                || !actor->transportPassengerIds.empty() || !actor->garrisonOccupantIds.empty()
                || (actor->mission!="Guard" && actor->mission!="Move")
                || !runtimeStrictSlotVehicleCanonical({actor->x,actor->y})
                || !runtimeObjectCanReceiveMoveCommand(state,*actor)
                || !runtimeObjectAllowsMoveToCell(state,*actor,target)
                || runtimeObjectMovementMode(state,*actor)!=PathMovementMode::Ground
                || runtimeObjectOffGroundByMagBeam(state,id)) return guardFail();
            const auto* unit=findRuntimeUnitType(state.rules,*actor);
            const auto& deploy=runtimeCachedCapabilityProfileForObject(state,*actor).deploy;
            if(!unit || unit->teleporter || unit->harvester || !unit->deploysInto.empty()
                || deploy.togglesDeployedState || deploy.deployToLand
                || runtimeObjectHasDeployedState(state,*actor)
                || runtimeObjectIsMobileSlaveMiner(state,*actor)) return guardFail();
            const auto behavior=state.behaviors.find(id);
            if(behavior!=state.behaviors.end() && behavior->second.kind!=RuntimeBehaviorKind::Move) return guardFail();
            actors.push_back(actor);
        }
        for(const auto& [key,value]:terrain) {
            ++stats.layoutEntries;
            if(!value || !runtimeStrictSlotVehicleCanonical({value->x,value->y})
                || key!=runtimeCellKey(value->x,value->y)) { ++stats.layoutFallbacks; return false; }
        }
        std::vector<RuntimeCellPoint> slots; slots.reserve(actors.size());
        for(std::size_t i=0;i<actors.size();++i) {
            const auto offset=formationOffsetForIndex(static_cast<int>(i),1);
            const RuntimeCellPoint slot{target.x+offset.x,target.y+offset.y};
            std::uint64_t tie=runtimeStableHashString("moveSlot");
            runtimeStableHashMix(tie,actors[i]->instanceId); runtimeStableHashMix(tie,slot.x); runtimeStableHashMix(tie,slot.y);
            if(!runtimeStrictSlotVehicleCanonical(slot)
                || !runtimeCellIsPassable(state,terrain,occupancy,slot,*actors[i],-1)
                || !runtimeObjectAllowsMoveToCell(state,*actors[i],slot)
                || !runtimeStrictSlotVehicleStrictWinner(runtimeMoveTargetSoftPenalty(state,terrain,occupancy,slot),tie)) {
                ++stats.slotFallbacks; return false;
            }
            // Keep the proof valid if the authored offset implementation changes.
            if(std::find(slots.begin(),slots.end(),slot)!=slots.end()) { ++stats.slotFallbacks; return false; }
            slots.push_back(slot); ++stats.strictSlots;
        }
        const auto& representative=*actors.front();
        RuntimeBuildingDepartureReadOnlyQueryScope departure(state,representative);
        if(runtimeStructureDepartureHostId(state,representative)>=0) return guardFail();
        struct Vertex { RuntimeCellPoint point; unsigned char checked=0; };
        std::unordered_map<int,Vertex> vertices; vertices.reserve(std::min<std::size_t>(vertexLimit,512));
        const auto vertex=[&](const RuntimeCellPoint& point)->bool {
            const int key=runtimeCellKey(point.x,point.y);
            if(vertices.contains(key)) return true;
            if(vertices.size()>=vertexLimit || !runtimeStrictSlotVehicleCanonical(point)
                || !runtimeCellIsPassable(state,terrain,occupancy,point,representative,-1,nullptr,false)) return false;
            vertices.emplace(key,Vertex{point,0}); ++stats.vertices; return true;
        };
        const auto walk=[&](const RuntimeCellPoint& goal)->bool {
            RuntimeCellPoint current{representative.x,representative.y};
            if(!vertex(current)) return false;
            while(current!=goal) {
                const RuntimeCellPoint next{current.x+(goal.x>current.x)-(goal.x<current.x),
                    current.y+(goal.y>current.y)-(goal.y<current.y)};
                if(!vertex(next)) return false;
                const int direction=(next.y-current.y+1)*3+(next.x-current.x+1);
                const unsigned char bit=static_cast<unsigned char>(1U<<(direction<4?direction:direction-1));
                auto& from=vertices.at(runtimeCellKey(current.x,current.y));
                if(!(from.checked&bit)) {
                    if(stats.orientedEdges+2>edgeLimit) return false;
                    ++stats.orientedEdges;
                    if(!runtimeCellTransitionIsPassable(state,terrain,occupancy,current,next,representative)) return false;
                    ++stats.orientedEdges;
                    if(!runtimeCellTransitionIsPassable(state,terrain,occupancy,next,current,representative)) return false;
                    from.checked|=bit;
                    const int reverse=8-direction;
                    vertices.at(runtimeCellKey(next.x,next.y)).checked|=
                        static_cast<unsigned char>(1U<<(reverse<4?reverse:reverse-1));
                }
                current=next;
            }
            return true;
        };
        for(const auto* actor:actors) if(!walk({actor->x,actor->y})) { ++stats.walkFallbacks; return false; }
        for(const auto& slot:slots) if(!walk(slot)) { ++stats.walkFallbacks; return false; }
        if(provenPoints) { std::vector<RuntimeCellPoint> owned; owned.reserve(vertices.size());
            for(const auto& [key,value]:vertices) { (void)key; owned.push_back(value.point); }
            *provenPoints=std::move(owned); }
        ++stats.admitted; return true;
    } catch(const std::bad_alloc&) { ++stats.allocationFallbacks; return false; }
}
