#include "RuntimeBuildingDepth.h"
#include "RuntimeBuildingDepthRowKernel.h"

struct RuntimeBuildingDepthDraw {
    RuntimeBuildingDepthFrame description;
    const RuntimeShpFrameImage* image = nullptr;
    std::shared_ptr<std::vector<unsigned char>> pinnedPixels;
    RECT rect{};
    int instanceId = -1;
    double ySort = 0.0;
    std::size_t sequence = 0;
    int opacity = 255;
    std::function<std::vector<std::uint32_t>()> colors;
};

struct RuntimeBuildingDepthBlendPixel {
    struct Layer { std::uint32_t color; int opacity; int owner; };
    std::vector<Layer> layers;
    bool emitted = false;
};

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

struct RuntimeBuildingDepthObjectState {
    int instanceId = -1;
    POINT center{};
    int nativeY = 0, heightPixel = 0;
    int foundationWidth = 1, foundationHeight = 1;
    int moveX = 0, moveY = 0, normalZAdjust = 0;
    double ySort = 0.0, animationYSort = 0.0;
    bool mobileVoxel = false;
};

struct RuntimeBuildingDepthPartState {
    RuntimeBuildingDepthLayer layer = RuntimeBuildingDepthLayer::Body;
    int zAdjust = 0, yDrawOffset = 0;
    double ySortAdjust = 0;
    bool independentSort = false;
    int nativeOffsetY = 0;
};

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
// A scope lends this storage only on its own thread and returns it at exit.
// Every reused pixel is initialized before resolution; neither winners nor
// addresses survive into a later paint. Large surfaces retain no giant pool.
static thread_local std::vector<RuntimeBuildingDepthPixel> runtimeBuildingDepthPixelScratch;

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
    if (cache.buildingDepthTemplateKey != key) {
        cache.buildingDepthTemplateKey = key;
        cache.buildingDepthTemplateAttempted = false;
        cache.buildingDepthTemplate = {};
    }
    if (!cache.buildingDepthTemplateAttempted) {
        cache.buildingDepthTemplateAttempted = true;
        std::vector<unsigned char> bytes;
        std::string source, error;
        bool loaded = loadRuntimeResourceBytesFromMixArchives(context.assetRootPath,
            "BUILDNGZ.SHA", cache, bytes, source, error);
        const auto& mixRoot = cachedRuntimeMixExportsRootForContext(context, cache);
        if (!loaded && !mixRoot.empty()) loaded = loadRuntimeResourceBytesFromMixArchives(
            mixRoot, "BUILDNGZ.SHA", cache, bytes, source, error);
        if (loaded) (void)decodeRuntimeShpFrameFromBytes(bytes, source, 0,
            cache.buildingDepthTemplate, error);
    }
    const auto& image = cache.buildingDepthTemplate;
    return image.width > 0 && image.height > 0
        && image.pixels.size() >= static_cast<std::size_t>(image.width) * image.height;
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
        state.width = std::max(0L, viewport.right - viewport.left);
        const int height = std::max(0L, viewport.bottom - viewport.top);
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
                state.pixels.swap(runtimeBuildingDepthPixelScratch);
                ownsScratch = true;
            } else state.pixels.resize(static_cast<std::size_t>(state.width) * height);
            state.profileDepthInitializeMs = runtimeMaybeElapsedMs(state.stageTimingEnabled, initializeStarted);
            runtimeBuildingDepthFrame = &state;
        } else runtimeBuildingDepthFrame = nullptr;
        state.profileDepthConstructorMs = runtimeMaybeElapsedMs(state.stageTimingEnabled, constructorStarted);
    }
    ~RuntimeBuildingDepthFrameScope() {
        runtimeBuildingDepthFrame = previous;
        constexpr std::size_t maximumScratchBytes = 64U * 1024U * 1024U;
        if (ownsScratch && state.pixels.capacity() <= maximumScratchBytes / sizeof(RuntimeBuildingDepthPixel))
            state.pixels.swap(runtimeBuildingDepthPixelScratch);
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
        state.center = center;
        const auto subCell = worldObjectSubCellOffset(object);
        const double level = drawLevelForObject(object);
        state.nativeY = static_cast<int>(std::llround((objectRenderX(object) + subCell.x
            + objectRenderY(object) + subCell.y - level) * 15.0));
        state.heightPixel = static_cast<int>(std::llround(level * 15.0));
        const auto footprint = runtimeObjectFoundationDimensions(object, context);
        state.foundationWidth = footprint.width;
        state.foundationHeight = footprint.height;
        state.animationYSort = (objectRenderX(object) + subCell.x
            + objectRenderY(object) + subCell.y) * 256.0;
        // Retail BuildingClass::GetYSort uses its foundation origin; the
        // existing whole-object compositor's far-corner key is unrelated.
        state.ySort = state.animationYSort;
        // The retail unit uses cell-centre coordinates; the building sort
        // subtracts half a cell on each axis. This is a sort-only distinction.
        if (mobileUnit) state.ySort += 256.0;
        if (const auto* rules = !mobileUnit && context.rules ? context.rules->findBuildingType(object.type) : nullptr) {
            if (rules->turretAnimIsVoxel) state.ySort += 32;
            if (rules->gate) state.ySort -= 16;
        }
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
    const auto& object = *runtimeBuildingDepthObject;
    RuntimeBuildingDepthFrame frame;
    frame.canvasWidth = image.globalWidth > 0 ? image.globalWidth : image.width;
    frame.canvasHeight = image.globalHeight > 0 ? image.globalHeight : image.height;
    frame.width = image.width; frame.height = image.height;
    frame.offsetX = image.offsetX; frame.offsetY = image.offsetY;
    frame.foundationWidth = object.foundationWidth; frame.foundationHeight = object.foundationHeight;
    frame.moveX = object.moveX; frame.moveY = object.moveY;
    frame.normalZAdjust = object.normalZAdjust;
    frame.nativeFrameTopY = object.nativeY + imageRect.top - object.center.y
        + runtimeBuildingDepthPart.nativeOffsetY;
    frame.heightPixel = object.heightPixel;
    frame.layer = runtimeBuildingDepthPart.layer;
    frame.zAdjust = runtimeBuildingDepthPart.zAdjust;
    frame.yDrawOffset = runtimeBuildingDepthPart.yDrawOffset;
    if (object.mobileVoxel) {
        frame.layer = RuntimeBuildingDepthLayer::NavalVoxel;
        frame.navalBottom = image.sceneDepthBoundsValid ? image.sceneDepthBoundsBottom : image.height;
    }
    return frame;
}

