#include "AudioDecoder.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace {

std::uint16_t readUInt16LE(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset])
        | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U)
    );
}

std::uint32_t readUInt32LE(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void appendUInt16LE(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendUInt32LE(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void appendAscii(std::vector<std::uint8_t>& bytes, const char* value) {
    while (*value != '\0') {
        bytes.push_back(static_cast<std::uint8_t>(*value));
        ++value;
    }
}

int clampInt(int value, int minimum, int maximum) {
    return std::max(minimum, std::min(value, maximum));
}

bool checkedMulSize(
    std::size_t left,
    std::size_t right,
    std::size_t& result
) {
    if (left != 0U
        && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

constexpr std::size_t MaxDecodedPcmBytes = 256U * 1024U * 1024U;

// AUD type 1 / Westwood SND1: unsigned 8-bit mono, with a fresh
// predictor for each chunk. The two sizes also distinguish raw chunks.
bool decodeWestwoodSnd1Chunk(
    const std::vector<std::uint8_t>& input,
    std::size_t offset,
    std::size_t inputSize,
    std::size_t outputSize,
    std::vector<std::uint8_t>& output,
    std::string& error
) {
    const std::size_t inputEnd = offset + inputSize;
    const std::size_t outputEnd = output.size() + outputSize;
    if (inputSize == outputSize) {
        output.insert(output.end(), input.begin() + offset, input.begin() + inputEnd);
        return true;
    }
    constexpr int delta4[] = {-9, -8, -6, -5, -4, -3, -2, -1,
                              0,  1,  2,  3,  4,  5,  6,  8};
    int sample = 128;
    while (offset < inputEnd) {
        const unsigned command = input[offset++];
        const unsigned mode = command >> 6U;
        const std::size_t count = (command & 63U) + 1U;
        const bool singleDelta = mode == 2U && (command & 32U) != 0U;
        const std::size_t reads = (mode == 3U || singleDelta) ? 0U : count;
        const std::size_t writes = mode == 0U ? count * 4U
            : mode == 1U ? count * 2U : singleDelta ? 1U : count;
        if (reads > inputEnd - offset || writes > outputEnd - output.size()) {
            error = "AUD SND1 command exceeds its declared input or output chunk.";
            return false;
        }
        if (singleDelta) {
            int delta = static_cast<int>(command & 31U);
            if (delta >= 16) delta -= 32;
            sample = clampInt(sample + delta, 0, 255);
            output.push_back(static_cast<std::uint8_t>(sample));
        } else if (mode == 3U) {
            output.insert(output.end(), count, static_cast<std::uint8_t>(sample));
        } else if (mode == 2U) {
            output.insert(output.end(), input.begin() + offset,
                          input.begin() + offset + count);
            offset += count;
            sample = input[offset - 1U];
        } else {
            const unsigned width = mode == 0U ? 2U : 4U;
            const unsigned mask = (1U << width) - 1U;
            for (std::size_t n = 0; n < count; ++n) {
                const unsigned packed = input[offset++];
                for (unsigned shift = 0; shift < 8U; shift += width) {
                    const unsigned code = (packed >> shift) & mask;
                    const int delta = mode == 0U ? static_cast<int>(code) - 2 : delta4[code];
                    sample = clampInt(sample + delta, 0, 255);
                    output.push_back(static_cast<std::uint8_t>(sample));
                }
            }
        }
    }
    if (output.size() != outputEnd) {
        error = "AUD SND1 chunk produces fewer samples than declared.";
        return false;
    }
    return true;
}

std::int16_t decodeImaNibble(
    std::uint8_t nibble,
    int& index,
    int& currentSample
) {
    static constexpr int IndexTable[16] = {
        -1, -1, -1, -1,
        2, 4, 6, 8,
        -1, -1, -1, -1,
        2, 4, 6, 8
    };

    static constexpr int StepTable[89] = {
        7, 8, 9, 10, 11, 12, 13, 14,
        16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143,
        157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411,
        1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
        3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767
    };

    nibble &= 0x0FU;
    const int step = StepTable[index];
    int difference = step >> 3;
    if ((nibble & 0x01U) != 0U) {
        difference += step >> 2;
    }
    if ((nibble & 0x02U) != 0U) {
        difference += step >> 1;
    }
    if ((nibble & 0x04U) != 0U) {
        difference += step;
    }

    if ((nibble & 0x08U) != 0U) {
        currentSample -= difference;
    } else {
        currentSample += difference;
    }

    currentSample = clampInt(
        currentSample,
        std::numeric_limits<std::int16_t>::min(),
        std::numeric_limits<std::int16_t>::max()
    );
    index = clampInt(index + IndexTable[nibble], 0, 88);
    return static_cast<std::int16_t>(currentSample);
}

void appendPcm16Sample(
    std::vector<std::uint8_t>& pcmBytes,
    std::int16_t sample
) {
    pcmBytes.push_back(static_cast<std::uint8_t>(sample & 0xFF));
    pcmBytes.push_back(static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(sample) >> 8U) & 0xFFU
    ));
}

bool decodeImaAdpcmBlock(
    const std::vector<std::uint8_t>& compressedBytes,
    std::size_t blockOffset,
    std::size_t blockBytes,
    int channels,
    std::vector<std::uint8_t>& pcmBytes,
    std::string& errorMessage
) {
    const std::size_t headerBytes = static_cast<std::size_t>(channels) * 4U;
    if (blockBytes < headerBytes
        || blockOffset > compressedBytes.size()
        || blockBytes > compressedBytes.size() - blockOffset) {
        errorMessage = "BAG IMA ADPCM block is shorter than its channel headers.";
        return false;
    }

    std::vector<std::vector<std::int16_t>> channelSamples(
        static_cast<std::size_t>(channels)
    );
    std::vector<int> indices(static_cast<std::size_t>(channels), 0);
    std::vector<int> predictors(static_cast<std::size_t>(channels), 0);
    for (int channel = 0; channel < channels; ++channel) {
        const std::size_t headerOffset =
            blockOffset + static_cast<std::size_t>(channel) * 4U;
        const std::uint16_t predictorBits = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(compressedBytes[headerOffset])
            | (static_cast<std::uint16_t>(
                    compressedBytes[headerOffset + 1U]
                ) << 8U)
        );
        const int index = compressedBytes[headerOffset + 2U];
        if (index > 88) {
            errorMessage = "BAG IMA ADPCM block has an invalid step index.";
            return false;
        }

        predictors[static_cast<std::size_t>(channel)] =
            static_cast<std::int16_t>(predictorBits);
        indices[static_cast<std::size_t>(channel)] = index;
        channelSamples[static_cast<std::size_t>(channel)].push_back(
            static_cast<std::int16_t>(
                predictors[static_cast<std::size_t>(channel)]
            )
        );
    }

    const std::size_t blockEnd = blockOffset + blockBytes;
    std::size_t cursor = blockOffset + headerBytes;
    if (channels == 1) {
        std::vector<std::int16_t>& samples = channelSamples.front();
        int& index = indices.front();
        int& predictor = predictors.front();
        while (cursor < blockEnd) {
            const std::uint8_t byte = compressedBytes[cursor++];
            samples.push_back(decodeImaNibble(byte, index, predictor));
            samples.push_back(decodeImaNibble(
                static_cast<std::uint8_t>(byte >> 4U),
                index,
                predictor
            ));
        }
    } else {
        // Microsoft IMA ADPCM stores stereo payload in alternating groups of
        // four encoded bytes per channel.  Decode each channel independently,
        // then interleave complete sample frames below.
        while (cursor < blockEnd) {
            for (int channel = 0;
                channel < channels && cursor < blockEnd;
                ++channel) {
                std::vector<std::int16_t>& samples =
                    channelSamples[static_cast<std::size_t>(channel)];
                int& index = indices[static_cast<std::size_t>(channel)];
                int& predictor = predictors[static_cast<std::size_t>(channel)];
                for (int byteIndex = 0;
                    byteIndex < 4 && cursor < blockEnd;
                    ++byteIndex) {
                    const std::uint8_t byte = compressedBytes[cursor++];
                    samples.push_back(
                        decodeImaNibble(byte, index, predictor)
                    );
                    samples.push_back(decodeImaNibble(
                        static_cast<std::uint8_t>(byte >> 4U),
                        index,
                        predictor
                    ));
                }
            }
        }
    }

    std::size_t completeFrames = channelSamples.front().size();
    for (const std::vector<std::int16_t>& samples : channelSamples) {
        completeFrames = std::min(completeFrames, samples.size());
    }
    if (completeFrames == 0U) {
        errorMessage = "BAG IMA ADPCM block produced no complete sample frames.";
        return false;
    }
    if (completeFrames
        > (MaxDecodedPcmBytes - pcmBytes.size())
            / (static_cast<std::size_t>(channels) * sizeof(std::int16_t))) {
        errorMessage = "BAG IMA ADPCM output is too large to decode safely.";
        return false;
    }

    for (std::size_t frame = 0; frame < completeFrames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            appendPcm16Sample(
                pcmBytes,
                channelSamples[static_cast<std::size_t>(channel)][frame]
            );
        }
    }
    return true;
}

} // namespace

