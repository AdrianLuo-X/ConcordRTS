#include "assets/codecs/RuntimeLzo1xCodec.h"
#include "assets/codecs/RuntimePackedByteOrder.h"
#include <cstdint>
namespace concord::assets {
namespace {
using concord::asset_bytes::readUInt16LE;
bool appendLzoByte(
    std::vector<unsigned char>& output,
    unsigned char value,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (output.size() >= expectedBytes) {
        errorMessage = "LZO output exceeded expected byte count";
        return false;
    }

    output.push_back(value);
    return true;
}

bool copyLzoLiterals(
    const std::vector<unsigned char>& data,
    std::size_t& offset,
    std::size_t endOffset,
    std::size_t count,
    std::vector<unsigned char>& output,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (offset + count > endOffset) {
        errorMessage = "LZO literal run exceeds compressed chunk";
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (!appendLzoByte(output, data[offset + i], expectedBytes, errorMessage)) {
            return false;
        }
    }

    offset += count;
    return true;
}

bool copyLzoOutputRange(
    std::vector<unsigned char>& output,
    std::size_t sourceOffset,
    std::size_t count,
    std::size_t expectedBytes,
    std::string& errorMessage
) {
    if (sourceOffset >= output.size()) {
        errorMessage = "LZO match source is outside decoded output";
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t readOffset = sourceOffset + i;
        if (readOffset >= output.size()) {
            errorMessage = "LZO match source overran decoded output";
            return false;
        }

        if (!appendLzoByte(output, output[readOffset], expectedBytes, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool subtractLzoBackReference(
    std::size_t outputSize,
    std::size_t distance,
    std::size_t& sourceOffset,
    std::string& errorMessage
) {
    if (distance > outputSize) {
        errorMessage = "LZO match points before decoded output";
        return false;
    }

    sourceOffset = outputSize - distance;
    return true;
}

bool readLzoLengthExtension(
    const std::vector<unsigned char>& data,
    std::size_t& offset,
    std::size_t endOffset,
    std::size_t baseLength,
    unsigned int& length,
    std::string& errorMessage
) {
    length = 0;

    while (offset < endOffset && data[offset] == 0) {
        length += 255;
        ++offset;
    }

    if (offset >= endOffset) {
        errorMessage = "LZO extended length is truncated";
        return false;
    }

    length += static_cast<unsigned int>(baseLength + data[offset]);
    ++offset;
    return true;
}
} // namespace
bool decodeLzo1xBlock(
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
    unsigned int token = 0;
    bool matchDone = false;

    if (offset >= endOffset) {
        errorMessage = "LZO chunk is empty";
        return false;
    }

    if (data[offset] > 17) {
        token = static_cast<unsigned int>(data[offset] - 17);
        ++offset;

        if (token < 4) {
            if (!copyLzoLiterals(
                    data,
                    offset,
                    endOffset,
                    token,
                    output,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }

            if (offset >= endOffset) {
                errorMessage = "LZO stream ended after initial literal run";
                return false;
            }

            token = data[offset];
            ++offset;
            goto lzo_match;
        }

        if (!copyLzoLiterals(
                data,
                offset,
                endOffset,
                token,
                output,
                expectedBytes,
                errorMessage
            )) {
            return false;
        }

        goto lzo_first_literal_run;
    }

    while (true) {
        if (offset >= endOffset) {
            errorMessage = "LZO stream ended before command";
            return false;
        }

        token = data[offset];
        ++offset;

        if (token >= 16) {
            goto lzo_match;
        }

        if (token == 0) {
            if (!readLzoLengthExtension(
                    data,
                    offset,
                    endOffset,
                    15,
                    token,
                    errorMessage
                )) {
                return false;
            }
        }

        if (!copyLzoLiterals(
                data,
                offset,
                endOffset,
                static_cast<std::size_t>(token) + 3,
                output,
                expectedBytes,
                errorMessage
            )) {
            return false;
        }

lzo_first_literal_run:
        if (offset >= endOffset) {
            errorMessage = "LZO stream ended before match command";
            return false;
        }

        token = data[offset];
        ++offset;

        if (token >= 16) {
            goto lzo_match;
        }

        if (offset >= endOffset) {
            errorMessage = "LZO short match is missing its offset byte";
            return false;
        }

        {
            const std::size_t distance =
                1 + 0x0800u + (token >> 2)
                + (static_cast<std::size_t>(data[offset]) << 2);
            ++offset;

            std::size_t sourceOffset = 0;
            if (!subtractLzoBackReference(
                    output.size(),
                    distance,
                    sourceOffset,
                    errorMessage
                )) {
                return false;
            }

            if (!copyLzoOutputRange(
                    output,
                    sourceOffset,
                    3,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }
        }

        matchDone = true;

lzo_match:
        do {
            if (matchDone) {
                matchDone = false;
                goto lzo_match_done;
            }

            if (token >= 64) {
                if (offset >= endOffset) {
                    errorMessage = "LZO M2 match is missing its offset byte";
                    return false;
                }

                const std::size_t distance =
                    1 + ((token >> 2) & 7)
                    + (static_cast<std::size_t>(data[offset]) << 3);
                ++offset;

                std::size_t sourceOffset = 0;
                if (!subtractLzoBackReference(
                        output.size(),
                        distance,
                        sourceOffset,
                        errorMessage
                    )) {
                    return false;
                }

                const std::size_t count = (token >> 5) + 1;
                if (!copyLzoOutputRange(
                        output,
                        sourceOffset,
                        count,
                        expectedBytes,
                        errorMessage
                    )) {
                    return false;
                }

                goto lzo_match_done;
            }

            if (token >= 32) {
                unsigned int countToken = token & 31u;
                if (countToken == 0) {
                    if (!readLzoLengthExtension(
                            data,
                            offset,
                            endOffset,
                            31,
                            countToken,
                            errorMessage
                        )) {
                        return false;
                    }
                }

                if (offset + 2 > endOffset) {
                    errorMessage = "LZO M3 match is missing its offset";
                    return false;
                }

                const std::size_t distance =
                    1 + (readUInt16LE(data, offset) >> 2);
                offset += 2;

                std::size_t sourceOffset = 0;
                if (!subtractLzoBackReference(
                        output.size(),
                        distance,
                        sourceOffset,
                        errorMessage
                    )) {
                    return false;
                }

                if (!copyLzoOutputRange(
                        output,
                        sourceOffset,
                        static_cast<std::size_t>(countToken) + 2,
                        expectedBytes,
                        errorMessage
                    )) {
                    return false;
                }
            } else if (token >= 16) {
                const std::size_t highDistance = (token & 8u) << 11;
                unsigned int countToken = token & 7u;

                if (countToken == 0) {
                    if (!readLzoLengthExtension(
                            data,
                            offset,
                            endOffset,
                            7,
                            countToken,
                            errorMessage
                        )) {
                        return false;
                    }
                }

                if (offset + 2 > endOffset) {
                    errorMessage = "LZO M4 match is missing its offset";
                    return false;
                }

                const std::size_t lowDistance = readUInt16LE(data, offset) >> 2;
                offset += 2;

                if (highDistance == 0 && lowDistance == 0) {
                    decodedBytes = output.size();

                    if (decodedBytes != expectedBytes) {
                        errorMessage = "LZO decoded byte count does not match chunk header";
                        return false;
                    }

                    if (offset != endOffset) {
                        errorMessage = "LZO stream has trailing bytes after end marker";
                        return false;
                    }

                    if (decodedOutput != nullptr) {
                        decodedOutput->insert(
                            decodedOutput->end(),
                            output.begin(),
                            output.end()
                        );
                    }

                    return true;
                }

                const std::size_t distance = highDistance + lowDistance + 0x4000u;
                std::size_t sourceOffset = 0;
                if (!subtractLzoBackReference(
                        output.size(),
                        distance,
                        sourceOffset,
                        errorMessage
                    )) {
                    return false;
                }

                if (!copyLzoOutputRange(
                        output,
                        sourceOffset,
                        static_cast<std::size_t>(countToken) + 2,
                        expectedBytes,
                        errorMessage
                    )) {
                    return false;
                }
            } else {
                if (offset >= endOffset) {
                    errorMessage = "LZO M1 match is missing its offset byte";
                    return false;
                }

                const std::size_t distance =
                    1 + (token >> 2)
                    + (static_cast<std::size_t>(data[offset]) << 2);
                ++offset;

                std::size_t sourceOffset = 0;
                if (!subtractLzoBackReference(
                        output.size(),
                        distance,
                        sourceOffset,
                        errorMessage
                    )) {
                    return false;
                }

                if (!copyLzoOutputRange(
                        output,
                        sourceOffset,
                        2,
                        expectedBytes,
                        errorMessage
                    )) {
                    return false;
                }
            }

lzo_match_done:
            if (offset < 2) {
                errorMessage = "LZO match state is invalid";
                return false;
            }

            token = data[offset - 2] & 3u;
            if (token == 0) {
                break;
            }

            if (!copyLzoLiterals(
                    data,
                    offset,
                    endOffset,
                    token,
                    output,
                    expectedBytes,
                    errorMessage
                )) {
                return false;
            }

            if (offset >= endOffset) {
                errorMessage = "LZO stream ended after match literals";
                return false;
            }

            token = data[offset];
            ++offset;
        } while (true);
    }
}
} // namespace concord::assets
