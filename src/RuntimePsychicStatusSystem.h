#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

class IniFile;

enum class RuntimePsychicObjectKind : std::uint8_t {
    Infantry = 0,
    Vehicle,
    Structure,
    Aircraft,
    Other
};

enum class RuntimePsychicRelation : std::uint8_t {
    Self = 0,
    Allied,
    Enemy,
    Neutral,
    Unknown
};

// Complete value snapshots supplied by the host. The system never keeps a
// world pointer, callback, or borrowed string.
struct RuntimePsychicObjectSnapshot {
    int objectId = -1;
    std::string owner;
    std::string typeId;
    RuntimePsychicObjectKind kind = RuntimePsychicObjectKind::Other;
    bool present = false;
    bool alive = false;
    bool airborne = false;
    bool immuneToPsionics = false;
    // This object cannot be targeted by a berserk actor (retail CAOS).
    bool berserkFriendly = false;
    bool attackable = true;
};

struct RuntimePsychicWarheadSnapshot {
    std::string warheadId;
    bool warheadExists = false;
    bool psychedelic = false;
};

struct RuntimePsychicStatusLimits {
    std::size_t maxStatuses = 512;
    std::size_t maxHitEvents = 4096;
    std::size_t maxActionsPerOperation = 2048;
    std::size_t maxInputRecords = 4096;
    std::size_t maxTextLength = 128;
    int maxRecursionDepth = 0;
    int maxDurationTicks = 1000000;
};

enum class RuntimePsychicStatusVeto : std::uint8_t {
    None = 0,
    InvalidTick,
    InvalidEventId,
    InvalidSource,
    InvalidTarget,
    SelfTarget,
    IllegalTargetKind,
    TargetAirborne,
    TargetImmune,
    InvalidWarhead,
    InvalidDuration,
    RecursiveRequest,
    CapacityReached,
    IdExhausted,
    EventIdConflict,
    EventCapacityReached,
    ActionLimitReached
};

enum class RuntimePsychicStatusEndReason : std::uint8_t {
    None = 0,
    Expired,
    SourceMissing,
    SourceDead,
    SourceIdentityChanged,
    TargetMissing,
    TargetDead,
    TargetIdentityChanged,
    TargetBecameIneligible
};

enum class RuntimePsychicStatusActionKind : std::uint8_t {
    Applied = 0,
    Refreshed,
    Ended
};

struct RuntimePsychicStatusAction {
    RuntimePsychicStatusActionKind kind =
        RuntimePsychicStatusActionKind::Applied;
    RuntimePsychicStatusEndReason endReason =
        RuntimePsychicStatusEndReason::None;
    int tick = -1;
    std::uint64_t statusId = 0;
    std::uint64_t hitEventId = 0;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    std::string sourceOwner;
    std::string targetOwner;
    std::string warheadId;
    int startedTick = -1;
    int expiresTick = -1;
    // Serialized legacy metadata. New effects set this true; queryTargeting
    // ignores the old false value because berserk also targets original allies.
    bool attackOriginalAllies = false;
    // The projectile cached this attribution while the source was valid.
    // It remains attribution only; the status never borrows a world object.
    bool sourceValidatedAtLaunch = false;
};

struct RuntimePsychicStatusHitRequest {
    int currentTick = -1;
    int recursionDepth = 0;
    std::uint64_t hitEventId = 0;
    // The host derives this fixed-tick duration from the authoritative hit
    // rules/damage path. A refresh restarts (and never shortens) the window.
    int durationTicks = 0;
    // hitEventId is the projectile actor ID. Together with target.objectId it
    // forms the stable hit-event key supplied by the host.
    //
    // When true, source is a complete attribution snapshot captured while the
    // projectile source was valid. The impact may therefore be accepted even
    // if source is now absent or dead.
    bool sourceValidatedAtLaunch = false;
    RuntimePsychicObjectSnapshot source;
    RuntimePsychicObjectSnapshot target;
    RuntimePsychicWarheadSnapshot warhead;
};

struct RuntimePsychicStatusHitResult {
    bool accepted = false;
    bool idempotent = false;
    RuntimePsychicStatusVeto veto = RuntimePsychicStatusVeto::None;
    std::uint64_t statusId = 0;
    std::vector<RuntimePsychicStatusAction> actions;
};

struct RuntimePsychicStatusAdvanceInput {
    int currentTick = -1;
    // Source validation is diagnostic only after a status has been created.
    // Missing/dead sources do not cancel an already generated gas effect.
    bool validateSources = true;
    bool validateTargets = true;
    std::vector<RuntimePsychicObjectSnapshot> objects;
};

struct RuntimePsychicStatusAdvanceResult {
    bool advanced = false;
    bool inputRejected = false;
    bool actionLimitReached = false;
    std::size_t retained = 0;
    std::size_t ended = 0;
    std::vector<std::uint64_t> sourceIdentityChangedStatusIds;
    std::vector<RuntimePsychicStatusAction> actions;
};

struct RuntimePsychicStatusReconcileInput {
    int currentTick = -1;
    // Source validation is diagnostic only after a status has been created.
    bool validateSources = true;
    bool validateTargets = true;
    std::vector<RuntimePsychicObjectSnapshot> objects;
};

