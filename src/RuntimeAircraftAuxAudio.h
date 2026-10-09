#pragma once

#include "GameWorld.h"
#include "RuntimeWorldAudioSystem.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct RuntimeAircraftAuxPendingSound {
    RuntimeWorldAudioAction action;
    std::uint64_t dueMilliseconds = 0;
    std::uint64_t endMilliseconds = 0;
    bool playing = false;
};

// Device-time state is local. The world only transports unplayed notifications
// through successful transactions; neither layer changes gameplay randomness.
struct RuntimeAircraftAuxAudioState {
    std::vector<RuntimeAircraftAuxPendingSound> pending;
    std::map<std::string, int> durationMilliseconds;
    std::uint64_t nextHandle = 1;
    std::uint64_t lastMilliseconds = 0;
    int variationOrdinal = 0;

    static std::uint64_t after(std::uint64_t now, std::uint64_t duration) {
        return now + std::min(duration, (std::numeric_limits<std::uint64_t>::max)() - now);
    }

    bool enqueue(const RuntimeWorldAudioSoundSpec& sound,
        const WorldAircraftAuxSoundEvent& event, int objectId,
        std::uint64_t now, int gainPermille) {
        if (sound.samples.empty() || gainPermille <= 0 || pending.size() >= 256) return false;
        const auto sameEvent = std::count_if(pending.begin(), pending.end(), [&](const auto& entry) {
            return entry.action.eventId == sound.eventId;
        });
        if (sound.simultaneousLimit > 0 && sameEvent >= sound.simultaneousLimit) return false;
        if (variationOrdinal == (std::numeric_limits<int>::max)()) variationOrdinal = 0;
        const auto seed = runtimeAudioVariationSeed(sound.eventId,
            static_cast<std::uint64_t>(objectId), variationOrdinal++);
        const auto variation = runtimeAudioVariationFor(sound.variation, seed);
        const auto sampleIndex = sound.randomSample ? seed % sound.samples.size() : 0;
        int delay = 0;
        if (sound.predelay) {
            const int low = std::max(0, sound.delayMinMilliseconds);
            const int high = std::max(low, sound.delayMaxMilliseconds);
            delay = low + static_cast<int>((seed >> 16) % (static_cast<std::uint64_t>(high) - low + 1));
            if (delay < 33) delay = 0;
        }
        RuntimeAircraftAuxPendingSound entry;
        auto& action = entry.action;
        action.kind = RuntimeWorldAudioActionKind::Play;
        action.handleId = 0x1000000000000000ULL | nextHandle++;
        action.eventId = sound.eventId; action.sampleName = sound.samples[sampleIndex];
        action.cell = {event.x, event.y, event.level}; action.eventTick = event.tick;
        action.playbackOrdinal = 0; action.frequencyPermille = variation.frequencyPermille;
        const int authored = std::max(1, sound.volumePercent * 10);
        action.gainPermille = std::clamp(gainPermille * std::clamp(authored + variation.volumeDeltaPermille, 0, 1000) / authored, 0, 1000);
        action.local = sound.local; action.rangeCells = sound.rangeCells;
        action.authoredGainPermille = std::clamp(sound.volumePercent * 10 + variation.volumeDeltaPermille, 0, 1000);
        action.trackCamera = false;
        action.reason = event.takingOff ? "aircraft AuxSound1 takeoff" : "aircraft AuxSound2 landing";
        entry.dueMilliseconds = after(now, delay);
        pending.push_back(std::move(entry));
        return true;
    }

    std::vector<RuntimeWorldAudioAction> advance(std::uint64_t now,
        const std::function<int(const std::string&, int)>& duration) {
        std::vector<RuntimeWorldAudioAction> actions;
        const auto stop = [&](const RuntimeAircraftAuxPendingSound& entry, const char* reason) {
            if (!entry.playing) return;
            auto action = entry.action; action.kind = RuntimeWorldAudioActionKind::Stop;
            action.reason = reason; actions.push_back(std::move(action));
        };
        if (now < lastMilliseconds) {
            for (const auto& entry : pending) stop(entry, "aircraft audio clock reset");
            pending.clear();
        }
        lastMilliseconds = now;
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->playing && now >= it->endMilliseconds) {
                stop(*it, "aircraft auxiliary sample completed");
                it = pending.erase(it); continue;
            }
            if (!it->playing && now >= it->dueMilliseconds) {
                actions.push_back(it->action); it->playing = true;
                it->endMilliseconds = after(now, std::max(1, duration(it->action.sampleName, it->action.frequencyPermille)));
            }
            ++it;
        }
        return actions;
    }
};
