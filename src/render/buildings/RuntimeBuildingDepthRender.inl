#include "render/buildings/RuntimeBuildingDepthRowKernel.h"

using concord::render::RuntimeBuildingDepthDraw;
using concord::render::RuntimeBuildingDepthBlendPixel;

struct RuntimeBuildingDepthFrameState {
    RECT viewport{};
    RECT requestedViewport{};
    int width = 0;
    bool compactOwnershipBuffer = false;
    bool stageTimingEnabled = false;
    bool geometryOnly = false;
    bool collectOnly = false;
    bool resolved = false;
    bool composeColors = false;
    bool opaqueMarkerCovers = true;
    bool referenceMode = false;
    const RuntimeShpFrameImage* zShape = nullptr;
    std::vector<RuntimeBuildingDepthPixel> pixels;
    std::vector<RuntimeBuildingDepthDraw> draws;
    std::unordered_set<int> participatingIds;
    std::unordered_set<int> groundVehicleIds;
    std::unordered_set<int> occludingStructureIds;
    std::unordered_map<std::size_t, RuntimeBuildingDepthBlendPixel> blends;
    std::unordered_set<int> frozenOwners;
    std::unordered_map<std::size_t, RuntimeBuildingDepthBlendPixel> frozenUnderlays;
    std::vector<std::size_t> nonBuildingPixels;
    std::size_t paintOrdinal = 0;
    std::unordered_map<int,std::size_t> ownerPaintOrdinals;
    // Optional stage diagnostics. No clocks are read inside the pixel loops.
    bool profileEnabled = false;
    double profileDepthSortMs = 0.0;
    double profileDepthConstructorMs = 0.0;
    double profileDepthTemplateMs = 0.0;
    double profileDepthInitializeMs = 0.0;
    double profileDepthRasterMs = 0.0;
    double profileBlendMs = 0.0;
    // Aggregate only the existing explicit PROFILE collection work.
    struct CollectionProfile {
        double sharedSnapshotMs = 0.0, fallbackSnapshotMs = 0.0;
        double collectPinMs = 0.0, colourCallbackMs = 0.0;
        double vxlPoseMs = 0.0, vxlCacheLoadMs = 0.0;
        double vxlBodyTintMs = 0.0, vxlWreckLookupMs = 0.0, vxlBodyPoseMs = 0.0;
        double vxlBodyMs = 0.0, vxlPartsMs = 0.0;
        std::size_t fullSnapshots = 0, colourCallbacks = 0;
        std::array<std::uint64_t, 9> rawPixelMaterialBytes{};
        std::map<std::pair<int, int>, std::size_t> fullSnapshotDimensions;
    } profileCollection;
};

using concord::render::RuntimeBuildingDepthObjectState;
using concord::render::RuntimeBuildingDepthPartState;

thread_local RuntimeBuildingDepthFrameState* runtimeBuildingDepthFrame = nullptr;
thread_local const RuntimeBuildingDepthObjectState* runtimeBuildingDepthObject = nullptr;
thread_local RuntimeBuildingDepthPartState runtimeBuildingDepthPart;

static thread_local std::optional<bool> runtimeBuildingDepthBufferReferenceOverride;
static bool runtimeBuildingDepthBufferReference() {
    if (runtimeBuildingDepthBufferReferenceOverride) return *runtimeBuildingDepthBufferReferenceOverride;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_BUILDING_DEPTH_BUFFER_REFERENCE");
    return value && value[0] == '1' && value[1] == '\0';
}
struct RuntimeBuildingDepthBufferReferenceScope {
    std::optional<bool> previous = runtimeBuildingDepthBufferReferenceOverride;
    explicit RuntimeBuildingDepthBufferReferenceScope(bool enabled) { runtimeBuildingDepthBufferReferenceOverride = enabled; }
    ~RuntimeBuildingDepthBufferReferenceScope() { runtimeBuildingDepthBufferReferenceOverride = previous; }
};
static thread_local std::optional<bool> runtimeBuildingDepthRasterReferenceOverride;
static bool runtimeBuildingDepthRasterReference() {
    if (runtimeBuildingDepthRasterReferenceOverride) return *runtimeBuildingDepthRasterReferenceOverride;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_BUILDING_DEPTH_RASTER_REFERENCE");
    return value && value[0] == '1' && value[1] == '\0';
}
struct RuntimeBuildingDepthRasterReferenceScope {
    std::optional<bool> previous = runtimeBuildingDepthRasterReferenceOverride;
    explicit RuntimeBuildingDepthRasterReferenceScope(bool enabled) { runtimeBuildingDepthRasterReferenceOverride = enabled; }
    ~RuntimeBuildingDepthRasterReferenceScope() { runtimeBuildingDepthRasterReferenceOverride = previous; }
};

