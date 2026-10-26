#include "assets/codecs/ShpCodec.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

namespace {

using Bytes = std::span<const unsigned char>;
constexpr std::size_t fileHeaderSize = 8;
constexpr std::size_t frameRecordSize = 24;

// All callers supply a bounded header or row before reading its little-endian fields.
unsigned int wordAt(Bytes bytes, std::size_t offset) {
    return static_cast<unsigned int>(bytes[offset])
        | (static_cast<unsigned int>(bytes[offset + 1]) << 8);
}

int signedWordAt(Bytes bytes, std::size_t offset) {
    const unsigned int bits = wordAt(bytes, offset);
    return bits < 0x8000U ? static_cast<int>(bits) : static_cast<int>(bits) - 0x10000;
}

std::uint32_t dwordAt(Bytes bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(wordAt(bytes, offset))
        | (static_cast<std::uint32_t>(wordAt(bytes, offset + 2)) << 16);
}

struct FrameRecord {
    int x;
    int y;
    int width;
    int height;
    unsigned char compression;
    unsigned char previewFormatHighByte;
    std::uint32_t payload;
};

FrameRecord readFrameRecord(Bytes record) {
    // x/y/w/h are signed 16-bit fields; the compression tag occupies one byte.
    // Bytes 9..19 are not pixel data. Preview retains its old interpretation
    // of byte 9 only in the compatibility projection below.
    return {signedWordAt(record, 0), signedWordAt(record, 2),
        signedWordAt(record, 4), signedWordAt(record, 6), record[8], record[9],
        dwordAt(record, 20)};
}

int policyDimension(int signedValue, ShpDecodePolicy policy) {
    return policy == ShpDecodePolicy::Preview && signedValue < 0
        ? signedValue + 0x10000 : signedValue;
}

ShpIndexedFrame frameMetadata(Bytes file, Bytes record, ShpDecodePolicy policy) {
    const auto header = readFrameRecord(record);
    ShpIndexedFrame result;
    result.globalWidth = policyDimension(signedWordAt(file, 2), policy);
    result.globalHeight = policyDimension(signedWordAt(file, 4), policy);
    result.offsetX = policyDimension(header.x, policy);
    result.offsetY = policyDimension(header.y, policy);
    result.width = policyDimension(header.width, policy);
    result.height = policyDimension(header.height, policy);
    result.format = static_cast<int>(header.compression);
    if (policy == ShpDecodePolicy::Preview) {
        result.format += static_cast<int>(header.previewFormatHighByte) * 256;
    }
    result.dataOffset = header.payload;
    return result;
}

bool isRuntimePause(const ShpIndexedFrame& frame, ShpDecodePolicy policy) {
    if (policy != ShpDecodePolicy::Runtime || frame.globalWidth <= 0 || frame.globalHeight <= 0
        || frame.width != 0 || frame.height != 0 || frame.dataOffset != 0) {
        return false;
    }
    switch (frame.format) {
    case 0: case 1: case 3: case 4: return true;
    default: return false;
    }
}

struct RowDiagnostics {
    const char* missingLength;
    const char* invalidLength;
    const char* missingPayload;
};

constexpr RowDiagnostics literalRowErrors{
    "SHP format 2 row header is truncated",
    "SHP format 2 row length is invalid",
    "SHP format 2 row data is truncated"
};
constexpr RowDiagnostics transparentRowErrors{
    "SHP format 3 row table ended early.",
    "SHP format 3 row has an invalid length.",
    "SHP format 3 row extends past the end of the file."
};

bool takeRow(Bytes& remaining, Bytes& row, const RowDiagnostics& diagnostics, std::string& error) {
    if (remaining.size() < 2) {
        error = diagnostics.missingLength;
        return false;
    }
    // The stored row size includes this length word, including an empty row (2).
    const std::size_t storedSize = wordAt(remaining, 0);
    if (storedSize < 2) {
        error = diagnostics.invalidLength;
        return false;
    }
    if (storedSize > remaining.size()) {
        error = diagnostics.missingPayload;
        return false;
    }
    row = remaining.subspan(2, storedSize - 2);
    remaining = remaining.subspan(storedSize);
    return true;
}

bool expandTransparentRow(Bytes row, std::span<unsigned char> destination, std::string& error) {
    std::size_t column = 0;
    while (!row.empty()) {
        const unsigned char token = row.front();
        row = row.subspan(1);
        if (token != 0) {
            if (column < destination.size()) destination[column] = token;
            ++column;
        } else {
            if (row.empty()) {
                error = "SHP format 3 transparent run is truncated.";
                return false;
            }
            column += row.front();
            row = row.subspan(1);
        }
    }
    return true;
}

bool decodeRows(Bytes payload, ShpIndexedFrame& frame, std::string& error) {
    const std::size_t width = static_cast<std::size_t>(frame.width);
    frame.pixels.assign(width * static_cast<std::size_t>(frame.height), 0);
    const bool literals = frame.format == 2;
    const auto& diagnostics = literals ? literalRowErrors : transparentRowErrors;
    auto output = std::span<unsigned char>(frame.pixels);
    for (int rowIndex = 0; rowIndex < frame.height; ++rowIndex) {
        Bytes encoded;
        if (!takeRow(payload, encoded, diagnostics, error)) return false;
        const auto row = output.first(width);
        output = output.subspan(width);
        if (literals) {
            // Short rows remain transparent; long rows are consumed in full and clipped.
            const auto visible = encoded.first(std::min(encoded.size(), row.size()));
            std::copy(visible.begin(), visible.end(), row.begin());
        } else if (!expandTransparentRow(encoded, row, error)) {
            return false;
        }
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
    const Bytes file(bytes);
    if (file.size() < fileHeaderSize + frameRecordSize) {
        error = "SHP file is too small for a frame table: " + source;
        return false;
    }
    const unsigned int frameCount = wordAt(file, 6);
    if (frameCount == 0) {
        error = "SHP file has no frames: " + source;
        return false;
    }
    if (frameIndex < 0 || static_cast<unsigned int>(frameIndex) >= frameCount) {
        error = "SHP frame " + std::to_string(frameIndex) + " is outside the frame table ("
            + std::to_string(frameCount) + " frames): " + source;
        return false;
    }
    // Validate only the selected record: callers historically tolerate missing
    // unselected records and data offsets that point before the frame table.
    const auto records = file.subspan(fileHeaderSize);
    const std::size_t selected = static_cast<std::size_t>(frameIndex);
    if (selected >= records.size() / frameRecordSize) {
        error = "SHP frame table is truncated: " + source;
        return false;
    }
    auto decoded = frameMetadata(file, records.subspan(selected * frameRecordSize, frameRecordSize), policy);
    decoded.frameIndex = frameIndex;
    decoded.sourcePath = source;
    if (isRuntimePause(decoded, policy)) {
        decoded.offsetX = 0;
        decoded.offsetY = 0;
        decoded.authoredPause = true;
        frame = std::move(decoded);
        return true;
    }

    const std::string description = policy == ShpDecodePolicy::Preview ? "first frame" : "frame";
    const bool invalidCanvas = policy == ShpDecodePolicy::Runtime
        && (decoded.globalWidth <= 0 || decoded.globalHeight <= 0);
    if (invalidCanvas || decoded.width <= 0 || decoded.height <= 0
        || decoded.width > 4096 || decoded.height > 4096) {
        error = "SHP " + description + " dimensions are unsupported: " + source;
        return false;
    }
    if (decoded.dataOffset >= file.size()) {
        error = "SHP " + description + " data offset is outside the file: " + source;
        return false;
    }
    const auto payload = file.subspan(decoded.dataOffset);
    switch (decoded.format) {
    case 0:
    case 1: {
        const std::size_t count = static_cast<std::size_t>(decoded.width)
            * static_cast<std::size_t>(decoded.height);
        if (count > payload.size()) {
            error = "SHP raw " + description + " is shorter than expected: " + source;
            return false;
        }
        const auto image = payload.first(count);
        decoded.pixels.assign(image.begin(), image.end());
        break;
    }
    case 2:
    case 3:
        if (decoded.format == 2 && policy == ShpDecodePolicy::Preview) {
            error = "Unsupported SHP " + description + " format 2: " + source;
            return false;
        }
        if (!decodeRows(payload, decoded, error)) {
            error += " Path: " + source;
            return false;
        }
        break;
    default:
        error = "Unsupported SHP " + description + " format "
            + std::to_string(decoded.format) + ": " + source;
        return false;
    }
    frame = std::move(decoded);
    return true;
}