struct RuntimePsychicStatusReconcileReport {
    bool inputRejected = false;
    bool actionLimitReached = false;
    std::size_t retained = 0;
    std::size_t ended = 0;
    std::vector<std::uint64_t> sourceIdentityChangedStatusIds;
    std::vector<RuntimePsychicStatusAction> actions;
};

struct RuntimePsychicTargetingQuery {
    int currentTick = -1;
    RuntimePsychicObjectSnapshot actor;
    RuntimePsychicObjectSnapshot candidate;
    RuntimePsychicRelation originalRelation =
        RuntimePsychicRelation::Unknown;
    // The ordinary weapon/range/class filters remain authoritative. Berserk
    // only supplies an allegiance override; it cannot make an illegal target
    // otherwise attackable.
    bool hostTargetLegal = false;
};

struct RuntimePsychicTargetingResult {
    bool statusFound = false;
    bool berserkActive = false;
    bool overridesRelation = false;
    bool attackOriginalAllies = false;
    bool canAttack = false;
    RuntimePsychicRelation effectiveRelation =
        RuntimePsychicRelation::Unknown;
    std::uint64_t statusId = 0;
    int expiresTick = -1;
};

struct RuntimePsychicStatusRecord {
    std::uint64_t statusId = 0;
    int sourceObjectId = -1;
    std::string sourceOwner;
    std::string sourceTypeId;
    RuntimePsychicObjectKind sourceKind = RuntimePsychicObjectKind::Other;
    int targetObjectId = -1;
    std::string targetOwner;
    std::string targetTypeId;
    RuntimePsychicObjectKind targetKind = RuntimePsychicObjectKind::Other;
    std::string warheadId;
    // Retained verbatim for old save/hash compatibility, not a targeting gate.
    bool attackOriginalAllies = false;
    bool sourceValidatedAtLaunch = false;
    int startedTick = -1;
    int lastRefreshTick = -1;
    int expiresTick = -1;
    std::uint64_t lastHitEventId = 0;
};

// A bounded value-only event receipt. Receipts live only as long as their
// status and make replay/conflict handling deterministic across save/load.
struct RuntimePsychicStatusHitEventRecord {
    std::uint64_t projectileActorId = 0;
    int targetObjectId = -1;
    std::uint64_t statusId = 0;
    int acceptedTick = -1;
    int sourceObjectId = -1;
    std::string sourceOwner;
    std::string sourceTypeId;
    RuntimePsychicObjectKind sourceKind = RuntimePsychicObjectKind::Other;
    std::string targetOwner;
    std::string targetTypeId;
    RuntimePsychicObjectKind targetKind = RuntimePsychicObjectKind::Other;
    std::string warheadId;
};

class RuntimePsychicStatusSystemState {
public:
    RuntimePsychicStatusSystemState();
    explicit RuntimePsychicStatusSystemState(
        RuntimePsychicStatusLimits limits
    );

    void reset(RuntimePsychicStatusLimits limits = {});

    RuntimePsychicStatusHitResult applyHit(
        const RuntimePsychicStatusHitRequest& request
    );
    RuntimePsychicStatusAdvanceResult advance(
        const RuntimePsychicStatusAdvanceInput& input
    );
    RuntimePsychicStatusReconcileReport reconcile(
        const RuntimePsychicStatusReconcileInput& input
    );
    // Normal saved-world loader only. The caller supplies its safely parsed
    // complete object count (at most 100000); no stored/generic limit changes.
    RuntimePsychicStatusReconcileReport reconcileSavedWorld(
        const RuntimePsychicStatusReconcileInput& input,
        std::size_t validatedObjectCount
    );

    RuntimePsychicTargetingResult queryTargeting(
        const RuntimePsychicTargetingQuery& query
    ) const;

    void save(std::ostream& output) const;
    bool load(const IniFile& ini, std::string* errorMessage = nullptr);
    std::uint64_t stateHash() const;

    const RuntimePsychicStatusRecord* findByTarget(int targetObjectId) const;
    const RuntimePsychicStatusLimits& limits() const;
    const std::vector<RuntimePsychicStatusRecord>& statuses() const;
    const std::vector<RuntimePsychicStatusHitEventRecord>& hitEvents() const;
    int lastAdvancedTick() const;
    std::uint64_t nextStatusId() const;

private:
    RuntimePsychicStatusLimits limits_;
    int lastAdvancedTick_ = -1;
    std::uint64_t nextStatusId_ = 1;
    std::vector<RuntimePsychicStatusRecord> statuses_;
    std::vector<RuntimePsychicStatusHitEventRecord> hitEvents_;

    RuntimePsychicStatusReconcileReport reconcileWithInputRecordLimit(
        const RuntimePsychicStatusReconcileInput& input,
        std::size_t inputRecordLimit
    );
    void sortState();
};

const char* runtimePsychicStatusVetoName(RuntimePsychicStatusVeto veto);
const char* runtimePsychicStatusEndReasonName(
    RuntimePsychicStatusEndReason reason
);
const char* runtimePsychicStatusActionName(
    RuntimePsychicStatusActionKind kind
);

// Converts legacy 15 Hz authored frames into fixed simulation ticks using a
// ceiling division. Zero is a valid sentinel and converts to zero. Negative
// frames, non-positive rates and int overflow are rejected without changing
// outputTicks.
bool runtimePsychicLegacyFramesToFixedTicks(
    int authoredFrames,
    int fixedTickHz,
    int& outputTicks
);
