#include "AudioDecoder.h"
#include "AudioSampleResolver.h"
#include "MixArchive.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void expect(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
void number(Bytes& bytes, std::uint32_t value, unsigned count) {
    while (count-- != 0U) { bytes.push_back(static_cast<std::uint8_t>(value)); value >>= 8U; }
}
void setNumber(Bytes& bytes, std::size_t at, std::uint32_t value, unsigned count) {
    while (count-- != 0U) { bytes.at(at++) = static_cast<std::uint8_t>(value); value >>= 8U; }
}
std::uint32_t getNumber(const Bytes& bytes, std::size_t at, unsigned count) {
    std::uint32_t value = 0;
    for (unsigned n = 0; n < count; ++n) value |= static_cast<std::uint32_t>(bytes.at(at + n)) << (8U * n);
    return value;
}
struct Chunk { Bytes bytes; std::uint16_t output; };
Bytes aud(const std::vector<Chunk>& chunks, unsigned format = 1, unsigned flags = 0, unsigned rate = 22050) {
    Bytes payload;
    std::uint32_t size = 0;
    for (const auto& chunk : chunks) {
        number(payload, static_cast<std::uint32_t>(chunk.bytes.size()), 2);
        number(payload, chunk.output, 2);
        number(payload, 0xDEAF, 4);
        payload.insert(payload.end(), chunk.bytes.begin(), chunk.bytes.end());
        size += chunk.output;
    }
    Bytes bytes;
    number(bytes, rate, 2); number(bytes, static_cast<std::uint32_t>(payload.size()), 4);
    number(bytes, size, 4); number(bytes, flags, 1); number(bytes, format, 1);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}
Bytes pcm16(const std::vector<int>& values) {
    Bytes result;
    for (int value : values) number(result, static_cast<std::uint16_t>(value), 2);
    return result;
}
void check(const Bytes& file, const Bytes& expected, int rate, int bits, int channels) {
    const auto decoded = decodeAudToWave(file);
    expect(decoded.success, decoded.errorMessage);
    expect(decoded.pcmBytes == expected, "PCM differs from hand-calculated samples");
    expect(decoded.sampleRate == rate && decoded.bitsPerSample == bits && decoded.channels == channels,
           "decoded audio metadata differs");
    const auto& wave = decoded.wavBytes;
    expect(isWaveFileBytes(wave), "generated RIFF/WAVE is invalid");
    expect(wave.size() == 44U + expected.size() + (expected.size() & 1U), "RIFF padding length");
    expect(getNumber(wave, 4, 4) + 8U == wave.size(), "RIFF size");
    expect(getNumber(wave, 40, 4) == expected.size(), "data size must exclude RIFF padding");
    expect(getNumber(wave, 22, 2) == channels && getNumber(wave, 24, 4) == rate
           && getNumber(wave, 34, 2) == bits, "WAV format metadata");
    expect(getNumber(wave, 32, 2) == channels * bits / 8
           && getNumber(wave, 28, 4) == rate * channels * bits / 8, "playback byte rate and frame size");
    expect(std::equal(expected.begin(), expected.end(), wave.begin() + 44), "WAV samples differ");
}
void reject(const Bytes& file, const char* message) {
    const auto result = decodeAudToWave(file);
    expect(!result.success && !result.errorMessage.empty() && result.pcmBytes.empty()
           && result.wavBytes.empty(), message);
}
void write(const std::filesystem::path& path, const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    expect(static_cast<bool>(output), "fixture write failed");
}
Bytes read(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    expect(static_cast<bool>(input), "cached WAV is missing");
    return Bytes(std::istreambuf_iterator<char>(input), {});
}
void mix(const std::filesystem::path& path, const std::string& name, const Bytes& payload) {
    Bytes bytes;
    number(bytes, 1, 2); number(bytes, static_cast<std::uint32_t>(payload.size()), 4);
    number(bytes, mixFileIdFromName(name), 4); number(bytes, 0, 4);
    number(bytes, static_cast<std::uint32_t>(payload.size()), 4);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    write(path, bytes);
}
void resolver(const std::filesystem::path& base, const Bytes& file, const Bytes& expected) {
    const auto root = base / ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto cache = root / "cache";
    write(root / "audio" / "loose.aud", file);
    mix(root / "audio.mix", "packed.aud", file);
    write(root / "theme" / "Power.aud", file);
    mix(root / "theme.mix", "Grinder.aud", file);
    const auto expectedWave = makePcmWaveFile(22050, 8, 1, expected);
    for (const auto* name : {"loose", "packed"}) {
        const auto result = resolveAudioSampleToWave(root.string(), cache.string(), name);
        expect(result.success, "sound resolver: " + result.errorMessage);
        expect(result.decoded && !result.fromCache && result.sampleRate == 22050
               && result.channels == 1 && result.bitsPerSample == 8
               && result.pcmBytes == expected.size(), "sound resolver metadata");
        expect(read(result.wavPath) == expectedWave, "sound resolver PCM differs");
        const auto repeat = resolveAudioSampleToWave(root.string(), cache.string(), name);
        expect(repeat.success && repeat.fromCache && repeat.wavPath == result.wavPath, "sound cache reuse");
    }
    for (const auto* name : {"Power", "Grinder"}) {
        const auto result = resolveBackgroundMusicThemeToWave(root.string(), cache.string(), name);
        expect(result.success && result.decoded && !result.fromCache, "theme resolver: " + result.errorMessage);
        expect(read(result.wavPath) == expectedWave, "theme resolver PCM differs");
        const auto repeat = resolveBackgroundMusicThemeToWave(root.string(), cache.string(), name);
        expect(repeat.success && repeat.fromCache && repeat.wavPath == result.wavPath, "theme cache reuse");
    }
    write(root / "audio" / "broken.aud", aud({{{0x00}, 4}}));
    expect(!resolveAudioSampleToWave(root.string(), cache.string(), "broken").success,
           "invalid AUD reached playable resolver result");
    Bytes silence;
    for (unsigned remaining = 22050; remaining != 0;) {
        const unsigned count = std::min(remaining, 64U);
        silence.push_back(static_cast<std::uint8_t>(0xC0U | (count - 1U)));
        remaining -= count;
    }
    const auto oneSecond = aud({{silence,22050}});
    check(oneSecond, Bytes(22050,128), 22050,8,1);
    write(root / "audio" / "playback.aud",oneSecond);
    const auto playback = resolveAudioSampleToWave(root.string(),cache.string(),"playback");
    expect(playback.success && read(playback.wavPath) == makePcmWaveFile(22050,8,1,Bytes(22050,128)),
           "one-second playback fixture failed");
    std::ofstream manifest(base / "playback-wave.txt");
    manifest << playback.wavPath;
    expect(static_cast<bool>(manifest), "playback fixture manifest write failed");
    std::cout << "Loose/MIX sound and music resolution, WAV cache reuse, invalid audio rejection passed\n";
}
}

