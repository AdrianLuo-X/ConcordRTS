#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <string>

struct BuildingType;
struct RuntimeMapCamera;

// Original turret animation screen offset in signed screen pixels.
// The original footprint/camera helper and all arithmetic stay unchanged.
RuntimeInputPixelPoint runtimeStructureTurretAnimScreenOffsetForRender(
    const BuildingType& buildingType,
    const std::string& structureType,
    int footprintWidth,
    int footprintHeight,
    const RuntimeMapCamera& camera
);
