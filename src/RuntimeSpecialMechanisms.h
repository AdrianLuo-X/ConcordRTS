#pragma once

#include "GameWorld.h"
#include "IniFile.h"
#include "RulesDatabase.h"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

struct SpecialCell {
    int x = -1;
    int y = -1;
};

// The shell's existing terrain targets use -(base + y * 1000 + x).
// Only these bounded cell IDs, positive object IDs, and -1 (no target)
// are accepted by the carrier state machine.
bool decodeRuntimeCarrierCellTarget(int targetId, SpecialCell& cell, bool& bridge);

struct SpecialWorldPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SpecialMechanismEvent {
    std::string kind;
    int tick = 0;
    int objectId = -1;
    int targetObjectId = -1;
    int value = 0;
    SpecialCell cell;
    std::string reason;
};

struct MindControlLinkState {
    int controllerObjectId = -1;
    int targetObjectId = -1;
    // Kept separately from the live controller object so a permanent
    // conversion can remain deterministic after the controller leaves the
    // world.
    std::string controllerOwner;
    std::string originalOwner;
    bool permanent = false;
    int startedTick = 0;
};

struct CamouflageState {
    int objectId = -1;
    bool active = false;
    std::string disguiseType;
    int lastActivityTick = 0;
    int rearmUntilTick = 0;
    int fakeBlinkUntilTick = 0;
};

enum class ChronoPhase {
    None,
    WarpOut,
    InTransit,
    WarpIn,
    Cooldown
};

struct ChronoMoveState {
    int objectId = -1;
    ChronoPhase phase = ChronoPhase::None;
    SpecialCell source;
    SpecialCell destination;
    int sourceLevel = 0;
    int destinationLevel = 0;
    int startTick = 0;
    int completeTick = 0;
    int cooldownUntilTick = 0;
};

struct TemporalTargetState {
    int targetObjectId = -1;
    int requiredProgress = 0;
    bool frozen = false;
    std::map<int, int> progressByAttacker;
    int lastAdvancedTick = -1;
};

struct ElectricArcState {
    int arcId = -1;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    SpecialWorldPoint source;
    SpecialWorldPoint target;
    std::string color;
    bool alternateColor = false;
    bool assault = false;
    int startTick = 0;
    int expireTick = 0;
    // Saved presentation correction; never part of battle coordinates/hash.
    int sourceScreenOffsetX = 0;
    int sourceScreenOffsetY = 0;
    // Native sprite pixels stay fixed when the isometric tile scale changes.
    int sourceNativeFixedX = 0;
    int sourceNativeFixedY = 0;
};

struct ElectricDisableState {
    int objectId = -1;
    int untilTick = 0;
};

struct ElectricChargeState {
    int targetObjectId = -1;
    int sourceObjectId = -1;
    int startTick = 0;
    int expireTick = 0;
    int level = 1;
};

struct RadiationAreaState {
    int areaId = -1;
    int sourceObjectId = -1;
    std::string owner;
    SpecialCell center;
    int radius = 0;
    int strength = 0;
    int maximumStrength = 0;
    // Retained in old save records; site damage comes from strength and rules.
    int damageBase = 1;
    int nextApplicationTick = 0;
    int nextDecayTick = 0;
    int remainingTicks = 0;
    int damageInterval = 1;
    int decayAmount = 1;
    std::string warheadId;
    bool persistsAfterSourceDeath = true;
};

struct RadiationDamageEvent {
    int areaId = -1;
    int targetObjectId = -1;
    int damage = 0;
    int intensity = 0;
    // Carry the winning area's cause by value: the site can expire during
    // this very pulse, before the host handles a lethal damage event.
    int sourceObjectId = -1;
    std::string sourceOwner;
    std::string warheadId;
};

enum class TimedBombKind {
    C4,
    Ivan
};

