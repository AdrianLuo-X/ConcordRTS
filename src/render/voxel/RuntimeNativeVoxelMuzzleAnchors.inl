#include "render/voxel/RuntimeVoxelMuzzleModel.h"

bool runtimeNativeVoxelMuzzleAnchor(const std::string& imageId,
    const std::vector<unsigned char>& vxlBytes,
    const std::vector<unsigned char>& hvaBytes, std::array<double, 3>& point) {
    return concord::render::voxel::findNativeMuzzleAnchor(imageId, vxlBytes, hvaBytes, point);
}
