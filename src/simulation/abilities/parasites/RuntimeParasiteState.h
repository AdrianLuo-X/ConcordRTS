#pragma once
#include "RuntimeParasiteValues.h"
#include <map>
struct WorldObjectInstance;

// Each lookup borrows the original first matching object for this call only.
class RuntimeParasiteObjectQueries {
public:
    virtual ~RuntimeParasiteObjectQueries() = default;
    virtual const WorldObjectInstance* findObject(int objectId) const = 0;
};
class RuntimeParasiteState {
public:
    void reset();
    void swap(RuntimeParasiteState& other) noexcept;
    ParasiteOperationResult beginParasiteLaunch(const ParasiteLaunchRequest& request,
        const SpecialEventSink& emit);
    ParasiteOperationResult resolveParasiteImpact(const ParasiteImpactRequest& request,
        const SpecialEventSink& emit);
    ParasiteOperationResult handleParasiteHostEvent(const ParasiteHostEventRequest& request,
        const SpecialEventSink& emit);
    std::vector<SpecialMechanismAction> reconcileParasites(const RuntimeParasiteObjectQueries& queries,
        const std::vector<int>& activeProjectileIds, int tick, const SpecialEventSink& emit);
    std::vector<SpecialMechanismAction> tickParasites(const RuntimeParasiteObjectQueries& queries,
        int tick, const SpecialEventSink& emit);
    bool isParasiteSourceLimboed(int objectId) const;
    int parasiteTargetForSource(int objectId) const;
    bool isParasiteHostParalyzed(int objectId) const;
    void onObjectRemoved(const RuntimeParasiteObjectQueries& queries, int objectId, int tick,
        const SpecialEventSink& emit, const std::string& reason,
        std::vector<SpecialMechanismAction>* actions);
    const std::map<int, ParasitePendingLaunchState>& pendingParasiteLaunches() const;
    const std::map<int, ParasiteLinkState>& parasiteLinks() const;
    const std::map<int, ParasiteResolvedLaunchState>& resolvedParasiteLaunches() const;
private:
    // Only the original aggregate's uncommitted whole-codec transaction writes
    // these maps directly; ordinary callers use the typed state operations.
    friend class RuntimeSpecialMechanismState;
    std::map<int, ParasitePendingLaunchState> pendingByProjectile_;
    std::map<int, ParasiteLinkState> byTarget_;
    std::map<int, ParasiteResolvedLaunchState> resolvedByProjectile_;
};
