#pragma once

#include "data/maps/MapDatabase.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

class IniFile;

enum class RuntimeLightingMode {
    Base,
    Ion,
    Dominator
};

struct RuntimeLightingConfig {
    int fixedTickHz = 30;
    // [AudioVisual] AmbientChangeRate is expressed in game minutes.
    double ambientChangeRate = 0.0;
    double ambientChangeStep = 0.0;
};

// A renderer-facing, value-only snapshot.  The renderer receives this
// explicitly through RuntimeMapRenderContext; no global lighting singleton is
// consulted.  Identity (1,1,1) intentionally preserves the old pixels.
struct RuntimeLightingTint {
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
};

// Renderer-facing local illumination.  These are derived value snapshots,
// not persistent lighting authority: the owning gameplay system remains the
// single source of truth across save/load and replay.
struct RuntimeLocalLightSnapshot {
    std::uint64_t stableId = 0;
    std::string owner;
    double worldX = 0.0;
    double worldY = 0.0;
    double worldLevel = 0.0;
    int radiusPixels = 0;
    std::uint8_t red = 255;
    std::uint8_t green = 255;
    std::uint8_t blue = 255;
    std::uint8_t intensity = 0;
    std::uint64_t revision = 0;
};

enum class RuntimeLightingTriggerActionKind {
    SetAmbientStep = 71,
    SetAmbientRate = 72,
    SetAmbientLight = 73
};

// rawValue follows the map trigger format.  Actions 71/72 store an IEEE-754
// float in the bits of a signed 32-bit integer; action 73 stores a percentage.
struct RuntimeLightingTriggerAction {
    int actionType = 0;
    std::int32_t rawValue = 0;
};

struct RuntimeLightingTriggerActionResult {
    bool supported = false;
    bool accepted = false;
    int actionType = 0;
    double decodedValue = 0.0;
    std::string reason;
};

enum class RuntimeLightingThresholdDirection {
    Below,
    Above
};

// eventId is the host's stable event-instance identity, not merely 45/46.
// Separate trigger events may therefore watch the same threshold independently.
struct RuntimeLightingThresholdRequest {
    int eventId = -1;
    int eventType = 0;
    int thresholdPercent = 0;
};

struct RuntimeLightingThresholdResult {
    bool supported = false;
    bool valid = false;
    bool triggered = false;
    bool hadPreviousSample = false;
    RuntimeLightingThresholdDirection direction =
        RuntimeLightingThresholdDirection::Below;
    double threshold = 0.0;
    double previousAmbient = 0.0;
    double currentAmbient = 0.0;
};

enum class RuntimeLightingChangeKind {
    ProfileActivated,
    AmbientChanged,
    AmbientTargetReached
};

struct RuntimeLightingChange {
    RuntimeLightingChangeKind kind =
        RuntimeLightingChangeKind::AmbientChanged;
    int eventTick = -1;
    RuntimeLightingMode mode = RuntimeLightingMode::Base;
    double previousAmbient = 1.0;
    double currentAmbient = 1.0;
};

struct RuntimeLightingReconcileReport {
    bool contextValid = false;
    bool profilesMatched = false;
    bool stateReset = false;
    bool valuesSanitized = false;
    std::size_t retainedThresholdWatchers = 0;
    std::size_t removedThresholdWatchers = 0;
};

class RuntimeLightingSystemState {
public:
    RuntimeLightingSystemState();
    explicit RuntimeLightingSystemState(
        const MapLighting& lighting,
        RuntimeLightingConfig config = {}
    );

    void reset(
        const MapLighting& lighting = {},
        RuntimeLightingConfig config = {}
    );

    const RuntimeLightingConfig& config() const;
    const MapLighting& authoredLighting() const;
    const MapLightingProfile& currentProfile() const;
    const MapLightingProfile& profile(RuntimeLightingMode mode) const;
    RuntimeLightingMode mode() const;
    int lastAdvancedTick() const;
    std::vector<int> thresholdWatcherEventIds() const;
    bool hasAmbientTarget() const;
    double targetAmbient() const;
    int ticksUntilAmbientStep() const;

    // Ion lighting is normally immediate.  Dominator activation defaults to
    // the map-authored DominatorAmbientChangeRate while changing colour fields
    // immediately.  Hosts may also request an immediate profile explicitly.
    RuntimeLightingChange activateProfile(
        RuntimeLightingMode mode,
        int currentTick,
        bool transitionAmbient
    );
    RuntimeLightingChange activateIon(int currentTick);
    RuntimeLightingChange activateDominator(int currentTick);
    RuntimeLightingChange restoreBase(
        int currentTick,
        bool transitionAmbient = true
    );

    void setAmbientChangeRate(double rate);
    void setAmbientChangeStep(double step);
    void setTargetAmbient(double ambient);
    void setTargetAmbientPercent(int percent);

    RuntimeLightingTriggerActionResult applyTriggerAction(
        const RuntimeLightingTriggerAction& action
    );
    RuntimeLightingThresholdResult evaluateThreshold(
        const RuntimeLightingThresholdRequest& request
    );

    std::vector<RuntimeLightingChange> tick(int currentTick);
    RuntimeLightingTint renderTint(double elevation = 1.0) const;

    RuntimeLightingReconcileReport reconcile(
        const MapLighting& lighting
    );

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const MapLighting& lighting,
        std::string* errorMessage = nullptr,
        RuntimeLightingReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash() const;

private:
    struct ThresholdWatcher {
        int eventId = -1;
        int eventType = 0;
        int thresholdPercent = 0;
        double previousAmbient = 1.0;
    };

    RuntimeLightingConfig config_;
    MapLighting authoredLighting_;
    MapLightingProfile currentProfile_;
    RuntimeLightingMode mode_ = RuntimeLightingMode::Base;
    int lastAdvancedTick_ = -1;
    std::optional<double> targetAmbient_;
    int ticksUntilAmbientStep_ = -1;
    std::optional<double> transitionRateOverride_;
    std::vector<ThresholdWatcher> thresholdWatchers_;

    void sortThresholdWatchers();
};

const char* runtimeLightingModeName(RuntimeLightingMode mode);
const char* runtimeLightingChangeName(RuntimeLightingChangeKind kind);

bool runtimeLightingModeFromName(
    const std::string& value,
    RuntimeLightingMode& mode
);

double runtimeLightingDecodeTriggerFloat(std::int32_t rawValue);
RuntimeLightingTint runtimeLightingTintForProfile(
    const MapLightingProfile& profile,
    double elevation = 1.0
);
bool runtimeLightingTintIsIdentity(const RuntimeLightingTint& tint);
std::uint32_t runtimeApplyLightingTintToPackedDibColor(
    std::uint32_t packedColor,
    const RuntimeLightingTint& tint
);
