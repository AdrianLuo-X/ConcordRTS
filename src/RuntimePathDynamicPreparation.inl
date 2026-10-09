// Include after MetadataQueries, beside the existing snapshot helper includes.
// ONE synchronous builder only. End/invalidate before changing ANY source.
// Identity/shape/tick are defensive fences, not an in-place content revision.
class RuntimePathDynamicPreparation {
public:
    using Workset = RuntimePathDynamicWorkset<PathTerrainCell>;
private:
    const RuntimeWindowState& state_;
    const WorldObjectInstance& mover_;
    std::vector<PathTerrainCell>& snapshot_;
    const Workset::Occupants& occupants_;
    const void* previous_;
    RuntimePathDynamicPreparationObserver observer_;
    void* observerContext_;
    bool nonAir_, ground_, naval_, tiberium_, eligible_, enabled_ = true;
    bool resourceFusion_; double resourcePercent_;
    bool denseAuthoredLandPolicyDispatched_=false;
    std::optional<RuntimePathSnapshotMetadataQueries> metadata_;
    std::optional<RuntimePathSnapshotMetadataQueries::ReadOnlyView> metadataView_;
    std::optional<Workset> workset_;
    RuntimePathDynamicPreparationStats stats_;
public:
    RuntimePathDynamicPreparation(const RuntimeWindowState& state,
        const WorldObjectInstance& mover,std::vector<PathTerrainCell>& snapshot,
        const Workset::Occupants& occupants,bool eligible,bool nonAir,bool ground,bool naval,bool tiberium,bool resourceFusion=false,double resourcePercent=100.0)
        : state_(state),mover_(mover),snapshot_(snapshot),occupants_(occupants),
          previous_(runtimeActivePathDynamicPreparation),observer_(runtimePathDynamicPreparationObserver),
          observerContext_(runtimePathDynamicPreparationObserverContext),nonAir_(nonAir),ground_(ground),naval_(naval),
          tiberium_(tiberium),eligible_(eligible),resourceFusion_(resourceFusion),resourcePercent_(resourcePercent) {
        stats_.entries=snapshot.size();stats_.reference=runtimePathDynamicPreparationReference();
        runtimeActivePathDynamicPreparation=this;
    }
    ~RuntimePathDynamicPreparation() {
        // Member Workset remains alive through the original body and tails.
        // Test observers are pure diagnostics and may not edit World/state.
        runtimeActivePathDynamicPreparation=previous_;
        runtimeRecordPathDynamicPreparation(stats_);
        if(observer_)observer_(observerContext_,&state_,stats_);
    }
    RuntimePathDynamicPreparation(const RuntimePathDynamicPreparation&) = delete;
    RuntimePathDynamicPreparation& operator=(const RuntimePathDynamicPreparation&) = delete;
    RuntimePathDynamicPreparation(RuntimePathDynamicPreparation&&) = delete;
    RuntimePathDynamicPreparation& operator=(RuntimePathDynamicPreparation&&) = delete;
    void prepareMetadata() {
        if(metadata_)return;
        metadata_.emplace(state_,snapshot_.size(),nonAir_);
        metadataView_=metadata_->viewFor(state_);
    }
    template<class VisitRepair,class LandAllows,class DenseLand=RuntimePathOriginalAuthoredLandPermission>
    void prepare(bool early,VisitRepair&& visitRepair,LandAllows&& landAllows,DenseLand denseLand={}) {
        if(workset_)return;
        if(early && (!enabled_ || stats_.reference || runtimePathDynamicPreparationReference() || previous_
                || !eligible_ || runtimePathDynamicWorksetReference()))return;
        std::optional<RuntimePerfTraceScope> preparationTrace;
        if(runtimePathDynamicPreparationTraceEnabled())
            preparationTrace.emplace("pathDynamicPreparationProof",state_.tick);
        prepareMetadata();
        const auto findMetadata=[&](int key){return metadataView_?metadataView_->find(key)
            :RuntimePathSnapshotMetadataQueries::originalFind(state_,key);};
        const auto construct=[&](auto collectBounds,auto resourcePolicy,auto landPolicy) {
            denseAuthoredLandPolicyDispatched_=std::decay_t<decltype(landPolicy)>::exactDenseAuthoredLand;
            workset_.emplace(&state_,&mover_,state_.tick,snapshot_,occupants_,eligible_ && enabled_,
                ground_,naval_,tiberium_,std::forward<VisitRepair>(visitRepair),findMetadata,
                std::forward<LandAllows>(landAllows),collectBounds,resourcePolicy,landPolicy);
        };
        // One constructor dispatch per builder, never one dispatch per row.
        // Late/reference preparation retains C33's original scan arithmetic.
        const auto dispatch=[&](auto landPolicy) {
            if(resourceFusion_) {
                if(early)construct(std::true_type{},RuntimePathDynamicResourceFusionPolicy{resourcePercent_},landPolicy);
                else construct(std::false_type{},RuntimePathDynamicResourceFusionPolicy{resourcePercent_},landPolicy);
            } else {
                if(early)construct(std::true_type{},std::false_type{},landPolicy);
                else construct(std::false_type{},std::false_type{},landPolicy);
            }
        };
        // Only the private early builder may request the exact ordinal path.
        // Public/default and late/reference calls instantiate the old policy.
        if constexpr (DenseLand::exactDenseAuthoredLand) {
            if(early && denseLand.available())dispatch(denseLand);
            else dispatch(RuntimePathOriginalAuthoredLandPermission{});
        } else dispatch(RuntimePathOriginalAuthoredLandPermission{});
        stats_.earlyPrepared=early;
        stats_.admitted=workset_->viewFor(&state_,&mover_,state_.tick,snapshot_,occupants_).has_value();
    }
    std::optional<Workset::ReadOnlyView> earlyView() {
        if(!enabled_ || !stats_.earlyPrepared || runtimePathDynamicPreparationReference()
            || runtimeActivePathDynamicPreparation!=this || !workset_)return std::nullopt;
        return workset_->viewFor(&state_,&mover_,state_.tick,snapshot_,occupants_);
    }
    const RuntimePathDynamicPreparationBounds* boundsFor(bool needed) {
        if(!needed || !earlyView() || !workset_->boundsCollected())return nullptr;
        if(collectsStats()){++stats_.boundsPassesAvoided;stats_.boundsRowsAvoided+=snapshot_.size();}
        return &workset_->bounds();
    }
    Workset& workset(){return *workset_;}
    void finishWorkset() noexcept { workset_.reset(); }
    const auto& metadataView() const noexcept{return metadataView_;}
    bool permitsDenseAuthoredLand() const noexcept {
        return enabled_ && eligible_ && !previous_ && !stats_.reference && !observer_
            && runtimeActivePathDynamicPreparation==this;
    }
    RuntimePathDenseAuthoredLandPreparationStats denseAuthoredLandObservation(bool eligible,bool acquired) {
        RuntimePathDenseAuthoredLandPreparationStats result;
        result.eligible=eligible;result.arrayAcquired=acquired;result.entries=snapshot_.size();
        result.policyDispatched=denseAuthoredLandPolicyDispatched_;
        result.proofAdmitted=earlyView().has_value();
        result.denseAdmitted=result.policyDispatched && result.proofAdmitted;
        if(workset_)result.candidates=workset_->stats().candidates;
        return result;
    }
    const std::vector<std::size_t>* infantryIndices() {
        return earlyView()?&workset_->indicesForDiagnostics():nullptr;
    }
    void recordInfantryCollection(std::size_t visited,std::size_t destinations) noexcept {
        if(!collectsStats())return;
        stats_.infantryRangeEntries+=visited;stats_.infantryOriginalRangeEntries+=snapshot_.size();
        stats_.infantryDestinations+=destinations;
    }
    bool collectsStats() const noexcept{return observer_ || runtimePathDynamicPreparationTraceEnabled();}
    const RuntimePathDynamicPreparationStats& stats() const noexcept{return stats_;}
    void invalidate() noexcept {
        enabled_=false;
        if(workset_)workset_->invalidate();
        if(metadata_)metadata_->invalidate();
        metadataView_.reset();
    }
};

// Declared at the original late Workset point, after ConditionalQueries.
// Restore the original Workset-before-Conditional diagnostic destruction order
// on normal return and on exception, even when proof was prepared early.
class RuntimePathDynamicPreparationWorksetExit {
    RuntimePathDynamicPreparation& owner_;
public:
    explicit RuntimePathDynamicPreparationWorksetExit(RuntimePathDynamicPreparation& owner) noexcept : owner_(owner) {}
    ~RuntimePathDynamicPreparationWorksetExit(){owner_.finishWorkset();}
    RuntimePathDynamicPreparationWorksetExit(const RuntimePathDynamicPreparationWorksetExit&)=delete;
    RuntimePathDynamicPreparationWorksetExit& operator=(const RuntimePathDynamicPreparationWorksetExit&)=delete;
};
