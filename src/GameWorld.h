#pragma once

#include "MapDatabase.h"
#include "ModExtensionTypes.h"
#include "RuntimeRuleTime.h"

#include <cstdint>
#include <string>
#include <vector>

class RulesDatabase;

enum class WorldObjectKind {
    Structure,
    Unit,
    Infantry
};

struct OverlayWallState {
    int originalIndex = -1;
    int originalFrame = -1;
    std::string originalType;
    std::string originalImage;
    // -1: not a confirmed original wall; 0: retained death tombstone.
    int health = -1;
};

struct TerrainCell {
    int x = -1;
    int y = -1;
    int cellIndex512 = -1;
    int tileIndex = -1;
    int subTileIndex = -1;
    int level = 0;
    int iceGrowth = 0;
    bool hasOverlay = false;
    int overlayIndex = -1;
    int overlayFrame = 0;
    std::string overlayType;
    std::string overlayImage;
    bool overlayIsOre = false;
    bool overlayIsGem = false;
    OverlayWallState overlayWall;
};

void initializeWorldOverlayWallState(TerrainCell&, const RulesDatabase&);

struct WorldTerrainObject {
    std::string mapId;
    std::string type;
    std::string tag;
    int x = -1;
    int y = -1;
    int cell = -1;
    int level = 0;
    int strength = 1;
    int health = 1;
    bool immune = false;
    bool alive = true;
    int occupationBits = 7;
    std::vector<int> occupiedSubCells{0, 1, 2, 3, 4};
    int footprintWidth = 1;
    int footprintHeight = 1;
};

struct WorldSmudge {
    std::string mapId;
    std::string type;
    int x = -1;
    int y = -1;
    int cell = -1;
    int level = 0;
    int data = 0;
    bool authored = false;
};

// Transient presentation events travel with a staged world object so rejected
// command/owner previews cannot reach the live audio device. They are deliberately
// absent from gameplay hashes and save/snapshot formats, like audio playheads.
struct WorldAircraftAuxSoundEvent {
    std::string event;
    int tick = 0;
    int x = 0;
    int y = 0;
    int level = 0;
    bool takingOff = false;
};

struct WorldTurretSoundEvent {
    std::string event;
    int tick = 0;
    int x = 0;
    int y = 0;
    int level = 0;
    bool rotating = false;
};