static thread_local std::optional<bool> runtimeBuildingDepthPixelCopyReferenceOverride;
static bool runtimeBuildingDepthPixelCopyReference() {
    if (runtimeBuildingDepthPixelCopyReferenceOverride) return *runtimeBuildingDepthPixelCopyReferenceOverride;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_BUILDING_DEPTH_PIXEL_COPY_REFERENCE");
    return value && value[0] == '1' && value[1] == '\0';
}
struct RuntimeBuildingDepthPixelCopyReferenceScope {
    std::optional<bool> previous = runtimeBuildingDepthPixelCopyReferenceOverride;
    explicit RuntimeBuildingDepthPixelCopyReferenceScope(bool enabled) { runtimeBuildingDepthPixelCopyReferenceOverride = enabled; }
    ~RuntimeBuildingDepthPixelCopyReferenceScope() { runtimeBuildingDepthPixelCopyReferenceOverride = previous; }
};

bool runtimeNavalUnitUsesBuildingDepth(const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context) {
    if (object.kind != WorldObjectKind::Unit || object.airborne || object.onBridge
        || object.factoryExitHostId >= 0 || object.tankBunkerHostId >= 0
        || !context.rules) return false;
    const auto* type = context.rules->findUnitType("vehicle", object.type);
    // Other locomotors, bridges and hosted bodies have different Z fudge rules.
    return type && type->naval && toLowerAscii(type->locomotor)
        == "{2bea74e1-7cca-11d3-be14-00104b62a16c}";
}

bool prepareRuntimeBuildingDepthTemplate(const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache) {
    const std::string key = context.assetRootPath + "|"
        + cachedRuntimeMixExportsRootForContext(context, cache);
    return concord::render::ensureRuntimeBuildingDepthTemplate(
        {cache.buildingDepthTemplateKey, cache.buildingDepthTemplateAttempted, cache.buildingDepthTemplate},
        key, [&](RuntimeShpFrameImage& image) {
            std::vector<unsigned char> bytes;
            std::string source, error;
            bool loaded = loadRuntimeResourceBytesFromMixArchives(context.assetRootPath,
                "BUILDNGZ.SHA", cache, bytes, source, error);
            const auto& mixRoot = cachedRuntimeMixExportsRootForContext(context, cache);
            if (!loaded && !mixRoot.empty()) loaded = loadRuntimeResourceBytesFromMixArchives(
                mixRoot, "BUILDNGZ.SHA", cache, bytes, source, error);
            if (loaded) (void)decodeRuntimeShpFrameFromBytes(bytes, source, 0,
                image, error);
        });
}

