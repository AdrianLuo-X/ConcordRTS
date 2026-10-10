#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "data/art/ArtDatabase.h"
#include "world/model/GameWorld.h"
#include "assets/archive/MixArchive.h"
#include "simulation/vision/RuntimeVision.h"
#include "render/units/RuntimeVehicleBodyDynamics.h"
#include "simulation/combat/damage/RuntimeWreck.h"
#include "simulation/buildings/RuntimeBuildingCollapse.h"
#include "render/effects/RuntimeScreenShake.h"
#include "simulation/combat/projectiles/RuntimeProjectileLineTrail.h"
#include "simulation/effects/RuntimeParticleSystem.h"
#include "render/shadows/RuntimeShadowOpaqueSpan.h"
#include "render/voxel/RuntimeVoxelLoadingPrewarm.h"
#include "data/art/TheaterDatabase.h"

#include <array>
#include <cstdint>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct MapFacts;
struct MapCellFacts;
struct BuildingType;
struct SpecialMechanismRenderSnapshot;
struct RuntimeLightingTint;
struct MapLightingProfile;
struct RuntimeLocalLightSnapshot;
struct RuntimeParticleRenderSnapshot;
struct RuntimeParticlePoint;
struct RuntimeParticleBeamSnapshot;
struct RuntimeParticleAirbornePresentation;

enum class RuntimeGateRenderPhase {
    Closed,
    Opening,
    Open,
    Closing
};

// Value-only renderer input. normalizedProgress is the current phase's
// clamped 0..1 progress; Closing therefore maps it in reverse.
struct RuntimeGateRenderSnapshot {
    int objectId = -1;
    RuntimeGateRenderPhase phase = RuntimeGateRenderPhase::Closed;
    double normalizedProgress = 0.0;
};

struct RuntimePaletteColor {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
};

struct RuntimePaletteCacheEntry {
    std::string key;
    bool loadAttempted = false;
    bool loaded = false;
    std::vector<RuntimePaletteColor> palette;
    std::string sourcePath;
};

struct RuntimeTmpSubTileImage {
    bool present = false;
    bool hasTerrainType = false;
    unsigned char terrainType = 0;
    unsigned char rampType = 0;
    bool hasRadarColors = false;
    RuntimePaletteColor radarLeft;
    RuntimePaletteColor radarRight;
    int offsetX = 0;
    int offsetY = 0;
    int width = 0;
    int height = 0;
    int globalWidth = 0;
    int globalHeight = 0;
    std::vector<unsigned char> pixels;
    mutable std::uint64_t packedPixelPaletteHash = 0;
    mutable std::vector<std::uint32_t> packedPixels;
    mutable int resourcePixelNonZeroCount = -1;
    int baseOffsetX = 0;
    int baseOffsetY = 0;
    int baseWidth = 0;
    int baseHeight = 0;
    std::vector<unsigned char> basePixels;
    mutable int resourceBaseNonZeroCount = -1;
    std::vector<unsigned char> baseZData;
    bool hasBaseZData = false;
    bool hasExtra = false;
    int extraOffsetX = 0;
    int extraOffsetY = 0;
    int extraWidth = 0;
    int extraHeight = 0;
    std::vector<unsigned char> extraPixels;
    mutable int resourceExtraNonZeroCount = -1;
    std::vector<unsigned char> extraZData;
    bool hasExtraZData = false;
};

struct RuntimeTmpTileFile {
    bool loadAttempted = false;
    bool loaded = false;
    int blockWidth = 0;
    int blockHeight = 0;
    std::vector<RuntimeTmpSubTileImage> images;
    std::string errorMessage;
    // Authored terrain grid, including empty image slots. SHP-style raw
    // format-3 resources do not encode a terrain grid and leave this unknown.
    int gridWidth = 0;
    int gridHeight = 0;
};

struct RuntimeTerrainVisualVariant {
    std::string locator;
    std::string sourceName;
    std::string resourceName;
};

struct RuntimeTerrainVisualFamily {
    std::string canonicalLocator;
    std::string canonicalSourceName;
    std::vector<RuntimeTerrainVisualVariant> variants;
    std::uint32_t rejectedVariantCount = 0;
    bool siblingOnly = false;
};

struct RuntimeTerrainVisualCellInfo {
    int tileIndex = -1;
    int subTileIndex = -1;
    bool mappedClear = false;
    bool authoredEmptySubTile = false;
    bool selectedVariantFallback = false;
    std::string canonicalResourceName;
    std::string selectedResourceName;
    std::string canonicalLocator;
    std::string selectedLocator;
    std::string canonicalSourceName;
    std::string selectedSourceName;
    std::uint64_t selectionHash = 0;
    std::uint32_t eligibleVariantCount = 0;
    std::uint32_t rejectedVariantCount = 0;
    bool siblingOnly = false;
};

// Retail land-type ids after translating the TMP terrain byte. These values
// match the engine's LandType table and are simulation data, not art labels.
enum class RuntimeTerrainLandType : int {
    Unknown = -1,
    Clear = 0,
    Road = 1,
    Water = 2,
    Rock = 3,
    Wall = 4,
    Tiberium = 5,
    Beach = 6,
    Rough = 7,
    Ice = 8,
    Railroad = 9,
    Tunnel = 10
};

struct RuntimeVxlRasterPathStats {
    int pointSamplesWritten = 0;
    int cuboidFacesRasterized = 0;
    int indexedCoveragePixelsResolved = 0;
    int supersampledPixelsResolved = 0;
    int triangleFacesRasterized = 0;
    int coverageZOverwrites = 0;
};

// Horizontal nontransparent run. right is exclusive; rows are sorted by y/left.
struct RuntimeSpriteHitSpan {
    LONG y = 0;
    LONG left = 0;
    LONG right = 0;
};

struct RuntimeShpFrameImage {
    int offsetX = 0;
    int offsetY = 0;
    int globalWidth = 0;
    int globalHeight = 0;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
    std::vector<unsigned char> rawColorIndices;
    std::vector<unsigned char> ownerRemapMask;
    std::vector<unsigned char> lightLevels;
    std::vector<unsigned char> normalIndices;
    std::vector<unsigned char> normalBrightnessLevels;
    std::vector<unsigned char> vplSections;
    std::vector<unsigned char> depthLevels;
    // Final scene-Z bounds from projected VXL section boxes, excluding shadow
    // allocation. These retain retail's four-pixel padding at either edge.
    bool sceneDepthBoundsValid = false;
    int sceneDepthBoundsTop = 0;
    int sceneDepthBoundsBottom = 0;
    std::vector<unsigned int> pixelMaterials;
    unsigned char paletteRemapStart = 0;
    unsigned char paletteRemapEnd = 0;
    bool paletteRemapRangeFromHeader = false;
    int fallbackOwnerRemapCount = 0;
    int zeroColorVoxelCount = 0;
    RuntimeVxlRasterPathStats rasterPathStats;
    std::string rasterFunctionActuallyCalled;
    std::string rasterCacheKey;
    bool rasterCacheHit = false;
    bool vxlAlreadyVplResolved = false;
    bool lightLevelsApplied = false;
    bool remapLightApplied = false;
    int opaqueBoundsWidth = 0;
    int opaqueBoundsHeight = 0;
    int topFacePixelCount = 0;
    int sideFacePixelCount = 0;
    int lowerFacePixelCount = 0;
    int pointSampleCount = 0;
    int faceSampleCount = 0;
    int exposedFaceCount = 0;
    std::string sourcePath;
    mutable bool packedColorTablesValid = false;
    mutable const RuntimePaletteColor* packedColorPaletteSource = nullptr;
    mutable COLORREF packedColorRemap = RGB(255, 255, 255);
    mutable int packedColorBrightnessLift = 0;
    mutable double packedColorGradeContrast = 1.0;
    mutable double packedColorGradeSaturation = 1.0;
    mutable double packedColorGradeValueLift = 0.0;
    mutable double packedColorExtraUnitLight = 0.0;
    mutable unsigned char packedColorRemapStart = 16;
    mutable unsigned char packedColorRemapEnd = 31;
    mutable bool packedColorOwnerRemap = false;
    mutable bool packedColorGradeEnabled = false;
    mutable bool packedColorVxlPaletteLookup = false;
    mutable std::vector<std::uint32_t> packedBaseColors;
    mutable std::vector<std::uint32_t> packedRemapColors;
    // Decoded frames are immutable after insertion in the render cache.
    // Keep their opaque crop alongside the palette caches for cheap bounds queries.
    mutable bool spriteOpaqueBoundsValid = false;
    mutable RECT spriteOpaqueBounds{};
    mutable bool spriteOpaqueSpansValid = false;
    mutable std::vector<RuntimeSpriteHitSpan> spriteOpaqueSpans;
    // Shadow occupancy is independent of the body image's opaque geometry.
    mutable RuntimeShadowOpaqueSpanCache shadowMaskSpanCache;
    mutable bool infantryContactScanned = false;
    mutable bool infantryContactValid = false;
    mutable POINT infantryContact{};
};

struct RuntimeObjectShpSpriteCacheEntry {
    bool loadAttempted = false;
    bool loaded = false;
    RuntimeShpFrameImage image;
    RuntimeShpFrameImage shadowImage;
    std::string resolvedImageId;
    std::string sourcePath;
    int frameIndex = 0;
    int frameCount = 0;
    int shadowFrameIndex = -1;
    bool hasShadowFrame = false;
    bool usedSequenceFrame = false;
    std::uint64_t sourceFingerprint = 0;
    std::string sequenceName;
    std::string errorMessage;
};

struct RuntimeAuxiliaryShpBytesCacheEntry {
    bool loadAttempted = false;
    bool loaded = false;
    std::vector<unsigned char> bytes;
    std::uint64_t sourceFingerprint = 0;
    std::string resolvedImageId;
    std::string sourcePath;
    int frameCount = 0;
    std::string errorMessage;
};

