#include "assets/inspection/AssetInspector.h"

#include "data/art/ArtDatabase.h"
#include "core/io/IniFile.h"
#include "data/maps/MapDatabase.h"
#include "assets/archive/MixArchive.h"
#include "data/rules/RulesDatabase.h"
#include "data/art/TheaterDatabase.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct RgbColor {
    unsigned char red = 0;
    unsigned char green = 0;
    unsigned char blue = 0;
};

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

struct TheaterTileMetadata {
    std::string layout;
    std::size_t slotCount = 0;
    std::size_t presentTileCount = 0;
    std::size_t emptySlotCount = 0;
    std::size_t tableOffset = 0;
    std::size_t tableBytes = 0;
    std::size_t firstDataOffset = 0;
    std::size_t firstTileBytes = 0;
    std::size_t slotRecordBytes = 0;
    unsigned int xBlocks = 0;
    unsigned int yBlocks = 0;
    unsigned int blockWidth = 0;
    unsigned int blockHeight = 0;
    unsigned int minTileWidth = 0;
    unsigned int maxTileWidth = 0;
    unsigned int minTileHeight = 0;
    unsigned int maxTileHeight = 0;
    bool hasTileDimensions = false;
    std::vector<std::size_t> presentOffsets;
    std::vector<std::size_t> presentSlots;
};

struct ShpFrameHeader {
    unsigned int offsetX = 0;
    unsigned int offsetY = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int format = 0;
    unsigned int dataOffset = 0;
};

std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );

    return value;
}

std::string trimNulls(const std::string& value) {
    const std::size_t end = value.find('\0');
    if (end == std::string::npos) {
        return value;
    }

    return value.substr(0, end);
}

std::string formatHex(unsigned int value, int width) {
    std::ostringstream output;
    output << "0x"
           << std::uppercase
           << std::hex
           << std::setw(width)
           << std::setfill('0')
           << value;
    return output.str();
}

std::string formatUint64Hex(std::uint64_t value, int width) {
    std::ostringstream output;
    output << "0x"
           << std::uppercase
           << std::hex
           << std::setw(width)
           << std::setfill('0')
           << value;
    return output.str();
}

void addField(
    AssetInspectionReport& report,
    const std::string& name,
    const std::string& value
) {
    report.fields.push_back({name, value});
}

void addField(
    AssetInspectionReport& report,
    const std::string& name,
    unsigned long long value
) {
    addField(report, name, std::to_string(value));
}

bool loadBytes(const std::string& path, std::vector<unsigned char>& bytes, std::string& errorMessage) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorMessage = "Failed to open asset file.";
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        errorMessage = "Failed to get asset file size.";
        return false;
    }

    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));

    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    if (!input && !bytes.empty()) {
        errorMessage = "Failed to read asset file.";
        return false;
    }

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

int readInt32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<int>(static_cast<std::int32_t>(readUInt32(bytes, offset)));
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

std::string formatFloat(float value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << value;
    return output.str();
}

std::string formatSectionNames(const std::vector<std::string>& names) {
    if (names.empty()) {
        return "(none)";
    }

    std::ostringstream output;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            output << ", ";
        }

        output << (names[i].empty() ? "(empty)" : names[i]);
    }

    return output.str();
}

std::string formatMatrixRow(const std::vector<float>& matrix, std::size_t row) {
    const std::size_t offset = row * 4;
    if (offset + 4 > matrix.size()) {
        return "";
    }

    return formatFloat(matrix[offset])
        + ", " + formatFloat(matrix[offset + 1])
        + ", " + formatFloat(matrix[offset + 2])
        + ", " + formatFloat(matrix[offset + 3]);
}

std::string yesNo(bool value) {
    return value ? "yes" : "no";
}

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

std::size_t nextFrameDataOffset(
    const std::vector<unsigned int>& sortedOffsets,
    unsigned int dataOffset,
    std::size_t fileSize
) {
    for (const unsigned int nextOffset : sortedOffsets) {
        if (nextOffset > dataOffset) {
            return static_cast<std::size_t>(nextOffset);
        }
    }

    return fileSize;
}

void inspectPalette(AssetInspectionReport& report, const std::vector<unsigned char>& bytes) {
    report.kind = "PAL palette";

    if (bytes.size() % 3 != 0) {
        report.warnings.push_back("Palette size is not divisible by 3.");
    }

    const std::size_t colorCount = bytes.size() / 3;
    addField(report, "colorCount", static_cast<unsigned long long>(colorCount));
    addField(report, "expectedRa2Palette", bytes.size() == 768 ? "yes" : "no");

    unsigned int maxChannel = 0;
    for (const unsigned char value : bytes) {
        maxChannel = std::max(maxChannel, static_cast<unsigned int>(value));
    }

    addField(report, "maxChannelValue", maxChannel);
    addField(report, "channelScale", maxChannel <= 63 ? "0-63" : "0-255");

    if (bytes.size() >= 3) {
        addField(
            report,
            "firstColorRgb",
            std::to_string(bytes[0]) + "," + std::to_string(bytes[1]) + "," + std::to_string(bytes[2])
        );
    }
}

int visibleMainFrameCount(unsigned int frameCount) {
    if (frameCount == 0) {
        return 0;
    }

    if (frameCount >= 4 && (frameCount % 2U) == 0U) {
        return static_cast<int>(std::max(1U, frameCount / 2U));
    }

    return static_cast<int>(std::max(1U, frameCount));
}

