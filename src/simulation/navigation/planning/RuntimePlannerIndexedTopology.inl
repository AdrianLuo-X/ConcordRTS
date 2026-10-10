// Include after runtimeCachedTerrainInfoForCell and RuntimePlannerTerrainQueries.
// Owned only by ONE complete-component BFS. Every dynamic predicate and the
// original seed/FIFO/output-map order remain outside this geometry lease.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

inline thread_local int runtimeComponentIndexedTopologyReferenceOverride=-1;
inline bool runtimeComponentIndexedTopologyReference() noexcept {
    if(runtimeComponentIndexedTopologyReferenceOverride>=0)
        return runtimeComponentIndexedTopologyReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_COMPONENT_INDEXED_TOPOLOGY_REFERENCE");
        // The native mixed-unit command measurements showed that preparing
        // this geometry table costs more than it saves on the first BFS.
        // Keep it opt-in for the exact contract and isolated measurements.
        return !(value && value[0]=='0' && value[1]=='\0');
    }();
    return reference;
}
class RuntimeComponentIndexedTopologyReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentIndexedTopologyReferenceScope(bool reference) noexcept
        :previous_(runtimeComponentIndexedTopologyReferenceOverride) {
        runtimeComponentIndexedTopologyReferenceOverride=reference?1:0;
    }
    ~RuntimeComponentIndexedTopologyReferenceScope() {
        runtimeComponentIndexedTopologyReferenceOverride=previous_;
    }
    RuntimeComponentIndexedTopologyReferenceScope(const RuntimeComponentIndexedTopologyReferenceScope&)=delete;
    RuntimeComponentIndexedTopologyReferenceScope& operator=(const RuntimeComponentIndexedTopologyReferenceScope&)=delete;
};

class RuntimePlannerIndexedTopology;
inline thread_local RuntimePlannerIndexedTopology* runtimeActivePlannerIndexedTopology=nullptr;
using RuntimePlannerIndexedTopologyObserver=void(*)(void*,bool,std::size_t,std::size_t,std::size_t);
inline thread_local RuntimePlannerIndexedTopologyObserver runtimePlannerIndexedTopologyObserver=nullptr;
inline thread_local void* runtimePlannerIndexedTopologyObserverContext=nullptr;
class RuntimePlannerIndexedTopologyObserverScope {
    RuntimePlannerIndexedTopologyObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerIndexedTopologyObserverScope(RuntimePlannerIndexedTopologyObserver observer,void* context)
        :previous_(runtimePlannerIndexedTopologyObserver),previousContext_(runtimePlannerIndexedTopologyObserverContext) {
        runtimePlannerIndexedTopologyObserver=observer; runtimePlannerIndexedTopologyObserverContext=context;
    }
    ~RuntimePlannerIndexedTopologyObserverScope() {
        runtimePlannerIndexedTopologyObserver=previous_; runtimePlannerIndexedTopologyObserverContext=previousContext_;
    }
    RuntimePlannerIndexedTopologyObserverScope(const RuntimePlannerIndexedTopologyObserverScope&)=delete;
    RuntimePlannerIndexedTopologyObserverScope& operator=(const RuntimePlannerIndexedTopologyObserverScope&)=delete;
};

