// These keys describe metadata requests, not complete object poses. Movement,
// firing/idle animation, facing, height, transparency and placement still run
// through their normal per-object paths on every geometry query.
struct RuntimeGeometryMetadataKeyView {
    std::string_view type, mission;
    WorldObjectKind kind;
    int activeTurret, chargeStart;
    bool deployed, emptyDeck;
};
struct RuntimeGeometryMetadataKey {
    std::string type, mission;
    WorldObjectKind kind;
    int activeTurret, chargeStart;
    bool deployed, emptyDeck;
    explicit RuntimeGeometryMetadataKey(const RuntimeGeometryMetadataKeyView& key)
        : type(key.type), mission(key.mission), kind(key.kind), activeTurret(key.activeTurret),
          chargeStart(key.chargeStart), deployed(key.deployed), emptyDeck(key.emptyDeck) {}
};
static void runtimeGeometryHashCombine(std::size_t& hash, std::size_t value) {
    hash ^= value + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
}
struct RuntimeGeometryMetadataHash {
    using is_transparent = void;
    template<class Key> std::size_t operator()(const Key& key) const {
        std::size_t hash = std::hash<std::string_view>{}(key.type);
        runtimeGeometryHashCombine(hash, std::hash<std::string_view>{}(key.mission));
        runtimeGeometryHashCombine(hash, static_cast<std::size_t>(key.kind));
        runtimeGeometryHashCombine(hash, static_cast<std::size_t>(key.activeTurret));
        runtimeGeometryHashCombine(hash, static_cast<std::size_t>(key.chargeStart));
        runtimeGeometryHashCombine(hash, key.deployed); runtimeGeometryHashCombine(hash, key.emptyDeck);
        return hash;
    }
};
struct RuntimeGeometryMetadataEqual {
    using is_transparent = void;
    template<class Left, class Right> bool operator()(const Left& left, const Right& right) const {
        return left.type == right.type && left.mission == right.mission && left.kind == right.kind
            && left.activeTurret == right.activeTurret && left.chargeStart == right.chargeStart
            && left.deployed == right.deployed && left.emptyDeck == right.emptyDeck;
    }
};

struct RuntimeGeometryVoxelKeyView {
    std::string_view image, owner;
    std::uint64_t scaleBits;
    int facing, shadow, frame, pitch;
};
struct RuntimeGeometryVoxelKey {
    std::string image, owner;
    std::uint64_t scaleBits;
    int facing, shadow, frame, pitch;
    explicit RuntimeGeometryVoxelKey(const RuntimeGeometryVoxelKeyView& key)
        : image(key.image), owner(key.owner), scaleBits(key.scaleBits), facing(key.facing),
          shadow(key.shadow), frame(key.frame), pitch(key.pitch) {}
};
struct RuntimeGeometryVoxelHash {
    using is_transparent = void;
    template<class Key> std::size_t operator()(const Key& key) const {
        std::size_t hash = std::hash<std::string_view>{}(key.image);
        runtimeGeometryHashCombine(hash, std::hash<std::string_view>{}(key.owner));
        runtimeGeometryHashCombine(hash, std::hash<std::uint64_t>{}(key.scaleBits));
        runtimeGeometryHashCombine(hash, static_cast<std::size_t>(key.facing));
        runtimeGeometryHashCombine(hash, static_cast<std::size_t>(key.shadow));
        runtimeGeometryHashCombine(hash, static_cast<std::size_t>(key.frame));
        runtimeGeometryHashCombine(hash, static_cast<std::size_t>(key.pitch));
        return hash;
    }
};
struct RuntimeGeometryVoxelEqual {
    using is_transparent = void;
    template<class Left, class Right> bool operator()(const Left& left, const Right& right) const {
        return left.image == right.image && left.owner == right.owner && left.scaleBits == right.scaleBits
            && left.facing == right.facing && left.shadow == right.shadow
            && left.frame == right.frame && left.pitch == right.pitch;
    }
};

static thread_local std::optional<bool> runtimePaintGeometryReferenceOverride;
static bool runtimePaintGeometryReference() {
    if (runtimePaintGeometryReferenceOverride) return *runtimePaintGeometryReferenceOverride;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_PAINT_GEOMETRY_REFERENCE");
    return runtimeMovementReferenceForRender() || (value && value[0] == '1' && value[1] == '\0');
}
struct RuntimePaintGeometryReferenceScope {
    std::optional<bool> previous = runtimePaintGeometryReferenceOverride;
    explicit RuntimePaintGeometryReferenceScope(bool enabled) { runtimePaintGeometryReferenceOverride = enabled; }
    ~RuntimePaintGeometryReferenceScope() { runtimePaintGeometryReferenceOverride = previous; }
};