void inspectShp(AssetInspectionReport& report, const std::vector<unsigned char>& bytes) {
    report.kind = "SHP sprite";

    if (bytes.size() < 8) {
        report.success = false;
        report.errorMessage = "SHP file is too small for a basic header.";
        return;
    }

    const unsigned int zeroOrType = readUInt16(bytes, 0);
    const unsigned int width = readUInt16(bytes, 2);
    const unsigned int height = readUInt16(bytes, 4);
    const unsigned int frameCount = readUInt16(bytes, 6);

    addField(report, "header0", zeroOrType);
    addField(report, "width", width);
    addField(report, "height", height);
    addField(report, "frameCount", frameCount);
    addField(report, "visibleMainFrames", visibleMainFrameCount(frameCount));
    addField(report, "paletteModel", "indexed-external");
    addField(report, "anchorFormula", "topLeft=project(anchor)+(-globalWidth/2+frameOffsetX,tileH/2-globalHeight+frameOffsetY)");

    if (frameCount == 0) {
        report.warnings.push_back("SHP file has no frames.");
        return;
    }

    constexpr std::size_t fileHeaderBytes = 8;
    constexpr std::size_t frameHeaderBytes = 24;
    std::size_t frameTablePayloadBytes = 0;
    std::size_t frameTableBytes = 0;
    if (!checkedMul(static_cast<std::size_t>(frameCount), frameHeaderBytes, frameTablePayloadBytes)
        || !checkedAdd(fileHeaderBytes, frameTablePayloadBytes, frameTableBytes)) {
        report.warnings.push_back("SHP frame table size overflows.");
        return;
    }

    addField(report, "frameHeaderBytes", static_cast<unsigned long long>(frameHeaderBytes));
    addField(report, "frameTableBytes", static_cast<unsigned long long>(frameTableBytes));
    addField(report, "frameTableComplete", frameTableBytes <= bytes.size() ? "yes" : "no");

    if (frameTableBytes > bytes.size()) {
        report.warnings.push_back("SHP frame table extends past the end of the file.");
        return;
    }

    unsigned int format1Frames = 0;
    unsigned int format3Frames = 0;
    unsigned int format0Frames = 0;
    unsigned int unsupportedFormatFrames = 0;
    unsigned int invalidDimensionFrames = 0;
    unsigned int invalidDataOffsetFrames = 0;
    unsigned int emptyDimensionFrames = 0;
    unsigned int repeatedDataOffsetFrames = 0;
    unsigned int format0RawValidatedFrames = 0;
    unsigned int format0RawInvalidFrames = 0;
    std::size_t format0RawPayloadBytes = 0;
    unsigned int minDataOffset = 0;
    unsigned int maxDataOffset = 0;
    unsigned int lastFrameDataOffset = 0;
    std::vector<ShpFrameHeader> frameHeaders;
    frameHeaders.reserve(frameCount);
    std::ostringstream framePreview;

    for (unsigned int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const std::size_t frameOffset =
            fileHeaderBytes + (static_cast<std::size_t>(frameIndex) * frameHeaderBytes);
        const unsigned int frameOffsetX = readUInt16(bytes, frameOffset);
        const unsigned int frameOffsetY = readUInt16(bytes, frameOffset + 2);
        const unsigned int frameWidth = readUInt16(bytes, frameOffset + 4);
        const unsigned int frameHeight = readUInt16(bytes, frameOffset + 6);
        const unsigned int frameFormat = readUInt16(bytes, frameOffset + 8);
        const unsigned int dataOffset = readUInt32(bytes, frameOffset + 20);
        frameHeaders.push_back({
            frameOffsetX,
            frameOffsetY,
            frameWidth,
            frameHeight,
            frameFormat,
            dataOffset
        });

        if (frameIndex < 8) {
            if (frameIndex > 0) {
                framePreview << "; ";
            }
            framePreview
                << "#" << frameIndex
                << " off=" << frameOffsetX << "," << frameOffsetY
                << " size=" << frameWidth << "x" << frameHeight
                << " fmt=" << frameFormat
                << " data=" << dataOffset;
        }

        if (frameIndex == 0) {
            addField(report, "firstFrameOffsetX", frameOffsetX);
            addField(report, "firstFrameOffsetY", frameOffsetY);
            addField(report, "firstFrameWidth", frameWidth);
            addField(report, "firstFrameHeight", frameHeight);
            addField(report, "firstFrameFormat", frameFormat);
            addField(report, "firstFrameDataOffset", dataOffset);
        } else if (dataOffset == lastFrameDataOffset) {
            ++repeatedDataOffsetFrames;
        }

        if (frameIndex + 1 == frameCount) {
            addField(report, "lastFrameOffsetX", frameOffsetX);
            addField(report, "lastFrameOffsetY", frameOffsetY);
            addField(report, "lastFrameWidth", frameWidth);
            addField(report, "lastFrameHeight", frameHeight);
            addField(report, "lastFrameFormat", frameFormat);
            addField(report, "lastFrameDataOffset", dataOffset);
        }

        if (frameWidth == 0 || frameHeight == 0) {
            ++emptyDimensionFrames;
        }

        if (dataOffset < bytes.size()) {
            if (minDataOffset == 0 || dataOffset < minDataOffset) {
                minDataOffset = dataOffset;
            }

            if (dataOffset > maxDataOffset) {
                maxDataOffset = dataOffset;
            }
        }

        if (frameFormat == 0) {
            ++format0Frames;
        } else if (frameFormat == 1) {
            ++format1Frames;
        } else if (frameFormat == 3) {
            ++format3Frames;
        } else {
            ++unsupportedFormatFrames;
        }

        if (frameWidth == 0 || frameHeight == 0 || frameWidth > 4096 || frameHeight > 4096) {
            ++invalidDimensionFrames;
        }

        if (dataOffset >= bytes.size()) {
            ++invalidDataOffsetFrames;
        }

        lastFrameDataOffset = dataOffset;
    }

    std::vector<unsigned int> sortedDataOffsets;
    sortedDataOffsets.reserve(frameHeaders.size());
    for (const ShpFrameHeader& frame : frameHeaders) {
        if (frame.dataOffset < bytes.size()) {
            sortedDataOffsets.push_back(frame.dataOffset);
        }
    }
    std::sort(sortedDataOffsets.begin(), sortedDataOffsets.end());
    sortedDataOffsets.erase(
        std::unique(sortedDataOffsets.begin(), sortedDataOffsets.end()),
        sortedDataOffsets.end()
    );

    for (const ShpFrameHeader& frame : frameHeaders) {
        if (frame.format != 0) {
            continue;
        }

        std::size_t expectedBytes = 0;
        if (frame.width == 0
            || frame.height == 0
            || frame.width > 4096
            || frame.height > 4096
            || !checkedMul(
                static_cast<std::size_t>(frame.width),
                static_cast<std::size_t>(frame.height),
                expectedBytes
            )
            || frame.dataOffset >= bytes.size()) {
            ++format0RawInvalidFrames;
            continue;
        }

        const std::size_t dataEnd =
            nextFrameDataOffset(sortedDataOffsets, frame.dataOffset, bytes.size());
        if (dataEnd < frame.dataOffset
            || dataEnd - static_cast<std::size_t>(frame.dataOffset) < expectedBytes) {
            ++format0RawInvalidFrames;
            continue;
        }

        ++format0RawValidatedFrames;
        format0RawPayloadBytes += expectedBytes;
    }

    const unsigned int effectiveUnsupportedFormatFrames =
        unsupportedFormatFrames + format0RawInvalidFrames;

    addField(report, "format0Frames", format0Frames);
    addField(report, "format1Frames", format1Frames);
    addField(report, "format3Frames", format3Frames);
    addField(report, "nonFormat0UnsupportedFrames", unsupportedFormatFrames);
    addField(report, "unsupportedFormatFrames", effectiveUnsupportedFormatFrames);
    addField(report, "format0RawValidatedFrames", format0RawValidatedFrames);
    addField(report, "format0RawInvalidFrames", format0RawInvalidFrames);
    addField(
        report,
        "format0RawPayloadBytes",
        static_cast<unsigned long long>(format0RawPayloadBytes)
    );
    addField(
        report,
        "format0RawDecodeStatus",
        format0Frames == 0
            ? "not-needed"
            : (format0RawInvalidFrames == 0 && format0RawValidatedFrames == format0Frames
                ? "validated-raw-indexed-rows"
                : (format0RawValidatedFrames > 0
                    ? "partial-raw-indexed-rows"
                    : "invalid-or-unknown"))
    );
    addField(
        report,
        "decoderSupport",
        effectiveUnsupportedFormatFrames == 0
            ? (format0Frames == 0
                ? "diagnostic-supported"
                : "diagnostic-supported-raw-format0-palette-external")
            : (format0Frames > 0
                && unsupportedFormatFrames == 0
                && format0RawValidatedFrames > 0
                ? "diagnostic-partial-raw-format0"
                : "diagnostic-header-only-mixed-unsupported-formats")
    );
    addField(
        report,
        "format0DecoderPlan",
        format0Frames == 0
            ? "not-needed"
            : "diagnostic raw indexed rows are width*height bytes from each frame data offset; renderer still needs sidebar/cameo palette selection and transparency/remap policy"
    );
    addField(report, "invalidDimensionFrames", invalidDimensionFrames);
    addField(report, "emptyDimensionFrames", emptyDimensionFrames);
    addField(report, "invalidDataOffsetFrames", invalidDataOffsetFrames);
    addField(report, "repeatedDataOffsetFrames", repeatedDataOffsetFrames);
    addField(report, "minFrameDataOffset", minDataOffset);
    addField(report, "maxFrameDataOffset", maxDataOffset);
    addField(report, "framePreview", framePreview.str());

    if (effectiveUnsupportedFormatFrames > 0) {
        report.warnings.push_back(
            format0Frames > 0 && unsupportedFormatFrames == 0
                ? "SHP contains format 0 frames whose raw indexed row payload could not be fully validated; resource exists but renderer decode needs a stricter format0 reader."
                : "SHP contains frames with unsupported formats."
        );
    }

    if (invalidDimensionFrames > 0) {
        report.warnings.push_back("SHP contains frames with invalid dimensions.");
    }

    if (emptyDimensionFrames > 0) {
        report.warnings.push_back("SHP contains frames with empty dimensions.");
    }

    if (invalidDataOffsetFrames > 0) {
        report.warnings.push_back("SHP contains frames with data offsets outside the file.");
    }

    if (repeatedDataOffsetFrames > 0) {
        report.warnings.push_back("SHP contains consecutive frames sharing the same data offset.");
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

void inspectVxl(AssetInspectionReport& report, const std::vector<unsigned char>& bytes) {
    report.kind = "VXL voxel";

    VxlMetadata metadata;
    if (!parseVxlMetadata(bytes, metadata, report.errorMessage)) {
        report.success = false;
        return;
    }

    addField(report, "signature", metadata.signature);

    if (metadata.signature != "Voxel Animation") {
        report.warnings.push_back("VXL signature is not 'Voxel Animation'.");
    }

    addField(report, "headerValue0", metadata.headerValue0);
    addField(report, "sectionCount", metadata.sectionCount);
    addField(report, "sectionCountMirror", metadata.sectionCountMirror);
    addField(report, "bodySize", metadata.bodySize);
    addField(report, "paletteMarker", metadata.paletteMarker);
    addField(report, "embeddedPaletteOffset", static_cast<unsigned long long>(metadata.embeddedPaletteOffset));
    addField(report, "embeddedPaletteColors", 256ULL);
    addField(report, "embeddedPaletteMaxChannel", metadata.embeddedPaletteMaxChannel);
    addField(report, "sectionTableOffset", static_cast<unsigned long long>(metadata.sectionTableOffset));
    addField(report, "sectionRecordBytes", static_cast<unsigned long long>(metadata.sectionRecordBytes));
    addField(report, "sectionNames", formatSectionNames(metadata.sectionNames));

    if (metadata.sectionCount != metadata.sectionCountMirror) {
        report.warnings.push_back("VXL section count mirror does not match section count.");
    }
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

void inspectHva(AssetInspectionReport& report, const std::vector<unsigned char>& bytes) {
    report.kind = "HVA animation";

    HvaMetadata metadata;
    if (!parseHvaMetadata(bytes, metadata, report.errorMessage)) {
        report.success = false;
        return;
    }

    addField(report, "path", metadata.path);
    addField(report, "frameCount", metadata.frameCount);
    addField(report, "sectionCount", metadata.sectionCount);
    addField(report, "sectionNames", formatSectionNames(metadata.sectionNames));
    addField(report, "matrixDataOffset", static_cast<unsigned long long>(metadata.matrixDataOffset));
    addField(report, "expectedFileBytes", static_cast<unsigned long long>(metadata.expectedFileBytes));

    if (metadata.firstMatrix.size() == 12) {
        addField(report, "firstMatrixRow0", formatMatrixRow(metadata.firstMatrix, 0));
        addField(report, "firstMatrixRow1", formatMatrixRow(metadata.firstMatrix, 1));
        addField(report, "firstMatrixRow2", formatMatrixRow(metadata.firstMatrix, 2));
    }

    if (metadata.expectedFileBytes < bytes.size()) {
        report.warnings.push_back("HVA file has extra trailing bytes after declared matrix data.");
    }

    if (metadata.frameCount == 0 || metadata.sectionCount == 0) {
        report.warnings.push_back("HVA frame count or section count is zero.");
    }
}

std::string formatOffsetPreview(const std::vector<std::size_t>& offsets) {
    if (offsets.empty()) {
        return "(none)";
    }

    std::ostringstream output;
    const std::size_t previewCount = std::min<std::size_t>(offsets.size(), 8);

    for (std::size_t i = 0; i < previewCount; ++i) {
        if (i > 0) {
            output << ", ";
        }

        output << offsets[i];
    }

    if (offsets.size() > previewCount) {
        output << ", ...";
    }

    return output.str();
}

std::string formatIndexPreview(const std::vector<std::size_t>& indexes) {
    if (indexes.empty()) {
        return "(none)";
    }

    std::ostringstream output;
    const std::size_t previewCount = std::min<std::size_t>(indexes.size(), 16);

    for (std::size_t i = 0; i < previewCount; ++i) {
        if (i > 0) {
            output << ", ";
        }

        output << indexes[i];
    }

    if (indexes.size() > previewCount) {
        output << ", ...";
    }

    return output.str();
}

std::string formatRange(unsigned int minimum, unsigned int maximum) {
    if (minimum == maximum) {
        return std::to_string(minimum);
    }

    return std::to_string(minimum) + "-" + std::to_string(maximum);
}

void updateDimensionRange(
    TheaterTileMetadata& metadata,
    unsigned int width,
    unsigned int height
) {
    if (!metadata.hasTileDimensions) {
        metadata.minTileWidth = width;
        metadata.maxTileWidth = width;
        metadata.minTileHeight = height;
        metadata.maxTileHeight = height;
        metadata.hasTileDimensions = true;
        return;
    }

    metadata.minTileWidth = std::min(metadata.minTileWidth, width);
    metadata.maxTileWidth = std::max(metadata.maxTileWidth, width);
    metadata.minTileHeight = std::min(metadata.minTileHeight, height);
    metadata.maxTileHeight = std::max(metadata.maxTileHeight, height);
}

void finalizeTileDataRange(TheaterTileMetadata& metadata, std::size_t fileSize) {
    if (metadata.presentOffsets.empty()) {
        return;
    }

    std::vector<std::size_t> sortedOffsets = metadata.presentOffsets;
    std::sort(sortedOffsets.begin(), sortedOffsets.end());

    metadata.firstDataOffset = sortedOffsets.front();

    if (sortedOffsets.size() >= 2 && sortedOffsets[1] > sortedOffsets[0]) {
        metadata.firstTileBytes = sortedOffsets[1] - sortedOffsets[0];
    } else if (metadata.firstDataOffset < fileSize) {
        metadata.firstTileBytes = fileSize - metadata.firstDataOffset;
    }
}

bool parseWrappedTileSet(
    const std::vector<unsigned char>& bytes,
    TheaterTileMetadata& metadata
) {
    constexpr std::size_t headerBytes = 16;
    constexpr std::size_t offsetBytes = 4;

    if (bytes.size() < headerBytes + offsetBytes) {
        return false;
    }

    const unsigned int xBlocks = readUInt32(bytes, 0);
    const unsigned int yBlocks = readUInt32(bytes, 4);
    const unsigned int blockWidth = readUInt32(bytes, 8);
    const unsigned int blockHeight = readUInt32(bytes, 12);

    if (
        xBlocks == 0
        || yBlocks == 0
        || xBlocks > 64
        || yBlocks > 64
        || blockWidth == 0
        || blockHeight == 0
        || blockWidth > 512
        || blockHeight > 512
    ) {
        return false;
    }

    std::size_t slotCount = 0;
    if (!checkedMul(static_cast<std::size_t>(xBlocks), static_cast<std::size_t>(yBlocks), slotCount)) {
        return false;
    }

    std::size_t tableBytes = 0;
    if (!checkedMul(slotCount, offsetBytes, tableBytes)) {
        return false;
    }

    std::size_t tableEnd = 0;
    if (!checkedAdd(headerBytes, tableBytes, tableEnd) || tableEnd > bytes.size()) {
        return false;
    }

    std::vector<std::size_t> offsets;
    offsets.reserve(slotCount);
    std::vector<std::size_t> slots;
    slots.reserve(slotCount);

    for (std::size_t i = 0; i < slotCount; ++i) {
        const unsigned int offset = readUInt32(bytes, headerBytes + (i * offsetBytes));

        if (offset == 0) {
            continue;
        }

        if (offset < tableEnd || offset >= bytes.size()) {
            return false;
        }

        offsets.push_back(static_cast<std::size_t>(offset));
        slots.push_back(i);
    }

    metadata.layout = "offset-table tile set";
    metadata.slotCount = slotCount;
    metadata.presentTileCount = offsets.size();
    metadata.emptySlotCount = slotCount - offsets.size();
    metadata.tableOffset = headerBytes;
    metadata.tableBytes = tableBytes;
    metadata.xBlocks = xBlocks;
    metadata.yBlocks = yBlocks;
    metadata.blockWidth = blockWidth;
    metadata.blockHeight = blockHeight;
    metadata.presentOffsets = offsets;
    metadata.presentSlots = slots;

    updateDimensionRange(metadata, blockWidth, blockHeight);
    finalizeTileDataRange(metadata, bytes.size());
    return true;
}

bool parseRawTileSlotTable(
    const std::vector<unsigned char>& bytes,
    TheaterTileMetadata& metadata
) {
    constexpr std::size_t preludeBytes = 8;
    constexpr std::size_t slotRecordBytes = 24;
    constexpr std::size_t dataOffsetOffset = 20;

    if (bytes.size() < preludeBytes + slotRecordBytes) {
        return false;
    }

    std::size_t firstDataOffset = 0;

    for (
        std::size_t offsetField = preludeBytes + dataOffsetOffset;
        offsetField + 4 <= bytes.size();
        offsetField += slotRecordBytes
    ) {
        const unsigned int candidate = readUInt32(bytes, offsetField);

        if (
            candidate > preludeBytes
            && candidate < bytes.size()
            && ((static_cast<std::size_t>(candidate) - preludeBytes) % slotRecordBytes) == 0
            && (firstDataOffset == 0 || candidate < firstDataOffset)
        ) {
            firstDataOffset = static_cast<std::size_t>(candidate);
        }
    }

    if (firstDataOffset == 0 || firstDataOffset <= preludeBytes) {
        return false;
    }

    const std::size_t tableBytes = firstDataOffset - preludeBytes;
    if (tableBytes % slotRecordBytes != 0) {
        return false;
    }

    const std::size_t slotCount = tableBytes / slotRecordBytes;
    if (slotCount == 0 || slotCount > 4096) {
        return false;
    }

    std::vector<std::size_t> offsets;
    offsets.reserve(slotCount);
    std::vector<std::size_t> slots;
    slots.reserve(slotCount);

    TheaterTileMetadata parsed;
    parsed.layout = "raw tile slot table";
    parsed.slotCount = slotCount;
    parsed.tableOffset = preludeBytes;
    parsed.tableBytes = tableBytes;
    parsed.slotRecordBytes = slotRecordBytes;

    for (std::size_t i = 0; i < slotCount; ++i) {
        const std::size_t recordOffset = preludeBytes + (i * slotRecordBytes);
        const unsigned int packedDimensions = readUInt32(bytes, recordOffset + 4);
        const unsigned int width = packedDimensions & 0xFFFFU;
        const unsigned int height = (packedDimensions >> 16) & 0xFFFFU;
        const unsigned int dataOffset = readUInt32(bytes, recordOffset + dataOffsetOffset);

        if (dataOffset == 0) {
            continue;
        }

        if (dataOffset < firstDataOffset || dataOffset >= bytes.size()) {
            return false;
        }

        offsets.push_back(static_cast<std::size_t>(dataOffset));
        slots.push_back(i);

        if (width > 0 && height > 0) {
            updateDimensionRange(parsed, width, height);
        }
    }

    parsed.presentTileCount = offsets.size();
    parsed.emptySlotCount = slotCount - offsets.size();
    parsed.presentOffsets = offsets;
    parsed.presentSlots = slots;

    finalizeTileDataRange(parsed, bytes.size());

    if (parsed.presentTileCount == 0) {
        return false;
    }

    metadata = parsed;
    return true;
}

void inspectTileLike(AssetInspectionReport& report, const std::vector<unsigned char>& bytes) {
    report.kind = "TMP/theater tile";

    if (bytes.size() < 16) {
        report.success = false;
        report.errorMessage = "Tile file is too small for a basic header.";
        return;
    }

    TheaterTileMetadata metadata;
    if (parseWrappedTileSet(bytes, metadata) || parseRawTileSlotTable(bytes, metadata)) {
        addField(report, "layout", metadata.layout);
        addField(report, "paletteModel", "indexed-theater");
        addField(report, "slotCount", static_cast<unsigned long long>(metadata.slotCount));
        addField(report, "presentTiles", static_cast<unsigned long long>(metadata.presentTileCount));
        addField(report, "emptySlots", static_cast<unsigned long long>(metadata.emptySlotCount));
        addField(
            report,
            "slotCoverage",
            std::to_string(metadata.presentTileCount) + "/"
                + std::to_string(metadata.slotCount)
        );
        addField(report, "tableOffset", static_cast<unsigned long long>(metadata.tableOffset));
        addField(report, "tableBytes", static_cast<unsigned long long>(metadata.tableBytes));

        if (metadata.slotRecordBytes > 0) {
            addField(report, "slotRecordBytes", static_cast<unsigned long long>(metadata.slotRecordBytes));
        }

        if (metadata.xBlocks > 0 || metadata.yBlocks > 0) {
            addField(report, "xBlocks", metadata.xBlocks);
            addField(report, "yBlocks", metadata.yBlocks);
            addField(report, "blockWidth", metadata.blockWidth);
            addField(report, "blockHeight", metadata.blockHeight);
        }

        if (metadata.hasTileDimensions) {
            addField(
                report,
                "tileWidthRange",
                formatRange(metadata.minTileWidth, metadata.maxTileWidth)
            );
            addField(
                report,
                "tileHeightRange",
                formatRange(metadata.minTileHeight, metadata.maxTileHeight)
            );
        }

        addField(report, "firstDataOffset", static_cast<unsigned long long>(metadata.firstDataOffset));
        addField(report, "firstTileBytes", static_cast<unsigned long long>(metadata.firstTileBytes));
        addField(report, "firstDataOffsets", formatOffsetPreview(metadata.presentOffsets));
        addField(report, "presentSlots", formatIndexPreview(metadata.presentSlots));
        if (!metadata.presentSlots.empty()) {
            addField(report, "firstPresentSlot", static_cast<unsigned long long>(metadata.presentSlots.front()));
        }

        if (metadata.presentTileCount == 0) {
            report.warnings.push_back("Tile set has no present tile data offsets.");
        }

        if (metadata.emptySlotCount > 0) {
            report.warnings.push_back("Tile set has empty sub-tile slots; renderers may need frame fallback.");
        }

        if (metadata.firstTileBytes == 0) {
            report.warnings.push_back("Tile set first present tile has no measurable byte range.");
        }

        return;
    }

    addField(report, "layout", "unknown");
    addField(report, "dword0", readUInt32(bytes, 0));
    addField(report, "dword1", readUInt32(bytes, 4));
    addField(report, "dword2", readUInt32(bytes, 8));
    addField(report, "dword3", readUInt32(bytes, 12));

    if (bytes.size() >= 32) {
        addField(report, "dword4", readUInt32(bytes, 16));
        addField(report, "dword5", readUInt32(bytes, 20));
        addField(report, "dword6", readUInt32(bytes, 24));
        addField(report, "dword7", readUInt32(bytes, 28));
    }

    report.warnings.push_back(
        "Tile layout is not recognized yet; this phase only reports header dwords."
    );
}

void inspectUnknown(AssetInspectionReport& report, const std::vector<unsigned char>& bytes) {
    report.kind = "unknown";

    const std::size_t previewCount = std::min<std::size_t>(bytes.size(), 16);
    std::ostringstream preview;
    for (std::size_t i = 0; i < previewCount; ++i) {
        if (i > 0) {
            preview << " ";
        }
        preview << formatHex(bytes[i], 2);
    }

    addField(report, "firstBytes", preview.str());
    report.warnings.push_back("No asset inspector is registered for this extension.");
}

void inspectPcxUnsupported(
    AssetInspectionReport& report,
    const std::vector<unsigned char>& bytes
) {
    report.kind = "PCX image";
    std::string decoderSupport = "diagnostic-header-only";
    bool pcxDecodeAttempted = false;
    bool pcxDecodeValidated = false;
    std::string pcxDecodeMessage = "not-attempted";
    if (bytes.size() >= 4) {
        addField(report, "manufacturer", formatHex(bytes[0], 2));
        addField(report, "version", formatHex(bytes[1], 2));
        addField(report, "encoding", formatHex(bytes[2], 2));
        addField(report, "bitsPerPixel", static_cast<unsigned int>(bytes[3]));
    }
    if (bytes.size() >= 128) {
        const unsigned int xmin = readUInt16(bytes, 4);
        const unsigned int ymin = readUInt16(bytes, 6);
        const unsigned int xmax = readUInt16(bytes, 8);
        const unsigned int ymax = readUInt16(bytes, 10);
        const unsigned int width = xmax >= xmin ? xmax - xmin + 1U : 0U;
        const unsigned int height = ymax >= ymin ? ymax - ymin + 1U : 0U;
        const unsigned int colorPlanes = bytes[65];
        const unsigned int bytesPerLine = readUInt16(bytes, 66);
        const unsigned int paletteType = readUInt16(bytes, 68);
        const bool hasTrailingVgaPalette =
            bytes.size() >= 769 && bytes[bytes.size() - 769] == 0x0c;
        const bool indexed8Candidate = bytes[3] == 8 && colorPlanes == 1;

        addField(report, "xmin", xmin);
        addField(report, "ymin", ymin);
        addField(report, "xmax", xmax);
        addField(report, "ymax", ymax);
        addField(report, "width", width);
        addField(report, "height", height);
        addField(report, "colorPlanes", colorPlanes);
        addField(report, "bytesPerLine", bytesPerLine);
        addField(report, "paletteType", paletteType);
        addField(report, "hasTrailingVgaPalette", hasTrailingVgaPalette ? "yes" : "no");
        addField(
            report,
            "indexed8Candidate",
            indexed8Candidate ? "yes" : "no"
        );
        if (indexed8Candidate
            && width > 0
            && height > 0
            && bytesPerLine >= width
            && bytesPerLine <= 8192
            && height <= 8192) {
            std::size_t expectedBytes = 0;
            if (checkedMul(
                    static_cast<std::size_t>(bytesPerLine),
                    static_cast<std::size_t>(height),
                    expectedBytes
                )) {
                pcxDecodeAttempted = true;
                const std::size_t dataBegin = 128;
                const std::size_t dataEnd =
                    hasTrailingVgaPalette ? bytes.size() - 769 : bytes.size();
                std::vector<unsigned char> decoded;
                decoded.reserve(std::min<std::size_t>(expectedBytes, 1024 * 1024));
                std::size_t position = dataBegin;
                while (position < dataEnd && decoded.size() < expectedBytes) {
                    const unsigned char value = bytes[position++];
                    if ((value & 0xc0U) == 0xc0U) {
                        const unsigned int count = value & 0x3fU;
                        if (position >= dataEnd) {
                            pcxDecodeMessage = "truncated-rle-run";
                            break;
                        }
                        const unsigned char repeated = bytes[position++];
                        for (unsigned int repeat = 0;
                             repeat < count && decoded.size() < expectedBytes;
                             ++repeat) {
                            decoded.push_back(repeated);
                        }
                    } else {
                        decoded.push_back(value);
                    }
                }
                pcxDecodeValidated = decoded.size() == expectedBytes;
                if (pcxDecodeValidated) {
                    decoderSupport =
                        hasTrailingVgaPalette
                            ? "diagnostic-supported-pcx-rle-indexed8-trailing-vga-palette"
                            : "diagnostic-supported-pcx-rle-indexed8-palette-external";
                    pcxDecodeMessage = "validated-rle-indexed-rows";
                    if (!decoded.empty()) {
                        addField(report, "firstPixelIndex", static_cast<unsigned int>(decoded.front()));
                    }
                    unsigned int nonZeroPreviewPixels = 0;
                    const std::size_t previewCount =
                        std::min<std::size_t>(decoded.size(), static_cast<std::size_t>(256));
                    for (std::size_t index = 0; index < previewCount; ++index) {
                        if (decoded[index] != 0) {
                            ++nonZeroPreviewPixels;
                        }
                    }
                    addField(report, "nonZeroPreviewPixels256", nonZeroPreviewPixels);
                } else if (pcxDecodeMessage == "not-attempted") {
                    pcxDecodeMessage =
                        "decoded-bytes="
                        + std::to_string(decoded.size())
                        + "/"
                        + std::to_string(expectedBytes);
                }
                addField(report, "pcxRleDecodedBytes", static_cast<unsigned long long>(decoded.size()));
                addField(report, "pcxRleExpectedBytes", static_cast<unsigned long long>(expectedBytes));
            } else {
                pcxDecodeMessage = "expected-byte-count-overflow";
            }
        } else {
            pcxDecodeMessage =
                indexed8Candidate
                    ? "unsupported-or-invalid-dimensions"
                    : "not-indexed8-single-plane";
        }
    } else {
        pcxDecodeMessage = "header-shorter-than-128-bytes";
    }
    addField(report, "decoderSupport", decoderSupport);
    addField(report, "pcxRleDecodeAttempted", pcxDecodeAttempted ? "yes" : "no");
    addField(report, "pcxRleDecodeStatus", pcxDecodeValidated ? "validated" : pcxDecodeMessage);
    addField(
        report,
        "pcxDecoderPlan",
        pcxDecodeValidated
            ? "diagnostic PCX RLE indexed rows are available; renderer still needs sidebar/cameo palette selection and transparency/remap policy"
            : "unpack PCX RLE per scanline using bytesPerLine/colorPlanes, then apply trailing VGA palette when present or sidebar/cameo PAL candidate"
    );
    if (!pcxDecodeValidated) {
        report.warnings.push_back(
            "PCX resource exists but full pixel decode is not validated; header/RLE diagnostics are available for decoder implementation."
        );
    }
}

unsigned char scalePaletteChannel(unsigned char value, bool sixBitPalette) {
    if (!sixBitPalette) {
        return value;
    }

    return static_cast<unsigned char>(
        (static_cast<unsigned int>(value) * 255U + 31U) / 63U
    );
}

bool decodeRa2Palette(
    const std::vector<unsigned char>& bytes,
    std::vector<RgbColor>& colors,
    std::string& errorMessage
) {
    if (bytes.size() != 768) {
        errorMessage = "PAL file must be exactly 768 bytes for 256 RGB colors.";
        return false;
    }

    colors.clear();
    colors.reserve(256);

    unsigned int maxChannel = 0;
    for (const unsigned char value : bytes) {
        maxChannel = std::max(maxChannel, static_cast<unsigned int>(value));
    }

    const bool sixBitPalette = maxChannel <= 63;

    for (std::size_t i = 0; i < 256; ++i) {
        const std::size_t offset = i * 3;

        RgbColor color;
        color.red = scalePaletteChannel(bytes[offset], sixBitPalette);
        color.green = scalePaletteChannel(bytes[offset + 1], sixBitPalette);
        color.blue = scalePaletteChannel(bytes[offset + 2], sixBitPalette);

        colors.push_back(color);
    }

    return true;
}

bool writePpm(
    const std::string& outputPath,
    unsigned int width,
    unsigned int height,
    const std::vector<unsigned char>& pixelIndexes,
    const std::vector<RgbColor>& palette,
    std::string& errorMessage
) {
    const std::filesystem::path path(outputPath);
    const std::filesystem::path parent = path.parent_path();

    if (!parent.empty()) {
        try {
            std::filesystem::create_directories(parent);
        } catch (const std::exception& exception) {
            errorMessage = "Failed to create output directory: ";
            errorMessage += exception.what();
            return false;
        }
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.is_open()) {
        errorMessage = "Failed to open PPM output file.";
        return false;
    }

    output << "P6\n" << width << " " << height << "\n255\n";

    for (const unsigned char paletteIndex : pixelIndexes) {
        const RgbColor& color = palette[paletteIndex];
        output.write(reinterpret_cast<const char*>(&color.red), 1);
        output.write(reinterpret_cast<const char*>(&color.green), 1);
        output.write(reinterpret_cast<const char*>(&color.blue), 1);
    }

    if (!output.good()) {
        errorMessage = "Failed while writing PPM output file.";
        return false;
    }

    return true;
}

bool decodeRawTmpFormat3PixelsForExport(
    const std::vector<unsigned char>& bytes,
    std::size_t dataOffset,
    std::size_t dataEnd,
    unsigned int width,
    unsigned int height,
    std::vector<unsigned char>& pixels,
    std::string& errorMessage
) {
    if (dataOffset >= dataEnd || dataEnd > bytes.size()) {
        errorMessage = "TMP raw image data bounds are invalid.";
        return false;
    }

    pixels.assign(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        0
    );

    std::size_t cursor = dataOffset;
    for (unsigned int y = 0; y < height; ++y) {
        if (cursor + 2 > dataEnd) {
            errorMessage = "TMP raw format 3 row table ended early.";
            return false;
        }

        const std::size_t rowStart = cursor;
        const std::size_t rowLength = readUInt16(bytes, cursor);
        if (rowLength < 2) {
            errorMessage = "TMP raw format 3 row has an invalid length.";
            return false;
        }

        const std::size_t rowEnd = rowStart + rowLength;
        if (rowEnd < rowStart || rowEnd > dataEnd) {
            errorMessage = "TMP raw format 3 row extends past the image record.";
            return false;
        }

        cursor += 2;
        unsigned int x = 0;
        while (cursor < rowEnd) {
            const unsigned char value = bytes[cursor++];
            if (value != 0) {
                if (x >= width) {
                    errorMessage = "TMP raw format 3 row contains too many opaque pixels.";
                    return false;
                }

                pixels[(static_cast<std::size_t>(y) * width) + x] = value;
                ++x;
                continue;
            }

            if (cursor >= rowEnd) {
                errorMessage = "TMP raw format 3 transparent run is truncated.";
                return false;
            }

            unsigned int runLength = bytes[cursor++];
            if (x + runLength > width) {
                runLength = width - x;
            }
            x += runLength;
        }

        cursor = rowEnd;
    }

    return true;
}

bool exportRawTmpFirstTileToPpm(
    const std::vector<unsigned char>& tmpBytes,
    const TheaterTileMetadata& metadata,
    const std::vector<RgbColor>& palette,
    const std::string& outputPath,
    std::string& errorMessage
) {
    if (metadata.presentOffsets.empty() || metadata.presentSlots.empty()) {
        errorMessage = "TMP raw file has no present sub-tile images.";
        return false;
    }

    constexpr std::size_t preludeBytes = 8;
    constexpr std::size_t dataOffsetOffset = 20;
    const std::size_t slotRecordBytes =
        metadata.slotRecordBytes == 0 ? 24 : metadata.slotRecordBytes;
    const std::size_t slotIndex = metadata.presentSlots.front();
    const std::size_t recordOffset = preludeBytes + (slotIndex * slotRecordBytes);
    if (recordOffset + dataOffsetOffset + 4 > tmpBytes.size()) {
        errorMessage = "TMP raw first slot record is outside file.";
        return false;
    }

    const unsigned int packedDimensions = readUInt32(tmpBytes, recordOffset + 4);
    const unsigned int width = packedDimensions & 0xFFFFU;
    const unsigned int height = (packedDimensions >> 16) & 0xFFFFU;
    const unsigned int dataOffset = readUInt32(tmpBytes, recordOffset + dataOffsetOffset);
    if (width == 0 || height == 0 || width > 512 || height > 512) {
        errorMessage = "TMP raw first slot dimensions are unsupported.";
        return false;
    }

    std::size_t dataEnd = tmpBytes.size();
    for (const std::size_t candidate : metadata.presentOffsets) {
        if (candidate > dataOffset && candidate < dataEnd) {
            dataEnd = candidate;
        }
    }

    std::vector<unsigned char> pixelIndexes;
    if (!decodeRawTmpFormat3PixelsForExport(
            tmpBytes,
            static_cast<std::size_t>(dataOffset),
            dataEnd,
            width,
            height,
            pixelIndexes,
            errorMessage
        )) {
        return false;
    }

    return writePpm(outputPath, width, height, pixelIndexes, palette, errorMessage);
}

bool sectionNamesMatch(
    const std::vector<std::string>& left,
    const std::vector<std::string>& right
) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }

    return true;
}

struct CapabilityOptions {
    std::string rulesPath;
    std::string artPath;
    std::string assetRootPath;
    std::string mapPath;
    std::string theaterIniPath;
    std::string tileRootPath;
    std::string reportMode = "summary";
    std::string theater = "NEWURBAN";
    std::vector<std::string> objectIds;
    bool includeUi = true;
    bool verbose = false;
};

struct CapabilityMixEntryRef {
    std::size_t archiveIndex = 0;
    std::size_t entryIndex = 0;
    std::uint32_t id = 0;
    std::uint32_t size = 0;
};

struct CapabilityResourceIndex {
    std::unordered_map<std::string, std::vector<std::string>> filesByName;
    std::unordered_map<std::uint32_t, std::vector<CapabilityMixEntryRef>> mixEntriesById;
    std::unordered_map<std::string, std::size_t> mixArchiveByPath;
    std::vector<MixArchive> mixArchives;
    std::vector<std::string> warnings;
    std::size_t looseResourceCount = 0;
    std::size_t mixEntryRefCount = 0;
};

struct CapabilityResourceIndexBuild {
    const CapabilityResourceIndex* index = nullptr;
    std::string cacheKey;
    std::string cacheStatus;
    std::size_t processCacheEntries = 0;
    bool processCacheHit = false;
};

struct CapabilityProbe {
    bool found = false;
    bool decoded = false;
    bool unsupported = false;
    std::string label;
    std::string source;
    std::string decodeSummary;
    std::string paletteStatus;
    std::string candidateSummary;
};

struct CapabilityCoverageCounter {
    unsigned int checked = 0;
    unsigned int decodable = 0;
    unsigned int unsupported = 0;
    unsigned int missing = 0;
    std::vector<std::string> sources;
};

struct CapabilityDecodeCacheEntry {
    bool decoded = false;
    bool unsupported = false;
    std::string decodeSummary;
};

struct CapabilityResolvedResource {
    bool found = false;
    bool loose = false;
    std::string candidate;
    std::string loosePath;
    std::size_t mixArchiveIndex = static_cast<std::size_t>(-1);
    std::size_t mixEntryIndex = static_cast<std::size_t>(-1);
};

struct CapabilityReportState {
    AssetInspectionReport& report;
    const CapabilityResourceIndex& index;
    const RulesDatabase& rules;
    const ArtDatabase& art;
    std::string theater;
    bool verbose = false;
    std::unordered_map<std::string, CapabilityDecodeCacheEntry> decodeCache;
    std::unordered_map<std::string, CapabilityResolvedResource> resolutionCache;
    std::set<std::string> summaryProblemKeys;
    unsigned int requiredResourceCount = 0;
    unsigned int presentDecodableCount = 0;
    unsigned int presentUnsupportedCount = 0;
    unsigned int missingCount = 0;
    unsigned int logicalCount = 0;
    unsigned int suppressedDetailCount = 0;
    unsigned int suppressedDuplicateProblemCount = 0;
    unsigned int resourceIndexHits = 0;
    unsigned int resourceIndexMisses = 0;
    unsigned int resolutionCacheHits = 0;
    unsigned int resolutionCacheMisses = 0;
    unsigned int decodeCacheHits = 0;
    unsigned int decodeCacheMisses = 0;
    CapabilityCoverageCounter uiSidebarCoverage;
    CapabilityCoverageCounter uiPowerCoverage;
    CapabilityCoverageCounter uiRadarCoverage;
    CapabilityCoverageCounter uiPipsCoverage;
    CapabilityCoverageCounter uiCameoCoverage;
    CapabilityCoverageCounter uiPaletteCoverage;
    CapabilityCoverageCounter uiStandaloneHealthCoverage;
};

std::string trimWhitespace(const std::string& text) {
    const char* whitespace = " \t\r\n";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

std::string stripOuterQuotes(const std::string& text) {
    std::string result = trimWhitespace(text);
    if (result.size() >= 2
        && ((result.front() == '"' && result.back() == '"')
            || (result.front() == '\'' && result.back() == '\''))) {
        result = trimWhitespace(result.substr(1, result.size() - 2));
    }
    return result;
}

bool startsWithCapabilityPrefix(const std::string& path) {
    return toLower(stripOuterQuotes(path)).rfind("capability://", 0) == 0;
}

std::string decodeCapabilityValue(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (value[i] == '%' && i + 2 < value.size()) {
            const auto hexValue = [](char character) -> int {
                if (character >= '0' && character <= '9') {
                    return character - '0';
                }
                if (character >= 'a' && character <= 'f') {
                    return 10 + character - 'a';
                }
                if (character >= 'A' && character <= 'F') {
                    return 10 + character - 'A';
                }
                return -1;
            };
            const int high = hexValue(value[i + 1]);
            const int low = hexValue(value[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }

        decoded.push_back(value[i]);
    }

    return decoded;
}

std::vector<std::string> splitCapabilityList(
    const std::string& value,
    const std::string& separators = ","
) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find_first_of(separators, start);
        const std::string token = trimWhitespace(value.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start
        ));
        if (!token.empty() && toLower(token) != "none") {
            result.push_back(token);
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return result;
}

std::map<std::string, std::string> parseCapabilityParameters(
    const std::string& path
) {
    std::map<std::string, std::string> parameters;
    const std::string normalizedPath = stripOuterQuotes(path);
    std::string payload = normalizedPath.substr(std::string("capability://").size());
    if (!payload.empty() && payload.front() == '?') {
        payload.erase(payload.begin());
    }

    std::size_t start = 0;
    while (start <= payload.size()) {
        const std::size_t end = payload.find_first_of(";&", start);
        const std::string token = payload.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start
        );
        const std::size_t equals = token.find('=');
        if (equals != std::string::npos) {
            const std::string key = toLower(trimWhitespace(token.substr(0, equals)));
            const std::string value = decodeCapabilityValue(
                trimWhitespace(token.substr(equals + 1))
            );
            if (!key.empty()) {
                parameters[key] = value;
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return parameters;
}

CapabilityOptions parseCapabilityOptions(const std::string& path) {
    const std::map<std::string, std::string> parameters =
        parseCapabilityParameters(path);

    CapabilityOptions options;
    const auto readParameter = [&](const std::string& name) -> std::string {
        const auto iterator = parameters.find(name);
        return iterator == parameters.end() ? "" : iterator->second;
    };

    options.rulesPath = readParameter("rules");
    options.artPath = readParameter("art");
    options.assetRootPath = readParameter("assetroot");
    if (options.assetRootPath.empty()) {
        options.assetRootPath = readParameter("asset-root");
    }
    options.mapPath = readParameter("map");
    options.theaterIniPath = readParameter("theaterini");
    if (options.theaterIniPath.empty()) {
        options.theaterIniPath = readParameter("theater-ini");
    }
    options.tileRootPath = readParameter("tileroot");
    if (options.tileRootPath.empty()) {
        options.tileRootPath = readParameter("tile-root");
    }

    const std::string theater = readParameter("theater");
    if (!theater.empty()) {
        options.theater = theater;
    }

    std::string mode = toLower(readParameter("mode"));
    if (mode.empty()) {
        mode = toLower(readParameter("detail"));
    }
    const std::string verbose = toLower(readParameter("verbose"));
    if (mode == "verbose" || mode == "full" || mode == "detail"
        || verbose == "1" || verbose == "yes" || verbose == "true") {
        options.reportMode = "verbose";
        options.verbose = true;
    }

    options.objectIds = splitCapabilityList(readParameter("objects"));
    if (options.objectIds.empty()) {
        options.objectIds = {"E1", "GGI", "FV", "MTNK", "GAPOWR", "GAPILE", "GAWEAP"};
    }

    const std::string ui = toLower(readParameter("ui"));
    options.includeUi = ui.empty() || ui == "1" || ui == "yes" || ui == "true";
    return options;
}

std::string joinStrings(
    const std::vector<std::string>& values,
    const std::string& separator = ","
) {
    if (values.empty()) {
        return "(none)";
    }

    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            output << separator;
        }
        output << values[i];
    }
    return output.str();
}

void addUniqueString(std::vector<std::string>& values, const std::string& value) {
    const std::string trimmed = trimWhitespace(value);
    if (trimmed.empty() || toLower(trimmed) == "none") {
        return;
    }

    const std::string lower = toLower(trimmed);
    for (const std::string& existing : values) {
        if (toLower(existing) == lower) {
            return;
        }
    }
    values.push_back(trimmed);
}

std::string normalizeResourceBaseName(const std::string& baseName) {
    std::string result = trimWhitespace(baseName);
    if (result.size() >= 2
        && ((result.front() == '"' && result.back() == '"')
            || (result.front() == '\'' && result.back() == '\''))) {
        result = trimWhitespace(result.substr(1, result.size() - 2));
    }

    const std::size_t separator = result.find_last_of("/\\");
    if (separator != std::string::npos) {
        result = result.substr(separator + 1);
    }

    return result;
}

std::vector<std::string> resourceCandidates(
    const std::string& baseName,
    const std::vector<std::string>& extensions
) {
    const std::string normalized = normalizeResourceBaseName(baseName);
    std::vector<std::string> candidates;
    if (normalized.empty()) {
        return candidates;
    }

    const std::filesystem::path path(normalized);
    if (!path.extension().empty()) {
        addUniqueString(candidates, toLower(normalized));
        return candidates;
    }

    for (const std::string& extension : extensions) {
        addUniqueString(candidates, toLower(normalized + extension));
    }
    return candidates;
}

std::vector<std::string> shpCandidates(const std::string& baseName) {
    return resourceCandidates(baseName, {".shp"});
}

std::vector<std::string> graphicCandidates(const std::string& baseName) {
    return resourceCandidates(baseName, {".shp", ".vxl", ".hva"});
}

std::vector<std::string> paletteCandidates(const std::string& baseName) {
    return resourceCandidates(baseName, {".pal"});
}

std::vector<std::string> tmpCandidates(
    const std::string& baseName,
    const std::string& theater
) {
    std::vector<std::string> extensions;
    const std::string lower = toLower(theater);
    if (lower == "newurban") {
        extensions.push_back(".ubn");
    } else if (lower == "desert") {
        extensions.push_back(".des");
    } else if (lower == "urban") {
        extensions.push_back(".urb");
    } else if (lower == "snow") {
        extensions.push_back(".sno");
    } else if (lower == "lunar") {
        extensions.push_back(".lun");
    } else {
        extensions.push_back(".tem");
    }

    for (const std::string& extension : {".tem", ".sno", ".urb", ".ubn", ".des", ".lun", ".tmp", ".shp"}) {
        addUniqueString(extensions, extension);
    }

    return resourceCandidates(baseName, extensions);
}

std::vector<std::string> unitPaletteCandidatesForTheater(const std::string& theater) {
    const std::string lower = toLower(theater);
    if (lower == "temperate") {
        return {"cachemd_unittem.pal", "cache_unittem.pal", "unittem.pal"};
    }
    if (lower == "snow") {
        return {"cachemd_unitsno.pal", "cache_unitsno.pal", "unitsno.pal"};
    }
    if (lower == "urban") {
        return {"cachemd_uniturb.pal", "cache_uniturb.pal", "uniturb.pal"};
    }
    if (lower == "newurban") {
        return {"cachemd_unitubn.pal", "cache_unitubn.pal", "unitubn.pal"};
    }
    if (lower == "desert") {
        return {"cachemd_unitdes.pal", "cache_unitdes.pal", "unitdes.pal"};
    }
    if (lower == "lunar") {
        return {"cachemd_unitlun.pal", "cache_unitlun.pal", "unitlun.pal"};
    }
    return {};
}

std::vector<std::string> isoPaletteCandidatesForTheater(const std::string& theater) {
    const std::string lower = toLower(theater);
    if (lower == "temperate") {
        return {"isotem.pal", "cachemd_isotem.pal", "cache_isotem.pal"};
    }
    if (lower == "snow") {
        return {"isosno.pal", "cachemd_isosno.pal", "cache_isosno.pal"};
    }
    if (lower == "urban") {
        return {"isourb.pal", "cachemd_isourb.pal", "cache_isourb.pal"};
    }
    if (lower == "newurban") {
        return {"isoubn.pal", "cachemd_isoubn.pal", "cache_isoubn.pal"};
    }
    if (lower == "desert") {
        return {"isodes.pal", "cachemd_isodes.pal", "cache_isodes.pal"};
    }
    if (lower == "lunar") {
        return {"isolun.pal", "cachemd_isolun.pal", "cache_isolun.pal"};
    }
    return {};
}

std::vector<std::string> projectilePaletteCandidatesForTheater(
    const std::string& theater
) {
    std::vector<std::string> candidates;
    for (const std::string& candidate : unitPaletteCandidatesForTheater(theater)) {
        addUniqueString(candidates, candidate);
    }
    addUniqueString(candidates, "anim.pal");
    for (const std::string& candidate : isoPaletteCandidatesForTheater(theater)) {
        addUniqueString(candidates, candidate);
    }
    return candidates;
}

AssetInspectionReport inspectVirtualAssetBytes(
    const std::string& path,
    const std::string& extension,
    const std::vector<unsigned char>& bytes
) {
    AssetInspectionReport report;
    report.path = path;
    report.extension = toLower(extension);
    report.fileSize = static_cast<unsigned long long>(bytes.size());
    report.success = true;

    if (report.extension == ".pal") {
        inspectPalette(report, bytes);
    } else if (report.extension == ".shp") {
        inspectShp(report, bytes);
    } else if (report.extension == ".vxl") {
        inspectVxl(report, bytes);
    } else if (report.extension == ".hva") {
        inspectHva(report, bytes);
    } else if (report.extension == ".pcx") {
        inspectPcxUnsupported(report, bytes);
    } else if (
        report.extension == ".tmp"
        || report.extension == ".tem"
        || report.extension == ".sno"
        || report.extension == ".urb"
        || report.extension == ".des"
        || report.extension == ".lun"
        || report.extension == ".ubn"
    ) {
        inspectTileLike(report, bytes);
    } else {
        inspectUnknown(report, bytes);
    }

    return report;
}

std::string capabilityPathKey(const std::string& path) {
    return toLower(std::filesystem::path(path).lexically_normal().string());
}

std::string capabilityResourceIndexCacheKey(const std::string& assetRootPath) {
    std::error_code errorCode;
    const std::filesystem::path root(assetRootPath);
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(root, errorCode);
    if (!errorCode && !canonical.empty()) {
        return toLower(canonical.lexically_normal().string());
    }

    errorCode.clear();
    const std::filesystem::path absolute =
        std::filesystem::absolute(root, errorCode);
    if (!errorCode && !absolute.empty()) {
        return toLower(absolute.lexically_normal().string());
    }

    return toLower(root.lexically_normal().string());
}

CapabilityResourceIndex buildCapabilityResourceIndexUncached(
    const std::string& assetRootPath
) {
    CapabilityResourceIndex index;
    std::error_code errorCode;
    const std::filesystem::path root(assetRootPath);
    if (!std::filesystem::is_directory(root, errorCode)) {
        index.warnings.push_back("asset root is not a readable directory: " + assetRootPath);
        return index;
    }

    std::vector<std::string> mixPaths;
    for (std::filesystem::recursive_directory_iterator iterator(
             root,
             std::filesystem::directory_options::skip_permission_denied,
             errorCode
         );
         iterator != std::filesystem::recursive_directory_iterator();
         iterator.increment(errorCode)) {
        if (errorCode) {
            index.warnings.push_back("asset root scan warning: " + errorCode.message());
            errorCode.clear();
            continue;
        }

        if (!iterator->is_regular_file(errorCode)) {
            errorCode.clear();
            continue;
        }

        const std::filesystem::path path = iterator->path();
        const std::string filename = toLower(path.filename().string());
        index.filesByName[filename].push_back(path.string());
        ++index.looseResourceCount;
        if (toLower(path.extension().string()) == ".mix") {
            mixPaths.push_back(path.string());
        }
    }

    for (auto& [filename, paths] : index.filesByName) {
        std::sort(paths.begin(), paths.end());
    }

    std::sort(mixPaths.begin(), mixPaths.end());
    for (const std::string& mixPath : mixPaths) {
        MixArchive archive;
        if (!archive.loadFromFile(mixPath)) {
            index.warnings.push_back("failed to load MIX: " + mixPath);
            continue;
        }
        if (!archive.isIndexReadable()) {
            index.warnings.push_back(
                "unreadable MIX index: " + mixPath + " reason=" + archive.getErrorMessage()
            );
            continue;
        }
        index.mixArchives.push_back(std::move(archive));
        const std::size_t archiveIndex = index.mixArchives.size() - 1U;
        index.mixArchiveByPath[capabilityPathKey(index.mixArchives.back().getPath())] =
            archiveIndex;
        const std::vector<MixEntry>& entries = index.mixArchives.back().getEntries();
        for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
            const MixEntry& entry = entries[entryIndex];
            index.mixEntriesById[entry.id].push_back(
                CapabilityMixEntryRef{archiveIndex, entryIndex, entry.id, entry.size}
            );
            ++index.mixEntryRefCount;
        }
    }

    for (auto& [id, refs] : index.mixEntriesById) {
        std::stable_sort(
            refs.begin(),
            refs.end(),
            [](const CapabilityMixEntryRef& left, const CapabilityMixEntryRef& right) {
                return left.size > right.size;
            }
        );
    }

    return index;
}

CapabilityResourceIndexBuild buildCapabilityResourceIndex(
    const std::string& assetRootPath
) {
    static std::unordered_map<std::string, CapabilityResourceIndex> processCache;

    const std::string cacheKey = capabilityResourceIndexCacheKey(assetRootPath);
    const auto cached = processCache.find(cacheKey);
    if (cached != processCache.end()) {
        return CapabilityResourceIndexBuild{
            &cached->second,
            cacheKey,
            "hit; reused process-local loose/MIX basename-extension index",
            processCache.size(),
            true
        };
    }

    CapabilityResourceIndex index =
        buildCapabilityResourceIndexUncached(assetRootPath);
    auto inserted = processCache.emplace(cacheKey, std::move(index));
    return CapabilityResourceIndexBuild{
        &inserted.first->second,
        cacheKey,
        "miss; built once for this process; new CLI processes rebuild by design",
        processCache.size(),
        false
    };
}

std::string getInspectionField(
    const AssetInspectionReport& report,
    const std::string& name
) {
    for (const AssetField& field : report.fields) {
        if (field.name == name) {
            return field.value;
        }
    }
    return "";
}

bool inspectionHasUnsupportedWarning(const AssetInspectionReport& report) {
    if (!report.success) {
        return true;
    }

    const std::string decoderSupport = toLower(getInspectionField(report, "decoderSupport"));
    if (decoderSupport.find("diagnostic-header-only") != std::string::npos
        || decoderSupport.find("unsupported") != std::string::npos) {
        return true;
    }

    for (const std::string& warning : report.warnings) {
        const std::string lower = toLower(warning);
        if (lower.find("unsupported") != std::string::npos
            || lower.find("no asset inspector") != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::string summarizeInspection(const AssetInspectionReport& report) {
    if (!report.success) {
        return "decode=fail " + report.errorMessage;
    }

    std::ostringstream output;
    output << "decode=ok kind=" << report.kind << " bytes=" << report.fileSize;

    if (report.extension == ".shp") {
        output << " frameCount=" << getInspectionField(report, "frameCount")
               << " visibleMainFrames=" << getInspectionField(report, "visibleMainFrames")
               << " globalSize="
               << getInspectionField(report, "width")
               << "x" << getInspectionField(report, "height")
               << " firstOffset="
               << getInspectionField(report, "firstFrameOffsetX")
               << "," << getInspectionField(report, "firstFrameOffsetY")
               << " firstSize="
               << getInspectionField(report, "firstFrameWidth")
               << "x" << getInspectionField(report, "firstFrameHeight")
               << " firstFormat=" << getInspectionField(report, "firstFrameFormat")
               << " format0Frames=" << getInspectionField(report, "format0Frames")
               << " format0Raw="
               << getInspectionField(report, "format0RawDecodeStatus")
               << " rawValidated="
               << getInspectionField(report, "format0RawValidatedFrames")
               << " unsupportedFormatFrames=" << getInspectionField(report, "unsupportedFormatFrames")
               << " decoderSupport=" << getInspectionField(report, "decoderSupport");
    } else if (report.extension == ".vxl") {
        output << " sections=" << getInspectionField(report, "sectionNames");
    } else if (report.extension == ".hva") {
        output << " frames=" << getInspectionField(report, "frameCount")
               << " sections=" << getInspectionField(report, "sectionNames");
    } else if (report.extension == ".pal") {
        output << " colors=" << getInspectionField(report, "colorCount");
    } else if (report.extension == ".pcx") {
        output << " size="
               << getInspectionField(report, "width")
               << "x" << getInspectionField(report, "height")
               << " bpp=" << getInspectionField(report, "bitsPerPixel")
               << " planes=" << getInspectionField(report, "colorPlanes")
               << " bytesPerLine=" << getInspectionField(report, "bytesPerLine")
               << " vgaPalette=" << getInspectionField(report, "hasTrailingVgaPalette")
               << " rle=" << getInspectionField(report, "pcxRleDecodeStatus")
               << " decoderSupport=" << getInspectionField(report, "decoderSupport");
    } else if (
        report.extension == ".tmp"
        || report.extension == ".tem"
        || report.extension == ".sno"
        || report.extension == ".urb"
        || report.extension == ".des"
        || report.extension == ".lun"
        || report.extension == ".ubn"
    ) {
        output << " layout=" << getInspectionField(report, "layout")
               << " slots=" << getInspectionField(report, "slotCount")
               << " present=" << getInspectionField(report, "presentTileCount");
    }

    if (!report.warnings.empty()) {
        output << " warnings=" << joinStrings(report.warnings, " | ");
    }

    return output.str();
}

bool paletteStatusIsUnsupported(const std::string& paletteStatus) {
    const std::string lower = toLower(paletteStatus);
    return lower.find("undetermined") != std::string::npos
        || lower.find("missing") != std::string::npos
        || lower.find("unsupported") != std::string::npos;
}

void appendCapabilityDetail(
    CapabilityReportState& state,
    const std::string& line
) {
    if (state.verbose) {
        state.report.warnings.push_back(line);
    } else {
        ++state.suppressedDetailCount;
    }
}

void addUniqueSource(std::vector<std::string>& sources, const std::string& source) {
    if (source.empty()) {
        return;
    }
    if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
        sources.push_back(source);
    }
}

void countCapabilityCoverage(
    CapabilityCoverageCounter& coverage,
    const CapabilityProbe& probe
) {
    ++coverage.checked;
    if (!probe.found) {
        ++coverage.missing;
        return;
    }
    if (probe.unsupported) {
        ++coverage.unsupported;
    } else {
        ++coverage.decodable;
    }
    addUniqueSource(coverage.sources, probe.source);
}

std::string coverageCounterSummary(const CapabilityCoverageCounter& coverage) {
    const unsigned int present = coverage.decodable + coverage.unsupported;
    return
        std::to_string(present)
        + "/"
        + std::to_string(coverage.checked)
        + " present decodable="
        + std::to_string(coverage.decodable)
        + " unsupported="
        + std::to_string(coverage.unsupported)
        + " missing="
        + std::to_string(coverage.missing);
}

std::string coverageCounterSourcesSummary(const CapabilityCoverageCounter& coverage) {
    if (!coverage.sources.empty()) {
        return joinStrings(coverage.sources, " | ");
    }
    return "(none)";
}

void appendCapabilityProbeLine(
    CapabilityReportState& state,
    const CapabilityProbe& probe,
    bool countForCoreCoverage = true
) {
    const std::string category = !probe.found
        ? "[missing]"
        : (probe.unsupported ? "[exists-unsupported]" : "[exists-decodable]");

    if (countForCoreCoverage) {
        if (!probe.found) {
            ++state.missingCount;
        } else if (probe.unsupported) {
            ++state.presentUnsupportedCount;
        } else {
            ++state.presentDecodableCount;
        }
    }

    if (!state.verbose && probe.found && !probe.unsupported) {
        ++state.suppressedDetailCount;
        return;
    }

    if (!state.verbose) {
        const std::string problemKey =
            category
            + "|"
            + (probe.source.empty() ? probe.candidateSummary : probe.source)
            + "|"
            + probe.decodeSummary
            + "|"
            + probe.paletteStatus;
        if (!state.summaryProblemKeys.insert(problemKey).second) {
            ++state.suppressedDuplicateProblemCount;
            return;
        }
    }

    state.report.warnings.push_back(
        category
        + " "
        + probe.label
        + " noReexportNeeded="
        + (probe.found || !countForCoreCoverage ? "yes" : "no")
        + " candidates="
        + probe.candidateSummary
        + " source="
        + (probe.source.empty() ? "missing" : probe.source)
        + " "
        + probe.decodeSummary
        + " palette="
        + (probe.paletteStatus.empty() ? "not-applicable" : probe.paletteStatus)
        + " coreResource="
        + yesNo(countForCoreCoverage)
    );
}

CapabilityDecodeCacheEntry decodeLooseCapabilityResource(
    CapabilityReportState& state,
    const std::string& path
) {
    const std::string cacheKey = "loose:" + path;
    const auto cached = state.decodeCache.find(cacheKey);
    if (cached != state.decodeCache.end()) {
        ++state.decodeCacheHits;
        return cached->second;
    }

    ++state.decodeCacheMisses;
    CapabilityDecodeCacheEntry decoded;
    const std::string extension = toLower(std::filesystem::path(path).extension().string());
    if (extension == ".mix") {
        const auto indexedArchive =
            state.index.mixArchiveByPath.find(capabilityPathKey(path));
        if (indexedArchive != state.index.mixArchiveByPath.end()
            && indexedArchive->second < state.index.mixArchives.size()) {
            const MixArchive& archive = state.index.mixArchives[indexedArchive->second];
            decoded.decoded = archive.isIndexReadable();
            decoded.unsupported = !decoded.decoded;
            decoded.decodeSummary = decoded.decoded
                ? "decode=ok kind=MIX container entries="
                    + std::to_string(archive.getFileCount())
                    + " source=index-cache"
                : "decode=fail kind=MIX container reason=" + archive.getErrorMessage();
            state.decodeCache[cacheKey] = decoded;
            return decoded;
        }

        MixArchive archive;
        decoded.decoded = archive.loadFromFile(path) && archive.isIndexReadable();
        decoded.unsupported = !decoded.decoded;
        decoded.decodeSummary = decoded.decoded
            ? "decode=ok kind=MIX container entries=" + std::to_string(archive.getFileCount())
            : "decode=fail kind=MIX container reason=" + archive.getErrorMessage();
        state.decodeCache[cacheKey] = decoded;
        return decoded;
    }

    std::vector<unsigned char> bytes;
    std::string errorMessage;
    if (!loadBytes(path, bytes, errorMessage)) {
        decoded.decoded = false;
        decoded.unsupported = true;
        decoded.decodeSummary = "decode=fail " + errorMessage;
        state.decodeCache[cacheKey] = decoded;
        return decoded;
    }

    const AssetInspectionReport inspection =
        inspectVirtualAssetBytes(path, extension, bytes);
    decoded.decoded = inspection.success;
    decoded.unsupported = inspectionHasUnsupportedWarning(inspection);
    decoded.decodeSummary = summarizeInspection(inspection);
    state.decodeCache[cacheKey] = decoded;
    return decoded;
}

CapabilityDecodeCacheEntry decodeMixCapabilityResource(
    CapabilityReportState& state,
    const std::string& candidate,
    const MixArchive& archive,
    const MixEntry& entry
) {
    const std::string extension = toLower(std::filesystem::path(candidate).extension().string());
    const std::string cacheKey =
        "mix:"
        + archive.getPath()
        + "!"
        + formatMixId(entry.id)
        + "#"
        + std::to_string(entry.size)
        + "#"
        + extension;
    const auto cached = state.decodeCache.find(cacheKey);
    if (cached != state.decodeCache.end()) {
        ++state.decodeCacheHits;
        return cached->second;
    }

    ++state.decodeCacheMisses;
    CapabilityDecodeCacheEntry decoded;
    std::vector<std::uint8_t> rawBytes;
    std::string errorMessage;
    if (!archive.readEntryData(entry, rawBytes, errorMessage)) {
        decoded.decoded = false;
        decoded.unsupported = true;
        decoded.decodeSummary = "decode=fail " + errorMessage;
        state.decodeCache[cacheKey] = decoded;
        return decoded;
    }

    const std::vector<unsigned char> bytes(rawBytes.begin(), rawBytes.end());
    const AssetInspectionReport inspection =
        inspectVirtualAssetBytes(archive.getPath() + "!" + candidate, extension, bytes);
    decoded.decoded = inspection.success;
    decoded.unsupported = inspectionHasUnsupportedWarning(inspection);
    decoded.decodeSummary = summarizeInspection(inspection);
    state.decodeCache[cacheKey] = decoded;
    return decoded;
}

CapabilityProbe inspectLooseCapabilityResource(
    CapabilityReportState& state,
    const std::string& label,
    const std::string& candidate,
    const std::string& path,
    const std::string& paletteStatus
) {
    CapabilityProbe probe;
    probe.found = true;
    probe.label = label;
    probe.source = "loose:" + path;
    probe.paletteStatus = paletteStatus;

    const CapabilityDecodeCacheEntry decoded =
        decodeLooseCapabilityResource(state, path);
    probe.decoded = decoded.decoded;
    probe.unsupported =
        decoded.unsupported
        || paletteStatusIsUnsupported(paletteStatus);
    probe.decodeSummary = decoded.decodeSummary;
    (void)candidate;
    return probe;
}

CapabilityProbe inspectMixCapabilityResource(
    CapabilityReportState& state,
    const std::string& label,
    const std::string& candidate,
    const MixArchive& archive,
    const MixEntry& entry,
    const std::string& paletteStatus
) {
    CapabilityProbe probe;
    probe.found = true;
    probe.label = label;
    probe.source =
        "mix:" + archive.getPath()
        + "!" + candidate
        + " id=" + formatMixId(entry.id)
        + " size=" + std::to_string(entry.size);
    probe.paletteStatus = paletteStatus;

    const CapabilityDecodeCacheEntry decoded =
        decodeMixCapabilityResource(state, candidate, archive, entry);
    probe.decoded = decoded.decoded;
    probe.unsupported =
        decoded.unsupported
        || paletteStatusIsUnsupported(paletteStatus);
    probe.decodeSummary = decoded.decodeSummary;
    return probe;
}

CapabilityResolvedResource resolveCapabilityResourceCandidate(
    CapabilityReportState& state,
    const std::string& lowerCandidate
) {
    const auto cached = state.resolutionCache.find(lowerCandidate);
    if (cached != state.resolutionCache.end()) {
        ++state.resolutionCacheHits;
        return cached->second;
    }

    ++state.resolutionCacheMisses;
    CapabilityResolvedResource resolved;
    resolved.candidate = lowerCandidate;

    const auto looseIterator = state.index.filesByName.find(lowerCandidate);
    if (looseIterator != state.index.filesByName.end()
        && !looseIterator->second.empty()) {
        resolved.found = true;
        resolved.loose = true;
        resolved.loosePath = looseIterator->second.front();
        ++state.resourceIndexHits;
        state.resolutionCache[lowerCandidate] = resolved;
        return resolved;
    }

    const auto mixIterator =
        state.index.mixEntriesById.find(mixFileIdFromName(lowerCandidate));
    if (mixIterator != state.index.mixEntriesById.end()) {
        for (const CapabilityMixEntryRef& ref : mixIterator->second) {
            if (ref.archiveIndex >= state.index.mixArchives.size()) {
                continue;
            }
            const std::vector<MixEntry>& entries =
                state.index.mixArchives[ref.archiveIndex].getEntries();
            if (ref.entryIndex >= entries.size()) {
                continue;
            }

            resolved.found = true;
            resolved.loose = false;
            resolved.mixArchiveIndex = ref.archiveIndex;
            resolved.mixEntryIndex = ref.entryIndex;
            ++state.resourceIndexHits;
            state.resolutionCache[lowerCandidate] = resolved;
            return resolved;
        }
    }

    ++state.resourceIndexMisses;
    state.resolutionCache[lowerCandidate] = resolved;
    return resolved;
}

CapabilityProbe probeCapabilityResource(
    CapabilityReportState& state,
    const std::string& label,
    const std::vector<std::string>& candidates,
    const std::string& paletteStatus,
    bool countForCoreCoverage = true
) {
    CapabilityProbe missingProbe;
    missingProbe.label = label;
    missingProbe.candidateSummary = joinStrings(candidates);
    missingProbe.paletteStatus = paletteStatus;

    if (candidates.empty()) {
        missingProbe.decodeSummary = "decode=not-attempted";
        appendCapabilityProbeLine(state, missingProbe, countForCoreCoverage);
        if (countForCoreCoverage) {
            ++state.requiredResourceCount;
        }
        return missingProbe;
    }

    if (countForCoreCoverage) {
        ++state.requiredResourceCount;
    }
    for (const std::string& candidate : candidates) {
        const std::string lowerCandidate = toLower(normalizeResourceBaseName(candidate));
        const CapabilityResolvedResource resolved =
            resolveCapabilityResourceCandidate(state, lowerCandidate);
        if (!resolved.found) {
            continue;
        }

        if (resolved.loose) {
            CapabilityProbe probe = inspectLooseCapabilityResource(
                state,
                label,
                lowerCandidate,
                resolved.loosePath,
                paletteStatus
            );
            probe.candidateSummary = joinStrings(candidates);
            appendCapabilityProbeLine(state, probe, countForCoreCoverage);
            return probe;
        }

        if (resolved.mixArchiveIndex < state.index.mixArchives.size()) {
            const MixArchive& archive = state.index.mixArchives[resolved.mixArchiveIndex];
            const std::vector<MixEntry>& entries = archive.getEntries();
            if (resolved.mixEntryIndex < entries.size()) {
                CapabilityProbe probe = inspectMixCapabilityResource(
                    state,
                    label,
                    lowerCandidate,
                    archive,
                    entries[resolved.mixEntryIndex],
                    paletteStatus
                );
                probe.candidateSummary = joinStrings(candidates);
                appendCapabilityProbeLine(state, probe, countForCoreCoverage);
                return probe;
            }
        }
    }

    missingProbe.decodeSummary =
        "decode=not-attempted exactHashMiss ids="
        + (candidates.empty() ? "(none)" : formatMixId(mixFileIdFromName(candidates.front())));
    appendCapabilityProbeLine(state, missingProbe, countForCoreCoverage);
    return missingProbe;
}

void recordLogicalCapability(
    CapabilityReportState& state,
    const std::string& label,
    const std::string& details
) {
    ++state.logicalCount;
    appendCapabilityDetail(state, "[logical] " + label + " " + details);
}

const UnitType* findCapabilityUnit(
    const std::vector<UnitType>& units,
    const std::string& id
) {
    const std::string lowerId = toLower(id);
    for (const UnitType& unit : units) {
        if (toLower(unit.id) == lowerId) {
            return &unit;
        }
    }
    return nullptr;
}

const BuildingType* findCapabilityBuilding(
    const RulesDatabase& rules,
    const std::string& id
) {
    const std::string lowerId = toLower(id);
    for (const BuildingType& building : rules.getBuildingTypes()) {
        if (toLower(building.id) == lowerId) {
            return &building;
        }
    }
    return nullptr;
}

const WeaponType* findCapabilityWeapon(
    const RulesDatabase& rules,
    const std::string& id
) {
    const std::string lowerId = toLower(id);
    for (const WeaponType& weapon : rules.getWeaponTypes()) {
        if (toLower(weapon.id) == lowerId) {
            return &weapon;
        }
    }
    return nullptr;
}

const ProjectileType* findCapabilityProjectile(
    const RulesDatabase& rules,
    const std::string& id
) {
    const std::string lowerId = toLower(id);
    for (const ProjectileType& projectile : rules.getProjectileTypes()) {
        if (toLower(projectile.id) == lowerId) {
            return &projectile;
        }
    }
    return nullptr;
}

const WarheadType* findCapabilityWarhead(
    const RulesDatabase& rules,
    const std::string& id
) {
    const std::string lowerId = toLower(id);
    for (const WarheadType& warhead : rules.getWarheadTypes()) {
        if (toLower(warhead.id) == lowerId) {
            return &warhead;
        }
    }
    return nullptr;
}

const ArtObjectLink* findCapabilityLink(
    const std::vector<ArtObjectLink>& links,
    const std::string& kind,
    const std::string& id
) {
    const std::string lowerId = toLower(id);
    for (const ArtObjectLink& link : links) {
        if (link.objectKind == kind && toLower(link.objectId) == lowerId) {
            return &link;
        }
    }
    return nullptr;
}

std::string sequenceFrameRange(const ArtType::SequenceFrame& frame) {
    if (!frame.present) {
        return frame.name + "=missing";
    }

    const int frameCount = std::max(0, frame.frameCount);
    const int facingMultiplier = std::max(1, frame.facingMultiplier);
    const int totalFrames = std::max(1, frameCount * facingMultiplier);
    const int endFrame = frame.startFrame + totalFrames - 1;
    return frame.name
        + "="
        + std::to_string(frame.startFrame)
        + "-"
        + std::to_string(endFrame)
        + "(count="
        + std::to_string(frameCount)
        + ",facing="
        + std::to_string(frame.facingMultiplier)
        + ")";
}

std::string sequenceListSummary(const std::vector<ArtType::SequenceFrame>& frames) {
    std::vector<std::string> summaries;
    for (const ArtType::SequenceFrame& frame : frames) {
        if (frame.present) {
            summaries.push_back(sequenceFrameRange(frame));
        }
    }
    return joinStrings(summaries, "; ");
}

const ArtType::SequenceFrame* findCapabilitySequenceFrame(
    const ArtObjectLink& link,
    const std::string& name
) {
    const std::string lowerName = toLower(name);
    for (const ArtType::SequenceFrame& frame : link.sequenceFrames) {
        if (toLower(frame.name) == lowerName) {
            return &frame;
        }
    }
    return nullptr;
}

std::string presentMissing(const ArtType::SequenceFrame* frame) {
    return frame != nullptr && frame->present ? "present" : "missing";
}

std::vector<std::string> collectUnitWeapons(const UnitType& unit) {
    std::vector<std::string> weapons;
    addUniqueString(weapons, unit.primaryWeapon);
    addUniqueString(weapons, unit.secondaryWeapon);
    addUniqueString(weapons, unit.elitePrimaryWeapon);
    addUniqueString(weapons, unit.eliteSecondaryWeapon);
    addUniqueString(weapons, unit.deployWeapon);
    addUniqueString(weapons, unit.eliteDeployWeapon);
    addUniqueString(weapons, unit.occupyWeapon);
    addUniqueString(weapons, unit.eliteOccupyWeapon);
    return weapons;
}

std::vector<std::string> collectBuildingWeapons(const BuildingType& building) {
    std::vector<std::string> weapons;
    addUniqueString(weapons, building.primaryWeapon);
    addUniqueString(weapons, building.secondaryWeapon);
    addUniqueString(weapons, building.elitePrimaryWeapon);
    addUniqueString(weapons, building.eliteSecondaryWeapon);
    addUniqueString(weapons, building.deployWeapon);
    addUniqueString(weapons, building.eliteDeployWeapon);
    return weapons;
}

std::string imageNameFromLink(
    const ArtObjectLink& link,
    const std::string& fallbackId
) {
    if (!link.image.empty()) {
        return link.image;
    }
    if (!link.artId.empty()) {
        return link.artId;
    }
    return fallbackId;
}

std::string imageNameFromArtId(
    const ArtDatabase& art,
    const std::string& artId
) {
    const ArtType* artType = art.findArtType(artId);
    if (artType != nullptr && !artType->image.empty()) {
        return artType->image;
    }
    return artId;
}

std::vector<std::string> artShpCandidatesForId(
    const ArtDatabase& art,
    const std::string& artId
) {
    return shpCandidates(imageNameFromArtId(art, artId));
}

std::string unitPaletteStatus(const CapabilityReportState& state) {
    return "candidates=" + joinStrings(unitPaletteCandidatesForTheater(state.theater));
}

std::string projectilePaletteStatus(
    const CapabilityReportState& state,
    const ProjectileType& projectile
) {
    if (projectile.imageIsNone || projectile.inviso) {
        return "not-applicable";
    }
    if (projectile.paletteIsSet && !projectile.palette.empty()) {
        return "explicit=" + projectile.palette;
    }
    if (projectile.animPalette) {
        return "anim.pal";
    }
    if (projectile.firersPalette) {
        return "firers palette";
    }
    return "candidates=" + joinStrings(projectilePaletteCandidatesForTheater(state.theater));
}

void appendPaletteCandidateProbe(
    CapabilityReportState& state,
    const std::string& label,
    const std::vector<std::string>& candidates
) {
    if (candidates.empty()) {
        return;
    }
    probeCapabilityResource(
        state,
        label + " palette candidates",
        candidates,
        "palette-resource"
    );
}

void appendAnimationResourceList(
    CapabilityReportState& state,
    const std::string& label,
    const std::string& list,
    const std::string& paletteStatus
) {
    for (const std::string& animationId : splitCapabilityList(list)) {
        probeCapabilityResource(
            state,
            label + " " + animationId,
            artShpCandidatesForId(state.art, animationId),
            paletteStatus
        );
    }
}

void appendWeaponCapability(
    CapabilityReportState& state,
    const std::string& ownerId,
    const std::string& weaponId,
    std::set<std::string>& visitedWeapons
) {
    const std::string lowerWeaponId = toLower(weaponId);
    if (lowerWeaponId.empty() || visitedWeapons.count(lowerWeaponId) > 0) {
        return;
    }
    visitedWeapons.insert(lowerWeaponId);

    const WeaponType* weapon = findCapabilityWeapon(state.rules, weaponId);
    if (weapon == nullptr) {
        recordLogicalCapability(
            state,
            ownerId + " weapon " + weaponId,
            "rules section missing; this is a rules reference gap, not a MIX export gap"
        );
        return;
    }

    appendCapabilityDetail(
        state,
        "[capability] "
        + ownerId
        + " weapon="
        + weapon->id
        + " projectile="
        + (weapon->projectile.empty() ? "-" : weapon->projectile)
        + " warhead="
        + (weapon->warhead.empty() ? "-" : weapon->warhead)
        + " anim="
        + (weapon->anim.empty() ? "-" : weapon->anim)
    );

    appendAnimationResourceList(
        state,
        ownerId + " weapon " + weapon->id + " muzzle/anim",
        weapon->anim,
        "anim.pal candidates=anim.pal"
    );
    if (!weapon->anim.empty()) {
        appendPaletteCandidateProbe(state, ownerId + " weapon " + weapon->id + " anim", {"anim.pal"});
    }

    const ProjectileType* projectile =
        findCapabilityProjectile(state.rules, weapon->projectile);
    if (projectile == nullptr) {
        recordLogicalCapability(
            state,
            ownerId + " weapon " + weapon->id + " projectile " + weapon->projectile,
            weapon->projectile.empty()
                ? "no projectile reference"
                : "projectile rules section missing"
        );
    } else if (projectile->imageIsNone || projectile->inviso) {
        recordLogicalCapability(
            state,
            ownerId + " weapon " + weapon->id + " projectile " + projectile->id,
            "Image=none/Inviso=yes; renderer should not treat this as a missing SHP"
        );
        appendCapabilityDetail(
            state,
            "[capability] "
            + ownerId
            + " weapon="
            + weapon->id
            + " projectilePaletteHint projectile="
            + projectile->id
            + " rendererProjectileSprite=none logicalProjectile=yes"
            + " weaponAnim="
            + (weapon->anim.empty() ? "-" : weapon->anim)
            + " weaponAnimPaletteField=anim.pal"
            + " rendererRule=draw weapon Anim/warhead/trailer resources when present; do not request projectile SHP"
        );
    } else {
        const std::string projectileImage =
            projectile->image.empty() ? projectile->id : projectile->image;
        const std::string projectileResourceImage =
            imageNameFromArtId(state.art, projectileImage);
        const std::string paletteStatus = projectilePaletteStatus(state, *projectile);
        const std::vector<std::string> projectilePaletteCandidateList =
            projectile->paletteIsSet && !projectile->palette.empty()
                ? paletteCandidates(projectile->palette)
                : projectilePaletteCandidatesForTheater(state.theater);
        probeCapabilityResource(
            state,
            ownerId + " weapon " + weapon->id + " projectile image " + projectileImage,
            graphicCandidates(projectileResourceImage),
            paletteStatus
        );
        appendPaletteCandidateProbe(
            state,
            ownerId + " weapon " + weapon->id + " projectile " + projectileImage,
            projectilePaletteCandidateList
        );
        appendCapabilityDetail(
            state,
            "[capability] "
            + ownerId
            + " weapon="
            + weapon->id
            + " projectilePaletteHint image="
            + projectileImage
            + " resourceImage="
            + projectileResourceImage
            + " hint="
            + paletteStatus
            + " rendererPaletteFields=explicitProjectilePalette:"
            + (projectile->paletteIsSet ? (projectile->palette.empty() ? "(empty)" : projectile->palette) : "(not-set)")
            + ",firersPalette:"
            + yesNo(projectile->firersPalette)
            + ",candidateList:"
            + joinStrings(projectilePaletteCandidateList)
            + ",fallbackAnimPal:anim.pal"
            + " rendererRule=Image=none/Inviso has no sprite; otherwise consume resolved SHP/VXL/HVA with this palette hint. "
            + "purpleLikely=palette-consumption-not-missing-resource"
        );
    }

    if (projectile != nullptr) {
        appendCapabilityDetail(
            state,
            "[capability] "
            + ownerId
            + " weapon="
            + weapon->id
            + " projectileLifecycle actor=runtime-projectile"
            + " projectile="
            + projectile->id
            + " rendererImage="
            + (projectile->imageIsNone || projectile->inviso
                ? "none-or-inviso"
                : (projectile->image.empty() ? projectile->id : projectile->image))
            + " trailer="
            + (projectile->trailer.empty() ? "-" : projectile->trailer)
            + " airburstWeapon="
            + (projectile->airburstWeapon.empty() ? "-" : projectile->airburstWeapon)
            + " replayKeys=weapon/projectile/warhead"
            + " warhead="
            + (weapon->warhead.empty() ? "-" : weapon->warhead)
        );
        appendAnimationResourceList(
            state,
            ownerId + " weapon " + weapon->id + " projectile trailer",
            projectile->trailer,
            "anim.pal candidates=anim.pal"
        );
        if (!projectile->trailer.empty()) {
            appendPaletteCandidateProbe(
                state,
                ownerId + " weapon " + weapon->id + " projectile trailer",
                {"anim.pal"}
            );
        }
        if (!projectile->airburstWeapon.empty()) {
            appendWeaponCapability(
                state,
                ownerId + " airburst",
                projectile->airburstWeapon,
                visitedWeapons
            );
        }
    }

    const WarheadType* warhead = findCapabilityWarhead(state.rules, weapon->warhead);
    if (warhead == nullptr) {
        recordLogicalCapability(
            state,
            ownerId + " weapon " + weapon->id + " warhead " + weapon->warhead,
            weapon->warhead.empty()
                ? "no warhead reference"
                : "warhead rules section missing"
        );
        return;
    }

    appendCapabilityDetail(
        state,
        "[capability] "
        + ownerId
        + " weapon="
        + weapon->id
        + " warheadImpact="
        + (warhead->animList.empty() ? "-" : warhead->animList)
        + " explosion="
        + (warhead->explosion.empty() ? "-" : warhead->explosion)
        + " particle="
        + (warhead->particle.empty() ? "-" : warhead->particle)
    );
    appendAnimationResourceList(
        state,
        ownerId + " weapon " + weapon->id + " warhead impact",
        warhead->animList,
        "anim.pal candidates=anim.pal"
    );
    appendAnimationResourceList(
        state,
        ownerId + " weapon " + weapon->id + " warhead explosion",
        warhead->explosion,
        "anim.pal candidates=anim.pal"
    );
    if (!warhead->animList.empty() || !warhead->explosion.empty()) {
        appendPaletteCandidateProbe(
            state,
            ownerId + " weapon " + weapon->id + " warhead anim",
            {"anim.pal"}
        );
    }
}

void appendUnitCapability(
    CapabilityReportState& state,
    const std::string& objectKind,
    const UnitType& unit,
    const ArtObjectLink* link
) {
    const std::vector<std::string> weapons = collectUnitWeapons(unit);
    appendCapabilityDetail(
        state,
        "[capability] object="
        + unit.id
        + " rulesSection="
        + objectKind
        + " art="
        + (link == nullptr ? "(missing)" : link->artId)
        + " image="
        + (link == nullptr ? unit.image : imageNameFromLink(*link, unit.id))
        + " sequenceList="
        + (link == nullptr ? "(missing art link)" : sequenceListSummary(link->sequenceFrames))
        + " weapons="
        + joinStrings(weapons)
    );

    if (link == nullptr) {
        recordLogicalCapability(
            state,
            unit.id + " art link",
            "missing art link; resource lookup cannot be trusted until art section resolves"
        );
        return;
    }

    const std::string image = imageNameFromLink(*link, unit.id);
    if (objectKind == "infantry") {
        probeCapabilityResource(
            state,
            unit.id + " infantry SHP " + image,
            shpCandidates(image),
            unitPaletteStatus(state)
        );
        appendPaletteCandidateProbe(
            state,
            unit.id + " infantry " + image,
            unitPaletteCandidatesForTheater(state.theater)
        );

        const ArtType::SequenceFrame* deploy = findCapabilitySequenceFrame(*link, "Deploy");
        const ArtType::SequenceFrame* deployed = findCapabilitySequenceFrame(*link, "Deployed");
        const ArtType::SequenceFrame* fireUp = findCapabilitySequenceFrame(*link, "FireUp");
        const ArtType::SequenceFrame* deployedFire = findCapabilitySequenceFrame(*link, "DeployedFire");
        appendCapabilityDetail(
            state,
            "[capability] "
            + unit.id
            + " infantrySequences deploy="
            + presentMissing(deploy)
            + " deployed="
            + presentMissing(deployed)
            + " fire="
            + presentMissing(fireUp)
            + " deployedFire="
            + presentMissing(deployedFire)
            + " deployer="
            + yesNo(unit.deployer)
            + " deployFire="
            + yesNo(unit.deployFire)
        );
    } else {
        if (link->voxel || objectKind == "vehicle" || objectKind == "aircraft") {
            const CapabilityProbe vxlProbe = probeCapabilityResource(
                state,
                unit.id + " vehicle voxel " + image,
                resourceCandidates(image, {".vxl"}),
                "embedded voxel palette"
            );
            probeCapabilityResource(
                state,
                unit.id + " vehicle hva " + image,
                resourceCandidates(image, {".hva"}),
                "not-applicable"
            );
            appendCapabilityDetail(
                state,
                "[capability] "
                + unit.id
                + " turret/barrel art rulesTurret="
                + yesNo(unit.turret)
                + " artTurret="
                + yesNo(link->turret)
                + " rendererNeeds=VXL section names from voxel probe"
                + (vxlProbe.decodeSummary.empty() ? "" : " (" + vxlProbe.decodeSummary + ")")
            );
        } else {
            probeCapabilityResource(
                state,
                unit.id + " unit SHP " + image,
                shpCandidates(image),
                unitPaletteStatus(state)
            );
            appendPaletteCandidateProbe(
                state,
                unit.id + " unit " + image,
                unitPaletteCandidatesForTheater(state.theater)
            );
        }
    }

    if (!link->cameo.empty()) {
        const CapabilityProbe cameoProbe = probeCapabilityResource(
            state,
            unit.id + " cameo " + link->cameo,
            shpCandidates(link->cameo),
            "cameo/sidebar palette"
        );
        countCapabilityCoverage(state.uiCameoCoverage, cameoProbe);
    }

    std::set<std::string> visitedWeapons;
    for (const std::string& weaponId : weapons) {
        appendWeaponCapability(state, unit.id, weaponId, visitedWeapons);
    }
}

void appendBuildingAnimationCapability(
    CapabilityReportState& state,
    const std::string& buildingId,
    const std::string& layerKind,
    const ArtType::ActiveAnimation& animation
) {
    const std::string layerInfo =
        " offset=" + std::to_string(animation.offsetX)
        + "," + std::to_string(animation.offsetY)
        + " ySort=" + std::to_string(animation.ySort)
        + " zAdjust=" + std::to_string(animation.zAdjust)
        + " powered=" + yesNo(animation.powered);
    if (!animation.image.empty()) {
        probeCapabilityResource(
            state,
            buildingId + " " + layerKind + " " + animation.key + " " + animation.image + layerInfo,
            artShpCandidatesForId(state.art, animation.image),
            unitPaletteStatus(state)
        );
    }
    if (!animation.damagedImage.empty()) {
        probeCapabilityResource(
            state,
            buildingId + " " + layerKind + " damaged " + animation.key + " " + animation.damagedImage + layerInfo,
            artShpCandidatesForId(state.art, animation.damagedImage),
            unitPaletteStatus(state)
        );
    }
    appendCapabilityDetail(
        state,
        "[capability] "
        + buildingId
        + " "
        + layerKind
        + " "
        + animation.key
        + " layerAnchor=same-footprintRearCell-as-base"
        + layerInfo
        + " expectedRect=project(rearCell)+(-globalWidth/2+frameOffsetX+layerOffsetX,tileH/2-globalHeight+frameOffsetY+layerOffsetY)"
        + " visibleRect=frameWidthxframeHeight-within-globalSize"
        + " rendererRule=apply globalSize/frameOffset first, then layer offset; clip against global rect, not local frame; use YSort/ZAdjust only for ordering"
    );
}

struct CapabilityFoundationTemplate {
    int width = 1;
    int height = 1;
    std::string parseStatus = "default-1x1";
};

bool parseCapabilityFoundationInt(
    const std::string& value,
    std::size_t begin,
    std::size_t end,
    int& parsed
) {
    if (begin >= end) {
        return false;
    }
    int result = 0;
    for (std::size_t index = begin; index < end; ++index) {
        const char ch = value[index];
        if (ch < '0' || ch > '9') {
            return false;
        }
        result = (result * 10) + (ch - '0');
        if (result > 16) {
            return false;
        }
    }
    if (result <= 0) {
        return false;
    }
    parsed = result;
    return true;
}

CapabilityFoundationTemplate parseCapabilityFoundationTemplate(
    const std::string& foundation
) {
    CapabilityFoundationTemplate result;
    std::string compact;
    compact.reserve(foundation.size());
    for (const char ch : foundation) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            compact.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    const std::size_t separator = compact.find('x');
    if (separator == std::string::npos) {
        result.parseStatus = foundation.empty() ? "missing-default-1x1" : "unparsed-default-1x1";
        return result;
    }
    int width = 1;
    int height = 1;
    if (!parseCapabilityFoundationInt(compact, 0, separator, width)
        || !parseCapabilityFoundationInt(compact, separator + 1, compact.size(), height)) {
        result.parseStatus = "unparsed-default-1x1";
        return result;
    }
    result.width = width;
    result.height = height;
    result.parseStatus = "parsed";
    return result;
}

void appendBuildingCapability(
    CapabilityReportState& state,
    const BuildingType& building,
    const ArtObjectLink* link
) {
    const std::vector<std::string> weapons = collectBuildingWeapons(building);
    appendCapabilityDetail(
        state,
        "[capability] object="
        + building.id
        + " rulesSection=building art="
        + (link == nullptr ? "(missing)" : link->artId)
        + " image="
        + (link == nullptr ? building.image : imageNameFromLink(*link, building.id))
        + " foundation="
        + (link == nullptr ? "-" : link->foundation)
        + " weapons="
        + joinStrings(weapons)
        + " anchorFormula=topLeft=project(footprintRearCell)+(-globalWidth/2+frameOffsetX,tileH/2-globalHeight+frameOffsetY)+layerOffset"
        + (link == nullptr
            ? ""
            : " rectHints=globalSize+frameOffset shadow="
                + (link->shadowSpecified
                    ? yesNo(link->shadow)
                    : "default")
                + " shadowIndex="
                + std::to_string(std::max(0, link->shadowIndex))
                + " useTurretShadow="
                + yesNo(link->useTurretShadow)
                + " remap="
                + yesNo(link->remapable)
                + " palette="
                + (link->palette.empty() ? unitPaletteStatus(state) : link->palette)
                + " ownerRemapRule=map-owner->house-remap-color-if-remapable"
                + " terrainPalette="
                + yesNo(link->terrainPalette)
                + " animPalette="
                + yesNo(link->animPalette))
    );

    if (link == nullptr) {
        recordLogicalCapability(
            state,
            building.id + " art link",
            "missing art link; resource lookup cannot be trusted until art section resolves"
        );
        return;
    }

    const std::string image = imageNameFromLink(*link, building.id);
    const CapabilityFoundationTemplate foundationTemplate =
        parseCapabilityFoundationTemplate(link->foundation);
    appendCapabilityDetail(
        state,
        "[capability] "
        + building.id
        + " placementHitFootprint foundation="
        + (link->foundation.empty() ? "-" : link->foundation)
        + " parsed="
        + std::to_string(foundationTemplate.width)
        + "x"
        + std::to_string(foundationTemplate.height)
        + " parseStatus="
        + foundationTemplate.parseStatus
        + " rearCellOffset=+"
        + std::to_string(std::max(0, foundationTemplate.width - 1))
        + ",+"
        + std::to_string(std::max(0, foundationTemplate.height - 1))
        + " placementPreviewExpectedRect=union-footprint-iso-diamonds"
        + " screenIsoDiamondTemplate=union(project(x+dx,y+dy,level) diamonds for all footprint cells; tile=60x30 relative coordinates"
        + " javara2PlaceRef=place.shp frames0/1 crop y=30..58 to 60x29 green/red diamond; reference-only"
        + " mapFacts=structure rearCell/footprintCells/edgeCells/approachCellsById"
        + " mapFactsFields=footprintIsoDiamondBounds,placementPreviewIsoDiamondBounds,hitTestIsoDiamondBounds,selectionIsoDiamondBounds,footprintBuildable,footprintLegality,per-cell-water/shore/bridge/resource/overlayBlocker/buildableReason"
        + ",occupiedByStructure/occupancyReason"
        + " purposes=attack,engineer-enter,repair,occupy"
        + " rendererRule=placement-preview-and-hit-test-consume-the-same-cached-footprint-facts"
    );
    appendCapabilityDetail(
        state,
        "[capability] "
        + building.id
        + " expectedScreenRectFormula base=project(rearCell)+(-globalWidth/2+frameOffsetX,tileH/2-globalHeight+frameOffsetY)"
        + " active/special/aux=baseFormula+layerOffset"
        + " damaged=base-file-damaged-frame-or-damaged-layer-image"
        + " placementVsFinalRule=preview uses footprint iso diamonds; final building uses resolved SHP global rect from rearCell; they share rearCell but not rectangle size"
        + " clippingRule=clip-to-global-rect-not-local-frame"
        + " neutralColorRule=if map owner is Neutral, Renderer should apply Neutral/white house remap only when art remapable=yes; otherwise draw palette indexes literally"
        + " consumerRule=placement-preview,hit-test,selection,attack,engineer-enter,repair,occupy all use MapFacts footprint facts"
    );
    if (toLower(building.id) == "caoild") {
        appendCapabilityDetail(
            state,
            "[capability] CAOILD oilDerrickHalfRenderDiagnostic base=CAOILD active=CAOILD_A damagedActive=CAOILD_AD flare=CAOILD_F"
            " likelyIfLowerHalfOnly=renderer-clipping-or-active-layer-not-consumed unless probe below reports missing/decode-failed"
            " neutralRemapFact=map instances are Neutral on dryheat; apply Neutral/white house remap only if art remapable flag permits"
        );
    }
    probeCapabilityResource(
        state,
        building.id + " building base " + image,
        shpCandidates(image),
        unitPaletteStatus(state)
    );
    appendPaletteCandidateProbe(
        state,
        building.id + " building " + image,
        unitPaletteCandidatesForTheater(state.theater)
    );
    recordLogicalCapability(
        state,
        building.id + " building damaged frame",
        "uses base SHP damaged/remap frame set when present; same file as base"
    );

    if (!link->buildup.empty()) {
        probeCapabilityResource(
            state,
            building.id + " building buildup " + link->buildup,
            shpCandidates(link->buildup),
            unitPaletteStatus(state)
        );
    }
    if (!link->bibShape.empty()) {
        probeCapabilityResource(
            state,
            building.id + " building bib " + link->bibShape,
            tmpCandidates(link->bibShape, state.theater),
            "theater terrain palette candidates=" + joinStrings(isoPaletteCandidatesForTheater(state.theater))
        );
    }

    for (const ArtType::ActiveAnimation& animation : link->activeAnimations) {
        appendBuildingAnimationCapability(state, building.id, "active", animation);
    }
    for (const ArtType::ActiveAnimation& animation : link->specialAnimations) {
        appendBuildingAnimationCapability(state, building.id, "special", animation);
    }
    for (const ArtType::ActiveAnimation& animation : link->auxiliaryAnimations) {
        appendBuildingAnimationCapability(state, building.id, "auxiliary", animation);
    }

    if (!building.turretAnim.empty()) {
        probeCapabilityResource(
            state,
            building.id + " building turretAnim " + building.turretAnim
                + " offset=" + std::to_string(building.turretAnimX)
                + "," + std::to_string(building.turretAnimY)
                + " zAdjust=" + std::to_string(building.turretAnimZAdjust),
            building.turretAnimIsVoxel
                ? graphicCandidates(building.turretAnim)
                : shpCandidates(building.turretAnim),
            building.turretAnimIsVoxel ? "embedded voxel palette" : unitPaletteStatus(state)
        );
    }

    if (!link->cameo.empty()) {
        const CapabilityProbe cameoProbe = probeCapabilityResource(
            state,
            building.id + " cameo " + link->cameo,
            shpCandidates(link->cameo),
            "cameo/sidebar palette"
        );
        countCapabilityCoverage(state.uiCameoCoverage, cameoProbe);
    }

    std::set<std::string> visitedWeapons;
    for (const std::string& weaponId : weapons) {
        appendWeaponCapability(state, building.id, weaponId, visitedWeapons);
    }
}

void appendUiCapability(CapabilityReportState& state) {
    appendCapabilityDetail(
        state,
        "[capability] UI resources sidebar/cameo/pips/power/health/repair/sell/radar "
        "decoderStatus=format0-shp-raw-indexed-rows-validated-when-present pcx-rle-indexed8-validated-when-present palette-selection-required"
    );
    appendCapabilityDetail(
        state,
        "[capability] UI decode plan format0-shp=read-validated-width*height-indexed-rows-with-sidebar-palette "
        "pcx=8bpp-indexed-with-embedded-or-sidebar-palette pal=768-byte-rgb-table "
        "repairSellButtonRule=use resolved frame offsets and sidec palette; healthStandalone=optional; health=pips+pipbrd; status=pips2; power=powerp/power"
    );
    appendCapabilityDetail(
        state,
        "[capability] UI frame candidate facts tabs=frame0 repair=frames0-1 sell=frames0-1 "
        "power=frames0-1 radar=frames0-32 pips=health-and-veterancy-frames pips2=status-frames pipbrd=health-border-frames "
        "rendererFields=sourceMix,entryName,frameIndex,frameOffset,globalSize,paletteCandidate"
    );
    appendCapabilityDetail(
        state,
        "[capability] UI reference facts readOnly=javara2 repairRect=20,8,64x31 sellRect=84,8,64x31 "
        "tabRects=start=26,38 step=29 frames=tab00..tab03 stateFrames=0..4 "
        "powerStrip=width17 side2-left-slice height=side2*12 rows; powerp frames black/green/yellow/red/gray as 12x1 strips "
        "radarRect=0,0,radarFrameSize radarAnim=all radar.shp frames at 50ms; placeDiamond=place.shp frames0/1 crop y=30..58 size=60x29"
    );
    appendCapabilityDetail(
        state,
        "[capability] UI queue/progress reference facts readOnly=javara2 build queues use two icon columns at x=22-powerStripWidth and x=85-powerStripWidth per side2 slot; "
        "tab flashing toggles bright/dark frames; production countdown/progress should use resolved cameo frame plus palette and draw progress overlay separately"
    );
    const CapabilityProbe sidec01Probe = probeCapabilityResource(
        state,
        "UI sidebar container sidec01",
        resourceCandidates("sidec01", {".mix"}),
        "not-applicable"
    );
    countCapabilityCoverage(state.uiSidebarCoverage, sidec01Probe);
    const CapabilityProbe sidec02Probe = probeCapabilityResource(
        state,
        "UI sidebar container sidec02md",
        resourceCandidates("sidec02md", {".mix"}),
        "not-applicable"
    );
    countCapabilityCoverage(state.uiSidebarCoverage, sidec02Probe);
    const CapabilityProbe tabsProbe = probeCapabilityResource(
        state,
        "UI sidebar tabs decodeMetadata=format0-shp-or-pcx frameOffsetsRequired",
        resourceCandidates("tabs", {".shp", ".pcx"}),
        "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal"
    );
    countCapabilityCoverage(state.uiSidebarCoverage, tabsProbe);
    const CapabilityProbe repairProbe = probeCapabilityResource(
        state,
        "UI repair button decodeMetadata=format0-shp-or-pcx expectedRectRef=20,8,64x31",
        resourceCandidates("repair", {".shp", ".pcx"}),
        "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal"
    );
    countCapabilityCoverage(state.uiSidebarCoverage, repairProbe);
    const CapabilityProbe sellProbe = probeCapabilityResource(
        state,
        "UI sell button decodeMetadata=format0-shp-or-pcx expectedRectRef=84,8,64x31",
        resourceCandidates("sell", {".shp", ".pcx"}),
        "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal"
    );
    countCapabilityCoverage(state.uiSidebarCoverage, sellProbe);
    const CapabilityProbe pipsProbe = probeCapabilityResource(
        state,
        "UI pips structure-health/veterancy/unit-health-fill frames decodeMetadata=indexed-shp-or-pcx paletteRequired",
        resourceCandidates("pips", {".shp", ".pcx"}),
        "game palette candidate=palette.pal"
    );
    countCapabilityCoverage(state.uiPipsCoverage, pipsProbe);
    const CapabilityProbe pips2Probe = probeCapabilityResource(
        state,
        "UI pips2 passenger/ammo/ore/gem frames decodeMetadata=indexed-shp-or-pcx paletteRequired",
        resourceCandidates("pips2", {".shp", ".pcx"}),
        "game palette candidate=palette.pal"
    );
    countCapabilityCoverage(state.uiPipsCoverage, pips2Probe);
    const CapabilityProbe pipBorderProbe = probeCapabilityResource(
        state,
        "UI pipbrd unit/infantry health-border frames decodeMetadata=indexed-shp-or-pcx paletteRequired",
        resourceCandidates("pipbrd", {".shp", ".pcx"}),
        "game palette candidate=palette.pal"
    );
    countCapabilityCoverage(state.uiPipsCoverage, pipBorderProbe);
    const CapabilityProbe powerProbe = probeCapabilityResource(
        state,
        "UI standalone power image frameCandidate=power-meter decodeMetadata=format0-shp-or-pcx",
        resourceCandidates("power", {".shp", ".pcx"}),
        "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal"
    );
    countCapabilityCoverage(state.uiPowerCoverage, powerProbe);
    const CapabilityProbe healthProbe = probeCapabilityResource(
        state,
        "UI standalone health image optionalStandalone=true pipsMayProvideHealthFrames=true",
        resourceCandidates("health", {".shp", ".pcx"}),
        "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal",
        false
    );
    countCapabilityCoverage(state.uiStandaloneHealthCoverage, healthProbe);
    const CapabilityProbe radarProbe = probeCapabilityResource(
        state,
        "UI radar panel decodeMetadata=format0-shp-or-pcx animTimingRef=50ms",
        resourceCandidates("radar", {".shp", ".pcx"}),
        "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal"
    );
    countCapabilityCoverage(state.uiRadarCoverage, radarProbe);
    probeCapabilityResource(
        state,
        "UI optional Java/sidebar reference side2 power-strip background",
        resourceCandidates("side2", {".shp", ".pcx"}),
        "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal",
        false
    );
    probeCapabilityResource(
        state,
        "UI optional Java/sidebar reference powerp strip frames",
        resourceCandidates("powerp", {".shp", ".pcx"}),
        "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal",
        false
    );
    for (const char* tabName : {"tab00", "tab01", "tab02", "tab03"}) {
        probeCapabilityResource(
            state,
            std::string("UI optional Java/sidebar reference ") + tabName + " state frames",
            resourceCandidates(tabName, {".shp", ".pcx"}),
            "sidebar palette candidates=sidebar.pal,cameo.pal,cachemd.pal",
            false
        );
    }
    probeCapabilityResource(
        state,
        "UI optional placement diamond place.shp frames0/1 crop60x29",
        resourceCandidates("place", {".shp", ".pcx"}),
        "place/sidebar palette candidates=palette.pal,sidebar.pal,cameo.pal,cachemd.pal",
        false
    );
    const CapabilityProbe gamePaletteProbe = probeCapabilityResource(
        state,
        "UI game palette for pips/pips2/pipbrd",
        resourceCandidates("palette", {".pal"}),
        "palette-resource"
    );
    countCapabilityCoverage(state.uiPaletteCoverage, gamePaletteProbe);
    const CapabilityProbe animPaletteProbe = probeCapabilityResource(
        state,
        "UI anim/effect palette candidates",
        {"anim.pal"},
        "palette-resource"
    );
    countCapabilityCoverage(state.uiPaletteCoverage, animPaletteProbe);
}

void appendUiCoverageFields(CapabilityReportState& state) {
    addField(state.report, "uiSidebarCoverage", coverageCounterSummary(state.uiSidebarCoverage));
    addField(state.report, "uiSidebarSources", coverageCounterSourcesSummary(state.uiSidebarCoverage));
    addField(state.report, "uiPowerCoverage", coverageCounterSummary(state.uiPowerCoverage));
    addField(state.report, "uiPowerSources", coverageCounterSourcesSummary(state.uiPowerCoverage));
    addField(state.report, "uiRadarCoverage", coverageCounterSummary(state.uiRadarCoverage));
    addField(state.report, "uiRadarSources", coverageCounterSourcesSummary(state.uiRadarCoverage));
    addField(state.report, "uiPipsCoverage", coverageCounterSummary(state.uiPipsCoverage));
    addField(state.report, "uiPipsSources", coverageCounterSourcesSummary(state.uiPipsCoverage));
    addField(state.report, "uiCameoCoverage", coverageCounterSummary(state.uiCameoCoverage));
    addField(state.report, "uiCameoSources", coverageCounterSourcesSummary(state.uiCameoCoverage));
    addField(state.report, "uiPaletteCoverage", coverageCounterSummary(state.uiPaletteCoverage));
    addField(state.report, "uiPaletteSources", coverageCounterSourcesSummary(state.uiPaletteCoverage));
    addField(
        state.report,
        "uiStandaloneHealthCoverage",
        coverageCounterSummary(state.uiStandaloneHealthCoverage)
        + " optional=true coreResource=no"
    );
    addField(
        state.report,
        "uiStandaloneHealthSources",
        coverageCounterSourcesSummary(state.uiStandaloneHealthCoverage)
    );
    addField(
        state.report,
        "uiUnsupportedPolicy",
        "format0-shp and indexed8 PCX are exists-decodable in diagnostics when raw/RLE indexed rows validate; unresolved palette remains exists-unsupported when bytes are present; standalone health is optional because pips can carry health frames"
    );
    addField(
        state.report,
        "uiPowerHealthFrameCandidates",
        "pips.shp=structure health frames0/1/2/4, veterancy pair13/14 or14/15, unit health fill tail; pipbrd.shp=unit/infantry borders; pips2.shp=passenger/ammo/ore/gem state; power.shp=standalone power meter frames0-1; health.shp/health.pcx=optional standalone only"
    );
    addField(
        state.report,
        "uiSidebarFrameCandidates",
        "tabs.shp frame0; repair.shp frames0-1; sell.shp frames0-1; radar.shp frames0-32; cameos frame0; verbose probe carries firstFrame size/offset/global"
    );
    addField(
        state.report,
        "uiSidebarJavaReferenceFacts",
        "readOnly javara2: repair rect 20,8,64x31; sell rect 84,8,64x31; tabs start 26,38 step 29 tab00..03 frames 0..4; radar anim frames at 50ms; facts only, no Runtime constants copied"
    );
    addField(
        state.report,
        "uiPowerHealthDrawFacts",
        "power strip width 17; side2 left slice is background; powerp frames are 12x1 colored strips when present; RA2 health uses pips.shp plus pipbrd.shp with palette.pal; cargo/status uses pips2.shp; health standalone optional"
    );
    addField(
        state.report,
        "uiPlacementPreviewFacts",
        "place.shp frames 0/1 crop y=30..58 to 60x29 green/red diamond in Java reference; MapFacts footprint iso diamonds are authoritative for legality and hit-test"
    );
    addField(
        state.report,
        "uiSidebarReferenceCheck",
        "read-only refs agree sidec01/sidec02 containers carry sidebar frames; Java layout facts are reported for Renderer reference only; no runtime layout constants copied"
    );

    state.report.warnings.push_back(
        "[capability-summary] uiCoverage sidebar="
        + coverageCounterSummary(state.uiSidebarCoverage)
        + " pips="
        + coverageCounterSummary(state.uiPipsCoverage)
        + " power="
        + coverageCounterSummary(state.uiPowerCoverage)
        + " radar="
        + coverageCounterSummary(state.uiRadarCoverage)
        + " cameo="
        + coverageCounterSummary(state.uiCameoCoverage)
        + " palette="
        + coverageCounterSummary(state.uiPaletteCoverage)
        + " healthStandaloneOptional="
        + coverageCounterSummary(state.uiStandaloneHealthCoverage)
    );

    appendCapabilityDetail(
        state,
        "[capability] UI decode policy format0-shp=diagnostic-raw-indexed-decodable-when-validated pcx=diagnostic-rle-indexed8-decodable-when-validated pal=exists-decodable-when-768-bytes healthStandalone=optional health=pips+pipbrd status=pips2 power=powerp/power"
    );
    appendCapabilityDetail(
        state,
        "[capability] UI renderer fields repairRectRef=20,8,64x31 sellRectRef=84,8,64x31 tabRectRef=start26,38 step29 powerStripRef=width17 queueIconRef=x=22/85-powerStripWidth radarAnimRef=radar.shp frames all 50ms; health=pips.shp+pipbrd.shp palette.pal; cargo=pips2.shp palette.pal; power.shp optional standalone, health standalone optional; use per-entry embedded PCX palette or authored external palette and per-frame offset/global metadata from resource probe"
    );
}

std::string firstLooseResourcePath(
    const CapabilityResourceIndex& index,
    const std::vector<std::string>& candidates
) {
    for (const std::string& candidate : candidates) {
        const auto found = index.filesByName.find(toLower(normalizeResourceBaseName(candidate)));
        if (found != index.filesByName.end() && !found->second.empty()) {
            return found->second.front();
        }
    }
    return {};
}

std::string iniValueOrDash(
    const IniFile& ini,
    const std::string& section,
    const std::string& key
) {
    const std::string value = trimWhitespace(ini.getValue(section, key, ""));
    return value.empty() ? "-" : value;
}

std::vector<std::string> evaVoiceCandidates(const std::string& voiceId) {
    if (voiceId.empty() || voiceId == "-") {
        return {};
    }
    return resourceCandidates(voiceId, {".wav", ".aud"});
}

void appendEvaAudioCapability(CapabilityReportState& state) {
    const CapabilityProbe evamdProbe = probeCapabilityResource(
        state,
        "EVA dialog ini evamd",
        {"evamd.ini"},
        "ini-text",
        false
    );
    const CapabilityProbe ra2mdCsfProbe = probeCapabilityResource(
        state,
        "EVA text csf ra2md",
        {"ra2md.csf"},
        "csf-string-table decoder-gap",
        false
    );
    const CapabilityProbe ra2CsfProbe = probeCapabilityResource(
        state,
        "EVA fallback text csf ra2",
        {"ra2.csf"},
        "csf-string-table decoder-gap",
        false
    );
    const CapabilityProbe audioMdMixProbe = probeCapabilityResource(
        state,
        "EVA audio container audiomd",
        {"audiomd.mix"},
        "mix-container",
        false
    );
    const CapabilityProbe audioMixProbe = probeCapabilityResource(
        state,
        "EVA audio container audio",
        {"audio.mix"},
        "mix-container",
        false
    );
    const CapabilityProbe audioBagProbe = probeCapabilityResource(
        state,
        "EVA optional audio bag/index",
        {"audiomd.bag", "audio.bag", "audiomd.idx", "audio.idx"},
        "optional-audio-bag-index",
        false
    );

    addField(
        state.report,
        "evaResourceChain",
        "evamd.ini DialogList -> EVA section Text + Allied/Russian/Yuri voice id -> voiceId.wav/aud in audiomd.mix/audio.mix; CSF text tables ra2md.csf/ra2.csf exist separately"
    );
    addField(
        state.report,
        "evaCoreSources",
        "evamd="
            + (evamdProbe.source.empty() ? "missing" : evamdProbe.source)
            + " ra2mdCsf="
            + (ra2mdCsfProbe.source.empty() ? "missing" : ra2mdCsfProbe.source)
            + " ra2Csf="
            + (ra2CsfProbe.source.empty() ? "missing" : ra2CsfProbe.source)
            + " audiomd="
            + (audioMdMixProbe.source.empty() ? "missing" : audioMdMixProbe.source)
            + " audio="
            + (audioMixProbe.source.empty() ? "missing" : audioMixProbe.source)
    );
    addField(
        state.report,
        "evaPayloadStatus",
        audioMdMixProbe.found || audioMixProbe.found
            ? "voice payload containers exist; WAV/AUD/CSF decode is a decoder/runtime consumption gap, not a MIX re-export gap"
            : "voice payload container missing from inspected asset root"
    );
    addField(
        state.report,
        "evaBagStatus",
        audioBagProbe.found
            ? "bag/index payload exists"
            : "no loose .bag/.idx found in exported resources; language audio is present as MIX entries"
    );

    const std::string evamdPath = firstLooseResourcePath(state.index, {"evamd.ini"});
    if (evamdPath.empty()) {
        state.report.warnings.push_back(
            "[capability-summary] EVA evamd.ini missing; voice label chain cannot be sampled"
        );
        return;
    }

    IniFile evaIni;
    if (!evaIni.loadFromFile(evamdPath)) {
        state.report.warnings.push_back(
            "[capability-summary] EVA failed to parse evamd.ini path=" + evamdPath
        );
        return;
    }

    const IniFile::Section* dialogList = evaIni.getSection("DialogList");
    const std::size_t dialogCount = dialogList == nullptr ? 0 : dialogList->size();
    addField(state.report, "evaDialogCount", static_cast<unsigned long long>(dialogCount));

    std::vector<std::string> sampleLabels = {
        "EVA_ConstructionComplete",
        "EVA_LowPower",
        "EVA_UnitReady",
        "EVA_CannotDeployHere",
        "EVA_InsufficientFunds"
    };
    if (dialogList != nullptr) {
        for (const IniFile::KeyValue& entry : *dialogList) {
            const std::string label = trimWhitespace(entry.second);
            if (!label.empty()
                && label[0] != ';'
                && std::find(sampleLabels.begin(), sampleLabels.end(), label)
                    == sampleLabels.end()) {
                sampleLabels.push_back(label);
            }
            if (sampleLabels.size() >= 8) {
                break;
            }
        }
    }

    unsigned int voiceProbeCount = 0;
    unsigned int voicePresentCount = 0;
    unsigned int voiceUnsupportedCount = 0;
    unsigned int voiceMissingCount = 0;
    for (const std::string& label : sampleLabels) {
        if (evaIni.getSection(label) == nullptr) {
            continue;
        }
        const std::string text = iniValueOrDash(evaIni, label, "Text");
        const std::string allied = iniValueOrDash(evaIni, label, "Allied");
        const std::string russian = iniValueOrDash(evaIni, label, "Russian");
        const std::string yuri = iniValueOrDash(evaIni, label, "Yuri");
        const std::string priority = iniValueOrDash(evaIni, label, "Priority");
        const std::string type = iniValueOrDash(evaIni, label, "Type");
        appendCapabilityDetail(
            state,
            "[capability] EVA label="
                + label
                + " text="
                + text
                + " alliedVoice="
                + allied
                + " russianVoice="
                + russian
                + " yuriVoice="
                + yuri
                + " type="
                + type
                + " priority="
                + priority
        );

        const std::vector<std::pair<std::string, std::string>> voices = {
            {"Allied", allied},
            {"Russian", russian},
            {"Yuri", yuri}
        };
        for (const auto& [house, voice] : voices) {
            if (voice.empty() || voice == "-") {
                continue;
            }
            if (!state.verbose) {
                ++voiceProbeCount;
                bool found = false;
                for (const std::string& candidate : evaVoiceCandidates(voice)) {
                    const CapabilityResolvedResource resolved =
                        resolveCapabilityResourceCandidate(
                            state,
                            toLower(normalizeResourceBaseName(candidate))
                        );
                    if (resolved.found) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    ++voicePresentCount;
                    ++voiceUnsupportedCount;
                } else {
                    ++voiceMissingCount;
                }
                continue;
            }
            const CapabilityProbe voiceProbe = probeCapabilityResource(
                state,
                "EVA " + label + " " + house + " voice " + voice,
                evaVoiceCandidates(voice),
                "wav/aud decoder gap; payload bytes in language audio MIX",
                false
            );
            ++voiceProbeCount;
            if (voiceProbe.found) {
                ++voicePresentCount;
                if (voiceProbe.unsupported) {
                    ++voiceUnsupportedCount;
                }
            } else {
                ++voiceMissingCount;
            }
        }
    }

    addField(
        state.report,
        "evaVoiceSampleCoverage",
        std::to_string(voicePresentCount)
            + "/"
            + std::to_string(voiceProbeCount)
            + " present unsupported="
            + std::to_string(voiceUnsupportedCount)
            + " missing="
            + std::to_string(voiceMissingCount)
    );
    state.report.warnings.push_back(
        std::string("[capability-summary] EVA chain evamd.ini=")
        + (evamdProbe.found ? "present" : "missing")
        + " csf="
        + ((ra2mdCsfProbe.found || ra2CsfProbe.found) ? "present" : "missing")
        + " audioMix="
        + ((audioMdMixProbe.found || audioMixProbe.found) ? "present" : "missing")
        + " sampledVoices="
        + std::to_string(voicePresentCount)
        + "/"
        + std::to_string(voiceProbeCount)
        + " decoderGap=wav/aud/csf payload parsing"
    );
}

std::string overlayFactSummary(const MapCellFacts& cell) {
    if (!cell.hasOverlay) {
        return "overlay=none";
    }

    return "overlayIndex="
        + std::to_string(cell.overlay.overlayIndex)
        + " type="
        + (cell.overlay.type.empty() ? "-" : cell.overlay.type)
        + " image="
        + (cell.overlay.image.empty() ? "-" : cell.overlay.image)
        + " frame="
        + std::to_string(cell.overlay.frame)
        + " fallbackFrame="
        + std::to_string(cell.overlay.fallbackFrame)
        + " overlayData="
        + std::to_string(cell.overlay.overlayData);
}

std::string theaterFactSummary(const MapCellFacts& cell) {
    std::string text = cell.theaterTileReference.empty()
        ? "theaterTile=none"
        : cell.theaterTileReference;
    if (!cell.theaterTile.candidateFileNames.empty()) {
        text += " candidates=" + joinStrings(cell.theaterTile.candidateFileNames);
    }
    return text;
}

bool parseCapabilityPositiveIntToken(
    const std::string& value,
    std::size_t begin,
    std::size_t end,
    int& parsed
) {
    if (begin >= end) {
        return false;
    }

    int result = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const char ch = value[i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        result = (result * 10) + (ch - '0');
        if (result > 16) {
            return false;
        }
    }

    if (result <= 0) {
        return false;
    }

    parsed = result;
    return true;
}

MapStructureFootprintOverride mapFootprintOverrideFromLink(
    const ArtObjectLink& link
) {
    MapStructureFootprintOverride overrideFact;
    overrideFact.type = link.objectId;
    overrideFact.source = "Art foundation=" + (link.foundation.empty() ? "-" : link.foundation);

    std::string compact;
    compact.reserve(link.foundation.size());
    for (const char ch : link.foundation) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            compact.push_back(ch);
        }
    }
    compact = toLower(compact);

    const std::size_t separator = compact.find('x');
    if (separator == std::string::npos) {
        overrideFact.width = 1;
        overrideFact.height = 1;
        overrideFact.source += " (unparsed; default 1x1)";
        return overrideFact;
    }

    int width = 1;
    int height = 1;
    if (!parseCapabilityPositiveIntToken(compact, 0, separator, width)
        || !parseCapabilityPositiveIntToken(compact, separator + 1, compact.size(), height)) {
        overrideFact.width = 1;
        overrideFact.height = 1;
        overrideFact.source += " (unparsed; default 1x1)";
        return overrideFact;
    }

    overrideFact.width = width;
    overrideFact.height = height;
    return overrideFact;
}

std::vector<MapStructureFootprintOverride> buildMapFootprintOverrides(
    const std::vector<ArtObjectLink>& links
) {
    std::vector<MapStructureFootprintOverride> overrides;
    overrides.reserve(links.size());
    for (const ArtObjectLink& link : links) {
        if (link.objectKind != "building" || link.objectId.empty()) {
            continue;
        }
        overrides.push_back(mapFootprintOverrideFromLink(link));
    }
    return overrides;
}

std::string overlayResourceFactSummary(const MapOverlayResourceFact& fact) {
    if (!fact.present) {
        return "resourceFact=none";
    }

    return "resourceKind="
        + (fact.kind.empty() ? "-" : fact.kind)
        + " overlayKind="
        + (fact.overlayKind.empty() ? "-" : fact.overlayKind)
        + " base="
        + (fact.baseName.empty() ? "-" : fact.baseName)
        + " image="
        + (fact.image.empty() ? "-" : fact.image)
        + " requestedFrame="
        + std::to_string(fact.requestedFrame)
        + " fallbackFrame="
        + std::to_string(fact.fallbackFrame)
        + " resourceFrame="
        + std::to_string(fact.resourceFrame)
        + " paletteHint="
        + (fact.paletteHint.empty() ? "-" : fact.paletteHint)
        + " theaterHint="
        + (fact.theaterHint.empty() ? "-" : fact.theaterHint)
        + " rendererLayerHint="
        + (fact.rendererLayerHint.empty() ? "-" : fact.rendererLayerHint)
        + " candidates="
        + (fact.candidateFileNames.empty() ? "-" : joinStrings(fact.candidateFileNames));
}

std::string footprintCellSummary(const MapFootprintCellFact& cell) {
    return std::to_string(cell.x)
        + ","
        + std::to_string(cell.y)
        + " idx="
        + std::to_string(cell.cellIndex)
        + " level="
        + std::to_string(cell.heightLevel)
        + " inBounds="
        + yesNo(cell.inBounds)
        + " hasCell="
        + yesNo(cell.hasMapCell)
        + " buildable="
        + yesNo(cell.buildable)
        + " reason="
        + (cell.buildableReason.empty() ? "-" : cell.buildableReason)
        + " approach="
        + yesNo(cell.approach)
        + " purpose="
        + (cell.purpose.empty() ? "-" : cell.purpose)
        + " terrain="
        + (cell.terrainClass.empty() ? "-" : cell.terrainClass)
        + " water="
        + yesNo(cell.isWater)
        + " shore="
        + yesNo(cell.isShore)
        + " bridge="
        + yesNo(cell.isBridge)
        + " resource="
        + yesNo(cell.isResourceCell)
        + " overlay="
        + yesNo(cell.hasOverlay)
        + " overlayBlocker="
        + yesNo(cell.isOverlayBlocker)
        + " occupiedByStructure="
        + yesNo(cell.occupiedByStructure)
        + " occupiedByOtherStructure="
        + yesNo(cell.occupiedByOtherStructure)
        + " occupyingStructureCount="
        + std::to_string(cell.occupyingStructureCount)
        + " occupyingStructureIds="
        + (cell.occupyingStructureIds.empty() ? "-" : cell.occupyingStructureIds)
        + " occupyingStructureTypes="
        + (cell.occupyingStructureTypes.empty() ? "-" : cell.occupyingStructureTypes)
        + " occupyingStructureOwners="
        + (cell.occupyingStructureOwners.empty() ? "-" : cell.occupyingStructureOwners)
        + " occupancyReason="
        + (cell.occupancyReason.empty() ? "-" : cell.occupancyReason)
        + " overlayKind="
        + (cell.overlayKind.empty() ? "-" : cell.overlayKind)
        + " overlayIndex="
        + std::to_string(cell.overlayIndex)
        + " overlayType="
        + (cell.overlayType.empty() ? "-" : cell.overlayType)
        + " overlayImage="
        + (cell.overlayImage.empty() ? "-" : cell.overlayImage)
        + " overlayFrame="
        + std::to_string(cell.overlayFrame)
        + " overlayResourceFrame="
        + std::to_string(cell.overlayResourceFrame)
        + " overlayFallbackFrame="
        + std::to_string(cell.overlayFallbackFrame)
        + " passability="
        + (cell.passabilityHint.empty() ? "-" : cell.passabilityHint)
        + " blocked="
        + (cell.blockedReason.empty() ? "-" : cell.blockedReason);
}

std::string isoDiamondBoundsSummary(const MapIsoDiamondBounds& bounds) {
    if (!bounds.valid) {
        return "invalid";
    }
    return "min="
        + std::to_string(bounds.minX)
        + ","
        + std::to_string(bounds.minY)
        + " max="
        + std::to_string(bounds.maxX)
        + ","
        + std::to_string(bounds.maxY)
        + " size="
        + std::to_string(bounds.width)
        + "x"
        + std::to_string(bounds.height)
        + " space="
        + (bounds.coordinateSpace.empty() ? "-" : bounds.coordinateSpace);
}

void appendMapFactsCapability(
    CapabilityReportState& state,
    const MapFacts& facts
) {
    addField(state.report, "mapFactsSchemaVersion", facts.schemaVersion);
    addField(state.report, "mapFactsContentHash", formatUint64Hex(facts.contentHash, 16));
    addField(state.report, "mapFactsContentHashAlgorithm", facts.contentHashAlgorithm);
    addField(state.report, "mapFactsOwnershipModel", facts.ownershipModel);
    addField(state.report, "mapFactsQueryLifetime", facts.queryLifetime);
    addField(state.report, "mapFactsTheater", facts.theater.empty() ? "(unknown)" : facts.theater);
    addField(state.report, "mapFactsSize", std::to_string(facts.width) + "x" + std::to_string(facts.height));
    addField(state.report, "mapFactsCells", static_cast<unsigned long long>(facts.cells.size()));
    addField(state.report, "mapFactsTerrainCells", static_cast<unsigned long long>(facts.terrainCellCount));
    addField(state.report, "mapFactsOverlayCells", static_cast<unsigned long long>(facts.overlayCellCount));
    addField(state.report, "mapFactsResourceCells", static_cast<unsigned long long>(facts.resourceCellCount));
    addField(state.report, "mapFactsOreCells", static_cast<unsigned long long>(facts.oreCellCount));
    addField(state.report, "mapFactsGemCells", static_cast<unsigned long long>(facts.gemCellCount));
    addField(state.report, "mapFactsBridgeCells", static_cast<unsigned long long>(facts.bridgeCellCount));
    addField(state.report, "mapFactsHighBridgeCells", static_cast<unsigned long long>(facts.highBridgeCellCount));
    addField(state.report, "mapFactsLowBridgeCells", static_cast<unsigned long long>(facts.lowBridgeCellCount));
    addField(state.report, "mapFactsWaterCells", static_cast<unsigned long long>(facts.waterCellCount));
    addField(state.report, "mapFactsShoreCells", static_cast<unsigned long long>(facts.shoreCellCount));
    addField(state.report, "mapFactsCliffCells", static_cast<unsigned long long>(facts.cliffCellCount));
    addField(state.report, "mapFactsRampCells", static_cast<unsigned long long>(facts.rampCellCount));
    addField(state.report, "mapFactsTunnelCells", static_cast<unsigned long long>(facts.tunnelCellCount));
    addField(state.report, "mapFactsMissingTheaterTiles", static_cast<unsigned long long>(facts.missingTheaterTileCount));
    addField(state.report, "mapFactsLookupIndexSize", static_cast<unsigned long long>(facts.lookupIndexSize));
    addField(state.report, "mapFactsStructureAnchors", static_cast<unsigned long long>(facts.structureAnchors.size()));
    addField(state.report, "mapFactsStructureAnchorIndexSize", static_cast<unsigned long long>(facts.structureAnchorIdLookup.size()));
    addField(state.report, "mapFactsStructureFootprintCells", static_cast<unsigned long long>(facts.structureFootprintCellCount));
    addField(state.report, "mapFactsStructureEdgeCells", static_cast<unsigned long long>(facts.structureEdgeCellCount));
    addField(state.report, "mapFactsStructureApproachCells", static_cast<unsigned long long>(facts.structureApproachCellCount));
    addField(state.report, "mapFactsAttackApproachCells", static_cast<unsigned long long>(facts.structureAttackApproachCellCount));
    addField(state.report, "mapFactsEngineerApproachCells", static_cast<unsigned long long>(facts.structureEngineerApproachCellCount));
    addField(state.report, "mapFactsRepairApproachCells", static_cast<unsigned long long>(facts.structureRepairApproachCellCount));
    addField(state.report, "mapFactsOccupyApproachCells", static_cast<unsigned long long>(facts.structureOccupyApproachCellCount));
    addField(state.report, "mapFactsStructureBuildableFootprintCells", static_cast<unsigned long long>(facts.structureBuildableFootprintCellCount));
    addField(state.report, "mapFactsStructureIllegalFootprintCells", static_cast<unsigned long long>(facts.structureIllegalFootprintCellCount));
    addField(state.report, "mapFactsStructureWaterFootprintCells", static_cast<unsigned long long>(facts.structureWaterFootprintCellCount));
    addField(state.report, "mapFactsStructureShoreFootprintCells", static_cast<unsigned long long>(facts.structureShoreFootprintCellCount));
    addField(state.report, "mapFactsStructureBridgeFootprintCells", static_cast<unsigned long long>(facts.structureBridgeFootprintCellCount));
    addField(state.report, "mapFactsStructureResourceFootprintCells", static_cast<unsigned long long>(facts.structureResourceFootprintCellCount));
    addField(state.report, "mapFactsStructureOverlayBlockerFootprintCells", static_cast<unsigned long long>(facts.structureOverlayBlockerFootprintCellCount));
    addField(state.report, "mapFactsStructureOccupiedMapCells", static_cast<unsigned long long>(facts.structureOccupiedMapCellCount));
    addField(state.report, "mapFactsStructureOccupiedFootprintCells", static_cast<unsigned long long>(facts.structureOccupiedFootprintCellCount));
    addField(state.report, "mapFactsStructureOtherOccupiedFootprintCells", static_cast<unsigned long long>(facts.structureOtherOccupiedFootprintCellCount));
    addField(state.report, "mapQueryLookupMode", "getCell/getCellByIndex use cellIndexLookup; lists use cached offsets");
    addField(state.report, "mapQueryConstAccess", "yes; MapQuery keeps a non-owning const MapFacts pointer");

    std::size_t overlayKindOre = 0;
    std::size_t overlayKindGem = 0;
    std::size_t overlayKindBridge = 0;
    std::size_t overlayKindWall = 0;
    std::size_t overlayKindOther = 0;
    std::size_t oreBridgeOverlapCells = 0;
    for (const MapCellFacts& cell : facts.cells) {
        if (!cell.hasOverlay) {
            continue;
        }

        const std::string overlayKind =
            cell.overlayResource.overlayKind.empty()
                ? (cell.isGem ? "gem" : (cell.isOre ? "ore" : (cell.isBridge ? "bridge" : "other")))
                : cell.overlayResource.overlayKind;
        if (overlayKind == "ore") {
            ++overlayKindOre;
        } else if (overlayKind == "gem") {
            ++overlayKindGem;
        } else if (overlayKind == "bridge") {
            ++overlayKindBridge;
        } else if (overlayKind == "wall") {
            ++overlayKindWall;
        } else {
            ++overlayKindOther;
        }

        if (cell.isResourceCell && cell.isBridge) {
            ++oreBridgeOverlapCells;
        }
    }
    addField(
        state.report,
        "mapFactsOverlayKindCounts",
        "ore=" + std::to_string(overlayKindOre)
            + " gem=" + std::to_string(overlayKindGem)
            + " bridge=" + std::to_string(overlayKindBridge)
            + " wall=" + std::to_string(overlayKindWall)
            + " other=" + std::to_string(overlayKindOther)
    );
    addField(
        state.report,
        "mapFactsOreBridgeOverlapCells",
        static_cast<unsigned long long>(oreBridgeOverlapCells)
    );
    addField(
        state.report,
        "mapFactsOverlayDisambiguation",
        "overlayKind is authoritative: ore/gem use resource/tiberium candidates; bridge uses bridge TMP/SHP candidates; wall/other never imply ore"
    );
    addField(
        state.report,
        "mapQueryApproachByIdApi",
        "getStructureFootprintCellsById/getStructureEdgeCellsById/getStructureApproachCellsById/getStructureFootprintIsoDiamondBoundsById/isStructureFootprintBuildableById use structureAnchorIdLookup; purposes=attack,engineer-enter,repair,occupy"
    );
    addField(
        state.report,
        "mapQueryApproachByIdCachePolicy",
        "cached-only; by-id methods return buildMapFacts footprint/edge/purpose approach vectors and never recompute footprint/ring geometry"
    );

    state.report.warnings.push_back(
        "[map-fact] summary theater="
        + (facts.theater.empty() ? "(unknown)" : facts.theater)
        + " version="
        + facts.schemaVersion
        + " hash="
        + formatUint64Hex(facts.contentHash, 16)
        + " cells="
        + std::to_string(facts.cells.size())
        + " terrain="
        + std::to_string(facts.terrainCellCount)
        + " overlay="
        + std::to_string(facts.overlayCellCount)
        + " resource="
        + std::to_string(facts.resourceCellCount)
        + " ore="
        + std::to_string(facts.oreCellCount)
        + " gem="
        + std::to_string(facts.gemCellCount)
        + " bridge="
        + std::to_string(facts.bridgeCellCount)
        + " overlayKinds(ore/gem/bridge/wall/other)="
        + std::to_string(overlayKindOre)
        + "/"
        + std::to_string(overlayKindGem)
        + "/"
        + std::to_string(overlayKindBridge)
        + "/"
        + std::to_string(overlayKindWall)
        + "/"
        + std::to_string(overlayKindOther)
        + " oreBridgeOverlap="
        + std::to_string(oreBridgeOverlapCells)
        + " water="
        + std::to_string(facts.waterCellCount)
        + " shore="
        + std::to_string(facts.shoreCellCount)
        + " cliff="
        + std::to_string(facts.cliffCellCount)
        + " ramp="
        + std::to_string(facts.rampCellCount)
        + " tunnel="
        + std::to_string(facts.tunnelCellCount)
        + " lookupIndex="
        + std::to_string(facts.lookupIndexSize)
        + " structureAnchors="
        + std::to_string(facts.structureAnchors.size())
        + " approach="
        + std::to_string(facts.structureApproachCellCount)
        + " footprintBuildable="
        + std::to_string(facts.structureBuildableFootprintCellCount)
        + " footprintIllegal="
        + std::to_string(facts.structureIllegalFootprintCellCount)
        + " footprintWater/Shore/Bridge="
        + std::to_string(facts.structureWaterFootprintCellCount)
        + "/"
        + std::to_string(facts.structureShoreFootprintCellCount)
        + "/"
        + std::to_string(facts.structureBridgeFootprintCellCount)
        + " footprintResource/OverlayBlocker="
        + std::to_string(facts.structureResourceFootprintCellCount)
        + "/"
        + std::to_string(facts.structureOverlayBlockerFootprintCellCount)
        + " occupiedMapCells="
        + std::to_string(facts.structureOccupiedMapCellCount)
        + " footprintOccupied/OtherOccupied="
        + std::to_string(facts.structureOccupiedFootprintCellCount)
        + "/"
        + std::to_string(facts.structureOtherOccupiedFootprintCellCount)
        + " missingTheaterTiles="
        + std::to_string(facts.missingTheaterTileCount)
    );

    state.report.warnings.push_back(
        "[map-query] api getCell(x,y)=cellIndexLookup getCellByIndex(cellIndex)=cellIndexLookup "
        "resource/ore/gem/bridge/water/shore/cliff/ramp/tunnel lists=cachedOffsets "
        "structureFootprint/edge/approach/screenIsoDiamond/buildableLegality/overlayBlocker=anchor facts "
        "occupiedByStructure/occupiedByOtherStructure/occupancyReason=cached cell facts "
        "structureFootprintById/edgeById/approachById=structureAnchorIdLookup cachedOnly=no-recompute "
        "structureFootprintIsoDiamondBoundsById/isStructureFootprintBuildableById=cached facts "
        "ownership=const MapFacts snapshot lifetime=caller-owned"
    );

    state.report.warnings.push_back(
        "[map-query] rendererFacts overlay id/type/image/frame/fallbackFrame/resourceFrame/candidates/paletteHint/theaterHint "
        "overlayKind=ore/gem/bridge/wall/other disambiguates resource vs bridge; bridge high/low and ore/gem/tiberium cells are available without asset decode"
    );

    for (const std::string& warning : facts.warnings) {
        state.report.warnings.push_back("[map-fact-warning] " + warning);
    }

    if (!state.verbose) {
        if (!facts.resourceCellOffsets.empty()
            || !facts.bridgeCellOffsets.empty()
            || !facts.waterCellOffsets.empty()
            || !facts.structureAnchors.empty()) {
            ++state.suppressedDetailCount;
        }
        return;
    }

    MapQuery query(facts);
    std::size_t overlayCellDetailCount = 0;
    for (const MapCellFacts& cell : facts.cells) {
        if (!cell.hasOverlay) {
            continue;
        }
        ++overlayCellDetailCount;
        state.report.warnings.push_back(
            "[map-fact] overlayCell "
            + std::to_string(cell.x)
            + ","
            + std::to_string(cell.y)
            + " cellIndex="
            + std::to_string(cell.cellIndex)
            + " overlayKind="
            + (cell.overlayResource.overlayKind.empty() ? "-" : cell.overlayResource.overlayKind)
            + " "
            + overlayFactSummary(cell)
            + " "
            + overlayResourceFactSummary(cell.overlayResource)
            + " terrain="
            + cell.terrainClass
            + " passability="
            + cell.passabilityHint
            + " rendererDirectSource=resolve candidate list in order; no broad MIX search; never use bridge candidates for ore/gem"
        );
    }
    if (overlayCellDetailCount > 0) {
        state.report.warnings.push_back(
            "[map-fact] overlayCell verbose total="
            + std::to_string(overlayCellDetailCount)
            + " fields=overlayKind,overlayId,type,image,frame,resourceFrame,fallbackFrame,candidates,paletteHint"
        );
    }

    const std::vector<const MapCellFacts*> resourceCells = query.getResourceCells();
    const std::size_t resourceLimit = std::min<std::size_t>(resourceCells.size(), 16);
    for (std::size_t i = 0; i < resourceLimit; ++i) {
        const MapCellFacts& cell = *resourceCells[i];
        state.report.warnings.push_back(
            "[map-fact] resourceCell "
            + std::to_string(cell.x)
            + ","
            + std::to_string(cell.y)
            + " kind="
            + (cell.isGem ? "gem" : (cell.isOre ? "ore" : "resource"))
            + " "
            + overlayFactSummary(cell)
            + " "
            + overlayResourceFactSummary(cell.overlayResource)
            + " "
            + theaterFactSummary(cell)
            + " passability="
            + cell.passabilityHint
            + " rendererHint=overlay image/frame are authoritative before frame fallback"
        );
    }
    if (resourceCells.size() > resourceLimit) {
        state.report.warnings.push_back(
            "[map-fact] resourceCell samplesTruncated total="
            + std::to_string(resourceCells.size())
        );
    }

    const std::vector<const MapCellFacts*> bridgeCells = query.getBridgeCells();
    const std::size_t bridgeLimit = std::min<std::size_t>(bridgeCells.size(), 16);
    for (std::size_t i = 0; i < bridgeLimit; ++i) {
        const MapCellFacts& cell = *bridgeCells[i];
        state.report.warnings.push_back(
            "[map-fact] bridgeCell "
            + std::to_string(cell.x)
            + ","
            + std::to_string(cell.y)
            + " kind="
            + (cell.isHighBridge ? "high" : "low")
            + " "
            + overlayFactSummary(cell)
            + " "
            + overlayResourceFactSummary(cell.overlayResource)
            + " "
            + theaterFactSummary(cell)
            + " height="
            + std::to_string(cell.heightLevel)
            + " passability="
            + cell.passabilityHint
            + " tmpSlotFallback=try map subTile/frame first, then decoded TMP present slot"
        );
    }
    if (bridgeCells.size() > bridgeLimit) {
        state.report.warnings.push_back(
            "[map-fact] bridgeCell samplesTruncated total="
            + std::to_string(bridgeCells.size())
        );
    }

    const std::vector<const MapCellFacts*> waterCells = query.getWaterCells();
    const std::size_t waterLimit = std::min<std::size_t>(waterCells.size(), 8);
    for (std::size_t i = 0; i < waterLimit; ++i) {
        const MapCellFacts& cell = *waterCells[i];
        state.report.warnings.push_back(
            "[map-fact] waterCell "
            + std::to_string(cell.x)
            + ","
            + std::to_string(cell.y)
            + " shore="
            + yesNo(cell.isShore)
            + " cliff="
            + yesNo(cell.isCliff)
            + " "
            + theaterFactSummary(cell)
            + " passability="
            + cell.passabilityHint
        );
    }
    if (waterCells.size() > waterLimit) {
        state.report.warnings.push_back(
            "[map-fact] waterCell samplesTruncated total="
            + std::to_string(waterCells.size())
        );
    }

    const std::size_t anchorLimit =
        std::min<std::size_t>(facts.structureAnchors.size(), 12);
    for (std::size_t i = 0; i < anchorLimit; ++i) {
        const MapStructureAnchorFact& anchor = facts.structureAnchors[i];
        const MapStructureAnchorFact* indexedAnchor =
            anchor.id.empty() ? nullptr : query.getStructureAnchorById(anchor.id);
        const std::vector<MapFootprintCellFact> footprintCells =
            query.getStructureFootprintCells(anchor);
        const MapIsoDiamondBounds* byIdBounds = anchor.id.empty()
            ? nullptr
            : query.getStructureFootprintIsoDiamondBoundsById(anchor.id);
        const bool byIdBuildable = !anchor.id.empty()
            && query.isStructureFootprintBuildableById(anchor.id);
        const std::vector<MapFootprintCellFact> edgeCells =
            query.getStructureEdgeCells(anchor);
        const std::vector<MapFootprintCellFact> approachCells =
            query.getStructureApproachCells(anchor, "attack/engineer-enter/repair/occupy");
        const std::vector<MapFootprintCellFact> attackApproachCells =
            query.getStructureAttackApproachCells(anchor);
        const std::vector<MapFootprintCellFact> engineerApproachCells =
            query.getStructureEngineerApproachCells(anchor);
        const std::vector<MapFootprintCellFact> repairApproachCells =
            query.getStructureRepairApproachCells(anchor);
        const std::vector<MapFootprintCellFact> occupyApproachCells =
            query.getStructureOccupyApproachCells(anchor);
        state.report.warnings.push_back(
            "[map-fact] buildingAnchor id="
            + (anchor.id.empty() ? "-" : anchor.id)
            + " type="
            + (anchor.type.empty() ? "-" : anchor.type)
            + " owner="
            + (anchor.owner.empty() ? "-" : anchor.owner)
            + " mapCell="
            + std::to_string(anchor.x)
            + ","
            + std::to_string(anchor.y)
            + " footprint="
            + std::to_string(anchor.footprintWidth)
            + "x"
            + std::to_string(anchor.footprintHeight)
            + " rearCell="
            + std::to_string(anchor.rearCellX)
            + ","
            + std::to_string(anchor.rearCellY)
            + " screenIsoDiamondBounds="
            + isoDiamondBoundsSummary(anchor.footprintIsoDiamondBounds)
            + " placementPreviewBounds="
            + isoDiamondBoundsSummary(anchor.placementPreviewIsoDiamondBounds)
            + " hitTestBounds="
            + isoDiamondBoundsSummary(anchor.hitTestIsoDiamondBounds)
            + " selectionBounds="
            + isoDiamondBoundsSummary(anchor.selectionIsoDiamondBounds)
            + " footprintBuildable="
            + yesNo(anchor.footprintBuildable)
            + " buildableCells="
            + std::to_string(anchor.footprintBuildableCellCount)
            + " illegalCells="
            + std::to_string(anchor.footprintIllegalCellCount)
            + " waterCells="
            + std::to_string(anchor.footprintWaterCellCount)
            + " shoreCells="
            + std::to_string(anchor.footprintShoreCellCount)
            + " bridgeCells="
            + std::to_string(anchor.footprintBridgeCellCount)
            + " resourceCells="
            + std::to_string(anchor.footprintResourceCellCount)
            + " overlayBlockerCells="
            + std::to_string(anchor.footprintOverlayBlockerCellCount)
            + " occupiedCells="
            + std::to_string(anchor.footprintOccupiedCellCount)
            + " otherStructureCells="
            + std::to_string(anchor.footprintOtherStructureCellCount)
            + " legality="
            + anchor.footprintLegality
            + " anchorFormula="
            + anchor.anchorFormula
            + " placementPreviewFormula="
            + anchor.placementPreviewFormula
            + " hitTestFormula="
            + anchor.hitTestFormula
            + " selectionFormula="
            + anchor.selectionFormula
            + " finalStructureRectFormula="
            + anchor.finalStructureRectFormula
            + " footprintSource="
            + anchor.footprintSource
            + " footprintCells="
            + std::to_string(anchor.footprintCells.size())
            + " edgeCells="
            + std::to_string(anchor.footprintEdgeCells.size())
            + " approachCells="
            + std::to_string(anchor.approachCells.size())
            + " queryById="
            + yesNo(indexedAnchor != nullptr)
            + " queryBoundsById="
            + (byIdBounds == nullptr ? "-" : isoDiamondBoundsSummary(*byIdBounds))
            + " queryBuildableById="
            + yesNo(byIdBuildable)
            + " queryFootprintCells="
            + std::to_string(footprintCells.size())
            + " queryEdgeCells="
            + std::to_string(edgeCells.size())
            + " queryApproachCells="
            + std::to_string(approachCells.size())
            + " attackApproachCells="
            + std::to_string(attackApproachCells.size())
            + " engineerApproachCells="
            + std::to_string(engineerApproachCells.size())
            + " repairApproachCells="
            + std::to_string(repairApproachCells.size())
            + " occupyApproachCells="
            + std::to_string(occupyApproachCells.size())
            + " blockedFallback="
            + anchor.blockedTargetFallbackHint
        );
        const std::size_t footprintSampleLimit =
            std::min<std::size_t>(anchor.footprintCells.size(), 8);
        for (std::size_t cellIndex = 0; cellIndex < footprintSampleLimit; ++cellIndex) {
            state.report.warnings.push_back(
                "[map-query] buildingFootprintCellSample id="
                + (anchor.id.empty() ? "-" : anchor.id)
                + " cell="
                + footprintCellSummary(anchor.footprintCells[cellIndex])
            );
        }
        if (anchor.footprintCells.size() > footprintSampleLimit) {
            state.report.warnings.push_back(
                "[map-query] buildingFootprintCellSamplesTruncated id="
                + (anchor.id.empty() ? "-" : anchor.id)
                + " total="
                + std::to_string(anchor.footprintCells.size())
            );
        }
        if (!approachCells.empty()) {
            state.report.warnings.push_back(
                "[map-query] buildingApproachSample id="
                + (anchor.id.empty() ? "-" : anchor.id)
                + " purpose=attack/engineer-enter/repair/occupy cell="
                + footprintCellSummary(approachCells.front())
            );
        }
        if (!attackApproachCells.empty()
            || !engineerApproachCells.empty()
            || !repairApproachCells.empty()
            || !occupyApproachCells.empty()) {
            state.report.warnings.push_back(
                "[map-query] buildingApproachPurposeSamples id="
                + (anchor.id.empty() ? "-" : anchor.id)
                + " attack="
                + (attackApproachCells.empty()
                    ? "-"
                    : footprintCellSummary(attackApproachCells.front()))
                + " engineer-enter="
                + (engineerApproachCells.empty()
                    ? "-"
                    : footprintCellSummary(engineerApproachCells.front()))
                + " repair="
                + (repairApproachCells.empty()
                    ? "-"
                    : footprintCellSummary(repairApproachCells.front()))
                + " occupy="
                + (occupyApproachCells.empty()
                    ? "-"
                    : footprintCellSummary(occupyApproachCells.front()))
            );
        }
    }
    if (facts.structureAnchors.size() > anchorLimit) {
        state.report.warnings.push_back(
            "[map-fact] buildingAnchor samplesTruncated total="
            + std::to_string(facts.structureAnchors.size())
        );
    }
}

void appendObjectCapability(
    CapabilityReportState& state,
    const std::vector<ArtObjectLink>& links,
    const std::string& objectId
) {
    if (const UnitType* infantry = findCapabilityUnit(state.rules.getInfantryTypes(), objectId)) {
        appendUnitCapability(
            state,
            "infantry",
            *infantry,
            findCapabilityLink(links, "infantry", objectId)
        );
        return;
    }

    if (const UnitType* vehicle = findCapabilityUnit(state.rules.getVehicleTypes(), objectId)) {
        appendUnitCapability(
            state,
            "vehicle",
            *vehicle,
            findCapabilityLink(links, "vehicle", objectId)
        );
        return;
    }

    if (const UnitType* aircraft = findCapabilityUnit(state.rules.getAircraftTypes(), objectId)) {
        appendUnitCapability(
            state,
            "aircraft",
            *aircraft,
            findCapabilityLink(links, "aircraft", objectId)
        );
        return;
    }

    if (const BuildingType* building = findCapabilityBuilding(state.rules, objectId)) {
        appendBuildingCapability(
            state,
            *building,
            findCapabilityLink(links, "building", objectId)
        );
        return;
    }

    recordLogicalCapability(
        state,
        "object " + objectId,
        "rules section not found in infantry/vehicle/aircraft/building lists"
    );
}

AssetInspectionReport inspectCapabilityReport(const std::string& path) {
    AssetInspectionReport report;
    report.path = path;
    report.extension = "capability://";
    report.kind = "Capability resource report";
    report.success = true;

    const CapabilityOptions options = parseCapabilityOptions(path);
    addField(report, "rulesPath", options.rulesPath.empty() ? "(missing)" : options.rulesPath);
    addField(report, "artPath", options.artPath.empty() ? "(missing)" : options.artPath);
    addField(report, "assetRootPath", options.assetRootPath.empty() ? "(missing)" : options.assetRootPath);
    addField(report, "mapPath", options.mapPath.empty() ? "(not requested)" : options.mapPath);
    addField(report, "theaterIniPath", options.theaterIniPath.empty() ? "(not requested)" : options.theaterIniPath);
    addField(report, "tileRootPath", options.tileRootPath.empty() ? "(not requested)" : options.tileRootPath);
    addField(report, "reportMode", options.reportMode);
    addField(report, "theater", options.theater);
    addField(report, "objects", joinStrings(options.objectIds));
    addField(report, "includeUi", yesNo(options.includeUi));

    if (options.rulesPath.empty() || options.artPath.empty() || options.assetRootPath.empty()) {
        report.success = false;
        report.errorMessage =
            "capability:// requires rules=<path>;art=<path>;assetRoot=<dir>";
        return report;
    }

    IniFile rulesIni;
    if (!rulesIni.loadFromFile(options.rulesPath)) {
        report.success = false;
        report.errorMessage = "Failed to load rules INI: " + options.rulesPath;
        return report;
    }

    RulesDatabase rules;
    if (!rules.loadFromIni(rulesIni)) {
        report.success = false;
        report.errorMessage = "Failed to parse rules INI: " + options.rulesPath;
        return report;
    }

    IniFile artIni;
    if (!artIni.loadFromFile(options.artPath)) {
        report.success = false;
        report.errorMessage = "Failed to load art INI: " + options.artPath;
        return report;
    }

    ArtDatabase art;
    if (!art.loadFromIni(artIni)) {
        report.success = false;
        report.errorMessage = "Failed to parse art INI: " + options.artPath;
        return report;
    }

    const CapabilityResourceIndexBuild indexBuild =
        buildCapabilityResourceIndex(options.assetRootPath);
    const CapabilityResourceIndex& index = *indexBuild.index;
    addField(report, "looseResourceNames", static_cast<unsigned long long>(index.filesByName.size()));
    addField(report, "resourceIndexLooseResources", static_cast<unsigned long long>(index.looseResourceCount));
    addField(report, "mixArchives", static_cast<unsigned long long>(index.mixArchives.size()));
    addField(report, "resourceIndexMixPathEntries", static_cast<unsigned long long>(index.mixArchiveByPath.size()));
    addField(report, "resourceIndexMixEntryRefs", static_cast<unsigned long long>(index.mixEntryRefCount));
    addField(report, "resourceIndexMixHashBuckets", static_cast<unsigned long long>(index.mixEntriesById.size()));
    addField(report, "resourceIndexProcessCacheHit", yesNo(indexBuild.processCacheHit));
    addField(report, "resourceIndexProcessCacheEntries", static_cast<unsigned long long>(indexBuild.processCacheEntries));
    addField(report, "resourceIndexCacheKey", indexBuild.cacheKey);
    addField(report, "resourceIndexCacheStatus", indexBuild.cacheStatus);
    addField(
        report,
        "performanceColdPath",
        "assetRoot recursive scan and MIX directory indexing happen once per CLI process; decode/resolution caches are per capability report; no persistent cache is written"
    );
    addField(
        report,
        "summaryDecodePolicy",
        options.verbose
            ? "verbose reports every probed resource; unique source bytes still decode once"
            : "summary suppresses decodable detail; unique source bytes decode once only when needed for coverage or unsupported classification"
    );
    for (const std::string& warning : index.warnings) {
        report.warnings.push_back("[index-warning] " + warning);
    }

    const std::vector<ArtObjectLink> links = art.buildLinks(rules);
    const std::vector<MapStructureFootprintOverride> footprintOverrides =
        buildMapFootprintOverrides(links);
    CapabilityReportState state{report, index, rules, art, options.theater, options.verbose};
    if (!options.mapPath.empty()) {
        IniFile mapIni;
        if (!mapIni.loadFromFile(options.mapPath)) {
            report.warnings.push_back(
                "[map-fact-warning] failed to load map INI: " + options.mapPath
            );
        } else {
            MapDatabase mapDatabase;
            if (!mapDatabase.loadFromIni(mapIni, &rulesIni)) {
                report.warnings.push_back(
                    "[map-fact-warning] failed to parse map INI: " + options.mapPath
                );
            } else {
                bool hasTheaterDatabase = false;
                TheaterDatabase theaterDatabase;
                if (!options.theaterIniPath.empty()) {
                    IniFile theaterIni;
                    if (!theaterIni.loadFromFile(options.theaterIniPath)) {
                        report.warnings.push_back(
                            "[map-fact-warning] failed to load theater INI: "
                            + options.theaterIniPath
                        );
                    } else if (!theaterDatabase.loadFromIni(theaterIni, options.theaterIniPath)) {
                        report.warnings.push_back(
                            "[map-fact-warning] failed to parse theater INI: "
                            + options.theaterIniPath
                        );
                    } else {
                        hasTheaterDatabase = true;
                    }
                }

                const MapFacts facts = mapDatabase.buildMapFacts(
                    hasTheaterDatabase ? &theaterDatabase : nullptr,
                    options.tileRootPath,
                    footprintOverrides
                );
                appendMapFactsCapability(state, facts);
            }
        }
    }
    for (const std::string& objectId : options.objectIds) {
        appendObjectCapability(state, links, objectId);
    }
    if (options.includeUi) {
        appendUiCapability(state);
    }
    appendUiCoverageFields(state);
    appendEvaAudioCapability(state);

    addField(report, "requiredResources", state.requiredResourceCount);
    addField(report, "resourcesPresentDecodable", state.presentDecodableCount);
    addField(report, "resourcesPresentUnsupportedOrPalette", state.presentUnsupportedCount);
    addField(report, "resourcesMissing", state.missingCount);
    addField(report, "logicalReferences", state.logicalCount);
    addField(report, "suppressedSummaryDetails", state.suppressedDetailCount);
    addField(report, "suppressedDuplicateProblems", state.suppressedDuplicateProblemCount);
    addField(report, "resourceIndexHits", state.resourceIndexHits);
    addField(report, "resourceIndexMisses", state.resourceIndexMisses);
    addField(report, "resolutionCacheEntries", static_cast<unsigned long long>(state.resolutionCache.size()));
    addField(report, "resolutionCacheHits", state.resolutionCacheHits);
    addField(report, "resolutionCacheMisses", state.resolutionCacheMisses);
    addField(report, "decodeCacheEntries", static_cast<unsigned long long>(state.decodeCache.size()));
    addField(report, "decodeCacheHits", state.decodeCacheHits);
    addField(report, "decodeCacheMisses", state.decodeCacheMisses);
    const unsigned int covered =
        state.presentDecodableCount + state.presentUnsupportedCount;
    const unsigned int percent =
        state.requiredResourceCount == 0
            ? 100U
            : (covered * 100U) / state.requiredResourceCount;
    addField(report, "capabilityResourceCoverage", std::to_string(covered)
        + "/"
        + std::to_string(state.requiredResourceCount)
        + " ("
        + std::to_string(percent)
        + "% present, including decoder/palette gaps)"
    );
    addField(
        report,
        "mixReexportNeeded",
        state.missingCount == 0
            ? "no for inspected resources"
            : "only for resources listed under [missing], after checking optional/logical cases"
    );
    report.warnings.push_back(
        "[capability-summary] mode="
        + options.reportMode
        + " resources="
        + std::to_string(covered)
        + "/"
        + std::to_string(state.requiredResourceCount)
        + " unsupported="
        + std::to_string(state.presentUnsupportedCount)
        + " missing="
        + std::to_string(state.missingCount)
        + " decodeCache="
        + std::to_string(state.decodeCacheHits)
        + "/"
        + std::to_string(state.decodeCacheHits + state.decodeCacheMisses)
        + " decodeCacheEntries="
        + std::to_string(state.decodeCache.size())
        + " resolutionCache="
        + std::to_string(state.resolutionCacheHits)
        + "/"
        + std::to_string(state.resolutionCacheHits + state.resolutionCacheMisses)
        + " resolutionCacheEntries="
        + std::to_string(state.resolutionCache.size())
        + " indexHits="
        + std::to_string(state.resourceIndexHits)
        + " processIndexCache="
        + (indexBuild.processCacheHit ? "hit" : "miss")
        + " suppressedDuplicateProblems="
        + std::to_string(state.suppressedDuplicateProblemCount)
        + " noReexportNeeded="
        + (state.missingCount == 0 ? "yes" : "check-missing")
    );

    return report;
}

} // namespace

