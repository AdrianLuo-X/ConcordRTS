#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RuntimePaletteColor {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
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