struct WorldObjectInstance {
    int instanceId = -1;
    WorldObjectKind kind = WorldObjectKind::Structure;
    std::string mapId;
    std::string owner;
    std::string type;
    int health = -1;
    int x = -1;
    int y = -1;
    int cell = -1;
    int level = 0;
    // Runtime facings use screen-up=0 and advance clockwise. Authored map
    // Dir256 values are converted once when entering the world model.
    int facing = -1;
    int turretFacing = -1;
    RuntimeFacingClock bodyFacingClock;
    RuntimeFacingClock turretFacingClock;
    int subCell = -1;
    std::string mission;
    std::string tag;
    // Map/object veterancy: 0=rookie, 1=veteran, 2=elite.
    int veterancy = 0;
    // Progress toward the next veterancy level. Experience is intentionally
    // stored separately from the authored/map veterancy level so promoted
    // units retain partial progress across save/load and transformations.
    int veterancyExperience = 0;
    // Permanent per-instance multipliers granted by classic powerup crates.
    // A value of one means the unit has not received that upgrade yet.
    double crateArmorMultiplier = 1.0;
    double crateFirepowerMultiplier = 1.0;
    double crateSpeedMultiplier = 1.0;
    // Classic Cloak crates permanently grant the per-instance capability;
    // authored type rules remain unchanged for every other instance.
    bool crateCloakable = false;
    // A ground move suspends the mining mission without discarding cargo.
    // Only a subsequent ore command resumes automatic collection/return.
    bool harvestAutoSearchSuspended = false;
    // Retained across recapture and MCV transformations; captured structures
    // have a reduced original-game crew and cannot return an engineer.
    bool hasBeenCaptured = false;
    bool onBridge = false;
    // True only for objects authored in the map's [Aircraft] section. This
    // keeps initial flight-state hydration distinct from aircraft-like units
    // placed in [Units].
    bool authoredAircraft = false;
    bool alive = true;
    // Health=0 structures with LeaveRubble=yes enter the world as destroyed
    // rubble rather than as live zero-hit-point blockers.
    bool rubble = false;
    // Authoritative per-instance dive state. UnitType::Underwater supplies
    // only the initial value; gameplay may surface or submerge the instance.
    bool submerged = false;
    // Cloaking is per-instance runtime state. A zero cooldown means the
    // object is immediately eligible to begin/re-enter its cloaked state.
    bool cloaked = false;
    int cloakCooldownTicks = 0;
    int submergeCooldownTicks = 0;
    bool deployed = false;
    // Shared authoritative countdown for deploy substates. It holds the
    // remaining UndeployDelay while deployed; -1 means no timed transition.
    int deployStateTicksRemaining = -1;
    // Authoritative infantry cover stance. This is independent from mission
    // so a prone infantryman can crawl or fire without losing damage state.
    bool prone = false;
    int proneTicksRemaining = 0;
    // Command-independent reload authority. Behaviors mirror this value for
    // animation/task state, but replacing a command must not reset weapon ROF.
    int weaponCooldownTicksRemaining = 0;
    // Native mobile-SHP firing has its own countdown, independent of ROF,
    // rank and mission. This visual timestamp is saved separately and does
    // not participate in gameplay hashes (retail UnitClass +0x6C0).
    int spriteFiringStartTick = -1;
    // Deployment radiation has its own ROF; toggling stance cannot reset it.
    int radiationEruptionCooldownTicksRemaining = 0;
    bool hasRenderPosition = false;
    double renderX = 0.0;
    double renderY = 0.0;
    // Transient visual pose used while crossing a movement segment. Static
    // placement/dock anchors may have a render position without being in
    // motion, so renderMoveActive is the authoritative walk-animation gate.
    bool renderMoveActive = false;
    double renderLevel = 0.0;
    bool renderOnBridge = false;
    // Distance travelled by the interpolated pose, in cells. This keeps walk
    // frames continuous across cell boundaries and direction changes.
    double movementAnimationDistance = 0.0;
    int buildAnimTicksTotal = 0;
    int buildAnimTicksRemaining = 0;
    // Structure recoil is derived from this simulation tick at render time.
    // Keeping one timestamp avoids both per-paint mutation and a per-tick
    // scan over every world object.
    int structureRecoilStartTick = -1;
    // Charge visuals use this saved timestamp without painting mutations:
    // IsChargeTurret starts on discharge; an IsAnimDelayedFire structure
    // starts SpecialAnim on prefire and only shows it while charging.
    int chargeTurretStartTick = -1;
    // Structure animation/operation state is kept on the world object so
    // rendering can gate each layer without freezing unrelated flag anims.
    bool workAnimationActive = true;
    bool poweredOperational = true;
    // Powered building animations freeze on their current authored frame.
    // Track both the active outage and prior outage duration so restoring
    // power resumes from that frame instead of jumping with the global clock.
    int powerAnimationPausedAtTick = -1;
    int powerAnimationPausedTicks = 0;
    // Stock war factories use separate authored overlays for a vehicle door
    // and for aircraft/zeppelin roof delivery. These ticks keep the transient
    // overlay deterministic and allow it to resume correctly after a save.
    int factoryExitAnimationStartTick = -1;
    int factoryExitAnimationEndTick = -1;
    bool factoryExitAirborne = false;
    // A produced air unit keeps its own factory roof open until its egress
    // clears, including AI deliveries and a save taken during takeoff.
    int factoryExitHostId = -1;
    // Oil Derrick income uses a per-instance deadline so capture late in a
    // global frame cycle still waits the full ProduceCashDelay. This belongs
    // to authoritative save/replay state rather than rendering state.
    int specialCashNextTick = -1;
    // A Cloning Vat remains occupied until this concrete clone clears the
    // delivery area. The lease prevents another completion from reusing a
    // blocked vat merely because its door animation elapsed.
    int specialCloneDeliveryObjectId = -1;

    // Transport manifests keep boarded objects in the world model while they
    // are hidden from ordinary simulation/rendering. Gunner vehicles cache
    // the active authored mode so weapon and turret selection stay
    // deterministic across rendering and save/load.
    std::vector<int> transportPassengerIds;
    // Actual bio-reactor entry/exit restarts its original working animation.
    // Optional save state: legacy objects retain the global animation clock.
    int bioReactorAnimationStartTick = -1;
    int bioReactorAnimationPoweredStartTick = -1;
    int activeGunnerMode = -1;
    int activeGunnerWeaponIndex = -1;
    int activeGunnerTurretIndex = -1;

    // Bunkers retain a live armed vehicle, unlike hidden transport passengers.
    int tankBunkerHostId = -1;
    int tankBunkerOccupantId = -1;
    int tankBunkerAnimationStartTick = -1;
    bool tankBunkerLowering = false;

    // Airport-bound aircraft occupy a concrete pad on their host structure.
    int dockedToObjectId = -1;
    int dockIndex = -1;
    // Flight altitude is independent of the terrain cell underneath the
    // aircraft.  `level` mirrors this value while airborne so existing
    // projection/rendering code receives the correct world height.
    bool airborne = false;
    int flightLevel = 0;
    // Derived presentation progress for ordinary fixed-wing airport descent
    // and climb. A negative value retains the normal airborne sprite lift.
    // Rebuilt from flight authority; absent from saves, snapshots and hashes.
    double aircraftDockFlightProgress = -1.0;
    // Tick-driven Kirov roof clearance, in projected world levels. This is
    // rebuilt from its saved presentation flight state and never changes Z.
    // A negative value retains the ordinary aircraft/factory rendering path.
    double aircraftBuildingClearanceLevel = -1.0;
    // Derived cardinal adjacency for wall artwork: N=1, E=2, S=4, W=8.
    // The mask is refreshed from live neighboring wall objects and is not
    // serialized as authoritative game state.
    int wallConnectionMask = 0;

