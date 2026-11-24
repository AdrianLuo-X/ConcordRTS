#include "render/geometry/RuntimeStructureMuzzleGeometry.h"

bool runtimeNativeStructureMuzzleAnchor(const std::string& imageId,
    std::uint64_t fingerprint, int frame, int width, int height, POINT& result) {
    RuntimeInputPixelPoint point{};
    if (!concord::render::geometry::findStructureMuzzleAnchor(imageId, fingerprint,
            frame, width, height, point)) return false;
    result = {point.x, point.y};
    return true;
}
