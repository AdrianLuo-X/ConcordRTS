#include "render/terrain/RuntimeBridgeViewportBounds.h"

#include "assets/codecs/RuntimeTmpTypes.h"

namespace concord::render {

RuntimeInputPixelPoint runtimeBridgeViewportCenter(
    RuntimeInputPixelPoint center, RuntimeInputPixelSize tileSize, bool highBridge) noexcept {
    if (highBridge) {
        center.x -= tileSize.width / 2;
        center.y -= tileSize.height / 2 + 1;
    }
    return center;
}

RuntimeInputPixelRect runtimeBridgeViewportTmpBounds(
    RuntimeInputPixelPoint center, const RuntimeTmpSubTileImage& image) noexcept {
    const int globalWidth = image.globalWidth > 0 ? image.globalWidth : image.width;
    const int globalHeight = image.globalHeight > 0 ? image.globalHeight : image.height;
    const int left = center.x - globalWidth / 2 + image.offsetX;
    const int top = center.y - globalHeight / 2 + image.offsetY;
    return {left, top, left + image.width, top + image.height};
}

} // namespace concord::render
