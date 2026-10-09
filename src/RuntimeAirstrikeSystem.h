#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

class IniFile;

struct RuntimeAirstrikeCell {
    int x = -1;
    int y = -1;
};

struct RuntimeAirstrikeBounds {
    int minX = 0;
    int minY = 0;
    int maxX = -1;
    int maxY = -1;
};

struct RuntimeAirstrikeLimits {
    std::size_t maxCallers = 1024;
    std::size_t maxActiveStrikes = 128;
    std::size_t maxPlanesPerStrike = 32;
    std::size_t maxTotalPlanes = 512;
    std::size_t maxActionsPerAdvance = 2048;
    std::size_t maxTextLength = 128;
    int maxRecursionDepth = 0;
    int spawnDelayTicks = 1;
    int launchSpacingTicks = 2;
    int ingressTicks = 20;
    int egressTicks = 20;
    int routeMarginCells = 6;
    std::size_t maxProjectileReceipts = 4096;
    int terminalReceiptRetentionTicks = 600;
};

// These values map directly to UnitType::AirstrikeTeam* fields.  The word
// "Team" in the stock rules means a count of one AircraftTypes ID; it is not
// a map [TeamTypes] template.
struct RuntimeAirstrikeCallerRules {
    int normalAircraftCount = 0;
    int eliteAircraftCount = 0;
    std::string normalAircraftTypeId;
    std::string eliteAircraftTypeId;
    int normalRechargeTicks = 0;
    int eliteRechargeTicks = 0;
};

// The host resolves the selected AircraftTypes ID and its weapon before
// entering this value-only core.  No world object or rules pointer is kept.
struct RuntimeAirstrikeAircraftDefinition {
    std::string typeId;
    bool exists = false;
    bool isAircraft = false;
    bool hasAttackWeapon = false;
    std::string weaponId;
    std::string warheadId;
};

struct RuntimeAirstrikeTargetSnapshot {
    int objectId = -1;
    RuntimeAirstrikeCell cell;
    bool alive = false;
    bool visibleToCaller = false;
    bool targetable = false;
};

struct RuntimeAirstrikeRoute {
    RuntimeAirstrikeCell entry;
    RuntimeAirstrikeCell target;
    RuntimeAirstrikeCell exit;
};

enum class RuntimeAirstrikeVeto : std::uint8_t {
    None = 0,
    InvalidTick,
    InvalidCaller,
    InvalidOwner,
    RecursiveRequest,
    CallerBusy,
    CooldownActive,
    InvalidTarget,
    InvalidBounds,
    InvalidAircraftCount,
    InvalidAircraftType,
    IllegalAircraftDefinition,
    CallerLimitReached,
    StrikeLimitReached,
    PlaneLimitReached,
    IdExhausted,
    ProjectileReceiptLimitReached
};

enum class RuntimeAirstrikeCancelReason : std::uint8_t {
    None = 0,
    TargetMissing,
    TargetDead,
    TargetHidden,
    TargetIllegal,
    CallerMissing,
    AircraftInvalid,
    Reconciled
};

enum class RuntimeAirstrikePlanePhase : std::uint8_t {
    Pending = 0,
    Ingress,
    Egress,
    Terminated,
    Destroyed
};

enum class RuntimeAirstrikeActionKind : std::uint8_t {
    TargetMarked = 0,
    SpawnAircraft,
    AssignIngressRoute,
    DropBomb,
    BeginEgress,
    DespawnAircraft,
    CancelStrike,
    StartCallerCooldown
};

struct RuntimeAirstrikeAction {
    RuntimeAirstrikeActionKind kind =
        RuntimeAirstrikeActionKind::TargetMarked;
    int tick = 0;
    int strikeId = 0;
    int callerObjectId = -1;
    int targetObjectId = -1;
    int planeToken = 0;
    int planeSlot = -1;
    int aircraftCount = 0;
    int readyTick = 0;
    bool elite = false;
    std::string owner;
    std::string aircraftTypeId;
    std::string weaponId;
    std::string warheadId;
    RuntimeAirstrikeRoute route;
    RuntimeAirstrikeCancelReason cancelReason =
        RuntimeAirstrikeCancelReason::None;
};

struct RuntimeAirstrikeRequest {
    int callerObjectId = -1;
    std::string callerOwner;
    int targetObjectId = -1;
    int currentTick = -1;
    bool elite = false;
    // Explicit attack intent survives Stop, aircraft flight and saved games.
    bool forceFire = false;
    RuntimeAirstrikeCallerRules rules;
    RuntimeAirstrikeAircraftDefinition aircraft;
    RuntimeAirstrikeTargetSnapshot target;
    RuntimeAirstrikeBounds bounds;

    // Optional host movement rate, Q16 fixed ticks per map cell. Zero keeps
    // the value-core's explicit ingress/egress durations for tools and saves.
    std::int64_t flightTicksPerCellQ16 = 0;

    // Managed strike aircraft may not recursively request another strike.
    int sourceStrikeId = 0;
    int sourcePlaneToken = 0;
    int recursionDepth = 0;
};

struct RuntimeAirstrikeRequestResult {
    bool accepted = false;
    RuntimeAirstrikeVeto veto = RuntimeAirstrikeVeto::None;
    int strikeId = 0;
    int readyTick = 0;
    std::vector<RuntimeAirstrikeAction> actions;
};

struct RuntimeAirstrikeAdvanceInput {
    int currentTick = -1;
    std::vector<RuntimeAirstrikeTargetSnapshot> targets;
    std::vector<int> destroyedPlaneTokens;
};

struct RuntimeAirstrikeAdvanceResult {
    bool advanced = false;
    bool inputRejected = false;
    bool actionLimitReached = false;
    std::vector<RuntimeAirstrikeAction> actions;
};