struct RuntimeBuildingDepthFrameScope {
    RuntimeBuildingDepthFrameState state;
    RuntimeBuildingDepthFrameState* previous = runtimeBuildingDepthFrame;
    bool ready = false;
    bool ownsScratch = false;
    RuntimeBuildingDepthFrameScope(const RECT& viewport, const RuntimeMapRenderContext& context,
        RuntimeMapRenderCache& cache, bool geometryOnly = false) {
        state.viewport = viewport;
        state.requestedViewport = viewport;
        const char* profile = runtimeRendererEnvironmentValue("CONCORD_DENSE_BUILDING_PROFILE");
        state.stageTimingEnabled = context.coarseStageTimingEnabled || runtimeRenderDetailDiagnosticsEnabled(context)
            || (profile && profile[0] == '1');
        const auto constructorStarted = runtimeMaybeTimingStart(state.stageTimingEnabled);
        const auto extent = concord::render::runtimeBuildingDepthBufferExtent(
            runtimeRenderPlacementPixelRect(viewport));
        state.width = extent.width;
        const int height = extent.height;
        const auto templateStarted = runtimeMaybeTimingStart(state.stageTimingEnabled);
        ready = state.width > 0 && height > 0 && prepareRuntimeBuildingDepthTemplate(context, cache);
        state.profileDepthTemplateMs = runtimeMaybeElapsedMs(state.stageTimingEnabled, templateStarted);
        if (ready) {
            state.geometryOnly = geometryOnly;
            state.composeColors = !geometryOnly;
            const char* reference = runtimeRendererEnvironmentValue("CONCORD_DENSE_BUILDING_REFERENCE");
            state.referenceMode = reference && reference[0] == '1' && reference[1] == '\0';
            if(state.composeColors&&context.specialMechanismSnapshots)
                for(const auto& snapshot:*context.specialMechanismSnapshots)
                    if(snapshot.kind=="temporal")state.frozenOwners.insert(snapshot.targetObjectId);
            state.zShape = &cache.buildingDepthTemplate;
            state.compactOwnershipBuffer = previous == nullptr && !state.referenceMode
                && !runtimeBuildingDepthBufferReference();
            const auto initializeStarted = runtimeMaybeTimingStart(state.stageTimingEnabled);
            if (state.compactOwnershipBuffer) {
                concord::render::borrowRuntimeBuildingDepthScratch(state.pixels);
                ownsScratch = true;
            } else state.pixels.resize(static_cast<std::size_t>(state.width) * height);
            state.profileDepthInitializeMs = runtimeMaybeElapsedMs(state.stageTimingEnabled, initializeStarted);
            runtimeBuildingDepthFrame = &state;
        } else runtimeBuildingDepthFrame = nullptr;
        state.profileDepthConstructorMs = runtimeMaybeElapsedMs(state.stageTimingEnabled, constructorStarted);
    }
    ~RuntimeBuildingDepthFrameScope() {
        runtimeBuildingDepthFrame = previous;
        if (ownsScratch)
            concord::render::releaseRuntimeBuildingDepthScratch(state.pixels);
    }
};

struct RuntimeBuildingDepthObjectScope {
    RuntimeBuildingDepthObjectState state;
    const RuntimeBuildingDepthObjectState* previous = runtimeBuildingDepthObject;
    RuntimeBuildingDepthPartState previousPart = runtimeBuildingDepthPart;
    RuntimeBuildingDepthObjectScope(const WorldObjectInstance& object, const POINT& center,
        const RuntimeMapRenderContext& context) {
        runtimeBuildingDepthPart = {};
        const bool mobileUnit = runtimeNavalUnitUsesBuildingDepth(object, context)
            || (runtimeBuildingDepthFrame
                && runtimeBuildingDepthFrame->groundVehicleIds.contains(object.instanceId));
        state.mobileVoxel = mobileUnit && objectUsesVoxelArt(object, context);
        if (!runtimeBuildingDepthFrame || runtimeObjectIsGroundRubble(object, context)
            || (object.kind != WorldObjectKind::Structure && !mobileUnit)
            || (runtimeBuildingDepthFrame->resolved
                && !runtimeBuildingDepthFrame->participatingIds.contains(object.instanceId))) {
            runtimeBuildingDepthObject = nullptr;
            return;
        }
        state.instanceId = object.instanceId;
        state.center = runtimeRenderPlacementPixelPoint(center);
        const auto subCell = worldObjectSubCellOffset(object);
        const double level = drawLevelForObject(object);
        const auto projection = concord::render::projectRuntimeBuildingDepthOrigin(
            objectRenderX(object), subCell.x, objectRenderY(object), subCell.y, level);
        state.nativeY = projection.nativeY;
        state.heightPixel = projection.heightPixel;
        const auto footprint = runtimeObjectFoundationDimensions(object, context);
        state.foundationWidth = footprint.width;
        state.foundationHeight = footprint.height;
        state.animationYSort = projection.animationYSort;
        const auto* rules = !mobileUnit && context.rules ? context.rules->findBuildingType(object.type) : nullptr;
        state.ySort = concord::render::runtimeBuildingDepthDrawSort(projection.animationYSort,
            mobileUnit, rules && rules->turretAnimIsVoxel, rules && rules->gate);
        if (mobileUnit) runtimeBuildingDepthPart.layer = state.mobileVoxel
            ? RuntimeBuildingDepthLayer::NavalVoxel
            : object.kind == WorldObjectKind::Infantry ? RuntimeBuildingDepthLayer::InfantrySprite
            : RuntimeBuildingDepthLayer::NavalSprite;
        if (const auto* art = findArtTypeForObject(object, context)) {
            state.moveX = art->zShapePointMove[0];
            state.moveY = art->zShapePointMove[1];
            state.normalZAdjust = art->normalZAdjust;
        }
        runtimeBuildingDepthObject = &state;
    }
    ~RuntimeBuildingDepthObjectScope() {
        runtimeBuildingDepthObject = previous;
        runtimeBuildingDepthPart = previousPart;
    }
};

