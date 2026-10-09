#pragma once

#include "RuntimeWorldAudioSystem.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

struct RuntimeObjectAmbientSpec {
    RuntimeWorldAudioSoundSpec sound;
    bool all = false;
    bool attack = false;
    bool decay = false;
    bool literalLoop = false;
    bool shroud = false;
    bool global = false;
};

struct RuntimeObjectAmbientSource {
    std::string key;
    std::string type;
    std::string event;
    RuntimeWorldAudioCell cell;
    int gainPermille = 0;
    int sourceObjectId = -1;
};

struct RuntimeObjectAmbientBinding {
    std::uint64_t handle = 0;
    RuntimeObjectAmbientSource source;
    std::uint64_t nextMilliseconds = 0;
    std::uint64_t endMilliseconds = 0;
    std::vector<int> remainingSamples;
    std::string sample;
    int ordinal = 0;
    int cycles = 0;
    int volumeDelta = 0;
    int lastGain = 0;
    bool introductionPending = true;
    bool active = false;
    bool backendLoop = false;
};

// Local sound controllers mirror ObjectClass's non-persisted audio controllers.
// Camera movement and device clocks never enter gameplay hashes or RNG streams.
struct RuntimeObjectAmbientAudioState {
    std::map<std::string, RuntimeObjectAmbientSpec> specs;
    std::map<std::string, RuntimeObjectAmbientBinding> bindings;
    std::map<std::string, int> sampleDurationMilliseconds;
    std::map<std::string, std::string> typeEvents;
    std::uint64_t preparedRulesHash = 0;
    bool prepared = false;
    std::uint64_t nextHandle = 1;
    std::uint64_t lastMilliseconds = 0;

    static std::uint64_t after(std::uint64_t now, std::uint64_t duration) {
        return now + std::min(duration, (std::numeric_limits<std::uint64_t>::max)() - now);
    }
    static std::uint64_t sourceSeed(const RuntimeObjectAmbientSource& source, int ordinal) {
        std::uint64_t id = 14695981039346656037ULL;
        for (unsigned char c : source.key) id = (id ^ c) * 1099511628211ULL;
        return runtimeAudioVariationSeed(source.event, id, ordinal);
    }
    static int delay(const RuntimeObjectAmbientSpec& spec,
        const RuntimeObjectAmbientSource& source, int ordinal, bool initial) {
        int lo = spec.sound.delayMinMilliseconds;
        const int hi = std::max(lo, spec.sound.delayMaxMilliseconds);
        // Retail 40572C: Ambient's first delay begins at 33 ms, while Predelay
        // uses the authored minimum. Subsequent gaps use Delay=min..max.
        if (initial) {
            if (spec.sound.ambient) lo = 33;
            else if (!spec.sound.predelay) return 0;
        }
        if (hi < 33) return 0;
        lo = std::min(lo, hi);
        const int selected = lo + static_cast<int>(sourceSeed(source, ordinal + 7919)
            % static_cast<std::uint64_t>(hi - lo + 1));
        return selected >= 33 ? selected : 0;
    }

