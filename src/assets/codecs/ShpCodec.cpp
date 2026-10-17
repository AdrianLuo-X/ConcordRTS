#include "assets/codecs/ShpCodec.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace {

unsigned int read16(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
}

std::uint32_t read32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

int dimension(unsigned int value, ShpDecodePolicy policy) {
    if (policy == ShpDecodePolicy::Runtime && (value & 0x8000U) != 0) {
        return static_cast<int>(value) - 0x10000;
    }
    return static_cast<int>(value);
}

bool decodeFormat3(
    const std::vector<unsigned char>& bytes, std::size_t dataOffset,
    int width, int height, std::vector<unsigned char>& pixels, std::string& error
) {
    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    std::size_t cursor = dataOffset;
    for (int y = 0; y < height; ++y) {
        if (cursor + 2 > bytes.size()) {
            error = "SHP format 3 row table ended early.";
            return false;
        }
        const std::size_t rowStart = cursor;
        const std::size_t rowLength = read16(bytes, cursor);
        if (rowLength < 2) {
            error = "SHP format 3 row has an invalid length.";
            return false;
        }
        const std::size_t rowEnd = rowStart + rowLength;
        if (rowEnd > bytes.size()) {
            error = "SHP format 3 row extends past the end of the file.";
            return false;
        }
        cursor += 2;
        int x = 0;
        while (cursor < rowEnd) {
            const unsigned char value = bytes[cursor++];
            if (value == 0) {
                if (cursor >= rowEnd) {
                    error = "SHP format 3 transparent run is truncated.";
                    return false;
                }
                x += bytes[cursor++];
                continue;
            }
            if (x >= 0 && x < width) {
                pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                    + static_cast<std::size_t>(x)] = value;
            }
            ++x;
        }
        cursor = rowEnd;
    }
    return true;
}

bool decodeFormat2(
    const std::vector<unsigned char>& bytes, std::size_t dataOffset,
    int width, int height, std::vector<unsigned char>& pixels, std::string& error
) {
    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    std::size_t cursor = dataOffset;
    for (int y = 0; y < height; ++y) {
        if (cursor + 2 > bytes.size()) {
            error = "SHP format 2 row header is truncated";
            return false;
        }
        const int rowLength = static_cast<int>(read16(bytes, cursor));
        cursor += 2;
        if (rowLength < 2) {
            error = "SHP format 2 row length is invalid";
            return false;
        }
        const int rowDataLength = rowLength - 2;
        if (cursor + static_cast<std::size_t>(rowDataLength) > bytes.size()) {
            error = "SHP format 2 row data is truncated";
            return false;
        }
        const int copyLength = std::min(width, rowDataLength);
        if (copyLength > 0) {
            const std::size_t rowStart = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), copyLength,
                pixels.begin() + static_cast<std::ptrdiff_t>(rowStart));
        }
        cursor += static_cast<std::size_t>(rowDataLength);
    }
    return true;
}

} // namespace

bool decodeShpIndexedFrame(
    const std::vector<unsigned char>& bytes,
    const std::string& source,
    int frameIndex,
    ShpDecodePolicy policy,
    ShpIndexedFrame& frame,
    std::string& error
) {
    constexpr std::size_t fileHeaderBytes = 8;
    constexpr std::size_t frameHeaderBytes = 24;
    if (bytes.size() < fileHeaderBytes + frameHeaderBytes) {
        error = "SHP file is too small for a frame table: " + source;
        return false;
    }
    const unsigned int frameCount = read16(bytes, 6);
    if (frameCount == 0) {
        error = "SHP file has no frames: " + source;
        return false;
    }
    if (frameIndex < 0 || static_cast<unsigned int>(frameIndex) >= frameCount) {
        error = "SHP frame " + std::to_string(frameIndex) + " is outside the frame table ("
            + std::to_string(frameCount) + " frames): " + source;
        return false;
    }
    const std::size_t frameOffset = fileHeaderBytes
        + static_cast<std::size_t>(frameIndex) * frameHeaderBytes;
    if (frameOffset + frameHeaderBytes > bytes.size()) {
        error = "SHP frame table is truncated: " + source;
        return false;
    }
    ShpIndexedFrame decoded;
    decoded.globalWidth = dimension(read16(bytes, 2), policy);
    decoded.globalHeight = dimension(read16(bytes, 4), policy);
    decoded.offsetX = dimension(read16(bytes, frameOffset), policy);
    decoded.offsetY = dimension(read16(bytes, frameOffset + 2), policy);
    decoded.width = dimension(read16(bytes, frameOffset + 4), policy);
    decoded.height = dimension(read16(bytes, frameOffset + 6), policy);
    decoded.frameIndex = frameIndex;
    decoded.format = static_cast<int>(policy == ShpDecodePolicy::Runtime
        ? bytes[frameOffset + 8] : read16(bytes, frameOffset + 8));
    decoded.dataOffset = read32(bytes, frameOffset + 20);
    decoded.sourcePath = source;
    if (policy == ShpDecodePolicy::Runtime
        && decoded.globalWidth > 0 && decoded.globalHeight > 0
        && decoded.width == 0 && decoded.height == 0
        && (decoded.format <= 1 || decoded.format == 3 || decoded.format == 4)
        && decoded.dataOffset == 0) {
        decoded.offsetX = 0;
        decoded.offsetY = 0;
        decoded.authoredPause = true;
        frame = std::move(decoded);
        return true;
    }
    const std::string frameDescription = policy == ShpDecodePolicy::Preview ? "first frame" : "frame";
    if ((policy == ShpDecodePolicy::Runtime && (decoded.globalWidth <= 0 || decoded.globalHeight <= 0))
        || decoded.width <= 0 || decoded.height <= 0 || decoded.width > 4096 || decoded.height > 4096) {
        error = "SHP " + frameDescription + " dimensions are unsupported: " + source;
        return false;
    }
    const std::size_t dataOffset = decoded.dataOffset;
    if (dataOffset >= bytes.size()) {
        error = "SHP " + frameDescription + " data offset is outside the file: " + source;
        return false;
    }
    if (decoded.format <= 1) {
        const std::size_t pixelCount = static_cast<std::size_t>(decoded.width)
            * static_cast<std::size_t>(decoded.height);
        if (dataOffset + pixelCount > bytes.size()) {
            error = "SHP raw " + frameDescription + " is shorter than expected: " + source;
            return false;
        }
        decoded.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + pixelCount));
    } else if (policy == ShpDecodePolicy::Runtime && decoded.format == 2) {
        if (!decodeFormat2(bytes, dataOffset, decoded.width, decoded.height, decoded.pixels, error)) {
            error += " Path: " + source;
            return false;
        }
    } else if (decoded.format == 3) {
        if (!decodeFormat3(bytes, dataOffset, decoded.width, decoded.height, decoded.pixels, error)) {
            error += " Path: " + source;
            return false;
        }
    } else {
        error = "Unsupported SHP " + frameDescription + " format "
            + std::to_string(decoded.format) + ": " + source;
        return false;
    }
    frame = std::move(decoded);
    return true;
}