#pragma once

#include "core/io/IniFile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class TheaterDatabase;

struct MapLightingProfile {
    double ambient = 1.0;
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
    double ground = 0.0;
    double level = 0.032;
};

struct MapLighting {
    bool sectionPresent = false;
    std::size_t authoredFieldCount = 0;
    MapLightingProfile base;
    MapLightingProfile ion;
    // FinalAlert's Yuri's Revenge scenario defaults for Psychic Dominator
    // lighting. Stock maps normally author these fields explicitly.
    MapLightingProfile dominator{1.5, 0.85, 0.2, 0.3, 0.0, 0.0};
    double dominatorAmbientChangeRate = 0.009;
};

struct MapSummary {
    std::size_t sectionCount = 0;

    std::string name;
    std::string briefing;
    std::string theater;
    std::string size;
    std::string localSize;
    std::string nextScenario;
    std::string multiplayerOnly;

    int startX = -1;
    int startY = -1;
    int width = -1;
    int height = -1;
    int numberStartingPoints = -1;
    int coopHumanStartSpots = -1;

    bool lightingSectionPresent = false;
    std::size_t lightingFieldCount = 0;

    std::size_t houseCount = 0;
    std::size_t waypointCount = 0;
    std::size_t terrainCount = 0;
    std::size_t structureCount = 0;
    std::size_t unitCount = 0;
    std::size_t infantryCount = 0;
    std::size_t aircraftCount = 0;
    std::size_t smudgeCount = 0;
    std::size_t cellTagCount = 0;
    std::size_t triggerCount = 0;
    std::size_t eventCount = 0;
    std::size_t actionCount = 0;
    std::size_t tagCount = 0;
    std::size_t scriptTypeCount = 0;
    std::size_t taskForceCount = 0;
    std::size_t teamTypeCount = 0;
    std::size_t aiTriggerEnableCount = 0;
    std::size_t isoMapPackLineCount = 0;
    std::size_t overlayPackLineCount = 0;
    std::size_t overlayDataPackLineCount = 0;
};

struct MapPreviewImage {
    int width = 0;
    int height = 0;

    // Top-down, tightly packed BGRA32 pixels in B, G, R, A byte order.
    // Alpha is always 255. For Win32 StretchDIBits, use BI_RGB, 32 bpp,
    // and a negative BITMAPINFOHEADER::biHeight to preserve the row order.
    std::vector<unsigned char> pixels;
};

// Decodes the Format5/LZO1X [PreviewPack] whose dimensions are declared by
// [Preview] Size=x,y,width,height. The packed source is RGB888; successful
// output is converted to the BGRA32 layout documented by MapPreviewImage.
bool decodeMapPreviewImage(
    const IniFile& ini,
    MapPreviewImage& image,
    std::string& errorMessage
);

struct MapPosition {
    int x = -1;
    int y = -1;
    int cell = -1;
    bool valid = false;
};

struct MapWaypoint {
    std::string id;
    std::string cellValue;
    MapPosition position;
};

struct MapSmudgeObject {
    std::string id;
    std::string type;
    int x = -1;
    int y = -1;
    // Authored fourth [Smudge] field. Stock maps normally use zero, but the
    // value is preserved so runtime smudge state can round-trip faithfully.
    int data = 0;
    MapPosition position;
};

struct MapTerrainObject {
    // [Terrain] uses the encoded cell key as its stable authored identity.
    std::string mapId;
    std::string cell;
    std::string type;
    // The stock format has no terrain tag field.
    std::string tag;
    int strength = 1;
    int health = 1;
    bool immune = false;
    bool alive = true;
    int temperateOccupationBits = 7;
    int snowOccupationBits = 7;
    int occupationBits = 7;
    std::vector<int> occupiedSubCells{0, 1, 2, 3, 4};
    int footprintWidth = 1;
    int footprintHeight = 1;
    MapPosition position;
};

struct MapStructureObject {
    std::string id;
    std::string owner;
    std::string type;
    // Authored map health uses the Westwood 0..256 proportional scale.
    int health = -1;
    // Resolved rules Strength used when instantiating absolute hit points.
    int strength = -1;
    int x = -1;
    int y = -1;
    int facing = -1;
    std::string tag;
    bool leaveRubble = false;
    MapPosition position;
};

