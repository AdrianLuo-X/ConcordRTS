#pragma once
#include <vector>
struct SuperWeaponObjectLifecycleEvent;
namespace concord::runtime::session_detail {
struct RuntimeProductionHostState;
// Host-only binding; the domain completion policy receives narrow typed ports.
void completeRuntimeDominatorOwnerChanges(RuntimeProductionHostState& state,
    const std::vector<SuperWeaponObjectLifecycleEvent>& events);
}
