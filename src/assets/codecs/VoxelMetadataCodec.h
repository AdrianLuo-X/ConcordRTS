#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Canonical complete existing render values: names, members and defaults are unchanged.
struct RuntimeVoxelMetadata {
    std::string imageId;
    std::string vxlPath;
    std::string hvaPath;
    int vxlSectionCount = 0;
    int hvaSectionCount = 0;
    int hvaFrameCount = 0;
    std::uint64_t vxlFileSize = 0;
    std::uint64_t hvaFileSize = 0;
    std::string vxlSectionSample;
    std::string hvaSectionSample;
};

struct RuntimeHvaTransform {
    bool valid = false;
    std::array<double, 12> m{
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0
    };
};

namespace concord::assets::voxel {
// These contracts intentionally preserve each consumer's existing admission, errors and partial writes.
namespace render {
bool parseRuntimeVxlMetadataFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    RuntimeVoxelMetadata& metadata,
    std::string& errorMessage
);

bool parseRuntimeHvaMetadataFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    RuntimeVoxelMetadata& metadata,
    std::string& errorMessage
);

bool parseRuntimeHvaFrameTransformsFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    std::vector<RuntimeHvaTransform>& transforms,
    std::string& errorMessage,
    int frameIndex = 0
);
}
namespace inspection {
struct VxlMetadata {
    std::string signature;
    unsigned int headerValue0 = 0;
    unsigned int sectionCount = 0;
    unsigned int sectionCountMirror = 0;
    unsigned int bodySize = 0;
    unsigned int paletteMarker = 0;
    unsigned int embeddedPaletteMaxChannel = 0;
    std::size_t embeddedPaletteOffset = 0;
    std::size_t sectionTableOffset = 0;
    std::size_t sectionRecordBytes = 0;
    std::vector<std::string> sectionNames;
};

struct HvaMetadata {
    std::string path;
    unsigned int frameCount = 0;
    unsigned int sectionCount = 0;
    std::size_t matrixDataOffset = 0;
    std::size_t expectedFileBytes = 0;
    std::vector<std::string> sectionNames;
    std::vector<float> firstMatrix;
};

bool parseVxlMetadata(
    const std::vector<unsigned char>& bytes,
    VxlMetadata& metadata,
    std::string& errorMessage
);

bool parseHvaMetadata(
    const std::vector<unsigned char>& bytes,
    HvaMetadata& metadata,
    std::string& errorMessage
);
}
}
