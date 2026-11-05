#pragma once
#include "render/core/RuntimeMapRenderer.h"

// Frozen original data declaration, used only to witness native ABI and value bits.
struct OriginalNativeShpFrameImage {
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