    std::vector<RuntimeWorldAudioAction> advance(
        std::vector<RuntimeObjectAmbientSource> sources, std::uint64_t now, int tick,
        const std::function<int(const std::string&, int)>& duration) {
        std::vector<RuntimeWorldAudioAction> actions;
        const auto stop = [&](RuntimeObjectAmbientBinding& binding, const char* reason) {
            if (!binding.active) return;
            RuntimeWorldAudioAction action;
            action.kind = RuntimeWorldAudioActionKind::Stop;
            action.handleId = binding.handle; action.eventId = binding.source.event;
            action.cell = binding.source.cell; action.eventTick = tick; action.reason = reason;
            actions.push_back(action); binding.active = false;
        };
        if (now < lastMilliseconds) {
            for (auto& [key, binding] : bindings) stop(binding, "object ambient clock reset");
            bindings.clear();
        }
        lastMilliseconds = now;
        std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) {return a.key < b.key;});
        sources.erase(std::unique(sources.begin(), sources.end(), [](const auto& a, const auto& b) {
            return a.key == b.key;
        }), sources.end());
        std::map<std::string, const RuntimeObjectAmbientSource*> eligible;
        for (const auto& source : sources) {
            const auto spec = specs.find(source.event);
            if (source.gainPermille > 0 && spec != specs.end() && !spec->second.sound.samples.empty())
                eligible[source.key] = &source;
        }
        std::map<std::string, int> eventCounts;
        for (auto it = bindings.begin(); it != bindings.end();) {
            const auto found = eligible.find(it->first);
            if (found == eligible.end() || found->second->type != it->second.source.type
                || found->second->event != it->second.source.event) {
                stop(it->second, "object left map, audible range, or changed sound");
                it = bindings.erase(it);
            } else {
                it->second.source = *found->second;
                ++eventCounts[it->second.source.event]; ++it;
            }
        }
        for (const auto& source : sources) {
            if (!eligible.contains(source.key) || bindings.contains(source.key) || bindings.size() >= 4096) continue;
            const auto& spec = specs.at(source.event);
            if (spec.sound.simultaneousLimit > 0 && eventCounts[source.event] >= spec.sound.simultaneousLimit) continue;
            RuntimeObjectAmbientBinding binding;
            binding.handle = 0x2000000000000000ULL | nextHandle++;
            binding.source = source;
            binding.nextMilliseconds = after(now, delay(spec, source, 0, true));
            bindings.emplace(source.key, std::move(binding)); ++eventCounts[source.event];
        }
        for (auto& [key, binding] : bindings) {
            const auto& spec = specs.at(binding.source.event);
            const auto& sound = spec.sound;
            const auto variedGain = [&](int delta) {
                const int authored = std::max(1, sound.volumePercent * 10);
                return std::clamp(binding.source.gainPermille * std::clamp(authored + delta, 0, 1000) / authored, 0, 1000);
            };
            const int gain = variedGain(binding.volumeDelta);
            if (binding.active && gain != binding.lastGain) {
                RuntimeWorldAudioAction action;
                action.kind = RuntimeWorldAudioActionKind::SetGain; action.handleId = binding.handle;
                action.eventId = binding.source.event; action.cell = binding.source.cell;
                action.gainPermille = gain; action.eventTick = tick; action.reason = "object ambient moved";
                action.sourceObjectId = binding.source.sourceObjectId;
                action.authoredGainPermille = std::clamp(spec.sound.volumePercent * 10 + binding.volumeDelta, 0, 1000);
                actions.push_back(action); binding.lastGain = gain;
            }
            if (binding.backendLoop) continue;
            if (binding.active && now >= binding.endMilliseconds) stop(binding, "object ambient sample completed");
            if (now < binding.nextMilliseconds) continue;
            int sampleIndex = -1;
            const int first = spec.attack ? 1 : 0;
            const int last = static_cast<int>(sound.samples.size()) - (spec.decay ? 1 : 0);
            if (first >= last) continue;
            if (binding.introductionPending && spec.attack) sampleIndex = 0;
            else {
                if (binding.remainingSamples.empty()) {
                    // With a substantial Delay the original reloads its sample
                    // list; immediate All loops consume a shuffled bag per cycle.
                    for (int i = first; i < last; ++i) binding.remainingSamples.push_back(i);
                    ++binding.cycles;
                }
                const auto pick = sound.randomSample
                    ? sourceSeed(binding.source, binding.ordinal) % binding.remainingSamples.size() : 0;
                sampleIndex = binding.remainingSamples[pick];
                binding.remainingSamples.erase(binding.remainingSamples.begin() + pick);
                if (!spec.all || sound.delayMinMilliseconds >= 33) binding.remainingSamples.clear();
            }
            binding.introductionPending = false;
            binding.sample = sound.samples[sampleIndex];
            const auto variation = runtimeAudioVariationFor(sound.variation, sourceSeed(binding.source, binding.ordinal));
            binding.volumeDelta = variation.volumeDeltaPermille;
            RuntimeWorldAudioAction action;
            action.kind = binding.ordinal == 0 ? RuntimeWorldAudioActionKind::Play : RuntimeWorldAudioActionKind::Replay;
            action.handleId = binding.handle; action.eventId = binding.source.event;
            action.sampleName = binding.sample; action.cell = binding.source.cell; action.eventTick = tick;
            action.playbackOrdinal = binding.ordinal++; action.gainPermille = variedGain(binding.volumeDelta);
            action.sourceObjectId = binding.source.sourceObjectId;
            action.authoredGainPermille = std::clamp(spec.sound.volumePercent * 10 + binding.volumeDelta, 0, 1000);
            action.frequencyPermille = variation.frequencyPermille; action.local = !spec.global; action.rangeCells = sound.rangeCells;
            action.reason = "object AmbientSound controller";
            binding.backendLoop = spec.literalLoop && sound.loopIterations == 0 && !spec.attack
                && sound.samples.size() == 1 && sound.delayMaxMilliseconds < 33;
            action.backendLoopIterations = binding.backendLoop ? -1 : 0;
            actions.push_back(action); binding.active = true; binding.lastGain = action.gainPermille;
            binding.endMilliseconds = after(now, std::max(1, duration(binding.sample, action.frequencyPermille)));
            binding.nextMilliseconds = after(binding.endMilliseconds, delay(spec, binding.source, binding.ordinal, false));
        }
        return actions;
    }
};