struct MapUnitObject {
    std::string id;
    std::string owner;
    std::string type;
    // Authored map health uses the Westwood 0..256 proportional scale.
    int health = -1;
    int strength = -1;
    int x = -1;
    int y = -1;
    int facing = -1;
    std::string mission;
    std::string tag;
    int veterancy = 0;
    bool onBridge = false;
    MapPosition position;
};

struct MapInfantryObject {
    std::string id;
    std::string owner;
    std::string type;
    // Authored map health uses the Westwood 0..256 proportional scale.
    int health = -1;
    int strength = -1;
    int x = -1;
    int y = -1;
    int subCell = -1;
    std::string mission;
    int facing = -1;
    std::string tag;
    int veterancy = 0;
    bool onBridge = false;
    MapPosition position;
};

struct MapAircraftObject {
    std::string id;
    std::string owner;
    std::string type;
    // Authored map health uses the Westwood 0..256 proportional scale.
    int health = -1;
    int strength = -1;
    int x = -1;
    int y = -1;
    int facing = -1;
    std::string mission;
    std::string tag;
    int veterancy = 0;
    MapPosition position;
};

struct MapIsoTile {
    int x = 0;
    int y = 0;
    int tileIndex = 0;
    int subTileIndex = 0;
    int level = 0;
    int iceGrowth = 0;
};

struct MapOverlayCell {
    int cellIndex = -1;
    int x = -1;
    int y = -1;
    int overlayIndex = -1;
    int frameIndex = 0;
    int frame = 0;
    int fallbackFrame = 0;
    int overlayData = 0;
    std::string type;
    std::string image;
    bool isOre = false;
    bool isGem = false;
};

struct MapOverlayTypeCount {
    int overlayIndex = -1;
    std::string type;
    std::size_t count = 0;
    bool isOre = false;
    bool isGem = false;
};

struct MapOverlaySummary {
    std::size_t cellCount = 0;
    std::size_t bytesPerCell = 0;
    std::size_t nonEmptyCount = 0;
    std::size_t oreCount = 0;
    std::size_t gemCount = 0;
    std::size_t unknownTypeCount = 0;
    std::vector<MapOverlayTypeCount> typeCounts;
};

struct MapTheaterTileFact {
    bool present = false;
    bool validTileSet = false;
    bool fileExists = false;
    int tileIndex = -1;
    int subTileIndex = -1;
    int tileSetIndex = -1;
    int tileIndexInSet = -1;
    std::string tileSetName;
    std::string baseFileName;
    std::string expectedFileName;
    std::string resolvedPath;
    std::vector<std::string> candidateFileNames;
};

struct MapOverlayResourceFact {
    bool present = false;
    std::string overlayKind;
    std::string kind;
    std::string baseName;
    std::string image;
    int requestedFrame = -1;
    int fallbackFrame = -1;
    int resourceFrame = -1;
    std::string paletteHint;
    std::string theaterHint;
    std::string rendererLayerHint;
    std::vector<std::string> candidateFileNames;
};

struct MapCellFacts {
    int x = -1;
    int y = -1;
    int cellIndex = -1;
    bool hasTerrain = false;
    bool hasOverlay = false;
    MapIsoTile terrain;
    MapOverlayCell overlay;
    bool isResourceCell = false;
    bool isOre = false;
    bool isGem = false;
    bool isBridge = false;
    bool isHighBridge = false;
    bool isLowBridge = false;
    bool isWater = false;
    bool isShore = false;
    bool isCliff = false;
    bool isRamp = false;
    bool isTunnel = false;
    bool occupiedByStructure = false;
    int occupyingStructureCount = 0;
    std::string occupyingStructureIds;
    std::string occupyingStructureTypes;
    std::string occupyingStructureOwners;
    int heightLevel = 0;
    std::string terrainClass;
    std::string passabilityHint;
    std::string theaterTileReference;
    MapTheaterTileFact theaterTile;
    MapOverlayResourceFact overlayResource;
};

struct MapStructureFootprintOverride {
    std::string type;
    int width = 1;
    int height = 1;
    std::string source;
};