struct RuntimeAirstrikeCallerState {
    int callerObjectId = -1;
    int readyTick = 0;
    int activeStrikeId = 0;
};

struct RuntimeAirstrikePlaneState {
    int planeToken = 0;
    int slot = -1;
    RuntimeAirstrikePlanePhase phase = RuntimeAirstrikePlanePhase::Pending;
    int spawnTick = 0;
    int bombTick = 0;
    int despawnTick = 0;
    RuntimeAirstrikeRoute route;
};

struct RuntimeAirstrikeStrikeState {
    int strikeId = 0;
    int callerObjectId = -1;
    std::string owner;
    int targetObjectId = -1;
    RuntimeAirstrikeCell targetCell;
    bool elite = false;
    // Explicit attack intent survives Stop, aircraft flight and saved games.
    bool forceFire = false;
    std::string aircraftTypeId;
    std::string weaponId;
    std::string warheadId;
    int rechargeTicks = 0;
    int requestedTick = 0;
    int terminalTick = -1;
    bool everSpawned = false;
    bool cancelled = false;
    bool cancelActionEmitted = false;
    bool cooldownRequired = false;
    RuntimeAirstrikeCancelReason cancelReason =
        RuntimeAirstrikeCancelReason::None;
    std::vector<RuntimeAirstrikePlaneState> planes;
};

// A receipt is created in the same state transition that emits DropBomb.  It
// remains authoritative after its strike and plane records have been removed,
// so a separately saved root projectile can still prove its launch identity.
struct RuntimeAirstrikeProjectileIdentity {
    int strikeId = 0;
    int planeToken = 0;
};

struct RuntimeAirstrikeProjectileReceipt {
    int strikeId = 0;
    int planeToken = 0;
    int launchTick = -1;
    int terminalTick = -1;
};

enum class RuntimeAirstrikeProjectileReceiptStatus : std::uint8_t {
    NotFound = 0,
    Pending,
    Terminal,
    IdentityConflict
};

enum class RuntimeAirstrikeProjectileAckResult : std::uint8_t {
    Accepted = 0,
    AlreadyAcknowledged,
    NotFound,
    IdentityConflict,
    InvalidTick,
    InvalidIdentity
};

struct RuntimeAirstrikeReconcileInput {
    int currentTick = -1;
    bool validateCallers = false;
    bool validateTargets = false;
    bool validateAircraft = false;
    std::vector<int> liveCallerObjectIds;
    std::vector<RuntimeAirstrikeTargetSnapshot> targets;
    std::vector<RuntimeAirstrikeAircraftDefinition> aircraft;
    bool validateProjectileReceipts = false;
    std::vector<RuntimeAirstrikeProjectileIdentity> liveRootProjectiles;
};

struct RuntimeAirstrikeReconcileReport {
    bool inputRejected = false;
    bool actionLimitReached = false;
    std::size_t retainedCallers = 0;
    std::size_t removedCallers = 0;
    std::size_t retainedStrikes = 0;
    std::size_t cancelledStrikes = 0;
    std::vector<RuntimeAirstrikeAction> actions;
    std::size_t retainedProjectileReceipts = 0;
    std::size_t terminatedProjectileReceipts = 0;
    std::size_t expiredProjectileReceipts = 0;
};

class RuntimeAirstrikeSystemState {
public:
    RuntimeAirstrikeSystemState();
    explicit RuntimeAirstrikeSystemState(RuntimeAirstrikeLimits limits);

    void reset(RuntimeAirstrikeLimits limits = {});

    RuntimeAirstrikeRequestResult requestStrike(
        const RuntimeAirstrikeRequest& request
    );
    RuntimeAirstrikeAdvanceResult advance(
        const RuntimeAirstrikeAdvanceInput& input
    );
    RuntimeAirstrikeReconcileReport reconcile(
        const RuntimeAirstrikeReconcileInput& input
    );

    RuntimeAirstrikeProjectileReceiptStatus rootProjectileReceiptStatus(
        int strikeId,
        int planeToken
    ) const;
    const RuntimeAirstrikeProjectileReceipt* findRootProjectileReceipt(
        int strikeId,
        int planeToken
    ) const;
    RuntimeAirstrikeProjectileAckResult acknowledgeRootProjectile(
        int strikeId,
        int planeToken,
        int currentTick
    );

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        std::string* errorMessage = nullptr
    );
    std::uint64_t stateHash() const;

    bool callerReady(int callerObjectId, int currentTick) const;
    int callerReadyTick(int callerObjectId) const;
    const RuntimeAirstrikeStrikeState* findStrike(int strikeId) const;

    const RuntimeAirstrikeLimits& limits() const;
    const std::vector<RuntimeAirstrikeCallerState>& callers() const;
    const std::vector<RuntimeAirstrikeStrikeState>& strikes() const;
    const std::vector<RuntimeAirstrikeProjectileReceipt>&
    projectileReceipts() const;
    int lastAdvancedTick() const;
    int nextStrikeId() const;
    int nextPlaneToken() const;

private:
    RuntimeAirstrikeLimits limits_;
    int lastAdvancedTick_ = -1;
    int nextStrikeId_ = 1;
    int nextPlaneToken_ = 1;
    std::vector<RuntimeAirstrikeCallerState> callers_;
    std::vector<RuntimeAirstrikeStrikeState> strikes_;
    std::vector<RuntimeAirstrikeProjectileReceipt> projectileReceipts_;

    void sortState();
};

const char* runtimeAirstrikeVetoName(RuntimeAirstrikeVeto veto);
const char* runtimeAirstrikeActionName(RuntimeAirstrikeActionKind kind);
const char* runtimeAirstrikeCancelReasonName(
    RuntimeAirstrikeCancelReason reason
);