AssetInspectionReport inspectAssetFile(const std::string& path) {
    if (startsWithCapabilityPrefix(path)) {
        return inspectCapabilityReport(path);
    }

    AssetInspectionReport report;
    report.path = path;

    const std::filesystem::path filesystemPath(path);
    report.extension = toLower(filesystemPath.extension().string());

    std::vector<unsigned char> bytes;
    if (!loadBytes(path, bytes, report.errorMessage)) {
        report.success = false;
        return report;
    }

    report.fileSize = static_cast<unsigned long long>(bytes.size());
    report.success = true;

    if (report.extension == ".pal") {
        inspectPalette(report, bytes);
    } else if (report.extension == ".shp") {
        inspectShp(report, bytes);
    } else if (report.extension == ".vxl") {
        inspectVxl(report, bytes);
    } else if (report.extension == ".hva") {
        inspectHva(report, bytes);
    } else if (report.extension == ".pcx") {
        inspectPcxUnsupported(report, bytes);
    } else if (
        report.extension == ".tmp"
        || report.extension == ".tem"
        || report.extension == ".sno"
        || report.extension == ".urb"
        || report.extension == ".des"
        || report.extension == ".lun"
        || report.extension == ".ubn"
    ) {
        inspectTileLike(report, bytes);
    } else {
        inspectUnknown(report, bytes);
    }

    return report;
}