struct RuntimeVoxelMetadata {
    std::string imageId;
    std::string vxlPath;
    std::string hvaPath;
    int vxlSectionCount = 0;
    int hvaSectionCount = 0;
    int hvaFrameCount = 0;
    std::uint64_t vxlFileSize = 0;
    std::uint64_t hvaFileSize = 0;
    std::string vxlSectionSample;
    std::string hvaSectionSample;
};

struct RuntimeHvaTransform {
    bool valid = false;
    std::array<double, 12> m{
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0
    };
};

struct RuntimeVoxelSourceCacheEntry {
    bool loadAttempted = false;
    bool loaded = false;
    RuntimeVoxelMetadata metadata;
    std::vector<unsigned char> vxlBytes;
    std::vector<unsigned char> hvaBytes;
    std::vector<RuntimeHvaTransform> firstFrameTransforms;
    std::unordered_map<int, std::vector<RuntimeHvaTransform>> transformsByFrame;
    std::string errorMessage;
};

struct RuntimeVoxelCacheEntry {
    // Value handles remain valid when a render cache is copied or moved.
    std::uint64_t vehicleBodyPoseUse = 0;
    RuntimeVoxelModelBounds modelBounds;
    bool loadAttempted = false;
    bool loaded = false;
    bool hasSprite = false;
    RuntimeVoxelMetadata metadata;
    RuntimeShpFrameImage image;
    std::vector<unsigned char> shadowPixels;
    mutable RuntimeShadowOpaqueSpanCache shadowSpanCache;
    int shadowSectionRequested = 0;
    int shadowSectionApplied = 0;
    int facingBucket = 0;
    int animationFrameIndex = 0;
    double rasterFitScale = 0.0;
    double projectedRangeX = 0.0;
    double projectedRangeY = 0.0;
    std::string errorMessage;
};

struct RuntimeVehicleBodyPoseCacheReference {
    std::string rasterKey;
    std::string lookupKey;
};

struct RuntimeVxlFixedScaleCacheEntry {
    bool loadAttempted = false;
    bool valid = false;
    double fitScale = 0.0;
    double assetMaxRangeX = 0.0;
    double assetMaxRangeY = 0.0;
    double targetWidth = 0.0;
    double targetHeight = 0.0;
    std::string category;
    std::string mode;
    std::string errorMessage;
};

struct RuntimeRenderFootprintCacheEntry {
    int width = 0;
    int height = 0;
};

struct RuntimeTerrainLayerCache {
    bool built = false;
    std::string key;
    int width = 0;
    int height = 0;
    int originX = 0;
    int originY = 0;
    int terrainCells = 0;
    int texturedTerrainCells = 0;
    int fallbackTerrainCells = 0;
    int authoredEmptyTerrainSubTiles = 0;
    int highBridgeTerminalUnderlays = 0;
    std::vector<std::uint32_t> pixels;
    // Source map cell for each opaque terrain pixel. TMP cliff and ramp art
    // extends beyond the base isometric diamond, so the vision compositor
    // needs this ownership map to cover those authored overhangs exactly.
    std::vector<std::int32_t> terrainCellIndices;
    // Interaction ownership is separate from shroud ownership: bridge deck
    // pixels can belong to an elevated, walkable cell while fog still needs
    // the authored terrain tile beneath them.
    std::vector<std::int32_t> surfaceCellIndices;
    std::unordered_set<int> bridgeOverlayCellIndexes;
};

// Only the terrain portion is retained. Object silhouettes are written into a
// private frame copy, so moving objects cannot leave stale light receivers.
// Value ownership also keeps diagnostic copies of the render cache independent.
struct RuntimeMapLightTerrainCache {
    bool valid = false;
    std::string key;
    std::vector<int> pixels;
    std::vector<std::array<std::uint32_t, 256>> lookups;
    bool firstTintAvailable = false;
    std::array<std::uint64_t, 3> firstTintBits{};
    std::size_t reusedTables = 0;
};

// Camera- and revision-bound fog/shroud compositor output. Each encoded pixel
// stores a 32-bit surface offset followed by an 8-bit opacity and 24-bit DIB
// tint. Reusing this list avoids rebuilding and blurring a full-screen mask on
// animation-only frames while still applying fog to the freshly drawn world.
struct RuntimeNativeShroudAtlas {
    std::string key;
    std::string sourcePath;
    bool attempted = false;
    bool loaded = false;
    std::uint64_t fingerprint = 0;
    std::vector<unsigned char> alpha;
};

struct RuntimeVisionOverlayTileCell {
    int cellIndex = -1;
    int centerX = 0;
    int centerY = 0;
};

// Only opaque, contiguous runs of at least eight pixels are retained. The
// original encoded list remains the independent diagnostic reference, while
// at most one run per eight source pixels (512 runs per 64x64 tile). Actual
// vector capacity is separately reported, including retained allocation slack.
struct RuntimeVisionOpaqueRun {
    std::uint32_t encodedBegin = 0;
    std::uint32_t surfaceOffset = 0;
    std::uint32_t length = 0;
    std::uint32_t tint = 0;
};

struct RuntimeVisionOverlayTile {
    int left = 0, top = 0, right = 0, bottom = 0;
    std::vector<RuntimeVisionOverlayTileCell> geometricCells;
    std::vector<std::uint64_t> encodedPixels;
    std::vector<RuntimeVisionOpaqueRun> opaqueRuns;
    std::size_t opaqueRunPixels = 0;
};

// Independent of camera/viewport. Exact geometry values are checked only when
// an overlay must be rebuilt; warm animation frames never scan the map here.
// Flat source storage avoids one allocation for every projected map cell.
struct RuntimeNativeShroudGeometryCache {
    bool valid = false;
    int minX = 512, minY = 512, columns = 0, rows = 0;
    std::vector<std::array<int, 4>> terrainGeometry;
    std::vector<std::size_t> sourceOffsets;
    std::vector<int> sourceIndices;
};

// Value-owned so copied render caches never share mutable incremental state.
// Geometry and TMP ownership dependencies use the parent's complete identity.
struct RuntimeVisionIncrementalOverlayCache {
    static constexpr int tileSize = 64;
    bool valid = false;
    int left = 0, top = 0, right = 0, bottom = 0;
    int columns = 0;
    int rows = 0;
    std::vector<unsigned char> mask;
    // Shroud is tiled on one screen plane. Elevated terrain maps to this grid;
    // independent per-height diamonds leave hard seams at cliff boundaries.
    int nativeMinX = 0, nativeMinY = 0, nativeColumns = 0, nativeRows = 0;
    std::vector<std::vector<int>> nativeSources;
    std::vector<RuntimeVisionOverlayTile> tiles;
    std::unordered_map<int, std::vector<int>> tilesByCell;
    std::size_t encodedPixelCount = 0;
    std::uint64_t rebuiltMaskPixels = 0;
    std::uint64_t rebuiltEffectPixels = 0;
};

struct RuntimeVisionOverlayCache {
    bool valid = false;
    bool performanceReference = false;
    std::uint64_t nativeShroudFingerprint = 0;
    const RuntimeVisionSystem* vision = nullptr;
    std::uint64_t visionRevision = 0;
    std::uint64_t visionLayoutEpoch = 0;
    const std::uint8_t* visibleCells = nullptr;
    const std::uint8_t* exploredCells = nullptr;
    const std::uint8_t* gapShroudedCells = nullptr;
    std::string observerOwner;
    const TerrainCell* terrainData = nullptr;
    std::size_t terrainCount = 0;
    bool terrainLayerBuilt = false;
    std::string terrainLayerKey;
    const std::int32_t* terrainLayerCellData = nullptr;
    std::size_t terrainLayerCellCount = 0;
    int terrainLayerWidth = 0;
    int terrainLayerHeight = 0;
    int terrainLayerOriginX = 0;
    int terrainLayerOriginY = 0;
    int viewportLeft = 0;
    int viewportTop = 0;
    int viewportRight = 0;
    int viewportBottom = 0;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    int worldOffsetX = 0;
    int worldOffsetY = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int visibleCellCount = 0;
    int foggedCellCount = 0;
    int shroudedCellCount = 0;
    int hits = 0;
    int misses = 0;
    // Visible and Fogged cells have identical compositor output. Compare the
    // exact shrouded mask when sight changes, retaining general vision revision
    // semantics for gameplay and refreshing visible/fogged statistics below.
    std::vector<std::uint8_t> shroudedCellMask;
    std::vector<int> viewportTerrainCellIndices;
    std::vector<std::uint64_t> encodedPixels;
    RuntimeVisionIncrementalOverlayCache incremental;
    RuntimeNativeShroudGeometryCache nativeGeometry;
};

struct RuntimeSequenceFrameMetadataCacheEntry {
    bool matched = false;
    int startFrame = 0;
    int framesPerFacing = 1;
    int facingStride = 1;
    int facingBucket = 0;
    bool reversedDeployFallback = false;
    std::string sequenceName;
};

struct RuntimeBuildingAnimationLayerCacheEntry {
    std::vector<int> activeIndices;
    std::vector<int> specialIndices;
    std::vector<int> auxiliaryIndices;
    std::vector<int> sortedLayerCodes;
};

struct RuntimeMixArchiveCacheEntry {
    bool loadAttempted = false;
    bool loaded = false;
    MixArchive archive;
    std::string errorMessage;
};

struct RuntimeMapCamera {
    int worldOffsetX = 0;
    int worldOffsetY = 0;
    int tileWidth = 60;
    int tileHeight = 30;
    bool initialized = false;
};

inline int runtimeMuzzlePixelX(double value, const RuntimeMapCamera& camera, double fixed = 0.0) {
    return static_cast<int>(std::lround(fixed + (value - fixed) * camera.tileWidth / 60.0));
}
inline int runtimeMuzzlePixelY(double value, const RuntimeMapCamera& camera, double fixed = 0.0) {
    return static_cast<int>(std::lround(fixed + (value - fixed) * camera.tileHeight / 30.0));
}

