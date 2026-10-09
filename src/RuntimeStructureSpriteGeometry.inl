namespace {

bool drawRuntimeStructureGeometryBody(HDC dc, const RECT& viewport,
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RECT* spriteRect = nullptr,
    std::vector<RuntimeSpriteHitSpan>* foregroundSpans = nullptr) {
    if (object.kind != WorldObjectKind::Structure || runtimeInvisibleMapBuilding(context.rules, object)) return false;
    const auto* building = context.rules ? context.rules->findBuildingType(object.type) : nullptr;
    if (building && toLowerAscii(building->image) == "null") return false;
    RuntimeBuildingDepthObjectScope buildingDepthObject(object, center, context);
    const auto footprint = runtimeObjectFoundationDimensions(object, context);
    const bool bib = tryDrawRuntimeStructureBib(dc, viewport, object, center, context, camera, cache);
    const bool buildup = tryDrawRuntimeStructureBuildup(dc, viewport, object, center, context, camera, cache);
    RuntimeMapRenderStats stats;
    const bool overlay = !buildup && tryDrawRuntimeStructureOverlaySprite(
        dc, viewport, object, center, context, camera, cache, stats);
    const bool base = !buildup && !overlay && tryDrawObjectShpSprite(
        dc, viewport, object, center, context, camera, cache, nullptr, spriteRect);
    RuntimeStructureAnimationStats animationStats;
    int layers = drawRuntimeStructureActiveAnimations(dc, viewport, object, center,
        context, camera, cache, &animationStats);
    // The world paints an occupied tank bunker's front wall after its tank.
    // It belongs to the same selectable structure even in a body-only query.
    if (building && building->bunker && object.tankBunkerOccupantId >= 0) {
        auto* capture = runtimeSpriteGeometryObservation
            ? &runtimeSpriteGeometryObservation->geometry : runtimeSpriteGeometryCapture;
        auto* priorSpans = capture ? capture->hitSpans : nullptr;
        if (capture && foregroundSpans) capture->hitSpans = foregroundSpans;
        layers += drawRuntimeStructureActiveAnimations(dc, viewport, object, center,
            context, camera, cache, nullptr, false, nullptr, nullptr, 1, true);
        if (capture) capture->hitSpans = priorSpans;
    }
    if (object.factoryExitAirborne && foregroundSpans) {
        auto* capture = runtimeSpriteGeometryObservation
            ? &runtimeSpriteGeometryObservation->geometry : runtimeSpriteGeometryCapture;
        auto* priorSpans = capture ? capture->hitSpans : nullptr;
        if (capture) capture->hitSpans = foregroundSpans;
        (void)drawRuntimeFactoryAirExitForeground(dc, viewport, object, center, context, camera, cache);
        if (capture) capture->hitSpans = priorSpans;
    }
    const bool turret = tryDrawRuntimeStructureTurretAnim(dc, viewport, object, center,
        footprint.width, footprint.height, context, camera, cache);
    return bib || buildup || overlay || base || layers > 0 || turret;
}

} // namespace

static void runtimeNormalizeStructureGeometry(RuntimeStructureSpriteGeometry& geometry) {
    geometry.hitSpans.insert(geometry.hitSpans.end(), geometry.foregroundHitSpans.begin(), geometry.foregroundHitSpans.end());
    const auto normalize = [](std::vector<RuntimeSpriteHitSpan>& spans) {
        std::sort(spans.begin(), spans.end(), [](const auto& left, const auto& right) {
            if (left.y != right.y) return left.y < right.y;
            if (left.left != right.left) return left.left < right.left;
            return left.right < right.right;
        });
        std::size_t written = 0;
        for (const auto span : spans) {
            if (written && spans[written - 1].y == span.y && spans[written - 1].right >= span.left)
                spans[written - 1].right = std::max(spans[written - 1].right, span.right);
            else spans[written++] = span;
        }
        spans.resize(written);
    };
    normalize(geometry.hitSpans);
    normalize(geometry.foregroundHitSpans);
}

bool resolveRuntimeStructureSpriteGeometry(const WorldObjectInstance& object,
    const POINT& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    RuntimeStructureSpriteGeometry& geometry) {
    geometry = {};
    // Interaction keeps its footprint record for every object, but an explicitly
    // viewport-limited query need not copy and sort sprite spans that the world
    // compositor itself culls. Unscoped and full-geometry callers retain the
    // original unlimited bounds, including atlas and placement queries.
    const auto* batch = RuntimeGeometryQueryFrame::active;
    if (object.kind == WorldObjectKind::Structure && batch
        && batch->viewportStructureGeometryOnly && batch->matchesMetadata(context)
        && &batch->cache == &cache && context.stableRenderConfiguration
        && batch->identity.stableRenderConfiguration
        && batch->camera.worldOffsetX == camera.worldOffsetX
        && batch->camera.worldOffsetY == camera.worldOffsetY
        && batch->camera.tileWidth == camera.tileWidth
        && batch->camera.tileHeight == camera.tileHeight
        && batch->camera.initialized == camera.initialized) {
        const int margin = runtimeObjectCullMargin(object,
            runtimeObjectFoundationDimensions(object, context));
        const auto& viewport = batch->viewport;
        if (static_cast<long long>(center.x) < static_cast<long long>(viewport.left) - margin
            || static_cast<long long>(center.x) >= static_cast<long long>(viewport.right) + margin
            || static_cast<long long>(center.y) < static_cast<long long>(viewport.top) - margin
            || static_cast<long long>(center.y) >= static_cast<long long>(viewport.bottom) + margin)
            return false;
    }
    bool memoResolved = false;
    if (findRuntimeGeometryStructure(object, center, context, camera, cache, geometry, memoResolved))
        return memoResolved;
    RuntimeSpriteGeometryCapture capture;
    capture.hitSpans = &geometry.hitSpans;
    RuntimeSpriteGeometryScope scope(&capture);
    const RECT viewport{LONG_MIN / 2, LONG_MIN / 2, LONG_MAX / 2, LONG_MAX / 2};
    const bool resolved = drawRuntimeStructureGeometryBody(nullptr, viewport,
        object, center, context, camera, cache, nullptr, &geometry.foregroundHitSpans);
    geometry.bounds = capture.bounds;
    geometry.layerCount = capture.layerCount;
    runtimeNormalizeStructureGeometry(geometry);
    rememberRuntimeGeometryStructure(object, center, context, camera, cache, geometry, resolved && capture.valid);
    return resolved && capture.valid;
}