int main(int argc, char** argv) {
    try {
        const Bytes coded = {0x00,0xE4,0x40,0xF0,0xAF,0xB0,0x82,0,255,40,0xC2};
        const Bytes expected = {126,125,125,126,117,125,140,124,0,255,40,40,40,40};
        const auto file = aud({{coded,14}});
        check(file, expected, 22050, 8, 1);
        Bytes combined = expected;
        combined.insert(combined.end(), {0,128,255,128,128,128,128});
        check(aud({{coded,14},{{0,128,255},3},{{0xC3},4}},1,0,11025), combined,11025,8,1);
        check(aud({{{0xC2},3}},1,0,8000), {128,128,128},8000,8,1);
        check(aud({{{0x81,0,255,0x00,0xFF,0x40,0,0x80,0,0xB0,0x40,0,0xAF,0xC3},17}}),
              {0,255,255,255,255,255,246,237,0,0,0,0,15,15,15,15,15},22050,8,1);
        check(aud({{{0xFF},64}}),Bytes(64,128),22050,8,1);
        Bytes longCommand(65,0xAA); longCommand[0] = 0x3F;
        check(aud({{longCommand,256}}),Bytes(256,128),22050,8,1);
        check(aud({{{0x77},4},{{0},4}},99,2),pcm16({11,41,45,48}),22050,16,1);
        check(aud({{{0xF1},2},{{0x11},4}},99,2),pcm16({1,2,3}),22050,16,1);
        check(aud({{{0x11,0x99,0x11,0x99},16},{{0x11,0x99},8}},99,3,44100),
              pcm16({1,-1,2,-2,3,-3,4,-4,5,-5,6,-6}),44100,16,2);
        check(aud({{{0xF1,0xF9},4}},99,3),pcm16({1,-1}),22050,16,2);
        for (std::size_t n = 0; n < file.size(); ++n)
            reject(Bytes(file.begin(), file.begin() + n), "truncated AUD accepted");
        for (const auto& bad : std::vector<Bytes>{
                aud({{{0x00},4}}), aud({{{0x40},2}}), aud({{{0x82,1},3}}),
                aud({{{0xC3},3}}), aud({{{0xC1},4}}), aud({{{0xC3,0xC3},4}}),
                aud({{{},4}}), aud({{{0xC3},0}}), aud({}),
                aud({{coded,14}},0), aud({{coded,14}},1,1), aud({{coded,14}},1,2),
                aud({{coded,14}},1,128), aud({{coded,14}},1,0,0),
                aud({{{0x11},4}},99,0), aud({{{0x11},3}},99,2),
                aud({{{0x11},8}},99,2), aud({{{0x11,0x11},4}},99,2),
                aud({{{0x11,0x99},6}},99,3)}) reject(bad,"malformed chunk or flags accepted");
        for (const auto& mutation : std::vector<std::pair<std::size_t,std::uint32_t>>{
                {2,1},{2,7},{2,18},{6,13},{6,15},{6,0xFFFFFFFF},{16,0}}) {
            auto bad = file; setNumber(bad,mutation.first,mutation.second,4);
            reject(bad,"invalid declared boundary accepted");
        }
        // Suffix bytes cannot satisfy a payload that extends beyond dataSize.
        auto suffix = file; suffix.insert(suffix.end(),20,0xC3); setNumber(suffix,2,8,4);
        reject(suffix,"payload crossed declared AUD boundary into suffix");
        // A valid declared stream may have an unrelated container suffix.
        suffix = file; suffix.insert(suffix.end(),4,0xFF); check(suffix,expected,22050,8,1);
        resolver(argc > 1 ? argv[1] : "audio_decoder_smoke", file, expected);
        std::cout << "AUD codec smoke passed: SND1 commands/raw/reset/clipping, IMA mono/stereo, RIFF length, strict bounds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AUD codec smoke failed: " << error.what() << '\n';
        return 1;
    }
}
