#pragma once

#include "simulation/navigation/terrain/RuntimeTerrainTypes.h"

struct TerrainCell;

// Synchronous read-only domain queries. The private adapter retains the
// original resource caches; no renderer/window type or untyped handle escapes.
class RuntimeNavigationTerrainQueryPort {
public:
    virtual ~RuntimeNavigationTerrainQueryPort() = default;
    virtual bool theaterLoaded() const = 0;
    virtual bool overlayWallRuleForCell(const TerrainCell& terrain) const = 0;
    virtual bool overlayWallUsesCurrentOverlay(const TerrainCell& terrain) const = 0;
    virtual bool overlayProvidesBridgeDeck(const TerrainCell& terrain) const = 0;
    virtual bool highBridgeFrameIsDisconnected(const TerrainCell& terrain) const = 0;
    virtual bool overlayIsBrokenLowBridgePlaceholder(const TerrainCell& terrain) const = 0;
    virtual RuntimeNavigationTerrainInfo baseTerrainInfoForCell(const TerrainCell& terrain) const = 0;
    virtual RuntimeTerrainLandType subTileLandType(const TerrainCell& terrain) = 0;
    virtual unsigned char subTileRampType(const TerrainCell& terrain) = 0;
    virtual bool cellHasDisconnectedHighBridgeDeck(const TerrainCell& terrain) const = 0;
};

// Complete original navigation policy. Keep separate land/ramp/shore-land
// calls, their short circuits, and the entire sentinel TerrainCell copy.
RuntimeNavigationTerrainInfo queryRuntimeNavigationTerrainInfo(
    RuntimeNavigationTerrainQueryPort& port,
    const TerrainCell& terrain);