struct RuntimeProjectileRenderActor {
    bool hasFlightDirection = false;
    double flightDirectionX = 0.0;
    double flightDirectionY = 0.0;
    double flightDirectionZ = 0.0;
    bool voxelDebris = false;
    std::array<double, 4> debrisOrientation{0, 0, 0, 1};
    int actorId = -1;
    int targetObjectId = -1;
    std::string owner;
    std::string projectileId;
    std::string projectileImageId;
    std::string weaponId;
    std::string weaponAnimId;
    std::string warheadId;
    std::string trailerAnimId;
    std::string impactAnimId;
    std::string particleId;
    std::string lifecycleState;
    std::string renderIntent;
    double fromX = 0.0;
    double fromY = 0.0;
    double fromLevel = 0.0;
    int sourceScreenOffsetX = 0;
    int sourceScreenOffsetY = 0;
    int sourceNativeFixedX = 0;
    int sourceNativeFixedY = 0;
    double toX = 0.0;
    double toY = 0.0;
    double toLevel = 0.0;
    double currentX = 0.0;
    double currentY = 0.0;
    double currentLevel = 0.0;
    double progress = 0.0;
    int totalTicks = 0;
    int remainingTicks = 0;
    int frameIndex = -1;
    int facing = -1;
    bool hasCurrentPosition = false;
    bool arcing = false;
    bool high = false;
    bool veryHigh = false;
    bool shadow = false;
    bool bright = false;
    bool laser = false;
    bool houseColorLaser = false;
    bool electricBolt = false;
    bool alternateElectricColor = false;
    std::string electricColor;
    int laserThickness = 1;
    bool animates = false;
    int cluster = 0;
    int translucency = 0;
    bool trailerCadenceManaged = false;
    // A prism fragment starts at the same body attachment as its parent hit.
    int sourceAnchorObjectId = -1;
    bool shrapnel = false;
};

struct RuntimeImpactRenderActor {
    int actorId = -1;
    int projectileActorId = -1;
    int attackerObjectId = -1;
    std::string owner;
    int cellX = -1;
    int cellY = -1;
    std::string animId;
    std::string projectileImageId;
    std::string particleId;
    std::string weaponId;
    std::string warheadId;
    std::string lifecycleState;
    std::string renderIntent;
    double x = 0.0;
    bool centerOnAnchor = false;
    bool bodyCenterAnchored = false;
    RuntimeParticleAirbornePresentation bodyCenterPresentation;
    int bodyCenterSpriteOffsetY = 0;
    double y = 0.0;
    double level = 0.0;
    int screenOffsetX = 0;
    int screenOffsetY = 0;
    int nativeFixedX = 0;
    int nativeFixedY = 0;
    int totalTicks = 0;
    int remainingTicks = 0;
    int fadeTicks = 0;
    int frameIndex = -1;
    int facing = -1;
    int damage = 0;
    bool bright = false;
    int targetObjectId = -1;
};

struct RuntimeUiResourceRenderSummary {
    int resourceCandidates = 0;
    int resourcesDecoded = 0;
    int resourcesMissing = 0;
    int paletteCandidates = 0;
    int palettesDecoded = 0;
    int palettesUnsupported = 0;
    int sidebarCandidates = 0;
    int radarCandidates = 0;
    int cameoCandidates = 0;
    int pipCandidates = 0;
    int powerCandidates = 0;
};

struct RuntimeWaterWakeActor {
    int objectInstanceId = -1;
    double x = 0.0;
    double y = 0.0;
    double level = 0.0;
    int spawnTick = 0;
    std::string imageId;
};

struct RuntimeMapRenderCache {
    // Bounded presentation diagnostics only, excluded from authority/save.
    std::uint64_t terrainViewportFillCalls = 0;
    std::uint64_t terrainViewportFillElisions = 0;
    std::string buildingDepthTemplateKey;
    bool buildingDepthTemplateAttempted = false;
    RuntimeShpFrameImage buildingDepthTemplate;
    RuntimeNativeShroudAtlas nativeShroudAtlas;
    std::unordered_map<int, COLORREF> terrainColorsByTileIndex;
    std::string loadedPalettePath;
    bool paletteLoadAttempted = false;
    bool paletteLoaded = false;
    std::vector<RuntimePaletteColor> palette;
    std::string loadedStructurePalettePath;
    bool structurePaletteLoadAttempted = false;
    bool structurePaletteLoaded = false;
    std::vector<RuntimePaletteColor> structurePalette;
    RuntimePaletteCacheEntry animPalette;
    RuntimePaletteCacheEntry unitPalette;
    bool voxelPaletteLookupLoadAttempted = false;
    bool voxelPaletteLookupLoaded = false;
    unsigned int voxelPaletteFirstRemap = 0;
    unsigned int voxelPaletteLastRemap = 0;
    unsigned int voxelPaletteSectionCount = 0;
    unsigned int voxelPaletteNeutralSection = 0;
    std::vector<RuntimePaletteColor> voxelPalette;
    std::vector<unsigned char> voxelPaletteLookup;
    std::string voxelPaletteLookupSourcePath;
    std::string voxelPaletteLookupErrorMessage;
    std::unordered_map<std::string, RuntimePaletteCacheEntry>
        namedPalettesByKey;
    std::unordered_map<std::string, RuntimeTmpTileFile> tmpTilesByPath;
    std::string lastTmpTileLoadError;
    std::string terrainTilePathCacheRoot;
    std::unordered_map<int, std::string> terrainTilePathByIndex;
    std::unordered_set<int> missingTerrainTilePathByIndex;
    std::string terrainVisualFamilyCacheRoot;
    std::unordered_map<int, RuntimeTerrainVisualFamily> terrainVisualFamiliesByTileIndex;
    std::unordered_map<std::string, RuntimeObjectShpSpriteCacheEntry>
        objectShpSpritesByType;
    std::unordered_map<std::string, RuntimeAuxiliaryShpBytesCacheEntry>
        objectShpBytesByImage;
    std::unordered_map<std::string, RuntimeObjectShpSpriteCacheEntry>
        auxiliaryShpSpritesByImage;
    std::unordered_map<std::string, RuntimeAuxiliaryShpBytesCacheEntry>
        auxiliaryShpBytesByImage;
    // Asset roots are stable across paints; probing their parent directories
    // for every explosion/fire SHP frame otherwise repeats filesystem work.
    bool mixExportsRootResolved = false;
    std::string mixExportsRootAssetPath;
    std::string mixExportsRootStructurePath;
    std::string mixExportsRootTilePath;
    std::string resolvedMixExportsRoot;
    std::unordered_map<std::string, RuntimeVoxelSourceCacheEntry>
        voxelSourcesByImage;
    int vehicleBodyPoseCacheEntries = 0;
    std::uint64_t vehicleBodyPoseUseSequence = 0;
    std::uint64_t vehicleBodyPoseCacheEvictions = 0;
    // Updated only by actual frame/raster generation, never by warm lookups.
    // Resource labels contain IDs (at most 96 characters), never file paths.
    std::string lastColdResourceType;
    std::string lastColdResourceName;
    std::uint64_t lastColdResourceSequence = 0;
    std::map<std::uint64_t, RuntimeVehicleBodyPoseCacheReference>
        vehicleBodyPoseUses;
    std::unordered_map<std::string, RuntimeVoxelCacheEntry>
        voxelObjectsByType;
    std::unordered_map<std::string, std::string>
        stableVoxelCacheKeyByLookup;
    std::unordered_map<std::string, std::string>
        structureTurretBarrelImageByKey;
    // While the cold GTGCAN atlas is generated off-thread, rendering may use
    // the nearest ready direction instead of rasterizing on the paint thread.
    bool grandCannonVoxelPrewarmActive = false;
    std::uint32_t grandCannonVoxelReadyMask = 0;
    std::unordered_map<std::string, RuntimeVxlFixedScaleCacheEntry>
        voxelFixedScaleByKey;
    std::unordered_map<std::string, RuntimeSequenceFrameMetadataCacheEntry>
        sequenceFrameMetadataByKey;
    std::unordered_map<std::string, std::string>
        projectilePaletteSourceByKey;
    std::unordered_map<std::string, std::string>
        auxiliaryPaletteSourceByKey;
    std::unordered_map<std::string, int>
        projectileTypeIndexByImage;
    std::unordered_map<std::string, int>
        auxiliaryEffectKindByImage;
    std::string resourceOverlayPlanCacheSignature;
    std::unordered_map<std::string, const RuntimeTmpSubTileImage*>
        resourceOverlayTmpImageByIdentity;
    std::unordered_map<std::string, RuntimeBuildingAnimationLayerCacheEntry>
        buildingAnimationLayersByKey;
    std::string objectTypeRenderCacheSignature;
    std::unordered_map<std::string, RuntimeRenderFootprintCacheEntry>
        structureFootprintsByType;
    std::unordered_map<std::string, bool> objectVoxelArtByType;
    std::unordered_map<std::string, bool> objectAircraftByType;
    std::vector<RuntimeWaterWakeActor> waterWakeActors;
    std::unordered_map<int, int> waterWakeLastSpawnTickByObject;
    int waterWakeLastAnimationTick = -1;
    int waterWakeLastUpdateTick = -1;
    int waterWakeLastUpdateEligibleObjects = 0;
    int waterWakeLastUpdateEmissions = 0;
    int waterWakeLastUpdateSuppressedNotMoving = 0;
    int waterWakeLastUpdateSuppressedNotWater = 0;
    int waterWakeLastUpdateSuppressedSubmerged = 0;
    std::string waterWakeConfigurationSignature;
    const GameWorld* terrainLookupWorld = nullptr;
    const TerrainCell* terrainLookupData = nullptr;
    std::size_t terrainLookupCount = 0;
    std::unordered_map<std::uint64_t, const TerrainCell*> terrainByCell;
    std::vector<const TerrainCell*> terrainByCellIndex;
    std::string mixArchiveRootPath;
    bool mixArchivePathsScanned = false;
    std::vector<std::string> mixArchivePaths;
    std::unordered_map<std::string, RuntimeMixArchiveCacheEntry>
        mixArchivesByPath;
    std::string looseResourceIndexRootPath;
    bool looseResourceIndexBuilt = false;
    std::unordered_map<std::string, std::string> looseResourcePathsByName;
    RuntimeTerrainLayerCache terrainLayer;
    RuntimeMapLightTerrainCache mapLightTerrain;
    RuntimeVisionOverlayCache visionOverlay;
    int terrainLayerCacheHits = 0;
    int terrainLayerCacheMisses = 0;
    const MapFacts* mapFactsLookupSource = nullptr;
    const MapCellFacts* mapFactsLookupData = nullptr;
    std::uint64_t mapFactsLookupContentHash = 0;
    std::size_t mapFactsLookupCellCount = 0;
    std::size_t mapFactsLookupIndexSize = 0;
    std::vector<const MapCellFacts*> mapFactsByCellIndex;
    int mapFactLookupCacheRebuilds = 0;
    // Cumulative, inexpensive paint-batch geometry reuse diagnostics.
    std::uint64_t geometryMobileBoundsMemoHits = 0;
    std::uint64_t geometryMobileBoundsMemoMisses = 0;
    std::uint64_t geometryMobileBoundsPaintRecords = 0;
    std::uint64_t geometryStructureMemoHits = 0;
    std::uint64_t geometryStructureMemoMisses = 0;
    std::uint64_t geometryBuildingDepthMemoHits = 0;
    std::uint64_t geometryBuildingDepthMemoMisses = 0;
    std::uint64_t geometryBuildingDepthPaintRecords = 0;
    std::uint64_t lastBuildingDepthRequestedPixels = 0;
    std::uint64_t lastBuildingDepthBufferPixels = 0;
    std::uint64_t lastBuildingDepthBufferCapacityBytes = 0;
    bool lastBuildingDepthTimingEnabled = false;
    double lastBuildingDepthTemplateMs = 0.0;
    double lastBuildingDepthInitializeMs = 0.0;
    double lastBuildingDepthGatherMs = 0.0;
    double lastBuildingDepthBodyCollectMs = 0.0;
    double lastBuildingDepthMobileCollectMs = 0.0;
    double lastBuildingDepthSortMs = 0.0;
    double lastBuildingDepthRasterMs = 0.0;
    double lastBuildingDepthBlendMs = 0.0;
    double lastBuildingDepthTotalMs = 0.0;
    std::uint64_t geometryTerrainTileMemoHits = 0;
    std::uint64_t geometryTerrainTileMemoMisses = 0;
    int projectileShpAttempts = 0;
    int projectileShpDrawn = 0;
    int projectileShpSkippedInvisible = 0;
    int projectileShpMissing = 0;
    int projectilePaletteHits = 0;
    int projectilePaletteFallbacks = 0;
    int projectilePaletteStructureFallbacks = 0;
    int projectileTravelDrawn = 0;
    int trailerAnimAttempts = 0;
    int trailerAnimDrawn = 0;
    int trailerAnimMissing = 0;
    int impactAnimAttempts = 0;
    int impactAnimDrawn = 0;
    int impactAnimMissing = 0;
    int weaponAnimAttempts = 0;
    int weaponAnimDrawn = 0;
    int weaponAnimMissing = 0;
    int paletteAnimPaletteHits = 0;
    int paletteFirersPaletteHits = 0;
    int paletteExplicitHits = 0;
    int paletteArtHits = 0;
    int paletteColorHits = 0;
    int paletteUnitFallbacks = 0;
    int paletteIsoFallbacks = 0;
    int paletteUnknownHits = 0;
    std::unordered_map<std::string, int> paletteSourceHitsByName;
    int sequenceFrameCacheHits = 0;
    int sequenceFrameCacheMisses = 0;
    int projectilePaletteCacheHits = 0;
    int projectilePaletteCacheMisses = 0;
    int auxiliaryPaletteCacheHits = 0;
    int auxiliaryPaletteCacheMisses = 0;
    int auxiliaryShpBytesCacheHits = 0;
    int auxiliaryShpBytesCacheMisses = 0;
    int auxiliaryShpFrameCacheHits = 0;
    int auxiliaryShpFrameCacheMisses = 0;
    int voxelCacheHits = 0;
    int voxelCacheMisses = 0;
    int buildingAnimationLayerCacheHits = 0;
    int buildingAnimationLayerCacheMisses = 0;
    int projectileTypeCacheHits = 0;
    int projectileTypeCacheMisses = 0;
    int auxiliaryEffectKindCacheHits = 0;
    int auxiliaryEffectKindCacheMisses = 0;
    double projectileEffectMs = 0.0;
    double auxiliaryShpBytesCacheMissMs = 0.0;
    double auxiliaryShpFrameCacheMissMs = 0.0;
    std::string lastProjectileImageId;
    std::string lastProjectilePaletteSource;
    std::string lastAuxiliaryEffectKind;
    std::vector<std::uint32_t> screenShakePixels;
    std::unordered_map<int, RuntimeProjectileLineTrail> projectileLineTrails;
    int projectileLineTrailTick = -1;
    std::unordered_map<std::string, std::vector<std::array<double, 3>>> voxelMuzzleEndpointsByKey;
};

