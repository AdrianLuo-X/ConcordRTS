#pragma once

#include <vector>

struct RuntimeCellPoint;

namespace concord::runtime::session_detail {

struct RuntimeGameSessionState;

bool runtimeSelectedUnitsCanHarvestAt(
    const RuntimeGameSessionState& state,
    const std::vector<int>& selectedObjectIds,
    RuntimeCellPoint cell);

} // namespace concord::runtime::session_detail
