#include "assets/codecs/RuntimeTmpCodec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

int readInt32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<int>(static_cast<std::int32_t>(readUInt32(bytes, offset)));
}

unsigned int readUInt16(
    const std::vector<unsigned char>& bytes,
    std::size_t offset
) {
    if (offset + 2 > bytes.size()) {
        return 0;
    }

    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
}

int signExtendUInt16(unsigned int value) {
    value &= 0xFFFFU;
    if ((value & 0x8000U) == 0) {
        return static_cast<int>(value);
    }

    return static_cast<int>(value) - 0x10000;
}

std::size_t findNextTmpImageOffset(
    const std::vector<std::size_t>& imageOffsets,
    std::size_t currentOffset,
    std::size_t fileSize
) {
    std::size_t nextOffset = fileSize;
    for (const std::size_t candidate : imageOffsets) {
        if (candidate > currentOffset && candidate < nextOffset) {
            nextOffset = candidate;
        }
    }

    return nextOffset;
}

bool isValidTmpImageOffset(
    std::size_t imageOffset,
    std::size_t tableEnd,
    std::size_t fileSize
) {
    return imageOffset >= tableEnd && imageOffset < fileSize;
}

bool validateTmpImageOffsets(
    const std::vector<std::size_t>& imageOffsets,
    std::size_t tableEnd,
    std::size_t fileSize,
    const std::string& path,
    std::string& errorMessage
) {
    for (const std::size_t imageOffset : imageOffsets) {
        if (imageOffset == 0) {
            continue;
        }

        if (!isValidTmpImageOffset(imageOffset, tableEnd, fileSize)) {
            errorMessage = "TMP image offset is outside file: " + path;
            return false;
        }
    }

    return true;
}

