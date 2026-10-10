#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

class IniFile;

struct RuntimeMagBeamLocation {
    int x = -1;
    int y = -1;
    int cell = -1;
    int level = 0;
    bool onBridge = false;
    bool water = false;
};

// A value-only copy of the host locomotor pose.  Magnetron movement is driven
// by the temporary locomotor, never by guessed fixed phase timers in this
// core.
struct RuntimeMagBeamKinematics {
    RuntimeMagBeamLocation location;
    double worldX = 0.0;
    double worldY = 0.0;
    double worldZ = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double velocityZ = 0.0;
};

enum class RuntimeMagBeamTargetKind : std::uint8_t {
    Vehicle = 0,
    Infantry,
    Structure,
    Aircraft,
    Other
};

// The host constructs these snapshots from its world and rules databases.
// The core never stores a world pointer, callback, passenger pointer, or
// borrowed string.
struct RuntimeMagBeamSourceSnapshot {
    int objectId = -1;
    std::string owner;
    std::string typeId;
    bool present = false;
    bool alive = false;
};

struct RuntimeMagBeamTargetSnapshot {
    int objectId = -1;
    std::string owner;
    std::string typeId;
    RuntimeMagBeamTargetKind kind = RuntimeMagBeamTargetKind::Other;
    bool present = false;
    bool alive = false;
    bool enemyToSource = false;
    bool onGround = false;
    bool airborne = false;
    bool liftable = false;
    bool magBeamImmune = false;
    bool locomotorImmune = false;
    double weight = 0.0;
    int currentStrength = 0;
    int maximumStrength = 0;
    int suppressionThreshold = 0;
    std::string originalLocomotorId;
    RuntimeMagBeamKinematics kinematics;
};

struct RuntimeMagBeamWeaponSnapshot {
    std::string weaponId;
    std::string warheadId;
    std::string locomotorId;
    bool weaponExists = false;
    // Visual-only.  Stock locomotor behavior is keyed by the warhead below.
    bool isMagBeam = false;
    bool warheadExists = false;
    bool isLocomotor = false;
    // The integer damage payload is the strict stock weight ceiling:
    // rawDamage > TechnoType::Weight.  It is not HP damage.
    int rawDamage = 0;
};

struct RuntimeMagBeamRulesSnapshot {
    double fallingDamageMultiplier = 1.0;
    bool currentStrengthDamage = true;
};

struct RuntimeMagBeamLimits {
    std::size_t maxBindings = 512;
    std::size_t maxActionsPerAdvance = 2048;
    std::size_t maxInputRecords = 4096;
    std::size_t maxTextLength = 128;
    int maxRecursionDepth = 0;
};

enum class RuntimeMagBeamPhase : std::uint8_t {
    Acquired = 0,
    Lifting,
    Carried,
    Falling
};

enum class RuntimeMagBeamVeto : std::uint8_t {
    None = 0,
    InvalidTick,
    InvalidSource,
    InvalidOwner,
    InvalidTarget,
    FriendlyTarget,
    IllegalTargetKind,
    TargetAirborne,
    TargetImmune,
    TargetTooHeavy,
    InvalidWeapon,
    InvalidRules,
    RecursiveRequest,
    SourceBusy,
    TargetBusy,
    BindingLimitReached,
    IdExhausted
};

// Values 0..8 intentionally retain schema-1 numbering.
enum class RuntimeMagBeamReleaseReason : std::uint8_t {
    None = 0,
    Explicit = 1,
    SourceMissing = 2,
    SourceDead = 3,
    SourceIdentityChanged = 4,
    TargetMissing = 5,
    TargetDead = 6,
    TargetIdentityChanged = 7,
    Reconciled = 8,
    GentleLanding = 9,
    Impact = 10
};

enum class RuntimeMagBeamHostUpdateKind : std::uint8_t {
    LiftStarted = 0,
    Carried,
    GentleLanded,
    ReleasedFalling,
    ImpactResolved
};

struct RuntimeMagBeamOccupancySnapshot {
    bool occupied = false;
    int objectId = -1;
    RuntimeMagBeamTargetKind kind = RuntimeMagBeamTargetKind::Other;
    std::string owner;
    std::string typeId;
};

// "Resolved" here means the locomotor has reported physical contact.  The
// host has not applied damage yet; ResolveLanding below requests one atomic
// collision/damage resolution using this immutable snapshot.
struct RuntimeMagBeamImpactSnapshot {
    RuntimeMagBeamKinematics kinematics;
    bool cellLegal = true;
    RuntimeMagBeamOccupancySnapshot occupancy;
    int targetCurrentStrength = 0;
    int targetMaximumStrength = 0;
};

struct RuntimeMagBeamHostUpdate {
    int bindingId = 0;
    RuntimeMagBeamHostUpdateKind kind =
        RuntimeMagBeamHostUpdateKind::LiftStarted;
    RuntimeMagBeamKinematics kinematics;
    RuntimeMagBeamReleaseReason releaseReason =
        RuntimeMagBeamReleaseReason::None;
    RuntimeMagBeamImpactSnapshot impact;
};

enum class RuntimeMagBeamActionKind : std::uint8_t {
    Acquired = 0,
    ApplyLocomotor,
    LiftStarted,
    Carried,
    BeginFall,
    ResolveLanding,
    RestoreLocomotor,
    Released,
    Cancelled
};

