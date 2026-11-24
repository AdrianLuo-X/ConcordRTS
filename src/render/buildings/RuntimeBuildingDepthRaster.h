#pragma once

#include "render/buildings/RuntimeBuildingDepth.h"
#include "ui/input/RuntimeInputPixelValues.h"

#include <cstddef>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct RuntimeShpFrameImage;

namespace concord::render {

using RuntimeBuildingDepthDrawChains = std::unordered_map<std::size_t, std::vector<int>>;

struct RuntimeBuildingDepthRasterDraw {
    const RuntimeBuildingDepthFrame& description;
    RuntimeInputPixelRect rect;
    const RuntimeShpFrameImage* image;
    const std::vector<unsigned char>* pinnedPixels;
    int instanceId;
    int opacity;
};

struct RuntimeBuildingDepthRasterTarget {
    RuntimeInputPixelRect viewport;
    int width;
    std::span<RuntimeBuildingDepthPixel> pixels;
};

struct RuntimeBuildingDepthRasterTemplate {
    const RuntimeShpFrameImage* image;
    bool reference;
};

struct RuntimeBuildingDepthAlphaRaster {
    std::span<int> lastOpaque;
    RuntimeBuildingDepthDrawChains& chains;
};

struct RuntimeBuildingDepthFrozenRaster {
    RuntimeBuildingDepthRasterTarget background;
    std::span<const RuntimeBuildingDepthPixel> visible;
    RuntimeBuildingDepthDrawChains& underlayChains;
};

bool runtimeBuildingDepthImageRequiresPin(const RuntimeShpFrameImage& image);

void rasterRuntimeBuildingDepthDraw(const RuntimeBuildingDepthRasterDraw& draw,
    RuntimeBuildingDepthRasterTarget target, RuntimeBuildingDepthRasterTemplate zShape,
    const std::unordered_set<int>& frozenOwners, std::size_t drawIndex,
    RuntimeBuildingDepthAlphaRaster* alpha);

void rasterRuntimeBuildingDepthFrozenUnderlay(const RuntimeBuildingDepthRasterDraw& draw,
    RuntimeBuildingDepthFrozenRaster target, RuntimeBuildingDepthRasterTemplate zShape,
    const std::unordered_set<int>& frozenOwners, std::size_t index);

} // namespace concord::render