struct RuntimeMapRenderContext {
    RuntimeMapRenderContext();
    ~RuntimeMapRenderContext();

    const TheaterDatabase* theater = nullptr;
    const ArtDatabase* art = nullptr;
    const RulesDatabase* rules = nullptr;
    const MapFacts* mapFacts = nullptr;
    const std::vector<RuntimeProjectileRenderActor>* projectileActors = nullptr;
    const std::vector<RuntimeImpactRenderActor>* impactActors = nullptr;
    const std::vector<RuntimeWreckState>* wrecks = nullptr;
    const std::vector<RuntimeBuildingCollapseState>* buildingCollapses = nullptr;
    const std::vector<RuntimeParticleRenderSnapshot>* particleSnapshots = nullptr;
    const std::vector<RuntimeParticleBeamSnapshot>* particleBeamSnapshots = nullptr;
    const std::vector<RuntimeLocalLightSnapshot>* localLightSnapshots = nullptr;
    const std::vector<SpecialMechanismRenderSnapshot>*
        specialMechanismSnapshots = nullptr;
    // Presentation-only selection controls the persistent mind-control arc.
    const std::vector<int>* selectedObjectIds = nullptr;
    const std::unordered_map<int, int>* playerCheerUntilTick = nullptr;
    const std::vector<RuntimeGateRenderSnapshot>* gateSnapshots = nullptr;
    const RuntimeUiResourceRenderSummary* uiResources = nullptr;
    const std::unordered_map<std::string, COLORREF>* ownerRemapColors = nullptr;
    const std::unordered_set<std::string>* neutralRemapOwners = nullptr;
    const RuntimeVisionSystem* vision = nullptr;
    // Explicit value-only world lighting.  A null pointer or identity tint
    // preserves the pre-lighting renderer output exactly.
    const RuntimeLightingTint* lightingTint = nullptr;
    const MapLightingProfile* lightingProfile = nullptr;
    std::string visionObserverOwner;
    std::string tileRootPath;
    std::string palettePath;
    std::string assetRootPath;
    std::string structureRootPath;
    std::string structurePalettePath;
    int animationTick = 0;
    int fixedTickHz = 30;
    // 0=low, 1=medium, 2=high. Art entries whose authored DetailLevel is
    // above this value are presentation-only and are skipped.
    int visualDetail = 2;
    bool seeHiddenObjects = true;
    bool useTileTextures = true;
    bool detailStatsEnabled = false;
    // Eight coarse drawing stages only; does not enable per-object timing,
    // diagnostics, counters, or slower reference drawing paths.
    bool coarseStageTimingEnabled = false;
    // Opt in only when this paint expects a building hit query. A standalone
    // paint or a cached/no-interaction overlay does no extra hit-span scan.
    bool captureBuildingDepthHitSpans = false;
    bool stableRenderConfiguration = false;
    bool effectOnlyProbe = false;
    bool chaosBodyTint = false;
    // Per-object RGB565 ColorAdd applied after palette/remap, before opacity.
    std::uint16_t airstrikeBodyColorAdd = 0;
    bool shadowsEnabled = true;
    std::uint32_t* surfacePixels = nullptr;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    // Only the own negative-height 32-bit DIB creator may supply this proof.
    // Foreign and diagnostic surfaces retain the original viewport fill.
    bool surfaceTopDownOwned = false;
};

