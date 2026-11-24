#include "audio/world/RuntimeWorldAudioBackendSession.h"
#include "audio/backend/RuntimeVoiceAudio.h"
#include "audio/backend/RuntimeVoicePath.h"
#include <utility>

namespace concord::audio {

void RuntimeWorldAudioBackendSession::remember(const RuntimeWorldAudioAction& action) {
    history_.push_back(action);
    while (history_.size() > 32U)
        history_.pop_front();
}

void RuntimeWorldAudioBackendSession::evictVoice(std::uint64_t handle, RuntimeVoiceAudio& audio) {
    const auto binding = bindings_.find(handle);
    if (binding != bindings_.end()
        && binding->second.action.sourceObjectId >= 0 && binding->second.action.queueAfterCurrent)
        evicted_.insert(handle);
    audio.close(handle);
    bindings_.erase(handle);
}

void RuntimeWorldAudioBackendSession::apply(const RuntimeWorldAudioAction& input, RuntimeWorldAudioBackendHost& host) {
    RuntimeWorldAudioAction action = input;
    if (action.kind == RuntimeWorldAudioActionKind::Stop) {
        remember(action);
        if (host.audioIfInitialized()) host.audioIfInitialized()->close(action.handleId);
        bindings_.erase(action.handleId);
        evicted_.erase(action.handleId); return;
    }
    auto found = bindings_.find(action.handleId);
    if (action.kind == RuntimeWorldAudioActionKind::SetGain && found != bindings_.end()) {
        const auto& previous = found->second.action;
        if (action.eventId.empty()) {action.eventId = previous.eventId; action.cell = previous.cell;}
        if (action.sourceObjectId < 0) action.sourceObjectId = previous.sourceObjectId;
        if (action.authoredGainPermille < 0) action.authoredGainPermille = previous.authoredGainPermille;
    }
    const auto position = host.sourcePosition(action);
    const auto spatial = host.spatial(action.eventId, position);
    action.pan16384 = spatial.pan16384; action.spatialGain16384 = spatial.gain16384;
    const int authored = action.authoredGainPermille >= 0 ? action.authoredGainPermille : action.gainPermille;
    action.authoredGainPermille = authored;
    action.gainPermille = authored * spatial.gain16384 / 16384;
    remember(action);
    if (!host.soundsEnabled()) return;
    const int gain = host.effectiveGain(authored, spatial.gain16384);
    if (action.kind == RuntimeWorldAudioActionKind::SetGain) {
        if (found == bindings_.end()) return;
        auto& binding = found->second;
        if (host.audioIfInitialized() && host.audioIfInitialized()->setMix(action.handleId, gain, action.pan16384)) {
            binding.action.authoredGainPermille = authored;
            binding.action.cell = action.cell; binding.action.sourceObjectId = action.sourceObjectId;
            binding.action.spatialGain16384 = action.spatialGain16384; binding.action.pan16384 = action.pan16384;
            binding.position = position; binding.gainPermille = action.gainPermille;
            binding.gain16384 = gain; binding.pan16384 = action.pan16384;
        }
        return;
    }
    if (action.queueAfterCurrent && action.sourceObjectId >= 0
        && evicted_.contains(action.handleId)) return;
    std::optional<std::string> path = host.samplePath(action);
    auto& audio = host.audioFor();
    if (action.queueAfterCurrent && path && found != bindings_.end()
        && audio.queue(action.handleId, concord::audio::runtimeVoiceUtf8PathFromLegacyNative(*path))) return;
    audio.close(action.handleId); bindings_.erase(action.handleId);
    if (!path || (spatial.gain16384 == 0 && action.backendLoopIterations != -1)) return;
    if (spatial.gain16384 > 0)
        host.enforceVoiceLimit(action.eventId, action.handleId);
    if (!audio.open(action.handleId, concord::audio::runtimeVoiceUtf8PathFromLegacyNative(*path)) || !audio.setMix(action.handleId, gain, action.pan16384)) {
        audio.close(action.handleId); return;
    }
    const bool active = spatial.gain16384 > 0;
    if (active && !audio.playFromStart(action.handleId, action.backendLoopIterations == -1, action.queueAfterCurrent)) {
        audio.close(action.handleId); return;
    }
    RuntimeWorldAudioBackendBinding binding;
    binding.handleId = action.handleId; binding.playbackOrder = nextPlaybackOrder();
    binding.active = active; binding.action = action;
    binding.position = position; binding.gainPermille = action.gainPermille;
    binding.gain16384 = gain; binding.pan16384 = action.pan16384;
    bindings_.emplace(action.handleId, std::move(binding));
}

void RuntimeWorldAudioBackendSession::stop(RuntimeWorldAudioBackendHost& host) {
    if (host.audioIfInitialized()) for (const auto& [id, binding] : bindings_)
        host.audioIfInitialized()->close(id);
    bindings_.clear();
    evicted_.clear();
}

void RuntimeWorldAudioBackendSession::reap(RuntimeWorldAudioBackendHost& host) {
    if (!host.audioIfInitialized()) return;
    auto& audio = *host.audioIfInitialized();
    for (auto it = bindings_.begin(); it != bindings_.end();) {
        auto& binding = it->second;
        if (binding.active && !audio.isPlaying(it->first).value_or(false)) {
            audio.close(it->first); it = bindings_.erase(it); continue;
        }
        ++it;
    }
}

void RuntimeWorldAudioBackendSession::mix(RuntimeWorldAudioBackendHost& host) {
    if (!host.audioIfInitialized()) return;
    auto& audio = *host.audioIfInitialized();
    for (auto it = bindings_.begin(); it != bindings_.end();) {
        auto& binding = it->second; auto& action = binding.action;
        if (action.trackCamera) {
            if (action.sourceObjectId >= 0) binding.position = host.sourcePosition(action);
            const auto spatial = host.spatial(action.eventId, binding.position);
            action.spatialGain16384 = spatial.gain16384; action.pan16384 = spatial.pan16384;
        }
        if (action.spatialGain16384 == 0) {
            if (binding.active) audio.stop(it->first);
            binding.active = false;
            if (action.backendLoopIterations != -1) {
                audio.close(it->first); it = bindings_.erase(it); continue;
            }
        } else if (!binding.active && action.backendLoopIterations == -1) {
            host.enforceVoiceLimit(action.eventId, it->first);
            binding.active = audio.playFromStart(it->first, true);
            if (binding.active) {
                binding.action.eventTick = host.tick();
                binding.playbackOrder = nextPlaybackOrder();
            }
        }
        const int gain = host.effectiveGain(action.authoredGainPermille, action.spatialGain16384);
        if (binding.gain16384 != gain || binding.pan16384 != action.pan16384) {
            if (audio.setMix(it->first, gain, action.pan16384)) {
                binding.gain16384 = gain; binding.pan16384 = action.pan16384;
                auto updated = action; updated.kind = RuntimeWorldAudioActionKind::SetGain;
                updated.gainPermille = action.authoredGainPermille * action.spatialGain16384 / 16384;
                updated.reason = "local listener mix changed";
                remember(updated);
            }
        }
        ++it;
    }
    reap(host);
}

} // namespace concord::audio