struct TimedBombState {
    int targetObjectId = -1;
    int sourceObjectId = -1;
    std::string owner;
    TimedBombKind kind = TimedBombKind::Ivan;
    int attachedTick = 0;
    int detonateTick = 0;
    int damage = 0;
    std::string warheadId;
    // Stock C4 uses C4Warhead as the engine's absolute-damage marker and
    // destroys a building regardless of its current hit points.
    bool forceDestruction = false;
};

enum class ParasiteTargetKind {
    Infantry,
    Vehicle
};

enum class ParasiteImpactKind {
    Hit,
    Miss
};

enum class ParasiteHostEventKind {
    Healed,
    SonicHit,
    Teleporting,
    DestroyedExternally,
    DestroyedByParasite
};

// The host resolves rules and live-world facts before beginning a launch.
// Keeping the complete value snapshot here prevents an in-flight parasite
// from changing behaviour if the source, weapon, or rules disappear later.
struct ParasiteLaunchRequest {
    int projectileId = -1;
    int currentTick = -1;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    std::string sourceOwner;
    std::string sourceTypeId;
    std::string targetTypeId;
    std::string weaponId;
    std::string warheadId;
    ParasiteTargetKind targetKind = ParasiteTargetKind::Vehicle;
    SpecialCell targetCell;
    int damage = 0;
    int damageIntervalTicks = 1;
    int paralyzes = 0;
    int targetMaximumHealth = 1;
    bool targetParasiteable = false;
    bool sourceOrganic = false;
    bool sourceElite = false;
    bool culling = false;
    // Permyriad health ratios supplied from the authoritative rules snapshot.
    // Stock callers pass ConditionRed for normal and ConditionYellow for elite.
    int normalCullingThresholdPermyriad = 0;
    int eliteCullingThresholdPermyriad = 0;
};

struct ParasiteImpactRequest {
    int projectileId = -1;
    int currentTick = -1;
    ParasiteImpactKind impact = ParasiteImpactKind::Miss;
    int targetObjectId = -1;
    std::string targetTypeId;
    ParasiteTargetKind targetKind = ParasiteTargetKind::Vehicle;
    bool targetPresent = false;
    bool targetAlive = false;
    bool targetParasiteable = false;
    int targetMaximumHealth = 1;
    SpecialCell impactCell;
    bool returnCellAvailable = false;
    SpecialCell returnCell;
};

struct ParasiteHostEventRequest {
    int currentTick = -1;
    int targetObjectId = -1;
    ParasiteHostEventKind event = ParasiteHostEventKind::Healed;
    bool ejectCellAvailable = false;
    SpecialCell ejectCell;
    int stunTicks = 0;
};

struct ParasitePendingLaunchState {
    int projectileId = -1;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    std::string owner;
    std::string sourceTypeId;
    std::string targetTypeId;
    std::string weaponId;
    std::string warheadId;
    ParasiteTargetKind targetKind = ParasiteTargetKind::Vehicle;
    SpecialCell targetCell;
    int launchedTick = 0;
    int damage = 0;
    int damageIntervalTicks = 1;
    int paralyzes = 0;
    int targetMaximumHealth = 1;
    int cullingThresholdPerMyriad = 0;
    bool targetParasiteable = false;
    bool organic = false;
    bool sourceElite = false;
    bool culling = false;
};

struct ParasiteResolvedLaunchState {
    int projectileId = -1;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    int resolvedTick = -1;
    bool hostTerminated = false;
};

struct ParasiteLinkState {
    int projectileId = -1;
    int targetObjectId = -1;
    int sourceObjectId = -1;
    std::string owner;
    std::string sourceTypeId;
    std::string targetTypeId;
    std::string weaponId;
    std::string warheadId;
    int damage = 0;
    int attachedTick = 0;
    int nextDamageTick = 0;
    int damageIntervalTicks = 1;
    bool organic = false;
    bool sourceElite = false;
    int paralyzes = 0;
    bool culling = false;
    int cullingThresholdPerMyriad = 0;
    int targetMaximumHealth = 1;
    bool sourceExpectedInLimbo = true;
};

