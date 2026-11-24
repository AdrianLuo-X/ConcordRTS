#pragma once
struct SuperWeaponWorldContext;
namespace concord::runtime::session_detail {
struct RuntimeProductionHostState;
// Bind the existing session owners to the two typed domain ports.
bool advanceRuntimeSuperWeaponAi(RuntimeProductionHostState& state,
    bool computerAiEnabled, const SuperWeaponWorldContext& context);
}