struct MapFootprintCellFact {
    int x = -1;
    int y = -1;
    int cellIndex = -1;
    int heightLevel = 0;
    bool inBounds = false;
    bool hasMapCell = false;
    bool occupiedByFootprint = false;
    bool edge = false;
    bool approach = false;
    bool isWater = false;
    bool isShore = false;
    bool isBridge = false;
    bool isCliff = false;
    bool isRamp = false;
    bool isTunnel = false;
    bool isResourceCell = false;
    bool hasOverlay = false;
    bool isOre = false;
    bool isGem = false;
    bool isWall = false;
    bool isOverlayBlocker = false;
    bool occupiedByStructure = false;
    bool occupiedByOtherStructure = false;
    int occupyingStructureCount = 0;
    int overlayIndex = -1;
    int overlayFrame = -1;
    int overlayResourceFrame = -1;
    int overlayFallbackFrame = -1;
    bool buildable = false;
    std::string purpose;
    std::string terrainClass;
    std::string overlayKind;
    std::string overlayType;
    std::string overlayImage;
    std::string occupyingStructureIds;
    std::string occupyingStructureTypes;
    std::string occupyingStructureOwners;
    std::string occupancyReason;
    std::string passabilityHint;
    std::string buildableReason;
    std::string blockedReason;
    std::string fallbackHint;
};

struct MapIsoDiamondBounds {
    bool valid = false;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    int width = 0;
    int height = 0;
    std::string coordinateSpace;
};

struct MapStructureAnchorFact {
    std::string id;
    std::string owner;
    std::string type;
    int x = -1;
    int y = -1;
    int footprintWidth = 1;
    int footprintHeight = 1;
    int rearCellX = -1;
    int rearCellY = -1;
    std::string anchorFormula;
    std::string footprintSource;
    MapIsoDiamondBounds footprintIsoDiamondBounds;
    MapIsoDiamondBounds placementPreviewIsoDiamondBounds;
    MapIsoDiamondBounds hitTestIsoDiamondBounds;
    MapIsoDiamondBounds selectionIsoDiamondBounds;
    bool footprintBuildable = false;
    int footprintBuildableCellCount = 0;
    int footprintIllegalCellCount = 0;
    int footprintWaterCellCount = 0;
    int footprintShoreCellCount = 0;
    int footprintBridgeCellCount = 0;
    int footprintResourceCellCount = 0;
    int footprintOverlayBlockerCellCount = 0;
    int footprintOccupiedCellCount = 0;
    int footprintOtherStructureCellCount = 0;
    std::string footprintLegality;
    std::string placementPreviewFormula;
    std::string hitTestFormula;
    std::string selectionFormula;
    std::string finalStructureRectFormula;
    std::vector<MapFootprintCellFact> footprintCells;
    std::vector<MapFootprintCellFact> footprintEdgeCells;
    std::vector<MapFootprintCellFact> approachCells;
    std::vector<MapFootprintCellFact> attackApproachCells;
    std::vector<MapFootprintCellFact> engineerApproachCells;
    std::vector<MapFootprintCellFact> repairApproachCells;
    std::vector<MapFootprintCellFact> occupyApproachCells;
    std::string blockedTargetFallbackHint;
};