AssetInspectionReport inspectVxlHvaPair(
    const std::string& vxlPath,
    const std::string& hvaPath
) {
    AssetInspectionReport report;
    report.path = vxlPath + " | " + hvaPath;
    report.extension = ".vxl+.hva";
    report.kind = "VXL/HVA pair";

    std::vector<unsigned char> vxlBytes;
    if (!loadBytes(vxlPath, vxlBytes, report.errorMessage)) {
        report.success = false;
        report.errorMessage = "Failed to load VXL file: " + report.errorMessage;
        return report;
    }

    std::vector<unsigned char> hvaBytes;
    if (!loadBytes(hvaPath, hvaBytes, report.errorMessage)) {
        report.success = false;
        report.errorMessage = "Failed to load HVA file: " + report.errorMessage;
        return report;
    }

    VxlMetadata vxlMetadata;
    if (!parseVxlMetadata(vxlBytes, vxlMetadata, report.errorMessage)) {
        report.success = false;
        report.errorMessage = "Failed to parse VXL file: " + report.errorMessage;
        return report;
    }

    HvaMetadata hvaMetadata;
    if (!parseHvaMetadata(hvaBytes, hvaMetadata, report.errorMessage)) {
        report.success = false;
        report.errorMessage = "Failed to parse HVA file: " + report.errorMessage;
        return report;
    }

    report.success = true;
    report.fileSize =
        static_cast<unsigned long long>(vxlBytes.size())
        + static_cast<unsigned long long>(hvaBytes.size());

    addField(report, "vxlFileSize", static_cast<unsigned long long>(vxlBytes.size()));
    addField(report, "hvaFileSize", static_cast<unsigned long long>(hvaBytes.size()));
    addField(report, "vxlSignature", vxlMetadata.signature);
    addField(report, "vxlSectionCount", vxlMetadata.sectionCount);
    addField(report, "hvaSectionCount", hvaMetadata.sectionCount);
    addField(report, "hvaFrameCount", hvaMetadata.frameCount);
    addField(report, "sectionCountMatch", yesNo(vxlMetadata.sectionCount == hvaMetadata.sectionCount));
    addField(report, "vxlSectionNames", formatSectionNames(vxlMetadata.sectionNames));
    addField(report, "hvaSectionNames", formatSectionNames(hvaMetadata.sectionNames));
    addField(report, "sectionNamesMatch", yesNo(sectionNamesMatch(
        vxlMetadata.sectionNames,
        hvaMetadata.sectionNames
    )));
    addField(report, "vxlBodySize", vxlMetadata.bodySize);
    addField(report, "vxlPaletteMarker", vxlMetadata.paletteMarker);
    addField(report, "vxlEmbeddedPaletteMaxChannel", vxlMetadata.embeddedPaletteMaxChannel);
    addField(report, "vxlSectionRecordBytes", static_cast<unsigned long long>(vxlMetadata.sectionRecordBytes));
    addField(report, "hvaMatrixDataOffset", static_cast<unsigned long long>(hvaMetadata.matrixDataOffset));
    addField(report, "hvaExpectedFileBytes", static_cast<unsigned long long>(hvaMetadata.expectedFileBytes));

    if (hvaMetadata.firstMatrix.size() == 12) {
        addField(report, "hvaFirstMatrixRow0", formatMatrixRow(hvaMetadata.firstMatrix, 0));
        addField(report, "hvaFirstMatrixRow1", formatMatrixRow(hvaMetadata.firstMatrix, 1));
        addField(report, "hvaFirstMatrixRow2", formatMatrixRow(hvaMetadata.firstMatrix, 2));
    }

    if (vxlMetadata.signature != "Voxel Animation") {
        report.warnings.push_back("VXL signature is not 'Voxel Animation'.");
    }

    if (vxlMetadata.sectionCount != vxlMetadata.sectionCountMirror) {
        report.warnings.push_back("VXL section count mirror does not match section count.");
    }

    if (vxlMetadata.sectionCount != hvaMetadata.sectionCount) {
        report.warnings.push_back("VXL and HVA section counts do not match.");
    }

    if (!sectionNamesMatch(vxlMetadata.sectionNames, hvaMetadata.sectionNames)) {
        report.warnings.push_back("VXL and HVA section names do not match.");
    }

    if (hvaMetadata.expectedFileBytes < hvaBytes.size()) {
        report.warnings.push_back("HVA file has extra trailing bytes after declared matrix data.");
    }

    return report;
}

