#include "render/voxel/RuntimeVoxelMuzzleModel.h"

bool resolveRuntimeVoxelMuzzleEndpoints(
    const std::vector<unsigned char>& bytes,
    const std::vector<RuntimeHvaTransform>& transforms,
    std::vector<RuntimeVoxelMuzzleEndpoint>& endpoints,
    std::string& error
) {
    return concord::render::voxel::resolveMuzzleEndpoints(bytes, transforms, endpoints, error);
}
