#include "VoxelMetadataCodec.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace concord::assets::voxel {
namespace render {
namespace {
bool checkedAdd(std::size_t left, std::size_t right, std::size_t& result) {
    if (left > static_cast<std::size_t>(-1) - right) {
        return false;
    }

    result = left + right;
    return true;
}

bool checkedMul(std::size_t left, std::size_t right, std::size_t& result) {
    if (left != 0 && right > static_cast<std::size_t>(-1) / left) {
        return false;
    }

    result = left * right;
    return true;
}

unsigned int readUInt32(
    const std::vector<unsigned char>& bytes,
    std::size_t offset
) {
    if (offset + 4 > bytes.size()) {
        return 0;
    }

    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8)
        | (static_cast<unsigned int>(bytes[offset + 2]) << 16)
        | (static_cast<unsigned int>(bytes[offset + 3]) << 24);
}

float readFloat32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    const std::uint32_t raw = readUInt32(bytes, offset);
    float value = 0.0F;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

std::string trimNulls(const std::string& value) {
    const std::size_t end = value.find('\0');
    return end == std::string::npos ? value : value.substr(0, end);
}

void appendRuntimeSectionSample(std::string& sample, const std::string& name) {
    if (name.empty()) {
        return;
    }
    if (!sample.empty()) {
        sample += ",";
    }
    sample += name;
}

std::string readAsciiString(
    const std::vector<unsigned char>& bytes,
    std::size_t offset,
    std::size_t length
) {
    if (offset >= bytes.size()) {
        return "";
    }

    const std::size_t available = std::min(length, bytes.size() - offset);
    return trimNulls(std::string(
        reinterpret_cast<const char*>(bytes.data() + offset),
        available
    ));
}

}
bool parseRuntimeVxlMetadataFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    RuntimeVoxelMetadata& metadata,
    std::string& errorMessage
) {
    constexpr std::size_t signatureBytes = 16;
    constexpr std::size_t fixedHeaderBytes = 34;
    constexpr std::size_t embeddedPaletteBytes = 768;
    constexpr std::size_t sectionRecordBytes = 28;
    if (bytes.size() < fixedHeaderBytes + embeddedPaletteBytes) {
        errorMessage = "VXL file is too small: " + sourceName;
        return false;
    }

    const std::string signature = readAsciiString(bytes, 0, signatureBytes);
    if (signature != "Voxel Animation") {
        errorMessage = "VXL signature is not Voxel Animation: " + sourceName;
        return false;
    }

    const unsigned int sectionCount = readUInt32(bytes, 20);
    if (sectionCount > 1024) {
        errorMessage = "VXL section count is unreasonably large: " + sourceName;
        return false;
    }

    const std::size_t sectionTableOffset =
        fixedHeaderBytes + embeddedPaletteBytes;
    std::size_t sectionTableBytes = 0;
    if (!checkedMul(
            static_cast<std::size_t>(sectionCount),
            sectionRecordBytes,
            sectionTableBytes
        )) {
        errorMessage = "VXL section table overflowed: " + sourceName;
        return false;
    }

    if (sectionTableOffset + sectionTableBytes > bytes.size()) {
        errorMessage = "VXL section table is outside file: " + sourceName;
        return false;
    }

    metadata.vxlPath = sourceName;
    metadata.vxlSectionCount = static_cast<int>(sectionCount);
    metadata.vxlFileSize = static_cast<std::uint64_t>(bytes.size());
    metadata.vxlSectionSample.clear();
    const unsigned int sectionSampleCount = std::min<unsigned int>(sectionCount, 4U);
    for (unsigned int sectionIndex = 0;
        sectionIndex < sectionSampleCount;
        ++sectionIndex) {
        appendRuntimeSectionSample(
            metadata.vxlSectionSample,
            readAsciiString(
                bytes,
                sectionTableOffset
                    + (static_cast<std::size_t>(sectionIndex) * sectionRecordBytes),
                16
            )
        );
    }
    return true;
}


bool parseRuntimeHvaMetadataFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    RuntimeVoxelMetadata& metadata,
    std::string& errorMessage
) {
    constexpr std::size_t fixedHeaderBytes = 24;
    constexpr std::size_t sectionNameBytes = 16;
    constexpr std::size_t matrixFloatCount = 12;
    constexpr std::size_t matrixBytes = matrixFloatCount * 4;
    if (bytes.size() < fixedHeaderBytes) {
        errorMessage = "HVA file is too small: " + sourceName;
        return false;
    }

    const unsigned int frameCount = readUInt32(bytes, 16);
    const unsigned int sectionCount = readUInt32(bytes, 20);
    if (frameCount > 100000 || sectionCount > 1024) {
        errorMessage = "HVA frame or section count is unreasonably large: "
            + sourceName;
        return false;
    }

    std::size_t sectionNameTableBytes = 0;
    if (!checkedMul(
            static_cast<std::size_t>(sectionCount),
            sectionNameBytes,
            sectionNameTableBytes
        )) {
        errorMessage = "HVA section table overflowed: " + sourceName;
        return false;
    }

    std::size_t matrixDataOffset = 0;
    if (!checkedAdd(fixedHeaderBytes, sectionNameTableBytes, matrixDataOffset)) {
        errorMessage = "HVA matrix data offset overflowed: " + sourceName;
        return false;
    }

    std::size_t matrixCount = 0;
    if (!checkedMul(
            static_cast<std::size_t>(frameCount),
            static_cast<std::size_t>(sectionCount),
            matrixCount
        )) {
        errorMessage = "HVA matrix count overflowed: " + sourceName;
        return false;
    }

    std::size_t allMatrixBytes = 0;
    if (!checkedMul(matrixCount, matrixBytes, allMatrixBytes)) {
        errorMessage = "HVA matrix byte count overflowed: " + sourceName;
        return false;
    }

    if (matrixDataOffset + allMatrixBytes > bytes.size()) {
        errorMessage = "HVA matrix data is outside file: " + sourceName;
        return false;
    }

    metadata.hvaPath = sourceName;
    metadata.hvaFrameCount = static_cast<int>(frameCount);
    metadata.hvaSectionCount = static_cast<int>(sectionCount);
    metadata.hvaFileSize = static_cast<std::uint64_t>(bytes.size());
    metadata.hvaSectionSample.clear();
    const unsigned int sectionSampleCount = std::min<unsigned int>(sectionCount, 4U);
    for (unsigned int sectionIndex = 0;
        sectionIndex < sectionSampleCount;
        ++sectionIndex) {
        appendRuntimeSectionSample(
            metadata.hvaSectionSample,
            readAsciiString(
                bytes,
                fixedHeaderBytes
                    + (static_cast<std::size_t>(sectionIndex) * sectionNameBytes),
                sectionNameBytes
            )
        );
    }
    return true;
}