enum class SpecialMechanismActionKind {
    TimedBombDetonate,
    ParasiteEnterLimbo,
    ParasiteAttachHost,
    ParasiteReturnFromLimbo,
    ParasiteKillTarget,
    ParasiteDamage,
    ParasiteEject,
    ParasiteKillParasite,
    ParasiteReleaseTarget
};

// RuntimeSpecialMechanismState never owns or mutates the referenced objects
// for these mechanisms.  Callers consume these value-only actions at a safe
// point in their world update.
struct SpecialMechanismAction {
    SpecialMechanismActionKind kind =
        SpecialMechanismActionKind::TimedBombDetonate;
    int tick = 0;
    int projectileId = -1;
    int objectId = -1;
    int targetObjectId = -1;
    std::string owner;
    int damage = 0;
    std::string warheadId;
    bool forceDestruction = false;
    int paralyzes = 0;
    bool culling = false;
    bool suppressDeathWeapon = false;
    int stunTicks = 0;
    SpecialCell cell;
    std::string reason;
    // Transient placement permission for this parasite's own lethal bite or
    // infantry cull. Other host events retain ordinary occupied-cell checks.
    bool replacesTerminatingHost = false;
    // Audio provenance only: the source was already inside this host, rather
    // than still travelling toward a pending launch target. Not persisted.
    bool sourceWasAttachedToHost = false;
};

struct ParasiteOperationResult {
    bool accepted = false;
    bool idempotent = false;
    std::string reason;
    std::vector<SpecialMechanismAction> actions;
};

enum class CarrierChildPhase {
    Docked,
    Launching,
    Attacking,
    Returning,
    Destroyed
};

struct CarrierParentState {
    int parentObjectId = -1;
    std::string childType;
    std::string owner;
    int capacity = 0;
    int regenRate = 0;
    int reloadRate = 0;
    std::map<int, int> childBySlot;
    std::map<int, int> slotReadyTick;
};

struct CarrierChildState {
    int childObjectId = -1;
    int parentObjectId = -1;
    int slot = -1;
    CarrierChildPhase phase = CarrierChildPhase::Docked;
    int targetObjectId = -1;
    int actionTick = 0;
    int ammoRemaining = 1;
    // Native aircraft Ammo remains one magazine; CARRIER/HORNET and DEST/ASW
    // sorties discharge three real rounds before consuming that magazine.
    int sortieShotsRemaining = 1;
};

struct CarrierAction {
    enum class Kind {
        Spawn,
        Launch,
        Return,
        Dock,
        Destroy
    };

    Kind kind = Kind::Spawn;
    int parentObjectId = -1;
    int childObjectId = -1;
    int slot = -1;
    int targetObjectId = -1;
    std::string childType;
    std::string owner;
};

struct MindControlOverloadState {
    int nextCheckFrame = 0;
    bool soundPlayed = false;
};

struct MindControlOverloadDamageEvent {
    int controllerObjectId = -1;
    int controlledCount = 0;
    int damage = 0;
    bool playSound = false;
};

bool runtimeHasInfiniteMindControl(const RulesDatabase& rules, const WorldObjectInstance& object);

// The world object owns missile health/armor. This record owns its launch
// sequence and flight; removing that object also removes all impact authority.
struct SpawnedMissileState {
    int objectId = -1;
    int parentObjectId = -1;
    int slot = -1;
    int targetObjectId = -1;
    int startTick = 0;
    int releaseTick = 0;
    int lastTick = 0;
    bool released = false;
    std::string typeId;
    std::string parentType;
    std::string owner;
    std::string weaponId;
    std::string warheadId;
    int damage = 0;
    double damageMultiplier = 1.0;
    double x = 0.0, y = 0.0, z = 0.0;
    double originX = 0.0, originY = 0.0, originZ = 0.0;
    double targetX = 0.0, targetY = 0.0, targetZ = 0.0;
    double pitch = 0.0;
    double heading = 0.0;
    double speed = 0.0;
    // 0: vertical climb, 1: level off / cruise, 2: terminal dive.
    int flightStage = 0;
    // Initial deck attachment in pixels, independent of authoritative flight.
    int originScreenOffsetX = 0;
    int originScreenOffsetY = 0;
    int originNativeFixedX = 0;
    int originNativeFixedY = 0;
};

