#include "assets/audio/AudioSampleResolver.h"
#include "assets/audio/AudioDecoder.h"
#include "assets/archive/MixArchive.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
int checks = 0;
void expect(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
std::string label(const fs::path& path) {
    const auto text = fs::weakly_canonical(path).generic_u8string();
    return std::string(text.begin(), text.end());
}
void write(const fs::path& path, const Bytes& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    expect(static_cast<bool>(stream), "fixture write");
}
void text(const fs::path& path, const std::string& value) {
    write(path, Bytes(value.begin(), value.end()));
}
Bytes read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    expect(static_cast<bool>(stream), "owned output missing");
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}
void number(Bytes& bytes, std::uint32_t value, unsigned count = 4) {
    for (unsigned n = 0; n < count; ++n) {
        bytes.push_back(static_cast<std::uint8_t>(value));
        value >>= 8;
    }
}
Bytes wave(unsigned marker) {
    return makePcmWaveFile(22050, 8, 1, Bytes(8, static_cast<std::uint8_t>(marker)));
}
Bytes aud(unsigned marker) {
    // Westwood uncompressed AUD, one raw chunk with independently declared size.
    Bytes bytes;
    number(bytes, 22050, 2); number(bytes, 16); number(bytes, 8);
    number(bytes, 0, 1); number(bytes, 1, 1);
    number(bytes, 8, 2); number(bytes, 8, 2); number(bytes, 0xDEAF);
    bytes.insert(bytes.end(), 8, static_cast<std::uint8_t>(marker));
    return bytes;
}
void mix(const fs::path& path, const std::vector<std::pair<std::string, Bytes>>& entries) {
    Bytes bytes, payload;
    number(bytes, static_cast<std::uint32_t>(entries.size()), 2);
    std::uint32_t length = 0;
    for (const auto& entry : entries) length += static_cast<std::uint32_t>(entry.second.size());
    number(bytes, length);
    for (const auto& [name, data] : entries) {
        number(bytes, mixFileIdFromName(name));
        number(bytes, static_cast<std::uint32_t>(payload.size()));
        number(bytes, static_cast<std::uint32_t>(data.size()));
        payload.insert(payload.end(), data.begin(), data.end());
    }
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    write(path, bytes);
}
Bytes idx(std::uint32_t size = 8, std::uint32_t offset = 0) {
    Bytes bytes{'G','A','B','A'};
    number(bytes, 2); number(bytes, 1);
    const std::string name = "probe";
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.resize(28, 0);
    number(bytes, offset); number(bytes, size); number(bytes, 22050);
    number(bytes, 0); number(bytes, 0);
    return bytes;
}
AudioSampleResolveResult sound(const fs::path& root, const fs::path& cache,
                               const std::string& sample = "probe", const std::string& source = {}) {
    return resolveAudioSampleToWaveFromPaths({root, cache}, sample, source);
}
void payload(const AudioSampleResolveResult& result, unsigned marker) {
    expect(result.success, "sound resolve: " + result.errorMessage);
    expect(!result.wavFilePath.empty(), "missing owned wave path");
    expect(result.wavPath == label(result.wavFilePath), "native WAV label is not UTF-8");
    expect(read(result.wavFilePath) == wave(marker), "sound PCM/order differs");
}
void payload(const AudioBackgroundMusicResolveResult& result, unsigned marker) {
    expect(result.success, "music resolve: " + result.errorMessage);
    expect(result.wavPath == label(result.wavFilePath), "music native WAV label");
    expect(read(result.wavFilePath) == wave(marker), "music PCM/order differs");
}
const std::vector<fs::path> audioMixes = {
    "langmd/audiomd.mix", "langmd/audio.mix", "ra2md/localmd/audiomd.mix", "ra2md/localmd/audio.mix",
    "expandmd01/audiomd.mix", "expandmd01/audio.mix", "language/audio.mix", "language/audiomd.mix",
    "audiomd.mix", "audio.mix", "ra2md/audiomd.mix", "ra2/audio.mix"
};
void mixOrder(const fs::path& base) {
    for (std::size_t i = 0; i < audioMixes.size(); ++i) {
        const auto root = base / ("mix-" + std::to_string(i));
        mix(root / audioMixes[i], {{"probe.wav", wave(10)}});
        if (i + 1 < audioMixes.size()) mix(root / audioMixes[i + 1], {{"probe.wav", wave(20)}});
        const auto result = sound(root, root / "cache");
        payload(result, 10);
        expect(result.sourcePath == label(root / audioMixes[i]), "MIX candidate order");
        const auto repeat = sound(root, root / "cache");
        expect(repeat.success && repeat.fromCache && repeat.wavFilePath == result.wavFilePath, "MIX persistent reuse");
        const std::string edition = audioMixes[i].filename() == "audio.mix" ? " RA2 " : "YR";
        payload(sound(root, root / "edition-cache", "probe", edition), 10);
        expect(!sound(root, root / "other-cache", "probe", "unknown").success, "unknown edition fallback");
    }
    const auto root = base / "edition-separated";
    mix(root / "langmd/audiomd.mix", {{"audio.idx", idx()}, {"audio.bag", Bytes(8, 31)}});
    mix(root / "langmd/audio.mix", {{"audio.idx", idx()}, {"audio.bag", Bytes(8, 32)}});
    payload(sound(root, root / "cache", "probe", "YR"), 31);
    payload(sound(root, root / "cache", "probe", "ra2"), 32);
    const auto samples = collectIndexedAudioSamplesFromPaths({root, {}}, "yr");
    expect(samples.size() == 1 && samples[0].name == "probe" && samples[0].size == 8
           && samples[0].flags == 0 && samples[0].sourceMixPath == label(root / "langmd/audiomd.mix"), "indexed library metadata");
}
void looseOrder(const fs::path& base) {
    const std::vector<std::pair<std::string, std::vector<fs::path>>> sets = {
        {"ra2", {"audio", "language", "ra2/local", "ra2"}},
        {"yr", {"audiomd", "langmd", "ra2md/localmd", "expandmd01"}},
        {"", {"audio", "audiomd", "language", "langmd", "ra2md/localmd", "expandmd01", ""}}
    };
    int n = 0;
    for (const auto& [edition, directories] : sets) {
        for (std::size_t i = 0; i < directories.size(); ++i) {
            const auto root = base / ("loose-" + std::to_string(n++));
            write(root / directories[i] / "probe.wav", wave(41));
            if (i + 1 < directories.size()) write(root / directories[i + 1] / "probe.wav", wave(42));
            mix(root / (edition == "yr" ? "audiomd.mix" : "audio.mix"), {{"probe.wav", wave(43)}});
            auto result = sound(root, root / "cache", "probe", edition);
            payload(result, 41);
            expect(!result.fromCache && !result.decoded, "loose WAV state");
            expect(result.sourcePath == label(root / directories[i] / "probe.wav"), "loose root order");
        }
    }
    auto root = base / "invalid-loose";
    text(root / "audio/probe.wav", "invalid WAV");
    write(root / "audiomd/probe.wav", wave(51));
    payload(sound(root, root / "cache"), 51);
    root = base / "bad-aud-aborts-loose";
    text(root / "audio/probe.aud", "bad aud");
    write(root / "audiomd/probe.wav", wave(52));
    mix(root / "audio.mix", {{"probe.wav", wave(53)}});
    payload(sound(root, root / "cache"), 53);
    root = base / "aud";
    write(root / "audio/probe.aud", aud(54));
    auto result = sound(root, root / "cache");
    payload(result, 54);
    expect(result.decoded && !result.fromCache, "AUD decoder state");
    expect(sound(root, root / "cache").fromCache, "AUD cache reuse");
    expect(normalizeAudioSampleName(" '$@Probe.WAV' ; x") == "probe", "logical decorators");
    expect(normalizeAudioSampleName("<none>").empty(), "null logical name");
    expect(sound(root, root / "cache", "none").errorMessage == "Audio sample name is empty.", "missing/null failure");
}
void cachedFailures(const fs::path& base) {
    auto root = base / "missing-snapshot";
    expect(collectIndexedAudioSamplesFromPaths({root, {}}).empty(), "missing library");
    mix(root / "audio.mix", {{"audio.idx", idx()}, {"audio.bag", Bytes(8, 61)}});
    expect(collectIndexedAudioSamplesFromPaths({root, {}}).empty(), "candidate miss must remain cached");
    root = base / "failed-archive";
    text(root / "audio.mix", "bad mix");
    expect(!sound(root, root / "cache").success, "invalid archive");
    mix(root / "audio.mix", {{"probe.wav", wave(62)}});
    expect(!sound(root, root / "cache").success, "archive failure cache invalidated");
    root = base / "bag-bounds";
    mix(root / "audio.mix", {{"audio.idx", idx(9)}, {"audio.bag", Bytes(8, 63)}});
    expect(sound(root, root / "cache").errorMessage == "BAG sample points beyond the end of audio.bag.", "BAG bounds failure");
    root = base / "bag-reopen";
    mix(root / "audio.mix", {{"audio.idx", idx()}, {"audio.bag", Bytes(8, 64)}});
    auto result = sound(root, root / "cache");
    payload(result, 64);
    mix(root / "audio.mix", {{"audio.idx", idx()}, {"audio.bag", Bytes(8, 65)}});
    payload(sound(root, root / "cache"), 64);
    payload(sound(root, root / "another-cache"), 65);
    // Existing policy accepts any >44-byte cache, without rereading source.
    write(result.wavFilePath, Bytes(45, 0));
    expect(sound(root, root / "cache").fromCache, "nonempty cache acceptance changed");
    text(root / "blocked-cache", "file blocks directory");
    expect(!sound(root, root / "blocked-cache").success, "cache writer failure");
    root = base / "vox";
    text(root / "langmd/ra2md.csf", "VOX:voice_a\n");
    text(root / "langmd/langmd.mix", "VOX:voice_b\n");
    auto voice = sound(root, root / "cache", "voice_a", "yr");
    expect(!voice.success && voice.sourceFormat == "csf vox label"
           && voice.sourcePath == label(root / "langmd/ra2md.csf") + "|VOX:voice_a", "VOX CSF path");
    expect(sound(root, root / "cache", "voice_b", "yr").sourceFormat.empty(), "nonempty CSF suppresses MIX scan");
    root = base / "vox-mix";
    text(root / "langmd/langmd.mix", "VOX:voice_b\n");
    expect(sound(root, root / "cache", "voice_b", "yr").sourceFormat == "csf vox label", "VOX MIX fallback");
}
std::string theme(const std::string& soundName = "Marker", const std::string& normal = "yes") {
    return "[Themes]\n1=Custom\n[Custom]\nSound=" + soundName + "\nNormal=" + normal + "\nSide=Allied\n; original bytes \xD6\xD0\n";
}
void music(const fs::path& base) {
    const std::vector<fs::path> inis = {"ra2md/localmd/thememd.ini", "localmd/thememd.ini", "thememd.ini", "ra2/local/theme.ini", "local/theme.ini", "theme.ini"};
    for (std::size_t i = 0; i < inis.size(); ++i) {
        const auto root = base / ("ini-" + std::to_string(i));
        text(root / inis[i], theme());
        if (i + 1 < inis.size()) text(root / inis[i + 1], theme());
        write(root / "thememd/Marker.wav", wave(71));
        auto result = resolveBackgroundMusicThemeToWaveFromPaths({root, root / "cache"}, " CUSTOM ");
        payload(result, 71);
        expect(result.themeIniPath == label(root / inis[i]) && result.normalTrack, "theme INI order/bytes");
        const auto ids = collectBackgroundMusicThemeIdsFromPaths({root, {}}, AudioBackgroundMusicFaction::Allied);
        expect(!ids.empty() && ids.front() == "Custom", "authored theme before retail fallback");
    }
    const std::vector<fs::path> loose = {"thememd/Marker.wav", "theme/Marker.wav", "thememd/Marker.aud", "theme/Marker.aud"};
    for (std::size_t i = 0; i < loose.size(); ++i) {
        const auto root = base / ("theme-loose-" + std::to_string(i));
        text(root / "thememd.ini", theme());
        write(root / loose[i], loose[i].extension() == ".wav" ? wave(72) : aud(72));
        if (i + 1 < loose.size()) write(root / loose[i + 1], loose[i + 1].extension() == ".wav" ? wave(73) : aud(73));
        payload(resolveBackgroundMusicThemeToWaveFromPaths({root, root / "cache"}, "Marker"), 72);
    }
    const std::vector<fs::path> mixes = {"thememd.mix", "theme.mix", "ra2md/thememd.mix", "ra2/theme.mix", "GameMD/thememd.mix", "Game/theme.mix"};
    for (std::size_t i = 0; i < mixes.size(); ++i) {
        const auto root = base / ("theme-mix-" + std::to_string(i));
        text(root / "thememd.ini", theme());
        mix(root / mixes[i], {{"Marker.wav", wave(74)}});
        if (i + 1 < mixes.size()) mix(root / mixes[i + 1], {{"Marker.wav", wave(75)}});
        auto result = resolveBackgroundMusicThemeToWaveFromPaths({root, root / "cache"}, "Custom");
        payload(result, 74);
        expect(result.sourcePath == label(root / mixes[i]) + "|Marker.wav", "theme MIX order");
        expect(resolveBackgroundMusicThemeToWaveFromPaths({root, root / "cache"}, "Custom").fromCache, "theme MIX cache");
    }
    auto root = base / "nested-ini";
    auto value = theme("Packed", "no");
    mix(root / "ra2/local.mix", {{"theme.ini", Bytes(value.begin(), value.end())}});
    write(root / "theme/Packed.wav", wave(76));
    auto result = resolveBackgroundMusicThemeToWaveFromPaths({root, {}}, "Custom");
    payload(result, 76);
    expect(!result.normalTrack && result.themeIniPath == label(root / "ra2/local.mix") + "|theme.ini", "nested INI source");
    const auto ids = collectBackgroundMusicThemeIdsFromPaths({root, {}}, AudioBackgroundMusicFaction::Any);
    expect(ids.size() == 20 && std::find(ids.begin(), ids.end(), "Custom") == ids.end(), "normal filtering/retail fallback");
    expect(resolveBackgroundMusicThemeToWaveFromPaths({root, {}}, "absent").errorMessage == "Requested background music theme is not declared.", "theme missing diagnostic");
    root = base / "preferred";
    write(root / "theme/Power.wav", wave(77));
    write(root / "theme/Grinder.wav", wave(78));
    payload(resolveBackgroundMusicToWaveFromPaths({root, {}}, {"Grinder"}), 78);
    for (int depth = 0; depth < 6; ++depth) {
        root = base / ("theme-ancestor-" + std::to_string(depth)) / "a/b/c/d/e/f";
        text(root / "thememd.ini", theme());
        auto ancestor = root;
        for (int n = 0; n < depth; ++n) ancestor = ancestor.parent_path();
        mix(ancestor / "RedAlert2/GameMD/thememd.mix", {{"Marker.aud", aud(79)}});
        mix(ancestor / "RedAlert2/Game/theme.mix", {{"Marker.wav", wave(80)}});
        result = resolveBackgroundMusicThemeToWaveFromPaths({root, root / "cache"}, "Custom");
        payload(result, 79);
        expect(result.sourcePath == label(ancestor / "RedAlert2/GameMD/thememd.mix") + "|Marker.aud", "six-level theme ancestry/edition order");
    }
    root = base / "explicit-missing-does-not-fall-through";
    text(root / "ra2md/localmd/thememd.ini", theme("Missing"));
    text(root / "theme.ini", theme("Available"));
    write(root / "theme/Available.wav", wave(83));
    expect(!resolveBackgroundMusicThemeToWaveFromPaths({root, {}}, "Custom").success,
           "explicit theme must return first declared match failure");
    payload(resolveBackgroundMusicToWaveFromPaths({root, {}}, {"Custom"}), 83);
    // INI lookup is intentionally fresh, unlike cached MIX candidate lists.
    text(root / "ra2md/localmd/thememd.ini", theme("Available"));
    payload(resolveBackgroundMusicThemeToWaveFromPaths({root, {}}, "Custom"), 83);
}
void compatibility(const fs::path& root) {
    mix(root / "audio.mix", {{"audio.idx", idx()}, {"audio.bag", Bytes(8, 81)}});
    const auto old = resolveAudioSampleToWave(root.string(), (root / "cache").string(), "probe");
    expect(old.success && !old.fromCache && read(old.wavFilePath) == wave(81), "legacy resolver");
    const auto native = sound(root, root / "cache");
    expect(native.success && native.fromCache && native.wavFilePath == old.wavFilePath, "legacy/native persistent identity");
    const auto legacySamples = collectIndexedAudioSamples(root.string());
    const auto nativeSamples = collectIndexedAudioSamplesFromPaths({root, {}});
    expect(legacySamples.size() == 1 && nativeSamples.size() == 1 && legacySamples[0].name == nativeSamples[0].name, "legacy/native indexed samples");
    text(root / "thememd.ini", theme());
    write(root / "theme/Marker.wav", wave(82));
    const auto a = collectBackgroundMusicThemeIds(root.string(), AudioBackgroundMusicFaction::Any);
    const auto b = collectBackgroundMusicThemeIdsFromPaths({root, {}}, AudioBackgroundMusicFaction::Any);
    expect(a == b && a.size() == 21, "legacy/native playlist identity");
    payload(resolveBackgroundMusicThemeToWaveFromPaths({root, {}}, "Custom"), 82);
    const auto oldMusic = resolveBackgroundMusicThemeToWave(root.string(), "", "Custom");
    expect(oldMusic.success && oldMusic.wavFilePath == root / "theme/Marker.wav", "legacy music native result");
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc >= 2, "explicit isolated output directory required");
        const fs::path base(argv[1]);
        fs::create_directories(base);
        fs::current_path(base); // Isolate the resolver's existing cwd VOX fallback.
        if (argc == 3) {
            const std::string mode(argv[2]);
            const auto root = base / fs::path(u8"文件路径-🌲") / "locks";
            if (mode == "--prepare-locks") {
                mix(root / "audio.mix", {{"probe.wav", wave(91)}, {"packed.wav", wave(93)}});
                write(root / "audio/probe.wav", wave(92));
            } else if (mode == "--locked-mix") {
                expect(!sound(root, root / "cache", "packed", "ra2").success, "locked MIX unexpectedly readable");
            } else if (mode == "--locked-loose") {
                payload(sound(root, root / "cache"), 91);
            } else if (mode == "--readonly") {
                payload(sound(root, root / "cache"), 92);
                payload(sound(root, root / "cache", "packed"), 93);
            } else throw std::runtime_error("unknown fixture mode");
        } else {
            const auto root = base / fs::path(u8"文件路径-🌲");
            expect(!fs::exists(root), "retain previous fixture; use a new output directory");
            mixOrder(root); looseOrder(root); cachedFailures(root); music(root);
            compatibility(base / fs::path(u8"兼容代码页"));
        }
        std::cout << "Audio path contract passed: checks=" << checks << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio path contract failed after " << checks << " checks: " << error.what() << "\n";
        return 1;
    }
}