struct MapFacts {
    std::string schemaVersion = "MapFacts.v8";
    std::uint64_t contentHash = 0;
    std::string contentHashAlgorithm = "fnv1a64-mapfacts";
    std::string ownershipModel =
        "value-owned immutable snapshot after build; pass by const reference or shared ownership";
    std::string queryLifetime =
        "MapQuery is a non-owning const view; keep the referenced MapFacts alive";
    std::string theater;
    int width = -1;
    int height = -1;
    MapLighting lighting;
    std::size_t terrainCellCount = 0;
    std::size_t overlayCellCount = 0;
    std::size_t resourceCellCount = 0;
    std::size_t oreCellCount = 0;
    std::size_t gemCellCount = 0;
    std::size_t bridgeCellCount = 0;
    std::size_t highBridgeCellCount = 0;
    std::size_t lowBridgeCellCount = 0;
    std::size_t waterCellCount = 0;
    std::size_t shoreCellCount = 0;
    std::size_t cliffCellCount = 0;
    std::size_t rampCellCount = 0;
    std::size_t tunnelCellCount = 0;
    std::size_t missingTheaterTileCount = 0;
    std::size_t lookupIndexSize = 0;
    std::vector<MapCellFacts> cells;
    std::unordered_map<int, std::size_t> cellIndexLookup;
    std::vector<std::size_t> resourceCellOffsets;
    std::vector<std::size_t> oreCellOffsets;
    std::vector<std::size_t> gemCellOffsets;
    std::vector<std::size_t> bridgeCellOffsets;
    std::vector<std::size_t> highBridgeCellOffsets;
    std::vector<std::size_t> lowBridgeCellOffsets;
    std::vector<std::size_t> waterCellOffsets;
    std::vector<std::size_t> shoreCellOffsets;
    std::vector<std::size_t> cliffCellOffsets;
    std::vector<std::size_t> rampCellOffsets;
    std::vector<std::size_t> tunnelCellOffsets;
    std::vector<MapStructureAnchorFact> structureAnchors;
    std::unordered_map<std::string, std::size_t> structureAnchorIdLookup;
    std::size_t structureFootprintCellCount = 0;
    std::size_t structureEdgeCellCount = 0;
    std::size_t structureApproachCellCount = 0;
    std::size_t structureAttackApproachCellCount = 0;
    std::size_t structureEngineerApproachCellCount = 0;
    std::size_t structureRepairApproachCellCount = 0;
    std::size_t structureOccupyApproachCellCount = 0;
    std::size_t structureBuildableFootprintCellCount = 0;
    std::size_t structureIllegalFootprintCellCount = 0;
    std::size_t structureWaterFootprintCellCount = 0;
    std::size_t structureShoreFootprintCellCount = 0;
    std::size_t structureBridgeFootprintCellCount = 0;
    std::size_t structureResourceFootprintCellCount = 0;
    std::size_t structureOverlayBlockerFootprintCellCount = 0;
    std::size_t structureOccupiedMapCellCount = 0;
    std::size_t structureOccupiedFootprintCellCount = 0;
    std::size_t structureOtherOccupiedFootprintCellCount = 0;
    std::vector<std::string> warnings;
};

class MapQuery {
public:
    explicit MapQuery(const MapFacts& facts);

    bool isValid() const;
    const MapFacts* getFacts() const;
    std::uint64_t getFactsContentHash() const;
    const MapCellFacts* getCell(int x, int y) const;
    const MapCellFacts* getCellByIndex(int cellIndex) const;
    const MapCellFacts* getCellByCellIndex(int cellIndex) const;
    std::vector<const MapCellFacts*> getResourceCells() const;
    std::vector<const MapCellFacts*> getOreCells() const;
    std::vector<const MapCellFacts*> getGemCells() const;
    std::vector<const MapCellFacts*> getBridgeCells() const;
    std::vector<const MapCellFacts*> getHighBridgeCells() const;
    std::vector<const MapCellFacts*> getLowBridgeCells() const;
    std::vector<const MapCellFacts*> getWaterCells() const;
    std::vector<const MapCellFacts*> getShoreCells() const;
    std::vector<const MapCellFacts*> getCliffCells() const;
    std::vector<const MapCellFacts*> getRampCells() const;
    std::vector<const MapCellFacts*> getTunnelCells() const;
    const MapStructureAnchorFact* getStructureAnchorById(const std::string& id) const;
    std::vector<MapFootprintCellFact> getStructureFootprintCells(
        const MapStructureAnchorFact& anchor
    ) const;
    std::vector<MapFootprintCellFact> getStructureFootprintCellsById(
        const std::string& id
    ) const;
    const MapIsoDiamondBounds* getStructureFootprintIsoDiamondBoundsById(
        const std::string& id
    ) const;
    bool isStructureFootprintBuildableById(const std::string& id) const;
    std::vector<MapFootprintCellFact> getStructureEdgeCells(
        const MapStructureAnchorFact& anchor
    ) const;
    std::vector<MapFootprintCellFact> getStructureEdgeCellsById(
        const std::string& id
    ) const;
    std::vector<MapFootprintCellFact> getStructureApproachCells(
        const MapStructureAnchorFact& anchor,
        const std::string& purpose
    ) const;
    std::vector<MapFootprintCellFact> getStructureApproachCellsById(
        const std::string& id,
        const std::string& purpose
    ) const;
    std::vector<MapFootprintCellFact> getStructureAttackApproachCells(
        const MapStructureAnchorFact& anchor
    ) const;
    std::vector<MapFootprintCellFact> getStructureEngineerApproachCells(
        const MapStructureAnchorFact& anchor
    ) const;
    std::vector<MapFootprintCellFact> getStructureRepairApproachCells(
        const MapStructureAnchorFact& anchor
    ) const;
    std::vector<MapFootprintCellFact> getStructureOccupyApproachCells(
        const MapStructureAnchorFact& anchor
    ) const;

private:
    const MapFacts* facts_ = nullptr;
};