struct SpecialMechanismRenderSnapshot {
    std::string kind;
    int instanceId = -1;
    int objectId = -1;
    int targetObjectId = -1;
    std::string superWeaponId;
    SpecialCell sourceCell;
    SpecialCell targetCell;
    SpecialCell cell;
    SpecialCell cell2;
    double sourceX = 0.0;
    double sourceY = 0.0;
    double targetX = 0.0;
    double targetY = 0.0;
    // Presentation-only weapon endpoints, resolved from the current FLH pose.
    double sourceLevel = 0.0;
    double targetLevel = 0.0;
    int sourceScreenOffsetX = 0;
    int sourceScreenOffsetY = 0;
    int sourceNativeFixedX = 0;
    int sourceNativeFixedY = 0;
    bool hasBeamEndpoints = false;
    double missilePitch = 0.0;
    int missileFacing = 0;
    int radius = 0;
    int intensity = 0;
    int maximumIntensity = 0;
    int progress = 0;
    int requiredProgress = 0;
    int expireTick = 0;
    std::string owner;
    std::string color;
    std::string phase;
    std::string resourceId;
};

using SpecialAlliancePredicate =
    std::function<bool(const std::string&, const std::string&)>;
using SpecialCellPredicate = std::function<bool(int, int, int)>;
using SpecialEventSink = std::function<void(const SpecialMechanismEvent&)>;

class RuntimeSpecialMechanismState {
public:
    void swap(RuntimeSpecialMechanismState& other) noexcept;

    void reset(const SpecialMechanismRules& rules);

    const SpecialMechanismRules& rules() const;

    std::vector<MindControlOverloadDamageEvent> tickMindControlOverload(
        const std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules, int tick, int fixedTickHz
    );

    bool tryMindControl(
        std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int controllerObjectId,
        int targetObjectId,
        int capacity,
        bool permanent,
        bool infinite,
        bool allowStructures,
        int tick,
        const SpecialAlliancePredicate& areAllied,
        const SpecialEventSink& emit,
        std::string& reason
    );

    bool releaseMindControlByController(
        std::vector<WorldObjectInstance>& objects,
        int controllerObjectId,
        int tick,
        const SpecialEventSink& emit,
        const std::string& reason
    );

    // Dominator control has no living controller dependency and replaces
    // any earlier temporary or permanent link after the ownership transaction.
    void establishDominatorControl(int targetObjectId, const std::string& owner,
        const std::string& originalOwner, int tick);

    bool releaseMindControlByTarget(
        std::vector<WorldObjectInstance>& objects,
        int targetObjectId,
        int tick,
        const SpecialEventSink& emit,
        const std::string& reason
    );

    void notifyActivity(
        int objectId,
        int tick,
        const std::string& reason,
        int fakeBlinkTicks = -1
    );

    bool tickCamouflage(
        std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int tick,
        const std::function<bool(int)>& isStationary,
        const SpecialEventSink& emit
    );

    bool isCamouflaged(int objectId) const;
    bool isCamouflageBlinking(int objectId, int tick) const;

    bool requestChronoMove(
        std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int objectId,
        SpecialCell destination,
        int destinationLevel,
        int tick,
        const SpecialCellPredicate& cellIsLegal,
        const SpecialCellPredicate& cellIsOccupied,
        const SpecialEventSink& emit,
        std::string& reason,
        int fixedTickHz = 30
    );

    bool tickChronoMoves(
        std::vector<WorldObjectInstance>& objects,
        int tick,
        const SpecialCellPredicate& cellIsLegal,
        const SpecialCellPredicate& cellIsOccupied,
        const SpecialEventSink& emit
    );