bool acceptRuntimeBuildingPixel(const RuntimeBuildingDepthFrame& frame,
    const RECT& imageRect, int x, int y, int opacity = 255) {
    auto& state = *runtimeBuildingDepthFrame;
    const int screenX = imageRect.left + x, screenY = imageRect.top + y;
    if (screenX < state.viewport.left || screenX >= state.viewport.right
        || screenY < state.viewport.top || screenY >= state.viewport.bottom) return false;
    int depth = 0;
    // The resolved pass only reads the winner already computed for this
    // pixel. Its current part's depth cannot change that winner.
    if ((!state.resolved || state.referenceMode)
        && !runtimeBuildingRenderDepthAtPixel(frame, state.zShape->pixels, state.zShape->width,
            state.zShape->height, x, y, depth)) return false;
    auto& pixel = state.pixels[static_cast<std::size_t>(screenY - state.viewport.top)
        * state.width + screenX - state.viewport.left];
    if (state.resolved) {
        if (pixel.visibleOwner != runtimeBuildingDepthObject->instanceId) return false;
    } else if (!runtimeBuildingDepthAccept(pixel, runtimeBuildingDepthObject->instanceId,
        depth, frame.layer == RuntimeBuildingDepthLayer::Body
            || (frame.layer == RuntimeBuildingDepthLayer::Bib || frame.layer == RuntimeBuildingDepthLayer::WallSprite))) return false;
    if (!state.geometryOnly && opacity >= 128 && runtimeMapLightFrame
        && runtimeMapLightFrame->drawingReceiver >= 0) {
        auto& lights = *runtimeMapLightFrame;
        if (screenX >= lights.bounds.left && screenX < lights.bounds.right
            && screenY >= lights.bounds.top && screenY < lights.bounds.bottom)
            lights.pixels[static_cast<std::size_t>(screenY - lights.bounds.top)
                * lights.width + screenX - lights.bounds.left] = lights.drawingReceiver;
    }
    return true;
}