bool parseRuntimeHvaFrameTransformsFromBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    std::vector<RuntimeHvaTransform>& transforms,
    std::string& errorMessage,
    int frameIndex
) {
    constexpr std::size_t fixedHeaderBytes = 24;
    constexpr std::size_t sectionNameBytes = 16;
    constexpr std::size_t matrixFloatCount = 12;
    constexpr std::size_t matrixBytes = matrixFloatCount * 4;
    if (bytes.size() < fixedHeaderBytes) {
        errorMessage = "HVA file is too small: " + sourceName;
        return false;
    }

    const unsigned int frameCount = readUInt32(bytes, 16);
    const unsigned int sectionCount = readUInt32(bytes, 20);
    if (frameCount == 0 || frameCount > 100000 || sectionCount == 0 || sectionCount > 1024
        || frameIndex < 0 || static_cast<unsigned int>(frameIndex) >= frameCount) {
        errorMessage = "HVA animation frame transform table is invalid: "
            + sourceName;
        return false;
    }

    std::size_t sectionNameTableBytes = 0;
    if (!checkedMul(
            static_cast<std::size_t>(sectionCount),
            sectionNameBytes,
            sectionNameTableBytes
        )) {
        errorMessage = "HVA section table overflowed: " + sourceName;
        return false;
    }

    std::size_t matrixOffset = 0;
    if (!checkedAdd(fixedHeaderBytes, sectionNameTableBytes, matrixOffset)) {
        errorMessage = "HVA matrix offset overflowed: " + sourceName;
        return false;
    }

    std::size_t firstFrameBytes = 0;
    if (!checkedMul(
            static_cast<std::size_t>(sectionCount),
            matrixBytes,
            firstFrameBytes
        )) {
        errorMessage = "HVA first frame byte count overflowed: " + sourceName;
        return false;
    }

    std::size_t frameOffset = 0;
    if (!checkedMul(static_cast<std::size_t>(frameIndex), firstFrameBytes, frameOffset)
        || !checkedAdd(matrixOffset, frameOffset, matrixOffset)
        || matrixOffset > bytes.size() || firstFrameBytes > bytes.size() - matrixOffset) {
        errorMessage = "HVA animation frame matrix data is outside file: "
            + sourceName;
        return false;
    }

    transforms.clear();
    transforms.reserve(sectionCount);
    for (unsigned int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
        const std::size_t transformOffset =
            matrixOffset
            + (static_cast<std::size_t>(sectionIndex) * matrixBytes);
        RuntimeHvaTransform transform;
        transform.valid = true;
        for (std::size_t i = 0; i < matrixFloatCount; ++i) {
            const double value = readFloat32(bytes, transformOffset + (i * 4));
            if (!std::isfinite(value)) {
                errorMessage = "HVA matrix contains a non-finite value: "
                    + sourceName;
                return false;
            }
            transform.m[i] = value;
        }
        transforms.push_back(transform);
    }

    return true;
}


}
namespace inspection {
namespace {
bool checkedAdd(std::size_t left, std::size_t right, std::size_t& result) {
    if (left > static_cast<std::size_t>(-1) - right) {
        return false;
    }

    result = left + right;
    return true;
}

bool checkedMul(std::size_t left, std::size_t right, std::size_t& result) {
    if (left != 0 && right > static_cast<std::size_t>(-1) / left) {
        return false;
    }

    result = left * right;
    return true;
}

unsigned int readUInt16(const std::vector<unsigned char>& bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        return 0;
    }

    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
}

unsigned int readUInt32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        return 0;
    }

    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8)
        | (static_cast<unsigned int>(bytes[offset + 2]) << 16)
        | (static_cast<unsigned int>(bytes[offset + 3]) << 24);
}

float readFloat32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        return 0.0F;
    }

    static_assert(sizeof(float) == 4, "float must be 32-bit for HVA matrices");

    const unsigned int raw = readUInt32(bytes, offset);
    float value = 0.0F;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

std::string trimNulls(const std::string& value) {
    const std::size_t end = value.find('\0');
    if (end == std::string::npos) {
        return value;
    }

    return value.substr(0, end);
}

std::string readAscii(const std::vector<unsigned char>& bytes, std::size_t offset, std::size_t length) {
    if (offset >= bytes.size()) {
        return "";
    }

    const std::size_t available = std::min(length, bytes.size() - offset);
    return trimNulls(std::string(
        reinterpret_cast<const char*>(bytes.data() + offset),
        available
    ));
}

}
bool parseVxlMetadata(
    const std::vector<unsigned char>& bytes,
    VxlMetadata& metadata,
    std::string& errorMessage
) {
    constexpr std::size_t signatureBytes = 16;
    constexpr std::size_t fixedHeaderBytes = 34;
    constexpr std::size_t embeddedPaletteBytes = 768;
    constexpr std::size_t sectionNameBytes = 16;
    constexpr std::size_t sectionRecordBytes = 28;

    if (bytes.size() < fixedHeaderBytes + embeddedPaletteBytes) {
        errorMessage = "VXL file is too small for the fixed header and embedded palette.";
        return false;
    }

    metadata.signature = readAscii(bytes, 0, signatureBytes);
    metadata.headerValue0 = readUInt32(bytes, 16);
    metadata.sectionCount = readUInt32(bytes, 20);
    metadata.sectionCountMirror = readUInt32(bytes, 24);
    metadata.bodySize = readUInt32(bytes, 28);
    metadata.paletteMarker = readUInt16(bytes, 32);
    metadata.embeddedPaletteOffset = fixedHeaderBytes;
    metadata.sectionTableOffset = fixedHeaderBytes + embeddedPaletteBytes;
    metadata.sectionRecordBytes = sectionRecordBytes;

    if (metadata.sectionCount > 1024) {
        errorMessage = "VXL section count is unreasonably large.";
        return false;
    }

    for (std::size_t i = 0; i < embeddedPaletteBytes; ++i) {
        metadata.embeddedPaletteMaxChannel = std::max(
            metadata.embeddedPaletteMaxChannel,
            static_cast<unsigned int>(bytes[metadata.embeddedPaletteOffset + i])
        );
    }

    std::size_t sectionNameTableBytes = 0;
    if (!checkedMul(static_cast<std::size_t>(metadata.sectionCount), sectionRecordBytes, sectionNameTableBytes)) {
        errorMessage = "VXL section table size overflowed.";
        return false;
    }

    std::size_t sectionNameTableEnd = 0;
    if (!checkedAdd(metadata.sectionTableOffset, sectionNameTableBytes, sectionNameTableEnd)) {
        errorMessage = "VXL section name table offset overflowed.";
        return false;
    }

    if (sectionNameTableEnd > bytes.size()) {
        errorMessage = "VXL file is too small for the declared section table.";
        return false;
    }

    metadata.sectionNames.clear();
    metadata.sectionNames.reserve(metadata.sectionCount);

    for (std::size_t i = 0; i < metadata.sectionCount; ++i) {
        const std::size_t nameOffset = metadata.sectionTableOffset + (i * sectionRecordBytes);
        metadata.sectionNames.push_back(readAscii(bytes, nameOffset, sectionNameBytes));
    }

    return true;
}