static thread_local std::optional<bool> runtimePaintBuildingDepthReferenceOverride;
static bool runtimePaintBuildingDepthReference() {
    if (runtimePaintBuildingDepthReferenceOverride) return *runtimePaintBuildingDepthReferenceOverride;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_PAINT_BUILDING_DEPTH_REFERENCE");
    return value && value[0] == '1' && value[1] == '\0';
}
struct RuntimePaintBuildingDepthReferenceScope {
    std::optional<bool> previous = runtimePaintBuildingDepthReferenceOverride;
    explicit RuntimePaintBuildingDepthReferenceScope(bool enabled) { runtimePaintBuildingDepthReferenceOverride = enabled; }
    ~RuntimePaintBuildingDepthReferenceScope() { runtimePaintBuildingDepthReferenceOverride = previous; }
};

static thread_local std::optional<bool> runtimePaintTerrainTileReferenceOverride;
static bool runtimePaintTerrainTileReference() {
    if (runtimePaintTerrainTileReferenceOverride) return *runtimePaintTerrainTileReferenceOverride;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_PAINT_TERRAIN_TILE_REFERENCE");
    return value && value[0] == '1' && value[1] == '\0';
}
struct RuntimePaintTerrainTileReferenceScope {
    std::optional<bool> previous = runtimePaintTerrainTileReferenceOverride;
    explicit RuntimePaintTerrainTileReferenceScope(bool enabled) { runtimePaintTerrainTileReferenceOverride = enabled; }
    ~RuntimePaintTerrainTileReferenceScope() { runtimePaintTerrainTileReferenceOverride = previous; }
};

using RuntimeBuildingVisibleHitSpans = std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>>;

#include "render/geometry/RuntimePaintSpecialObjectIdMemo.inl"

struct RuntimeGeometryQueryFrame {
    static inline thread_local RuntimeGeometryQueryFrame* active = nullptr;
    RuntimeGeometryQueryFrame* previous = active;
    RuntimeGeometryQueryFrame* memoOwner = this;
    const GameWorld& world;
    RECT viewport;
    RuntimeMapRenderContext identity;
    RuntimeMapCamera camera;
    RuntimeMapRenderCache& cache;
    RuntimePaintSpecialObjectIdMemo specialObjectIds;
    bool specialObjectIdsReference = false;
    RuntimeSpecialObjectRenderLookup specialObjects;
    std::unordered_set<int> emptyDeckObjects;
    bool resolvingMetadata = false;
    bool allowFrameMemo = true;
    bool structuresPrepared = false;
    bool viewportStructureGeometryOnly = false;
    std::vector<RuntimeWorldObjectDrawRecord> structures;
    RuntimeRenderSpatialIndex structureIndex;
    std::unordered_map<RuntimeGeometryMetadataKey, RuntimeGeometryTypeMetadata,
        RuntimeGeometryMetadataHash, RuntimeGeometryMetadataEqual> metadata;
    std::unordered_map<RuntimeGeometryVoxelKey, RuntimeVoxelCacheEntry*,
        RuntimeGeometryVoxelHash, RuntimeGeometryVoxelEqual> voxels;
    std::unordered_map<std::string, int> voxelAnimationFrameCounts;
    struct TerrainTile {
        RuntimeTmpTileFile* file = nullptr;
        bool updatesLastLoadError = false;
    };
    // Resource-map nodes are stable across insertions. The public immutable
    // paint scope forbids clearing/replacing the resource cache while borrowed.
    // A present null entry means a resolved miss, rather than an absent memo.
    std::unordered_map<int, TerrainTile> terrainTiles;
    bool vehicleTerrainPrepared = false;
    std::unordered_map<std::uint64_t, const TerrainCell*> vehicleTerrainCells;
    struct MobileBounds {
        POINT center{};
        RECT bounds{};
        bool resolved = false;
    };
    struct StructureGeometry {
        POINT center{};
        RuntimeStructureSpriteGeometry geometry;
        bool resolved = false;
    };
    std::unordered_map<const WorldObjectInstance*, MobileBounds> mobileBounds;
    std::unordered_map<const WorldObjectInstance*, StructureGeometry> structureGeometry;
    // Own only the compact completed hit result. The full scene-Z frame dies
    // with drawRuntimeMap; no pointer into it may escape to interaction queries.
    RuntimeBuildingVisibleHitSpans buildingVisibleHitSpans;
    bool buildingVisibleHitSpansReady = false;

