#pragma once

#include "RuntimeAudioVariation.h"
#include "RuntimeSpatialAudio.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

class IniFile;

struct RuntimeWorldAudioCell {
    int x = 0;
    int y = 0;
    int level = 0;
};

struct RuntimeWorldAudioSoundSpec {
    int soundListIndex = -1;
    std::string eventId;
    std::vector<std::string> samples;
    int volumePercent = 80;
    RuntimeAudioVariationSpec variation;
    RuntimeSpatialAudioRules spatial;
    double rangeCells = 10.0;
    int delayMinMilliseconds = 0;
    int delayMaxMilliseconds = 0;
    int simultaneousLimit = 5;
    // Zero means unlimited, matching soundmd's missing Loop= value.
    int loopIterations = 0;
    bool loop = false;
    bool ambient = false;
    bool randomSample = false;
    bool predelay = false;
    bool local = false;
};

// Small runtime-facing soundmd catalog.  AudioCatalog is currently a report
// generator rather than a reusable gameplay database, so this parser remains
// deliberately narrow and only carries fields needed by world sound actions.
class RuntimeWorldAudioCatalog {
public:
    bool loadFromIni(
        const IniFile& ini,
        std::string* errorMessage = nullptr,
        std::vector<std::string>* warnings = nullptr
    );

    const RuntimeWorldAudioSoundSpec* find(
        const std::string& eventId
    ) const;
    const RuntimeWorldAudioSoundSpec* findBySoundListIndex(int index) const;
    const std::vector<RuntimeWorldAudioSoundSpec>& sounds() const;
    std::uint64_t stateHash(bool includeVariation = true) const;

private:
    std::vector<RuntimeWorldAudioSoundSpec> sounds_;
    // Value indexes remain valid when a catalog is copied or moved. Sound
    // specs stay in sound-list order for deterministic iteration and hashing.
    std::vector<std::size_t> soundsByEventId_;
};

struct RuntimeWorldAudioConfig {
    int fixedTickHz = 30;
    std::size_t maximumHandles = 4096;
};

struct RuntimeWorldAudioContext {
    const RuntimeWorldAudioCatalog* catalog = nullptr;
    bool listenerCellValid = false;
    RuntimeWorldAudioCell listenerCell;
};

struct RuntimeWorldAudioPlayRequest {
    // Action 99 supplies an event id.  soundListIndex is a fallback for hosts
    // that already decoded the numeric [SoundList] reference.
    std::string eventId;
    int soundListIndex = -1;
    int waypointId = -1;
    RuntimeWorldAudioCell cell;
    // Stable trigger/action identity.  Supplying it permits two identical
    // authored actions at one cell while making a retried action idempotent.
    std::string requestToken;
};

struct RuntimeWorldAudioStopRequest {
    std::uint64_t handleId = 0;
    int waypointId = -1;
    bool matchCell = false;
    RuntimeWorldAudioCell cell;
    std::string requestToken;
};

struct RuntimeWorldAudioHandleState {
    std::uint64_t handleId = 0;
    std::string eventId;
    std::string requestToken;
    int waypointId = -1;
    RuntimeWorldAudioCell cell;
    int createdTick = -1;
    int lastPlaybackTick = -1;
    int nextPlaybackTick = -1;
    int playbackOrdinal = 0;
    int lastGainPermille = -1;
    bool continuousLoopIssued = false;
};

enum class RuntimeWorldAudioActionKind {
    Play,
    Replay,
    SetGain,
    Stop
};

// Value-only host instruction.  The gameplay module never opens a device or
// calls MCI; RuntimeShell can translate these records to its audio backend.
struct RuntimeWorldAudioAction {
    RuntimeWorldAudioActionKind kind = RuntimeWorldAudioActionKind::Play;
    std::uint64_t handleId = 0;
    std::string eventId;
    std::string sampleName;
    int waypointId = -1;
    RuntimeWorldAudioCell cell;
    int eventTick = -1;
    int playbackOrdinal = 0;
    int gainPermille = 0;
    int frequencyPermille = 1000;
    bool local = false;
    double rangeCells = 0.0;
    // A zero value means one-shot.  -1 means an indefinite backend loop.
    // Finite Loop= playback is always scheduled by this core as Replay
    // actions, so hosts never need a device-specific counted-loop feature.
    int backendLoopIterations = 0;
    std::string reason;
    // Local presentation metadata; never written into authoritative save state.
    int authoredGainPermille = -1;
    int sourceObjectId = -1;
    bool trackCamera = true;
    int pan16384 = 8192;
    int spatialGain16384 = 16384;
    bool queueAfterCurrent = false;
};

struct RuntimeWorldAudioCommandResult {
    bool accepted = false;
    bool duplicate = false;
    std::uint64_t handleId = 0;
    std::string reason;
    std::vector<RuntimeWorldAudioAction> actions;
};

struct RuntimeWorldAudioReconcileReport {
    bool contextValid = false;
    std::size_t retainedHandles = 0;
    std::size_t removedMissingSounds = 0;
    std::size_t removedCompletedHandles = 0;
    std::size_t rescheduledLegacyFiniteHandles = 0;
    std::size_t removedByEventLimit = 0;
    std::size_t removedByGlobalLimit = 0;
    std::vector<std::uint64_t> removedHandleIds;
};

class RuntimeWorldAudioSystemState {
public:
    RuntimeWorldAudioSystemState();
    explicit RuntimeWorldAudioSystemState(RuntimeWorldAudioConfig config);

    void reset(RuntimeWorldAudioConfig config = {});
    const RuntimeWorldAudioConfig& config() const;

    RuntimeWorldAudioCommandResult playAt(
        const RuntimeWorldAudioContext& context,
        const RuntimeWorldAudioPlayRequest& request,
        int currentTick
    );
    RuntimeWorldAudioCommandResult stopAt(
        const RuntimeWorldAudioContext& context,
        const RuntimeWorldAudioStopRequest& request,
        int currentTick
    );
    std::vector<RuntimeWorldAudioAction> tick(
        const RuntimeWorldAudioContext& context,
        int currentTick
    );
    // Audio devices are intentionally not serialized.  After a successful
    // load (or backend reset), the host calls this once to recreate active
    // indefinite loops without advancing playback ordinals or fixed ticks.
    std::vector<RuntimeWorldAudioAction> rehydrateActions(
        const RuntimeWorldAudioContext& context,
        int currentTick
    ) const;
    RuntimeWorldAudioReconcileReport reconcile(
        const RuntimeWorldAudioContext& context
    );

    const RuntimeWorldAudioHandleState* findHandle(
        std::uint64_t handleId
    ) const;
    const std::vector<RuntimeWorldAudioHandleState>& handles() const;
    int lastAdvancedTick() const;

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const RuntimeWorldAudioContext& context,
        std::string* errorMessage = nullptr,
        RuntimeWorldAudioReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash() const;

private:
    RuntimeWorldAudioConfig config_;
    int lastAdvancedTick_ = -1;
    int lastCommandTick_ = -1;
    std::uint64_t nextHandleId_ = 1;
    std::vector<std::string> commandKeysThisTick_;
    std::vector<RuntimeWorldAudioHandleState> handles_;

    bool registerCommand(const std::string& key, int currentTick);
    void sortState();
};

const char* runtimeWorldAudioActionName(RuntimeWorldAudioActionKind kind);