// Ordinary opaque building parts need only the resolved owner and a palette
// lookup. Preserve the accepted pixels' local-light receiver without repeating
// depth calculation, transparency checks, or generic color-effect branches.
template<bool MaskedRemap, bool RecordLights>
void drawRuntimeResolvedBuildingOpaqueSpansImpl(
    const RuntimeShpFrameImage& image, const RuntimeMapRenderContext& context,
    int targetX, int targetY, int startX, int endX, int startY, int endY,
    const std::uint32_t* palette, const unsigned char* remapMask,
    const std::uint32_t* remapPalette
) {
    const auto& frame = *runtimeBuildingDepthFrame;
    const int owner = runtimeBuildingDepthObject->instanceId;
    startX = std::max(startX, static_cast<int>(frame.viewport.left) - targetX);
    endX = std::min(endX, static_cast<int>(frame.viewport.right) - targetX);
    startY = std::max(startY, static_cast<int>(frame.viewport.top) - targetY);
    endY = std::min(endY, static_cast<int>(frame.viewport.bottom) - targetY);
    if (startX >= endX || startY >= endY) return;
    for (const auto& span : image.spriteOpaqueSpans) {
        if (span.y < startY) continue;
        if (span.y >= endY) break;
        const int left = std::max(startX, static_cast<int>(span.left));
        const int right = std::min(endX, static_cast<int>(span.right));
        if (left >= right) continue;
        const int screenY = targetY + span.y;
        const int screenLeft = targetX + left;
        const auto sourceOffset = static_cast<std::size_t>(span.y) * image.width + left;
        const unsigned char* source = image.pixels.data() + sourceOffset;
        const auto* owners = frame.pixels.data()
            + (static_cast<std::size_t>(screenY - frame.viewport.top) * frame.width
                + static_cast<std::size_t>(screenLeft - frame.viewport.left));
        auto* target = context.surfacePixels
            + static_cast<std::size_t>(screenY) * context.surfaceWidth + screenLeft;
        const unsigned char* mask = nullptr;
        if constexpr (MaskedRemap) mask = remapMask + sourceOffset;
        int* lightRow = nullptr;
        int lightLeft = 0, lightRight = 0, lightReceiver = -1;
        if constexpr (RecordLights) {
            auto& lights = *runtimeMapLightFrame;
            if (screenY >= lights.bounds.top && screenY < lights.bounds.bottom) {
                lightRow = lights.pixels.data()
                    + static_cast<std::size_t>(screenY - lights.bounds.top) * lights.width;
                lightLeft = lights.bounds.left;
                lightRight = lights.bounds.right;
                lightReceiver = lights.drawingReceiver;
            }
        }
        for (int index = 0; index < right - left; ++index) {
            if (owners[index].visibleOwner != owner) continue;
            if constexpr (MaskedRemap)
                target[index] = mask[index] ? remapPalette[source[index]] : palette[source[index]];
            else
                target[index] = palette[source[index]];
            if constexpr (RecordLights) {
                const int screenX = screenLeft + index;
                if (lightRow && screenX >= lightLeft && screenX < lightRight)
                    lightRow[screenX - lightLeft] = lightReceiver;
            }
        }
    }
}

void drawRuntimeResolvedBuildingOpaqueSpans(
    const RuntimeShpFrameImage& image, const RuntimeMapRenderContext& context,
    int targetX, int targetY, int startX, int endX, int startY, int endY,
    const std::uint32_t* palette, const unsigned char* remapMask,
    const std::uint32_t* remapPalette
) {
    const bool recordLights = runtimeMapLightFrame && runtimeMapLightFrame->drawingReceiver >= 0;
    if (remapMask) {
        if (recordLights)
            drawRuntimeResolvedBuildingOpaqueSpansImpl<true, true>(image, context,
                targetX, targetY, startX, endX, startY, endY, palette, remapMask, remapPalette);
        else
            drawRuntimeResolvedBuildingOpaqueSpansImpl<true, false>(image, context,
                targetX, targetY, startX, endX, startY, endY, palette, remapMask, remapPalette);
    } else {
        if (recordLights)
            drawRuntimeResolvedBuildingOpaqueSpansImpl<false, true>(image, context,
                targetX, targetY, startX, endX, startY, endY, palette, remapMask, remapPalette);
        else
            drawRuntimeResolvedBuildingOpaqueSpansImpl<false, false>(image, context,
                targetX, targetY, startX, endX, startY, endY, palette, remapMask, remapPalette);
    }
}