// Share immutable asset/type and exact body geometry during one paint batch.
// World, context, camera and source snapshots must remain unchanged until exit;
// the render cache may load frames, but must not be reset inside the batch.
// Resource cache nodes may be inserted, but loaded TMP nodes must not be erased
// or rewritten while a batch borrows their pointers. Missing TMP resolutions
// also remain immutable until the next batch.
// Compatible nested scopes share the outer batch's value-owned geometry pool.
// Only original world objects are memoized; projected temporary copies replay
// their complete current pose. No geometry survives the outer scope's exit.
// Opt-in viewport-only structure geometry uses the compositor's body culling;
// ordinary scopes and standalone geometry queries retain complete sprite bounds.
class RuntimeGeometryQueryScope {
public:
    RuntimeGeometryQueryScope(const GameWorld& world, const RECT& viewport,
        const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
        RuntimeMapRenderCache& cache, bool viewportStructureGeometryOnly = false);
    ~RuntimeGeometryQueryScope();
    RuntimeGeometryQueryScope(const RuntimeGeometryQueryScope&) = delete;
    RuntimeGeometryQueryScope& operator=(const RuntimeGeometryQueryScope&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Read-only presentation pose at the same continuous ground position used by
// drawing, including a stopped vehicle. No simulation/save state is mutated.
RuntimeVehicleBodyPose resolveRuntimeVehicleBodyPose(
    const GameWorld& world, const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache);

bool runRuntimeVehicleTerrainSlopeRenderContract(
    const RuntimeMapRenderContext& context, std::string& error);

// Resolves presentation-only alternate vehicle classes such as refinery
// unloading voxels and the deployed Siege Chopper. Gameplay keeps the
// original WorldObjectInstance::type throughout the visual lifecycle.
std::string resolveRuntimeObjectVisualTypeId(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context
);

struct RuntimeMapRenderStats {
    int mapLightSources = 0;
    int mapLightReceivers = 0;
    int buildingCollapseSprites = 0;
    int buildingCollapseMissing = 0;
    int wreckBodySprites = 0;
    int sinkingBodySprites = 0;
    int behindMarkerSprites = 0;
    int behindMarkerMissing = 0;
    int behindMarkerFrame = -1;
    int parachuteSprites = 0;
    int parachuteMissing = 0;
    int tankBunkerFrontLayers = 0;
    int tankBunkerFrontFrame = -1;
    bool rendered = false;
    bool lightingTintApplied = false;
    int lightingTintedPixels = 0;
    int visibleTerrainCells = 0;
    int texturedTerrainCells = 0;
    int fallbackTerrainCells = 0;
    int authoredEmptyTerrainSubTiles = 0;
    int visibleSmudges = 0;
    int smudgeSprites = 0;
    int smudgeMissing = 0;
    int visibleOverlayCells = 0;
    int resourceOverlayCells = 0;
    int resourceOverlayOreCells = 0;
    int resourceOverlayGemCells = 0;
    int resourceOverlaySprites = 0;
    int resourceOverlayFallbacks = 0;
    int resourceOverlayMissing = 0;
    int resourceOverlayOreMissing = 0;
    int resourceOverlayGemMissing = 0;
    int resourceOverlayFallbackProxy = 0;
    int resourceOverlayBridgeRejected = 0;
    int resourceOverlayCandidateRejected = 0;
    int resourceOverlayRuntimeOverridesMapFact = 0;
    int resourceOverlayRuntimeClearsMapFact = 0;
    int resourceOverlayRuntimeIdentityOverridesMapFact = 0;
    int rockOverlaySpritesCentered = 0;
    int lastResourceOverlayFrame = -1;
    std::string lastResourceOverlayCandidates;
    std::string lastResourceOverlayFailure;
    int bridgeOverlayCells = 0;
    int bridgeOverlaySprites = 0;
    int bridgeOverlayTerrainBaked = 0;
    int bridgeOverlayDeferred = 0;
    int bridgeOverlayDeferredDrawn = 0;
    int bridgeOverlayMissing = 0;
    int bridgeOverlayFallbackFrameUsed = 0;
    int bridgeOverlayLevelMismatch = 0;
    int bridgeOverlayHighCells = 0;
    int bridgeOverlayLowCells = 0;
    int bridgeOverlayRuntimeFrameOverridesMapFact = 0;
    int bridgeOverlayRuntimeFrameMismatches = 0;
    int bridgeOverlayHighAnchorAdjusted = 0;
    int bridgeOverlayHighTerminalUnderlays = 0;
    int visibleTerrainObjects = 0;
    int terrainObjectSprites = 0;
    int terrainObjectAnimatedSprites = 0;
    int terrainObjectAnimationFrames = 0;
    int preCullWorldObjects = 0;
    int culledWorldObjects = 0;
    int sortedWorldObjects = 0;
    int visibleWorldObjects = 0;
    int visionHiddenWorldObjects = 0;
    int visionHiddenTerrainObjects = 0;
    int visionHiddenProjectileActors = 0;
    int visionHiddenImpactActors = 0;
    int visionVisibleCells = 0;
    int visionFoggedCells = 0;
    int visionShroudedCells = 0;
    int visionOverlayPixels = 0;
    int visionOverlayCacheHits = 0;
    int visionOverlayCacheMisses = 0;
    int visionGeometryCacheHits = 0;
    int visionGeometryCacheMisses = 0;
    std::size_t visionOpaqueRunPixels = 0;
    std::size_t visionOpaqueRunCacheBytes = 0;
    int airborneForegroundBodiesDrawn = 0;
    int dockedAircraftForegroundObjects = 0;
    int repairDepotForegroundObjects = 0;
    int repairDepotAnimationLayers = 0;
    int repairDepotAnimationForegroundLayers = 0;
    int repairDepotAnimationFrame = -1;
    int repairDepotAnimationPhase = 0;
    std::string repairDepotAnimationImage;
    int aircraftBuildingClearanceAdjustments = 0;
    int aircraftMaxBuildingClearanceLiftPixels = 0;
    int shadowCandidates = 0;
    int shadowsDrawn = 0;
    int shadowPixelsDarkened = 0;
    int worldObjectShadowOrderTraceCount = 0;
    std::uint64_t worldObjectShadowOrderTraceHash =
        1469598103934665603ULL;
    int shpShadowsDrawn = 0;
    int voxelShadowsDrawn = 0;
    int shpShadowMasksResolved = 0;
    int voxelShadowMasksResolved = 0;
    int generatedShadowFallbacks = 0;
    int structureShadowsDrawn = 0;
    int unitShadowsDrawn = 0;
    int infantryShadowsDrawn = 0;
    int aircraftShadowsDrawn = 0;
    int buildingAnimationShadowsDrawn = 0;
    int projectileShadowCandidates = 0;
    int projectileShadowsDrawn = 0;
    int projectileShadowPixelsDarkened = 0;
    int waterWakeEligibleObjects = 0;
    int waterWakeEmissions = 0;
    int waterWakeActiveActors = 0;
    int waterWakeSpritesDrawn = 0;
    int waterWakeSpritesCulled = 0;
    int waterWakeSpritesMissing = 0;
    int waterWakeSuppressedNotMoving = 0;
    int waterWakeSuppressedNotWater = 0;
    int waterWakeSuppressedSubmerged = 0;
    int waterWakeFrameCount = 0;
    int waterWakeFrameDelayTicks = 0;
    int lastWaterWakeFrame = -1;
    std::string lastWaterWakeImageId;
    std::string lastWaterWakeSource;
    std::string lastWaterWakePaletteSource;
    int shadowsSuppressed = 0;
    int shadowsMissing = 0;
    int voxelShadowIndexApplied = 0;
    int voxelShadowIndexFallbacks = 0;
    int visibleStructureSprites = 0;
    int missingStructureSprites = 0;
    int gateSpecialZOverlaySprites = 0;
    int gateSpecialZOverlayMissing = 0;
    int structureWallOverlaySprites = 0;
    int structureWallOverlayFallbacks = 0;
    std::uint32_t structureWallConnectionMasksSeen = 0;
    std::uint64_t structureWallOverlayFramesSeen = 0;
    int lastStructureWallConnectionMask = -1;
    int lastStructureWallOverlayFrame = -1;
    int visibleObjectShpSprites = 0;
    int missingObjectShpSprites = 0;
    int unloadingClassObjectSprites = 0;
    int unloadingClassForegroundObjects = 0;
    int unloadingClassSpecialForegroundLayers = 0;
    int unloadingClassTurretSprites = 0;
    int unloadingClassTurretSuppressedObjects = 0;
    std::string unloadingClassDiagnostic;
    int visibleVoxelProxies = 0;
    int missingVoxelProxies = 0;
    int visibleVoxelBodies = 0;
    int visibleVoxelTurrets = 0;
    int visibleVoxelBarrels = 0;
    int visibleVoxelBarrelCues = 0;
    int missingVoxelBarrels = 0;
    int missingVoxelTurrets = 0;
    int voxelTurretFacingFromTurret = 0;
    int voxelTurretFacingFromBody = 0;
    int voxelBodyMaxWidth = 0;
    int voxelBodyMaxHeight = 0;
    int voxelTurretMaxWidth = 0;
    int voxelTurretMaxHeight = 0;
    int voxelBarrelMaxWidth = 0;
    int voxelBarrelMaxHeight = 0;
    int structureTurretMaxWidth = 0;
    int structureTurretMaxHeight = 0;
    int firstStructureTurretFacing = -1;
    int firstStructureTurretScreenOffsetX = 0;
    int firstStructureTurretScreenOffsetY = 0;
    double firstStructureTurretFitScale = 0.0;
    std::string firstStructureTurretType;
    std::string firstStructureTurretImage;
    int animatedShpSprites = 0;
    int sequenceFrameHits = 0;
    int sequenceFrameFallbacks = 0;
    int sequenceReadyHits = 0;
    int sequenceGuardHits = 0;
    int sequenceWalkHits = 0;
    int sequenceShovelHits = 0;
    int sequenceFireHits = 0;
    int sequenceIdleHits = 0;
    int sequenceIdle1Hits = 0;
    int sequenceIdle2Hits = 0;
    int sequenceDeployHits = 0;
    int sequenceDeployedHits = 0;
    int sequenceDeployedFireHits = 0;
    int sequenceWalkAfterStop = 0;
    int buildingArtAnimationLayers = 0;
    int buildingSpecialAnimationLayers = 0;
    int buildingArtAnimationMissing = 0;
    int buildingPoweredAnimationLayers = 0;
    int buildingLowPowerAnimationLayers = 0;
    int buildingNeutralAnimationSuppressed = 0;
    int buildingOfflineHiddenAnimationLayers = 0;
    int buildingOfflinePausedVisibleAnimationLayers = 0;
    int buildingOfflineContinuingAnimationLayers = 0;
    int buildingWorkAnimationLayers = 0;
    int firstBuildingWorkAnimationFrame = -1;
    int buildingFactoryVehicleDoorAnimationLayers = 0;
    int buildingFactoryAirRoofAnimationLayers = 0;
    int buildingShipyardProductionAnimationLayers = 0;
    int buildingShipyardIdleAnimationLayers = 0;
    int shipyardProductionAnimationFrame = -1;
    std::string shipyardProductionAnimationImage;
    int firstBuildingArtAnimationFrame = -1;
    int firstBuildingOfflinePausedVisibleFrame = -1;
    int firstBuildingOfflineContinuingFrame = -1;
    int buildingDamageFireOffsets = 0;
    int buildingDamageFireDrawn = 0;
    int buildingDamageFireCulled = 0;
    int buildingDamageFireMissing = 0;
    int firstBuildingDamageFireFrame = -1;
    int structureTurretAnimAttempts = 0;
    int structureTurretAnimDrawn = 0;
    int structureTurretAnimMissing = 0;
    int structureTurretAnimVoxel = 0;
    int structureTurretAnimShp = 0;
    int structureTurretCuboidRasters = 0;
    int structureTurretPointRasters = 0;
    int structureRearAnchorObjects = 0;
    int structureMultiCellAnchorObjects = 0;
    int structureBaseGlobalRectObjects = 0;
    int structureActiveGlobalRectObjects = 0;
    int structureCullingRectDiagnostics = 0;
    std::string structureRectDiagnostic;
    std::string buildingSpecialAnimationImage;
    std::string buildingSpecialAnimationResolved;
    std::string buildingSpecialAnimationSource;
    std::string firstBuildingArtAnimationImage;
    std::string firstBuildingDamageFireImage;
    std::string firstBuildingOfflinePausedVisibleImage;
    std::string firstBuildingOfflineContinuingImage;
    std::string firstBuildingWorkAnimationImage;
    std::string structureTurretDiagnostic;
    std::string missingVoxelBarrelDiagnostic;
    std::string voxelSpriteDiagnostic;
    int ownerRemapObjects = 0;
    int playerRemapObjects = 0;
    int ownerRemapDeferredObjects = 0;
    int neutralRemapObjects = 0;
    int unexpectedBlueRemapObjects = 0;
    int uiResourceDecoded = 0;
    int uiResourceUnsupported = 0;
    int uiResourceCandidates = 0;
    int uiResourceMissing = 0;
    int uiPaletteCandidates = 0;
    int uiPaletteDecoded = 0;
    int uiPaletteUnsupported = 0;
    int uiSidebarResourceCandidates = 0;
    int uiRadarResourceCandidates = 0;
    int uiCameoResourceCandidates = 0;
    int uiPipResourceCandidates = 0;
    int uiPowerResourceCandidates = 0;
    int uiFormat0Unsupported = 0;
    bool detailStatsEnabled = false;
    bool coarseStageTimingEnabled = false;
    int sequenceFrameCacheHits = 0;
    int sequenceFrameCacheMisses = 0;
    int projectilePaletteCacheHits = 0;
    int projectilePaletteCacheMisses = 0;
    int auxiliaryPaletteCacheHits = 0;
    int auxiliaryPaletteCacheMisses = 0;
    int auxiliaryShpBytesCacheHits = 0;
    int auxiliaryShpBytesCacheMisses = 0;
    int auxiliaryShpFrameCacheHits = 0;
    int auxiliaryShpFrameCacheMisses = 0;
    int voxelCacheHits = 0;
    int voxelCacheMisses = 0;
    int buildingAnimationLayerCacheHits = 0;
    int buildingAnimationLayerCacheMisses = 0;
    int projectileTypeCacheHits = 0;
    int projectileTypeCacheMisses = 0;
    int auxiliaryEffectKindCacheHits = 0;
    int auxiliaryEffectKindCacheMisses = 0;
    int projectileShpAttempts = 0;
    int projectileShpDrawn = 0;
    int projectileShpSkippedInvisible = 0;
    int projectileShpMissing = 0;
    int projectilePaletteHits = 0;
    int projectilePaletteFallbacks = 0;
    int projectilePaletteStructureFallbacks = 0;
    int projectileActorSnapshots = 0;
    int missileExhaustDrawn = 0;
    int missileExhaustHeadDrawn = 0;
    int squidGrappleDrawn = 0;
    int squidGrappleMissing = 0;
    int squidSplashDrawn = 0;
    int squidSplashMissing = 0;
    int missileExhaustMissing = 0;
    int projectileLineTrailSegments = 0;
    int projectileLineTrailPixels = 0;
    int projectileDirectionalSprites = 0;
    int projectileActorTravelDrawn = 0;
    int sonicWavesDrawn = 0;
    int sonicWavePixels = 0;
    int projectileActorVoxelDrawn = 0;
    int projectileActorImageResolved = 0;
    int projectileActorTravelMissing = 0;
    int projectileActorPaletteMissing = 0;
    int projectileActorDecodeMissing = 0;
    int projectileActorExpired = 0;
    int projectileActorCulled = 0;
    int projectileActorInvisibleSkipped = 0;
    int impactActorSnapshots = 0;
    int impactActorDrawn = 0;
    int impactActorHideIfNoOreSuppressed = 0;
    int particleActorSnapshots = 0;
    int particleActorOmitted = 0;
    int particleActorShpAttempts = 0;
    int particleActorShpDrawn = 0;
    int particleActorFallbackDrawn = 0;
    int particleActorMissing = 0;
    int particleActorCulled = 0;
    int visionHiddenParticleActors = 0;
    int particleBeamSnapshots = 0;
    int particleBeamsDrawn = 0;
    int localLightSnapshots = 0;
    int localLightsApplied = 0;
    int localLightPixels = 0;
    int radiationTintCells = 0;
    int radiationTintPixels = 0;
    int lastParticleFrame = -1;
    std::string lastParticleImage;
    std::uint64_t particleSnapshotSignature = 0;
    int projectileRenderIntentActors = 0;
    int impactRenderIntentActors = 0;
    int particleRenderIntentActors = 0;
    int specialMechanismRenderActors = 0;
    int renderIntentMissing = 0;
    std::uint64_t renderIntentHash = 0;
    int projectileTravelDrawn = 0;
    int trailerAnimAttempts = 0;
    int trailerAnimDrawn = 0;
    int trailerAnimMissing = 0;
    int impactAnimAttempts = 0;
    int impactAnimDrawn = 0;
    int impactAnimMissing = 0;
    int weaponAnimAttempts = 0;
    int weaponAnimDrawn = 0;
    int weaponAnimMissing = 0;
    int paletteAnimPaletteHits = 0;
    int paletteFirersPaletteHits = 0;
    int paletteExplicitHits = 0;
    int paletteArtHits = 0;
    int paletteColorHits = 0;
    int paletteUnitFallbacks = 0;
    int paletteIsoFallbacks = 0;
    int paletteUnknownHits = 0;
    int mapFactsAvailable = 0;
    int mapFactOverlayCells = 0;
    int mapFactResourceCells = 0;
    int mapFactBridgeCells = 0;
    int mapFactFallbackCells = 0;
    int mapFactHits = 0;
    int mapFactMisses = 0;
    int terrainLayerCacheHits = 0;
    int terrainLayerCacheMisses = 0;
    int mapFactLookupCacheRebuilds = 0;
    double terrainMs = 0.0;
    double terrainBlitMs = 0.0;
    double overlayMs = 0.0;
    double terrainObjectsMs = 0.0;
    double worldObjectsMs = 0.0;
    double animationMs = 0.0;
    double worldObjectSortMs = 0.0;
    double structureSpriteMs = 0.0;
    double unitShpMs = 0.0;
    double voxelSpriteMs = 0.0;
    double objectOverlayMs = 0.0;
    double lightingMs = 0.0;
    double localLightMs = 0.0;
    double visionMs = 0.0;
    double projectileEffectMs = 0.0;
    double auxiliaryShpBytesCacheMissMs = 0.0;
    double auxiliaryShpFrameCacheMissMs = 0.0;
};

void initializeRuntimeMapCamera(
    RuntimeMapCamera& camera,
    const GameWorld& world,
    int viewportWidth,
    int viewportHeight
);

void scrollRuntimeMapCamera(RuntimeMapCamera& camera, int deltaX, int deltaY);

// Synchronously resolves the visible terrain/bridge surface, independently of
// WM_PAINT. Only presentation caches are updated; no world state is mutated.
bool pickRuntimeMapSurfaceCell(
    const GameWorld& world,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    const RECT& viewport,
    POINT screenPoint,
    RuntimeMapRenderCache& cache,
    POINT& cell
);

bool runtimeObjectUsesAircraftSpriteForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context
);

int runtimeAircraftSpriteLiftPixels(
    const WorldObjectInstance& object,
    bool aircraft
);

POINT runtimeObjectBodyAnchorForRender(
    const WorldObjectInstance& object,
    const RECT& viewport,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera
);

bool runtimeGrandCannonBarrelDrawsInFront(int facing);

bool runRuntimeSonicWaveRenderContractCheck(std::string& errorMessage);

bool runRuntimeMovementRenderContractCheck(std::string& errorMessage);

bool runRuntimeOwnerRemapPolicyContractCheck(
    std::string& errorMessage,
    const RuntimeMapRenderContext* assetContext = nullptr
);

bool runRuntimeBuildingDebrisPaletteContractCheck(
    const RuntimeMapRenderContext& context,
    std::string& errorMessage
);

bool runRuntimeArcticArtRenderContractCheck(
    const RuntimeMapRenderContext& temperate, const RuntimeMapRenderContext& snow,
    const std::string& output, int& liveFrames, int& deathFrames, std::string& error);

bool runRuntimeOpenToppedAnimRenderCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& hosts, const std::vector<RuntimeImpactRenderActor>& samples,
    const std::string& output, int& checks, std::string& error);

