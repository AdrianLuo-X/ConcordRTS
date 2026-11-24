bool runtimeFindInfantryMuzzleAnchor(const std::string& imageId,
    const std::string& sequenceName, int frameIndex, int globalWidth, int globalHeight,
    POINT& canvasPoint) {
    RuntimeInputPixelPoint point{};
    if (!concord::render::geometry::findInfantryMuzzleAnchor(imageId, sequenceName,
            frameIndex, globalWidth, globalHeight, point)) return false;
    canvasPoint = {point.x, point.y};
    return true;
}