    bool matchesBodyContext(const RuntimeMapRenderContext& context) const {
        // These sources are immutable for the batch. Pure color transforms,
        // destination pixels, timing and diagnostics do not affect body bounds.
        return identity.stableRenderConfiguration && context.stableRenderConfiguration
            && identity.theater == context.theater && identity.art == context.art
            && identity.rules == context.rules && identity.mapFacts == context.mapFacts
            && identity.projectileActors == context.projectileActors
            && identity.impactActors == context.impactActors && identity.wrecks == context.wrecks
            && identity.buildingCollapses == context.buildingCollapses
            && identity.particleSnapshots == context.particleSnapshots
            && identity.particleBeamSnapshots == context.particleBeamSnapshots
            && identity.localLightSnapshots == context.localLightSnapshots
            && identity.specialMechanismSnapshots == context.specialMechanismSnapshots
            && identity.selectedObjectIds == context.selectedObjectIds
            && identity.playerCheerUntilTick == context.playerCheerUntilTick
            && identity.gateSnapshots == context.gateSnapshots && identity.uiResources == context.uiResources
            && identity.ownerRemapColors == context.ownerRemapColors
            && identity.neutralRemapOwners == context.neutralRemapOwners
            && identity.vision == context.vision && identity.visionObserverOwner == context.visionObserverOwner
            && identity.tileRootPath == context.tileRootPath && identity.palettePath == context.palettePath
            && identity.assetRootPath == context.assetRootPath
            && identity.structureRootPath == context.structureRootPath
            && identity.structurePalettePath == context.structurePalettePath
            && identity.animationTick == context.animationTick && identity.fixedTickHz == context.fixedTickHz
            && identity.visualDetail == context.visualDetail && identity.seeHiddenObjects == context.seeHiddenObjects
            && identity.useTileTextures == context.useTileTextures
            && identity.effectOnlyProbe == context.effectOnlyProbe
            && identity.shadowsEnabled == context.shadowsEnabled;
    }
    bool matchesCamera(const RuntimeMapCamera& candidate) const {
        return camera.worldOffsetX == candidate.worldOffsetX && camera.worldOffsetY == candidate.worldOffsetY
            && camera.tileWidth == candidate.tileWidth && camera.tileHeight == candidate.tileHeight
            && camera.initialized == candidate.initialized;
    }
    bool matchesBodies(const RuntimeMapRenderContext& context, const RuntimeMapCamera& sourceCamera,
        RuntimeMapRenderCache& sourceCache) const {
        return allowFrameMemo && &cache == &sourceCache && matchesCamera(sourceCamera)
            && matchesBodyContext(context) && !runtimePaintGeometryReference();
    }
    bool isOriginalObject(const WorldObjectInstance& object) const {
        // Do not treat a copied spy identity or a recycled temporary address as
        // the original object. Membership also makes all pose/state dependencies
        // exact under the public batch's immutable-world contract, without a
        // partial field hash which could omit armor, facing or animation state.
        const auto& objects = world.getObjects();
        const auto address = reinterpret_cast<std::uintptr_t>(&object);
        const auto first = reinterpret_cast<std::uintptr_t>(objects.data());
        return address >= first && address - first < objects.size() * sizeof(WorldObjectInstance)
            && (address - first) % sizeof(WorldObjectInstance) == 0;
    }