bool exportShpFirstFrameToPpm(
    const std::string& shpPath,
    const std::string& palettePath,
    const std::string& outputPath,
    std::string& errorMessage
) {
    std::vector<unsigned char> shpBytes;
    if (!loadBytes(shpPath, shpBytes, errorMessage)) {
        return false;
    }

    std::vector<unsigned char> paletteBytes;
    if (!loadBytes(palettePath, paletteBytes, errorMessage)) {
        return false;
    }

    std::vector<RgbColor> palette;
    if (!decodeRa2Palette(paletteBytes, palette, errorMessage)) {
        return false;
    }

    if (shpBytes.size() < 32) {
        errorMessage = "SHP file is too small for the supported raw frame layout.";
        return false;
    }

    const unsigned int width = readUInt16(shpBytes, 2);
    const unsigned int height = readUInt16(shpBytes, 4);
    const unsigned int frameCount = readUInt16(shpBytes, 6);
    const unsigned int firstFrameWidth = readUInt16(shpBytes, 12);
    const unsigned int firstFrameHeight = readUInt16(shpBytes, 14);
    const unsigned int firstFrameFormat = readUInt16(shpBytes, 16);

    if (frameCount == 0) {
        errorMessage = "SHP file has no frames.";
        return false;
    }

    if (firstFrameFormat != 1) {
        errorMessage =
            "Only raw SHP frame format 1 is supported in this phase. "
            "Compressed SHP decoding will be added later.";
        return false;
    }

    if (firstFrameWidth != width || firstFrameHeight != height) {
        errorMessage = "SHP first frame dimensions do not match the file dimensions.";
        return false;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t pixelDataOffset = 32;

    if (pixelDataOffset + pixelCount > shpBytes.size()) {
        errorMessage = "SHP raw pixel data is shorter than expected.";
        return false;
    }

    std::vector<unsigned char> pixelIndexes(
        shpBytes.begin() + static_cast<std::ptrdiff_t>(pixelDataOffset),
        shpBytes.begin() + static_cast<std::ptrdiff_t>(pixelDataOffset + pixelCount)
    );

    return writePpm(outputPath, width, height, pixelIndexes, palette, errorMessage);
}

bool exportTmpFirstTileToPpm(
    const std::string& tmpPath,
    const std::string& palettePath,
    const std::string& outputPath,
    std::string& errorMessage
) {
    std::vector<unsigned char> tmpBytes;
    if (!loadBytes(tmpPath, tmpBytes, errorMessage)) {
        return false;
    }

    std::vector<unsigned char> paletteBytes;
    if (!loadBytes(palettePath, paletteBytes, errorMessage)) {
        return false;
    }

    std::vector<RgbColor> palette;
    if (!decodeRa2Palette(paletteBytes, palette, errorMessage)) {
        return false;
    }

    TheaterTileMetadata metadata;
    if (!parseWrappedTileSet(tmpBytes, metadata)) {
        if (!parseRawTileSlotTable(tmpBytes, metadata)) {
            errorMessage = "TMP file does not use a supported tile layout.";
            return false;
        }
        return exportRawTmpFirstTileToPpm(
            tmpBytes,
            metadata,
            palette,
            outputPath,
            errorMessage
        );
    }

    if (metadata.presentOffsets.empty()) {
        errorMessage = "TMP file has no present sub-tile images.";
        return false;
    }

    constexpr std::size_t imageHeaderBytes = 52;
    const unsigned int cellWidth = metadata.blockWidth;
    const unsigned int cellHeight = metadata.blockHeight;

    std::size_t baseColorBytes = 0;
    unsigned int rowWidth = 4;
    for (unsigned int y = 0; y < cellHeight; ++y) {
        if (rowWidth > cellWidth) {
            errorMessage = "TMP first sub-tile packed row is wider than the tile image.";
            return false;
        }

        baseColorBytes += rowWidth;
        if (y < (cellHeight / 2) - 1) {
            rowWidth += 4;
        } else {
            rowWidth -= 4;
        }
    }

    if (baseColorBytes == 0) {
        errorMessage = "TMP first sub-tile base color data is empty.";
        return false;
    }

    const std::size_t tileDataOffset = metadata.presentOffsets.front();
    std::size_t tileRecordEnd = tmpBytes.size();
    for (const std::size_t candidate : metadata.presentOffsets) {
        if (candidate > tileDataOffset && candidate < tileRecordEnd) {
            tileRecordEnd = candidate;
        }
    }

    const std::size_t tileRecordBytes = tileRecordEnd - tileDataOffset;
    if (tileRecordBytes < imageHeaderBytes + baseColorBytes) {
        errorMessage = "TMP first sub-tile record is too small for base pixels.";
        return false;
    }

    const int baseOffsetX = readInt32(tmpBytes, tileDataOffset);
    const int baseOffsetY = readInt32(tmpBytes, tileDataOffset + 4);
    const unsigned int extraDataOffset = readUInt32(tmpBytes, tileDataOffset + 8);
    const unsigned int zDataOffset = readUInt32(tmpBytes, tileDataOffset + 12);
    const unsigned int extraZDataOffset = readUInt32(tmpBytes, tileDataOffset + 16);
    const int extraOffsetX = readInt32(tmpBytes, tileDataOffset + 20);
    const int extraOffsetY = readInt32(tmpBytes, tileDataOffset + 24);
    const unsigned int extraWidth = readUInt32(tmpBytes, tileDataOffset + 28);
    const unsigned int extraHeight = readUInt32(tmpBytes, tileDataOffset + 32);

    const std::size_t expectedZDataOffset = imageHeaderBytes + baseColorBytes;
    if (zDataOffset != 0
        && zDataOffset != 0xCDCDCDCDU
        && static_cast<std::size_t>(zDataOffset) < expectedZDataOffset) {
        errorMessage = "TMP first sub-tile z-data offset overlaps base color data.";
        return false;
    }

    std::size_t extraPixelCount = 0;
    const bool hasExtra =
        extraWidth > 0
        && extraHeight > 0
        && extraWidth <= 512
        && extraHeight <= 512
        && checkedMul(
            static_cast<std::size_t>(extraWidth),
            static_cast<std::size_t>(extraHeight),
            extraPixelCount
        )
        && extraDataOffset >= imageHeaderBytes
        && static_cast<std::size_t>(extraDataOffset) + extraPixelCount <= tileRecordBytes
        && (
            extraZDataOffset == 0
            || extraZDataOffset == 0xCDCDCDCDU
            || static_cast<std::size_t>(extraZDataOffset) + extraPixelCount <= tileRecordBytes
        );

    int minX = baseOffsetX;
    int minY = baseOffsetY;
    int maxX = baseOffsetX + static_cast<int>(cellWidth);
    int maxY = baseOffsetY + static_cast<int>(cellHeight);

    if (hasExtra) {
        minX = std::min(minX, extraOffsetX);
        minY = std::min(minY, extraOffsetY);
        maxX = std::max(maxX, extraOffsetX + static_cast<int>(extraWidth));
        maxY = std::max(maxY, extraOffsetY + static_cast<int>(extraHeight));
    }

    if (maxX <= minX || maxY <= minY) {
        errorMessage = "TMP first sub-tile has invalid image bounds.";
        return false;
    }

    const unsigned int outputWidth = static_cast<unsigned int>(maxX - minX);
    const unsigned int outputHeight = static_cast<unsigned int>(maxY - minY);
    std::vector<unsigned char> pixelIndexes(
        static_cast<std::size_t>(outputWidth) * static_cast<std::size_t>(outputHeight),
        0
    );

    const std::size_t basePixelDataOffset = tileDataOffset + imageHeaderBytes;
    std::size_t sourceIndex = basePixelDataOffset;
    rowWidth = 4;
    for (unsigned int y = 0; y < cellHeight; ++y) {
        const unsigned int xStart = (cellWidth - rowWidth) / 2;
        for (unsigned int x = 0; x < rowWidth; ++x) {
            const unsigned char paletteIndex = tmpBytes[sourceIndex];
            ++sourceIndex;
            if (paletteIndex == 0) {
                continue;
            }

            const int targetX = baseOffsetX - minX + static_cast<int>(xStart + x);
            const int targetY = baseOffsetY - minY + static_cast<int>(y);
            pixelIndexes[
                (static_cast<std::size_t>(targetY) * outputWidth)
                + static_cast<std::size_t>(targetX)
            ] = paletteIndex;
        }

        if (y < (cellHeight / 2) - 1) {
            rowWidth += 4;
        } else {
            rowWidth -= 4;
        }
    }

    if (hasExtra) {
        const std::size_t extraPixelDataOffset =
            tileDataOffset + static_cast<std::size_t>(extraDataOffset);
        for (unsigned int y = 0; y < extraHeight; ++y) {
            for (unsigned int x = 0; x < extraWidth; ++x) {
                const unsigned char paletteIndex =
                    tmpBytes[
                        extraPixelDataOffset
                        + (static_cast<std::size_t>(y) * extraWidth)
                        + x
                    ];
                if (paletteIndex == 0) {
                    continue;
                }

                const int targetX = extraOffsetX - minX + static_cast<int>(x);
                const int targetY = extraOffsetY - minY + static_cast<int>(y);
                pixelIndexes[
                    (static_cast<std::size_t>(targetY) * outputWidth)
                    + static_cast<std::size_t>(targetX)
                ] = paletteIndex;
            }
        }
    }

    return writePpm(outputPath, outputWidth, outputHeight, pixelIndexes, palette, errorMessage);
}
