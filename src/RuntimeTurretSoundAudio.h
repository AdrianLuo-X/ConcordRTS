#pragma once

#include "RuntimeObjectAmbientAudio.h"
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

struct RuntimeTurretSoundSource {
    int objectId = -1;
    RuntimeObjectAmbientSource source;
};

struct RuntimeTurretSoundMotion {
    RuntimeTurretSoundSource source;
    bool rotating = false;
};

struct RuntimeTurretSoundVoice {
    RuntimeTurretSoundSource source;
    std::uint64_t handle = 0;
    std::uint64_t endMilliseconds = 0;
    std::vector<int> remainingSamples;
    std::string sample;
    std::string phase;
    int ordinal = 0;
    int frequencyPermille = 1000;
    int volumeDelta = 0;
    int lastGain = 0;
    bool playing = false;
};

// Techno's controller owns the rotating event. EndLooping detaches a finite
// decay event; a subsequent turn receives another controller/handle.
struct RuntimeTurretSoundAudioState {
    std::map<std::string, RuntimeObjectAmbientSpec> specs;
    std::map<int, RuntimeTurretSoundVoice> active;
    std::vector<RuntimeTurretSoundVoice> endings;
    std::map<std::string, int> durations;
    std::uint64_t preparedRulesHash = 0;
    std::uint64_t nextHandle = 1;
    std::uint64_t lastMilliseconds = 0;
    bool prepared = false;

    static int gain(const RuntimeTurretSoundVoice& voice, const RuntimeObjectAmbientSpec& spec) {
        const int authored = std::max(1, spec.sound.volumePercent * 10);
        return std::clamp(voice.source.source.gainPermille
            * std::clamp(authored + voice.volumeDelta, 0, 1000) / authored, 0, 1000);
    }

