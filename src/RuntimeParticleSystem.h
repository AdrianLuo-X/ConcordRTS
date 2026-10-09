#pragma once

#include "IniFile.h"
#include "RulesDatabase.h"
#include "RuntimeLightingSystem.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

struct ArtType;
class ArtDatabase;

// Hard deterministic safety limits.  Runtime spawning and save loading use
// the same ceilings so a hostile NumParticles value or save file cannot grow
// the backing vectors without bound.
inline constexpr std::size_t runtimeParticleMaximumSystems = 4096U;
inline constexpr std::size_t runtimeParticleMaximumParticles = 65536U;
inline constexpr std::size_t runtimeParticleMaximumParticlesPerSpawn = 4096U;
inline constexpr std::size_t runtimeParticleMaximumRenderSnapshots =
    runtimeParticleMaximumParticles;

enum class RuntimeParticleRenderKind {
    Shp,
    Spark,
    Pixel,
    Fallback
};

// Immutable emission-time geometry, expressed without a viewport or camera.
// Smoke can reproduce the body's presentation at any zoom after its source or
// the underlying building has moved/disappeared. No live object pointers/IDs.
inline constexpr std::size_t runtimeParticleMaximumClearanceRoofs = 64U;
struct RuntimeParticleClearanceRoof {
    double cellSum = 0.0;
    double level = 0.0;
    int authoredHeight = 0;
    int nativeSpriteHeight = 0;
    bool rubble = false;
    bool operator==(const RuntimeParticleClearanceRoof&) const = default;
};

struct RuntimeParticleAirbornePresentation {
    bool enabled = false;
    double sourceCellSum = 0.0;
    double sourceLevel = 0.0;
    int factoryLiftCancellation = -1;
    // Airport descent fades the complete roof clearance at the captured
    // flight height. Kept with the frozen geometry for every render zoom.
    double clearanceFraction = 1.0;
    // A managed Kirov freezes its already-smoothed display lift at emission.
    // This optional local presentation value is outside gameplay authority.
    double resolvedClearanceLevel = -1.0;
    std::vector<RuntimeParticleClearanceRoof> roofs;
    bool operator==(const RuntimeParticleAirbornePresentation&) const = default;
};

// Voxel launch attachments contain both scalable authored offsets and native
// raster offsets. Freezing all four components preserves them across zooms.
struct RuntimeParticleSpriteAttachment {
    int screenOffsetX = 0;
    int screenOffsetY = 0;
    int nativeFixedX = 0;
    int nativeFixedY = 0;
    bool voxelAnchor = false;
    bool airborne = false;
    bool operator==(const RuntimeParticleSpriteAttachment&) const = default;
};

// Renderer-facing value object.  Raw coordinates retain the authored
// lepton-like particle domain (256 units per map cell), while worldX/Y/Level
// are preconverted map-cell coordinates.  No pointer reaches the renderer.
struct RuntimeParticleRenderSnapshot {
    std::uint64_t instanceId = 0;
    std::uint64_t systemInstanceId = 0;
    std::string particleTypeId;
    std::string imageId;
    std::string owner;
    double rawX = 0.0;
    double rawY = 0.0;
    double rawZ = 0.0;
    double worldX = 0.0;
    double worldY = 0.0;
    double worldLevel = 0.0;
    // Native screen pixels inherited at emission; independent of later source motion.
    int screenOffsetY = 0;
    RuntimeParticleAirbornePresentation presentation;
    RuntimeParticleSpriteAttachment spriteAttachment;
    // Derived from the emitted particle's saved provenance, never its current source.
    bool airborneDamageSmoke = false;
    int frameIndex = 0;
    int translucency = 0;
    RuntimeParticleRenderKind renderKind = RuntimeParticleRenderKind::Fallback;
    std::uint8_t red = 255;
    std::uint8_t green = 255;
    std::uint8_t blue = 255;
    int ySortAdjust = 0;
    // Stable content revision used by render signatures.  It changes when
    // any renderer-visible field changes, not when hidden simulation fields do.
    std::uint64_t revision = 0;
};