bool runtimeSpriteHitSpansContain(const std::vector<RuntimeSpriteHitSpan>& spans, POINT point) {
    auto row = std::lower_bound(spans.begin(), spans.end(), point.y,
        [](const RuntimeSpriteHitSpan& span, LONG y) { return span.y < y; });
    for (; row != spans.end() && row->y == point.y && row->left <= point.x; ++row)
        if (point.x < row->right) return true;
    return false;
}

namespace {
void collectRuntimeMobileBuildingDepthGeometry(const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeBuildingDepthFrameState& frame) {
    // These diagnostics are confined to the existing explicit PROFILE mode.
    const bool mobileProfile = frame.profileEnabled;
    RuntimeBuildingDepthFrameState::CollectionProfile collectionBefore;
    if (mobileProfile) collectionBefore = frame.profileCollection;
    const auto rectanglesStarted = runtimeMaybeTimingStart(mobileProfile);
    double depthEligibilityMs = 0.0, contextMs = 0.0, objectScopeMs = 0.0;
    double drawMs = 0.0, drawVxlMs = 0.0, drawShpMs = 0.0, drawOtherMs = 0.0;
    double coverageMs = 0.0;
    std::size_t drawCalls = 0, vxlDrawCalls = 0, shpDrawCalls = 0, otherDrawCalls = 0;
    std::size_t acceptedMobiles = 0, rejectedMobiles = 0, pinnedFrames = 0;
    std::size_t emittedVxlFrames = 0, emittedShpFrames = 0;
    int voxelHits = 0, voxelMisses = 0;
    // Snapshot the building rectangles before appending mobile draws. Units
    // outside these bounds retain their ordinary paint and selection paths.
    std::vector<RECT> buildingRects;
    for (const auto& draw : frame.draws) {
        if (draw.opacity <= 0 || !draw.image || draw.description.width <= 0
            || draw.description.height <= 0) continue;
        RECT clipped{};
        if (IntersectRect(&clipped, &draw.rect, &viewport)) buildingRects.push_back(clipped);
    }
    if (buildingRects.empty()) return;
    const double rectanglesMs = runtimeMaybeElapsedMs(mobileProfile, rectanglesStarted);
    const auto repairMemoStarted = runtimeMaybeTimingStart(mobileProfile);
    RuntimeGroundVehicleRepairRecordsMemo repairRecordsMemo(world, context, !runtimeRepairRecordsMemoReference());
    const double repairMemoMs = runtimeMaybeElapsedMs(mobileProfile, repairMemoStarted);
    const auto overlapsBuilding = [&](const RECT& rect) {
        return std::any_of(buildingRects.begin(), buildingRects.end(),
            [&](const RECT& building) { return rectIntersects(rect, building); });
    };

    std::vector<RuntimeWorldObjectDrawRecord> mobileUnits;
    const auto candidateStarted = runtimeMaybeTimingStart(mobileProfile);
    for (const auto& object : world.getObjects()) {
        if ((object.kind != WorldObjectKind::Unit && object.kind != WorldObjectKind::Infantry)
            || !runtimeWorldObjectBodyShouldRender(object)
            || runtimeSpecialObjectIsHidden(context, object)
            || !runtimeVisionObjectBodyShouldRender(context, object)
            || runtimeSpyDisguiseSnapshot(context, object.instanceId)
            || runtimeCamouflageSnapshot(context, object, nullptr, true)) continue;
        const auto footprint = runtimeObjectFoundationDimensions(object, context);
        RuntimeWorldObjectDrawRecord record;
        record.object = &object;
        record.center = runtimeObjectBodyAnchorForRender(object, viewport, context, camera);
        record.cullMargin = runtimeObjectCullMargin(object, footprint);
        const RECT candidateRect{record.center.x - record.cullMargin,
            record.center.y - record.cullMargin, record.center.x + record.cullMargin,
            record.center.y + record.cullMargin};
        if (!overlapsBuilding(candidateRect)) continue;
        const auto depthEligibilityStarted = runtimeMaybeTimingStart(mobileProfile);
        if (!runtimeNavalUnitUsesBuildingDepth(object, context)) {
            const bool eligible = object.kind == WorldObjectKind::Infantry
                ? runtimeGroundInfantryUsesBuildingDepth(object, world, context, cache)
                : runtimeGroundVehicleUsesBuildingDepthImpl(object, world, context, cache, &repairRecordsMemo);
            if (!eligible) {
                if (mobileProfile) depthEligibilityMs += runtimeMaybeElapsedMs(true, depthEligibilityStarted);
                continue;
            }
            frame.groundVehicleIds.insert(object.instanceId);
        }
        if (mobileProfile) depthEligibilityMs += runtimeMaybeElapsedMs(true, depthEligibilityStarted);
        record.drawLevel = drawLevelForObject(object);
        record.sortPoint = runtimeObjectSortPoint(object, footprint, camera);
        record.cellSum = objectRenderX(object) + objectRenderY(object);
        record.kindBias = objectKindSortBias(object.kind);
        record.instanceId = object.instanceId;
        record.footprintWidth = footprint.width;
        record.footprintHeight = footprint.height;
        mobileUnits.push_back(record);
    }
    const double candidateLookupMs = runtimeMaybeElapsedMs(mobileProfile, candidateStarted);
    const auto sortStarted = runtimeMaybeTimingStart(mobileProfile);
    std::sort(mobileUnits.begin(), mobileUnits.end(), [](const auto& left, const auto& right) {
        return runtimeWorldObjectRenderOrderLess(left.sortPoint, left.drawLevel,
            left.cellSum, left.kindBias, left.instanceId, right.sortPoint,
            right.drawLevel, right.cellSum, right.kindBias, right.instanceId);
    });
    const double sortMs = runtimeMaybeElapsedMs(mobileProfile, sortStarted);
    for (const auto& record : mobileUnits) {
        const auto contextStarted = runtimeMaybeTimingStart(mobileProfile);
        auto objectContext = context;
        if (runtimeObjectHasAirstrikeTint(*record.object, objectContext))
            objectContext.airstrikeBodyColorAdd = runtimeAirstrikeColorAdd(objectContext);
        const auto firstDraw = frame.draws.size();
        if (mobileProfile) contextMs += runtimeMaybeElapsedMs(true, contextStarted);
        const auto objectScopeStarted = runtimeMaybeTimingStart(mobileProfile);
        RuntimeBuildingDepthObjectScope objectScope(*record.object, record.center, objectContext);
        if (mobileProfile) objectScopeMs += runtimeMaybeElapsedMs(true, objectScopeStarted);
        const int voxelHitsBefore = mobileProfile ? cache.voxelCacheHits : 0;
        const int voxelMissesBefore = mobileProfile ? cache.voxelCacheMisses : 0;
        const auto drawStarted = runtimeMaybeTimingStart(mobileProfile);
        const bool drawn = drawRuntimeMobileGeometryBody(nullptr, viewport, *record.object,
            record.center, objectContext, camera, cache);
        if (mobileProfile) {
            const double elapsed = runtimeMaybeElapsedMs(true, drawStarted);
            drawMs += elapsed;
            ++drawCalls;
            // The existing scope has already evaluated the same original Unit
            // voxel-art predicate used by the body dispatcher. No new query.
            if (!runtimeBuildingDepthObject) { drawOtherMs += elapsed; ++otherDrawCalls; }
            else if (objectScope.state.mobileVoxel) { drawVxlMs += elapsed; ++vxlDrawCalls; }
            else { drawShpMs += elapsed; ++shpDrawCalls; }
            voxelHits += cache.voxelCacheHits - voxelHitsBefore;
            voxelMisses += cache.voxelCacheMisses - voxelMissesBefore;
            for (std::size_t index = firstDraw; index < frame.draws.size(); ++index) {
                const auto& collected = frame.draws[index];
                if (collected.pinnedPixels) ++pinnedFrames;
                if (collected.description.layer == RuntimeBuildingDepthLayer::NavalVoxel) ++emittedVxlFrames;
                else if (collected.description.layer == RuntimeBuildingDepthLayer::NavalSprite
                    || collected.description.layer == RuntimeBuildingDepthLayer::InfantrySprite) ++emittedShpFrames;
            }
        }
        const auto coverageStarted = runtimeMaybeTimingStart(mobileProfile);
        bool overlappingBody = false;
        int nativeBottom = std::numeric_limits<int>::lowest();
        for (std::size_t index = firstDraw; index < frame.draws.size(); ++index) {
            const auto& draw = frame.draws[index];
            if (draw.description.layer == RuntimeBuildingDepthLayer::NavalVoxel) {
                const int incomingBottom = draw.description.nativeFrameTopY
                    + draw.description.navalBottom;
                // Retail copies the first part's bounds, then expands the
                // inclusive union only when a later part extends its bottom.
                if (nativeBottom == std::numeric_limits<int>::lowest()) nativeBottom = incomingBottom;
                else if (incomingBottom > nativeBottom) nativeBottom = incomingBottom + 1;
            }
            if (overlappingBody || draw.opacity <= 0 || !draw.image
                || draw.description.width <= 0 || draw.description.height <= 0
                || !overlapsBuilding(draw.rect)) continue;
            // A successful lookup can still contain an entirely transparent
            // frame. Keep fallback selection unless real body pixels exist.
            // VXL cache eviction must not invalidate this coverage check.
            const auto& pixels = draw.pinnedPixels ? *draw.pinnedPixels : draw.image->pixels;
            const auto pixelCount = static_cast<std::size_t>(draw.description.width)
                * draw.description.height;
            overlappingBody = pixels.size() >= pixelCount
                && std::any_of(pixels.begin(), pixels.begin() + pixelCount,
                    [](unsigned char pixel) { return pixel != 0; });
        }
        if (!drawn || !overlappingBody) {
            frame.draws.resize(firstDraw);
            frame.participatingIds.erase(record.instanceId);
            frame.groundVehicleIds.erase(record.instanceId);
            if (mobileProfile) {
                coverageMs += runtimeMaybeElapsedMs(true, coverageStarted);
                ++rejectedMobiles;
            }
            continue;
        }
        frame.participatingIds.insert(record.instanceId);
        // Retail composes the VXL body, turret and barrel into one offscreen
        // rectangle before its scene-Z test. Share that un-clipped bottom;
        // independently normalized VXL pixel depths are not scene depths.
        for (std::size_t index = firstDraw; index < frame.draws.size(); ++index) {
            auto& description = frame.draws[index].description;
            if (description.layer == RuntimeBuildingDepthLayer::NavalVoxel)
                description.navalBottom = nativeBottom - description.nativeFrameTopY;
        }
        if (mobileProfile) {
            coverageMs += runtimeMaybeElapsedMs(true, coverageStarted);
            ++acceptedMobiles;
        }
    }
    if (mobileProfile) {
        std::cout << "Dense building mobile collect profile: mode="
            << (frame.composeColors ? "paint" : "geometry-query")
            << " tick=" << context.animationTick
            << " worldObjects=" << world.getObjects().size()
            << " buildingRects=" << buildingRects.size()
            << " candidateMobiles=" << mobileUnits.size()
            << " acceptedMobiles=" << acceptedMobiles << " rejectedMobiles=" << rejectedMobiles
            << " rectanglesMs=" << rectanglesMs << " repairMemoMs=" << repairMemoMs
            << " candidateLookupMs=" << candidateLookupMs
            << " depthEligibilityMs=" << depthEligibilityMs << " sortMs=" << sortMs
            << " contextMs=" << contextMs << " objectScopeMs=" << objectScopeMs
            << " drawMs=" << drawMs << " drawVxlMs=" << drawVxlMs
            << " drawShpMs=" << drawShpMs << " drawOtherMs=" << drawOtherMs
            << " coverageMs=" << coverageMs
            << " drawCalls=" << drawCalls << " vxlDrawCalls=" << vxlDrawCalls
            << " shpDrawCalls=" << shpDrawCalls << " otherDrawCalls=" << otherDrawCalls
            << " voxelHits=" << voxelHits << " voxelMisses=" << voxelMisses
            << " pinnedFrames=" << pinnedFrames
            << " emittedVxlFrames=" << emittedVxlFrames << " emittedShpFrames=" << emittedShpFrames
            << " sharedSnapshotMs=" << frame.profileCollection.sharedSnapshotMs - collectionBefore.sharedSnapshotMs
            << " fallbackSnapshotMs=" << frame.profileCollection.fallbackSnapshotMs - collectionBefore.fallbackSnapshotMs
            << " collectPinMs=" << frame.profileCollection.collectPinMs - collectionBefore.collectPinMs
            << " colourCallbackMs=" << frame.profileCollection.colourCallbackMs - collectionBefore.colourCallbackMs
            << " fullSnapshots=" << frame.profileCollection.fullSnapshots - collectionBefore.fullSnapshots
            << " colourCallbacks=" << frame.profileCollection.colourCallbacks - collectionBefore.colourCallbacks
            << " snapshotPixelsBytes=" << frame.profileCollection.rawPixelMaterialBytes[0] - collectionBefore.rawPixelMaterialBytes[0]
            << " snapshotRawColorIndicesBytes=" << frame.profileCollection.rawPixelMaterialBytes[1] - collectionBefore.rawPixelMaterialBytes[1]
            << " snapshotOwnerRemapMaskBytes=" << frame.profileCollection.rawPixelMaterialBytes[2] - collectionBefore.rawPixelMaterialBytes[2]
            << " snapshotLightLevelsBytes=" << frame.profileCollection.rawPixelMaterialBytes[3] - collectionBefore.rawPixelMaterialBytes[3]
            << " snapshotNormalIndicesBytes=" << frame.profileCollection.rawPixelMaterialBytes[4] - collectionBefore.rawPixelMaterialBytes[4]
            << " snapshotNormalBrightnessLevelsBytes=" << frame.profileCollection.rawPixelMaterialBytes[5] - collectionBefore.rawPixelMaterialBytes[5]
            << " snapshotVplSectionsBytes=" << frame.profileCollection.rawPixelMaterialBytes[6] - collectionBefore.rawPixelMaterialBytes[6]
            << " snapshotDepthLevelsBytes=" << frame.profileCollection.rawPixelMaterialBytes[7] - collectionBefore.rawPixelMaterialBytes[7]
            << " snapshotPixelMaterialsBytes=" << frame.profileCollection.rawPixelMaterialBytes[8] - collectionBefore.rawPixelMaterialBytes[8]
            << " vxlPoseMs=" << frame.profileCollection.vxlPoseMs - collectionBefore.vxlPoseMs
            << " vxlBodyTintMs=" << frame.profileCollection.vxlBodyTintMs - collectionBefore.vxlBodyTintMs
            << " vxlWreckLookupMs=" << frame.profileCollection.vxlWreckLookupMs - collectionBefore.vxlWreckLookupMs
            << " vxlBodyPoseMs=" << frame.profileCollection.vxlBodyPoseMs - collectionBefore.vxlBodyPoseMs
            << " vxlCacheLoadMs=" << frame.profileCollection.vxlCacheLoadMs - collectionBefore.vxlCacheLoadMs
            << " vxlBodyMs=" << frame.profileCollection.vxlBodyMs - collectionBefore.vxlBodyMs
            << " vxlPartsMs=" << frame.profileCollection.vxlPartsMs - collectionBefore.vxlPartsMs
            << " fullSnapshotDimensions=";
        bool printedDimension = false;
        for (const auto& [dimensions, count] : frame.profileCollection.fullSnapshotDimensions) {
            const auto previous = collectionBefore.fullSnapshotDimensions.find(dimensions);
            const auto delta = count - (previous == collectionBefore.fullSnapshotDimensions.end() ? 0 : previous->second);
            if (!delta) continue;
            if (printedDimension) std::cout << ',';
            std::cout << dimensions.first << 'x' << dimensions.second << ':' << delta;
            printedDimension = true;
        }
        if (!printedDimension) std::cout << "none";
        std::cout << " candidateIncludesDepthEligibility=yes drawIncludesVoxelSetupAndSnapshots=yes"
            << " colourCallbackIncludesFallbackSnapshot=yes vxlPartsIncludesPartCacheLoad=yes"
            << " rawByteCountsExcludePaletteOpaqueShadowCaches=yes trimNotSeparatelyTimed=yes" << '\n';
    }
}

void replayRuntimeBuildingDepthGeometry(const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeBuildingDepthFrameState& frame) {
    const char* profileSetting = runtimeRendererEnvironmentValue("CONCORD_DENSE_BUILDING_PROFILE");
    frame.profileEnabled = profileSetting && profileSetting[0] == '1';
    const bool timingEnabled = frame.profileEnabled || frame.stageTimingEnabled;
    const auto replayStarted = runtimeMaybeTimingStart(timingEnabled);
    frame.geometryOnly = frame.collectOnly = true;
    drawRuntimeTerrainWallDepthSprites(world, viewport, context, camera, cache, frame);
    std::vector<RuntimeWorldObjectDrawRecord> buildings;
    for (const auto& object : world.getObjects()) {
        if (object.kind != WorldObjectKind::Structure || !runtimeWorldObjectBodyShouldRender(object)
            || runtimeObjectIsGroundRubble(object, context)
            || runtimeSpecialObjectIsHidden(context, object)
            || !runtimeVisionObjectBodyShouldRender(context, object)
            || runtimeBuildingCollapseForObject(context.buildingCollapses, object.instanceId,
                context.animationTick, context.fixedTickHz)) continue;
        const auto footprint = runtimeObjectFoundationDimensions(object, context);
        RuntimeWorldObjectDrawRecord record;
        record.object = &object;
        record.center = screenPoint(runtimeObjectDrawPoint(object, footprint, camera), viewport, camera);
        record.cullMargin = runtimeObjectCullMargin(object, footprint);
        if (record.center.x < viewport.left - record.cullMargin
            || record.center.x >= viewport.right + record.cullMargin
            || record.center.y < viewport.top - record.cullMargin
            || record.center.y >= viewport.bottom + record.cullMargin) continue;
        record.drawLevel = drawLevelForObject(object);
        record.sortPoint = runtimeObjectSortPoint(object, footprint, camera);
        record.cellSum = objectRenderX(object) + objectRenderY(object);
        record.kindBias = objectKindSortBias(object.kind);
        record.instanceId = object.instanceId;
        record.footprintWidth = footprint.width; record.footprintHeight = footprint.height;
        buildings.push_back(record);
    }
    // Start with the same object order before constructing the ground queue;
    // ties in the queue consequently retain the compositor's submission order.
    std::sort(buildings.begin(), buildings.end(), [](const auto& left, const auto& right) {
        return runtimeWorldObjectRenderOrderLess(left.sortPoint, left.drawLevel,
            left.cellSum, left.kindBias, left.instanceId, right.sortPoint,
            right.drawLevel, right.cellSum, right.kindBias, right.instanceId);
    });
    RuntimeSpriteGeometryCapture capture;
    RuntimeSpriteGeometryScope geometryScope(&capture);
    const auto bodies = buildRuntimeGroundBodyDrawRecords(buildings, {}, 0, camera, nullptr, &context);
    const double gatherMs = runtimeMaybeElapsedMs(timingEnabled, replayStarted);
    const auto bodyStarted = runtimeMaybeTimingStart(timingEnabled);
    for (const auto& body : bodies) {
        if (body.gateOverlayObjectRecord) {
            const auto& record = *body.gateOverlayObjectRecord;
            RuntimeBuildingDepthObjectScope objectScope(*record.object, record.center, context);
            (void)drawRuntimeGateSpecialZOverlay(nullptr, viewport, record, context, camera, cache);
        } else if (body.objectRecord) {
            const auto& record = *body.objectRecord;
            frame.participatingIds.insert(record.instanceId);
            frame.occludingStructureIds.insert(record.instanceId);
            // Invisible buildings intentionally retain an empty entry. Missing
            // resources keep the existing geometric/fallback selection instead.
            if (runtimeInvisibleMapBuilding(context.rules, *record.object)) continue;
            const auto* type = context.rules ? context.rules->findBuildingType(record.object->type) : nullptr;
            if (type && toLowerAscii(type->image) == "null") continue;
            auto objectContext=context;
            if(runtimeObjectHasAirstrikeTint(*record.object,objectContext))
                objectContext.airstrikeBodyColorAdd=runtimeAirstrikeColorAdd(objectContext);
            RuntimeStructureSpriteGeometry observed;
            RuntimeSpriteGeometryObservation observation;
            const bool observe = runtimeGeometryPaintRecordingEnabled(*record.object, objectContext, camera, cache);
            observation.geometry.hitSpans = &observed.hitSpans;
            RuntimeSpriteGeometryObservationScope observationScope(observe ? &observation : nullptr);
            const auto drawCount = frame.draws.size();
            const bool resolved = drawRuntimeStructureGeometryBody(nullptr, viewport, *record.object,
                record.center, objectContext, camera, cache, nullptr, &observed.foregroundHitSpans);
            observationScope.finish();
            if (observe && observation.supported) {
                observed.bounds = observation.geometry.bounds;
                observed.layerCount = observation.geometry.layerCount;
                runtimeNormalizeStructureGeometry(observed);
                rememberRuntimeGeometryStructure(*record.object, record.center, objectContext, camera,
                    cache, observed, resolved && observation.geometry.valid);
            }
            if (!resolved || frame.draws.size() == drawCount)
                frame.participatingIds.erase(record.instanceId);
        }
    }
    const double bodyCollectMs = runtimeMaybeElapsedMs(timingEnabled, bodyStarted);
    const auto mobileStarted = runtimeMaybeTimingStart(timingEnabled);
    collectRuntimeMobileBuildingDepthGeometry(world, viewport, context, camera, cache, frame);
    const double mobileCollectMs = runtimeMaybeElapsedMs(timingEnabled, mobileStarted);
    resolveRuntimeBuildingDepthDraws(frame);
    cache.lastBuildingDepthRequestedPixels = static_cast<std::uint64_t>(viewport.right - viewport.left)
        * static_cast<std::uint64_t>(viewport.bottom - viewport.top);
    cache.lastBuildingDepthBufferPixels = frame.pixels.size();
    cache.lastBuildingDepthBufferCapacityBytes = frame.pixels.capacity() * sizeof(RuntimeBuildingDepthPixel);
    cache.lastBuildingDepthTimingEnabled = timingEnabled;
    cache.lastBuildingDepthTemplateMs = frame.profileDepthTemplateMs;
    cache.lastBuildingDepthInitializeMs = frame.profileDepthInitializeMs;
    cache.lastBuildingDepthGatherMs = gatherMs;
    cache.lastBuildingDepthBodyCollectMs = bodyCollectMs;
    cache.lastBuildingDepthMobileCollectMs = mobileCollectMs;
    cache.lastBuildingDepthSortMs = frame.profileDepthSortMs;
    cache.lastBuildingDepthRasterMs = frame.profileDepthRasterMs;
    cache.lastBuildingDepthBlendMs = frame.profileBlendMs;
    cache.lastBuildingDepthTotalMs = runtimeMaybeElapsedMs(timingEnabled, replayStarted)
        + frame.profileDepthConstructorMs;
    if (frame.profileEnabled) {
        const double totalMs = cache.lastBuildingDepthTotalMs;
        std::size_t clippedRectanglePixels = 0, translucentDraws = 0;
        for (const auto& draw : frame.draws) {
            RECT clipped{};
            if (IntersectRect(&clipped, &draw.rect, &viewport))
                clippedRectanglePixels += static_cast<std::size_t>(clipped.right - clipped.left)
                    * static_cast<std::size_t>(clipped.bottom - clipped.top);
            if (draw.opacity > 0 && draw.opacity < 255) ++translucentDraws;
        }
        // Rectangle pixels are the candidate work area, including transparent
        // SHP pixels. Counting them needs no extra per-pixel pass.
        std::cout << "Dense building depth profile: mode="
            << (frame.composeColors ? "paint" : "geometry-query")
            << " rasterReference=" << (frame.referenceMode || runtimeBuildingDepthRasterReference())
            << " tick=" << context.animationTick
            << " worldObjects=" << world.getObjects().size()
            << " buildings=" << buildings.size()
            << " draws=" << frame.draws.size()
            << " participants=" << frame.participatingIds.size()
            << " viewportPixels=" << frame.pixels.size()
            << " clippedRectanglePixels=" << clippedRectanglePixels
            << " translucentDraws=" << translucentDraws
            << " blendPixels=" << frame.blends.size()
            << " frozenUnderlayPixels=" << frame.frozenUnderlays.size()
            << " gatherMs=" << gatherMs
            << " bodyCollectMs=" << bodyCollectMs
            << " mobileCollectMs=" << mobileCollectMs
            << " templateMs=" << frame.profileDepthTemplateMs
            << " initializeMs=" << frame.profileDepthInitializeMs
            << " depthSortMs=" << frame.profileDepthSortMs
            << " depthRasterMs=" << frame.profileDepthRasterMs
            << " blendMs=" << frame.profileBlendMs
            << " terrainTileMemoHits=" << cache.geometryTerrainTileMemoHits
            << " terrainTileMemoMisses=" << cache.geometryTerrainTileMemoMisses
            << " totalMs=" << totalMs << '\n';
    }
}
} // namespace

