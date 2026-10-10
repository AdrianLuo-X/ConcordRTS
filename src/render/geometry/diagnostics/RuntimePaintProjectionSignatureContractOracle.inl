// Contract-only: reused original oracle, with no new field normalization.
bool runtimePaintProjectionSignatureContractRenderEqual(
    const RuntimeMapRenderStats& original, const RuntimeMapRenderStats& memoized,
    const RuntimeMapRenderCache& originalCache, const RuntimeMapRenderCache& memoizedCache,
    std::string& field) {
    if (!runtimeGroundShadowContractStatsEqual(original, memoized, field)) return false;
    if (!runtimeGroundShadowContractCacheEqual(originalCache, memoizedCache)) {
        field = "deepResourceCache";
        return false;
    }
    return true;
}
