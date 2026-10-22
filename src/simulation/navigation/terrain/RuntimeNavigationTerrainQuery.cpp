#include "simulation/navigation/terrain/RuntimeNavigationTerrainQuery.h"
#include "world/model/GameWorld.h"

#include <limits>

RuntimeNavigationTerrainInfo queryRuntimeNavigationTerrainInfo(
    RuntimeNavigationTerrainQueryPort& port,
    const TerrainCell& terrain
) {
    if (terrain.overlayWall.health > 0 && port.overlayWallRuleForCell(terrain)
        && port.overlayWallUsesCurrentOverlay(terrain)) {
        RuntimeNavigationTerrainInfo blocked;
        blocked.kind = RuntimeTerrainKind::Blocked;
        blocked.passable = false;
        blocked.cost = std::numeric_limits<int>::max() / 4;
        blocked.movementLandType = blocked.authoredLandType = RuntimeTerrainLandType::Wall;
        return blocked;
    }
    // Resolve only ordinary sentinel ground through canonical metadata. Intact
    // bridge overlays retain their previous underlying movement-surface policy.
    if (terrain.tileIndex == 65535 && port.theaterLoaded()
        && !port.overlayProvidesBridgeDeck(terrain)
        && !port.highBridgeFrameIsDisconnected(terrain)
        && !port.overlayIsBrokenLowBridgePlaceholder(terrain)) {
        TerrainCell clear = terrain;
        clear.tileIndex = 0;
        clear.subTileIndex = 0;
        return queryRuntimeNavigationTerrainInfo(port, clear);
    }
    RuntimeNavigationTerrainInfo info = port.baseTerrainInfoForCell(terrain);
    info.movementLandType = port.subTileLandType(terrain);
    info.movementRampType = port.subTileRampType(terrain);
    if (info.bridge && !port.overlayProvidesBridgeDeck(terrain)
        && port.cellHasDisconnectedHighBridgeDeck(terrain)) {
        info.bridge = false;
        info.authoredLandType = info.movementLandType;
        info.water = info.movementLandType == RuntimeTerrainLandType::Water;
        info.kind = info.water ? RuntimeTerrainKind::Water : RuntimeTerrainKind::Land;
        info.cost = info.water ? 18
            : (info.movementLandType == RuntimeTerrainLandType::Road ? 8 : 10);
        if (info.movementLandType == RuntimeTerrainLandType::Rock
            || info.movementLandType == RuntimeTerrainLandType::Wall) {
            info.kind = RuntimeTerrainKind::Blocked;
            info.passable = false;
            info.cost = std::numeric_limits<int>::max() / 4;
        }
    }
    if (info.kind != RuntimeTerrainKind::Shore) {
        return info;
    }

    info.authoredLandType = port.subTileLandType(terrain);
    if (info.authoredLandType == RuntimeTerrainLandType::Water) {
        info.kind = RuntimeTerrainKind::Water;
        info.water = true;
        info.shore = false;
        info.cost = 18;
    } else if (info.authoredLandType == RuntimeTerrainLandType::Rough
               || info.authoredLandType == RuntimeTerrainLandType::Clear) {
        // Landward shoreline cells remain ordinary traversable land.
        info.kind = RuntimeTerrainKind::Land;
        info.water = false;
        info.shore = false;
        info.cost = 10;
    }
    return info;
}