bool isWaveFileBytes(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 12U
        || bytes[0] != 'R'
        || bytes[1] != 'I'
        || bytes[2] != 'F'
        || bytes[3] != 'F'
        || bytes[8] != 'W'
        || bytes[9] != 'A'
        || bytes[10] != 'V'
        || bytes[11] != 'E') {
        return false;
    }

    const std::uint32_t riffPayloadSize = readUInt32LE(bytes, 4U);
    const std::uint64_t riffEnd =
        8ULL + static_cast<std::uint64_t>(riffPayloadSize);
    if (riffEnd > static_cast<std::uint64_t>(bytes.size())) {
        return false;
    }

    const std::size_t riffLimit = static_cast<std::size_t>(riffEnd);
    std::size_t offset = 12U;
    bool hasFormatChunk = false;
    bool hasDataChunk = false;
    while (offset + 8U <= riffLimit) {
        const std::uint32_t chunkSize = readUInt32LE(bytes, offset + 4U);
        const std::size_t chunkDataOffset = offset + 8U;
        if (chunkSize > riffLimit - chunkDataOffset) {
            return false;
        }

        const bool isFormatChunk =
            bytes[offset] == 'f'
            && bytes[offset + 1U] == 'm'
            && bytes[offset + 2U] == 't'
            && bytes[offset + 3U] == ' ';
        const bool isDataChunk =
            bytes[offset] == 'd'
            && bytes[offset + 1U] == 'a'
            && bytes[offset + 2U] == 't'
            && bytes[offset + 3U] == 'a';

        hasFormatChunk = hasFormatChunk || (isFormatChunk && chunkSize >= 16U);
        hasDataChunk = hasDataChunk || (isDataChunk && chunkSize > 0U);

        std::size_t nextOffset = chunkDataOffset + chunkSize;
        if ((chunkSize & 1U) != 0U && nextOffset < riffLimit) {
            ++nextOffset;
        }
        if (nextOffset <= offset) {
            return false;
        }
        offset = nextOffset;
    }

    return hasFormatChunk && hasDataChunk;
}

