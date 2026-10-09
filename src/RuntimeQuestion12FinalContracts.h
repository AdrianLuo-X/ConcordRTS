#pragma once
#include "RuntimeMapRenderer.h"

bool runRuntimeSuperWeaponAnimationRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& hosts,
    const std::vector<std::vector<SpecialMechanismRenderSnapshot>>& phases, std::string& error);
bool runRuntimeFlakHeightRenderContractCheck(const RuntimeMapRenderContext& context,
    const WorldObjectInstance& source, std::string& error);
