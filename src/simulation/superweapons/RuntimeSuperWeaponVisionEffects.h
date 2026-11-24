#pragma once
class RuntimeVisionSystem;
struct SuperWeaponEvent;
namespace concord::simulation::superweapons {
// Applies the original reconnaissance event policy to the canonical vision owner.
// Returns the original mutation result; presentation invalidation stays with the caller.
bool applyRuntimeSuperWeaponVisionEvent(bool enabled, const SuperWeaponEvent& event,
    RuntimeVisionSystem& vision);
}
