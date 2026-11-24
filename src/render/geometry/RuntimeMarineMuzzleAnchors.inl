bool runtimeFindMarineMuzzleAnchor(const std::string& imageId, int frameIndex,
    int width, int height, POINT& result) {
    RuntimeInputPixelPoint point{};
    if (!concord::render::geometry::findMarineMuzzleAnchor(imageId, frameIndex,
            width, height, point)) return false;
    result = {point.x, point.y};
    return true;
}
