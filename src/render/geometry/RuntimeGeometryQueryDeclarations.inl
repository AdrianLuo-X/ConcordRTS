struct RuntimeGeometryTypeMetadata {
    const UnitType* unit = nullptr;
    const ArtType* art = nullptr;
    std::string visualTypeId;
    std::string artId;
    std::string spriteImageId;
    std::vector<std::string> bodyCandidates;
    std::vector<std::string> turretCandidates;
    std::vector<std::string> barrelCandidates;
    int footprintWidth = 1;
    int footprintHeight = 1;
    bool aircraft = false;
    bool turret = false;
};

// One process uses one mode so identical fixtures can time the original and
// optimized movement paths without switching caches midway through a frame.
bool runtimeMovementReferenceForRender() {
    static const bool enabled = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

int runtimeGeometryVoxelAnimationFrameCount(const std::string& imageId,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache);

const RuntimeGeometryTypeMetadata* runtimeGeometryMetadataForObject(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context);

bool findRuntimeGeometryVoxelFrame(const std::string& imageId,
    const std::string& ownerId, int facing, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, double scale, int shadow, int frame, int pitch,
    RuntimeVoxelCacheEntry*& result);
void rememberRuntimeGeometryVoxelFrame(const std::string& imageId,
    const std::string& ownerId, int facing, const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache, double scale, int shadow, int frame, int pitch,
    RuntimeVoxelCacheEntry* result);
