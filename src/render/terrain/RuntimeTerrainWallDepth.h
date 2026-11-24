#pragma once

#include "ui/input/RuntimeInputPixelValues.h"

#include <optional>
#include <span>

struct OverlayType;
struct TerrainCell;
struct WorldObjectInstance;

namespace concord::render {

// The selected rule is borrowed from the original ordered overlay registry.
const OverlayType* runtimeFindTerrainDepthWallRule(
    const TerrainCell& cell, std::span<const OverlayType> overlays);
bool runtimeTerrainHasDepthWalls(
    std::span<const TerrainCell> cells, std::span<const OverlayType> overlays);

std::optional<int> runtimeTerrainDepthWallInstanceId(int x, int y) noexcept;
bool runtimeTerrainDepthWallVisible(RuntimeInputPixelPoint center, RuntimeInputPixelSize tileSize,
    const RuntimeInputPixelRect& viewport) noexcept;
WorldObjectInstance runtimeTerrainDepthWallObject(
    const TerrainCell& cell, const OverlayType& rule, int instanceId);

} // namespace concord::render