struct RuntimeBuildingDepthPartScope {
    RuntimeBuildingDepthPartState previous = runtimeBuildingDepthPart;
    explicit RuntimeBuildingDepthPartScope(RuntimeBuildingDepthLayer layer, int zAdjust = 0,
        int yDrawOffset = 0, double ySortAdjust = 0, bool independentSort = false) {
        runtimeBuildingDepthPart = {layer, zAdjust, yDrawOffset, ySortAdjust, independentSort};
    }
    ~RuntimeBuildingDepthPartScope() { runtimeBuildingDepthPart = previous; }
};

// Color samples use the ordinary palette/remap renderer with destination I/O
// confined to a temporary sprite-sized surface. Do not mutate outer captures.
struct RuntimeBuildingDepthColorSampleScope {
    decltype(runtimeBuildingDepthFrame) depth = runtimeBuildingDepthFrame;
    decltype(runtimeSpriteGeometryCapture) geometry = runtimeSpriteGeometryCapture;
    decltype(runtimeMapLightFrame) lights = runtimeMapLightFrame;
    decltype(runtimeBehindMarkerFrame) markers = runtimeBehindMarkerFrame;
    RuntimeBuildingDepthColorSampleScope() {
        runtimeBuildingDepthFrame = nullptr; runtimeSpriteGeometryCapture = nullptr;
        runtimeMapLightFrame = nullptr; runtimeBehindMarkerFrame = nullptr;
    }
    ~RuntimeBuildingDepthColorSampleScope() {
        runtimeBuildingDepthFrame = depth; runtimeSpriteGeometryCapture = geometry;
        runtimeMapLightFrame = lights; runtimeBehindMarkerFrame = markers;
    }
};

RuntimeBuildingDepthFrame runtimeBuildingDepthDescription(const RECT& imageRect,
    const RuntimeShpFrameImage& image) {
    return concord::render::describeRuntimeBuildingDepthReplay(imageRect.top, image,
        *runtimeBuildingDepthObject, runtimeBuildingDepthPart);
}

bool acceptRuntimeBuildingPixel(const RuntimeBuildingDepthFrame& frame,
    const RECT& imageRect, int x, int y, int opacity = 255) {
    auto& state = *runtimeBuildingDepthFrame;
    const RuntimeInputPixelPoint screen{imageRect.left + x, imageRect.top + y};
    if (!concord::render::acceptRuntimeBuildingDepthReplayPixel(frame,
            {{runtimeRenderPlacementPixelRect(state.viewport), state.width, state.pixels},
                state.zShape, state.resolved, state.referenceMode}, runtimeBuildingDepthObject, screen, x, y))
        return false;
    if (!state.geometryOnly && opacity >= 128 && runtimeMapLightFrame
        && runtimeMapLightFrame->drawingReceiver >= 0) {
        auto& lights = *runtimeMapLightFrame;
        concord::render::recordRuntimeBuildingDepthReplayReceiver(
            {runtimeRenderPlacementPixelRect(lights.bounds), lights.width, lights.drawingReceiver, lights.pixels.data()},
            screen);
    }
    return true;
}