// Evaluate only the marker's small sample set. The accepted blend chain must
// contain an opaque-enough part of its final owner; a transparent or frozen
// silhouette alone does not justify the original behind-building hint.
static bool runtimeBuildingDepthOwnerCoversMarkerSample(
    const RuntimeBuildingDepthFrameState& frame, POINT point, int finalOwner) {
    if (!frame.occludingStructureIds.contains(finalOwner)
        || frame.frozenOwners.contains(finalOwner)) return false;
    if (frame.opaqueMarkerCovers) return true;
    RuntimeBuildingDepthPixel replay;
    bool covered = false;
    for (const auto& draw : frame.draws) {
        if (draw.opacity <= 0 || !PtInRect(&draw.rect, point)) continue;
        const int x = point.x - draw.rect.left, y = point.y - draw.rect.top;
        const auto& pixels = draw.pinnedPixels ? *draw.pinnedPixels : draw.image->pixels;
        if (!pixels[static_cast<std::size_t>(y) * draw.description.width + x]) continue;
        int depth = 0;
        if (!runtimeBuildingRenderDepthAtPixel(draw.description, frame.zShape->pixels,
            frame.zShape->width, frame.zShape->height, x, y, depth)) continue;
        const bool writeDepth = draw.description.layer == RuntimeBuildingDepthLayer::Body
            || draw.description.layer == RuntimeBuildingDepthLayer::Bib
            || draw.description.layer == RuntimeBuildingDepthLayer::WallSprite;
        if (!runtimeBuildingDepthAccept(replay, draw.instanceId, depth, writeDepth)) continue;
        if (draw.opacity == 255 && !frame.frozenOwners.contains(draw.instanceId)) covered = false;
        if (draw.instanceId == finalOwner && draw.opacity >= 192) covered = true;
    }
    return covered;
}
void recordRuntimeBuildingDepthBehindMarkers(const RuntimeBuildingDepthFrame& description,
    const RECT& imageRect, const RuntimeShpFrameImage& image, int opacity) {
    auto* markers = runtimeBehindMarkerFrame;
    if (!markers || opacity <= 0) return;
    const auto& frame = *runtimeBuildingDepthFrame;
    if (markers->drawingUnit) {
        // Participating mobiles still supply their own opaque sample points.
        // Resolve hiding from the same final owner as color and picking, so a
        // foreground structure need not happen to be painted after the mobile.
        recordRuntimeBehindMarkerShpFrame(imageRect, frame.requestedViewport, image, opacity);
        auto& unit = *markers->drawingUnit;
        if (frame.resolved && !unit.depthSamplesEvaluated) for (int index = 0; index < unit.sampleCount; ++index) {
            const auto point = unit.samples[index];
            if (!PtInRect(&frame.viewport, point)) continue;
            const auto& pixel = frame.pixels[static_cast<std::size_t>(point.y - frame.viewport.top)
                * frame.width + point.x - frame.viewport.left];
            if (runtimeBuildingDepthOwnerCoversMarkerSample(frame, point, pixel.visibleOwner)) unit.covered |= 1U << index;
        }
        if (frame.resolved && unit.sampleCount > 0) unit.depthSamplesEvaluated = true;
        return;
    }
    if (!markers->drawingStructure || opacity < 192) return;
    for (auto& unit : markers->candidates) for (int index = 0; index < unit.sampleCount; ++index) {
        const POINT point = unit.samples[index];
        if ((unit.covered & (1U << index)) || !PtInRect(&imageRect, point)
            || !PtInRect(&frame.viewport, point)) continue;
        const int x = point.x - imageRect.left, y = point.y - imageRect.top;
        if (!image.pixels[static_cast<std::size_t>(y) * image.width + x]) continue;
        int depth = 0;
        if (!runtimeBuildingRenderDepthAtPixel(description, frame.zShape->pixels,
            frame.zShape->width, frame.zShape->height, x, y, depth)) continue;
        auto previous = frame.pixels[static_cast<std::size_t>(point.y - frame.viewport.top)
            * frame.width + point.x - frame.viewport.left];
        if (frame.resolved ? previous.visibleOwner == runtimeBuildingDepthObject->instanceId
                && runtimeBuildingDepthOwnerCoversMarkerSample(frame, point, previous.visibleOwner)
            : runtimeBuildingDepthAccept(previous, runtimeBuildingDepthObject->instanceId, depth, false))
            unit.covered |= 1U << index;
    }
}

