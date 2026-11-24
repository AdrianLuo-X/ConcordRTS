#pragma once

#include "render/buildings/RuntimeBuildingDepthRaster.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace concord::render {

struct RuntimeBuildingDepthDraw {
    RuntimeBuildingDepthFrame description;
    const RuntimeShpFrameImage* image = nullptr;
    std::shared_ptr<std::vector<unsigned char>> pinnedPixels;
    RuntimeInputPixelRect rect{};
    int instanceId = -1;
    double ySort = 0.0;
    std::size_t sequence = 0;
    int opacity = 255;
    std::function<std::vector<std::uint32_t>()> colors;
    std::function<std::uint32_t()> gpuMaterial;
};

struct RuntimeBuildingDepthBlendPixel {
    struct Layer { std::uint32_t color; int opacity; int owner; std::uint32_t material=UINT32_MAX,sourceIndex=0; };
    std::vector<Layer> layers;
    bool emitted = false;
};

using RuntimeBuildingDepthBlendMap = std::unordered_map<std::size_t, RuntimeBuildingDepthBlendPixel>;

struct RuntimeBuildingDepthMaterialFrame {
    RuntimeInputPixelRect viewport;
    int width;
    std::span<const RuntimeBuildingDepthDraw> draws;
};

class RuntimeBuildingDepthMaterialReplay {
public:
    using SampleColors = std::vector<std::uint32_t> (*)(const RuntimeBuildingDepthDraw&);

    explicit RuntimeBuildingDepthMaterialReplay(SampleColors sampleColors) noexcept
        : sampleColors_(sampleColors) {}

    void composeVisible(RuntimeBuildingDepthMaterialFrame frame,
        std::span<const RuntimeBuildingDepthPixel> pixels, const std::unordered_set<int>& frozenOwners,
        const RuntimeBuildingDepthDrawChains& chains, RuntimeBuildingDepthBlendMap& blends);

    void composeFrozen(RuntimeBuildingDepthMaterialFrame frame,
        const RuntimeBuildingDepthDrawChains& underlayChains, RuntimeBuildingDepthBlendMap& blends);

private:
    SampleColors sampleColors_;
    std::unordered_map<int, std::vector<std::uint32_t>> colors_;
    std::unordered_map<int, std::uint32_t> gpuMaterials_;
};

std::uint32_t blendRuntimeBuildingDepthLayers(const RuntimeBuildingDepthBlendPixel& blend,
    std::uint32_t destination, const std::unordered_set<int>& frozenOwners,
    const std::unordered_map<int,std::size_t>& ownerPaintOrdinals, std::size_t afterOrdinal);

struct RuntimeBuildingDepthVisibleOwnerBuffer {
    RuntimeInputPixelRect viewport;
    int width;
    std::span<const RuntimeBuildingDepthPixel> pixels;
};

struct RuntimeBuildingDepthColorBuffer {
    std::uint32_t* pixels;
    int width;
};

struct RuntimeBuildingDepthReceiverBuffer {
    RuntimeInputPixelRect bounds;
    int width;
    int receiver;
    int* pixels;
};

void drawRuntimeResolvedBuildingOpaqueSpans(const RuntimeShpFrameImage& image,
    RuntimeBuildingDepthVisibleOwnerBuffer frame, int owner, RuntimeBuildingDepthColorBuffer color,
    const RuntimeBuildingDepthReceiverBuffer* lights, RuntimeInputPixelPoint target,
    RuntimeInputPixelRect sourceClip, const std::uint32_t* palette,
    const unsigned char* remapMask, const std::uint32_t* remapPalette);

bool runtimeBuildingDepthOwnerCoversMarkerSample(
    std::span<const RuntimeBuildingDepthDraw> draws, const RuntimeShpFrameImage* zShape,
    const std::unordered_set<int>& occludingStructureIds, const std::unordered_set<int>& frozenOwners,
    bool opaqueMarkerCovers, RuntimeInputPixelPoint point, int finalOwner);

struct RuntimeBuildingDepthBlendOrder {
    const std::unordered_set<int>& frozenOwners;
    const std::unordered_map<int, std::size_t>& paintOrdinals;
};

struct RuntimeBuildingDepthGpuChains {
    std::vector<std::array<std::uint32_t, 2>> descriptors;
    std::vector<std::array<std::uint32_t, 4>> operations;
};

bool compositeRuntimeBuildingDepthOnce(RuntimeBuildingDepthBlendMap& blends,
    std::size_t pixelIndex, RuntimeBuildingDepthBlendOrder order, std::uint32_t& destination);

void recordRuntimeBuildingDepthForeground(std::span<std::size_t> foreground,
    RuntimeInputPixelRect viewport, int width, RuntimeInputPixelPoint point, std::size_t ordinal);

bool appendRuntimeBuildingDepthGpuPixel(RuntimeBuildingDepthBlendMap& blends,
    std::size_t pixelIndex, std::size_t spriteIndex, RuntimeBuildingDepthBlendOrder order,
    RuntimeBuildingDepthGpuChains& output);

RuntimeBuildingDepthGpuChains buildRuntimeBuildingDepthGpuUnderlay(
    const RuntimeBuildingDepthBlendMap& blends, std::span<const RuntimeBuildingDepthPixel> pixels,
    std::span<const std::size_t> foreground, int owner, RuntimeBuildingDepthBlendOrder order);

} // namespace concord::render