namespace {
bool extractRuntimeBuildingVisibleHitSpans(const RuntimeBuildingDepthFrameState& frame,
    RuntimeBuildingVisibleHitSpans& visibleSpans, int animationTick,
    std::size_t spanLimit = std::numeric_limits<std::size_t>::max()) {
    visibleSpans.clear();
    const auto& viewport = frame.viewport;
    const auto scanStarted = runtimeMaybeTimingStart(frame.profileEnabled);
    for (const int id : frame.participatingIds) visibleSpans.try_emplace(id);
    const int height = std::max(0L, viewport.bottom - viewport.top);
    // Only collected draw rectangles can change an initially unowned pixel.
    // Keep one conservative interval per row so every emitted owner's spans
    // retain their original coordinates and left-to-right order. All original
    // participants retain entries even when fully transparent or occluded.
    // A diagnostic object may reuse the -1 background sentinel; preserve its
    // original full-viewport result instead of assuming it is a normal owner.
    const bool restrictScan = !frame.referenceMode && !frame.participatingIds.contains(-1);
    std::vector<std::pair<int, int>> rowRanges;
    if (restrictScan) {
        rowRanges.assign(static_cast<std::size_t>(height), {frame.width, 0});
        for (const auto& draw : frame.draws) {
            RECT clipped{};
            if (!IntersectRect(&clipped, &draw.rect, &viewport)) continue;
            const int left = clipped.left - viewport.left;
            const int right = clipped.right - viewport.left;
            for (int y = clipped.top - viewport.top; y < clipped.bottom - viewport.top; ++y) {
                auto& range = rowRanges[static_cast<std::size_t>(y)];
                range.first = std::min(range.first, left);
                range.second = std::max(range.second, right);
            }
        }
    }
    std::size_t scannedPixels = 0, spanCount = 0;
    for (int y = 0; y < height; ++y) {
        int x = restrictScan ? rowRanges[static_cast<std::size_t>(y)].first : 0;
        const int endX = restrictScan ? rowRanges[static_cast<std::size_t>(y)].second : frame.width;
        if (x >= endX) continue;
        if (frame.profileEnabled) scannedPixels += static_cast<std::size_t>(endX - x);
        const auto* row = frame.pixels.data() + static_cast<std::size_t>(y) * frame.width;
        while (x < endX) {
            const int id = row[x].visibleOwner;
            const int first = x++;
            while (x < endX && row[x].visibleOwner == id) ++x;
            const auto entry = visibleSpans.find(id);
            if (entry != visibleSpans.end()) {
                if (spanCount == spanLimit) { visibleSpans.clear(); return false; }
                entry->second.push_back({viewport.top + y, viewport.left + first, viewport.left + x});
                ++spanCount;
            }
        }
    }
    if (frame.profileEnabled) {
        const double scanMs = runtimeMaybeElapsedMs(true, scanStarted);
        std::cout << "Dense building hit span profile: tick=" << animationTick
            << " reference=" << (frame.referenceMode ? "yes" : "no")
            << " viewportPixels=" << frame.pixels.size()
            << " scannedPixels=" << scannedPixels
            << " scanMs=" << scanMs << '\n';
    }
    return true;
}

void publishRuntimePaintBuildingVisibleHitSpans(RuntimeGeometryQueryFrame& memo,
    const RuntimeBuildingDepthFrameState& frame, int animationTick, RuntimeMapRenderCache& cache) {
    // The public query remains unlimited. This derived paint result is bounded
    // so unusual maps fall back to the established replay without retaining a
    // second viewport-sized ownership representation.
    constexpr std::size_t maximumParticipants = 32768, maximumSpans = 262144;
    memo.buildingVisibleHitSpansReady = false;
    memo.buildingVisibleHitSpans.clear();
    if (!frame.resolved || frame.referenceMode || frame.participatingIds.size() > maximumParticipants)
        return;
    try {
        if (extractRuntimeBuildingVisibleHitSpans(frame, memo.buildingVisibleHitSpans,
                animationTick, maximumSpans)) {
            memo.buildingVisibleHitSpansReady = true;
            ++cache.geometryBuildingDepthPaintRecords;
        }
    } catch (const std::bad_alloc&) {
        memo.buildingVisibleHitSpans.clear();
        // Derived capture is optional; leave the original query available.
    }
}
} // namespace

