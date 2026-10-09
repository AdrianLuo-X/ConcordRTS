#pragma once

#include "GameWorld.h"
#include "IniFile.h"
#include "RulesDatabase.h"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

enum class SuperWeaponRuntimeState {
    Unavailable,
    Charging,
    Suspended,
    Ready,
    SelectingTarget,
    AwaitingSecondTarget,
    Launching,
    Active,
    Cooldown
};

enum class SuperWeaponTargetMode {
    None,
    Cell,
    Area,
    WholeMap,
    TwoStage
};

struct SuperWeaponCell {
    int x = -1;
    int y = -1;
};

bool operator==(const SuperWeaponCell& left, const SuperWeaponCell& right);
bool operator!=(const SuperWeaponCell& left, const SuperWeaponCell& right);

struct SuperWeaponTarget {
    SuperWeaponCell cell;
    int objectId = -1;
};

struct SuperWeaponEvent {
    int tick = 0;
    int instanceId = -1;
    // Stable for one active lifecycle. Unlike instanceId, this changes when
    // the same sidebar instance is fired again.
    int activeToken = -1;
    int sourceBuildingId = -1;
    // Presentation source, independent of the created/affected object IDs.
    // For a chute drop this is the plane, rather than the new infantry.
    int soundSourceObjectId = -1;
    int objectId = -1;
    int targetObjectId = -1;
    int value = 0;
    std::string owner;
    std::string superWeaponId;
    std::string kind;
    std::string state;
    std::string reason;
    std::string commandVerb;
    std::string soundIntent;
    std::string evaIntent;
    std::string animationIntent;
    SuperWeaponCell targetCell;
    SuperWeaponCell targetCell2;
};

enum class SuperWeaponObjectLifecycleKind {
    Damaged,
    Removed,
    Relocated,
    OwnerChanged
};

enum class SuperWeaponObjectRemovalDisposition {
    None,
    Destroyed,
    TransformSource,
    Consumed
};

// A value-only description of an object mutation performed by a super
// weapon.  The host may safely retain it for replay/attribution work without
// keeping a pointer into the world object vector.
struct SuperWeaponObjectLifecycleEvent {
    SuperWeaponObjectLifecycleKind kind =
        SuperWeaponObjectLifecycleKind::Damaged;
    SuperWeaponObjectRemovalDisposition removalDisposition =
        SuperWeaponObjectRemovalDisposition::None;
    int tick = 0;
    int objectId = -1;
    int healthBefore = 0;
    int healthAfter = 0;
    int superWeaponInstanceId = -1;
    std::string sourceOwner;
    std::string superWeaponId;
    std::string warheadId;
    std::string reason;
    SuperWeaponCell oldCell;
    SuperWeaponCell newCell;
    std::string oldOwner;
    std::string newOwner;
};

// A value-only description of one world-space super-weapon impact. The host
// receives one event per blast/bolt, independently of how many objects are
// damaged by that impact.
struct SuperWeaponImpactEvent {
    int tick = 0;
    int instanceId = -1;
    int activeToken = -1;
    std::string kind;
    std::string superWeaponId;
    std::string superWeaponType;
    std::string sourceOwner;
    int sourceObjectId = -1;
    SuperWeaponCell cell;
    int radius = 0;
    int damage = 0;
    std::string warheadId;
    std::string animationId;
    std::string secondaryAnimationId;
};

enum class SuperWeaponActiveLifecyclePhase {
    Started,
    Ended
};

// Describes a duration-bearing activation without retaining any world or
// rule pointers. activeToken distinguishes overlapping activations and
// repeated firings of the same sidebar instance.
struct SuperWeaponActiveLifecycleEvent {
    SuperWeaponActiveLifecyclePhase phase =
        SuperWeaponActiveLifecyclePhase::Started;
    int tick = 0;
    int instanceId = -1;
    int activeToken = -1;
    std::string superWeaponId;
    std::string superWeaponType;
    std::string sourceOwner;
    int sourceObjectId = -1;
    SuperWeaponCell cell;
    int startedTick = -1;
    int endsTick = -1;
    int durationTicks = 0;
};

// Current duration-bearing activations, copied out by value so a host can
// rebuild presentation-only state (for example lighting profile tokens)
// after load without retaining pointers into this system.
struct SuperWeaponActiveLifecycleSnapshot {
    int instanceId = -1;
    int activeToken = -1;
    std::string superWeaponId;
    std::string superWeaponType;
    std::string sourceOwner;
    int sourceObjectId = -1;
    SuperWeaponCell cell;
    int startedTick = -1;
    int activeUntilTick = -1;
};