bool runRuntimeOccupantAnimRenderCheck(const RuntimeMapRenderContext& context,
    const WorldObjectInstance& host, const std::vector<RuntimeImpactRenderActor>& samples,
    const std::string& output, int& checks, std::string& error);

bool runRuntimeInfantryDeathRenderContractCheck(
    const RuntimeMapRenderContext& context,
    std::string& errorMessage
);

bool runRuntimeMindControlRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeChronoPaletteContractCheck(const RuntimeMapRenderContext& context, std::string& error);

bool runRuntimeYuriWallRenderContractCheck(
    const RuntimeMapRenderContext& context,
    const std::string& outputDirectory,
    std::string& errorMessage
);

bool runRuntimeChronoRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeTemporalRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeProjectileOriginRenderContractCheck(
    const RuntimeMapRenderContext& context,
    const GameWorld& world,
    const std::vector<RuntimeProjectileRenderActor>& actors,
    std::string& errorMessage
);

bool runRuntimeGroundImpactRenderContractCheck(
    const RuntimeMapRenderContext& context,
    const std::vector<RuntimeProjectileRenderActor>& shots,
    const std::vector<RuntimeImpactRenderActor>& impacts,
    std::string& errorMessage
);

// Project the simulation's current position exactly once, including its arc.
POINT runtimeProjectileActorScreenPoint(const RuntimeProjectileRenderActor& actor,
    const RECT& viewport, const RuntimeMapCamera& camera);

