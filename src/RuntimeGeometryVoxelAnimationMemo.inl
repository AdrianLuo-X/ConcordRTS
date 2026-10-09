// Only source metadata is shared. The animation clock, movement/idle choice,
// and frozen/chrono state remain per object and are evaluated on every call.
int runtimeGeometryVoxelAnimationFrameCount(const std::string& imageId,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache) {
    auto* batch = RuntimeGeometryQueryFrame::active;
    const bool memoize = !runtimeMovementReferenceForRender()
        && batch && batch->matchesVoxels(context, cache)
        && !runtimeVxlDisableCache();
    if (memoize) {
        const auto& memo = batch->memoOwner->voxelAnimationFrameCounts;
        const auto found = memo.find(imageId);
        if (found != memo.end()) return found->second;
    }
    const auto& source = loadRuntimeVoxelSourceFromCache(imageId, context, cache, false);
    const int frames = source.loaded ? source.metadata.hvaFrameCount : 0;
    if (memoize) batch->memoOwner->voxelAnimationFrameCounts.emplace(imageId, frames);
    return frames;
}
