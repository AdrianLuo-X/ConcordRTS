#include "audio/backend/RuntimeVoiceAudio.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

int main(int argc, char** argv) {
    bool ok = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {std::cerr << "FAIL " << message << '\n'; ok = false;}
    };
    RuntimeSpatialAudioRules screen;
    const auto at = [&](const auto& spec, double x, double y, bool mapped = true) {
        return runtimeSpatialAudioAtProjection(spec, x, y, 640, 400, mapped);
    };
    check(at(screen, 320, 200).gain16384 == 16384, "center");
    check(at(screen, 0, 200).gain16384 == 16384 && at(screen, 0, 200).pan16384 == 4096, "left screen edge");
    check(at(screen, 640, 200).gain16384 == 16384 && at(screen, 640, 200).pan16384 == 12288, "right screen edge");
    check(at(screen, 940, 200).gain16384 == 8192, "outside horizontal half range");
    check(at(screen, 320, 550).gain16384 == 8192, "outside vertical half range");
    check(at(screen, 940, 550).gain16384 == 8192, "rectangle corner uses max");
    check(at(screen, 1210, 200).gain16384 == 819, "exact 5 percent retained");
    check(at(screen, 1211, 200).gain16384 == 0, "below 5 percent discarded");
    check(at(screen, 1240, 200).gain16384 == 0 && at(screen, 1240, 200).pan16384 == 16384, "range and right clamp");
    check(at(screen, -600, 200).gain16384 == 0 && at(screen, -600, 200).pan16384 == 0, "left clamp");
    auto local = screen; local.local = true;
    check(at(local, 620, 200).gain16384 == 8192 && at(local, 320, 350).gain16384 == 8192, "LOCAL center geometry");
    auto global = screen; global.global = true;
    check(at(global, 3000, 200).gain16384 == 8192 && at(global, 3000, 200).pan16384 == 16384, "GLOBAL floor preserves pan");
    global.minimumVolumePercent = 4;
    check(at(global, 3000, 200).gain16384 == 0, "GLOBAL below threshold");
    auto hidden = screen; hidden.shroud = true;
    check(at(hidden, 320, 200, false).gain16384 == 0 && at(screen, 320, 200, false).gain16384 == 16384, "SHROUD and UNSHROUD");
    screen.rangeCells = 0;
    check(at(screen, 320, 200).gain16384 == 0, "zero range");
    check(at(screen, std::numeric_limits<double>::infinity(), 200).gain16384 == 0, "invalid coordinate");
    const auto center = runtimeRetailAudioMix(16384, 8192);
    const auto half = runtimeRetailAudioMix(8192, 8192);
    const auto left = runtimeRetailAudioMix(16384, 4096);
    check(center.volume == 1 && center.left == 1 && center.right == 1, "center mixing");
    check(std::abs(half.volume - std::sqrt(0.1f)) < 0.000001f, "retail half volume is minus 10 dB");
    check(left.left == 1 && std::abs(left.right - half.volume) < 0.000001f, "retail pan table");
    check(runtimeRetailAudioMix(0, 8192).volume == 0 && runtimeRetailAudioMix(16384, 0).right == 0, "silent endpoints");
    if (argc != 3) { std::cerr << "Expected mono and stereo PCM paths\n"; return 2; }
    RuntimeVoiceAudio audio(true);
    check(audio.ready(), "XAudio2 muted graph creation");
    if (!audio.ready()) { std::cerr << "HRESULT " << audio.lastError() << '\n'; return 1; }
    check(audio.open(1, argv[1]) && audio.open(2, argv[2]), "real PCM source voices");
    check(audio.setMix(2, 16384, 8192), "independent center voice");
    const auto independent = audio.probe(2);
    for (int gain : {0, 4096, 8192, 16384}) for (int pan : {0, 4096, 8192, 12288, 16384}) {
        check(audio.setMix(1, gain, pan), "native volume and matrix calls");
        const auto probe = audio.probe(1); const auto expected = runtimeRetailAudioMix(gain, pan);
        check(probe.available && probe.channels == 1 && probe.masterVolume == 0 && probe.matrix.size() == 2,
            "mono graph remains muted");
        if (probe.matrix.size() == 2) check(std::abs(probe.volume - expected.volume) < 0.000001
            && std::abs(probe.matrix[0] - expected.left) < 0.000001
            && std::abs(probe.matrix[1] - expected.right) < 0.000001, "native mixer readback");
        std::cout << "mix," << gain << ',' << pan << ',' << probe.volume << ','
            << (probe.matrix.empty() ? -1 : probe.matrix[0]) << ',' << (probe.matrix.size() < 2 ? -1 : probe.matrix[1]) << '\n';
    }
    auto other = audio.probe(2);
    check(other.volume == independent.volume && other.matrix == independent.matrix
        && other.channels == 2 && other.matrix == std::vector<float>({1,0,0,1}), "voice isolation and stereo routing");
    check(audio.setMix(1, 8192, 4096) && audio.playFromStart(1, true) && audio.playFromStart(2, true), "muted source submission and start");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto running = audio.probe(1);
    check(running.masterVolume == 0 && running.samplesPlayed > 0 && running.buffersQueued == 1
        && audio.isPlaying(1).value_or(false), "actual audio engine advances muted buffers");
    std::cout << "nativeSamplesPlayed=" << running.samplesPlayed << " masterVolume=" << running.masterVolume << '\n';
    check(audio.stop(1) && !audio.isPlaying(1).value_or(true)
        && audio.isPlaying(2).value_or(false), "stop and flush isolation");
    check(audio.playFromStart(1), "retained buffer replay");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    check(audio.probe(1).samplesPlayed > 0, "replayed voice advances");
    audio.close(1); audio.close(2);
    check(audio.voiceCount() == 0 && !audio.probe(1).available, "all voices released");
    check(!audio.open(3, "nonexistent-spatial-audio-probe.wav") && audio.voiceCount() == 0, "missing file leaves no voice");
    std::cout << (ok ? "PASS" : "FAIL") << " spatial geometry=20 nativeMixes=20 independentVoices=2 speakerOutput=muted\n";
    return ok ? 0 : 1;
}
