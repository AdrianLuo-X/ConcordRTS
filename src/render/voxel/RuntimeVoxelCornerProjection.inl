#include "render/voxel/RuntimeVoxelCornerProjectionMemo.h"

thread_local int runtimeVoxelCornerProjectionReferenceOverride = -1;
thread_local std::size_t runtimeVoxelCornerProjectionByteLimit =
    RuntimeVoxelCornerProjectionMemo::maximumScratchBytes;

struct RuntimeVoxelCornerProjectionObservation {
    std::size_t requests = 0;
    std::size_t evaluations = 0;
    std::size_t admittedSections = 0;
    std::size_t fallbackSections = 0;
    std::size_t maximumScratchBytes = 0;
};
thread_local RuntimeVoxelCornerProjectionObservation* runtimeVoxelCornerProjectionObservation = nullptr;

bool runtimeVoxelCornerProjectionReferenceForDecode() {
    if (runtimeVoxelCornerProjectionReferenceOverride >= 0)
        return runtimeVoxelCornerProjectionReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_VOXEL_CORNER_PROJECTION_REFERENCE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return reference;
}

class RuntimeVoxelCornerProjectionReferenceScope {
public:
    explicit RuntimeVoxelCornerProjectionReferenceScope(bool reference)
        : previous_(runtimeVoxelCornerProjectionReferenceOverride) {
        runtimeVoxelCornerProjectionReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeVoxelCornerProjectionReferenceScope() {
        runtimeVoxelCornerProjectionReferenceOverride = previous_;
    }
    RuntimeVoxelCornerProjectionReferenceScope(const RuntimeVoxelCornerProjectionReferenceScope&) = delete;
    RuntimeVoxelCornerProjectionReferenceScope& operator=(const RuntimeVoxelCornerProjectionReferenceScope&) = delete;
private:
    int previous_;
};

class RuntimeVoxelCornerProjectionObservationScope {
public:
    explicit RuntimeVoxelCornerProjectionObservationScope(
        RuntimeVoxelCornerProjectionObservation& observation,
        std::size_t byteLimit = RuntimeVoxelCornerProjectionMemo::maximumScratchBytes)
        : previous_(runtimeVoxelCornerProjectionObservation),
          previousLimit_(runtimeVoxelCornerProjectionByteLimit) {
        runtimeVoxelCornerProjectionObservation = &observation;
        runtimeVoxelCornerProjectionByteLimit = byteLimit;
    }
    ~RuntimeVoxelCornerProjectionObservationScope() {
        runtimeVoxelCornerProjectionObservation = previous_;
        runtimeVoxelCornerProjectionByteLimit = previousLimit_;
    }
    RuntimeVoxelCornerProjectionObservationScope(const RuntimeVoxelCornerProjectionObservationScope&) = delete;
    RuntimeVoxelCornerProjectionObservationScope& operator=(const RuntimeVoxelCornerProjectionObservationScope&) = delete;
private:
    RuntimeVoxelCornerProjectionObservation* previous_;
    std::size_t previousLimit_;
};