bool decodeTmpImagePixels(
    const std::vector<unsigned char>& bytes,
    std::size_t imageOffset,
    std::size_t imageRecordEnd,
    unsigned int blockWidth,
    unsigned int blockHeight,
    RuntimeTmpSubTileImage& image,
    std::string& errorMessage
) {
    constexpr std::size_t imageHeaderBytes = 52;

    if (blockWidth == 0 || blockHeight == 0 || imageRecordEnd <= imageOffset) {
        errorMessage = "TMP tile has invalid image dimensions.";
        return false;
    }

    std::size_t baseColorBytes = 0;
    unsigned int rowWidth = 4;
    for (unsigned int y = 0; y < blockHeight; ++y) {
        if (rowWidth > blockWidth) {
            errorMessage = "TMP packed diamond row is wider than the tile image.";
            return false;
        }

        baseColorBytes += rowWidth;
        if (y < (blockHeight / 2) - 1) {
            rowWidth += 4;
        } else {
            rowWidth -= 4;
        }
    }

    if (baseColorBytes == 0) {
        errorMessage = "TMP base color data is empty.";
        return false;
    }

    const std::size_t imageRecordBytes = imageRecordEnd - imageOffset;
    if (imageRecordBytes < imageHeaderBytes + baseColorBytes) {
        errorMessage = "TMP image record is too small for base pixels.";
        return false;
    }

    const int rawBaseOffsetX = readInt32(bytes, imageOffset);
    const int rawBaseOffsetY = readInt32(bytes, imageOffset + 4);
    const unsigned int dataBlockSize = readUInt32(bytes, imageOffset + 16);
    const int rawExtraOffsetX = readInt32(bytes, imageOffset + 20);
    const int rawExtraOffsetY = readInt32(bytes, imageOffset + 24);
    const unsigned int extraWidth = readUInt32(bytes, imageOffset + 28);
    const unsigned int extraHeight = readUInt32(bytes, imageOffset + 32);
    const unsigned int flags = readUInt32(bytes, imageOffset + 36);
    const unsigned char terrainType = bytes[imageOffset + 41];
    const unsigned char rampType = bytes[imageOffset + 42];
    const RuntimePaletteColor radarLeft{
        bytes[imageOffset + 43],
        bytes[imageOffset + 44],
        bytes[imageOffset + 45]
    };
    const RuntimePaletteColor radarRight{
        bytes[imageOffset + 46],
        bytes[imageOffset + 47],
        bytes[imageOffset + 48]
    };

    constexpr unsigned int tmpImageFlagExtraData = 1U;
    constexpr unsigned int tmpImageFlagZData = 2U;

    std::size_t extraPixelCount = 0;
    const bool hasBaseZData = (flags & tmpImageFlagZData) != 0;
    const bool hasExtra =
        (flags & tmpImageFlagExtraData) != 0
        && extraWidth > 0
        && extraHeight > 0
        && extraWidth <= 512
        && extraHeight <= 512
        && checkedMul(
            static_cast<std::size_t>(extraWidth),
            static_cast<std::size_t>(extraHeight),
            extraPixelCount
        );
    const std::size_t basePixelDataOffset = imageOffset + imageHeaderBytes;
    const std::size_t zDataOffset =
        imageHeaderBytes + baseColorBytes;
    const std::size_t extraDataOffset =
        zDataOffset + (hasBaseZData ? baseColorBytes : 0);
    const std::size_t extraZDataOffset =
        extraDataOffset + (hasExtra ? extraPixelCount : 0);
    if (hasBaseZData
        && zDataOffset + baseColorBytes > imageRecordBytes) {
        errorMessage = "TMP z-data extends past the image record.";
        return false;
    }
    if (hasExtra
        && extraDataOffset + extraPixelCount > imageRecordBytes) {
        errorMessage = "TMP extra data extends past the image record.";
        return false;
    }

    const int baseOffsetX = 0;
    const int baseOffsetY = 0;
    const int extraOffsetX = rawExtraOffsetX - rawBaseOffsetX;
    const int extraOffsetY = rawExtraOffsetY - rawBaseOffsetY;

    int minX = baseOffsetX;
    int minY = baseOffsetY;
    int maxX = baseOffsetX + static_cast<int>(blockWidth);
    int maxY = baseOffsetY + static_cast<int>(blockHeight);

    if (hasExtra) {
        minX = std::min(minX, extraOffsetX);
        minY = std::min(minY, extraOffsetY);
        maxX = std::max(maxX, extraOffsetX + static_cast<int>(extraWidth));
        maxY = std::max(maxY, extraOffsetY + static_cast<int>(extraHeight));
    }

    if (maxX <= minX || maxY <= minY) {
        errorMessage = "TMP image bounds are invalid.";
        return false;
    }

    image.present = true;
    image.hasTerrainType = true;
    image.terrainType = terrainType;
    image.rampType = rampType;
    image.hasRadarColors = true;
    image.radarLeft = radarLeft;
    image.radarRight = radarRight;
    image.offsetX = minX;
    image.offsetY = minY;
    image.width = maxX - minX;
    image.height = maxY - minY;
    image.baseOffsetX = baseOffsetX;
    image.baseOffsetY = baseOffsetY;
    image.baseWidth = static_cast<int>(blockWidth);
    image.baseHeight = static_cast<int>(blockHeight);
    image.basePixels.assign(baseColorBytes, 0);
    image.baseZData.clear();
    image.hasBaseZData = hasBaseZData;
    if (hasBaseZData) {
        const std::size_t zDataAbsoluteOffset =
            imageOffset + static_cast<std::size_t>(zDataOffset);
        image.baseZData.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(zDataAbsoluteOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(zDataAbsoluteOffset + baseColorBytes)
        );
    }
    image.hasExtra = hasExtra;
    image.extraOffsetX = extraOffsetX;
    image.extraOffsetY = extraOffsetY;
    image.extraWidth = hasExtra ? static_cast<int>(extraWidth) : 0;
    image.extraHeight = hasExtra ? static_cast<int>(extraHeight) : 0;
    image.extraPixels.clear();
    image.extraZData.clear();
    image.hasExtraZData = false;
    image.pixels.assign(
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height),
        0
    );

    std::size_t sourceIndex = basePixelDataOffset;
    std::size_t packedBaseIndex = 0;
    rowWidth = 4;
    for (unsigned int y = 0; y < blockHeight; ++y) {
        const unsigned int xStart = (blockWidth - rowWidth) / 2;
        for (unsigned int x = 0; x < rowWidth; ++x) {
            const unsigned char paletteIndex = bytes[sourceIndex];
            ++sourceIndex;
            image.basePixels[packedBaseIndex] = paletteIndex;
            ++packedBaseIndex;
            if (paletteIndex == 0) {
                continue;
            }

            const int targetX = baseOffsetX - minX + static_cast<int>(xStart + x);
            const int targetY = baseOffsetY - minY + static_cast<int>(y);
            image.pixels[
                (static_cast<std::size_t>(targetY) * image.width)
                + static_cast<std::size_t>(targetX)
            ] = paletteIndex;
        }

        if (y < (blockHeight / 2) - 1) {
            rowWidth += 4;
        } else {
            rowWidth -= 4;
        }
    }

    if (hasExtra) {
        image.extraPixels.assign(extraPixelCount, 0);
        image.hasExtraZData =
            hasBaseZData
            && dataBlockSize > 0
            && extraZDataOffset + extraPixelCount <= imageRecordBytes;
        if (image.hasExtraZData) {
            const std::size_t extraZDataAbsoluteOffset =
                imageOffset + extraZDataOffset;
            image.extraZData.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(extraZDataAbsoluteOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(extraZDataAbsoluteOffset + extraPixelCount)
            );
        }

        const std::size_t extraPixelDataOffset = imageOffset + extraDataOffset;
        for (unsigned int y = 0; y < extraHeight; ++y) {
            for (unsigned int x = 0; x < extraWidth; ++x) {
                const unsigned char paletteIndex =
                    bytes[
                        extraPixelDataOffset
                        + (static_cast<std::size_t>(y) * extraWidth)
                        + x
                    ];
                image.extraPixels[
                    (static_cast<std::size_t>(y) * extraWidth) + x
                ] = paletteIndex;
                if (paletteIndex == 0) {
                    continue;
                }

                const int targetX = extraOffsetX - minX + static_cast<int>(x);
                const int targetY = extraOffsetY - minY + static_cast<int>(y);
                if (targetX < 0
                    || targetY < 0
                    || targetX >= image.width
                    || targetY >= image.height) {
                    errorMessage = "TMP extra pixels fall outside computed bounds.";
                    return false;
                }

                image.pixels[
                    (static_cast<std::size_t>(targetY) * image.width)
                    + static_cast<std::size_t>(targetX)
                ] = paletteIndex;
            }
        }
    }

    return true;
}

