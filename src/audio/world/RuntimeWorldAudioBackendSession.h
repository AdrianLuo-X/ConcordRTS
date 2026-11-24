#pragma once

#include "audio/world/RuntimeWorldAudioSystem.h"
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

class RuntimeVoiceAudio;

namespace concord::audio {

struct RuntimeWorldAudioBackendBinding {
    std::uint64_t handleId = 0;
    std::uint64_t playbackOrder = 0;
    int gainPermille = 0;
    bool active = false;
    RuntimeWorldAudioAction action;
    RuntimeSpatialAudioPosition position;
    int gain16384 = 0;
    int pan16384 = 8192;
};

// Synchronous, local playback queries. Each operation borrows its result only
// at the original call site; no World, root state, clock or device is retained.
class RuntimeWorldAudioBackendHost {
public:
    virtual ~RuntimeWorldAudioBackendHost() = default;
    virtual RuntimeSpatialAudioPosition sourcePosition(const RuntimeWorldAudioAction&) = 0;
    virtual RuntimeSpatialAudioResult spatial(const std::string&, const RuntimeSpatialAudioPosition&) = 0;
    virtual bool soundsEnabled() = 0;
    virtual int effectiveGain(int authoredPermille, int spatial16384) = 0;
    virtual std::optional<std::string> samplePath(const RuntimeWorldAudioAction&) = 0;
    virtual RuntimeVoiceAudio* audioIfInitialized() = 0;
    virtual RuntimeVoiceAudio& audioFor() = 0;
    virtual void enforceVoiceLimit(const std::string&, std::uint64_t excludedHandle) = 0;
    virtual int tick() = 0;
};

// Owns local voice bindings and their observations, never authoritative save
// state. The original Voice PImpl owns PCM/device lifetime. This owner has no
// device-closing destructor: the existing stop/rehydrate boundaries stay explicit.
class RuntimeWorldAudioBackendSession {
public:
    using Bindings = std::unordered_map<std::uint64_t, RuntimeWorldAudioBackendBinding>;
    const Bindings& bindings() const noexcept { return bindings_; }
    const std::deque<RuntimeWorldAudioAction>& history() const noexcept { return history_; }
    void clearHistory() { history_.clear(); }
    bool limitEvicted(std::uint64_t handle) const { return evicted_.contains(handle); }
    std::uint64_t nextPlaybackOrder() { return ++voiceOrder_; }
    void remember(const RuntimeWorldAudioAction&);
    void evictVoice(std::uint64_t handle, RuntimeVoiceAudio&);
    void apply(const RuntimeWorldAudioAction&, RuntimeWorldAudioBackendHost&);
    void stop(RuntimeWorldAudioBackendHost&);
    void reap(RuntimeWorldAudioBackendHost&);
    void mix(RuntimeWorldAudioBackendHost&);
private:
    Bindings bindings_;
    std::unordered_set<std::uint64_t> evicted_;
    std::uint64_t voiceOrder_ = 0;
    std::deque<RuntimeWorldAudioAction> history_;
};

} // namespace concord::audio
