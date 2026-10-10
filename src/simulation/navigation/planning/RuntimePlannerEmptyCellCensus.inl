// Include after footprint/origin helpers, before hard blocker/subcell queries.
inline thread_local int runtimeComponentEmptyCensusReferenceOverride = -1;
bool runtimeComponentEmptyCensusReference() {
    if (runtimeComponentEmptyCensusReferenceOverride >= 0)
        return runtimeComponentEmptyCensusReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_COMPONENT_EMPTY_CENSUS_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeComponentEmptyCensusReferenceScope {
    int previous_;
public:
    explicit RuntimeComponentEmptyCensusReferenceScope(bool reference)
        : previous_(runtimeComponentEmptyCensusReferenceOverride) {
        runtimeComponentEmptyCensusReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeComponentEmptyCensusReferenceScope() {
        runtimeComponentEmptyCensusReferenceOverride = previous_;
    }
    RuntimeComponentEmptyCensusReferenceScope(const RuntimeComponentEmptyCensusReferenceScope&) = delete;
    RuntimeComponentEmptyCensusReferenceScope& operator=(const RuntimeComponentEmptyCensusReferenceScope&) = delete;
};

class RuntimePlannerEmptyCellCensusScope;
inline thread_local RuntimePlannerEmptyCellCensusScope* runtimeActivePlannerEmptyCellCensus = nullptr;
enum class RuntimePlannerEmptyCellQuery { Probe, HardBlocker, Availability };
using RuntimePlannerEmptyCensusObserver = void(*)(void*,bool,std::size_t,std::size_t,std::size_t);
inline thread_local RuntimePlannerEmptyCensusObserver runtimePlannerEmptyCensusObserver = nullptr;
inline thread_local void* runtimePlannerEmptyCensusObserverContext = nullptr;
class RuntimePlannerEmptyCensusObserverScope {
    RuntimePlannerEmptyCensusObserver previous_;
    void* previousContext_;
public:
    RuntimePlannerEmptyCensusObserverScope(RuntimePlannerEmptyCensusObserver observer,void* context)
        : previous_(runtimePlannerEmptyCensusObserver),previousContext_(runtimePlannerEmptyCensusObserverContext) {
        runtimePlannerEmptyCensusObserver=observer; runtimePlannerEmptyCensusObserverContext=context;
    }
    ~RuntimePlannerEmptyCensusObserverScope() {
        runtimePlannerEmptyCensusObserver=previous_; runtimePlannerEmptyCensusObserverContext=previousContext_;
    }
    RuntimePlannerEmptyCensusObserverScope(const RuntimePlannerEmptyCensusObserverScope&) = delete;
    RuntimePlannerEmptyCensusObserverScope& operator=(const RuntimePlannerEmptyCensusObserverScope&) = delete;
};

// A conservative census for ONE synchronous read-only complete BFS. Mark ALL
// alive objects regardless of health, ID, transport, air, magbeam, alliance or
// layer, and ALL alive terrain footprints. A zero bit proves absence for both
// the hard blocker and subcell resident predicates; a set bit proves nothing.
// Noncanonical/oversized origins or rectangles disable the entire proof.
//
// End/invalidate BEFORE any source object/terrain, rule/footprint, gate, mover,
// behavior or reservation edit. State/tick checks are defensive, not a content
// revision. Disabled inner scopes deliberately mask outer scopes.
class RuntimePlannerEmptyCellCensusScope {
    RuntimePlannerEmptyCellCensusScope* previous_;
    const RuntimeWindowState* state_;
    int tick_;
    bool enabled_ = false;
    std::vector<std::uint64_t> bits_;
    std::size_t marked_ = 0, rectangleWork_ = 0;
    mutable std::size_t proofs_ = 0, hardProofs_ = 0, availabilityProofs_ = 0;
    static constexpr int side_ = 512;
    static constexpr std::size_t maximumRectangleWork_ = 1048576;
    bool markRectangle(int x, int y, int rawWidth, int rawHeight) {
        const std::int64_t width = std::max(1,rawWidth), height = std::max(1,rawHeight);
        if (x < 0 || y < 0 || x >= side_ || y >= side_
            || width > side_ || height > side_
            || static_cast<std::int64_t>(x) + width > side_
            || static_cast<std::int64_t>(y) + height > side_) return false;
        const auto work=static_cast<std::size_t>(width*height);
        if (work>maximumRectangleWork_-rectangleWork_) return false;
        rectangleWork_+=work;
        for (int row=y; row<static_cast<std::int64_t>(y)+height; ++row)
            for (int column=x; column<static_cast<std::int64_t>(x)+width; ++column) {
                const auto index = static_cast<std::size_t>(row)*side_+column;
                auto& word = bits_[index/64]; const auto mask = std::uint64_t{1}<<(index%64);
                if (!(word&mask)) { word|=mask; ++marked_; }
            }
        return true;
    }
    bool prepare(const RuntimeWindowState& state) {
        RuntimePerfTraceScope trace("componentEmptyCensusBuild",state.tick);
        bits_.assign(static_cast<std::size_t>(side_)*side_/64,std::uint64_t{0});
        const auto poseContext=runtimeGroundPoseContext(state);
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive) continue;
            bool poseValid=true;
            forEachRuntimeAdditionalGroundPoseOccupancy(object,[&](const auto& held) {
                poseValid=markRectangle(held.x,held.y,1,1) && poseValid;
            },&poseContext);
            if (!poseValid) return false;
            if (!markRectangle(object.x,object.y,1,1)) return false;
            const auto origin = runtimeCellPointForObject(object);
            if (!markRectangle(origin.x,origin.y,1,1)) return false;
            if (runtimeObjectUsesStructureFootprint(state,object)) {
                const auto footprint = runtimeStructureFootprintForObject(state,object);
                if (!markRectangle(object.x,object.y,footprint.width,footprint.height)
                    || !markRectangle(origin.x,origin.y,footprint.width,footprint.height)) return false;
            }
        }
        for (const auto& terrain : state.world.getTerrainObjects()) {
            if (!terrain.alive) continue;
            if (!markRectangle(terrain.x,terrain.y,terrain.footprintWidth,terrain.footprintHeight)) return false;
        }
        return true;
    }
public:
    explicit RuntimePlannerEmptyCellCensusScope(const RuntimeWindowState& state, bool enabled=true)
        : previous_(runtimeActivePlannerEmptyCellCensus), state_(&state), tick_(state.tick) {
        enabled_ = enabled && !runtimeComponentEmptyCensusReference() && prepare(state);
        // Publish only after all potentially throwing allocations/footprint
        // queries. Unsupported/disabled scopes still mask an outer census.
        runtimeActivePlannerEmptyCellCensus = this;
    }
    ~RuntimePlannerEmptyCellCensusScope() {
        runtimeActivePlannerEmptyCellCensus = previous_;
        if (runtimePlannerEmptyCensusObserver)
            runtimePlannerEmptyCensusObserver(runtimePlannerEmptyCensusObserverContext,
                enabled_,marked_,hardProofs_,availabilityProofs_);
    }
    RuntimePlannerEmptyCellCensusScope(const RuntimePlannerEmptyCellCensusScope&) = delete;
    RuntimePlannerEmptyCellCensusScope& operator=(const RuntimePlannerEmptyCellCensusScope&) = delete;
    RuntimePlannerEmptyCellCensusScope(RuntimePlannerEmptyCellCensusScope&&) = delete;
    RuntimePlannerEmptyCellCensusScope& operator=(RuntimePlannerEmptyCellCensusScope&&) = delete;
    void invalidate() noexcept { enabled_ = false; }
    bool enabled() const noexcept { return enabled_; }
    std::size_t marked() const noexcept { return marked_; }
    std::size_t proofs() const noexcept { return proofs_; }
    std::size_t hardProofs() const noexcept { return hardProofs_; }
    std::size_t availabilityProofs() const noexcept { return availabilityProofs_; }
    std::size_t rectangleWork() const noexcept { return rectangleWork_; }
    std::size_t bytes() const noexcept { return bits_.size()*sizeof(std::uint64_t); }
    bool provesEmpty(const RuntimeWindowState& state, const RuntimeCellPoint& cell,
        RuntimePlannerEmptyCellQuery kind=RuntimePlannerEmptyCellQuery::Probe) const {
        if (!enabled_ || runtimeActivePlannerEmptyCellCensus != this || state_ != &state
            || tick_ != state.tick || runtimeComponentEmptyCensusReference()
            || cell.x < 0 || cell.y < 0 || cell.x >= side_ || cell.y >= side_) return false;
        const auto index = static_cast<std::size_t>(cell.y)*side_+cell.x;
        if (bits_[index/64] & (std::uint64_t{1}<<(index%64))) return false;
        ++proofs_;
        if (kind==RuntimePlannerEmptyCellQuery::HardBlocker) ++hardProofs_;
        else if (kind==RuntimePlannerEmptyCellQuery::Availability) ++availabilityProofs_;
        return true;
    }
};

bool runtimePlannerCensusProvesEmptyCell(const RuntimeWindowState& state,
    const RuntimeCellPoint& cell,RuntimePlannerEmptyCellQuery kind=RuntimePlannerEmptyCellQuery::Probe) {
    const auto* scope = runtimeActivePlannerEmptyCellCensus;
    return scope && scope->provesEmpty(state,cell,kind);
}
