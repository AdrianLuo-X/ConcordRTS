#pragma once

#include <string>

struct RuntimeMapRenderContext;

bool runRuntimeBuildingDebrisPaletteContractCheck(
    const RuntimeMapRenderContext& context,
    std::string& errorMessage
);