static bool runtimeBuildingDepthImageRequiresPin(const RuntimeShpFrameImage& image) {
    return !image.rasterCacheKey.empty() || !image.depthLevels.empty()
        || !image.rasterFunctionActuallyCalled.empty();
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
    draw.description = description; draw.image = &image; draw.rect = imageRect;
    draw.instanceId = object.instanceId; draw.ySort = ySort; draw.sequence = state.draws.size();
    draw.opacity = std::clamp(opacity, 0, 255);
    // SHP frames occupy stable unordered_map nodes for the whole render cache.
    // VXL pose caches may evict an earlier pose during collection, so retain
    // its indexed coverage until this immediate replay completes. Color replay
    // already owns a complete, exact image copy; alias that copy's pixels instead
    // of allocating and copying the same bytes a second time. Both references
    // retain the snapshot, including for tilted/LRU, temporary and uncached poses.
    if (runtimeBuildingDepthImageRequiresPin(image)) {
        if (state.composeColors && ownedImageSnapshot && !runtimeBuildingDepthPixelCopyReference()) {
            draw.image = ownedImageSnapshot.get();
            draw.pinnedPixels = std::shared_ptr<std::vector<unsigned char>>(
                ownedImageSnapshot, &ownedImageSnapshot->pixels);
        } else draw.pinnedPixels = std::make_shared<std::vector<unsigned char>>(image.pixels);
    }
    state.draws.push_back(std::move(draw));
    return state.draws.back();
}

void initializeRuntimeBuildingDepthPixelBuffer(RuntimeBuildingDepthFrameState& state) {
    if (!state.compactOwnershipBuffer) return;
    // Collection completes before the first owner lookup. Every non-background
    // pixel comes from this accepted stream; retaining its rectangle union also
    // covers transparent, animation, bib, foreground and mobile/naval layers.
    RECT bounds = state.requestedViewport;
    if (!state.participatingIds.contains(-1)) {
        bool any = false;
        for (const auto& draw : state.draws) {
            RECT clipped{};
            if (!IntersectRect(&clipped, &draw.rect, &state.requestedViewport)) continue;
            if (!any) { bounds = clipped; any = true; }
            else {
                bounds.left = std::min(bounds.left, clipped.left);
                bounds.top = std::min(bounds.top, clipped.top);
                bounds.right = std::max(bounds.right, clipped.right);
                bounds.bottom = std::max(bounds.bottom, clipped.bottom);
            }
        }
        if (!any) bounds = {state.requestedViewport.left, state.requestedViewport.top,
            state.requestedViewport.left, state.requestedViewport.top};
    }
    state.viewport = bounds;
    state.width = std::max(0L, bounds.right - bounds.left);
    const auto height = std::max(0L, bounds.bottom - bounds.top);
    state.pixels.assign(static_cast<std::size_t>(state.width) * height, RuntimeBuildingDepthPixel{});
}