    // Garrison relationships are stored in both directions.  The host
    // manifest is authoritative and preserves deterministic firing order;
    // the reverse id keeps occupant lookups and corruption recovery cheap.
    int garrisonHostObjectId = -1;
    std::vector<int> garrisonOccupantIds;
    int garrisonFireCursor = 0;

    // -1 is the authoritative unlimited-ammunition sentinel. Limited types
    // are hydrated from their rules when entering the world model.
    int currentAmmo = -1;
    std::vector<WorldAircraftAuxSoundEvent> pendingAircraftAuxSounds;
    // Local presentation activity, carried through world transactions only.
    // These fields are absent from saves, snapshots and gameplay hashes.
    int turretSoundActivityTick = -1;
    std::string activeTurretSoundEvent;
    std::vector<WorldTurretSoundEvent> pendingTurretSounds;
    RuntimeModExtensionState modExtensions;
};

struct WorldObjectCellOffset {
    double x = 0.0;
    double y = 0.0;
};

// A stopped ground mover can rest between its committed navigation cells.
// Its continuous position remains authoritative, including height/bridge,
// while renderMoveActive stays false so walking/wakes stop immediately.
inline bool worldObjectHasGroundRenderPose(const WorldObjectInstance& object) {
    return object.hasRenderPosition && !object.airborne
        && (object.kind == WorldObjectKind::Unit || object.kind == WorldObjectKind::Infantry)
        && object.dockedToObjectId < 0 && object.tankBunkerHostId < 0
        && object.factoryExitHostId < 0 && object.garrisonHostObjectId < 0;
}

inline bool worldObjectHasStoppedGroundPose(const WorldObjectInstance& object) {
    return worldObjectHasGroundRenderPose(object) && !object.renderMoveActive
        && (object.renderX != object.x || object.renderY != object.y
            || object.renderLevel != object.level || object.renderOnBridge != object.onBridge);
}

WorldObjectCellOffset worldObjectSubCellOffset(
    const WorldObjectInstance& object
);

int worldObjectFacingFromMapFacing(int facing);
int worldObjectArtFacingBucket(int runtimeFacing, int facingCount);
int worldObjectMovementAnimationPhase(
    const WorldObjectInstance& object,
    int frameCount
);

struct PlayerState {
    std::string owner;
    int structureCount = 0;
    int unitCount = 0;
    int infantryCount = 0;
    int aircraftCount = 0;
    int objectCount = 0;
};

struct WorldWaypoint {
    std::string id;
    int x = -1;
    int y = -1;
    int cell = -1;
};

struct GameWorldSummary {
    bool loaded = false;
    std::string mapName;
    std::string theater;
    std::string localSize;
    int width = -1;
    int height = -1;
    int terrainCells = 0;
    int overlayCells = 0;
    int overlayOreCells = 0;
    int overlayGemCells = 0;
    int terrainObjects = 0;
    int smudges = 0;
    int structures = 0;
    int units = 0;
    int infantry = 0;
    int aircraft = 0;
    int objectInstances = 0;
    int waypoints = 0;
    int players = 0;
    int bridgeObjects = 0;
    int minCellX = -1;
    int minCellY = -1;
    int maxCellX = -1;
    int maxCellY = -1;
    bool countsMatchMap = false;
};

class GameWorld {
public:
    bool loadFromMap(
        const MapDatabase& mapDatabase,
        const RulesDatabase* rules = nullptr
    );

    const GameWorldSummary& getSummary() const;
    const std::vector<TerrainCell>& getTerrainCells() const;
    const std::vector<MapOverlayCell>& getOverlayCells() const;
    const std::vector<WorldTerrainObject>& getTerrainObjects() const;
    std::vector<WorldTerrainObject>& accessTerrainObjects();
    const std::vector<WorldSmudge>& getSmudges() const;
    std::vector<WorldSmudge>& accessSmudges();
    const std::vector<WorldObjectInstance>& getObjects() const;
    std::vector<TerrainCell>& accessTerrainCells();
    std::vector<WorldObjectInstance>& accessObjects();
    const std::vector<PlayerState>& getPlayers() const;
    const std::vector<WorldWaypoint>& getWaypoints() const;
    const std::vector<std::string>& getWarnings() const;

    // Canonical, insertion-order-independent hash of simulation-bearing world
    // state. Live terrain/resource data, terrain objects, smudges, waypoints,
    // and authoritative object fields participate, including the legacy
    // render pose and factory-exit window currently consumed by simulation.
    // Import mirrors, derived counters, diagnostics, and purely visual
    // animation caches do not.
    std::uint64_t deterministicSimulationStateHash() const;

private:
    GameWorldSummary summary_;
    std::vector<TerrainCell> terrainCells_;
    std::vector<MapOverlayCell> overlayCells_;
    std::vector<WorldTerrainObject> terrainObjects_;
    std::vector<WorldSmudge> smudges_;
    std::vector<WorldObjectInstance> objects_;
    std::vector<PlayerState> players_;
    std::vector<WorldWaypoint> waypoints_;
    std::vector<std::string> warnings_;
};

std::string toString(WorldObjectKind kind);
