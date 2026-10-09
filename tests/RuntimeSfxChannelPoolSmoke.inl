#include "RuntimeSfxChannelPool.h"

namespace {
struct RecordingSfxBackend {
    std::array<bool, 8> opened{};
    std::array<bool, 8> playing{};
    std::vector<std::string> commands;
    int opens = 0, closes = 0, stops = 0, plays = 0, volumes = 0, durations = 0;
    bool failOpen = false, failPlay = false, failStatus = false, failVolume = false;
    bool failStop = false;
    std::size_t lastPlayed = 8;
    std::optional<bool> isPlaying(std::size_t channel) {
        commands.push_back("status " + std::to_string(channel));
        if (failStatus) return std::nullopt;
        return playing[channel];
    }
    bool stop(std::size_t channel) {
        ++stops;
        commands.push_back("stop " + std::to_string(channel));
        if (failStop) return false;
        playing[channel] = false;
        return true;
    }
    void close(std::size_t channel) {
        ++closes;
        commands.push_back("close " + std::to_string(channel));
        opened[channel] = false;
        playing[channel] = false;
    }
    bool open(std::size_t channel, const std::string& path) {
        ++opens;
        commands.push_back("open " + std::to_string(channel) + " " + path);
        opened[channel] = true; // A failing backend can leave a partial alias.
        return !failOpen;
    }
    bool setMix(std::size_t channel, int volume, int pan) {
        (void)pan;
        ++volumes;
        commands.push_back("volume " + std::to_string(channel) + " " + std::to_string(volume));
        return !failVolume;
    }
    bool playFromStart(std::size_t channel) {
        ++plays;
        commands.push_back("play " + std::to_string(channel) + " from 0");
        if (failPlay) return false;
        playing[channel] = true;
        lastPlayed = channel;
        return true;
    }
    int durationTicks(const std::string&, int) { ++durations; return 30; }
};

template <std::size_t Count>
struct SfxPoolProbe {
    std::array<RuntimeSfxChannelReuseState, Count> cache{};
    std::array<bool, Count> active{};
    std::array<int, Count> busy{};
    std::size_t next = 0;
    int steals = 0;
    RecordingSfxBackend backend;
    bool play(const std::string& path, int tick, int volume = 75, int hz = 30) {
        return playRuntimeSfxPooled(cache, active, busy, next, steals,
            tick, hz, path, volume, backend);
    }
    void stop() { stopRuntimeSfxPooled(cache, active, busy, backend); }
};

bool verifyRuntimeSfxChannelPool() {
    bool ok = true;
    SfxPoolProbe<8> repeated;
    for (int tick = 0; tick < 12000; ++tick) {
        repeated.backend.playing.fill(false);
        if (!repeated.play("cannon.wav", tick)
            || repeated.backend.lastPlayed != static_cast<std::size_t>(tick % 8)) {
            return expect(false, "SFX reuse changed round-robin channel selection");
        }
    }
    ok &= expect(repeated.backend.opens == 8 && repeated.backend.closes == 0
            && repeated.backend.stops == 0 && repeated.backend.plays == 12000
            && repeated.backend.volumes == 8 && repeated.backend.durations == 8,
        "SFX repeated sample reopened devices or lost playback commands");
    const auto commandCount = repeated.backend.commands.size();
    const auto nextBeforeMute = repeated.next;
    ok &= expect(repeated.play("other.wav", 12001, 0)
            && repeated.backend.commands.size() == commandCount
            && repeated.next == nextBeforeMute,
        "muted SFX called a device or advanced channel rotation");
    repeated.stop();
    const int closedAfterStop = repeated.backend.closes;
    repeated.stop();
    ok &= expect(closedAfterStop == 8 && repeated.backend.closes == 8
            && std::none_of(repeated.active.begin(), repeated.active.end(), [](bool v) { return v; })
            && std::none_of(repeated.cache.begin(), repeated.cache.end(),
                [](const auto& lane) { return lane.opened || !lane.samplePath.empty(); }),
        "SFX stop left cached aliases or duplicated shutdown commands");
    ok &= expect(repeated.play("cannon.wav", 12002)
            && repeated.backend.opens == 9,
        "SFX new session reused a previously closed alias");
    repeated.stop();

    SfxPoolProbe<8> busy;
    for (int tick = 0; tick < 8; ++tick) ok &= busy.play("cannon.wav", tick);
    ok &= expect(busy.play("cannon.wav", 8) && busy.steals == 1
            && busy.backend.lastPlayed == 0 && busy.backend.opens == 8
            && busy.backend.stops == 1 && busy.backend.closes == 0,
        "SFX full pool changed oldest-channel stealing or restarted by reopening");
    busy.backend.playing[3] = false;
    ok &= expect(busy.play("cannon.wav", 9) && busy.backend.lastPlayed == 3
            && busy.steals == 1,
        "SFX did not choose the first finished lane from its rotation cursor");
    busy.stop();

    SfxPoolProbe<1> changing;
    ok &= changing.play("one.wav", 0);
    changing.backend.playing[0] = false;
    ok &= expect(changing.play("two.wav", 31) && changing.backend.opens == 2
            && changing.backend.closes == 1
            && changing.cache[0].samplePath == "two.wav",
        "SFX replacing a cached sample did not close its former alias");
    changing.backend.failVolume = true;
    changing.backend.playing[0] = false;
    ok &= !changing.play("two.wav", 62, 50);
    changing.backend.playing[0] = false;
    const int beforeRetryVolume = changing.backend.volumes;
    ok &= expect(!changing.play("two.wav", 93, 50)
            && changing.backend.volumes == beforeRetryVolume + 1
            && changing.cache[0].volumePercent == -1 && !changing.cache[0].opened,
        "SFX played after a rejected mix or retained a failed voice");
    changing.backend.failVolume = false;
    changing.backend.playing[0] = false;
    ok &= changing.play("two.wav", 124, 50, 60);
    ok &= expect(changing.cache[0].volumePercent == 50
            && changing.backend.durations == 3,
        "SFX reused a stale volume or duration after a tick-rate change");
    changing.backend.failPlay = true;
    ok &= expect(!changing.play("two.wav", 125, 50)
            && !changing.cache[0].opened && !changing.active[0]
            && !changing.backend.opened[0],
        "SFX failed play left a live or reusable alias");
    changing.backend.failPlay = false;
    changing.backend.failOpen = true;
    ok &= expect(!changing.play("bad.wav", 126)
            && !changing.cache[0].opened && !changing.backend.opened[0],
        "SFX failed open leaked a partially opened alias");
    changing.backend.failOpen = false;
    ok &= changing.play("two.wav", 127);
    changing.backend.failStatus = true;
    const int opensBeforeStatusFailure = changing.backend.opens;
    ok &= expect(changing.play("two.wav", 158)
            && changing.backend.opens == opensBeforeStatusFailure + 1,
        "SFX reused a device after its status query failed");
    changing.backend.failStatus = false;
    changing.backend.failStop = true;
    const int opensBeforeStopFailure = changing.backend.opens;
    ok &= expect(changing.play("two.wav", 159)
            && changing.backend.opens == opensBeforeStopFailure + 1,
        "SFX reused a stolen lane after stop failed");
    changing.backend.failStop = false;
    changing.stop();
    std::cout << "[runtime-sfx-pool] plays=12000 opens=8 closesAtShutdown=8"
        << " rotation=yes stealing=yes sampleReplacement=yes failures=yes mutedDeviceCalls=0\n";
    return ok;
}
} // namespace