void resolveRuntimeBuildingDepthDraws(RuntimeBuildingDepthFrameState& state) {
    const bool timingEnabled = state.stageTimingEnabled || state.profileEnabled;
    const bool rasterReference = state.referenceMode || runtimeBuildingDepthRasterReference();
    const auto initializeStarted = runtimeMaybeTimingStart(timingEnabled);
    initializeRuntimeBuildingDepthPixelBuffer(state);
    state.profileDepthInitializeMs += runtimeMaybeElapsedMs(timingEnabled, initializeStarted);
    const auto sortStarted = runtimeMaybeTimingStart(timingEnabled);
    std::stable_sort(state.draws.begin(), state.draws.end(), [](const auto& left, const auto& right) {
        const bool leftWall = left.description.layer == RuntimeBuildingDepthLayer::WallSprite;
        const bool rightWall = right.description.layer == RuntimeBuildingDepthLayer::WallSprite;
        if (leftWall != rightWall) return leftWall;
        if (left.ySort != right.ySort) return left.ySort < right.ySort;
        return left.sequence < right.sequence;
    });
    state.profileDepthSortMs = runtimeMaybeElapsedMs(timingEnabled, sortStarted);
    const auto rasterStarted = runtimeMaybeTimingStart(timingEnabled);
    state.opaqueMarkerCovers = state.frozenOwners.empty()
        && std::none_of(state.draws.begin(), state.draws.end(), [&](const auto& draw) {
            return draw.opacity < 255 && state.occludingStructureIds.contains(draw.instanceId);
        });
    const bool hasAlpha = state.composeColors && std::any_of(state.draws.begin(),state.draws.end(),
        [](const auto& draw) { return draw.opacity > 0 && draw.opacity < 255; });
    std::vector<int> lastOpaque(hasAlpha ? state.pixels.size() : 0, -1);
    std::unordered_map<std::size_t,std::vector<int>> chains;
    for (std::size_t drawIndex=0;drawIndex<state.draws.size();++drawIndex) {
        const auto& draw=state.draws[drawIndex];
        const auto& pixels = draw.pinnedPixels ? *draw.pinnedPixels : draw.image->pixels;
        const int width = draw.description.width, height = draw.description.height;
        const int firstY = std::max(0, static_cast<int>(state.viewport.top - draw.rect.top));
        const int lastY = std::min(height, static_cast<int>(state.viewport.bottom - draw.rect.top));
        const int firstX = std::max(0, static_cast<int>(state.viewport.left - draw.rect.left));
        const int lastX = std::min(width, static_cast<int>(state.viewport.right - draw.rect.left));
        std::optional<RuntimeBuildingDepthRowKernel> kernel;
        if (!rasterReference) kernel.emplace(draw.description, state.zShape->pixels,
            state.zShape->width, state.zShape->height);
        const bool writeDepth = draw.description.layer == RuntimeBuildingDepthLayer::Body
            || (draw.description.layer == RuntimeBuildingDepthLayer::Bib || draw.description.layer == RuntimeBuildingDepthLayer::WallSprite);
        const bool opaqueUnfrozen = draw.opacity == 255 && !state.frozenOwners.contains(draw.instanceId);
        const auto rasterRowRange = [&](int y, int rangeFirstX, int rangeLastX) {
            const auto row = kernel ? kernel->row(y) : RuntimeBuildingDepthRowKernel::Row{};
            const auto sourceRow = static_cast<std::size_t>(y) * width;
            const auto targetRow = static_cast<std::size_t>(draw.rect.top + y - state.viewport.top) * state.width
                + static_cast<std::size_t>(draw.rect.left + firstX - state.viewport.left);
            for (int x = rangeFirstX; x < rangeLastX; ++x) {
                if (!pixels[sourceRow + x]) continue;
                int depth = 0;
                if (rasterReference) {
                    if (!runtimeBuildingRenderDepthAtPixel(draw.description, state.zShape->pixels,
                        state.zShape->width, state.zShape->height, x, y, depth)) continue;
                } else depth = row.depthAtKnownX(x);
                const auto pixelIndex = targetRow + static_cast<std::size_t>(x - firstX);
                auto& pixel = state.pixels[pixelIndex];
                if (!runtimeBuildingDepthAccept(pixel, draw.instanceId, depth, writeDepth)) continue;
                if (hasAlpha) {
                    // A frozen object's completed body is translucent even when
                    // its individual SHP/VXL parts are opaque. Retain the scene
                    // underneath until those parts can be blended together once.
                    if (opaqueUnfrozen) {
                        lastOpaque[pixelIndex]=static_cast<int>(drawIndex);chains.erase(pixelIndex);
                    } else {
                        auto [entry,inserted]=chains.try_emplace(pixelIndex);
                        if(inserted&&lastOpaque[pixelIndex]>=0)entry->second.push_back(lastOpaque[pixelIndex]);
                        entry->second.push_back(static_cast<int>(drawIndex));
                    }
                }
            }
        };
        // Cached native SHP runs contain exactly the nonzero source pixels.
        // Keep pinned/VXL, frozen, reference and uncached images on the old scan.
        const bool cachedOpaqueSpans = !rasterReference && opaqueUnfrozen
            && !draw.pinnedPixels && !runtimeBuildingDepthImageRequiresPin(*draw.image)
            && draw.image->spriteOpaqueSpansValid
            && draw.image->width == width && draw.image->height == height
            && width > 0 && height > 0
            && pixels.size() >= static_cast<std::size_t>(width) * height;
        if (cachedOpaqueSpans) {
            for (const auto& span : draw.image->spriteOpaqueSpans) {
                if (span.y < firstY) continue;
                if (span.y >= lastY) break;
                const int left = std::max(firstX, static_cast<int>(span.left));
                const int right = std::min(lastX, static_cast<int>(span.right));
                if (left < right) rasterRowRange(span.y, left, right);
            }
        } else {
            for (int y = firstY; y < lastY; ++y) rasterRowRange(y, firstX, lastX);
        }
    }
    state.profileDepthRasterMs = runtimeMaybeElapsedMs(timingEnabled, rasterStarted);
    const auto blendStarted = runtimeMaybeTimingStart(timingEnabled);
    // A translucent winner must retain foreign building colors underneath.
    // Compose only those sparse pixels, at the winner's existing paint slot;
    // opaque pixels and single-building internal layers keep their old path.
    std::unordered_map<int,std::vector<std::uint32_t>> colors;
    for(const auto& [pixelIndex,chain]:chains) {
        const int owner=state.pixels[pixelIndex].visibleOwner;
        // The visible frozen object retains its existing whole-body temporal
        // scope. Its background is restored below; replaying this chain too
        // would apply the foreign frozen layers twice.
        if(state.frozenOwners.contains(owner))continue;
        if(!std::any_of(chain.begin(),chain.end(),[&](int index){return state.draws[index].instanceId!=owner;}))continue;
        auto& blend=state.blends[pixelIndex];
        const int screenX=state.viewport.left+static_cast<int>(pixelIndex%state.width);
        const int screenY=state.viewport.top+static_cast<int>(pixelIndex/state.width);
        for(const int index:chain) {
            const auto& draw=state.draws[index];
            auto [entry,inserted]=colors.try_emplace(index);
            if(inserted) {RuntimeBuildingDepthColorSampleScope isolated;entry->second=draw.colors();}
            const auto sourceIndex=static_cast<std::size_t>(screenY-draw.rect.top)*draw.description.width
                +screenX-draw.rect.left;
            blend.layers.push_back({entry->second[sourceIndex],draw.opacity,draw.instanceId});
        }
    }
    if(!state.frozenOwners.empty()) {
        // A whole-building temporal blend needs the scene behind that building.
        // Re-evaluate the native accepted stream while excluding the final
        // frozen owner independently at each pixel; do not attenuate each part.
        std::vector<RuntimeBuildingDepthPixel> background(state.pixels.size());
        std::unordered_map<std::size_t,std::vector<int>> underlayChains;
        for(std::size_t index=0;index<state.draws.size();++index) {
            const auto& draw=state.draws[index];
            const auto& pixels=draw.pinnedPixels?*draw.pinnedPixels:draw.image->pixels;
            std::optional<RuntimeBuildingDepthRowKernel> kernel;
            if (!rasterReference) kernel.emplace(draw.description, state.zShape->pixels,
                state.zShape->width, state.zShape->height);
            const bool writeDepth = draw.description.layer == RuntimeBuildingDepthLayer::Body
                || (draw.description.layer == RuntimeBuildingDepthLayer::Bib || draw.description.layer == RuntimeBuildingDepthLayer::WallSprite);
            for(int y=std::max(0,static_cast<int>(state.viewport.top-draw.rect.top));
                y<std::min(draw.description.height,static_cast<int>(state.viewport.bottom-draw.rect.top));++y) {
                const auto row = kernel ? kernel->row(y) : RuntimeBuildingDepthRowKernel::Row{};
                for(int x=std::max(0,static_cast<int>(state.viewport.left-draw.rect.left));
                    x<std::min(draw.description.width,static_cast<int>(state.viewport.right-draw.rect.left));++x) {
                    if(!pixels[static_cast<std::size_t>(y)*draw.description.width+x])continue;
                    const auto p=static_cast<std::size_t>(draw.rect.top+y-state.viewport.top)*state.width
                        +draw.rect.left+x-state.viewport.left;
                    const int owner=state.pixels[p].visibleOwner;
                    if(owner==draw.instanceId||!state.frozenOwners.contains(owner))continue;
                    int depth=0;
                    if (rasterReference) {
                        if(!runtimeBuildingRenderDepthAtPixel(draw.description,state.zShape->pixels,
                            state.zShape->width,state.zShape->height,x,y,depth))continue;
                    } else depth = row.depthAtKnownX(x);
                    if(!runtimeBuildingDepthAccept(background[p],draw.instanceId,depth,writeDepth))continue;
                    auto& chain=underlayChains[p];
                    if(draw.opacity==255&&!state.frozenOwners.contains(draw.instanceId))chain.clear();
                    chain.push_back(static_cast<int>(index));
                }
            }
        }
        for(const auto& [p,chain]:underlayChains) {
            auto& blend=state.frozenUnderlays[p];
            const int sx=state.viewport.left+static_cast<int>(p%state.width);
            const int sy=state.viewport.top+static_cast<int>(p/state.width);
            for(const int index:chain) {
                const auto& draw=state.draws[index];auto [entry,inserted]=colors.try_emplace(index);
                if(inserted){RuntimeBuildingDepthColorSampleScope isolated;entry->second=draw.colors();}
                blend.layers.push_back({entry->second[static_cast<std::size_t>(sy-draw.rect.top)
                    *draw.description.width+sx-draw.rect.left],draw.opacity,draw.instanceId});
            }
        }
        if(!state.frozenUnderlays.empty())state.nonBuildingPixels.resize(state.pixels.size());
    }
    state.collectOnly = state.geometryOnly = false;
    state.resolved = true;
    state.profileBlendMs = runtimeMaybeElapsedMs(timingEnabled, blendStarted);
}

