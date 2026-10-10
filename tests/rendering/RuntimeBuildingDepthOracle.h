#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Test-only raw resource reader. No production loader or depth helpers.
namespace RuntimeBuildingDepthOracle {
using Bytes = std::vector<unsigned char>;

inline std::uint32_t rotate(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

inline std::string sha256(Bytes input) {
    constexpr std::array<std::uint32_t, 64> constants{{
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    }};
    std::array<std::uint32_t, 8> hash{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    const std::uint64_t bitCount = static_cast<std::uint64_t>(input.size()) * 8;
    input.push_back(0x80);
    while (input.size() % 64 != 56) input.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) input.push_back(static_cast<unsigned char>(bitCount >> shift));
    for (std::size_t block = 0; block < input.size(); block += 64) {
        std::array<std::uint32_t, 64> words{};
        for (int i = 0; i < 16; ++i) for (int byte = 0; byte < 4; ++byte)
            words[i] = (words[i] << 8) | input[block + i * 4 + byte];
        for (int i = 16; i < 64; ++i) {
            const auto first = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const auto second = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + first + words[i - 7] + second;
        }
        auto values = hash;
        for (int i = 0; i < 64; ++i) {
            const auto first = values[7] + (rotate(values[4], 6) ^ rotate(values[4], 11) ^ rotate(values[4], 25))
                + ((values[4] & values[5]) ^ (~values[4] & values[6])) + constants[i] + words[i];
            const auto second = (rotate(values[0], 2) ^ rotate(values[0], 13) ^ rotate(values[0], 22))
                + ((values[0] & values[1]) ^ (values[0] & values[2]) ^ (values[1] & values[2]));
            for (int j = 7; j > 0; --j) values[j] = values[j - 1];
            values[4] += first; values[0] = first + second;
        }
        for (int i = 0; i < 8; ++i) hash[i] += values[i];
    }
    std::ostringstream result; result << std::hex << std::setfill('0');
    for (const auto word : hash) result << std::setw(8) << word;
    return result.str();
}

inline Bytes readPinned(const std::filesystem::path& path, const char* hash) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("missing original fixture: " + path.string());
    Bytes bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (sha256(bytes) != hash) throw std::runtime_error("original fixture SHA-256 differs: " + path.string());
    return bytes;
}

inline Bytes retailTemplate(const std::filesystem::path& root) {
    const auto archive = readPinned(root / "local_data/resources/mix_exports/ra2md/conqmd.mix",
        "068dcf91958fd9e582139cbeecfcb138501f821832c83fcef63cb7cfe27ab812");
    // This retail archive has an encrypted index. Use the independently
    // audited entry range only after authenticating the complete archive;
    // do not use the production MIX decoder as the test's source of truth.
    constexpr std::size_t absoluteOffset = 6037676, length = 188928;
    if (archive.size() != 6226784 || absoluteOffset + length > archive.size())
        throw std::runtime_error("retail MIX fixture range differs");
    Bytes result(archive.begin() + absoluteOffset, archive.begin() + absoluteOffset + length);
    if (sha256(result) != "f91653da1f2c4b8e823a7db6ffb5e4c0fc003a9e74eef920adcad2feec739490")
        throw std::runtime_error("independently extracted BUILDNGZ.SHA differs");
    return result;
}

inline std::uint32_t little(const Bytes& bytes, std::size_t offset, int count) {
    if (offset + count > bytes.size()) throw std::runtime_error("truncated independent SHP fixture");
    std::uint32_t value = 0;
    for (int byte = count - 1; byte >= 0; --byte) value = (value << 8) | bytes[offset + byte];
    return value;
}

struct OriginalFrame {
    int canvasWidth = 0, canvasHeight = 0, width = 0, height = 0, x = 0, y = 0;
    Bytes pixels;
};

inline OriginalFrame decodeFrameZero(const Bytes& bytes) {
    OriginalFrame frame;
    if (little(bytes, 6, 2) == 0) throw std::runtime_error("empty independent SHP fixture");
    frame.canvasWidth = little(bytes, 2, 2); frame.canvasHeight = little(bytes, 4, 2);
    frame.x = little(bytes, 8, 2); frame.y = little(bytes, 10, 2);
    frame.width = little(bytes, 12, 2); frame.height = little(bytes, 14, 2);
    const auto format = little(bytes, 16, 4);
    std::size_t cursor = little(bytes, 28, 4);
    frame.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height);
    if (format == 1) {
        if (cursor + frame.pixels.size() > bytes.size()) throw std::runtime_error("truncated raw SHP frame");
        std::copy_n(bytes.begin() + cursor, frame.pixels.size(), frame.pixels.begin());
    } else if (format == 3) {
        for (int y = 0; y < frame.height; ++y) {
            const auto rowLength = little(bytes, cursor, 2);
            if (rowLength < 2 || cursor + rowLength > bytes.size()) throw std::runtime_error("bad SHP row length");
            const auto end = cursor + rowLength; cursor += 2; int x = 0;
            while (cursor < end) {
                const auto color = bytes[cursor++];
                if (color == 0) {
                    if (cursor >= end) throw std::runtime_error("truncated SHP transparent run");
                    x += bytes[cursor++];
                } else {
                    if (x >= frame.width) throw std::runtime_error("opaque SHP row overflow");
                    frame.pixels[static_cast<std::size_t>(y) * frame.width + x++] = color;
                }
            }
            // The retail files occasionally end a row with one extra
            // transparent padding pixel. It must not become a source pixel.
            if (x < frame.width || x > frame.width + 1) throw std::runtime_error("bad SHP decoded row width");
        }
    } else throw std::runtime_error("unsupported independent SHP fixture format");
    return frame;
}

}