    std::vector<RuntimeWorldAudioAction> advance(
        const std::vector<RuntimeTurretSoundSource>& sources,
        const std::vector<RuntimeTurretSoundMotion>& motions,
        std::uint64_t now, int tick,
        const std::function<int(const std::string&, int)>& duration,
        bool enabled = true) {
        std::vector<RuntimeWorldAudioAction> actions;
        const auto stop = [&](RuntimeTurretSoundVoice& voice, const char* reason) {
            if (!voice.playing) return;
            RuntimeWorldAudioAction action;
            action.kind = RuntimeWorldAudioActionKind::Stop; action.handleId = voice.handle;
            action.eventId = voice.source.source.event; action.cell = voice.source.source.cell;
            action.eventTick = tick; action.reason = reason;
            actions.push_back(std::move(action)); voice.playing = false;
        };
        if (!enabled || now < lastMilliseconds) {
            for (auto& [id, voice] : active) stop(voice, "turret audio reset");
            for (auto& voice : endings) stop(voice, "turret ending reset");
            active.clear(); endings.clear();
        }
        lastMilliseconds = now;
        if (!enabled) return actions;

        const auto play = [&](RuntimeTurretSoundVoice& voice, const RuntimeObjectAmbientSpec& spec,
                              int index, const char* phase) {
            if (index < 0 || index >= static_cast<int>(spec.sound.samples.size())) return;
            voice.sample = spec.sound.samples[index]; voice.phase = phase;
            RuntimeWorldAudioAction action;
            action.kind = voice.ordinal == 0 ? RuntimeWorldAudioActionKind::Play : RuntimeWorldAudioActionKind::Replay;
            action.handleId = voice.handle; action.eventId = spec.sound.eventId;
            action.sampleName = voice.sample; action.cell = voice.source.source.cell;
            action.eventTick = tick; action.playbackOrdinal = voice.ordinal++;
            action.frequencyPermille = voice.frequencyPermille;
            action.gainPermille = gain(voice, spec); voice.lastGain = action.gainPermille;
            action.authoredGainPermille = std::clamp(spec.sound.volumePercent * 10 + voice.volumeDelta, 0, 1000);
            action.trackCamera = std::string(phase) != "decay";
            action.sourceObjectId = action.trackCamera ? voice.source.objectId : -1;
            action.local = !spec.global; action.rangeCells = spec.sound.rangeCells;
            action.reason = std::string("turret rotation ") + phase;
            actions.push_back(std::move(action)); voice.playing = true;
            voice.endMilliseconds = RuntimeObjectAmbientAudioState::after(now,
                std::max(1, duration(voice.sample, voice.frequencyPermille)));
        };
        const auto endLoop = [&](RuntimeTurretSoundVoice& voice) {
            const bool wasPlaying = voice.playing;
            stop(voice, "turret rotation EndLooping");
            const auto spec = specs.find(voice.source.source.event);
            // Retail 405894 stops the current clip and selects the decay now,
            // without waiting for the rest of a random All loop to complete.
            if (wasPlaying && spec != specs.end() && spec->second.decay && !spec->second.sound.samples.empty()) {
                play(voice, spec->second, static_cast<int>(spec->second.sound.samples.size()) - 1, "decay");
                endings.push_back(voice);
            }
        };
        for (auto it = endings.begin(); it != endings.end();) {
            if (now >= it->endMilliseconds) {stop(*it, "turret decay completed"); it = endings.erase(it);}
            else ++it;
        }
        const auto bind = [&](const RuntimeTurretSoundSource& source) {
            const auto spec = specs.find(source.source.event);
            if (spec == specs.end() || spec->second.sound.samples.empty()) return;
            auto found = active.find(source.objectId);
            if (found != active.end() && found->second.source.source.event != source.source.event) {
                endLoop(found->second); active.erase(found); found = active.end();
            }
            if (found == active.end()) {
                RuntimeTurretSoundVoice voice; voice.source = source;
                active.emplace(source.objectId, std::move(voice));
            }
        };
        const auto playingCount = [&](const std::string& event) {
            int count = 0;
            for (const auto& [id, voice] : active) if (voice.playing && voice.source.source.event == event) ++count;
            for (const auto& voice : endings) if (voice.playing && voice.source.source.event == event) ++count;
            return count;
        };
        const auto nextBody = [&](RuntimeTurretSoundVoice& voice, const RuntimeObjectAmbientSpec& spec) {
            const int first = spec.attack ? 1 : 0;
            const int last = static_cast<int>(spec.sound.samples.size()) - (spec.decay ? 1 : 0);
            if (first >= last) return -1;
            if (voice.remainingSamples.empty()) for (int i = first; i < last; ++i) voice.remainingSamples.push_back(i);
            const auto seed = runtimeAudioVariationSeed(spec.sound.eventId, voice.handle, voice.ordinal);
            const auto pick = spec.sound.randomSample ? seed % voice.remainingSamples.size() : 0;
            const int index = voice.remainingSamples[pick];
            voice.remainingSamples.erase(voice.remainingSamples.begin() + pick);
            if (!spec.all) voice.remainingSamples.clear();
            return index;
        };
        const auto start = [&](RuntimeTurretSoundVoice& voice) {
            const auto& spec = specs.at(voice.source.source.event);
            if (voice.playing || voice.source.source.gainPermille <= 0) return;
            const auto limit = spec.sound.simultaneousLimit;
            if (limit > 0 && playingCount(spec.sound.eventId) >= limit) return;
            voice.handle = 0x3000000000000000ULL | nextHandle++;
            voice.ordinal = 0; voice.remainingSamples.clear();
            const auto variation = runtimeAudioVariationFor(spec.sound.variation,
                runtimeAudioVariationSeed(spec.sound.eventId, voice.handle, 0));
            // Retail picks frequency/volume variation once for the audio event,
            // retaining it across attack, loop body and decay.
            voice.frequencyPermille = variation.frequencyPermille; voice.volumeDelta = variation.volumeDeltaPermille;
            play(voice, spec, spec.attack ? 0 : nextBody(voice, spec), spec.attack ? "attack" : "body");
        };
        // Committed transitions may contain a short turn and its stop between
        // two presentations. Consume both, preserving their event order.
        for (const auto& motion : motions) {
            if (motion.rotating) {
                bind(motion.source);
                auto found = active.find(motion.source.objectId);
                if (found != active.end()) start(found->second);
            } else {
                auto found = active.find(motion.source.objectId);
                if (found != active.end() && found->second.source.source.event == motion.source.source.event) {
                    endLoop(found->second); active.erase(found);
                }
            }
        }
        std::map<int, const RuntimeTurretSoundSource*> present;
        for (const auto& source : sources) {present[source.objectId] = &source; bind(source);}
        for (auto it = active.begin(); it != active.end();) {
            auto& voice = it->second;
            const auto source = present.find(it->first);
            if (source == present.end()) {endLoop(voice); it = active.erase(it); continue;}
            const auto previousCell = voice.source.source.cell;
            voice.source = *source->second;
            const auto& spec = specs.at(voice.source.source.event);
            if (voice.source.source.gainPermille <= 0) {
                stop(voice, "turret outside audible range");
                // Keep the event type as 750D40 does, permitting range re-entry.
                ++it; continue;
            }
            if (voice.playing && (gain(voice, spec) != voice.lastGain
                || previousCell.x != voice.source.source.cell.x || previousCell.y != voice.source.source.cell.y
                || previousCell.level != voice.source.source.cell.level)) {
                RuntimeWorldAudioAction action; action.kind = RuntimeWorldAudioActionKind::SetGain;
                action.handleId = voice.handle; action.eventId = spec.sound.eventId;
                action.cell = voice.source.source.cell; action.eventTick = tick;
                action.gainPermille = gain(voice, spec); voice.lastGain = action.gainPermille;
                action.authoredGainPermille = std::clamp(spec.sound.volumePercent * 10 + voice.volumeDelta, 0, 1000);
                action.sourceObjectId = voice.source.objectId;
                action.reason = "turret sound position changed"; actions.push_back(std::move(action));
            }
            if (voice.playing && now >= voice.endMilliseconds) {
                stop(voice, "turret clip completed");
                const int index = nextBody(voice, spec);
                if (index >= 0) play(voice, spec, index, "body");
            } else if (!voice.playing) start(voice);
            ++it;
        }
        return actions;
    }
};