void drawRuntimeResolvedBuildingOpaqueSpans(
    const RuntimeShpFrameImage& image, const RuntimeMapRenderContext& context,
    int targetX, int targetY, int startX, int endX, int startY, int endY,
    const std::uint32_t* palette, const unsigned char* remapMask, const std::uint32_t* remapPalette) {
    const auto& frame = *runtimeBuildingDepthFrame;
    auto* nativeLights = runtimeMapLightFrame;
    concord::render::RuntimeBuildingDepthReceiverBuffer lights{};
    const bool recordLights = nativeLights && nativeLights->drawingReceiver >= 0;
    if (recordLights) {
        lights = {runtimeRenderPlacementPixelRect(nativeLights->bounds), nativeLights->width,
            nativeLights->drawingReceiver, nativeLights->pixels.data()};
    }
    concord::render::drawRuntimeResolvedBuildingOpaqueSpans(image,
        {runtimeRenderPlacementPixelRect(frame.viewport), frame.width, frame.pixels},
        runtimeBuildingDepthObject->instanceId, {context.surfacePixels, context.surfaceWidth},
        recordLights ? &lights : nullptr, {targetX, targetY}, {startX, startY, endX, endY},
        palette, remapMask, remapPalette);
}

void recordRuntimeBuildingDepthBehindMarkers(const RuntimeBuildingDepthFrame& description,
    const RECT& imageRect, const RuntimeShpFrameImage& image, int opacity) {
    auto* markers = runtimeBehindMarkerFrame;
    if (!markers || opacity <= 0)
        return;
    const auto& frame = *runtimeBuildingDepthFrame;
    concord::render::recordRuntimeBehindMarkerDepthSprite(*markers,
        {runtimeRenderPlacementPixelRect(frame.viewport), runtimeRenderPlacementPixelRect(frame.requestedViewport),
            frame.width, frame.resolved, frame.opaqueMarkerCovers, frame.zShape, frame.pixels,
            frame.draws, frame.occludingStructureIds, frame.frozenOwners}, runtimeBuildingDepthObject, description,
        runtimeRenderPlacementPixelRect(imageRect), image, opacity);
}

static concord::render::RuntimeBuildingDepthRasterDraw runtimeBuildingDepthRasterDraw(
    const RuntimeBuildingDepthDraw& draw) {
    return {draw.description, draw.rect, draw.image,
        draw.pinnedPixels.get(), draw.instanceId, draw.opacity};
}

RuntimeBuildingDepthDraw& collectRuntimeBuildingDepthDraw(const RuntimeBuildingDepthFrame& description,
    const RECT& imageRect, const RuntimeShpFrameImage& image, int opacity,
    const std::shared_ptr<RuntimeShpFrameImage>& ownedImageSnapshot = {}) {
    auto& state = *runtimeBuildingDepthFrame;
    const auto& object = *runtimeBuildingDepthObject;
    const auto& part = runtimeBuildingDepthPart;
    const double ySort = part.independentSort
        ? object.animationYSort + part.ySortAdjust : object.ySort;
    RuntimeBuildingDepthDraw draw;
    draw.description = description; draw.image = &image; draw.rect = runtimeRenderPlacementPixelRect(imageRect);
    draw.instanceId = object.instanceId; draw.ySort = ySort; draw.sequence = state.draws.size();
    draw.opacity = std::clamp(opacity, 0, 255);
    // SHP frames occupy stable unordered_map nodes for the whole render cache.
    // VXL pose caches may evict an earlier pose during collection, so retain
    // its indexed coverage until this immediate replay completes. Color replay
    // already owns a complete, exact image copy; alias that copy's pixels instead
    // of allocating and copying the same bytes a second time. Both references
    // retain the snapshot, including for tilted/LRU, temporary and uncached poses.
    if (concord::render::runtimeBuildingDepthImageRequiresPin(image)) {
        const bool alias = state.composeColors && ownedImageSnapshot && !runtimeBuildingDepthPixelCopyReference();
        concord::render::pinRuntimeBuildingDepthDraw(draw, image, alias ? &ownedImageSnapshot : nullptr);
    }
    state.draws.push_back(std::move(draw));
    return state.draws.back();
}

