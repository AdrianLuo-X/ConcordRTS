int runtimeVisibleMainFrameCount(int frameCount, bool splitShadow) {
    if (splitShadow && frameCount >= 2 && frameCount % 2 == 0) {
        return std::max(1, frameCount / 2);
    }

    return std::max(1, frameCount);
}