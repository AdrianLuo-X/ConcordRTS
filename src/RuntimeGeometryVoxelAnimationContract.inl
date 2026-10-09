// Frozen pre-memo path: compare actual animation results, including states
// that intentionally never ask for a source, rather than mirroring the memo.
static int runtimeVoxelAnimationFrameReference(const WorldObjectInstance& object,
    const std::string& imageId, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache) {
    const UnitType* unit = findObjectUnitTypeForRender(context, object);
    const bool airborneModel = runtimeObjectLooksAircraftForRender(object, context);
    if (airborneModel && !object.airborne) return 0;
    const bool moving = runtimeObjectShouldUseMovingSequence(object);
    if (!airborneModel && !moving && (unit == nullptr || unit->idleRate <= 0)) return 0;
    const auto& source = loadRuntimeVoxelSourceFromCache(imageId, context, cache, false);
    if (!source.loaded || source.metadata.hvaFrameCount <= 1) return 0;
    const int delay = !moving && unit != nullptr && unit->idleRate > 0
        ? unit->idleRate : unit == nullptr ? 1 : unit->walkRate;
    return runtimeUnitAnimationStep(object, context, delay) % source.metadata.hvaFrameCount;
}

bool runRuntimeGeometryVoxelAnimationContractCheck(std::string& error) {
    const auto fail = [&](const char* message) {
        error = std::string("voxel animation batch: ") + message; return false;
    };
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString("[VehicleTypes]\n0=__HVA_WALK__\n1=__HVA_IDLE__\n"
            "[AircraftTypes]\n0=__HVA_AIR__\n1=__HVA_AIR_IDLE__\n"
            "[__HVA_WALK__]\nWalkRate=3\nIdleRate=0\n"
            "[__HVA_IDLE__]\nWalkRate=2\nIdleRate=5\n"
            "[__HVA_AIR__]\nWalkRate=4\nIdleRate=0\n"
            "[__HVA_AIR_IDLE__]\nWalkRate=2\nIdleRate=5\n")
        || !rules.loadFromIni(ini)) return fail("fixture rules did not load");
    GameWorld world;
    RuntimeMapRenderCache cache, otherCache;
    RuntimeMapCamera camera;
    const RECT viewport{0, 0, 1280, 720};
    RuntimeMapRenderContext context;
    context.rules = &rules; context.stableRenderConfiguration = true;
    context.assetRootPath = "__hva_contract__/root";
    std::vector<SpecialMechanismRenderSnapshot> snapshots(1);
    snapshots[0].kind = "chrono"; snapshots[0].phase = "WarpIn";
    snapshots[0].objectId = 7; snapshots[0].progress = 19;
    context.specialMechanismSnapshots = &snapshots;
    auto differentRoot = context; differentRoot.assetRootPath += "_other";
    const std::array<std::string, 4> images{{"__hva_one__", "__hva_many__",
        "__hva_missing__", "__hva_zero__"}};
    const auto seed = [&](RuntimeMapRenderCache& target,
        const RuntimeMapRenderContext& sourceContext, int count) {
        for (std::size_t index = 0; index < images.size(); ++index) {
            const auto& image = images[index];
            const std::string key = normalizedLowerPath(sourceContext.assetRootPath)
                + "|vxl=" + toLowerAscii(fileNameForVxlImageId(image))
                + "|hva=" + toLowerAscii(fileNameForHvaImageId(image));
            auto& source = target.voxelSourcesByImage[key];
            source.loadAttempted = true;
            source.loaded = index != 2;
            source.metadata.hvaFrameCount = index == 0 ? 1 : index == 1 ? count : 0;
        }
    };
    seed(cache, context, 7); seed(cache, differentRoot, 11); seed(otherCache, context, 13);
    std::vector<WorldObjectInstance> objects(18);
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto& object = objects[index];
        object.instanceId = static_cast<int>(index); object.kind = WorldObjectKind::Unit;
        object.type = index % 4 == 0 ? "__HVA_WALK__" : index % 4 == 1 ? "__HVA_IDLE__"
            : index % 4 == 2 ? "__HVA_AIR__" : "__HVA_AIR_IDLE__";
        object.airborne = index % 4 >= 2 && index % 3 != 0;
        object.hasRenderPosition = index % 5 != 0; object.renderMoveActive = index % 2 == 0;
        object.alive = index != 17;
        object.mission = index == 6 ? "chronoFrozen" : index == 10 ? "temporal"
            : index == 12 ? "parasiteparalyzed" : index == 14 ? "parasitestunned" : "Move";
    }
    bool observedMovingFrame = false, observedIdleFrame = false, observedAirborneFrame = false;
    bool checkedGroundedMoving = false, checkedGroundedIdleRate = false, observedGroundVehicleMove = false;
    for (int tick = 0; tick < 120; ++tick) {
        context.animationTick = tick; context.fixedTickHz = tick % 2 ? 30 : 60;
        differentRoot.animationTick = tick; differentRoot.fixedTickHz = context.fixedTickHz;
        std::vector<int> expected;
        for (const auto& object : objects)
            for (const auto& image : images)
                expected.push_back(runtimeVoxelAnimationFrameReference(object, image, context, cache));
        RuntimeGeometryQueryScope batch(world, viewport, context, camera, cache);
        std::size_t resultIndex = 0;
        for (const auto& object : objects) {
            for (const auto& image : images) {
                const int actual = runtimeVoxelAnimationFrame(object, image, context, cache);
                if (actual != expected[resultIndex++]) return fail("moving/idle/frozen tick result differs");
                const bool aircraft = object.type == "__HVA_AIR__" || object.type == "__HVA_AIR_IDLE__";
                if (aircraft && !object.airborne) {
                    if (actual != 0) return fail("grounded aircraft ignored the current flight state");
                    checkedGroundedMoving = checkedGroundedMoving || runtimeObjectShouldUseMovingSequence(object);
                    checkedGroundedIdleRate = checkedGroundedIdleRate || object.type == "__HVA_AIR_IDLE__";
                }
                if (aircraft && object.airborne && actual != 0) observedAirborneFrame = true;
                if (actual != 0 && runtimeObjectShouldUseMovingSequence(object)) observedMovingFrame = true;
                if (actual != 0 && runtimeObjectShouldUseMovingSequence(object)
                    && object.type == "__HVA_WALK__") observedGroundVehicleMove = true;
                if (actual != 0 && !runtimeObjectShouldUseMovingSequence(object)
                    && object.type == "__HVA_IDLE__") observedIdleFrame = true;
                auto copied = context; copied.chaosBodyTint = true;
                copied.fixedTickHz = 15; ++copied.animationTick;
                for (const auto* alternate : {&copied, &differentRoot}) {
                    if (runtimeVoxelAnimationFrame(object, image, *alternate, cache)
                        != runtimeVoxelAnimationFrameReference(object, image, *alternate, cache))
                        return fail("changed context clock/root result differs");
                }
                if (runtimeVoxelAnimationFrame(object, image, context, otherCache)
                    != runtimeVoxelAnimationFrameReference(object, image, context, otherCache))
                    return fail("different source cache result differs");
            }
        }
        if (!runtimeMovementReferenceForRender()
            && RuntimeGeometryQueryFrame::active->voxelAnimationFrameCounts.size() != images.size())
            return fail("source metadata was not shared by image within the batch");
    }
    if (!observedMovingFrame || !observedGroundVehicleMove || !observedIdleFrame || !observedAirborneFrame
        || !checkedGroundedMoving || !checkedGroundedIdleRate)
        return fail("moving/idle/airborne/grounded fixtures were not exercised");
    // Mutating/reloading a source between scopes must affect the next batch.
    seed(cache, context, 17);
    {
        RuntimeGeometryQueryScope batch(world, viewport, context, camera, cache);
        if (runtimeGeometryVoxelAnimationFrameCount(images[1], context, cache) != 17)
            return fail("source metadata leaked from the previous batch");
    }
    return true;
}