struct SuperWeaponRenderSnapshot {
    int instanceId = -1;
    int tick = 0;
    std::string owner;
    std::string superWeaponId;
    std::string kind;
    std::string phase;
    std::string resourceId;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    SuperWeaponCell cell;
    SuperWeaponCell cell2;
    int radius = 0;
    int sourceLevel = 0;
    int intensity = 0;
    int progress = 0;
    int requiredProgress = 0;
    int expireTick = -1;
    bool visibleToAll = true;
};

struct SuperWeaponSidebarSnapshot {
    int instanceId = -1;
    std::string owner;
    std::string superWeaponId;
    std::string uiName;
    std::string name;
    std::string sidebarImage;
    SuperWeaponRuntimeState state = SuperWeaponRuntimeState::Unavailable;
    double progress = 0.0;
    int remainingTicks = 0;
    int rechargeTicks = 0;
    int stateStartTick = 0;
    int launchTick = -1;
    int flashSidebarTabFrames = -1;
    double targetRange = 0.0;
    int targetLineMultiplier = 0;
    bool showTimer = false;
    bool selected = false;
    bool targetSelectionActive = false;
    bool firstTargetAccepted = false;
    SuperWeaponCell firstTargetCell;
    // Effect lifecycle stays in state; the independent recharge can pause.
    bool chargePaused = false;
};

struct SuperWeaponWorldContext {
    int tick = 0;
    int fixedTickHz = 30;
    int mapWidth = 0;
    int mapHeight = 0;
    bool superWeaponsAllowed = true;
    std::vector<WorldObjectInstance>* objects = nullptr;
    const RulesDatabase* rules = nullptr;

    // These callbacks keep map ownership, terrain, shroud, and power policy
    // in the host runtime while the state machine remains deterministic.
    std::function<bool(const std::string&, const std::string&)> areAllied;
    std::function<bool(int, int, SuperWeaponTargetMode)> cellLegal;
    std::function<bool(int, int)> cellOccupied;
    std::function<bool(const std::string&, int, int)> explored;
    std::function<bool(const std::string&)> ownerPowered;
    // Host-owned match option; a missing callback preserves authored timers.
    std::function<bool(const std::string&)> noCooldown;
    std::function<bool(const WorldObjectInstance&)> damageImmune;
    std::function<std::string(const std::string&)> ownerCountry;
    std::function<bool(const WorldObjectInstance&, int, int)> canPlaceObject;
    // A host-confirmed, unobstructed deep-water arrival that destroys this
    // unit. Terrain/domain and occupancy remain host policy; missing callback
    // preserves the existing blocked-destination behavior.
    std::function<bool(const WorldObjectInstance&, int, int)> chronoDestinationIsFatalWater;
    // Select a legal infantry entry subcell near the original drop search
    // point. The returned slot supplies the sole rendered quarter-cell offset.
    std::function<int(const WorldObjectInstance&, int, int, double, double)> paratrooperSubcell;
    std::function<int(int, int)> terrainLevel;
    // The host knows the art foundation's launch anchor. Latch it once so
    // a sold/destroyed silo cannot relocate an already airborne missile.
    std::function<SuperWeaponCell(const WorldObjectInstance&)> nuclearLaunchCell;
    std::function<int(int, int)> facingForDelta;
    // Returns the duration of one authored animation in simulation ticks.
    // Zero means metadata is unavailable; the engine uses a rule fallback.
    std::function<int(const std::string&)> animationDurationTicks;
    std::function<int(const std::string&)> animationMidpointTicks;
    // AI target acquisition may ignore ordinary fog-of-war while still
    // respecting host-defined stealth (cloak, camouflage, transit/docking).
    std::function<bool(
        const std::string&,
        const WorldObjectInstance&
    )> aiTargetVisible;
    // Optional host veto after the stock Genetic Mutator checks (Infantry,
    // Organic, !NotHuman, !ImmuneToPsionics, grounded) have passed.
    std::function<bool(const WorldObjectInstance&)> geneticMutatorEligible;
    std::function<int()> allocateObjectId;
    std::function<void(WorldObjectInstance&)> onObjectCreated;
    // Keep host-owned lifecycle state (selection, behaviors, cloak,
    // relationships, occupancy, and replay attribution) synchronized with
    // direct mutations performed by this standalone state machine.
    std::function<void(SuperWeaponObjectLifecycleEvent)> onObjectDamaged;
    std::function<void(SuperWeaponObjectLifecycleEvent)> onObjectRemoved;
    std::function<void(SuperWeaponObjectLifecycleEvent)> onObjectRelocated;
    std::function<void(SuperWeaponObjectLifecycleEvent)> onObjectOwnerChanged;
    std::function<void(SuperWeaponImpactEvent)> onSuperWeaponImpact;
    std::function<void(SuperWeaponActiveLifecycleEvent)>
        onSuperWeaponActiveLifecycle;
    std::function<void(const SuperWeaponEvent&)> emit;
    // Mutation creates a full-health replacement. The host may apply its
    // owner-specific maximum; standalone users fall back to typed Strength.
    std::function<int(const WorldObjectInstance&)> geneticMutationMaxHealth;
};