class RuntimePlannerIndexedTopology {
public:
    using TerrainMap=std::unordered_map<int,const TerrainCell*>;
    using OccupancyMap=std::unordered_map<int,int>;
    static constexpr std::size_t maximumSlots=512U*512U;
    struct Geometry {
        const TerrainCell* terrain=nullptr;
        int effectiveLevel=0;
        bool water=false,shore=false,bridge=false,ramp=false;
    };
    struct Pair {
        const Geometry* current;
        const Geometry* next;
    };
private:
    RuntimePlannerIndexedTopology* previous_;
    const RuntimeWindowState* state_;
    const WorldObjectInstance* mover_;
    const TerrainMap* terrain_;
    const OccupancyMap* occupancy_;
    const TerrainCell* cellData_;
    std::size_t cellCount_,terrainCount_,terrainBuckets_;
    std::uint64_t metadataGeneration_;
    int tick_;
    bool enabled_=false;
    int minX_=0,maxX_=-1,minY_=0,maxY_=-1;
    std::size_t width_=0;
    std::vector<Geometry> geometry_;
    Geometry missing_;
    mutable std::size_t pairs_=0,fallbacks_=0;
    static bool canonical(int x,int y) noexcept { return x>=0 && x<512 && y>=0 && y<512; }
    const Geometry* at(const RuntimeCellPoint& point) const noexcept {
        if(point.x<minX_ || point.x>maxX_ || point.y<minY_ || point.y>maxY_) return &missing_;
        return &geometry_[static_cast<std::size_t>(point.y-minY_)*width_+static_cast<std::size_t>(point.x-minX_)];
    }
    void prepare(const RuntimeWindowState& state,const TerrainMap& terrain,bool enabled) {
        if(!enabled || runtimeComponentIndexedTopologyReference() || terrain.empty() || terrain.size()>maximumSlots) return;
        RuntimePerfTraceScope geometryBuildTrace("componentIndexedTopologyBuild",state.tick);
        int minX=511,maxX=0,minY=511,maxY=0;
        // First prove the entire input. An anomalous entry disables the WHOLE
        // owner, including cells whose local coordinates happen to look valid.
        for(const auto& [key,cell]:terrain) {
            if(!cell || key<0 || !canonical(cell->x,cell->y)
                || key!=runtimeCellKey(cell->x,cell->y)
                || !state.terrainInfoByCell.contains(key)) return;
            minX=std::min(minX,cell->x); maxX=std::max(maxX,cell->x);
            minY=std::min(minY,cell->y); maxY=std::max(maxY,cell->y);
        }
        minX_=minX; maxX_=maxX; minY_=minY; maxY_=maxY;
        width_=static_cast<std::size_t>(maxX-minX+1);
        const auto area=width_*static_cast<std::size_t>(maxY-minY+1);
        if(area>maximumSlots) return;
        geometry_.resize(area);
        for(const auto& [key,cell]:terrain) {
            const auto found=state.terrainInfoByCell.find(key);
            if(found==state.terrainInfoByCell.end()) return;
            const auto& info=found->second;
            // The high-bridge test is pure, but its +4 must not introduce an
            // overflow in preparation for a cell the original BFS might skip.
            const bool high=runtimeOverlayLooksHighBridge(*cell);
            const auto effective=static_cast<std::int64_t>(cell->level)+(high?4:0);
            if(effective<std::numeric_limits<int>::min()/2 || effective>std::numeric_limits<int>::max()/2) return;
            auto& target=geometry_[static_cast<std::size_t>(cell->y-minY_)*width_+static_cast<std::size_t>(cell->x-minX_)];
            target={cell,static_cast<int>(effective),info.water,info.shore,info.bridge,info.ramp};
        }
        enabled_=true; // Publish only after complete proof/allocation/fill.
    }
public:
    // enabled=false still masks an outer owner. Preparation uses the actual
    // supplied map and cached metadata answers; it never invents metadata or
    // substitutes terrain raw coordinates for a mismatching legacy map key.
    RuntimePlannerIndexedTopology(const RuntimeWindowState& state,const WorldObjectInstance& mover,
        const TerrainMap& terrain,const OccupancyMap& occupancy,bool enabled=true)
        :previous_(runtimeActivePlannerIndexedTopology),state_(&state),mover_(&mover),terrain_(&terrain),
          occupancy_(&occupancy),cellData_(state.world.getTerrainCells().data()),
          cellCount_(state.world.getTerrainCells().size()),terrainCount_(terrain.size()),
          terrainBuckets_(terrain.bucket_count()),metadataGeneration_(state.terrainInfoByCell.generation()),tick_(state.tick) {
        prepare(state,terrain,enabled);
        // Install only after preparation returns, so an allocation exception
        // leaves the old TLS owner intact instead of exposing a dead object.
        runtimeActivePlannerIndexedTopology=this;
    }
    ~RuntimePlannerIndexedTopology() {
        runtimeActivePlannerIndexedTopology=previous_;
        if(runtimePlannerIndexedTopologyObserver)
            runtimePlannerIndexedTopologyObserver(runtimePlannerIndexedTopologyObserverContext,
                enabled_,geometry_.size(),pairs_,fallbacks_);
    }
    RuntimePlannerIndexedTopology(const RuntimePlannerIndexedTopology&)=delete;
    RuntimePlannerIndexedTopology& operator=(const RuntimePlannerIndexedTopology&)=delete;
    RuntimePlannerIndexedTopology(RuntimePlannerIndexedTopology&&)=delete;
    RuntimePlannerIndexedTopology& operator=(RuntimePlannerIndexedTopology&&)=delete;
    void invalidate() noexcept { enabled_=false; }
    bool enabled() const noexcept { return enabled_; }
    std::size_t retainedSlots() const noexcept { return geometry_.size(); }
    std::size_t retainedBytes() const noexcept { return geometry_.size()*sizeof(Geometry); }
    std::size_t pairs() const noexcept { return pairs_; }
    std::size_t fallbacks() const noexcept { return fallbacks_; }
    static bool allowsGeometry(const Pair& pair,PathMovementMode mode) noexcept {
        const auto& current=*pair.current; const auto& next=*pair.next;
        if(mode!=PathMovementMode::Amphibious && current.water!=next.water
            && !(current.shore || next.shore || current.bridge || next.bridge)) return false;
        const int delta=std::abs(next.effectiveLevel-current.effectiveLevel);
        if(delta>1) return false;
        if(delta>0 && !(current.ramp || next.ramp || current.bridge || next.bridge)) return false;
        return true;
    }
    std::optional<Pair> pairFor(const RuntimeWindowState& state,const WorldObjectInstance& mover,
        const TerrainMap& terrain,const OccupancyMap& occupancy,
        const RuntimeCellPoint& current,const RuntimeCellPoint& next) const {
        // The Pair is used synchronously inside this ONE transition only.
        // In-place level/overlay/metadata VALUE edits still require invalidate
        // BEFORE the write; shape checks do not pretend to be content revisions.
        if(!enabled_ || runtimeActivePlannerIndexedTopology!=this
            || runtimeComponentIndexedTopologyReference() || state_!=&state || mover_!=&mover
            || terrain_!=&terrain || occupancy_!=&occupancy || tick_!=state.tick
            || terrainCount_!=terrain.size() || terrainBuckets_!=terrain.bucket_count()
            || cellData_!=state.world.getTerrainCells().data() || cellCount_!=state.world.getTerrainCells().size()
            || metadataGeneration_!=state.terrainInfoByCell.generation()
            || !canonical(current.x,current.y) || !canonical(next.x,next.y)) {
            ++fallbacks_; return std::nullopt;
        }
        ++pairs_; return Pair{at(current),at(next)};
    }
};