std::vector<std::uint8_t> makePcmWaveFile(
    int sampleRate,
    int bitsPerSample,
    int channels,
    const std::vector<std::uint8_t>& pcmBytes
) {
    std::vector<std::uint8_t> wavBytes;
    if (sampleRate <= 0
        || channels <= 0
        || channels > 8
        || bitsPerSample <= 0
        || (bitsPerSample % 8) != 0) {
        return wavBytes;
    }
    const std::size_t padding = pcmBytes.size() & 1U;
    if (pcmBytes.size()
        > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 36U) - padding) {
        return wavBytes;
    }

    wavBytes.reserve(44U + pcmBytes.size() + padding);

    const int bytesPerSample = bitsPerSample / 8;
    const long long blockAlign64 =
        static_cast<long long>(channels) * bytesPerSample;
    const long long byteRate64 =
        static_cast<long long>(sampleRate) * blockAlign64;
    if (blockAlign64 <= 0
        || blockAlign64 > std::numeric_limits<std::uint16_t>::max()
        || byteRate64 <= 0
        || byteRate64 > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    const int blockAlign = static_cast<int>(blockAlign64);
    const int byteRate = static_cast<int>(byteRate64);
    const std::uint32_t dataSize =
        static_cast<std::uint32_t>(pcmBytes.size());
    const std::uint32_t riffSize = 36U + dataSize + static_cast<std::uint32_t>(padding);

    appendAscii(wavBytes, "RIFF");
    appendUInt32LE(wavBytes, riffSize);
    appendAscii(wavBytes, "WAVE");
    appendAscii(wavBytes, "fmt ");
    appendUInt32LE(wavBytes, 16U);
    appendUInt16LE(wavBytes, 1U);
    appendUInt16LE(wavBytes, static_cast<std::uint16_t>(channels));
    appendUInt32LE(wavBytes, static_cast<std::uint32_t>(sampleRate));
    appendUInt32LE(wavBytes, static_cast<std::uint32_t>(byteRate));
    appendUInt16LE(wavBytes, static_cast<std::uint16_t>(blockAlign));
    appendUInt16LE(wavBytes, static_cast<std::uint16_t>(bitsPerSample));
    appendAscii(wavBytes, "data");
    appendUInt32LE(wavBytes, dataSize);
    wavBytes.insert(wavBytes.end(), pcmBytes.begin(), pcmBytes.end());
    if (padding != 0U) wavBytes.push_back(0U);
    return wavBytes;
}