class SuperWeaponSystem {
public:
    SuperWeaponSystem() = default;

    void swap(SuperWeaponSystem& other) noexcept;

    void reset(const RulesDatabase& rules);
    void reset(const std::vector<SuperWeaponRule>& rules);

    // Rebuilds source ownership from live BuildingType.SuperWeapon and
    // SuperWeapon2 references.  One owner+definition has one shared charge;
    // multiple live sources are recorded for failover but do not multiply the
    // recharge rate.  This is an explicit compatibility assumption until a
    // mod supplies a different policy.
    void reconcileSources(const SuperWeaponWorldContext& context);
    void tick(const SuperWeaponWorldContext& context);

    bool beginTargetSelection(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponWorldContext& context,
        std::string* reason = nullptr
    );
    bool cancelTargetSelection(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponWorldContext& context,
        const std::string& reason = "cancelled"
    );
    bool submitTarget(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponTarget& target,
        const SuperWeaponWorldContext& context,
        std::string* reason = nullptr
    );
    bool submitTwoStageTarget(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponTarget& first,
        const SuperWeaponTarget& second,
        const SuperWeaponWorldContext& context,
        std::string* reason = nullptr
    );

    bool grant(
        const std::string& owner,
        const std::string& superWeaponId,
        int sourceBuildingId = -1,
        const SuperWeaponWorldContext* context = nullptr
    );
    // Grants a virtual source that is consumed after its first completed
    // launch. Dead concrete sources are pruned when the supplied context can
    // verify them. A surviving concrete or existing virtual source retains
    // its current lifetime policy and counts as a successful no-op.
    bool grantOneShot(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponWorldContext* context = nullptr
    );
    bool forceReady(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponWorldContext* context = nullptr
    );
    bool resetCharge(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponWorldContext* context = nullptr
    );
    bool setChargePercent(
        const std::string& owner,
        const std::string& superWeaponId,
        int percent,
        const SuperWeaponWorldContext* context = nullptr
    );
    bool setRechargeTicks(
        const std::string& owner,
        const std::string& superWeaponId,
        int ticks,
        const SuperWeaponWorldContext* context = nullptr
    );
    bool disable(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponWorldContext* context = nullptr
    );
    bool remove(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponWorldContext* context = nullptr
    );
    bool applyTriggerAction(
        const std::string& owner,
        const std::string& superWeaponId,
        const std::string& action,
        const SuperWeaponTarget& target,
        const SuperWeaponWorldContext& context,
        std::string* reason = nullptr
    );
    bool fireAi(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponWorldContext& context,
        std::string* reason = nullptr
    );
    bool fireAiDefense(
        const std::string& defenderOwner,
        const SuperWeaponCell& baseCenter,
        int probabilityPercent,
        int alertFrames,
        int distanceCells,
        const SuperWeaponWorldContext& context,
        std::string* reason = nullptr
    );

    bool previewTarget(
        const std::string& owner,
        const std::string& superWeaponId,
        const SuperWeaponTarget& target,
        const SuperWeaponWorldContext& context,
        std::string* reason = nullptr
    ) const;

