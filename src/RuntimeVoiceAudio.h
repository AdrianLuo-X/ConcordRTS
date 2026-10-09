#pragma once

#include "RuntimeSpatialAudio.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct RuntimeVoiceAudioProbe {
    bool available = false;
    unsigned channels = 0;
    unsigned sampleRate = 0;
    unsigned bitsPerSample = 0;
    unsigned frameBytes = 0;
    bool masteringLimiterEnabled = false;
    unsigned buffersQueued = 0;
    std::uint64_t samplesPlayed = 0;
    float volume = 0;
    float masterVolume = 0;
    std::vector<float> matrix;
};

// One local owner per runtime window, never copied into transactions or saves.
// Source voices retain their own PCM storage until DestroyVoice has completed.
class RuntimeVoiceAudio {
public:
    explicit RuntimeVoiceAudio(bool muteOutputForVerification = false);
    ~RuntimeVoiceAudio();
    RuntimeVoiceAudio(const RuntimeVoiceAudio&) = delete;
    RuntimeVoiceAudio& operator=(const RuntimeVoiceAudio&) = delete;
    bool ready() const;
    long lastError() const;
    bool open(std::uint64_t handle, const std::string& path);
    bool setMix(std::uint64_t handle, int gain16384, int pan16384);
    bool playFromStart(std::uint64_t handle, bool loop = false, bool continuousSequence = false);
    // Append compatible PCM before the current buffer ends, without restarting
    // the source voice. Storage is retained until XAudio2 has consumed it.
    bool queue(std::uint64_t handle, const std::string& path);
    unsigned queuedBuffers(std::uint64_t handle) const;
    std::optional<bool> isPlaying(std::uint64_t handle) const;
    bool stop(std::uint64_t handle);
    void close(std::uint64_t handle);
    RuntimeVoiceAudioProbe probe(std::uint64_t handle) const;
    std::size_t voiceCount() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