bool resolveRuntimeInfantryWeaponMuzzlePoint(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, POINT& result);
bool resolveRuntimeMarineWeaponMuzzlePoint(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, POINT& result);

bool resolveRuntimeStructureWeaponMuzzlePoint(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, bool secondary, POINT& result);

bool resolveRuntimeVoxelWeaponMuzzlePoint(const WorldObjectInstance& object,
    const POINT& bodyCenter, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, POINT& point, bool useBarrelEndpoint = true);

bool runRuntimeAllWeaponMuzzleRenderContract(
    const RuntimeMapRenderContext&, const GameWorld&,
    const std::vector<RuntimeProjectileRenderActor>&,
    const std::vector<std::string>&, const std::vector<std::string>&,
    const std::string&, std::string&);

bool runRuntimeTankArcRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<RuntimeProjectileRenderActor>& shots, std::string& errorMessage);

bool runRuntimeIfvTrajectoryRenderCheck(const RuntimeMapRenderContext& context,
    const std::vector<std::vector<RuntimeProjectileRenderActor>>& phases,
    const std::string& outputPath, std::string& errorMessage);

bool runRuntimeInitiateFlameRenderContractCheck(
    const RuntimeMapRenderContext& context, const GameWorld& world,
    const std::vector<RuntimeImpactRenderActor>& phases,
    const std::string& previewPath, std::string& errorMessage
);

bool runRuntimeGattlingRenderContractCheck(
    const RuntimeMapRenderContext& context, const WorldObjectInstance& source,
    const std::vector<RuntimeImpactRenderActor>& muzzles, std::string& errorMessage
);

bool runRuntimeBruteDeathRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeIfvEffectsRenderContractCheck(
    const RuntimeMapRenderContext& context, const GameWorld& world,
    const std::vector<std::vector<RuntimeProjectileRenderActor>>& phases,
    const RuntimeImpactRenderActor& legacySmoke, std::string& error);

bool runRuntimeUnitIdleAnimationContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeInfantrySwimRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeSlaveMinerRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeFloatingDiskRenderContractCheck(
    const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& poses,
    const std::vector<RuntimeProjectileRenderActor>& actors, std::string& errorMessage
);

bool runRuntimeDiskDrainRenderContractCheck(
    const RuntimeMapRenderContext& context, const GameWorld& world,
    const RuntimeImpactRenderActor& initial, const std::string& path, std::string& error
);

bool runRuntimeAirDamageSmokeRenderContractCheck(
    const RuntimeMapRenderContext&, const std::vector<WorldObjectInstance>&,
    const std::vector<RuntimeParticlePoint>&,
    const std::vector<std::vector<RuntimeParticleRenderSnapshot>>&,
    const std::vector<std::string>&, std::string&);

// Capture camera-independent emission geometry before a puff is born. The
// resulting value does not retain either the aircraft or its overlapping roofs.
bool captureRuntimeAircraftAirbornePresentation(
    const GameWorld& world, const WorldObjectInstance& aircraft,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    RuntimeParticleAirbornePresentation& presentation, std::string* error = nullptr
);
// One synchronous source update owns this batch. The world and its objects
// must remain unchanged until destruction; no index is retained across ticks.
class RuntimeAircraftAirbornePresentationBatch {
public:
    struct Statistics {
        std::size_t worldObjectsScanned = 0;
        std::size_t structuresIndexed = 0;
        std::size_t queries = 0;
        std::size_t candidatesVisited = 0;
    };
    RuntimeAircraftAirbornePresentationBatch(const GameWorld& world,
        const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache);
    ~RuntimeAircraftAirbornePresentationBatch();
    RuntimeAircraftAirbornePresentationBatch(const RuntimeAircraftAirbornePresentationBatch&) = delete;
    RuntimeAircraftAirbornePresentationBatch& operator=(const RuntimeAircraftAirbornePresentationBatch&) = delete;
    bool capture(const WorldObjectInstance& aircraft,
        RuntimeParticleAirbornePresentation& presentation, std::string* error = nullptr);
    Statistics statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Borrowed only until the current scope exits. A paint's interaction context can
// copy this immutable snapshot, preserving the complete visual dependencies.
const RuntimeMapRenderContext* runtimeGeometryBatchRenderContext();
int runtimeAircraftAirbornePresentationLiftPixels(
    const RuntimeParticleAirbornePresentation& presentation,
    const RuntimeMapCamera& camera, int screenOffsetY = -34
);

bool runRuntimeVehicleDamageSmokeRenderContractCheck(
    const RuntimeMapRenderContext& context, const std::vector<WorldObjectInstance>& vehicles,
    const std::vector<std::vector<RuntimeParticleRenderSnapshot>>& smoke, std::string& errorMessage
);

bool runRuntimeSpecialDamageSmokeRenderContractCheck(
    const GameWorld& world, const RuntimeMapRenderContext& context,
    int sourceObjectId, const RuntimeParticlePoint& origin,
    const std::vector<RuntimeParticleRenderSnapshot>& smoke,
    const std::string& label, std::string& error
);

bool runRuntimeDesolatorRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeNativeShroudContractCheck(const RuntimeMapRenderContext& assets, std::string& error);
bool runRuntimeNativeShroudMovementContractCheck(const RuntimeMapRenderContext& assets, std::string& error);
bool runRuntimeShroudPerformanceContractCheck(const RuntimeMapRenderContext& assets, std::string& error);
bool runRuntimeVisionRenderContractCheck(std::string& errorMessage);
bool runRuntimeVisionIncrementalContractCheck(std::string& errorMessage);

bool runRuntimeShadowRenderContractCheck(std::string& errorMessage);

// Contract-only complete production map comparison; no live resource cache is changed.
bool runRuntimeGroundVehicleRepairRecordsWorldContractCheck(const GameWorld& world,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera, std::string& error);

bool runRuntimeVoxelFacePreparationAssetContractCheck(
    const RuntimeMapRenderContext& context, std::string& errorMessage
);

bool runRuntimeTheaterAuxiliaryShpAssetContractCheck(
    const RuntimeMapRenderContext& context,
    std::string& errorMessage
);

bool runRuntimeLightingRenderContractCheck(std::string& errorMessage);

bool runRuntimeParticleRenderContractCheck(std::string& errorMessage);

bool runRuntimeChargeTurretRenderContractCheck(std::string& errorMessage);

// Exposes the exact production render-stage selection to deterministic
// Golden capture tooling.  This is presentation-only state and does not
// mutate the object or simulation.
int runtimeChargeTurretStageForGoldenCapture(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context
);

bool runRuntimeElectricTargetRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
);

bool runRuntimeShadowAssetRenderContractCheck(
    const GameWorld& world,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderStats& stats,
    std::string& errorMessage
);

bool runRuntimeInfantryContactShadowContractCheck(
    const GameWorld& world, const RuntimeMapRenderContext& context,
    std::string& errorMessage
);

bool runRuntimeWaterWakeAssetRenderContractCheck(
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderStats& stats,
    std::string& errorMessage
);

bool advanceRuntimeWaterWakeState(
    const GameWorld& world,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache
);

void resetRuntimeWaterWakeState(RuntimeMapRenderCache& cache);

POINT runtimeStructureTurretAnimScreenOffsetForRender(
    const BuildingType& buildingType,
    const std::string& structureType,
    int footprintWidth,
    int footprintHeight,
    const RuntimeMapCamera& camera
);

POINT runtimeStructureBarrelRecoilScreenOffsetForRender(
    const WorldObjectInstance& object,
    const BuildingType& buildingType,
    int animationTick
);

bool prewarmRuntimeStructureTurretVoxelFacingForRender(
    const std::string& structureType,
    int facingBucket,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    int& warmedSpriteCount,
    std::string& errorMessage
);

// Presentation-only selection; canonical metadata/radar/navigation are unchanged.
const RuntimeTmpSubTileImage* resolveRuntimeTerrainVisualCellImage(
    const TerrainCell& cell, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, RuntimeTerrainVisualCellInfo* info = nullptr
);

RuntimeMapRenderStats drawRuntimeMap(
    HDC dc,
    const RECT& viewport,
    const GameWorld& world,
    const RuntimeMapRenderContext& context,
    RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache
);

bool resolveRuntimeTerrainRadarColors(
    const TerrainCell& cell,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    RuntimePaletteColor& left,
    RuntimePaletteColor& right
);
// Fixed 60x30 projection expressed as world levels, independent of the user's
// camera and visibility. A zero direction requests current overlap only.
double runtimeKirovClearanceTargetLevel(
    const GameWorld& world, const WorldObjectInstance& aircraft,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    double directionX, double directionY, double approachSpeed = -1.0
);

// Returns the original TMP grid dimensions without querying or drawing pixels.
// Missing resources, invalid sub-tile indices and unknown grids return false.
bool resolveRuntimeTerrainTileGridShape(
    const TerrainCell& cell,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    int& gridWidth,
    int& gridHeight
);

// Audio uses the same actual body position and authored shoreline as animation.
bool resolveRuntimeInfantrySwimming(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache
);

