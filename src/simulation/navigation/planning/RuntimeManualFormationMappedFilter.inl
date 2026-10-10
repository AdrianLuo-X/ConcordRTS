// Include before formation finders, after RuntimeWindowState and unit lookup.
// Opt-in ONLY from a manual command. Automatic finder callers pass nullptr.
#pragma once
#include <cstdlib>

inline thread_local int runtimeManualFormationMappedReferenceOverride=-1;
bool runtimeManualFormationMappedReference() noexcept {
    if(runtimeManualFormationMappedReferenceOverride>=0)
        return runtimeManualFormationMappedReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_MANUAL_FORMATION_MAPPED_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
class RuntimeManualFormationMappedReferenceScope {
    int previous_;
public:
    explicit RuntimeManualFormationMappedReferenceScope(bool reference) noexcept
        :previous_(runtimeManualFormationMappedReferenceOverride) {
        runtimeManualFormationMappedReferenceOverride=reference?1:0;
    }
    ~RuntimeManualFormationMappedReferenceScope() {
        runtimeManualFormationMappedReferenceOverride=previous_;
    }
    RuntimeManualFormationMappedReferenceScope(const RuntimeManualFormationMappedReferenceScope&)=delete;
    RuntimeManualFormationMappedReferenceScope& operator=(const RuntimeManualFormationMappedReferenceScope&)=delete;
};

// The finder is synchronously read-only. State/mover/owner/vision/rules must
// remain unchanged until this stack object and the finder return. Nothing is
// retained in world state, a generic path queue, or a cross-command cache.
class RuntimeManualFormationMappedFilter {
    const RuntimeWindowState* state_;
    const WorldObjectInstance* mover_;
    bool restricted_=false;
    bool hasObserver_=false;
public:
    RuntimeManualFormationMappedFilter(const RuntimeWindowState& state,
        const WorldObjectInstance& mover,const UnitType* knownUnit=nullptr,
        bool enabled=!runtimeManualFormationMappedReference())
        :state_(&state),mover_(&mover) {
        if(!enabled || !state.visionEnabled) return;
        const UnitType* unit=knownUnit?knownUnit:findRuntimeUnitType(state.rules,mover);
        restricted_=unit && !unit->moveToShroud;
        if(restricted_) hasObserver_=state.vision.hasObserver(mover.owner);
    }
    const RuntimeManualFormationMappedFilter* restriction() const noexcept {
        return restricted_?this:nullptr;
    }
    bool allows(const RuntimeCellPoint& cell) const {
        // Retail original predicate uses owner Mapped, even with NoFog.
        return !restricted_ || (hasObserver_
            && state_->vision.isCellExploredBy(mover_->owner,cell.x,cell.y));
    }
};
