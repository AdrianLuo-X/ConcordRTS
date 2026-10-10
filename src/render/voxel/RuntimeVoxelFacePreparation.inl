// Included in the renderer translation unit. This changes preparation work
// only; the reference branch retains the original per-voxel face decisions.
thread_local int runtimeVoxelFacePreparationReferenceOverride = -1;

bool runtimeVoxelFacePreparationReferenceForDecode() {
    if (runtimeVoxelFacePreparationReferenceOverride >= 0)
        return runtimeVoxelFacePreparationReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = runtimeRendererEnvironmentValue(
            "CONCORD_VOXEL_FACE_PREPARATION_REFERENCE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return reference;
}

class RuntimeVoxelFacePreparationReferenceScope {
public:
    explicit RuntimeVoxelFacePreparationReferenceScope(bool reference)
        : previous_(runtimeVoxelFacePreparationReferenceOverride) {
        runtimeVoxelFacePreparationReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeVoxelFacePreparationReferenceScope() {
        runtimeVoxelFacePreparationReferenceOverride = previous_;
    }
    RuntimeVoxelFacePreparationReferenceScope(
        const RuntimeVoxelFacePreparationReferenceScope&) = delete;
    RuntimeVoxelFacePreparationReferenceScope& operator=(
        const RuntimeVoxelFacePreparationReferenceScope&) = delete;
private:
    int previous_;
};