// Shore TMPs contain a mix of beach, rough, and sometimes fully submerged
// sub-tiles. Query the authored cell instead of treating the whole
// ShorePieces tile set as one movement domain.
unsigned char resolveRuntimeTerrainSubTileRampType(
    const TerrainCell& cell, const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache);

RuntimeTerrainLandType resolveRuntimeTerrainSubTileLandType(
    const TerrainCell& cell,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache
);

bool resolveRuntimeSpawnedMissileSpriteBounds(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RECT& bounds
);

bool resolveRuntimeObjectShpSpriteBounds(
    const WorldObjectInstance& object,
    const POINT& center,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    RECT& spriteRect,
    bool stableGlobalCanvas = false
);

// Capture the physical infantry's authored whole-canvas pivot relative to its
// world draw point. Uses canonical art at standard 60/120 cameras, without
// viewer disguises or opaque-frame bounds. Outputs use runtimeMuzzlePixelX/Y's
// existing affine screen/fixed convention; failure leaves all outputs intact.
bool resolveRuntimeInfantryDeathCanvasPivotOffsets(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    int& screenX, int& screenY, int& fixedX, int& fixedY
);

// center is the object's actual projected draw point, including flight and
// sub-cell offsets. Bounds contain the current visible pixels of all body parts.
bool resolveRuntimeMobileObjectSpriteBounds(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RECT& spriteRect
);

// Capture the airborne body's opaque center at impact time. The affine pixel
// correction preserves bitmap size and world projection at other camera scales;
// the frozen roof/factory profile applies its support lift at the current scale.
// Shadows and parachutes are excluded; failure leaves all output values intact.
bool resolveRuntimeAirImpactBodyCenterOffset(
    const GameWorld& world, const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    double hitX, double hitY, double hitLevel,
    int& screenX, int& screenY, int& fixedX, int& fixedY,
    RuntimeParticleAirbornePresentation& presentation, int& spriteOffsetY
);

struct RuntimeStructureSpriteGeometry {
    RECT bounds{};
    std::vector<RuntimeSpriteHitSpan> hitSpans;
    std::vector<RuntimeSpriteHitSpan> foregroundHitSpans;
    int layerCount = 0;
};

// Replays base, buildup, bib, animated layers and independent turret/barrel
// through their production frame/offset decisions. Shadows are excluded.
bool resolveRuntimeStructureSpriteGeometry(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeStructureSpriteGeometry& geometry
);

bool runtimeSpriteHitSpansContain(const std::vector<RuntimeSpriteHitSpan>& spans, POINT point);

// Fresh building-only visibility replay. A participating but fully occluded
// building has an entry with no spans. False preserves the caller's fallback.
bool resolveRuntimeBuildingVisibleHitSpans(const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>>& visibleSpans);

// The audited flat-ground Drive/VXL branch only. Missing or exceptional
// neighboring terrain metadata keeps the existing rendering path.
bool runtimeGroundVehicleUsesBuildingDepth(const WorldObjectInstance& object,
    const GameWorld& world, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache);

// Retail InfantryClass's -10 SHP bias on verified same-height Walk terrain.
bool runtimeGroundInfantryUsesBuildingDepth(const WorldObjectInstance& object,
    const GameWorld& world, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache);

struct RuntimeObjectWorldRenderPlacement {
    POINT center{};
    POINT sortPoint{};
    RECT bodyViewport{};
    bool airborneForeground = false;
    int factoryForegroundHostId = -1;
};

// Includes the final aircraft-over-building lift and producer-roof clipping.
RuntimeObjectWorldRenderPlacement runtimeObjectWorldRenderPlacement(
    const WorldObjectInstance& object, const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache
);

struct RuntimeMobileSpriteBoundsProbe {
    RECT spriteBounds{};
    RECT renderedBounds{};
    int renderedPixels = 0;
    int outsidePixels = 0;
    int layerCount = 0;
    bool resourceResolved = false;
    bool exactMatch = false;
    bool clipped = false;
    std::string diagnostic;
    // The isolated body render, for contact sheets and visual review.
    std::vector<std::uint32_t> pixels;
    int imageWidth = 0;
    int imageHeight = 0;
    POINT imageOrigin{};
};

// Test-only: runs the actual compositor on two backgrounds and checks every
// changed pixel against the geometry query. Missing resources are failures.
bool verifyRuntimeMobileObjectSpriteBounds(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeMobileSpriteBoundsProbe& probe
);

bool drawRuntimeObjectPreviewSprite(
    HDC dc,
    const RECT& viewport,
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    RECT* spriteRect = nullptr
);

bool loadRuntimePaletteFile(
    const std::string& palettePath,
    std::vector<RuntimePaletteColor>& palette,
    std::string& errorMessage
);

bool loadRuntimePaletteResource(
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    const std::string& paletteId,
    std::vector<RuntimePaletteColor>& palette,
    std::string& sourcePath,
    std::string& errorMessage
);

bool loadRuntimePaletteFromMixArchive(
    const std::string& mixPath,
    RuntimeMapRenderCache& cache,
    const std::string& paletteId,
    std::vector<RuntimePaletteColor>& palette,
    std::string& sourcePath,
    std::string& errorMessage
);

bool loadRuntimeShpFrameFromMixArchive(
    const std::string& mixPath,
    RuntimeMapRenderCache& cache,
    const std::string& imageId,
    int frameIndex,
    RuntimeShpFrameImage& image,
    std::string& sourcePath,
    std::string& errorMessage
);

// Releases only the decoded frames and source bytes owned by one auxiliary
// SHP image. Structure FreeBuildup uses this after the final concurrent
// construction animation has completed.
std::size_t releaseRuntimeAuxiliaryShpImageFromCache(
    RuntimeMapRenderCache& cache,
    const std::string& imageId
);

// Returns a copy with the existing SHP owner remap applied to indices16..31.
// A palette shorter than the required256 entries returns an empty result.
std::vector<RuntimePaletteColor> runtimeOwnerRemappedUiPalette(
    const std::vector<RuntimePaletteColor>& palette,
    COLORREF ownerColor
);

bool decodeRuntimePcxImageBytes(
    const std::vector<unsigned char>& bytes,
    RuntimeShpFrameImage& image,
    std::vector<RuntimePaletteColor>& palette,
    std::string& errorMessage
);

bool decodeRuntimePcxImageFile(
    const std::string& pcxPath,
    RuntimeShpFrameImage& image,
    std::vector<RuntimePaletteColor>& palette,
    std::string& errorMessage
);

bool loadRuntimePcxImageFromMixArchive(
    const std::string& mixPath,
    RuntimeMapRenderCache& cache,
    const std::string& imageId,
    RuntimeShpFrameImage& image,
    std::vector<RuntimePaletteColor>& palette,
    std::string& sourcePath,
    std::string& errorMessage
);

bool decodeRuntimeShpFrameFile(
    const std::string& shpPath,
    int frameIndex,
    RuntimeShpFrameImage& image,
    std::string& errorMessage
);

bool drawRuntimeAuxiliaryShpFrame(
    HDC dc,
    const RECT& viewport,
    const POINT& center,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    const std::string& imageId,
    int frameIndex,
    int offsetX = 0,
    int offsetY = 0,
    bool mainFramesOnly = false
);

bool dumpRuntimeVoxelAtlasForObject(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    std::string& errorMessage
);

bool loadRuntimeAuxiliaryShpFrameImage(
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    const std::string& imageId,
    int frameIndex,
    RuntimeShpFrameImage& image,
    std::string& resolvedImageId,
    std::string& sourcePath,
    std::string& errorMessage,
    bool mainFramesOnly = false,
    int* frameCount = nullptr,
    const ArtType* fallbackArtType = nullptr
);

bool runRuntimeVehicleBodyRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
);
bool runRuntimeVehicleBlastRenderContractCheck(
    const RuntimeMapRenderContext& context, const std::vector<WorldObjectInstance>& vehicles,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& timeline,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& restoredTimeline,
    const std::string& label, std::string& error);
bool runRuntimeCannonImpulseRenderCheck(
    const RuntimeMapRenderContext& context, const WorldObjectInstance& vehicle,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& timeline,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& restoredTimeline,
    const std::string& label, std::string& error);

// Exercises original Cheer sequence selection without changing simulation orders.
bool runRuntimePlayerCheerSequenceCheck(std::string& error);
bool runRuntimeMagnetronRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<SpecialMechanismRenderSnapshot>& beams, std::string& error);
bool runRuntimeMagnetronLiftShadowContractCheck(const RuntimeMapRenderContext& context,
    const GameWorld& loadedWorld, std::string& error);
bool runRuntimePanicRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error);
bool runRuntimeIvanBombRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error);
bool runRuntimeSquidAnimationRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error);
int runtimeObjectShpFrameForGoldenCapture(const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, int frameCount);
bool runRuntimeModExtensionRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error);
RuntimeScreenShakeOffset runtimeScreenShakeForRender(const RuntimeMapRenderContext& context);
bool applyRuntimeScreenShake(HDC dc, const RECT& viewport, const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache);

bool runRuntimeChaosRenderContractCheck(const RuntimeMapRenderContext& context, const GameWorld& world,
    const std::vector<RuntimeImpactRenderActor>& gasFrames, std::string& error);

bool runRuntimeGrindingAnimationRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& hosts,
    const std::vector<std::vector<RuntimeImpactRenderActor>>& poses, std::string& error);

bool runRuntimeMutationPaletteRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error);

// Finite issue22 original TMP/VXL ground-shadow diagnostic; no simulation mutation.
bool runRuntimeVehicleGroundShadowIssue22Smoke(
    const RuntimeMapRenderContext& context, std::string& error);

// Finite issue24 original VXL hover diagnostic/strict visual contract.
bool runRuntimeHoverTransportIssue24Smoke(
    const RuntimeMapRenderContext& context, std::string& error);

// Finite issue25 map-overlay identity diagnostic using native registry ordinals.
bool runRuntimeNeutralWallIdentityIssue25Contract(const RuntimeMapRenderContext& context, std::string& error);