std::uint32_t blendRuntimeBuildingDepthLayers(const RuntimeBuildingDepthBlendPixel& blend,std::uint32_t destination,
    std::size_t afterOrdinal=0) {
    const auto blendColor=[](std::uint32_t background,std::uint32_t foreground,int opacity) {
        std::uint32_t color=0;
        for(unsigned shift:{0U,8U,16U})color|=((((foreground>>shift)&255U)*opacity
            +((background>>shift)&255U)*(255-opacity)+127U)/255U)<<shift;
        return color;
    };
    int groupOwner=-1;
    std::uint32_t groupBackground=destination;
    const auto finishGroup=[&] {
        if(groupOwner>=0&&runtimeBuildingDepthFrame->frozenOwners.contains(groupOwner))
            destination=blendColor(groupBackground,destination,128);
    };
    for(const auto& layer:blend.layers) {
        if(afterOrdinal) {
            const auto found=runtimeBuildingDepthFrame->ownerPaintOrdinals.find(layer.owner);
            if(found!=runtimeBuildingDepthFrame->ownerPaintOrdinals.end()&&found->second<=afterOrdinal)continue;
        }
        if(layer.owner!=groupOwner) {
            finishGroup();
            groupOwner=layer.owner;
            groupBackground=destination;
        }
        destination=blendColor(destination,layer.color,layer.opacity);
    }
    finishGroup();
    return destination;
}