void initializeRuntimeBuildingDepthPixelBuffer(RuntimeBuildingDepthFrameState& state) {
    if (!state.compactOwnershipBuffer) return;
    // Collection completes before the first owner lookup. Every non-background
    // pixel comes from this accepted stream; retaining its rectangle union also
    // covers transparent, animation, bib, foreground and mobile/naval layers.
    const auto bounds = concord::render::compactRuntimeBuildingDepthViewport(
        runtimeRenderPlacementPixelRect(state.requestedViewport), state.draws, state.participatingIds.contains(-1));
    const auto extent = concord::render::runtimeBuildingDepthBufferExtent(bounds);
    state.viewport = runtimeRenderPlacementNativeRect(bounds);
    state.width = extent.width;
    state.pixels.assign(static_cast<std::size_t>(extent.width) * extent.height, RuntimeBuildingDepthPixel{});
}

void resolveRuntimeBuildingDepthDraws(RuntimeBuildingDepthFrameState& state) {
    const bool timingEnabled = state.stageTimingEnabled || state.profileEnabled;
    const bool rasterReference = state.referenceMode || runtimeBuildingDepthRasterReference();
    const auto initializeStarted = runtimeMaybeTimingStart(timingEnabled);
    initializeRuntimeBuildingDepthPixelBuffer(state);
    state.profileDepthInitializeMs += runtimeMaybeElapsedMs(timingEnabled, initializeStarted);
    const auto sortStarted = runtimeMaybeTimingStart(timingEnabled);
    std::stable_sort(state.draws.begin(), state.draws.end(), [](const auto& left, const auto& right) {
        return concord::render::runtimeBuildingDepthDrawBefore(
            {left.description.layer == RuntimeBuildingDepthLayer::WallSprite, left.ySort, left.sequence},
            {right.description.layer == RuntimeBuildingDepthLayer::WallSprite, right.ySort, right.sequence});
    });
    state.profileDepthSortMs = runtimeMaybeElapsedMs(timingEnabled, sortStarted);
    const auto rasterStarted = runtimeMaybeTimingStart(timingEnabled);
    const auto flags = concord::render::runtimeBuildingDepthSceneFlags(
        state.draws, state.frozenOwners, state.occludingStructureIds, state.composeColors);
    state.opaqueMarkerCovers = flags.opaqueMarkerCovers;
    const bool hasAlpha = flags.hasAlpha;
    std::vector<int> lastOpaque(hasAlpha ? state.pixels.size() : 0, -1);
    concord::render::RuntimeBuildingDepthDrawChains chains;
    concord::render::RuntimeBuildingDepthAlphaRaster alpha{lastOpaque, chains};
    const concord::render::RuntimeBuildingDepthRasterTemplate zShape{state.zShape, rasterReference};
    const concord::render::RuntimeBuildingDepthRasterTarget target{
        runtimeRenderPlacementPixelRect(state.viewport), state.width, state.pixels};
    for (std::size_t drawIndex = 0; drawIndex < state.draws.size(); ++drawIndex) {
        concord::render::rasterRuntimeBuildingDepthDraw(
            runtimeBuildingDepthRasterDraw(state.draws[drawIndex]), target, zShape,
            state.frozenOwners, drawIndex, hasAlpha ? &alpha : nullptr);
    }
    state.profileDepthRasterMs = runtimeMaybeElapsedMs(timingEnabled, rasterStarted);
    const auto blendStarted = runtimeMaybeTimingStart(timingEnabled);
    // A translucent winner must retain foreign building colors underneath.
    // Compose only those sparse pixels, at the winner's existing paint slot;
    // opaque pixels and single-building internal layers keep their old path.
    concord::render::RuntimeBuildingDepthMaterialReplay materials([](const RuntimeBuildingDepthDraw& draw) {
        RuntimeBuildingDepthColorSampleScope isolated;
        return draw.colors();
    });
    materials.composeVisible({runtimeRenderPlacementPixelRect(state.viewport), state.width, state.draws},
        state.pixels, state.frozenOwners, chains, state.blends);
    if(!state.frozenOwners.empty()) {
        // A whole-building temporal blend needs the scene behind that building.
        // Re-evaluate the native accepted stream while excluding the final
        // frozen owner independently at each pixel; do not attenuate each part.
        std::vector<RuntimeBuildingDepthPixel> background(state.pixels.size());
        concord::render::RuntimeBuildingDepthDrawChains underlayChains;
        const concord::render::RuntimeBuildingDepthFrozenRaster underlay{
            {runtimeRenderPlacementPixelRect(state.viewport), state.width, background},
            state.pixels, underlayChains};
        for (std::size_t index = 0; index < state.draws.size(); ++index) {
            concord::render::rasterRuntimeBuildingDepthFrozenUnderlay(
                runtimeBuildingDepthRasterDraw(state.draws[index]), underlay, zShape, state.frozenOwners, index);
        }
        materials.composeFrozen({runtimeRenderPlacementPixelRect(state.viewport), state.width, state.draws},
            underlayChains, state.frozenUnderlays);
        if(!state.frozenUnderlays.empty())state.nonBuildingPixels.resize(state.pixels.size());
    }
    state.collectOnly = state.geometryOnly = false;
    state.resolved = true;
    state.profileBlendMs = runtimeMaybeElapsedMs(timingEnabled, blendStarted);
}