bool decodeRawTmpFormat3Pixels(
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

    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);

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

bool loadRawTmpTileFile(
    const std::vector<unsigned char>& bytes,
    const std::string& path,
    RuntimeTmpTileFile& tileFile,
    std::string& errorMessage
) {
    constexpr std::size_t preludeBytes = 8;
    constexpr std::size_t slotRecordBytes = 24;
    constexpr std::size_t dataOffsetOffset = 20;

    // This compatibility format stores SHP pixel dimensions and frame slots,
    // not the two-dimensional terrain grid found in a standard TMP header.
    tileFile.gridWidth = 0;
    tileFile.gridHeight = 0;

    if (bytes.size() < preludeBytes + slotRecordBytes) {
        errorMessage = "TMP raw slot table is too small: " + path;
        return false;
    }

    const unsigned int headerWidth = readUInt16(bytes, 2);
    const unsigned int headerHeight = readUInt16(bytes, 4);
    const unsigned int headerSlotCount = readUInt16(bytes, 6);

    std::size_t firstDataOffset = 0;
    for (
        std::size_t offsetField = preludeBytes + dataOffsetOffset;
        offsetField + 4 <= bytes.size();
        offsetField += slotRecordBytes
    ) {
        const unsigned int candidate = readUInt32(bytes, offsetField);
        if (candidate > preludeBytes
            && candidate < bytes.size()
            && ((static_cast<std::size_t>(candidate) - preludeBytes) % slotRecordBytes) == 0
            && (firstDataOffset == 0 || candidate < firstDataOffset)
        ) {
            firstDataOffset = static_cast<std::size_t>(candidate);
        }
    }

    if (firstDataOffset == 0 || firstDataOffset <= preludeBytes) {
        errorMessage = "TMP file has unsupported block dimensions: " + path;
        return false;
    }

    const std::size_t tableBytes = firstDataOffset - preludeBytes;
    if (tableBytes % slotRecordBytes != 0) {
        errorMessage = "TMP raw slot table size is not aligned: " + path;
        return false;
    }

    const std::size_t slotCount = tableBytes / slotRecordBytes;
    if (slotCount == 0 || slotCount > 4096) {
        errorMessage = "TMP raw slot count is unsupported: " + path;
        return false;
    }
    if (headerSlotCount != 0 && headerSlotCount != slotCount) {
        errorMessage = "TMP raw slot count disagrees with header: " + path;
        return false;
    }

    std::vector<std::size_t> imageOffsets(slotCount, 0);
    std::vector<std::pair<unsigned int, unsigned int>> dimensions(slotCount, {0, 0});
    std::vector<std::pair<int, int>> imageAnchors(slotCount, {0, 0});
    unsigned int maxWidth = 0;
    unsigned int maxHeight = 0;

    for (std::size_t i = 0; i < slotCount; ++i) {
        const std::size_t recordOffset = preludeBytes + (i * slotRecordBytes);
        const unsigned int packedAnchor = readUInt32(bytes, recordOffset);
        const unsigned int packedDimensions = readUInt32(bytes, recordOffset + 4);
        const unsigned int width = packedDimensions & 0xFFFFU;
        const unsigned int height = (packedDimensions >> 16) & 0xFFFFU;
        const unsigned int dataOffset = readUInt32(bytes, recordOffset + dataOffsetOffset);

        if (dataOffset == 0) {
            continue;
        }

        if (dataOffset < firstDataOffset || dataOffset >= bytes.size()) {
            errorMessage = "TMP raw image offset is outside file: " + path;
            return false;
        }

        if (width == 0 || height == 0 || width > 512 || height > 512) {
            errorMessage = "TMP raw image dimensions are unsupported: " + path;
            return false;
        }

        imageOffsets[i] = static_cast<std::size_t>(dataOffset);
        dimensions[i] = {width, height};
        imageAnchors[i] = {
            signExtendUInt16(packedAnchor & 0xFFFFU),
            signExtendUInt16((packedAnchor >> 16) & 0xFFFFU)
        };
        maxWidth = std::max(maxWidth, width);
        maxHeight = std::max(maxHeight, height);
    }

    tileFile.blockWidth = headerWidth > 0
        ? static_cast<int>(headerWidth)
        : static_cast<int>(maxWidth);
    tileFile.blockHeight = headerHeight > 0
        ? static_cast<int>(headerHeight)
        : static_cast<int>(maxHeight);
    tileFile.images.assign(slotCount, RuntimeTmpSubTileImage{});

    int decodedImages = 0;
    std::string firstImageDecodeWarning;
    for (std::size_t i = 0; i < slotCount; ++i) {
        const std::size_t imageOffset = imageOffsets[i];
        if (imageOffset == 0) {
            continue;
        }

        const std::size_t imageRecordEnd =
            findNextTmpImageOffset(imageOffsets, imageOffset, bytes.size());
        std::vector<unsigned char> pixels;
        if (!decodeRawTmpFormat3Pixels(
                bytes,
                imageOffset,
                imageRecordEnd,
                dimensions[i].first,
                dimensions[i].second,
                pixels,
                errorMessage
            )
        ) {
            if (firstImageDecodeWarning.empty()) {
                firstImageDecodeWarning =
                    errorMessage + " slot=" + std::to_string(i)
                    + " path=" + path;
            }
            continue;
        }

        RuntimeTmpSubTileImage& image = tileFile.images[i];
        image.present = true;
        image.offsetX = imageAnchors[i].first;
        image.offsetY = imageAnchors[i].second;
        image.width = static_cast<int>(dimensions[i].first);
        image.height = static_cast<int>(dimensions[i].second);
        image.globalWidth = tileFile.blockWidth;
        image.globalHeight = tileFile.blockHeight;
        image.pixels = std::move(pixels);
        image.baseOffsetX = 0;
        image.baseOffsetY = 0;
        image.baseWidth = 0;
        image.baseHeight = 0;
        image.basePixels.clear();
        image.baseZData.clear();
        image.hasBaseZData = false;
        image.hasExtra = false;
        image.extraOffsetX = 0;
        image.extraOffsetY = 0;
        image.extraWidth = 0;
        image.extraHeight = 0;
        image.extraPixels.clear();
        image.extraZData.clear();
        image.hasExtraZData = false;
        ++decodedImages;
    }

    if (decodedImages <= 0) {
        errorMessage = firstImageDecodeWarning.empty()
            ? "TMP raw file did not contain any decodable images: " + path
            : firstImageDecodeWarning;
        return false;
    }
    errorMessage = firstImageDecodeWarning;
    return true;
}

} // namespace

bool loadRuntimeTmpTileBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    RuntimeTmpTileFile& tileFile,
    std::string& errorMessage
) {
    constexpr std::size_t headerBytes = 16;
    constexpr std::size_t offsetBytes = 4;

    const std::string& path = sourceName;

    if (bytes.size() < headerBytes + offsetBytes) {
        errorMessage = "TMP file is too small: " + path;
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
        return loadRawTmpTileFile(bytes, path, tileFile, errorMessage);
    }

    std::size_t slotCount = 0;
    if (!checkedMul(static_cast<std::size_t>(xBlocks), static_cast<std::size_t>(yBlocks), slotCount)) {
        errorMessage = "TMP slot count overflows: " + path;
        return false;
    }

    std::size_t tableBytes = 0;
    if (!checkedMul(slotCount, offsetBytes, tableBytes)) {
        errorMessage = "TMP offset table size overflows: " + path;
        return false;
    }

    std::size_t tableEnd = 0;
    if (!checkedAdd(headerBytes, tableBytes, tableEnd) || tableEnd > bytes.size()) {
        errorMessage = "TMP offset table points beyond file: " + path;
        return false;
    }

    tileFile.blockWidth = static_cast<int>(blockWidth);
    tileFile.blockHeight = static_cast<int>(blockHeight);
    tileFile.gridWidth = static_cast<int>(xBlocks);
    tileFile.gridHeight = static_cast<int>(yBlocks);
    tileFile.images.assign(slotCount, RuntimeTmpSubTileImage{});

    std::vector<std::size_t> imageOffsets(slotCount, 0);
    for (std::size_t i = 0; i < slotCount; ++i) {
        const unsigned int rawOffset = readUInt32(bytes, headerBytes + (i * offsetBytes));
        if (rawOffset == 0) {
            continue;
        }

        imageOffsets[i] = static_cast<std::size_t>(rawOffset);
    }

    if (!validateTmpImageOffsets(imageOffsets, tableEnd, bytes.size(), path, errorMessage)) {
        return false;
    }

    for (std::size_t i = 0; i < slotCount; ++i) {
        const std::size_t imageOffset = imageOffsets[i];
        if (imageOffset == 0) {
            continue;
        }

        const std::size_t imageRecordEnd =
            findNextTmpImageOffset(imageOffsets, imageOffset, bytes.size());
        if (!decodeTmpImagePixels(
                bytes,
                imageOffset,
                imageRecordEnd,
                blockWidth,
                blockHeight,
                tileFile.images[i],
                errorMessage
            )
        ) {
            errorMessage += " path=" + path;
            return false;
        }
    }

    return true;
}

