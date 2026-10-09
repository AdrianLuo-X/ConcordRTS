#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AudioDecodeResult {
    bool success = false;
    std::string errorMessage;
    std::string sourceFormat;
    int sampleRate = 0;
    int bitsPerSample = 0;
    int channels = 0;
    std::vector<std::uint8_t> pcmBytes;
    std::vector<std::uint8_t> wavBytes;
};

bool isWaveFileBytes(const std::vector<std::uint8_t>& bytes);

std::vector<std::uint8_t> makePcmWaveFile(
    int sampleRate,
    int bitsPerSample,
    int channels,
    const std::vector<std::uint8_t>& pcmBytes
);

AudioDecodeResult decodeAudToWave(
    const std::vector<std::uint8_t>& audBytes
);

AudioDecodeResult decodeImaAdpcmBagSampleToWave(
    const std::vector<std::uint8_t>& compressedBytes,
    int sampleRate,
    int channels,
    std::uint32_t blockAlign
);
