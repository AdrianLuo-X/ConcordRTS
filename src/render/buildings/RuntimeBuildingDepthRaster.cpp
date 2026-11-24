#include "render/buildings/RuntimeBuildingDepthRaster.h"
#include "render/buildings/RuntimeBuildingDepthReplay.h"

#include "render/buildings/RuntimeBuildingDepthRowKernel.h"
#include "render/core/RuntimeSpriteFrameImage.h"

#include <algorithm>
#include <optional>

namespace concord::render {

namespace {
// A scope lends this storage only on its own thread and returns it at exit.
// Every reused pixel is initialized before resolution; neither winners nor
// addresses survive into a later paint. Large surfaces retain no giant pool.
static thread_local std::vector<RuntimeBuildingDepthPixel> runtimeBuildingDepthPixelScratch;
} // namespace

void borrowRuntimeBuildingDepthScratch(std::vector<RuntimeBuildingDepthPixel>& pixels) noexcept {
    pixels.swap(runtimeBuildingDepthPixelScratch);
}

void releaseRuntimeBuildingDepthScratch(std::vector<RuntimeBuildingDepthPixel>& pixels) noexcept {
    constexpr std::size_t maximumScratchBytes = 64U * 1024U * 1024U;
    if (pixels.capacity() <= maximumScratchBytes / sizeof(RuntimeBuildingDepthPixel))
        pixels.swap(runtimeBuildingDepthPixelScratch);
}

bool runtimeBuildingDepthImageRequiresPin(const RuntimeShpFrameImage& image) {
    return !image.rasterCacheKey.empty() || !image.depthLevels.empty()
        || !image.rasterFunctionActuallyCalled.empty();
}

void rasterRuntimeBuildingDepthDraw(const RuntimeBuildingDepthRasterDraw& draw,
    RuntimeBuildingDepthRasterTarget target, RuntimeBuildingDepthRasterTemplate zShape,
    const std::unordered_set<int>& frozenOwners, std::size_t drawIndex,
    RuntimeBuildingDepthAlphaRaster* alpha) {
    const auto& pixels = draw.pinnedPixels ? *draw.pinnedPixels : draw.image->pixels;
    const int width = draw.description.width, height = draw.description.height;
    const int firstY = std::max(0, static_cast<int>(target.viewport.top - draw.rect.top));
    const int lastY = std::min(height, static_cast<int>(target.viewport.bottom - draw.rect.top));
    const int firstX = std::max(0, static_cast<int>(target.viewport.left - draw.rect.left));
    const int lastX = std::min(width, static_cast<int>(target.viewport.right - draw.rect.left));
    std::optional<RuntimeBuildingDepthRowKernel> kernel;
    if (!zShape.reference) kernel.emplace(draw.description, zShape.image->pixels,
        zShape.image->width, zShape.image->height);
    const bool writeDepth = draw.description.layer == RuntimeBuildingDepthLayer::Body
        || (draw.description.layer == RuntimeBuildingDepthLayer::Bib || draw.description.layer == RuntimeBuildingDepthLayer::WallSprite);
    const bool opaqueUnfrozen = draw.opacity == 255 && !frozenOwners.contains(draw.instanceId);
    const auto rasterRowRange = [&](int y, int rangeFirstX, int rangeLastX) {
        const auto row = kernel ? kernel->row(y) : RuntimeBuildingDepthRowKernel::Row{};
        const auto sourceRow = static_cast<std::size_t>(y) * width;
        const auto targetRow = static_cast<std::size_t>(draw.rect.top + y - target.viewport.top) * target.width
            + static_cast<std::size_t>(draw.rect.left + firstX - target.viewport.left);
        for (int x = rangeFirstX; x < rangeLastX; ++x) {
            if (!pixels[sourceRow + x]) continue;
            int depth = 0;
            if (zShape.reference) {
                if (!runtimeBuildingRenderDepthAtPixel(draw.description, zShape.image->pixels,
                    zShape.image->width, zShape.image->height, x, y, depth)) continue;
            } else depth = row.depthAtKnownX(x);
            const auto pixelIndex = targetRow + static_cast<std::size_t>(x - firstX);
            auto& pixel = target.pixels[pixelIndex];
            if (!runtimeBuildingDepthAccept(pixel, draw.instanceId, depth, writeDepth)) continue;
            if (alpha) {
                // A frozen object's completed body is translucent even when
                // its individual SHP/VXL parts are opaque. Retain the scene
                // underneath until those parts can be blended together once.
                if (opaqueUnfrozen) {
                    alpha->lastOpaque[pixelIndex]=static_cast<int>(drawIndex);alpha->chains.erase(pixelIndex);
                } else {
                    auto [entry,inserted]=alpha->chains.try_emplace(pixelIndex);
                    if(inserted&&alpha->lastOpaque[pixelIndex]>=0)entry->second.push_back(alpha->lastOpaque[pixelIndex]);
                    entry->second.push_back(static_cast<int>(drawIndex));
                }
            }
        }
    };
    // Cached native SHP runs contain exactly the nonzero source pixels.
    // Keep pinned/VXL, frozen, reference and uncached images on the old scan.
    const bool cachedOpaqueSpans = !zShape.reference && opaqueUnfrozen
        && !draw.pinnedPixels && !runtimeBuildingDepthImageRequiresPin(*draw.image)
        && draw.image->spriteOpaqueSpansValid
        && draw.image->width == width && draw.image->height == height
        && width > 0 && height > 0
        && pixels.size() >= static_cast<std::size_t>(width) * height;
    if (cachedOpaqueSpans) {
        for (const auto& span : draw.image->spriteOpaqueSpans) {
            if (span.y < firstY) continue;
            if (span.y >= lastY) break;
            const int left = std::max(firstX, static_cast<int>(span.left));
            const int right = std::min(lastX, static_cast<int>(span.right));
            if (left < right) rasterRowRange(span.y, left, right);
        }
    } else {
        for (int y = firstY; y < lastY; ++y) rasterRowRange(y, firstX, lastX);
    }
}

void rasterRuntimeBuildingDepthFrozenUnderlay(const RuntimeBuildingDepthRasterDraw& draw,
    RuntimeBuildingDepthFrozenRaster target, RuntimeBuildingDepthRasterTemplate zShape,
    const std::unordered_set<int>& frozenOwners, std::size_t index) {
    const auto& pixels=draw.pinnedPixels?*draw.pinnedPixels:draw.image->pixels;
    std::optional<RuntimeBuildingDepthRowKernel> kernel;
    if (!zShape.reference) kernel.emplace(draw.description, zShape.image->pixels,
        zShape.image->width, zShape.image->height);
    const bool writeDepth = draw.description.layer == RuntimeBuildingDepthLayer::Body
        || (draw.description.layer == RuntimeBuildingDepthLayer::Bib || draw.description.layer == RuntimeBuildingDepthLayer::WallSprite);
    for(int y=std::max(0,static_cast<int>(target.background.viewport.top-draw.rect.top));
        y<std::min(draw.description.height,static_cast<int>(target.background.viewport.bottom-draw.rect.top));++y) {
        const auto row = kernel ? kernel->row(y) : RuntimeBuildingDepthRowKernel::Row{};
        for(int x=std::max(0,static_cast<int>(target.background.viewport.left-draw.rect.left));
            x<std::min(draw.description.width,static_cast<int>(target.background.viewport.right-draw.rect.left));++x) {
            if(!pixels[static_cast<std::size_t>(y)*draw.description.width+x])continue;
            const auto p=static_cast<std::size_t>(draw.rect.top+y-target.background.viewport.top)*target.background.width
                +draw.rect.left+x-target.background.viewport.left;
            const int owner=target.visible[p].visibleOwner;
            if(owner==draw.instanceId||!frozenOwners.contains(owner))continue;
            int depth=0;
            if (zShape.reference) {
                if(!runtimeBuildingRenderDepthAtPixel(draw.description,zShape.image->pixels,
                    zShape.image->width,zShape.image->height,x,y,depth))continue;
            } else depth = row.depthAtKnownX(x);
            if(!runtimeBuildingDepthAccept(target.background.pixels[p],draw.instanceId,depth,writeDepth))continue;
            auto& chain=target.underlayChains[p];
            if(draw.opacity==255&&!frozenOwners.contains(draw.instanceId))chain.clear();
            chain.push_back(static_cast<int>(index));
        }
    }
}

} // namespace concord::render
