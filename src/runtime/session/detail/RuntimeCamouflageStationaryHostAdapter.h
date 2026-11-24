#pragma once
namespace concord::runtime::session_detail {
struct RuntimeProductionHostState;
bool runtimeCamouflageObjectIsStationary(const RuntimeProductionHostState& state, int objectId);
}