AudioDecodeResult decodeAudToWave(
    const std::vector<std::uint8_t>& audBytes
) {
    AudioDecodeResult result;
    result.sourceFormat = "aud";

    if (audBytes.size() < 12U) {
        result.errorMessage = "AUD header is too short.";
        return result;
    }

    const int sampleRate = static_cast<int>(readUInt16LE(audBytes, 0));
    const std::uint32_t dataSize = readUInt32LE(audBytes, 2);
    const std::uint32_t outputSize = readUInt32LE(audBytes, 6);
    const std::uint8_t flags = audBytes[10];
    const std::uint8_t format = audBytes[11];
    const int channels = (flags & 0x01U) != 0U ? 2 : 1;
    const int bitsPerSample = (flags & 0x02U) != 0U ? 16 : 8;

    if (sampleRate <= 0 || outputSize == 0U) {
        result.errorMessage = "AUD header has an invalid sample rate or output size.";
        return result;
    }
    if (outputSize > MaxDecodedPcmBytes) {
        result.errorMessage = "AUD output size is too large to decode safely.";
        return result;
    }
    if (dataSize > audBytes.size() - 12U) {
        result.errorMessage = "AUD data size points beyond the end of the file.";
        return result;
    }

    if (format != 1U && format != 99U) {
        result.errorMessage =
            "AUD format " + std::to_string(static_cast<int>(format))
            + " is unsupported; expected Westwood SND1 (1) or IMA ADPCM (99).";
        return result;
    }
    if ((flags & 0xFCU) != 0U
        || (format == 1U && (channels != 1 || bitsPerSample != 8))
        || (format == 99U && bitsPerSample != 16)) {
        result.errorMessage = "AUD codec and channel/sample-width flags are incompatible.";
        return result;
    }
    const std::size_t frameBytes = static_cast<std::size_t>(channels * bitsPerSample / 8);
    if (outputSize % frameBytes != 0U) {
        result.errorMessage = "AUD output size is not a whole PCM frame.";
        return result;
    }

    std::size_t offset = 12U;
    const std::size_t dataEnd = offset + dataSize;
    std::vector<std::uint8_t> pcmBytes;
    pcmBytes.reserve(std::min<std::size_t>(outputSize, 1024U * 1024U));
    int indices[2] = {0, 0};
    int predictors[2] = {0, 0};

    while (offset < dataEnd) {
        if (dataEnd - offset < 8U) {
            result.errorMessage = "AUD chunk header is truncated within the declared data size.";
            return result;
        }
        const std::uint16_t compressedSize = readUInt16LE(audBytes, offset);
        const std::uint16_t chunkOutputSize = readUInt16LE(audBytes, offset + 2U);
        const std::uint32_t magic = readUInt32LE(audBytes, offset + 4U);
        offset += 8U;

        if (magic != 0x0000DEAFU) {
            result.errorMessage = "AUD chunk header magic is invalid.";
            return result;
        }
        if (compressedSize > dataEnd - offset) {
            result.errorMessage = "AUD chunk data exceeds the declared data size.";
            return result;
        }
        if (compressedSize == 0U || chunkOutputSize == 0U
            || chunkOutputSize > outputSize - pcmBytes.size()
            || chunkOutputSize % frameBytes != 0U) {
            result.errorMessage = "AUD chunk has an invalid PCM output size.";
            return result;
        }
        if (format == 1U) {
            if (!decodeWestwoodSnd1Chunk(audBytes, offset, compressedSize,
                                        chunkOutputSize, pcmBytes, result.errorMessage)) {
                return result;
            }
        } else {
            const std::size_t frames = chunkOutputSize / frameBytes;
            if (compressedSize != ((frames + 1U) / 2U) * channels) {
                result.errorMessage = "AUD IMA chunk input and PCM output lengths disagree.";
                return result;
            }
            // AUD stereo packs one byte per channel for each pair of frames.
            // Each channel retains its own predictor across chunk boundaries.
            for (std::size_t frame = 0; frame < frames; frame += 2U) {
                const std::size_t pairOffset = offset + (frame / 2U) * channels;
                for (unsigned half = 0; half < 2U && frame + half < frames; ++half) {
                    for (int channel = 0; channel < channels; ++channel) {
                        const auto nibble = static_cast<std::uint8_t>(
                            audBytes[pairOffset + channel] >> (half * 4U));
                        appendPcm16Sample(pcmBytes,
                            decodeImaNibble(nibble, indices[channel], predictors[channel]));
                    }
                }
            }
        }

        offset += compressedSize;
    }

    if (pcmBytes.size() != outputSize) {
        result.errorMessage = "AUD decoded PCM length differs from the declared output size.";
        return result;
    }

    result.success = true;
    result.sourceFormat = format == 1U ? "aud westwood-snd1" : "aud ima-adpcm";
    result.sampleRate = sampleRate;
    result.bitsPerSample = bitsPerSample;
    result.channels = channels;
    result.pcmBytes = std::move(pcmBytes);
    result.wavBytes =
        makePcmWaveFile(result.sampleRate, result.bitsPerSample, result.channels, result.pcmBytes);
    if (result.wavBytes.empty()) {
        result.success = false;
        result.errorMessage = "AUD decoder could not wrap PCM data as WAV.";
        return result;
    }
    return result;
}