struct MapPackedChunkSummary {
    std::size_t offset = 0;
    std::size_t compressedBytes = 0;
    std::size_t uncompressedBytes = 0;
};

struct MapPackedSectionSummary {
    std::string sectionName;
    std::string payloadCodec;
    std::size_t lineCount = 0;
    std::size_t encodedChars = 0;
    std::size_t decodedBytes = 0;
    std::size_t chunkCount = 0;
    std::size_t totalCompressedBytes = 0;
    std::size_t totalUncompressedBytes = 0;
    std::size_t payloadDecodedBytes = 0;
    std::size_t payloadDecodedChunks = 0;
    std::size_t payloadDecodeFailureCount = 0;
    std::size_t payloadRecordSize = 0;
    std::size_t payloadRecordCount = 0;
    std::size_t payloadRemainderBytes = 0;
    bool base64Decoded = false;
    bool chunkTableValid = false;
    bool payloadDecodeAttempted = false;
    bool payloadDecoded = false;
    std::vector<MapPackedChunkSummary> chunks;
};

class MapDatabase {
public:
    bool loadFromIni(const IniFile& ini, const IniFile* rulesIni = nullptr);

    const MapSummary& getSummary() const;
    const MapLighting& getLighting() const;
    const std::vector<MapWaypoint>& getWaypoints() const;
    const std::vector<MapSmudgeObject>& getSmudges() const;
    const std::vector<MapTerrainObject>& getTerrainObjects() const;
    const std::vector<MapStructureObject>& getStructures() const;
    const std::vector<MapUnitObject>& getUnits() const;
    const std::vector<MapInfantryObject>& getInfantry() const;
    const std::vector<MapAircraftObject>& getAircraft() const;
    const std::vector<MapIsoTile>& getIsoTiles() const;
    const std::vector<MapOverlayCell>& getOverlayCells() const;
    const MapOverlaySummary& getOverlaySummary() const;
    const MapPackedSectionSummary& getIsoMapPackSummary() const;
    const MapPackedSectionSummary& getOverlayPackSummary() const;
    const MapPackedSectionSummary& getOverlayDataPackSummary() const;
    const std::vector<std::string>& getWarnings() const;
    MapFacts buildMapFacts(
        const TheaterDatabase* theaterDatabase = nullptr,
        const std::string& tileRootPath = ""
    ) const;
    MapFacts buildMapFacts(
        const TheaterDatabase* theaterDatabase,
        const std::string& tileRootPath,
        const std::vector<MapStructureFootprintOverride>& footprintOverrides
    ) const;

private:
    MapSummary summary_;
    MapLighting lighting_;
    std::vector<MapWaypoint> waypoints_;
    std::vector<MapSmudgeObject> smudges_;
    std::vector<MapTerrainObject> terrainObjects_;
    std::vector<MapStructureObject> structures_;
    std::vector<MapUnitObject> units_;
    std::vector<MapInfantryObject> infantry_;
    std::vector<MapAircraftObject> aircraft_;
    std::vector<MapIsoTile> isoTiles_;
    std::vector<MapOverlayCell> overlayCells_;
    MapOverlaySummary overlaySummary_;
    MapPackedSectionSummary isoMapPackSummary_;
    MapPackedSectionSummary overlayPackSummary_;
    MapPackedSectionSummary overlayDataPackSummary_;
    std::vector<std::string> warnings_;
};
