#include "render/buildings/RuntimeBuildingDepthMaterials.h"

#include "render/core/RuntimeSpriteFrameImage.h"

#include <algorithm>

namespace concord::render {

void RuntimeBuildingDepthMaterialReplay::composeVisible(RuntimeBuildingDepthMaterialFrame frame,
    std::span<const RuntimeBuildingDepthPixel> pixels, const std::unordered_set<int>& frozenOwners,
    const RuntimeBuildingDepthDrawChains& chains, RuntimeBuildingDepthBlendMap& blends) {
    for(const auto& [pixelIndex,chain]:chains) {
        const int owner=pixels[pixelIndex].visibleOwner;
        // The visible frozen object retains its existing whole-body temporal
        // scope. Its background is restored below; replaying this chain too
        // would apply the foreign frozen layers twice.
        if(frozenOwners.contains(owner))continue;
        if(!std::any_of(chain.begin(),chain.end(),[&](int index){return frame.draws[index].instanceId!=owner;}))continue;
        auto& blend=blends[pixelIndex];
        const int screenX=frame.viewport.left+static_cast<int>(pixelIndex%frame.width);
        const int screenY=frame.viewport.top+static_cast<int>(pixelIndex/frame.width);
        for(const int index:chain) {
            const auto& draw=frame.draws[index];
            const auto sourceIndex=static_cast<std::size_t>(screenY-draw.rect.top)*draw.description.width+screenX-draw.rect.left;
            if(draw.gpuMaterial){auto [m,created]=gpuMaterials_.try_emplace(index);if(created)m->second=draw.gpuMaterial();
                blend.layers.push_back({0,draw.opacity,draw.instanceId,m->second,static_cast<std::uint32_t>(sourceIndex)});continue;}
            auto [entry,inserted]=colors_.try_emplace(index);
            if (inserted) entry->second=sampleColors_(draw);
            blend.layers.push_back({entry->second[sourceIndex],draw.opacity,draw.instanceId});
        }
    }
}

void RuntimeBuildingDepthMaterialReplay::composeFrozen(RuntimeBuildingDepthMaterialFrame frame,
    const RuntimeBuildingDepthDrawChains& underlayChains, RuntimeBuildingDepthBlendMap& blends) {
    for(const auto& [p,chain]:underlayChains) {
        auto& blend=blends[p];
        const int sx=frame.viewport.left+static_cast<int>(p%frame.width);
        const int sy=frame.viewport.top+static_cast<int>(p/frame.width);
        for(const int index:chain) {
            const auto& draw=frame.draws[index];
            if(draw.gpuMaterial){auto [m,created]=gpuMaterials_.try_emplace(index);if(created)m->second=draw.gpuMaterial();
                const auto sourceIndex=static_cast<std::size_t>(sy-draw.rect.top)*draw.description.width+sx-draw.rect.left;
                blend.layers.push_back({0,draw.opacity,draw.instanceId,m->second,static_cast<std::uint32_t>(sourceIndex)});continue;}
            auto [entry,inserted]=colors_.try_emplace(index);
            if (inserted) entry->second=sampleColors_(draw);
            blend.layers.push_back({entry->second[static_cast<std::size_t>(sy-draw.rect.top)
                *draw.description.width+sx-draw.rect.left],draw.opacity,draw.instanceId});
        }
    }
}

std::uint32_t blendRuntimeBuildingDepthLayers(const RuntimeBuildingDepthBlendPixel& blend, std::uint32_t destination,
    const std::unordered_set<int>& frozenOwners, const std::unordered_map<int,std::size_t>& ownerPaintOrdinals,
    std::size_t afterOrdinal) {
    const auto blendColor=[](std::uint32_t background,std::uint32_t foreground,int opacity) {
        std::uint32_t color=0;
        for(unsigned shift:{0U,8U,16U})color|=((((foreground>>shift)&255U)*opacity
            +((background>>shift)&255U)*(255-opacity)+127U)/255U)<<shift;
        return color;
    };
    int groupOwner=-1;
    std::uint32_t groupBackground=destination;
    const auto finishGroup=[&] {
        if(groupOwner>=0&&frozenOwners.contains(groupOwner))
            destination=blendColor(groupBackground,destination,128);
    };
    for(const auto& layer:blend.layers) {
        if(afterOrdinal) {
            const auto found=ownerPaintOrdinals.find(layer.owner);
            if(found!=ownerPaintOrdinals.end()&&found->second<=afterOrdinal)continue;
        }
        if(layer.owner!=groupOwner) {
            finishGroup();
            groupOwner=layer.owner;
            groupBackground=destination;
        }
        destination=blendColor(destination,layer.color,layer.opacity);
    }
    finishGroup();
    return destination;
}

namespace {
// Ordinary opaque building parts need only the resolved owner and a palette
// lookup. Preserve the accepted pixels' local-light receiver without repeating
// depth calculation, transparency checks, or generic color-effect branches.
template<bool MaskedRemap, bool RecordLights>
void drawRuntimeResolvedBuildingOpaqueSpansImpl(
    const RuntimeShpFrameImage& image, const RuntimeBuildingDepthVisibleOwnerBuffer& frame,
    int owner, RuntimeBuildingDepthColorBuffer color, const RuntimeBuildingDepthReceiverBuffer* lights,
    int targetX, int targetY, int startX, int endX, int startY, int endY,
    const std::uint32_t* palette, const unsigned char* remapMask, const std::uint32_t* remapPalette) {
    startX = std::max(startX, static_cast<int>(frame.viewport.left) - targetX);
    endX = std::min(endX, static_cast<int>(frame.viewport.right) - targetX);
    startY = std::max(startY, static_cast<int>(frame.viewport.top) - targetY);
    endY = std::min(endY, static_cast<int>(frame.viewport.bottom) - targetY);
    if (startX >= endX || startY >= endY) return;
    for (const auto& span : image.spriteOpaqueSpans) {
        if (span.y < startY) continue;
        if (span.y >= endY) break;
        const int left = std::max(startX, static_cast<int>(span.left));
        const int right = std::min(endX, static_cast<int>(span.right));
        if (left >= right) continue;
        const int screenY = targetY + span.y;
        const int screenLeft = targetX + left;
        const auto sourceOffset = static_cast<std::size_t>(span.y) * image.width + left;
        const unsigned char* source = image.pixels.data() + sourceOffset;
        const auto* owners = frame.pixels.data()
            + (static_cast<std::size_t>(screenY - frame.viewport.top) * frame.width
                + static_cast<std::size_t>(screenLeft - frame.viewport.left));
        auto* target = color.pixels
            + static_cast<std::size_t>(screenY) * color.width + screenLeft;
        const unsigned char* mask = nullptr;
        if constexpr (MaskedRemap) mask = remapMask + sourceOffset;
        int* lightRow = nullptr;
        int lightLeft = 0, lightRight = 0, lightReceiver = -1;
        if constexpr (RecordLights) {
            if (screenY >= lights->bounds.top && screenY < lights->bounds.bottom) {
                lightRow = lights->pixels
                    + static_cast<std::size_t>(screenY - lights->bounds.top) * lights->width;
                lightLeft = lights->bounds.left;
                lightRight = lights->bounds.right;
                lightReceiver = lights->receiver;
            }
        }
        for (int index = 0; index < right - left; ++index) {
            if (owners[index].visibleOwner != owner) continue;
            if constexpr (MaskedRemap)
                target[index] = mask[index] ? remapPalette[source[index]] : palette[source[index]];
            else
                target[index] = palette[source[index]];
            if constexpr (RecordLights) {
                const int screenX = screenLeft + index;
                if (lightRow && screenX >= lightLeft && screenX < lightRight)
                    lightRow[screenX - lightLeft] = lightReceiver;
            }
        }
    }
}

} // namespace

void drawRuntimeResolvedBuildingOpaqueSpans(const RuntimeShpFrameImage& image,
    RuntimeBuildingDepthVisibleOwnerBuffer frame, int owner, RuntimeBuildingDepthColorBuffer color,
    const RuntimeBuildingDepthReceiverBuffer* lights, RuntimeInputPixelPoint target,
    RuntimeInputPixelRect sourceClip, const std::uint32_t* palette,
    const unsigned char* remapMask, const std::uint32_t* remapPalette) {
    const bool recordLights = lights != nullptr;
    if (remapMask) {
        if (recordLights)
            drawRuntimeResolvedBuildingOpaqueSpansImpl<true, true>(image, frame, owner, color, lights,
                target.x, target.y, sourceClip.left, sourceClip.right, sourceClip.top, sourceClip.bottom,
                palette, remapMask, remapPalette);
        else
            drawRuntimeResolvedBuildingOpaqueSpansImpl<true, false>(image, frame, owner, color, lights,
                target.x, target.y, sourceClip.left, sourceClip.right, sourceClip.top, sourceClip.bottom,
                palette, remapMask, remapPalette);
    } else {
        if (recordLights)
            drawRuntimeResolvedBuildingOpaqueSpansImpl<false, true>(image, frame, owner, color, lights,
                target.x, target.y, sourceClip.left, sourceClip.right, sourceClip.top, sourceClip.bottom,
                palette, remapMask, remapPalette);
        else
            drawRuntimeResolvedBuildingOpaqueSpansImpl<false, false>(image, frame, owner, color, lights,
                target.x, target.y, sourceClip.left, sourceClip.right, sourceClip.top, sourceClip.bottom,
                palette, remapMask, remapPalette);
    }
}

// Evaluate only the marker's small sample set. The accepted blend chain must
// contain an opaque-enough part of its final owner; a transparent or frozen
// silhouette alone does not justify the original behind-building hint.
bool runtimeBuildingDepthOwnerCoversMarkerSample(
    std::span<const RuntimeBuildingDepthDraw> draws, const RuntimeShpFrameImage* zShape,
    const std::unordered_set<int>& occludingStructureIds, const std::unordered_set<int>& frozenOwners,
    bool opaqueMarkerCovers, RuntimeInputPixelPoint point, int finalOwner) {
    if (!occludingStructureIds.contains(finalOwner)
        || frozenOwners.contains(finalOwner)) return false;
    if (opaqueMarkerCovers) return true;
    RuntimeBuildingDepthPixel replay;
    bool covered = false;
    for (const auto& draw : draws) {
        if (draw.opacity <= 0 || point.x < draw.rect.left || point.x >= draw.rect.right
            || point.y < draw.rect.top || point.y >= draw.rect.bottom) continue;
        const int x = point.x - draw.rect.left, y = point.y - draw.rect.top;
        const auto& pixels = draw.pinnedPixels ? *draw.pinnedPixels : draw.image->pixels;
        if (!pixels[static_cast<std::size_t>(y) * draw.description.width + x]) continue;
        int depth = 0;
        if (!runtimeBuildingRenderDepthAtPixel(draw.description, zShape->pixels,
            zShape->width, zShape->height, x, y, depth)) continue;
        const bool writeDepth = draw.description.layer == RuntimeBuildingDepthLayer::Body
            || draw.description.layer == RuntimeBuildingDepthLayer::Bib
            || draw.description.layer == RuntimeBuildingDepthLayer::WallSprite;
        if (!runtimeBuildingDepthAccept(replay, draw.instanceId, depth, writeDepth)) continue;
        if (draw.opacity == 255 && !frozenOwners.contains(draw.instanceId)) covered = false;
        if (draw.instanceId == finalOwner && draw.opacity >= 192) covered = true;
    }
    return covered;
}
namespace {

void appendRuntimeBuildingDepthGpuOperations(const RuntimeBuildingDepthBlendPixel& blend,
    std::size_t afterOrdinal, RuntimeBuildingDepthBlendOrder order,
    std::vector<std::array<std::uint32_t, 4>>& operations) {
    int groupOwner = -1;
    const auto finish = [&] {
        if (groupOwner >= 0 && order.frozenOwners.contains(groupOwner))
            operations.push_back({0, 257});
    };
    for (const auto& layer : blend.layers) {
        if (afterOrdinal) {
            const auto found = order.paintOrdinals.find(layer.owner);
            if (found != order.paintOrdinals.end() && found->second <= afterOrdinal)
                continue;
        }
        if (layer.owner != groupOwner) {
            finish();
            groupOwner = layer.owner;
            if (order.frozenOwners.contains(groupOwner))
                operations.push_back({0, 256});
        }
        if (layer.material == UINT32_MAX)
            operations.push_back({layer.color, static_cast<std::uint32_t>(layer.opacity), 0, 0});
        else
            operations.push_back({layer.material, static_cast<std::uint32_t>(layer.opacity), layer.sourceIndex, 1});
    }
    finish();
}

} // namespace

bool compositeRuntimeBuildingDepthOnce(RuntimeBuildingDepthBlendMap& blends,
    std::size_t pixelIndex, RuntimeBuildingDepthBlendOrder order, std::uint32_t& destination) {
    const auto found = blends.find(pixelIndex);
    if (found == blends.end())
        return false;
    auto& blend = found->second;
    if (!blend.emitted) {
        destination = blendRuntimeBuildingDepthLayers(blend, destination, order.frozenOwners,
            order.paintOrdinals, 0);
        blend.emitted = true;
    }
    return true;
}

void recordRuntimeBuildingDepthForeground(std::span<std::size_t> foreground,
    RuntimeInputPixelRect viewport, int width, RuntimeInputPixelPoint point, std::size_t ordinal) {
    if (foreground.empty() || point.x < viewport.left || point.x >= viewport.right
        || point.y < viewport.top || point.y >= viewport.bottom)
        return;
    const auto index = static_cast<std::size_t>(point.y - viewport.top) * width
        + point.x - viewport.left;
    foreground[index] = ordinal;
}

bool appendRuntimeBuildingDepthGpuPixel(RuntimeBuildingDepthBlendMap& blends,
    std::size_t pixelIndex, std::size_t spriteIndex, RuntimeBuildingDepthBlendOrder order,
    RuntimeBuildingDepthGpuChains& output) {
    const auto found = blends.find(pixelIndex);
    if (found == blends.end())
        return false;
    auto& blend = found->second;
    if (!blend.emitted) {
        const auto start = output.operations.size();
        appendRuntimeBuildingDepthGpuOperations(blend, 0, order, output.operations);
        output.descriptors[spriteIndex] = {static_cast<std::uint32_t>(start),
            static_cast<std::uint32_t>(output.operations.size() - start)};
        blend.emitted = true;
    }
    return true;
}

RuntimeBuildingDepthGpuChains buildRuntimeBuildingDepthGpuUnderlay(
    const RuntimeBuildingDepthBlendMap& blends, std::span<const RuntimeBuildingDepthPixel> pixels,
    std::span<const std::size_t> foreground, int owner, RuntimeBuildingDepthBlendOrder order) {
    RuntimeBuildingDepthGpuChains output;
    output.descriptors.resize(pixels.size());
    for (const auto& [index, blend] : blends) {
        if (pixels[index].visibleOwner != owner)
            continue;
        const auto start = output.operations.size();
        appendRuntimeBuildingDepthGpuOperations(blend, foreground[index], order, output.operations);
        output.descriptors[index] = {static_cast<std::uint32_t>(start),
            static_cast<std::uint32_t>(output.operations.size() - start)};
    }
    return output;
}

} // namespace concord::render
