#include "render/terrain/RuntimeTerrainWallDepth.h"

#include "core/io/RuntimeSaveTextFields.h"
#include "data/rules/RulesDatabase.h"
#include "world/model/GameWorld.h"

#include <algorithm>
#include <limits>

namespace concord::render {

const OverlayType* runtimeFindTerrainDepthWallRule(
    const TerrainCell& cell, std::span<const OverlayType> overlays) {
    if (!cell.hasOverlay) return nullptr;
    // OverlayPack stores a zero-based registry ordinal. listIndex is the INI
    // key, which contains gaps; matching it can turn an unrelated rock into
    // a wall. Resolved map identity is authoritative when it is available.
    if (cell.overlayIndex >= 0 && cell.overlayIndex < static_cast<int>(overlays.size())) {
        const auto& type = overlays[static_cast<std::size_t>(cell.overlayIndex)];
        if (cell.overlayType.empty() || runtime_save_text::toLowerAsciiLocal(type.id) == runtime_save_text::toLowerAsciiLocal(cell.overlayType))
            return type.wall ? &type : nullptr;
    }
    if (!cell.overlayType.empty()) {
        const auto identity = runtime_save_text::toLowerAsciiLocal(cell.overlayType);
        for (const auto& type : overlays)
            if (runtime_save_text::toLowerAsciiLocal(type.id) == identity) return type.wall ? &type : nullptr;
    }
    return nullptr;
}

bool runtimeTerrainHasDepthWalls(
    std::span<const TerrainCell> cells, std::span<const OverlayType> overlays) {
    return std::any_of(cells.begin(), cells.end(), [&](const TerrainCell& cell) {
        return runtimeFindTerrainDepthWallRule(cell, overlays) != nullptr;
    });
}

std::optional<int> runtimeTerrainDepthWallInstanceId(int x, int y) noexcept {
    if (x < 0 || y < 0 || x >= 512 || y >= 512) return std::nullopt;
    return std::numeric_limits<int>::min() + y * 512 + x;
}

bool runtimeTerrainDepthWallVisible(RuntimeInputPixelPoint center, RuntimeInputPixelSize tileSize,
    const RuntimeInputPixelRect& viewport) noexcept {
    const RuntimeInputPixelRect bounds{center.x - tileSize.width, center.y - tileSize.height * 4,
        center.x + tileSize.width, center.y + tileSize.height * 3};
    return runtimeInputPixelRectsIntersect(bounds, viewport);
}

WorldObjectInstance runtimeTerrainDepthWallObject(
    const TerrainCell& cell, const OverlayType& rule, int instanceId) {
    WorldObjectInstance wall;
    wall.kind = WorldObjectKind::Structure;
    wall.type = rule.id;
    wall.x = cell.x;
    wall.y = cell.y;
    wall.level = cell.level;
    wall.instanceId = instanceId;
    wall.alive = wall.poweredOperational = true;
    wall.health = 1;
    return wall;
}

} // namespace concord::render