std::uint32_t blendRuntimeBuildingDepthLayers(const RuntimeBuildingDepthBlendPixel& blend,
    std::uint32_t destination, std::size_t afterOrdinal=0) {
    const auto& frame = *runtimeBuildingDepthFrame;
    return concord::render::blendRuntimeBuildingDepthLayers(
        blend, destination, frame.frozenOwners, frame.ownerPaintOrdinals, afterOrdinal);
}

void recordRuntimeBuildingUnderlayForeground(int screenX, int screenY) {
    auto* frame = runtimeBuildingDepthFrame;
    if (!frame)
        return;
    concord::render::recordRuntimeBuildingDepthForeground(frame->nonBuildingPixels,
        runtimeRenderPlacementPixelRect(frame->viewport), frame->width, {screenX, screenY}, frame->paintOrdinal);
}

void restoreRuntimeFrozenBuildingUnderlay(HDC dc,const RuntimeMapRenderContext& context,int owner) {
    if(runtimeGpuRestoreFrozenUnderlay(context,owner))return;
    auto* frame=runtimeBuildingDepthFrame;
    if(!frame||!frame->frozenOwners.contains(owner))return;
    for(const auto& [p,blend]:frame->frozenUnderlays) {
        if(frame->pixels[p].visibleOwner!=owner)continue;
        const auto afterOrdinal=frame->nonBuildingPixels[p];
        const int x=frame->viewport.left+static_cast<int>(p%frame->width);
        const int y=frame->viewport.top+static_cast<int>(p/frame->width);
        if(hasPixelSurface(context)&&x>=0&&y>=0&&x<context.surfaceWidth&&y<context.surfaceHeight) {
            auto& destination=context.surfacePixels[static_cast<std::size_t>(y)*context.surfaceWidth+x];
            destination=blendRuntimeBuildingDepthLayers(blend,destination,afterOrdinal);
        } else {
            const auto background=concord::ui::win32::MapNativeOverlayWin32::readPixel(dc,x,y);if(background==CLR_INVALID)continue;
            SetPixelV(dc,x,y,colorRefFromRuntimeDibColor(blendRuntimeBuildingDepthLayers(blend,packRuntimeDibColor(background),afterOrdinal)));
        }
    }
}

bool compositeRuntimeBuildingDepthPixel(const RECT& imageRect,int x,int y,std::uint32_t& destination) {
    auto& frame=*runtimeBuildingDepthFrame;
    if(frame.blends.empty())return false;
    const auto pixelIndex=static_cast<std::size_t>(imageRect.top+y-frame.viewport.top)*frame.width
        +imageRect.left+x-frame.viewport.left;
    return concord::render::compositeRuntimeBuildingDepthOnce(frame.blends, pixelIndex,
        {frame.frozenOwners, frame.ownerPaintOrdinals}, destination);
}