    const std::vector<SuperWeaponSidebarSnapshot>& sidebarSnapshots(
        const std::string& owner = ""
    ) const;
    const std::vector<SuperWeaponRenderSnapshot>& renderSnapshots() const;
    std::vector<SuperWeaponActiveLifecycleSnapshot>
        activeLifecycleSnapshots() const;
    const SuperWeaponSidebarSnapshot* findSidebarSnapshot(
        const std::string& owner,
        const std::string& superWeaponId
    ) const;
    SuperWeaponRuntimeState stateOf(
        const std::string& owner,
        const std::string& superWeaponId
    ) const;
    // Building animation follows the latest accepted cast independently of
    // the sidebar's readiness when the host enables no cooldown.
    int activeLaunchTick(const std::string& owner, const std::string& superWeaponId) const;
    bool isOwnerBlackout(const std::string& owner, int tick) const;
    bool isEnemyStormActive(const std::string& owner, int tick,
        const std::function<bool(const std::string&, const std::string&)>& areAllied) const;
    bool isObjectProtected(int objectId, int tick) const;
    std::uint64_t stateHash() const;
    int lastTick() const;
    bool alignEmptyStateTick(int tick);

    void save(std::ostream& output) const;
    bool load(const IniFile& ini, std::string* errorMessage = nullptr);

    // Standalone deterministic coverage used by CTest.  It does not depend
    // on Win32 rendering or a mounted resource directory.
    static int runSmokeTest();
    static int runOneShotSmokeTest();
    static std::string stateName(SuperWeaponRuntimeState state);

private:
    struct StormCloud {
        SuperWeaponCell cell;
        int untilTick = -1;
        int impactTick = -1; // -1 also migrates clouds whose old payload already fired.
        int boltIndex = 0;
        bool direct = false;
    };
    struct Instance {
        int instanceId = -1;
        std::string owner;
        std::string definitionId;
        std::vector<int> sourceBuildingIds;
        SuperWeaponRuntimeState state = SuperWeaponRuntimeState::Unavailable;
        SuperWeaponTargetMode targetMode = SuperWeaponTargetMode::Cell;
        bool disabled = false;
        bool powered = true;
        bool oneShot = false;
        // A committed cast keeps its identity and payload after its reusable
        // grant becomes a separate ready instance. It has no sidebar entry.
        bool effectOnly = false;
        int chargeTicks = 0;
        int rechargeTicks = 1;
        int stateStartTick = 0;
        int launchTick = -1;
        int activeToken = -1;
        int activeStartedTick = -1;
        int activeUntilTick = -1;
        int cooldownUntilTick = -1;
        int nextEffectTick = -1;
        int nextImpactTick = -1;
        int targetStage = 0;
        bool effectApplied = false;
        SuperWeaponTarget firstTarget;
        SuperWeaponTarget secondTarget;
        SuperWeaponTarget nuclearLaunchOrigin;
        int nuclearLaunchLevel = 0;
        std::uint32_t rngState = 0x9e3779b9U;
        std::vector<int> activeObjectIds;
        std::vector<StormCloud> stormClouds;
    };

    struct ProtectionEffect {
        int objectId = -1;
        int untilTick = -1;
        bool forceShield = false;
    };

    struct RadiationEffect {
        std::string owner;
        int instanceId = -1;
        SuperWeaponCell cell;
        int radius = 0;
        int intensity = 0;
        int untilTick = -1;
        int nextDamageTick = -1;
    };

    struct OwnerLockout {
        int blackoutUntil = -1;
        int penaltyUntil = -1;
        int penalty = 0;
    };

    std::vector<SuperWeaponRule> rules_;
    std::unordered_map<std::string, std::size_t> ruleIndexByKey_;
    std::vector<Instance> instances_;
    std::vector<ProtectionEffect> protections_;
    std::vector<RadiationEffect> radiation_;
    std::unordered_map<std::string, OwnerLockout> ownerLockouts_;
    std::vector<SuperWeaponSidebarSnapshot> sidebarSnapshots_;
    std::vector<SuperWeaponRenderSnapshot> renderSnapshots_;
    int nextInstanceId_ = 1;
    int nextActiveToken_ = 1;
    int lastTick_ = 0;

    static std::string keyFor(
        const std::string& owner,
        const std::string& superWeaponId
    );
    static std::string lowerAscii(std::string value);
    static SuperWeaponRuntimeState stateFromName(const std::string& value);
    static SuperWeaponTargetMode targetModeFromName(const std::string& value);
    static std::string targetModeName(SuperWeaponTargetMode mode);
    static std::uint64_t hashString(
        std::uint64_t hash,
        const std::string& value
    );
    static SuperWeaponObjectLifecycleEvent lifecycleEvent(
        SuperWeaponObjectLifecycleKind kind,
        const Instance& instance,
        int tick,
        int objectId
    );
    static int convertRuleFrames(int frames, int fixedTickHz);
    static int cellDistance(const SuperWeaponCell& left, const SuperWeaponCell& right);