    RuntimeGeometryQueryFrame(const GameWorld& sourceWorld, const RECT& sourceViewport,
        const RuntimeMapRenderContext& context, const RuntimeMapCamera& sourceCamera,
        RuntimeMapRenderCache& sourceCache, bool sourceViewportStructureGeometryOnly)
        : world(sourceWorld), viewport(sourceViewport), identity(context), camera(sourceCamera),
          cache(sourceCache), specialObjectIds(sourceWorld) {
        // This independent reference is latched once per immutable paint scope.
        specialObjectIdsReference = runtimePaintSpecialObjectIdReference()
            || runtimePaintGeometryReference();
        viewportStructureGeometryOnly = sourceViewportStructureGeometryOnly;
        if (viewportStructureGeometryOnly) {
            const char* reference = runtimeRendererEnvironmentValue("CONCORD_DENSE_BUILDING_REFERENCE");
            if (reference && reference[0] == '1' && reference[1] == '\0')
                viewportStructureGeometryOnly = false;
        }
        for (const char* setting : {"CONCORD_VXL_DUMP_ATLAS", "CONCORD_VXL_DUMP_SWEEPS",
                "CONCORD_VXL_DUMP_SWEEP_GROUP", "CONCORD_VXL_DUMP_ATLAS_OBJECTS",
                "CONCORD_VXL_DUMP_OWNER_COLORS"}) {
            const char* value = runtimeRendererEnvironmentValue(setting);
            if (value && *value) allowFrameMemo = false;
        }
        if (previous && &previous->world == &world && EqualRect(&previous->viewport, &viewport)
            && previous->matchesBodies(context, sourceCamera, sourceCache) && allowFrameMemo)
            memoOwner = previous->memoOwner;
        if (memoOwner == this) specialObjects = buildRuntimeSpecialObjectRenderLookup(context);
        if (memoOwner == this && context.specialMechanismSnapshots) {
            for (const auto& snapshot : *context.specialMechanismSnapshots)
                if (snapshot.kind == "missileDeck" || snapshot.kind == "noSpawnDeck")
                    emptyDeckObjects.insert(snapshot.objectId);
        }
        active = this;
    }
    ~RuntimeGeometryQueryFrame() { active = previous; }
    bool matchesMetadata(const RuntimeMapRenderContext& context) const {
        return identity.rules == context.rules && identity.art == context.art
            && identity.theater == context.theater && identity.mapFacts == context.mapFacts
            && identity.specialMechanismSnapshots == context.specialMechanismSnapshots
            && identity.animationTick == context.animationTick;
    }
    bool matchesVoxels(const RuntimeMapRenderContext& context, RuntimeMapRenderCache& targetCache) const {
        return allowFrameMemo && &cache == &targetCache && context.stableRenderConfiguration
            && identity.assetRootPath == context.assetRootPath;
    }
};

static RuntimePaintSpecialObjectIdMemo* runtimePaintSpecialObjectIdsForWorld(const GameWorld& world) {
    auto* frame = RuntimeGeometryQueryFrame::active;
    if (!frame || &frame->world != &world || !frame->allowFrameMemo
        || frame->specialObjectIdsReference) return nullptr;
    auto* owner = frame->memoOwner;
    // A compatible nested map/query scope shares the original owner. Foreign
    // or incompatible nesting keeps the scalar path, rather than borrowing a
    // different scope's lease based merely on a matching address/count.
    if ((frame->previous && owner == frame) || &owner->world != &world
        || !owner->allowFrameMemo || owner->specialObjectIdsReference) return nullptr;
    return owner->specialObjectIds.matches(world) ? &owner->specialObjectIds : nullptr;
}

const RuntimeMapRenderContext* runtimeGeometryBatchRenderContextInternal() {
    const auto* frame = RuntimeGeometryQueryFrame::active;
    return frame ? &frame->identity : nullptr;
}

static RuntimeGeometryQueryFrame* runtimeGeometryTerrainTileMemo(
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache) {
    auto* frame = RuntimeGeometryQueryFrame::active;
    if (!frame || runtimePaintTerrainTileReference()
        || !frame->matchesBodies(context, frame->camera, cache)) return nullptr;
    auto* owner = frame->memoOwner;
    return owner->matchesBodies(context, frame->camera, cache) ? owner : nullptr;
}

static bool findRuntimeGeometryTerrainTile(int index, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, RuntimeTmpTileFile*& file, bool& updatesLastLoadError) {
    auto* frame = runtimeGeometryTerrainTileMemo(context, cache);
    if (!frame) return false;
    const auto found = frame->terrainTiles.find(index);
    if (found == frame->terrainTiles.end()) {
        ++cache.geometryTerrainTileMemoMisses;
        return false;
    }
    ++cache.geometryTerrainTileMemoHits;
    file = found->second.file;
    updatesLastLoadError = found->second.updatesLastLoadError;
    return true;
}

static void rememberRuntimeGeometryTerrainTile(int index, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, RuntimeTmpTileFile* file, bool updatesLastLoadError) {
    auto* frame = runtimeGeometryTerrainTileMemo(context, cache);
    // Bound derived memory regardless of a mod's tile-index cardinality.
    constexpr std::size_t maximumMemoTiles = 4096;
    if (frame && frame->terrainTiles.size() < maximumMemoTiles)
        frame->terrainTiles.emplace(index, RuntimeGeometryQueryFrame::TerrainTile{file, updatesLastLoadError});
}