    bool isChronoInTransit(int objectId) const;
    bool isChronoMovementLocked(int objectId, int tick) const;
    bool cancelChronoMoveInTransit(int objectId) {
        const auto found = chronoByObject_.find(objectId);
        if (found == chronoByObject_.end()
            || (found->second.phase != ChronoPhase::WarpOut
                && found->second.phase != ChronoPhase::InTransit)) return false;
        chronoByObject_.erase(found);
        return true;
    }
    const ChronoMoveState* chronoMoveState(int objectId) const {
        const auto found = chronoByObject_.find(objectId);
        return found == chronoByObject_.end() ? nullptr : &found->second;
    }
    // A Chronosphere relocates immediately, then uses the same persisted
    // recovery state and command/attack locks as an ordinary chrono unit.
    void beginChronosphereRecovery(WorldObjectInstance& object, int tick,
        int fixedTickHz = 30);
    void beginChronoReinforcementRecovery(WorldObjectInstance& object, int tick,
        int fixedTickHz = 30);

    bool applyTemporalHit(
        std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int attackerObjectId,
        int targetObjectId,
        int progress,
        int requiredProgress,
        int tick,
        const SpecialAlliancePredicate& areAllied,
        const SpecialEventSink& emit,
        bool& completed,
        std::string& reason,
        bool allowAlliedTarget = false
    );

    bool isTemporalFrozen(int objectId) const;
    bool hasTemporalAttacker(int attackerObjectId, int targetObjectId) const {
        const auto target = temporalByTarget_.find(targetObjectId);
        return target != temporalByTarget_.end()
            && target->second.progressByAttacker.contains(attackerObjectId);
    }
    const std::map<int, TemporalTargetState>& temporalTargets() const {
        return temporalByTarget_;
    }
    bool beginTemporalTick(int targetObjectId, int tick) {
        const auto target = temporalByTarget_.find(targetObjectId);
        if (target == temporalByTarget_.end() || tick <= target->second.lastAdvancedTick) return false;
        target->second.lastAdvancedTick = tick;
        return true;
    }

    // Owner transitions must not leave a progress edge whose alliance
    // semantics were captured under a different identity.  Expose a
    // read-only aggregate query so the host can reject that transaction
    // before mutating either the world or this state machine.
    bool hasTemporalAuthorityForObject(int objectId) const;

    void clearTemporalAttacker(
        int attackerObjectId,
        int targetObjectId,
        int tick,
        const SpecialEventSink& emit,
        const std::string& reason
    );

    void clearTemporalTarget(
        int targetObjectId,
        int tick,
        const SpecialEventSink& emit,
        const std::string& reason
    );

    void addElectricArc(
        int sourceObjectId,
        int targetObjectId,
        SpecialWorldPoint source,
        SpecialWorldPoint target,
        const std::string& color,
        bool alternateColor,
        bool assault,
        int tick,
        int durationTicks,
        const SpecialEventSink& emit,
        int sourceScreenOffsetX = 0,
        int sourceScreenOffsetY = 0,
        int sourceNativeFixedX = 0,
        int sourceNativeFixedY = 0
    );

    void disableElectricTarget(
        int objectId,
        int untilTick,
        int tick,
        const SpecialEventSink& emit
    );

    bool isElectricDisabled(int objectId, int tick) const;
    bool applyElectricCharge(
        std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int sourceObjectId,
        int targetObjectId,
        int tick,
        int durationTicks,
        int level,
        const SpecialAlliancePredicate& areAllied,
        const SpecialEventSink& emit,
        std::string& reason
    );
    bool isElectricCharged(int objectId, int tick) const;
    void clearElectricChargeByObject(
        int objectId,
        int tick,
        const SpecialEventSink& emit,
        const std::string& reason
    );
    bool hasActiveElectricEffects() const;
    void tickElectric(int tick, const SpecialEventSink& emit);

    int spawnRadiationArea(
        int sourceObjectId,
        const std::string& owner,
        SpecialCell center,
        int radius,
        int strength,
        int damageBase,
        const std::string& warheadId,
        int tick,
        bool persistsAfterSourceDeath,
        const SpecialEventSink& emit
    );

