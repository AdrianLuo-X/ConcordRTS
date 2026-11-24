#include "assets/codecs/RuntimeLcwCodec.h"
#include "assets/codecs/RuntimePackedByteOrder.h"
namespace concord::assets {
namespace {
bool appendLcwByte(
    std::vector<unsigned char>& output,
    unsigned char value,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (output.size() >= expectedBytes) {
        errorMessage = "LCW output exceeded expected byte count";
        return false;
    }

    output.push_back(value);
    return true;
}

bool copyLcwOutputRange(
    std::vector<unsigned char>& output,
    std::size_t sourceOffset,
    std::size_t count,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (sourceOffset >= output.size()) {
        errorMessage = "LCW copy source is outside decoded output";
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t readOffset = sourceOffset + i;
        if (readOffset >= output.size()) {
            errorMessage = "LCW copy source overran decoded output";
            return false;
        }

        if (!appendLcwByte(output, output[readOffset], expectedBytes, errorMessage)) {
            return false;
        }
    }

    return true;
}
}
bool decodeLcwBlock(
    const std::vector<unsigned char>& data,
    std::size_t compressedOffset,
    std::size_t compressedBytes,
    std::size_t expectedBytes,
    std::size_t& decodedBytes,
    std::vector<unsigned char>* decodedOutput,
    std::string& errorMessage
) {
    decodedBytes = 0;
    errorMessage.clear();

    std::vector<unsigned char> output;
    output.reserve(expectedBytes);

    std::size_t offset = compressedOffset;
    const std::size_t endOffset = compressedOffset + compressedBytes;
    bool sawEndCommand = false;

    while (offset < endOffset) {
        const unsigned int command = data[offset];
        ++offset;

        if ((command & 0x80u) == 0) {
            if (offset >= endOffset) {
                errorMessage = "LCW short copy is missing its offset byte";
                return false;
            }

            const std::size_t count = (command >> 4) + 3;
            const std::size_t displacement =
                ((command & 0x0Fu) << 8) | data[offset];
            ++offset;

            if (displacement > output.size()) {
                errorMessage = "LCW short copy points before decoded output";
                return false;
            }

            if (!copyLcwOutputRange(
                    output,
                    output.size() - displacement,
                    count,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }
        } else if ((command & 0x40u) == 0) {
            const std::size_t count = command & 0x3Fu;
            if (count == 0) {
                sawEndCommand = true;
                break;
            }

            if (offset + count > endOffset) {
                errorMessage = "LCW literal copy exceeds compressed chunk";
                return false;
            }

            for (std::size_t i = 0; i < count; ++i) {
                if (!appendLcwByte(output, data[offset + i], expectedBytes, errorMessage)) {
                    return false;
                }
            }
            offset += count;
        } else if (command < 0xFEu) {
            if (offset + 2 > endOffset) {
                errorMessage = "LCW absolute copy is missing its source offset";
                return false;
            }

            const std::size_t count = (command & 0x3Fu) + 3;
            const std::size_t sourceOffset = concord::asset_bytes::readUInt16LE(data, offset);
            offset += 2;

            if (!copyLcwOutputRange(
                    output,
                    sourceOffset,
                    count,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }
        } else if (command == 0xFEu) {
            if (offset + 3 > endOffset) {
                errorMessage = "LCW fill command is truncated";
                return false;
            }

            const std::size_t count = concord::asset_bytes::readUInt16LE(data, offset);
            offset += 2;
            const unsigned char value = data[offset];
            ++offset;

            for (std::size_t i = 0; i < count; ++i) {
                if (!appendLcwByte(output, value, expectedBytes, errorMessage)) {
                    return false;
                }
            }
        } else {
            if (offset + 4 > endOffset) {
                errorMessage = "LCW long copy command is truncated";
                return false;
            }

            const std::size_t count = concord::asset_bytes::readUInt16LE(data, offset);
            offset += 2;
            const std::size_t sourceOffset = concord::asset_bytes::readUInt16LE(data, offset);
            offset += 2;

            if (!copyLcwOutputRange(
                    output,
                    sourceOffset,
                    count,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }
        }
    }

    decodedBytes = output.size();

    if (!sawEndCommand) {
        errorMessage = "LCW stream ended without an end command";
        return false;
    }

    if (decodedBytes != expectedBytes) {
        errorMessage = "LCW decoded byte count does not match chunk header";
        return false;
    }

    if (decodedOutput != nullptr) {
        decodedOutput->insert(decodedOutput->end(), output.begin(), output.end());
    }

    return true;
}
}
