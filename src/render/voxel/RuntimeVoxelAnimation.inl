int runtimeVoxelAnimationFrame(
    const WorldObjectInstance& object, const std::string& imageId,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache
) {
    const UnitType* unit = findObjectUnitTypeForRender(context, object);
    const bool airborneModel = runtimeObjectLooksAircraftForRender(object, context);
    // A flying model's authored HVA is its flight pose, including rotors.
    // Landed bodies keep the original first frame even during ground orders.
    if (airborneModel && !object.airborne) return 0;
    const bool moving = runtimeObjectShouldUseMovingSequence(object);
    if (!airborneModel && !moving && (unit == nullptr || unit->idleRate <= 0)) return 0;
    const int frames = runtimeGeometryVoxelAnimationFrameCount(imageId, context, cache);
    if (frames <= 1) return 0;
    const int delay = !moving && unit != nullptr && unit->idleRate > 0
        ? unit->idleRate : unit == nullptr ? 1 : unit->walkRate;
    return runtimeUnitAnimationStep(object, context, delay) % frames;
}

const std::vector<RuntimeHvaTransform>* runtimeVoxelFrameTransforms(
    RuntimeVoxelSourceCacheEntry& source, int frame, std::string& error
) {
    if (frame == 0) return &source.firstFrameTransforms;
    const auto found = source.transformsByFrame.find(frame);
    if (found != source.transformsByFrame.end()) return &found->second;
    std::vector<RuntimeHvaTransform> transforms;
    if (!parseRuntimeHvaFrameTransformsFromBytes(source.hvaBytes, source.metadata.hvaPath,
            transforms, error, frame)) return nullptr;
    return &source.transformsByFrame.emplace(frame, std::move(transforms)).first->second;
}
