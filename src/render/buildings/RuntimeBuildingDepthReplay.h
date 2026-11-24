#pragma once

#include "render/buildings/RuntimeBuildingDepthMaterials.h"
#include "render/core/RuntimeSpriteFrameImage.h"

namespace concord::render {

struct RuntimeBuildingDepthObjectState {
    int instanceId = -1;
    RuntimeInputPixelPoint center{};
    int nativeY = 0, heightPixel = 0;
    int foundationWidth = 1, foundationHeight = 1;
    int moveX = 0, moveY = 0, normalZAdjust = 0;
    double ySort = 0.0, animationYSort = 0.0;
    bool mobileVoxel = false;
};

struct RuntimeBuildingDepthPartState {
    RuntimeBuildingDepthLayer layer = RuntimeBuildingDepthLayer::Body;
    int zAdjust = 0, yDrawOffset = 0;
    double ySortAdjust = 0;
    bool independentSort = false;
    int nativeOffsetY = 0;
};

inline RuntimeBuildingDepthFrame describeRuntimeBuildingDepthReplay(int imageTop,
    const RuntimeShpFrameImage& image, const RuntimeBuildingDepthObjectState& object,
    const RuntimeBuildingDepthPartState& part) {
    RuntimeBuildingDepthFrame frame;
    frame.canvasWidth = image.globalWidth > 0 ? image.globalWidth : image.width;
    frame.canvasHeight = image.globalHeight > 0 ? image.globalHeight : image.height;
    frame.width = image.width; frame.height = image.height;
    frame.offsetX = image.offsetX; frame.offsetY = image.offsetY;
    frame.foundationWidth = object.foundationWidth; frame.foundationHeight = object.foundationHeight;
    frame.moveX = object.moveX; frame.moveY = object.moveY;
    frame.normalZAdjust = object.normalZAdjust;
    frame.nativeFrameTopY = object.nativeY + imageTop - object.center.y
        + part.nativeOffsetY;
    frame.heightPixel = object.heightPixel;
    frame.layer = part.layer;
    frame.zAdjust = part.zAdjust;
    frame.yDrawOffset = part.yDrawOffset;
    if (object.mobileVoxel) {
        frame.layer = RuntimeBuildingDepthLayer::NavalVoxel;
        frame.navalBottom = image.sceneDepthBoundsValid ? image.sceneDepthBoundsBottom : image.height;
    }
    return frame;
}

struct RuntimeBuildingDepthReplayTarget {
    RuntimeBuildingDepthRasterTarget target;
    const RuntimeShpFrameImage* zShape;
    bool resolved;
    bool referenceMode;
};

inline bool acceptRuntimeBuildingDepthReplayPixel(const RuntimeBuildingDepthFrame& frame,
    RuntimeBuildingDepthReplayTarget state, const RuntimeBuildingDepthObjectState* object,
    RuntimeInputPixelPoint screen, int x, int y) {
    if (screen.x < state.target.viewport.left || screen.x >= state.target.viewport.right
        || screen.y < state.target.viewport.top || screen.y >= state.target.viewport.bottom) return false;
    int depth = 0;
    // The resolved pass only reads the winner already computed for this
    // pixel. Its current part's depth cannot change that winner.
    if ((!state.resolved || state.referenceMode)
        && !runtimeBuildingRenderDepthAtPixel(frame, state.zShape->pixels, state.zShape->width,
            state.zShape->height, x, y, depth)) return false;
    auto& pixel = state.target.pixels[static_cast<std::size_t>(screen.y - state.target.viewport.top)
        * state.target.width + screen.x - state.target.viewport.left];
    if (state.resolved) {
        if (pixel.visibleOwner != object->instanceId) return false;
    } else if (!runtimeBuildingDepthAccept(pixel, object->instanceId,
        depth, frame.layer == RuntimeBuildingDepthLayer::Body
            || (frame.layer == RuntimeBuildingDepthLayer::Bib || frame.layer == RuntimeBuildingDepthLayer::WallSprite))) return false;
    return true;
}

inline void recordRuntimeBuildingDepthReplayReceiver(RuntimeBuildingDepthReceiverBuffer lights,
    RuntimeInputPixelPoint screen) {
    if (screen.x >= lights.bounds.left && screen.x < lights.bounds.right
        && screen.y >= lights.bounds.top && screen.y < lights.bounds.bottom) {
        lights.pixels[static_cast<std::size_t>(screen.y - lights.bounds.top)
            * lights.width + screen.x - lights.bounds.left] = lights.receiver;
    }
}

inline void pinRuntimeBuildingDepthDraw(RuntimeBuildingDepthDraw& draw, const RuntimeShpFrameImage& image,
    const std::shared_ptr<RuntimeShpFrameImage>* ownedImageSnapshot) {
    if (ownedImageSnapshot) {
        draw.image = ownedImageSnapshot->get();
        draw.pinnedPixels = std::shared_ptr<std::vector<unsigned char>>(
            *ownedImageSnapshot, &(*ownedImageSnapshot)->pixels);
    } else {
        draw.pinnedPixels = std::make_shared<std::vector<unsigned char>>(image.pixels);
    }
}

void borrowRuntimeBuildingDepthScratch(std::vector<RuntimeBuildingDepthPixel>& pixels) noexcept;
void releaseRuntimeBuildingDepthScratch(std::vector<RuntimeBuildingDepthPixel>& pixels) noexcept;

} // namespace concord::render