static RuntimeGeometryQueryFrame* runtimeGeometryBodyMemo(const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache) {
    auto* frame = RuntimeGeometryQueryFrame::active;
    return frame && frame->matchesBodies(context, camera, cache) && frame->isOriginalObject(object)
        ? frame->memoOwner : nullptr;
}

static bool runtimeGeometryPaintRecordingEnabled(const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache) {
    const auto* frame = RuntimeGeometryQueryFrame::active;
    // A standalone map paint has no later consumer. Keep its former cost; an
    // outer paint batch is the explicit lifetime for map-to-overlay reuse.
    return frame && frame->memoOwner != frame && runtimeGeometryBodyMemo(object, context, camera, cache);
}

static RuntimeGeometryQueryFrame* runtimePaintBuildingDepthMemo(const GameWorld& world,
    const RECT& viewport, const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    auto* frame = RuntimeGeometryQueryFrame::active;
    if (!frame || &frame->world != &world || !EqualRect(&frame->viewport, &viewport)
        || !frame->matchesBodies(context, camera, cache) || runtimePaintBuildingDepthReference()
        || context.effectOnlyProbe || !context.surfacePixels || context.surfaceWidth <= 0
        || context.surfaceHeight <= 0 || viewport.left < 0 || viewport.top < 0
        || viewport.right > context.surfaceWidth || viewport.bottom > context.surfaceHeight
        || runtimeBuildingDepthFrame != nullptr || runtimeSpriteGeometryCapture != nullptr)
        return nullptr;
    const char* denseReference = runtimeRendererEnvironmentValue("CONCORD_DENSE_BUILDING_REFERENCE");
    if ((denseReference && denseReference[0] == '1' && denseReference[1] == '\0')
        || runtimeEmptyBuildingDepthReferenceForRender()) return nullptr;
    auto* owner = frame->memoOwner;
    // A compatible nested scope must also match the actual publisher, rather
    // than merely another scope which happens to be nested beneath it.
    return &owner->world == &world && EqualRect(&owner->viewport, &viewport)
        && owner->matchesBodies(context, camera, cache)
        && owner->identity.chaosBodyTint == context.chaosBodyTint
        && owner->identity.airstrikeBodyColorAdd == context.airstrikeBodyColorAdd
        && owner->identity.lightingTint == context.lightingTint
        && owner->identity.lightingProfile == context.lightingProfile ? owner : nullptr;
}

static bool findRuntimePaintBuildingVisibleHitSpans(const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeBuildingVisibleHitSpans& visibleSpans) {
    auto* memo = runtimePaintBuildingDepthMemo(world, viewport, context, camera, cache);
    if (!memo || !memo->buildingVisibleHitSpansReady) {
        ++cache.geometryBuildingDepthMemoMisses;
        return false;
    }
    visibleSpans = memo->buildingVisibleHitSpans;
    ++cache.geometryBuildingDepthMemoHits;
    return true;
}

static bool findRuntimeGeometryMobileBounds(const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RECT& bounds, bool& resolved) {
    auto* memo = runtimeGeometryBodyMemo(object, context, camera, cache);
    if (!memo) return false;
    const auto found = memo->mobileBounds.find(&object);
    if (found == memo->mobileBounds.end() || found->second.center.x != center.x
        || found->second.center.y != center.y) { ++cache.geometryMobileBoundsMemoMisses; return false; }
    bounds = found->second.bounds; resolved = found->second.resolved;
    ++cache.geometryMobileBoundsMemoHits;
    return true;
}

static void rememberRuntimeGeometryMobileBounds(const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, const RECT& bounds, bool resolved, bool painted = false) {
    if (auto* memo = runtimeGeometryBodyMemo(object, context, camera, cache)) {
        memo->mobileBounds.insert_or_assign(&object, RuntimeGeometryQueryFrame::MobileBounds{center, bounds, resolved});
        if (painted) ++cache.geometryMobileBoundsPaintRecords;
    }
}

static bool findRuntimeGeometryStructure(const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeStructureSpriteGeometry& geometry, bool& resolved) {
    auto* memo = runtimeGeometryBodyMemo(object, context, camera, cache);
    if (!memo) return false;
    const auto found = memo->structureGeometry.find(&object);
    if (found == memo->structureGeometry.end() || found->second.center.x != center.x
        || found->second.center.y != center.y) { ++cache.geometryStructureMemoMisses; return false; }
    geometry = found->second.geometry; resolved = found->second.resolved;
    ++cache.geometryStructureMemoHits;
    return true;
}

