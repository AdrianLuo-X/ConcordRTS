#pragma once

#include "assets/codecs/VoxelMetadataCodec.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include <array>
#include <string>
#include <vector>

struct RuntimeVoxelMuzzleEndpoint {
    std::array<double, 3> modelPoint{};
    std::array<double, 3> modelForward{};
    unsigned int sectionIndex = 0;
    int faceVoxelCount = 0;
};

struct RuntimeVoxelBodyRotation;

namespace concord::render::voxel {
bool findNativeMuzzleAnchor(const std::string& imageId,
    const std::vector<unsigned char>& vxlBytes,
    const std::vector<unsigned char>& hvaBytes, std::array<double, 3>& point);

// Measure authored gun ends in model coordinates for the renderer's existing cache.
bool resolveMuzzleEndpoints(const std::vector<unsigned char>& bytes,
    const std::vector<RuntimeHvaTransform>& transforms,
    std::vector<RuntimeVoxelMuzzleEndpoint>& endpoints, std::string& error);

struct MuzzleProjectionScale {
    double screenXScale;
    double groundYScale;
    double heightYScale;
    double fitScale;
};

std::array<double, 3> orientMuzzlePoint(const std::array<double, 3>& local,
    double gunYaw, const RuntimeVoxelBodyRotation& rotation);

RuntimeInputPixelPoint projectMuzzlePoint(const RuntimeInputPixelPoint& center,
    const std::array<double, 3>& posedPoint, const MuzzleProjectionScale& scale);
}