bool resolveRuntimeBuildingVisibleHitSpans(const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>>& visibleSpans) {
    visibleSpans.clear();
    if (findRuntimePaintBuildingVisibleHitSpans(world, viewport, context, camera, cache, visibleSpans))
        return true;
    if (runtimeSkipEmptyBuildingDepthScene(world) && !runtimeWorldHasTerrainDepthWalls(world, context)) {
        // Preserve the public success/missing-template return contract without
        // allocating a viewport-sized depth array for an empty participant set.
        return viewport.right > viewport.left && viewport.bottom > viewport.top
            && prepareRuntimeBuildingDepthTemplate(context, cache);
    }
    RuntimeBuildingDepthFrameScope frameScope(viewport, context, cache, true);
    if (!frameScope.ready) return false;
    replayRuntimeBuildingDepthGeometry(world, viewport, context, camera, cache, frameScope.state);
    return extractRuntimeBuildingVisibleHitSpans(frameScope.state, visibleSpans, context.animationTick);
}

RuntimeObjectWorldRenderPlacement runtimeObjectWorldRenderPlacement(
    const WorldObjectInstance& object, const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    RuntimeObjectWorldRenderPlacement result;
    result.center = runtimeObjectBodyAnchorForRender(object, viewport, context, camera);
    const auto sortPoint = runtimeObjectSortPoint(object, runtimeObjectFoundationDimensions(object, context), camera);
    result.sortPoint = {sortPoint.x, sortPoint.y};
    result.bodyViewport = viewport;
    result.airborneForeground = object.airborne
        && runtimeObjectLooksAircraftForRender(object, context);
    if (!result.airborneForeground || object.mission == "MissileFlight" || object.mission == "MissilePreparing") return result;
    RuntimeWorldObjectDrawRecord aircraft;
    aircraft.object = &object;
    aircraft.center = result.center;
    aircraft.aircraft = aircraft.airborneForeground = true;
    auto* batch = RuntimeGeometryQueryFrame::active;
    if (batch && &batch->world == &world && &batch->cache == &cache
        && batch->matchesMetadata(context) && EqualRect(&batch->viewport, &viewport)
        && batch->camera.worldOffsetX == camera.worldOffsetX
        && batch->camera.worldOffsetY == camera.worldOffsetY
        && batch->camera.tileWidth == camera.tileWidth && batch->camera.tileHeight == camera.tileHeight
        && batch->identity.vision == context.vision
        && batch->identity.visionObserverOwner == context.visionObserverOwner
        && batch->identity.seeHiddenObjects == context.seeHiddenObjects) {
        if (!batch->structuresPrepared) {
            // Query placement has its own eligibility and world order. The
            // paint list additionally culls to the viewport and includes some
            // collapse actors, so it cannot substitute for this list.
            for (const auto& structure : world.getObjects()) {
                if (structure.kind != WorldObjectKind::Structure || !runtimeWorldObjectBodyShouldRender(structure)
                    || runtimeSpecialObjectIsHidden(context, structure)) continue;
                const auto footprint = runtimeObjectFoundationDimensions(structure, context);
                RuntimeWorldObjectDrawRecord record;
                record.object = &structure;
                record.footprintWidth = footprint.width; record.footprintHeight = footprint.height;
                record.center = screenPoint(runtimeObjectDrawPoint(structure, footprint, camera), viewport, camera);
                batch->structures.push_back(record);
            }
            batch->structureIndex = buildRuntimeStructureRenderSpatialIndex(batch->structures);
            batch->structuresPrepared = true;
        }
        std::vector<std::size_t> candidates;
        const auto subCell = worldObjectSubCellOffset(object);
        batch->structureIndex.query(objectRenderX(object) + subCell.x,
            objectRenderY(object) + subCell.y, candidates);
        result.center.y -= runtimeAircraftBuildingClearanceLiftPixels(aircraft,
            batch->structures, context, camera, cache, &candidates);
        // Factory clipping only considers the producing host. Keep its exact
        // overlap test and original first matching record order.
        if (object.factoryExitHostId >= 0) {
            std::vector<RuntimeWorldObjectDrawRecord> hosts;
            for (const auto candidate : candidates) {
                const auto& record = batch->structures[candidate];
                if (record.object->instanceId == object.factoryExitHostId) hosts.push_back(record);
            }
            result.bodyViewport = runtimeFactoryAirExitBodyViewport(aircraft, hosts,
                context, camera, cache, viewport);
            for (const auto& host : hosts)
                if (runtimeAircraftOverStructureFoundation(aircraft, host))
                    result.factoryForegroundHostId = host.object->instanceId;
        }
        return result;
    }
    std::vector<RuntimeWorldObjectDrawRecord> structures;
    for (const auto& structure : world.getObjects()) {
        if (structure.kind != WorldObjectKind::Structure || !runtimeWorldObjectBodyShouldRender(structure)
            || runtimeSpecialObjectIsHidden(context, structure)) continue;
        const auto footprint = runtimeObjectFoundationDimensions(structure, context);
        RuntimeWorldObjectDrawRecord record;
        record.object = &structure;
        record.footprintWidth = footprint.width; record.footprintHeight = footprint.height;
        record.center = screenPoint(runtimeObjectDrawPoint(structure, footprint, camera), viewport, camera);
        // The production renderer excludes records when body and shadow are
        // both outside their culling margin. An overlapping foundation is a
        // much tighter relevant set for placement queries.
        if (runtimeAircraftOverStructureFoundation(aircraft, record)) structures.push_back(record);
    }
    result.center.y -= runtimeAircraftBuildingClearanceLiftPixels(aircraft, structures, context, camera, cache);
    result.bodyViewport = runtimeFactoryAirExitBodyViewport(aircraft, structures, context, camera, cache, viewport);
    for (const auto& host : structures)
        if (host.object->instanceId == object.factoryExitHostId)
            result.factoryForegroundHostId = host.object->instanceId;
    return result;
}