    std::vector<RadiationDamageEvent> tickRadiation(
        std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int tick,
        const SpecialAlliancePredicate& areAllied,
        const std::function<bool(int)>& sourceIsAlive,
        const SpecialEventSink& emit
    );

    bool tryPlantC4(
        const std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int sourceObjectId,
        int targetObjectId,
        int tick,
        int fixedTickHz,
        const SpecialEventSink& emit,
        std::string& reason
    );

    bool tryAttachIvanBomb(
        const std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int sourceObjectId,
        int targetObjectId,
        int tick,
        int fixedTickHz,
        const SpecialEventSink& emit,
        std::string& reason
    );

    bool tryDisarmTimedBomb(
        const std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int sourceObjectId,
        int targetObjectId,
        const std::string& warheadId,
        int tick,
        const SpecialEventSink& emit,
        std::string& reason
    );

    std::vector<SpecialMechanismAction> tickTimedBombs(
        int tick,
        const SpecialEventSink& emit
    );

    ParasiteOperationResult beginParasiteLaunch(
        const ParasiteLaunchRequest& request,
        const SpecialEventSink& emit
    );

    ParasiteOperationResult resolveParasiteImpact(
        const ParasiteImpactRequest& request,
        const SpecialEventSink& emit
    );

    ParasiteOperationResult handleParasiteHostEvent(
        const ParasiteHostEventRequest& request,
        const SpecialEventSink& emit
    );

    std::vector<SpecialMechanismAction> reconcileParasites(
        const std::vector<WorldObjectInstance>& objects,
        const std::vector<int>& activeProjectileIds,
        int tick,
        const SpecialEventSink& emit
    );

    // Legacy one-step adapter retained for existing callers. New projectile
    // hosts should use beginParasiteLaunch() and resolveParasiteImpact().
    bool tryAttachParasite(
        const std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int sourceObjectId,
        int targetObjectId,
        const std::string& weaponId,
        int tick,
        int fixedTickHz,
        SpecialMechanismAction& enterLimboAction,
        const SpecialEventSink& emit,
        std::string& reason
    );

    std::vector<SpecialMechanismAction> tickParasites(
        const std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int tick,
        const SpecialEventSink& emit
    );

    bool isParasiteSourceLimboed(int objectId) const;
    int parasiteTargetForSource(int objectId) const;
    bool isParasiteHostParalyzed(int objectId) const;

    void ensureCarrierParents(
        const std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int tick
    );

    std::vector<CarrierAction> tickCarriers(
        const std::vector<WorldObjectInstance>& objects,
        const RulesDatabase& rules,
        int tick,
        const std::function<int(int)>& parentTarget,
        const SpecialEventSink& emit
    );

    bool attachCarrierChild(
        int parentObjectId,
        int slot,
        int childObjectId,
        int tick,
        const SpecialEventSink& emit
    );

    bool notifyCarrierChildLaunched(
        int childObjectId,
        int tick,
        const SpecialEventSink& emit
    );

    bool notifyCarrierChildFired(
        int childObjectId,
        int tick,
        const SpecialEventSink& emit
    );

    bool requestCarrierChildReturn(
        int childObjectId,
        int tick,
        const SpecialEventSink& emit,
        const std::string& reason
    );

    bool notifyCarrierChildDestroyed(
        int childObjectId,
        int tick,
        const SpecialEventSink& emit
    );

    bool notifyCarrierChildDocked(
        int childObjectId,
        int tick,
        const SpecialEventSink& emit
    );

    bool isCarrierChild(int objectId) const;
    bool isCarrierChildDocked(int objectId) const;
    int carrierParentForChild(int objectId) const;
    bool releaseCarrierMissile(int objectId, int tick, const SpecialEventSink& emit);
    const std::map<int, SpawnedMissileState>& spawnedMissiles() const;
    std::map<int, SpawnedMissileState>& accessSpawnedMissiles();