bool parseHvaMetadata(
    const std::vector<unsigned char>& bytes,
    HvaMetadata& metadata,
    std::string& errorMessage
) {
    constexpr std::size_t pathBytes = 16;
    constexpr std::size_t fixedHeaderBytes = 24;
    constexpr std::size_t sectionNameBytes = 16;
    constexpr std::size_t matrixFloatCount = 12;
    constexpr std::size_t matrixBytes = matrixFloatCount * 4;

    if (bytes.size() < fixedHeaderBytes) {
        errorMessage = "HVA file is too small for a fixed header.";
        return false;
    }

    metadata.path = readAscii(bytes, 0, pathBytes);
    metadata.frameCount = readUInt32(bytes, 16);
    metadata.sectionCount = readUInt32(bytes, 20);

    if (metadata.frameCount > 100000 || metadata.sectionCount > 1024) {
        errorMessage = "HVA frame count or section count is unreasonably large.";
        return false;
    }

    std::size_t sectionNameTableBytes = 0;
    if (!checkedMul(static_cast<std::size_t>(metadata.sectionCount), sectionNameBytes, sectionNameTableBytes)) {
        errorMessage = "HVA section name table size overflowed.";
        return false;
    }

    if (!checkedAdd(fixedHeaderBytes, sectionNameTableBytes, metadata.matrixDataOffset)) {
        errorMessage = "HVA matrix data offset overflowed.";
        return false;
    }

    if (metadata.matrixDataOffset > bytes.size()) {
        errorMessage = "HVA file is too small for the declared section name table.";
        return false;
    }

    metadata.sectionNames.clear();
    metadata.sectionNames.reserve(metadata.sectionCount);

    for (std::size_t i = 0; i < metadata.sectionCount; ++i) {
        const std::size_t nameOffset = fixedHeaderBytes + (i * sectionNameBytes);
        metadata.sectionNames.push_back(readAscii(bytes, nameOffset, sectionNameBytes));
    }

    std::size_t matrixCount = 0;
    if (!checkedMul(
            static_cast<std::size_t>(metadata.frameCount),
            static_cast<std::size_t>(metadata.sectionCount),
            matrixCount
        )) {
        errorMessage = "HVA matrix count overflowed.";
        return false;
    }

    std::size_t allMatrixBytes = 0;
    if (!checkedMul(matrixCount, matrixBytes, allMatrixBytes)) {
        errorMessage = "HVA matrix byte count overflowed.";
        return false;
    }

    if (!checkedAdd(metadata.matrixDataOffset, allMatrixBytes, metadata.expectedFileBytes)) {
        errorMessage = "HVA expected file size overflowed.";
        return false;
    }

    if (metadata.expectedFileBytes > bytes.size()) {
        errorMessage = "HVA file is too small for the declared matrix data.";
        return false;
    }

    metadata.firstMatrix.clear();
    if (matrixCount > 0) {
        metadata.firstMatrix.reserve(matrixFloatCount);

        for (std::size_t i = 0; i < matrixFloatCount; ++i) {
            metadata.firstMatrix.push_back(readFloat32(bytes, metadata.matrixDataOffset + (i * 4)));
        }
    }

    return true;
}


}
}