struct RuntimeParticleBeamSnapshot {
    std::uint64_t systemInstanceId = 0;
    std::string owner;
    double fromWorldX = 0.0;
    double fromWorldY = 0.0;
    double fromWorldLevel = 0.0;
    double toWorldX = 0.0;
    double toWorldY = 0.0;
    double toWorldLevel = 0.0;
    std::uint8_t red = 255;
    std::uint8_t green = 255;
    std::uint8_t blue = 255;
    int thickness = 1;
    std::uint64_t revision = 0;
};

struct RuntimeParticlePoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct RuntimeParticleSpawnContext {
    int sourceObjectId = -1;
    std::string owner;
    std::string sourceTag;
    RuntimeParticlePoint position;
    RuntimeParticlePoint direction;
    int screenOffsetY = 0;
    RuntimeParticleAirbornePresentation presentation;
    RuntimeParticleSpriteAttachment spriteAttachment;
    std::uint64_t seed = 0x9e3779b97f4a7c15ULL;
    // Optional producer lifetime in Westwood's authored 15 Hz frame domain.
    // A positive value overrides ParticleSystemType::Lifetime for this spawn.
    int systemLifetimeOverrideFrames = -1;
    // Animation gas and its dissipation stage can carry a physical velocity
    // even when the particle type does not author Velocity=.
    bool inheritGasVelocity = false;
};

struct RuntimeParticleDamageEvent {
    int tick = 0;
    std::uint64_t systemInstanceId = 0;
    std::uint64_t particleInstanceId = 0;
    int sourceObjectId = -1;
    std::string owner;
    std::string sourceTag;
    std::string particleTypeId;
    std::string warheadId;
    RuntimeParticlePoint position;
    int damage = 0;
    int radius = 0;
};

struct RuntimeParticleSystemInstance {
    std::uint64_t id = 0;
    std::string typeId;
    int sourceObjectId = -1;
    std::string owner;
    std::string sourceTag;
    RuntimeParticlePoint origin;
    RuntimeParticlePoint direction;
    int screenOffsetY = 0;
    RuntimeParticleAirbornePresentation presentation;
    RuntimeParticleSpriteAttachment spriteAttachment;
    int ageTicks = 0;
    int remainingLifetime = -1;
    int spawnCountdown = 0;
    std::uint64_t spawnSerial = 0;
    std::uint64_t rngState = 1;
    bool emitting = false;
};

struct RuntimeParticleInstance {
    std::uint64_t id = 0;
    std::uint64_t systemInstanceId = 0;
    std::string typeId;
    int sourceObjectId = -1;
    std::string owner;
    std::string sourceTag;
    RuntimeParticlePoint position;
    RuntimeParticlePoint velocity;
    int screenOffsetY = 0;
    RuntimeParticleAirbornePresentation presentation;
    RuntimeParticleSpriteAttachment spriteAttachment;
    double speed = 0.0;
    int ageTicks = 0;
    int remainingLifetime = -1;
    int damageCountdown = 0;
    int stateAI = 0;
    int stateAdvanceCountdown = 0;
    int translucency = 0;
    int authoredFrameProgress = 0;
    std::uint64_t rngState = 1;
};

struct RuntimeParticleSpawnResult {
    std::vector<std::uint64_t> systemInstanceIds;
    std::vector<std::uint64_t> particleInstanceIds;
    std::vector<std::string> warnings;

    bool spawned() const {
        return !systemInstanceIds.empty() || !particleInstanceIds.empty();
    }
};

struct RuntimeParticleReconcileReport {
    std::size_t removedSystems = 0;
    std::size_t removedParticles = 0;
    std::size_t stoppedSystems = 0;
};

class RuntimeParticleSystemState {
public:
    RuntimeParticleSystemState() = default;
    explicit RuntimeParticleSystemState(int fixedTickHz);