    void onObjectRemoved(
        std::vector<WorldObjectInstance>& objects,
        int objectId,
        int tick,
        const SpecialEventSink& emit,
        const std::string& reason
    );

    void onObjectRemoved(
        std::vector<WorldObjectInstance>& objects,
        int objectId,
        int tick,
        const SpecialEventSink& emit,
        const std::string& reason,
        std::vector<SpecialMechanismAction>* actions
    );

    void onOwnerChanged(
        std::vector<WorldObjectInstance>& objects,
        int objectId,
        const std::string& newOwner,
        int tick,
        const SpecialEventSink& emit
    );

    void reconcile(
        std::vector<WorldObjectInstance>& objects,
        int tick,
        const SpecialEventSink& emit
    );

    void reconcile(
        std::vector<WorldObjectInstance>& objects,
        int tick,
        const SpecialEventSink& emit,
        std::vector<SpecialMechanismAction>* actions
    );

    void save(std::ostream& output) const;
    bool load(const IniFile& ini, std::string& errorMessage);

    std::uint64_t stateHash() const;
    std::vector<SpecialMechanismRenderSnapshot> renderSnapshots(
        int tick,
        int fixedTickHz = 30
    ) const;

    const std::map<int, MindControlLinkState>& mindControlLinks() const;
    const std::map<int, CamouflageState>& camouflageStates() const;
    bool hasActiveChronoTemporalStates() const {
        return !chronoByObject_.empty() || !temporalByTarget_.empty();
    }
    const std::map<int, RadiationAreaState>& radiationAreas() const;
    const std::map<int, CarrierParentState>& carrierParents() const;
    const std::map<int, CarrierChildState>& carrierChildren() const;
    const std::map<int, ElectricChargeState>& electricCharges() const;
    const std::map<int, TimedBombState>& timedBombs() const;
    const std::map<int, ParasitePendingLaunchState>&
        pendingParasiteLaunches() const;
    const std::map<int, ParasiteLinkState>& parasiteLinks() const;

private:
    void beginChronoArrivalRecovery(WorldObjectInstance& object, int tick,
        int fixedTickHz, int delayFrames);
    SpecialMechanismRules rules_;
    std::map<int, MindControlLinkState> mindControlByTarget_;
    std::map<int, MindControlOverloadState> mindControlOverloadByController_;
    std::map<int, CamouflageState> camouflageByObject_;
    std::map<int, ChronoMoveState> chronoByObject_;
    std::map<int, TemporalTargetState> temporalByTarget_;
    std::map<int, ElectricDisableState> electricDisableByObject_;
    std::map<int, ElectricChargeState> electricChargeByTarget_;
    std::map<int, ElectricArcState> electricArcs_;
    std::map<int, RadiationAreaState> radiationAreas_;
    std::map<int, TimedBombState> timedBombByTarget_;
    std::map<int, ParasitePendingLaunchState> parasitePendingByProjectile_;
    std::map<int, ParasiteLinkState> parasiteByTarget_;
    std::map<int, ParasiteResolvedLaunchState>
        parasiteResolvedByProjectile_;
    std::map<int, CarrierParentState> carrierParents_;
    std::map<int, CarrierChildState> carrierChildren_;
    std::map<int, SpawnedMissileState> spawnedMissiles_;
    int nextElectricArcId_ = 1;
    int nextRadiationAreaId_ = 1;

    static WorldObjectInstance* findObject(
        std::vector<WorldObjectInstance>& objects,
        int objectId
    );
    static const WorldObjectInstance* findObject(
        const std::vector<WorldObjectInstance>& objects,
        int objectId
    );
};

void swap(
    RuntimeSpecialMechanismState& left,
    RuntimeSpecialMechanismState& right
) noexcept;

// The command-line smoke test intentionally uses a synthetic rules fixture so
// it remains independent of the retail resource mount while exercising the
// same state machine used by the runtime.
int runRuntimeSpecialMechanismsSmokeTest();