AudioDecodeResult decodeImaAdpcmBagSampleToWave(
    const std::vector<std::uint8_t>& compressedBytes,
    int sampleRate,
    int channels,
    std::uint32_t blockAlign
) {
    AudioDecodeResult result;
    result.sourceFormat = "idx/bag ima-adpcm";

    if (compressedBytes.empty()) {
        result.errorMessage = "BAG sample is empty.";
        return result;
    }
    if (sampleRate <= 0) {
        result.errorMessage = "BAG sample has an invalid sample rate.";
        return result;
    }
    if (channels <= 0) {
        result.errorMessage = "BAG sample has an invalid channel count.";
        return result;
    }
    if (channels > 2) {
        result.errorMessage = "BAG sample has an unsupported channel count.";
        return result;
    }
    const std::size_t channelHeaderBytes =
        static_cast<std::size_t>(channels) * 4U;
    if (blockAlign < channelHeaderBytes) {
        result.errorMessage = "BAG sample has an invalid IMA ADPCM block alignment.";
        return result;
    }
    std::size_t reserveBytes = 0;
    if (!checkedMulSize(compressedBytes.size(), 4U, reserveBytes)
        || reserveBytes > MaxDecodedPcmBytes) {
        result.errorMessage = "BAG sample output size is too large to decode safely.";
        return result;
    }
    result.pcmBytes.reserve(reserveBytes);

    std::size_t blockOffset = 0;
    while (blockOffset < compressedBytes.size()) {
        const std::size_t blockBytes = std::min<std::size_t>(
            static_cast<std::size_t>(blockAlign),
            compressedBytes.size() - blockOffset
        );
        if (!decodeImaAdpcmBlock(
                compressedBytes,
                blockOffset,
                blockBytes,
                channels,
                result.pcmBytes,
                result.errorMessage
            )) {
            result.pcmBytes.clear();
            return result;
        }
        blockOffset += blockBytes;
    }
    if (result.pcmBytes.empty()) {
        result.errorMessage = "BAG IMA ADPCM decoder produced no PCM data.";
        return result;
    }

    result.success = true;
    result.sourceFormat = "idx/bag ms-ima-adpcm blocks";
    result.sampleRate = sampleRate;
    result.bitsPerSample = 16;
    result.channels = channels;
    result.wavBytes =
        makePcmWaveFile(result.sampleRate, result.bitsPerSample, result.channels, result.pcmBytes);
    if (result.wavBytes.empty()) {
        result.success = false;
        result.errorMessage = "BAG IMA ADPCM decoder could not wrap PCM data as WAV.";
        return result;
    }
    return result;
}