    const SuperWeaponRule* findRule(const std::string& id) const;
    SuperWeaponRule* findRule(const std::string& id);
    const SuperWeaponRule* findPostClickHelper(
        const SuperWeaponRule& primary
    ) const;
    const SuperWeaponRule* targetRuleForInstance(
        const Instance& instance,
        bool secondStage
    ) const;
    Instance* findInstance(
        const std::string& owner,
        const std::string& superWeaponId
    );
    const Instance* findInstance(
        const std::string& owner,
        const std::string& superWeaponId
    ) const;
    Instance* findInstanceById(int instanceId);
    const Instance* findInstanceById(int instanceId) const;
    Instance& ensureInstance(
        const std::string& owner,
        const std::string& superWeaponId
    );
    void emit(
        const SuperWeaponWorldContext& context,
        const Instance& instance,
        const std::string& kind,
        const std::string& reason = "",
        const SuperWeaponTarget* target = nullptr,
        const SuperWeaponTarget* target2 = nullptr,
        int objectId = -1,
        int targetObjectId = -1,
        int value = 0,
        const std::string& soundIntent = "",
        const std::string& evaIntent = "",
        const std::string& animationIntent = "",
        int soundSourceObjectId = -1
    ) const;
    void emitImpact(
        const SuperWeaponWorldContext& context,
        const Instance& instance,
        const std::string& kind,
        const SuperWeaponCell& cell,
        int radius,
        int damage,
        const std::string& warheadId,
        const std::string& animationId,
        const std::string& secondaryAnimationId = ""
    ) const;
    void beginActiveLifecycle(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void endActiveLifecycle(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void cancelActiveLifecycle(
        Instance& instance,
        const SuperWeaponWorldContext* context
    );
    bool instanceHasLiveSource(
        const Instance& instance,
        const SuperWeaponWorldContext& context,
        bool* powered
    ) const;
    bool validateTarget(
        const Instance& instance,
        const SuperWeaponTarget& target,
        const SuperWeaponWorldContext& context,
        std::string* reason,
        const SuperWeaponRule* targetRule = nullptr
    ) const;
    bool targetIsAllowed(
        const Instance& instance,
        const WorldObjectInstance& object,
        const SuperWeaponWorldContext& context,
        const SuperWeaponRule* targetRule = nullptr
    ) const;
    const WorldObjectInstance* findObject(
        int objectId,
        const SuperWeaponWorldContext& context
    ) const;
    WorldObjectInstance* findObject(
        int objectId,
        const SuperWeaponWorldContext& context
    );
    void advanceInstance(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void detachNoCooldownCast(Instance& instance, const SuperWeaponWorldContext& context);
    void launchInstance(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void applyNuke(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void advanceStorm(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void applyProtection(
        Instance& instance,
        const SuperWeaponWorldContext& context,
        bool forceShield
    );
    void applyChrono(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void applyDominator(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void applyMutator(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void launchParadrop(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void launchSupportAircraft(Instance& instance, const SuperWeaponWorldContext& context, bool spy);
    void advanceSupportAircraft(Instance& instance, const SuperWeaponWorldContext& context, bool spy);
    std::vector<std::string> supportInfantryManifest(const Instance& instance,
        const SuperWeaponWorldContext& context) const;
    void launchReveal(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void applyRadialDamage(
        const Instance& instance,
        const SuperWeaponWorldContext& context,
        const SuperWeaponCell& cell,
        int radius,
        int damage,
        const std::string& warheadId,
        const std::string& eventKind
    );
    bool ownerAllied(
        const std::string& left,
        const std::string& right,
        const SuperWeaponWorldContext& context
    ) const;
    bool objectImmune(
        const WorldObjectInstance& object,
        const std::string& mechanism,
        const SuperWeaponWorldContext& context
    ) const;
    void finishCooldown(
        Instance& instance,
        const SuperWeaponWorldContext& context
    );
    void rebuildSnapshots(int tick);
    void cleanupEffects(int tick);
    void advanceRadiationEffects(const SuperWeaponWorldContext& context);
    void emitEffectSnapshots(int tick);
    void ensureOwnerSnapshotOrder();
};

void swap(SuperWeaponSystem& left, SuperWeaponSystem& right) noexcept;