    void clear();
    void reset(int fixedTickHz = 30);
    // Legacy saves can omit the particle section.  An empty value-state may
    // adopt the host's completed tick so its next advance remains in lockstep.
    bool alignEmptyStateTick(int hostTick);

    std::uint64_t spawnSystem(
        const RulesDatabase& rules,
        const std::string& particleSystemTypeId,
        const RuntimeParticleSpawnContext& context,
        std::string* errorMessage = nullptr
    );
    std::uint64_t spawnParticle(
        const RulesDatabase& rules,
        const std::string& particleTypeId,
        const RuntimeParticleSpawnContext& context,
        std::string* errorMessage = nullptr
    );
    RuntimeParticleSpawnResult spawnFromWarhead(
        const RulesDatabase& rules,
        const WarheadType& warhead,
        const RuntimeParticleSpawnContext& context
    );
    RuntimeParticleSpawnResult spawnFromAnimation(
        const RulesDatabase& rules,
        const ArtType& animation,
        const RuntimeParticleSpawnContext& context
    );

    std::vector<RuntimeParticleDamageEvent> tick(
        const RulesDatabase& rules
    );
    bool stopSystem(std::uint64_t systemInstanceId);
    // Only the producer follows its owner; already emitted smoke stays in world space.
    bool moveSystem(std::uint64_t systemInstanceId, const RuntimeParticlePoint& origin,
        const std::string& owner, int screenOffsetY = 0,
        const RuntimeParticleAirbornePresentation& presentation = {},
        const RuntimeParticleSpriteAttachment& spriteAttachment = {});
    std::size_t stopSystemsForSource(int sourceObjectId);

    RuntimeParticleReconcileReport reconcile(const RulesDatabase& rules);
    std::uint64_t stateHash() const;
    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const RulesDatabase& rules,
        std::string* errorMessage = nullptr,
        RuntimeParticleReconcileReport* reconcileReport = nullptr
    );

    int currentTick() const;
    int fixedTickHz() const;
    const std::vector<RuntimeParticleSystemInstance>& systems() const;
    const std::vector<RuntimeParticleInstance>& particles() const;
    std::vector<RuntimeParticleRenderSnapshot> renderSnapshots(
        const RulesDatabase& rules,
        const ArtDatabase* art = nullptr
    ) const;
    std::vector<RuntimeParticleBeamSnapshot> beamSnapshots(
        const RulesDatabase& rules
    ) const;
    std::vector<RuntimeLocalLightSnapshot> localLightSnapshots(
        const RulesDatabase& rules
    ) const;

private:
    void saveInternal(std::ostream& output, bool authoritative) const;
    // Monotonic allocation appends IDs; stable removal and load reconciliation
    // preserve this order. Snapshot producers can traverse without sorting.
    std::vector<RuntimeParticleSystemInstance> systems_;
    std::vector<RuntimeParticleInstance> particles_;
    struct TickSystemData {
        std::uint64_t id = 0;
        const ParticleSystemType* type = nullptr;
        int liveParticles = 0;
    };
    // Scratch capacity is bounded by the live-system ceiling and reused each
    // tick. Sizes are cleared afterward: copies and saves carry no lookup
    // state or rule pointers, and expired IDs never accumulate here.
    std::vector<TickSystemData> tickSystems_;
    std::vector<std::size_t> tickSystemSlots_;
    int currentTick_ = 0;
    std::uint64_t nextSystemId_ = 1;
    std::uint64_t nextParticleId_ = 1;
    int fixedTickHz_ = 30;

    std::uint64_t spawnParticleLinked(
        const RulesDatabase& rules,
        const std::string& particleTypeId,
        const RuntimeParticleSpawnContext& context,
        std::uint64_t systemInstanceId,
        std::uint64_t seed,
        std::string* errorMessage
    );

    int convertRuleFrames(int authoredFrames) const;
};