static void rememberRuntimeGeometryStructure(const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, const RuntimeStructureSpriteGeometry& geometry, bool resolved) {
    if (auto* memo = runtimeGeometryBodyMemo(object, context, camera, cache))
        memo->structureGeometry.insert_or_assign(&object,
            RuntimeGeometryQueryFrame::StructureGeometry{center, geometry, resolved});
}

const RuntimeGeometryTypeMetadata* runtimeGeometryMetadataForObject(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context) {
    auto* frame = RuntimeGeometryQueryFrame::active;
    if (!frame || frame->resolvingMetadata || !frame->matchesMetadata(context)) return nullptr;
    const RuntimeGeometryMetadataKeyView key{object.type, object.mission, object.kind,
        object.activeGunnerTurretIndex, object.chargeTurretStartTick, object.deployed,
        frame->memoOwner->emptyDeckObjects.find(object.instanceId) != frame->memoOwner->emptyDeckObjects.end()};
    auto& memo = frame->memoOwner->metadata;
    const auto found = memo.find(key);
    if (found != memo.end()) return &found->second;
    struct ResolveScope {
        bool& value;
        explicit ResolveScope(bool& target) : value(target) { value = true; }
        ~ResolveScope() { value = false; }
    } resolving(frame->resolvingMetadata);
    RuntimeGeometryTypeMetadata result;
    result.unit = findObjectUnitTypeForRender(context, object);
    result.art = findArtTypeForObject(object, context);
    result.visualTypeId = resolveObjectVisualTypeIdForRender(object, context);
    result.artId = resolveObjectArtId(object, context);
    result.spriteImageId = resolveObjectSpriteImageId(object, context);
    result.aircraft = runtimeObjectLooksAircraftForRender(object, context);
    result.turret = runtimeObjectHasTurretForRender(object, context);
    const auto footprint = runtimeObjectFoundationDimensions(object, context);
    result.footprintWidth = footprint.width; result.footprintHeight = footprint.height;
    result.bodyCandidates = resolveObjectVoxelImageIdCandidates(object, context);
    if (object.kind == WorldObjectKind::Unit) {
        if (result.turret) {
            result.turretCandidates = runtimeUnitTurretImageCandidates(object, context);
            result.barrelCandidates = runtimeUnitBarrelImageCandidates(object, context);
        }
    }
    return &memo.emplace(RuntimeGeometryMetadataKey(key), std::move(result)).first->second;
}

static RuntimeGeometryVoxelKeyView runtimeGeometryVoxelKey(const std::string& image,
    const std::string& owner, int facing, double scale, int shadow, int frame, int pitch) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(scale));
    std::memcpy(&bits, &scale, sizeof(bits));
    return {image, owner, bits, facing, shadow, frame, pitch};
}

bool findRuntimeGeometryVoxelFrame(const std::string& imageId,
    const std::string& ownerId, int facing, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, double scale, int shadow, int animation, int pitch,
    RuntimeVoxelCacheEntry*& result) {
    auto* frame = RuntimeGeometryQueryFrame::active;
    if (!frame || !frame->matchesVoxels(context, cache) || runtimeVxlDisableCache()) return false;
    const auto& memo = frame->memoOwner->voxels;
    const auto found = memo.find(runtimeGeometryVoxelKey(imageId, ownerId,
        facing, scale, shadow, animation, pitch));
    if (found == memo.end()) return false;
    result = found->second;
    ++cache.voxelCacheHits;
    if (result) result->image.rasterCacheHit = true;
    return true;
}

void rememberRuntimeGeometryVoxelFrame(const std::string& imageId,
    const std::string& ownerId, int facing, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, double scale, int shadow, int animation, int pitch,
    RuntimeVoxelCacheEntry* result) {
    auto* frame = RuntimeGeometryQueryFrame::active;
    if (!frame || !frame->matchesVoxels(context, cache) || runtimeVxlDisableCache()) return;
    frame->memoOwner->voxels.emplace(RuntimeGeometryVoxelKey(runtimeGeometryVoxelKey(imageId, ownerId,
        facing, scale, shadow, animation, pitch)), result);
}

const RuntimeSpecialObjectRenderLookup* runtimeGeometrySpecialObjectLookup(
    const RuntimeMapRenderContext& context) {
    const auto* frame = RuntimeGeometryQueryFrame::active;
    return frame && frame->matchesMetadata(context) ? &frame->memoOwner->specialObjects : nullptr;
}