void recordRuntimeBuildingUnderlayForeground(int screenX,int screenY) {
    auto* frame=runtimeBuildingDepthFrame;
    if(!frame||frame->nonBuildingPixels.empty()||screenX<frame->viewport.left
        ||screenX>=frame->viewport.right||screenY<frame->viewport.top||screenY>=frame->viewport.bottom)return;
    frame->nonBuildingPixels[static_cast<std::size_t>(screenY-frame->viewport.top)*frame->width
        +screenX-frame->viewport.left]=frame->paintOrdinal;
}

void restoreRuntimeFrozenBuildingUnderlay(HDC dc,const RuntimeMapRenderContext& context,int owner) {
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
            const auto background=GetPixel(dc,x,y);if(background==CLR_INVALID)continue;
            SetPixelV(dc,x,y,colorRefFromRuntimeDibColor(blendRuntimeBuildingDepthLayers(blend,packRuntimeDibColor(background),afterOrdinal)));
        }
    }
}

bool compositeRuntimeBuildingDepthPixel(const RECT& imageRect,int x,int y,std::uint32_t& destination) {
    auto& frame=*runtimeBuildingDepthFrame;
    if(frame.blends.empty())return false;
    const auto pixelIndex=static_cast<std::size_t>(imageRect.top+y-frame.viewport.top)*frame.width
        +imageRect.left+x-frame.viewport.left;
    const auto found=frame.blends.find(pixelIndex);
    if(found==frame.blends.end())return false;
    auto& blend=found->second;
    if(!blend.emitted) {
        destination=blendRuntimeBuildingDepthLayers(blend,destination);
        blend.emitted=true;
    }
    return true;
}