struct RuntimeMagBeamAction {
    RuntimeMagBeamActionKind kind = RuntimeMagBeamActionKind::Acquired;
    int tick = -1;
    int bindingId = 0;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    std::string sourceOwner;
    std::string targetOwner;
    std::string weaponId;
    std::string warheadId;
    std::string locomotorId;
    std::string originalLocomotorId;
    bool originalLocomotorKnown = false;
    bool magBeamVisual = false;
    int rawDamage = 0;
    double targetWeight = 0.0;
    RuntimeMagBeamPhase phase = RuntimeMagBeamPhase::Acquired;
    RuntimeMagBeamReleaseReason releaseReason =
        RuntimeMagBeamReleaseReason::None;
    RuntimeMagBeamKinematics kinematics;
    bool hasImpact = false;
    RuntimeMagBeamImpactSnapshot impact;
    double fallingDamageMultiplier = 1.0;
    bool currentStrengthDamage = true;
};

struct RuntimeMagBeamAcquireRequest {
    int currentTick = -1;
    int recursionDepth = 0;
    // An explicit, host-authorized force attack may acquire friendly units.
    // This does not bypass physical eligibility, immunity, or ownership identity.
    bool forceFire = false;
    RuntimeMagBeamSourceSnapshot source;
    RuntimeMagBeamTargetSnapshot target;
    RuntimeMagBeamWeaponSnapshot weapon;
    RuntimeMagBeamRulesSnapshot rules;
};

struct RuntimeMagBeamAcquireResult {
    bool accepted = false;
    bool idempotent = false;
    RuntimeMagBeamVeto veto = RuntimeMagBeamVeto::None;
    int bindingId = 0;
    std::vector<RuntimeMagBeamAction> actions;
};

struct RuntimeMagBeamReleaseRequest {
    int bindingId = 0;
    RuntimeMagBeamReleaseReason reason =
        RuntimeMagBeamReleaseReason::Explicit;
    RuntimeMagBeamKinematics kinematics;
};

struct RuntimeMagBeamAdvanceInput {
    int currentTick = -1;
    bool validateSources = true;
    bool validateTargets = true;
    std::vector<RuntimeMagBeamSourceSnapshot> sources;
    std::vector<RuntimeMagBeamTargetSnapshot> targets;
    std::vector<RuntimeMagBeamReleaseRequest> releases;
    std::vector<RuntimeMagBeamHostUpdate> hostUpdates;
};

struct RuntimeMagBeamAdvanceResult {
    bool advanced = false;
    bool inputRejected = false;
    bool actionLimitReached = false;
    std::vector<RuntimeMagBeamAction> actions;
};

struct RuntimeMagBeamBindingState {
    int bindingId = 0;
    int sourceObjectId = -1;
    std::string sourceOwner;
    std::string sourceTypeId;
    int targetObjectId = -1;
    std::string targetOwner;
    std::string targetTypeId;
    RuntimeMagBeamTargetKind targetKind = RuntimeMagBeamTargetKind::Other;
    double targetWeight = 0.0;
    int targetCurrentStrength = 0;
    int targetMaximumStrength = 0;
    int targetSuppressionThreshold = 0;
    RuntimeMagBeamKinematics kinematics;
    std::string weaponId;
    std::string warheadId;
    std::string locomotorId;
    std::string originalLocomotorId;
    bool originalLocomotorKnown = false;
    bool magBeamVisual = false;
    int rawDamage = 0;
    double fallingDamageMultiplier = 1.0;
    bool currentStrengthDamage = true;
    RuntimeMagBeamPhase phase = RuntimeMagBeamPhase::Acquired;
    RuntimeMagBeamReleaseReason releaseReason =
        RuntimeMagBeamReleaseReason::None;
    int acquiredTick = -1;
    int phaseChangedTick = -1;
};

struct RuntimeMagBeamReconcileInput {
    int currentTick = -1;
    bool validateSources = true;
    bool validateTargets = true;
    std::vector<RuntimeMagBeamSourceSnapshot> sources;
    std::vector<RuntimeMagBeamTargetSnapshot> targets;
};

struct RuntimeMagBeamReconcileReport {
    bool inputRejected = false;
    bool actionLimitReached = false;
    std::size_t retained = 0;
    std::size_t released = 0;
    std::size_t cancelled = 0;
    std::vector<RuntimeMagBeamAction> actions;
};

class RuntimeMagBeamSystemState {
public:
    RuntimeMagBeamSystemState();
    explicit RuntimeMagBeamSystemState(RuntimeMagBeamLimits limits);

    void reset(RuntimeMagBeamLimits limits = {});

    RuntimeMagBeamAcquireResult acquire(
        const RuntimeMagBeamAcquireRequest& request
    );
    RuntimeMagBeamAdvanceResult advance(
        const RuntimeMagBeamAdvanceInput& input
    );
    RuntimeMagBeamReconcileReport reconcile(
        const RuntimeMagBeamReconcileInput& input
    );

    void save(std::ostream& output) const;
    bool load(const IniFile& ini, std::string* errorMessage = nullptr);
    std::uint64_t stateHash() const;

    const RuntimeMagBeamBindingState* findBinding(int bindingId) const;
    const RuntimeMagBeamLimits& limits() const;
    const std::vector<RuntimeMagBeamBindingState>& bindings() const;
    int lastAdvancedTick() const;
    int nextBindingId() const;

private:
    RuntimeMagBeamLimits limits_;
    int lastAdvancedTick_ = -1;
    int nextBindingId_ = 1;
    std::vector<RuntimeMagBeamBindingState> bindings_;

    void sortState();
};

const char* runtimeMagBeamVetoName(RuntimeMagBeamVeto veto);
const char* runtimeMagBeamPhaseName(RuntimeMagBeamPhase phase);
const char* runtimeMagBeamActionName(RuntimeMagBeamActionKind kind);
const char* runtimeMagBeamReleaseReasonName(
    RuntimeMagBeamReleaseReason reason
);

