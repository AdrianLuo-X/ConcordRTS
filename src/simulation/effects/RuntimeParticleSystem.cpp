#include "simulation/effects/RuntimeParticleSystem.h"

#include "data/art/ArtDatabase.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr int kAuthoredTicksPerSecond = 15;
constexpr double kRawUnitsPerCell = 256.0;
constexpr double kMaximumRenderCoordinate = 268435456.0;
constexpr int kRailgunLaserLifetimeFrames = 10;

void hashParticlePresentation(std::uint64_t& hash,
    const RuntimeParticleAirbornePresentation& presentation,
    const RuntimeParticleSpriteAttachment& attachment);

const RuntimeParticleSystemInstance* findRuntimeSystem(
    const std::vector<RuntimeParticleSystemInstance>& systems,
    std::uint64_t id
);

std::uint64_t hashText(std::uint64_t hash, const std::string& value) {
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= kFnvPrime;
    }
    return hash;
}

void hashSnapshotValue(std::uint64_t& hash, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        hash ^= (value >> shift) & 0xffULL;
        hash *= kFnvPrime;
    }
}

void hashSnapshotDouble(std::uint64_t& hash, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    hashSnapshotValue(hash, bits);
}

ParticleColor defaultRenderColor(ParticleBehavior behavior) {
    switch (behavior) {
    case ParticleBehavior::Gas:
    case ParticleBehavior::WeakGas:
        return {112, 196, 112};
    case ParticleBehavior::Smoke:
        return {144, 144, 144};
    case ParticleBehavior::Fire:
        return {255, 128, 40};
    case ParticleBehavior::Spark:
        return {255, 232, 128};
    case ParticleBehavior::Railgun:
        return {128, 208, 255};
    case ParticleBehavior::Web:
        return {224, 224, 224};
    case ParticleBehavior::None:
    case ParticleBehavior::Unknown:
    default:
        return {208, 208, 208};
    }
}

ParticleColor renderColorForParticle(
    const RuntimeParticleInstance& particle,
    const ParticleType& type,
    int fixedTickHz
) {
    if (!type.colorList.empty()) {
        const double authoredAge = static_cast<double>(particle.ageTicks)
            * static_cast<double>(kAuthoredTicksPerSecond)
            / static_cast<double>(std::max(1, fixedTickHz));
        const double phase = std::clamp(
            authoredAge * type.colorSpeed,
            0.0,
            static_cast<double>(type.colorList.size() - 1U)
        );
        const std::size_t lower = static_cast<std::size_t>(
            std::floor(phase)
        );
        const std::size_t upper = std::min(
            lower + 1U,
            type.colorList.size() - 1U
        );
        const double fraction = phase - static_cast<double>(lower);
        ParticleColor first = type.colorList[lower];
        if (lower == 0U && (type.hasStartColor1 || type.hasStartColor2)) {
            const bool chooseSecond = type.hasStartColor2
                && (!type.hasStartColor1
                    || ((particle.id ^ particle.systemInstanceId) & 1ULL)
                        != 0ULL);
            first = chooseSecond ? type.startColor2 : type.startColor1;
        }
        const ParticleColor& second = type.colorList[upper];
        const auto interpolate = [fraction](int from, int to) {
            return static_cast<int>(std::lround(
                static_cast<double>(from)
                    + (static_cast<double>(to - from) * fraction)
            ));
        };
        return {
            interpolate(first.red, second.red),
            interpolate(first.green, second.green),
            interpolate(first.blue, second.blue)
        };
    }
    if (type.hasStartColor1 && type.hasStartColor2) {
        return ((particle.id ^ particle.systemInstanceId) & 1ULL) == 0ULL
            ? type.startColor1
            : type.startColor2;
    }
    if (type.hasStartColor1) {
        return type.startColor1;
    }
    if (type.hasStartColor2) {
        return type.startColor2;
    }
    return defaultRenderColor(type.behavior);
}

std::uint64_t renderSnapshotRevision(
    const RuntimeParticleRenderSnapshot& snapshot
) {
    std::uint64_t hash = kFnvOffset;
    hashSnapshotValue(hash, snapshot.instanceId);
    hashSnapshotValue(hash, snapshot.systemInstanceId);
    hash = hashText(hash, snapshot.particleTypeId);
    hashSnapshotValue(hash, snapshot.particleTypeId.size());
    hash = hashText(hash, snapshot.imageId);
    hashSnapshotValue(hash, snapshot.imageId.size());
    hash = hashText(hash, snapshot.owner);
    hashSnapshotValue(hash, snapshot.owner.size());
    hashSnapshotDouble(hash, snapshot.rawX);
    hashSnapshotDouble(hash, snapshot.rawY);
    hashSnapshotDouble(hash, snapshot.rawZ);
    hashSnapshotDouble(hash, snapshot.worldX);
    hashSnapshotDouble(hash, snapshot.worldY);
    hashSnapshotDouble(hash, snapshot.worldLevel);
    hashSnapshotValue(hash, static_cast<std::uint64_t>(
        static_cast<std::int64_t>(snapshot.screenOffsetY)));
    hashSnapshotValue(hash, snapshot.airborneDamageSmoke ? 1U : 0U);
    hashParticlePresentation(hash, snapshot.presentation, snapshot.spriteAttachment);
    hashSnapshotValue(
        hash,
        static_cast<std::uint64_t>(static_cast<std::int64_t>(
            snapshot.frameIndex
        ))
    );
    hashSnapshotValue(
        hash,
        static_cast<std::uint64_t>(snapshot.translucency)
    );
    hashSnapshotValue(
        hash,
        static_cast<std::uint64_t>(snapshot.renderKind)
    );
    hashSnapshotValue(hash, snapshot.red);
    hashSnapshotValue(hash, snapshot.green);
    hashSnapshotValue(hash, snapshot.blue);
    hashSnapshotValue(
        hash,
        static_cast<std::uint64_t>(static_cast<std::int64_t>(
            snapshot.ySortAdjust
        ))
    );
    return hash;
}

std::uint64_t mixedSeed(
    std::uint64_t seed,
    const std::string& typeId,
    std::uint64_t instanceId,
    int sourceObjectId
) {
    std::uint64_t value = hashText(kFnvOffset, typeId);
    value ^= seed + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
    value ^= instanceId * 0xbf58476d1ce4e5b9ULL;
    value ^= static_cast<std::uint64_t>(
        static_cast<std::int64_t>(sourceObjectId) + 0x100000001LL
    );
    return value == 0 ? 0x94d049bb133111ebULL : value;
}

std::uint64_t nextRandom(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double randomUnit(std::uint64_t& state) {
    return static_cast<double>(nextRandom(state) >> 11U)
        * (1.0 / 9007199254740992.0);
}

double randomCentered(std::uint64_t& state, double magnitude) {
    if (!(magnitude > 0.0) || !std::isfinite(magnitude)) {
        return 0.0;
    }
    return ((randomUnit(state) * 2.0) - 1.0) * magnitude;
}

int randomSigned(std::uint64_t& state, int magnitude) {
    if (magnitude <= 0) {
        return 0;
    }
    const std::uint64_t width = static_cast<std::uint64_t>(magnitude) * 2ULL + 1ULL;
    const std::int64_t sampled = static_cast<std::int64_t>(
        nextRandom(state) % width
    ) - static_cast<std::int64_t>(magnitude);
    return static_cast<int>(sampled);
}

bool hasDirection(const RuntimeParticlePoint& point) {
    return std::abs(point.x) > 1.0e-12
        || std::abs(point.y) > 1.0e-12
        || std::abs(point.z) > 1.0e-12;
}

RuntimeParticlePoint normalizedDirection(
    const RuntimeParticlePoint& direction,
    double length
) {
    const double magnitude = std::sqrt(
        direction.x * direction.x
        + direction.y * direction.y
        + direction.z * direction.z
    );
    if (magnitude <= 1.0e-12) {
        return {0.0, 0.0, length};
    }
    const double scale = length / magnitude;
    return {
        direction.x * scale,
        direction.y * scale,
        direction.z * scale
    };
}

double vectorLength(const RuntimeParticlePoint& value) {
    return std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z
    );
}

RuntimeParticlePoint crossProduct(
    const RuntimeParticlePoint& left,
    const RuntimeParticlePoint& right
) {
    return {
        (left.y * right.z) - (left.z * right.y),
        (left.z * right.x) - (left.x * right.z),
        (left.x * right.y) - (left.y * right.x)
    };
}

void perpendicularBasis(
    const RuntimeParticlePoint& axis,
    RuntimeParticlePoint& first,
    RuntimeParticlePoint& second
) {
    const RuntimeParticlePoint reference = std::abs(axis.z) < 0.9
        ? RuntimeParticlePoint{0.0, 0.0, 1.0}
        : RuntimeParticlePoint{0.0, 1.0, 0.0};
    first = normalizedDirection(crossProduct(axis, reference), 1.0);
    second = normalizedDirection(crossProduct(axis, first), 1.0);
}

RuntimeParticlePoint windOffset(
    ParticleBehavior behavior,
    int windDirection,
    int windEffect
) {
    static constexpr double gasOffsets[8][2] = {
        {0.0, -2.0}, {2.0, -2.0}, {2.0, 0.0}, {2.0, 2.0},
        {0.0, 2.0}, {-2.0, 2.0}, {-2.0, 0.0}, {-2.0, -2.0}
    };
    static constexpr double smokeOffsets[8][2] = {
        {0.0, -2.0}, {2.0, -2.0}, {2.0, 0.0}, {1.0, 2.0},
        {0.0, 2.0}, {-2.0, 2.0}, {-2.0, 0.0}, {-2.0, -2.0}
    };
    const int direction = ((windDirection % 8) + 8) % 8;
    const bool gas = behavior == ParticleBehavior::Gas
        || behavior == ParticleBehavior::WeakGas;
    const double scale = gas ? static_cast<double>(windEffect) : 1.0;
    const double (*offsets)[2] = gas ? gasOffsets : smokeOffsets;
    return {
        offsets[direction][0] * scale,
        offsets[direction][1] * scale,
        0.0
    };
}

bool behaviorCanDamage(ParticleBehavior behavior) {
    return behavior == ParticleBehavior::Gas
        || behavior == ParticleBehavior::WeakGas
        || behavior == ParticleBehavior::Fire;
}

bool behaviorSeedsOnce(ParticleBehavior behavior) {
    return behavior == ParticleBehavior::Gas
        || behavior == ParticleBehavior::WeakGas
        || behavior == ParticleBehavior::Railgun;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            if (character >= 'A' && character <= 'Z') {
                return static_cast<char>(character - 'A' + 'a');
            }
            return static_cast<char>(character);
        }
    );
    return value;
}

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char character : value) {
        encoded.push_back(digits[(character >> 4U) & 0x0FU]);
        encoded.push_back(digits[character & 0x0FU]);
    }
    return encoded;
}

int hexDigit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return 10 + character - 'a';
    }
    if (character >= 'A' && character <= 'F') {
        return 10 + character - 'A';
    }
    return -1;
}

bool hexDecode(const std::string& value, std::string& decoded) {
    if ((value.size() % 2U) != 0U) {
        return false;
    }
    decoded.clear();
    decoded.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = hexDigit(value[index]);
        const int low = hexDigit(value[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

bool parseIntStrict(const std::string& value, int& result) {
    try {
        std::size_t consumed = 0;
        const long long parsed = std::stoll(value, &consumed);
        if (consumed != value.size()
            || parsed < std::numeric_limits<int>::min()
            || parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        result = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseUint64Strict(const std::string& value, std::uint64_t& result) {
    try {
        if (value.empty() || value.front() == '-') {
            return false;
        }
        std::size_t consumed = 0;
        result = std::stoull(value, &consumed);
        return consumed == value.size();
    } catch (const std::exception&) {
        return false;
    }
}

bool parseDoubleStrict(const std::string& value, double& result) {
    try {
        std::size_t consumed = 0;
        result = std::stod(value, &consumed);
        return consumed == value.size() && std::isfinite(result);
    } catch (const std::exception&) {
        return false;
    }
}

bool parseBool(const std::string& value, bool& result) {
    const std::string normalized = lowerAscii(value);
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        result = true;
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0") {
        result = false;
        return true;
    }
    return false;
}

const RuntimeParticleSystemInstance* findRuntimeSystem(
    const std::vector<RuntimeParticleSystemInstance>& systems,
    std::uint64_t id
) {
    const auto found = std::find_if(
        systems.begin(),
        systems.end(),
        [id](const RuntimeParticleSystemInstance& system) {
            return system.id == id;
        }
    );
    return found == systems.end() ? nullptr : &*found;
}

struct PendingParticleSpawn {
    std::string typeId;
    RuntimeParticleSpawnContext context;
    std::uint64_t systemInstanceId = 0;
    std::uint64_t seed = 1;
    int minimumTranslucency = 0;
};

#include "simulation/effects/RuntimeParticlePresentation.inl"

} // namespace

RuntimeParticleSystemState::RuntimeParticleSystemState(int fixedTickHz) {
    reset(fixedTickHz);
}

bool RuntimeParticleSystemState::moveSystem(std::uint64_t id,
    const RuntimeParticlePoint& origin, const std::string& owner, int screenOffsetY,
    const RuntimeParticleAirbornePresentation& presentation,
    const RuntimeParticleSpriteAttachment& spriteAttachment) {
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)
        || std::abs(origin.x) > kMaximumRenderCoordinate || std::abs(origin.y) > kMaximumRenderCoordinate
        || std::abs(origin.z) > kMaximumRenderCoordinate
        || screenOffsetY < -4096 || screenOffsetY > 4096
        || !validParticlePresentation(presentation)
        || !validParticleAttachment(spriteAttachment)) return false;
    for (auto& system : systems_) {
        if (system.id != id || !system.emitting) continue;
        system.origin = origin;
        system.screenOffsetY = screenOffsetY;
        system.presentation = presentation;
        system.spriteAttachment = spriteAttachment;
        if (system.owner != owner) {
            system.owner = owner;
            // Keep linked provenance consistent for captures and save/load.
            // Existing puffs retain their world positions and animation ages.
            for (auto& particle : particles_) {
                if (particle.systemInstanceId == id) particle.owner = owner;
            }
        }
        return true;
    }
    return false;
}

void RuntimeParticleSystemState::reset(int fixedTickHz) {
    fixedTickHz_ = std::max(1, fixedTickHz);
    clear();
}

int RuntimeParticleSystemState::convertRuleFrames(int authoredFrames) const {
    if (authoredFrames <= 0) {
        return 0;
    }
    const long double converted =
        static_cast<long double>(authoredFrames)
        * static_cast<long double>(std::max(1, fixedTickHz_))
        / 15.0L;
    return static_cast<int>(std::clamp<long double>(
        std::round(converted),
        1.0L,
        static_cast<long double>(std::numeric_limits<int>::max())
    ));
}

void RuntimeParticleSystemState::clear() {
    systems_.clear();
    particles_.clear();
    currentTick_ = 0;
    nextSystemId_ = 1;
    nextParticleId_ = 1;
}

bool RuntimeParticleSystemState::alignEmptyStateTick(int hostTick) {
    if (!systems_.empty()
        || !particles_.empty()
        || hostTick < 0
        || hostTick < currentTick_
        || hostTick == std::numeric_limits<int>::max()) {
        return false;
    }

    currentTick_ = hostTick;
    return true;
}

std::uint64_t RuntimeParticleSystemState::spawnParticleLinked(
    const RulesDatabase& rules,
    const std::string& particleTypeId,
    const RuntimeParticleSpawnContext& context,
    std::uint64_t systemInstanceId,
    std::uint64_t seed,
    std::string* errorMessage
) {
    if (context.screenOffsetY < -4096 || context.screenOffsetY > 4096
        || !validParticlePresentation(context.presentation)
        || !validParticleAttachment(context.spriteAttachment)) {
        if (errorMessage != nullptr) *errorMessage = "runtime particle emission presentation is invalid";
        return 0;
    }
    const ParticleType* type = rules.findParticleType(particleTypeId);
    if (type == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "unknown particle type [" + particleTypeId + "]";
        }
        return 0;
    }
    if (particles_.size() >= runtimeParticleMaximumParticles) {
        if (errorMessage != nullptr) {
            *errorMessage = "runtime particle instance limit was reached";
        }
        return 0;
    }
    if (nextParticleId_ == 0
        || nextParticleId_ == std::numeric_limits<std::uint64_t>::max()) {
        if (errorMessage != nullptr) {
            *errorMessage = "runtime particle id space was exhausted";
        }
        return 0;
    }

    RuntimeParticleInstance particle;
    particle.id = nextParticleId_++;
    particle.systemInstanceId = systemInstanceId;
    particle.typeId = type->id;
    particle.sourceObjectId = context.sourceObjectId;
    particle.owner = context.owner;
    particle.sourceTag = context.sourceTag;
    particle.position = context.position;
    particle.screenOffsetY = context.screenOffsetY;
    particle.presentation = context.presentation;
    particle.spriteAttachment = context.spriteAttachment;
    particle.speed = type->velocity;
    particle.ageTicks = 0;
    particle.remainingLifetime = type->maxLifetime > 0
        ? convertRuleFrames(type->maxLifetime)
        : -1;
    particle.damageCountdown = convertRuleFrames(type->maxDamageDelay);
    particle.stateAI = type->startStateAI;
    particle.stateAdvanceCountdown = type->stateAIAdvance > 0
        ? convertRuleFrames(type->stateAIAdvance)
        : 0;
    particle.translucency = type->translucency;
    particle.rngState = mixedSeed(
        seed,
        type->id,
        particle.id,
        context.sourceObjectId
    );

    if (type->behavior == ParticleBehavior::Spark) {
        particle.velocity.x = static_cast<double>(
            randomSigned(particle.rngState, std::abs(type->xVelocity))
        );
        particle.velocity.y = static_cast<double>(
            randomSigned(particle.rngState, std::abs(type->yVelocity))
        );
        particle.velocity.z = static_cast<double>(type->minZVelocity);
        if (type->zVelocityRange > 0) {
            particle.velocity.z += static_cast<double>(
                nextRandom(particle.rngState)
                % static_cast<std::uint64_t>(type->zVelocityRange + 1)
            );
        }
        particle.speed = vectorLength(particle.velocity);
    } else {
        const bool gas = type->behavior == ParticleBehavior::Gas
            || type->behavior == ParticleBehavior::WeakGas;
        particle.velocity = gas && context.inheritGasVelocity && type->velocity <= 0.0
            ? context.direction : normalizedDirection(context.direction, type->velocity);
        particle.speed = vectorLength(particle.velocity);
    }

    particles_.push_back(std::move(particle));
    return particles_.back().id;
}

std::uint64_t RuntimeParticleSystemState::spawnParticle(
    const RulesDatabase& rules,
    const std::string& particleTypeId,
    const RuntimeParticleSpawnContext& context,
    std::string* errorMessage
) {
    return spawnParticleLinked(
        rules,
        particleTypeId,
        context,
        0,
        context.seed,
        errorMessage
    );
}

std::uint64_t RuntimeParticleSystemState::spawnSystem(
    const RulesDatabase& rules,
    const std::string& particleSystemTypeId,
    const RuntimeParticleSpawnContext& context,
    std::string* errorMessage
) {
    if (context.screenOffsetY < -4096 || context.screenOffsetY > 4096
        || !validParticlePresentation(context.presentation)
        || !validParticleAttachment(context.spriteAttachment)) {
        if (errorMessage != nullptr) *errorMessage = "runtime particle-system emission presentation is invalid";
        return 0;
    }
    const ParticleSystemType* type = rules.findParticleSystemType(
        particleSystemTypeId
    );
    if (type == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "unknown particle system type ["
                + particleSystemTypeId + "]";
        }
        return 0;
    }
    if (type->holdsWhat.empty()
        || rules.findParticleType(type->holdsWhat) == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "particle system [" + type->id
                + "] has no valid HoldsWhat particle";
        }
        return 0;
    }

    const bool railgunLine = type->behavior == ParticleBehavior::Railgun;
    const bool emits = !railgunLine && (type->spawns
        || type->behavior == ParticleBehavior::Spark);
    const bool seedImmediately = !railgunLine
        && (behaviorSeedsOnce(type->behavior) || !emits);
    if (systems_.size() >= runtimeParticleMaximumSystems) {
        if (errorMessage != nullptr) {
            *errorMessage = "runtime particle system instance limit was reached";
        }
        return 0;
    }
    if ((seedImmediately || (railgunLine && type->particlesPerCoord > 0.0))
        && particles_.size() >= runtimeParticleMaximumParticles) {
        if (errorMessage != nullptr) {
            *errorMessage = "runtime particle instance limit was reached";
        }
        return 0;
    }
    if (nextSystemId_ == 0
        || nextSystemId_ == std::numeric_limits<std::uint64_t>::max()) {
        if (errorMessage != nullptr) {
            *errorMessage = "runtime particle system id space was exhausted";
        }
        return 0;
    }

    RuntimeParticleSystemInstance system;
    system.id = nextSystemId_++;
    system.typeId = type->id;
    system.sourceObjectId = context.sourceObjectId;
    system.owner = context.owner;
    system.sourceTag = context.sourceTag;
    system.origin = context.position;
    system.screenOffsetY = context.screenOffsetY;
    system.presentation = context.presentation;
    system.spriteAttachment = context.spriteAttachment;
    system.direction = context.direction;
    if (!hasDirection(system.direction)) {
        system.direction = {
            type->spawnDirection.x,
            type->spawnDirection.y,
            type->spawnDirection.z
        };
    }
    int authoredLifetime = context.systemLifetimeOverrideFrames > 0
        ? context.systemLifetimeOverrideFrames
        : type->lifetime;
    if (railgunLine && type->laser) {
        authoredLifetime = std::max(
            authoredLifetime,
            kRailgunLaserLifetimeFrames
        );
    }
    system.remainingLifetime = authoredLifetime > 0
        ? convertRuleFrames(authoredLifetime)
        : -1;
    system.spawnCountdown = 0;
    system.rngState = mixedSeed(
        context.seed,
        type->id,
        system.id,
        context.sourceObjectId
    );
    system.emitting = emits;
    systems_.push_back(system);

    if (railgunLine) {
        RuntimeParticleSystemInstance& liveSystem = systems_.back();
        const double pathLength = vectorLength(liveSystem.direction);
        const std::size_t available = runtimeParticleMaximumParticles
            - particles_.size();
        std::size_t requested = 0;
        if (pathLength > 1.0e-12 && type->particlesPerCoord > 0.0) {
            requested = static_cast<std::size_t>(std::clamp<long double>(
                std::floor(
                    static_cast<long double>(pathLength)
                    * static_cast<long double>(type->particlesPerCoord)
                ),
                1.0L,
                static_cast<long double>(
                    runtimeParticleMaximumParticlesPerSpawn
                )
            ));
        }
        if (type->particleCap > 0) {
            requested = std::min(
                requested,
                static_cast<std::size_t>(type->particleCap)
            );
        }
        requested = std::min(requested, available);

        const RuntimeParticlePoint axis = normalizedDirection(
            liveSystem.direction,
            1.0
        );
        RuntimeParticlePoint basisU;
        RuntimeParticlePoint basisV;
        perpendicularBasis(axis, basisU, basisV);
        const double spacing = type->particlesPerCoord > 0.0
            ? 1.0 / type->particlesPerCoord
            : pathLength;
        for (std::size_t index = 0; index < requested; ++index) {
            const double distance = std::min(
                pathLength,
                (static_cast<double>(index) + 0.5) * spacing
            );
            const double phase = distance * type->spiralDeltaPerCoord;
            const double movementPhase = phase
                + randomCentered(
                    liveSystem.rngState,
                    type->movementPerturbationCoefficient
                );
            const double radius = type->spiralRadius;
            RuntimeParticleSpawnContext particleContext = context;
            particleContext.position = {
                liveSystem.origin.x + (axis.x * distance)
                    + (basisU.x * std::cos(movementPhase) * radius)
                    + (basisV.x * std::sin(movementPhase) * radius),
                liveSystem.origin.y + (axis.y * distance)
                    + (basisU.y * std::cos(movementPhase) * radius)
                    + (basisV.y * std::sin(movementPhase) * radius),
                liveSystem.origin.z + (axis.z * distance)
                    + (basisU.z * std::cos(movementPhase) * radius)
                    + (basisV.z * std::sin(movementPhase) * radius)
            };
            particleContext.position.x += randomCentered(
                liveSystem.rngState,
                type->positionPerturbationCoefficient
            );
            particleContext.position.y += randomCentered(
                liveSystem.rngState,
                type->positionPerturbationCoefficient
            );
            particleContext.position.z += randomCentered(
                liveSystem.rngState,
                type->positionPerturbationCoefficient
            );
            particleContext.direction = axis;
            const std::uint64_t particleSeed = nextRandom(
                liveSystem.rngState
            );
            const std::uint64_t particleId = spawnParticleLinked(
                rules,
                type->holdsWhat,
                particleContext,
                liveSystem.id,
                particleSeed,
                nullptr
            );
            if (particleId == 0) {
                break;
            }
            RuntimeParticleInstance& particle = particles_.back();
            particle.velocity.x += randomCentered(
                particle.rngState,
                type->velocityPerturbationCoefficient
            );
            particle.velocity.y += randomCentered(
                particle.rngState,
                type->velocityPerturbationCoefficient
            );
            particle.velocity.z += randomCentered(
                particle.rngState,
                type->velocityPerturbationCoefficient
            );
            particle.speed = vectorLength(particle.velocity);
            ++liveSystem.spawnSerial;
        }
    } else if (seedImmediately) {
        RuntimeParticleSpawnContext particleContext = context;
        particleContext.direction = system.direction;
        const std::uint64_t particleSeed = nextRandom(
            systems_.back().rngState
        );
        ++systems_.back().spawnSerial;
        std::string particleError;
        if (spawnParticleLinked(
                rules,
                type->holdsWhat,
                particleContext,
                system.id,
                particleSeed,
                &particleError
            ) == 0) {
            systems_.pop_back();
            if (errorMessage != nullptr) {
                *errorMessage = particleError;
            }
            return 0;
        }
    }
    return system.id;
}

RuntimeParticleSpawnResult RuntimeParticleSystemState::spawnFromWarhead(
    const RulesDatabase& rules,
    const WarheadType& warhead,
    const RuntimeParticleSpawnContext& context
) {
    RuntimeParticleSpawnResult result;
    std::unordered_set<std::string> spawnedReferences;
    const auto spawnSystemReference = [&](const std::string& authoredId) {
        if (authoredId.empty()) {
            return;
        }
        const std::string key = lowerAscii(authoredId);
        if (!spawnedReferences.insert("system:" + key).second) {
            return;
        }
        std::string error;
        const std::uint64_t id = spawnSystem(rules, authoredId, context, &error);
        if (id == 0) {
            result.warnings.push_back(std::move(error));
        } else {
            result.systemInstanceIds.push_back(id);
        }
    };

    spawnSystemReference(warhead.particleSystem);
    if (!warhead.particle.empty()) {
        if (rules.findParticleSystemType(warhead.particle) != nullptr) {
            spawnSystemReference(warhead.particle);
        } else if (rules.findParticleType(warhead.particle) != nullptr) {
            const std::string key = lowerAscii(warhead.particle);
            if (spawnedReferences.insert("particle:" + key).second) {
                std::string error;
                const std::uint64_t id = spawnParticle(
                    rules,
                    warhead.particle,
                    context,
                    &error
                );
                if (id == 0) {
                    result.warnings.push_back(std::move(error));
                } else {
                    result.particleInstanceIds.push_back(id);
                }
            }
        } else {
            result.warnings.push_back(
                "warhead [" + warhead.id
                + "] references unknown Particle [" + warhead.particle + "]"
            );
        }
    }
    return result;
}

RuntimeParticleSpawnResult RuntimeParticleSystemState::spawnFromAnimation(
    const RulesDatabase& rules,
    const ArtType& animation,
    const RuntimeParticleSpawnContext& context
) {
    RuntimeParticleSpawnResult result;
    if (animation.spawnsParticle.empty() || animation.numParticles <= 0) {
        return result;
    }
    const ParticleType* type = rules.findParticleType(animation.spawnsParticle);
    if (type == nullptr) {
        result.warnings.push_back(
            "animation [" + animation.id
            + "] references unknown SpawnsParticle ["
            + animation.spawnsParticle + "]"
        );
        return result;
    }
    const std::size_t authoredCount = static_cast<std::size_t>(
        animation.numParticles
    );
    const std::size_t available = particles_.size()
            >= runtimeParticleMaximumParticles
        ? 0U
        : runtimeParticleMaximumParticles - particles_.size();
    const std::size_t spawnCount = std::min({
        authoredCount,
        runtimeParticleMaximumParticlesPerSpawn,
        available
    });
    if (spawnCount < authoredCount) {
        result.warnings.push_back(
            "animation [" + animation.id + "] NumParticles truncated from "
            + std::to_string(authoredCount) + " to "
            + std::to_string(spawnCount)
            + " by runtime particle limits"
        );
    }
    result.particleInstanceIds.reserve(spawnCount);
    const bool virusGas = lowerAscii(type->warhead) == "virusgas";
    std::uint64_t driftSeed = mixedSeed(context.seed, animation.id, nextParticleId_, context.sourceObjectId);
    const std::size_t driftRotation = nextRandom(driftSeed) % 12U;
    for (std::size_t index = 0; index < spawnCount; ++index) {
        RuntimeParticleSpawnContext particleContext = context;
        if (type->behavior == ParticleBehavior::Gas || type->behavior == ParticleBehavior::WeakGas) {
            // Each virus cloud varies within its own sector, so a rupture has
            // three distinct directions without repeating a rigid triangle.
            // The integer PRNG and fixed vectors preserve deterministic replay.
            constexpr RuntimeParticlePoint directions[] = {
                {1,0,0}, {0.8660254037844386,0.5,0}, {0.5,0.8660254037844386,0},
                {0,1,0}, {-0.5,0.8660254037844386,0}, {-0.8660254037844386,0.5,0},
                {-1,0,0}, {-0.8660254037844386,-0.5,0}, {-0.5,-0.8660254037844386,0},
                {0,-1,0}, {0.5,-0.8660254037844386,0}, {0.8660254037844386,-0.5,0}
            };
            const std::size_t directionIndex = virusGas && authoredCount == 3U
                ? (driftRotation + index * 4U + nextRandom(driftSeed) % 3U) % 12U
                : (context.seed % 12U + index * 12U / spawnCount) % 12U;
            const auto& direction = directions[directionIndex];
            const double speed = type->velocity > 0.0 ? type->velocity : (virusGas ? 2.0 : 4.0);
            particleContext.direction = {direction.x * speed, direction.y * speed, 0.0};
            particleContext.inheritGasVelocity = true;
        }
        particleContext.seed ^= static_cast<std::uint64_t>(index + 1U)
            * 0x9e3779b97f4a7c15ULL;
        std::string error;
        const std::uint64_t id = spawnParticle(
            rules,
            animation.spawnsParticle,
            particleContext,
            &error
        );
        if (id == 0) {
            result.warnings.push_back(std::move(error));
        } else {
            result.particleInstanceIds.push_back(id);
        }
    }
    return result;
}

std::vector<RuntimeParticleDamageEvent> RuntimeParticleSystemState::tick(
    const RulesDatabase& rules
) {
    if (currentTick_ == std::numeric_limits<int>::max()) {
        return {};
    }
    ++currentTick_;
    if (systems_.empty() && particles_.empty()) {
        return {};
    }
    std::vector<RuntimeParticleDamageEvent> damageEvents;
    std::vector<PendingParticleSpawn> pendingSystemSpawns;
    // A flat, reusable lookup replaces three per-tick node-based hash tables.
    // IDs are monotonic but may be sparse after long battles and save loads.
    std::size_t slotCount = 8U;
    while (slotCount < systems_.size() * 2U) {
        slotCount *= 2U;
    }
    tickSystemSlots_.assign(slotCount, 0U);
    tickSystems_.resize(systems_.size());
    const auto initialSlot = [slotCount](std::uint64_t id) {
        id ^= id >> 30U;
        id *= 0xbf58476d1ce4e5b9ULL;
        id ^= id >> 27U;
        id *= 0x94d049bb133111ebULL;
        id ^= id >> 31U;
        return static_cast<std::size_t>(id) & (slotCount - 1U);
    };
    for (std::size_t index = 0; index < systems_.size(); ++index) {
        const RuntimeParticleSystemInstance& system = systems_[index];
        tickSystems_[index] = {
            system.id, rules.findParticleSystemType(system.typeId), 0
        };
        std::size_t slot = initialSlot(system.id);
        while (tickSystemSlots_[slot] != 0U) {
            slot = (slot + 1U) & (slotCount - 1U);
        }
        tickSystemSlots_[slot] = index + 1U;
    }
    const auto findTickSystem = [&](std::uint64_t id) -> TickSystemData* {
        if (id == 0) {
            return nullptr;
        }
        std::size_t slot = initialSlot(id);
        while (tickSystemSlots_[slot] != 0U) {
            TickSystemData& data = tickSystems_[tickSystemSlots_[slot] - 1U];
            if (data.id == id) {
                return &data;
            }
            slot = (slot + 1U) & (slotCount - 1U);
        }
        return nullptr;
    };
    for (const RuntimeParticleInstance& particle : particles_) {
        if (TickSystemData* system = findTickSystem(particle.systemInstanceId)) {
            ++system->liveParticles;
        }
    }

    for (std::size_t index = 0; index < systems_.size(); ++index) {
        RuntimeParticleSystemInstance& system = systems_[index];
        const ParticleSystemType* type = tickSystems_[index].type;
        if (type == nullptr) {
            system.emitting = false;
            continue;
        }
        if (system.ageTicks < std::numeric_limits<int>::max()) {
            ++system.ageTicks;
        }
        if (system.emitting) {
            if (system.spawnCountdown > 0) {
                --system.spawnCountdown;
            }
            if (system.spawnCountdown <= 0) {
                const int liveCount = tickSystems_[index].liveParticles;
                const bool underAuthoredCap = type->particleCap <= 0
                    || liveCount < type->particleCap;
                const bool underRuntimeCap = particles_.size()
                    + pendingSystemSpawns.size()
                    < runtimeParticleMaximumParticles;
                bool probabilityPassed = true;
                if (type->behavior == ParticleBehavior::Spark) {
                    probabilityPassed = type->spawnSparkPercentage > 0.0
                        && randomUnit(system.rngState)
                            < type->spawnSparkPercentage;
                }
                if (underAuthoredCap
                    && underRuntimeCap
                    && probabilityPassed) {
                    PendingParticleSpawn pending;
                    pending.typeId = type->holdsWhat;
                    pending.context.sourceObjectId = system.sourceObjectId;
                    pending.context.owner = system.owner;
                    pending.context.sourceTag = system.sourceTag;
                    pending.context.position = system.origin;
                    pending.context.screenOffsetY = system.screenOffsetY;
                    pending.context.presentation = system.presentation;
                    pending.context.spriteAttachment = system.spriteAttachment;
                    pending.context.direction = system.direction;
                    pending.context.seed = nextRandom(system.rngState);
                    pending.systemInstanceId = system.id;
                    pending.seed = pending.context.seed;
                    if (type->behavior == ParticleBehavior::Smoke
                        && type->spawnTranslucencyCutoff > 0.0) {
                        const long double ordinal =
                            static_cast<long double>(system.spawnSerial)
                            + 1.0L;
                        if (ordinal > type->spawnTranslucencyCutoff) {
                            const long double fadeSpan = type->particleCap > 0
                                ? std::max(
                                    1.0L,
                                    static_cast<long double>(type->particleCap)
                                        - type->spawnTranslucencyCutoff
                                )
                                : std::max(
                                    1.0L,
                                    static_cast<long double>(
                                        type->spawnTranslucencyCutoff
                                    )
                                );
                            const long double translucency = std::clamp(
                                ((ordinal
                                    - type->spawnTranslucencyCutoff)
                                    / fadeSpan) * 75.0L,
                                0.0L,
                                75.0L
                            );
                            pending.minimumTranslucency = static_cast<int>(
                                std::lround(translucency)
                            );
                        }
                    }
                    if (type->behavior == ParticleBehavior::Smoke
                        && type->spawnRadius > 0) {
                        int offsetX = 0;
                        int offsetY = 0;
                        do {
                            offsetX = randomSigned(
                                system.rngState,
                                type->spawnRadius
                            );
                            offsetY = randomSigned(
                                system.rngState,
                                type->spawnRadius
                            );
                        } while (
                            static_cast<std::int64_t>(offsetX) * offsetX
                                + static_cast<std::int64_t>(offsetY) * offsetY
                            > static_cast<std::int64_t>(type->spawnRadius)
                                * type->spawnRadius
                        );
                        pending.context.position.x += offsetX;
                        pending.context.position.y += offsetY;
                    }
                    pendingSystemSpawns.push_back(std::move(pending));
                    ++system.spawnSerial;
                }
                const int baseSpawnFrames = std::max(
                    1,
                    type->behavior == ParticleBehavior::Spark
                        ? type->sparkSpawnFrames
                        : type->spawnFrames
                );
                long double authoredDelay = static_cast<long double>(
                    baseSpawnFrames
                );
                if (type->behavior == ParticleBehavior::Smoke
                    && type->spawnCutoff > 0.0
                    && static_cast<long double>(system.spawnSerial)
                        > type->spawnCutoff) {
                    authoredDelay *= 1.0L
                        + ((static_cast<long double>(system.spawnSerial)
                                - type->spawnCutoff)
                            * type->slowdown);
                }
                authoredDelay = std::clamp(
                    authoredDelay,
                    1.0L,
                    static_cast<long double>(
                        std::numeric_limits<int>::max()
                    )
                );
                system.spawnCountdown = std::max(
                    1,
                    convertRuleFrames(static_cast<int>(std::llround(
                        authoredDelay
                    )))
                );
            }
        }
        if (system.remainingLifetime > 0) {
            --system.remainingLifetime;
            if (system.remainingLifetime == 0) {
                system.emitting = false;
            }
        }
    }

    for (const PendingParticleSpawn& pending : pendingSystemSpawns) {
        const std::uint64_t spawnedId = spawnParticleLinked(
            rules,
            pending.typeId,
            pending.context,
            pending.systemInstanceId,
            pending.seed,
            nullptr
        );
        if (spawnedId != 0 && !particles_.empty()) {
            particles_.back().translucency = std::max(
                particles_.back().translucency,
                pending.minimumTranslucency
            );
        }
    }

    for (TickSystemData& system : tickSystems_) {
        system.liveParticles = 0;
    }
    std::size_t retainedParticleCount = 0;
    std::vector<PendingParticleSpawn> chainedSpawns;
    const ParticleType* previousParticleType = nullptr;
    for (RuntimeParticleInstance& particle : particles_) {
        // Emission batches usually share a type. Reuse the resolved rule for
        // adjacent particles instead of canonicalizing the same ID each time.
        const ParticleType* type = previousParticleType != nullptr
                && previousParticleType->id == particle.typeId
            ? previousParticleType : rules.findParticleType(particle.typeId);
        previousParticleType = type;
        if (type == nullptr) {
            continue;
        }
        if (particle.ageTicks < std::numeric_limits<int>::max()) {
            ++particle.ageTicks;
        }
        TickSystemData* controllingSystem = findTickSystem(
            particle.systemInstanceId
        );
        const ParticleSystemType* controllingSystemType = controllingSystem
            ? controllingSystem->type : nullptr;
        const bool gas = type->behavior == ParticleBehavior::Gas
            || type->behavior == ParticleBehavior::WeakGas;
        // Unpowered gas spreads locally and settles instead of travelling
        // across the map for the entire MaxEC lifetime. Authored motion wins.
        const double deceleration = gas && type->velocity <= 0.0 && type->deacceleration <= 0.0
            ? (lowerAscii(type->warhead) == "virusgas" ? 0.005 : 0.02) : type->deacceleration;
        int remainingAuthoredUnits = kAuthoredTicksPerSecond;
        while (remainingAuthoredUnits > 0) {
            const int unitsToBoundary = fixedTickHz_
                - particle.authoredFrameProgress;
            const int unitsThisStep = std::min(
                remainingAuthoredUnits,
                unitsToBoundary
            );
            const double step = static_cast<double>(unitsThisStep)
                / static_cast<double>(fixedTickHz_);
            particle.position.x += particle.velocity.x * step;
            particle.position.y += particle.velocity.y * step;
            particle.position.z += particle.velocity.z * step;
            particle.authoredFrameProgress += unitsThisStep;
            remainingAuthoredUnits -= unitsThisStep;

            if (particle.authoredFrameProgress >= fixedTickHz_) {
                particle.authoredFrameProgress = 0;
                const double oldSpeed = vectorLength(particle.velocity);
                if (oldSpeed > 1.0e-12 && deceleration > 0.0) {
                    const double newSpeed = std::max(
                        0.0,
                        oldSpeed - deceleration
                    );
                    const double scale = newSpeed / oldSpeed;
                    particle.velocity.x *= scale;
                    particle.velocity.y *= scale;
                    particle.velocity.z *= scale;
                }
                if (controllingSystemType != nullptr
                    && controllingSystemType->behavior
                        == ParticleBehavior::Railgun
                    && controllingSystemType
                        ->movementPerturbationCoefficient > 0.0) {
                    const double coefficient = controllingSystemType
                        ->movementPerturbationCoefficient;
                    particle.velocity.x += randomCentered(
                        particle.rngState,
                        coefficient
                    );
                    particle.velocity.y += randomCentered(
                        particle.rngState,
                        coefficient
                    );
                    particle.velocity.z += randomCentered(
                        particle.rngState,
                        coefficient
                    );
                }
                if (type->windEffect > 0
                    && (type->behavior == ParticleBehavior::Smoke
                        || type->behavior == ParticleBehavior::Gas
                        || type->behavior == ParticleBehavior::WeakGas)) {
                    const int authoredFrame = std::max(
                        1,
                        static_cast<int>(
                            ((static_cast<long long>(
                                std::max(0, particle.ageTicks - 1)
                            ) * kAuthoredTicksPerSecond)
                                + (kAuthoredTicksPerSecond
                                    - remainingAuthoredUnits))
                            / std::max(1, fixedTickHz_)
                        )
                    );
                    const bool gas = type->behavior == ParticleBehavior::Gas
                        || type->behavior == ParticleBehavior::WeakGas;
                    const int interval = gas
                        ? 1
                        : std::max(1, 10 / type->windEffect);
                    if ((authoredFrame % interval) == 0) {
                        const RuntimeParticlePoint offset = windOffset(
                            type->behavior,
                            rules.getWindDirection(),
                            type->windEffect
                        );
                        particle.position.x += offset.x;
                        particle.position.y += offset.y;
                    }
                }
            }
        }
        particle.speed = vectorLength(particle.velocity);

        bool expired = false;
        if (type->stateAIAdvance > 0) {
            if (particle.stateAdvanceCountdown > 0) {
                --particle.stateAdvanceCountdown;
            }
            if (particle.stateAdvanceCountdown <= 0) {
                if (particle.stateAI < std::numeric_limits<int>::max()) {
                    ++particle.stateAI;
                }
                particle.stateAdvanceCountdown = convertRuleFrames(
                    type->stateAIAdvance
                );
                if (type->endStateAI > 0
                    && particle.stateAI > type->endStateAI) {
                    if (type->deleteOnStateLimit) {
                        expired = true;
                    } else {
                        particle.stateAI = type->endStateAI;
                    }
                }
            }
        }

        if (type->maxDamageDelay > 0
            && type->damage != 0
            && !type->warhead.empty()
            && behaviorCanDamage(type->behavior)) {
            if (particle.damageCountdown > 0) {
                --particle.damageCountdown;
            }
            if (particle.damageCountdown <= 0) {
                const bool stateCanDamage = type->finalDamageState <= 0
                    || particle.stateAI <= type->finalDamageState;
                if (stateCanDamage
                    && rules.findWarheadType(type->warhead) != nullptr) {
                    RuntimeParticleDamageEvent event;
                    event.tick = currentTick_;
                    event.systemInstanceId = particle.systemInstanceId;
                    event.particleInstanceId = particle.id;
                    event.sourceObjectId = particle.sourceObjectId;
                    event.owner = particle.owner;
                    event.sourceTag = particle.sourceTag;
                    event.particleTypeId = particle.typeId;
                    event.warheadId = type->warhead;
                    event.position = particle.position;
                    event.damage = type->damage;
                    event.radius = type->radius;
                    damageEvents.push_back(std::move(event));
                }
                particle.damageCountdown = convertRuleFrames(
                    type->maxDamageDelay
                );
            }
        }

        if (particle.remainingLifetime > 0) {
            --particle.remainingLifetime;
            if (particle.remainingLifetime == 0) {
                expired = true;
            }
        }
        if (!expired) {
            if (controllingSystem != nullptr) {
                ++controllingSystem->liveParticles;
            }
            // Stable compaction avoids allocating one hash node for each
            // expiring particle during a large explosion. Never self-move
            // the live entry: its strings must survive unchanged.
            if (&particle != &particles_[retainedParticleCount]) {
                particles_[retainedParticleCount] = std::move(particle);
            }
            ++retainedParticleCount;
            continue;
        }

        if (!type->nextParticle.empty()
            && rules.findParticleType(type->nextParticle) != nullptr) {
            PendingParticleSpawn pending;
            pending.typeId = type->nextParticle;
            pending.context.sourceObjectId = particle.sourceObjectId;
            pending.context.owner = particle.owner;
            pending.context.sourceTag = particle.sourceTag;
            pending.context.position = particle.position;
            pending.context.screenOffsetY = particle.screenOffsetY;
            pending.context.presentation = particle.presentation;
            pending.context.spriteAttachment = particle.spriteAttachment;
            pending.context.position.x += type->nextParticleOffset.x;
            pending.context.position.y += type->nextParticleOffset.y;
            pending.context.position.z += type->nextParticleOffset.z;
            pending.context.direction = particle.velocity;
            pending.context.inheritGasVelocity = gas;
            pending.context.seed = nextRandom(particle.rngState);
            pending.systemInstanceId = particle.systemInstanceId;
            pending.seed = pending.context.seed;
            chainedSpawns.push_back(std::move(pending));
        }
    }

    particles_.resize(retainedParticleCount);
    for (const PendingParticleSpawn& pending : chainedSpawns) {
        const std::uint64_t spawnedId = spawnParticleLinked(
            rules,
            pending.typeId,
            pending.context,
            pending.systemInstanceId,
            pending.seed,
            nullptr
        );
        if (spawnedId != 0) {
            if (TickSystemData* system = findTickSystem(pending.systemInstanceId)) {
                ++system->liveParticles;
            }
        }
    }

    systems_.erase(
        std::remove_if(
            systems_.begin(),
            systems_.end(),
            [&findTickSystem](
                const RuntimeParticleSystemInstance& system
            ) {
                return !system.emitting
                    && system.remainingLifetime <= 0
                    && findTickSystem(system.id)->liveParticles == 0;
            }
        ),
        systems_.end()
    );
    tickSystems_.clear();
    tickSystemSlots_.clear();
    return damageEvents;
}

bool RuntimeParticleSystemState::stopSystem(
    std::uint64_t systemInstanceId
) {
    bool found = false;
    for (RuntimeParticleSystemInstance& system : systems_) {
        if (system.id == systemInstanceId) {
            system.emitting = false;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    const bool hasParticles = std::any_of(
        particles_.begin(),
        particles_.end(),
        [systemInstanceId](const RuntimeParticleInstance& particle) {
            return particle.systemInstanceId == systemInstanceId;
        }
    );
    const auto foundSystem = std::find_if(
        systems_.begin(),
        systems_.end(),
        [systemInstanceId](const RuntimeParticleSystemInstance& system) {
            return system.id == systemInstanceId;
        }
    );
    if (!hasParticles
        && (foundSystem == systems_.end()
            || foundSystem->remainingLifetime <= 0)) {
        systems_.erase(
            std::remove_if(
                systems_.begin(),
                systems_.end(),
                [systemInstanceId](
                    const RuntimeParticleSystemInstance& system
                ) {
                    return system.id == systemInstanceId;
                }
            ),
            systems_.end()
        );
    }
    return true;
}

std::size_t RuntimeParticleSystemState::stopSystemsForSource(
    int sourceObjectId
) {
    std::size_t stopped = 0;
    for (RuntimeParticleSystemInstance& system : systems_) {
        if (system.sourceObjectId == sourceObjectId && system.emitting) {
            system.emitting = false;
            ++stopped;
        }
    }
    if (stopped != 0U) {
        std::unordered_set<std::uint64_t> systemsWithParticles;
        systemsWithParticles.reserve(particles_.size());
        for (const RuntimeParticleInstance& particle : particles_) {
            systemsWithParticles.insert(particle.systemInstanceId);
        }
        systems_.erase(
            std::remove_if(
                systems_.begin(),
                systems_.end(),
                [&](const RuntimeParticleSystemInstance& system) {
                    return !system.emitting
                        && system.remainingLifetime <= 0
                        && !systemsWithParticles.contains(system.id);
                }
            ),
            systems_.end()
        );
    }
    return stopped;
}

RuntimeParticleReconcileReport RuntimeParticleSystemState::reconcile(
    const RulesDatabase& rules
) {
    RuntimeParticleReconcileReport report;
    const std::uint64_t preservedNextSystemId = nextSystemId_;
    const std::uint64_t preservedNextParticleId = nextParticleId_;
    systems_.erase(
        std::remove_if(
            systems_.begin(),
            systems_.end(),
            [&](RuntimeParticleSystemInstance& system) {
                const ParticleSystemType* type =
                    rules.findParticleSystemType(system.typeId);
                if (type == nullptr) {
                    ++report.removedSystems;
                    return true;
                }
                if (type->holdsWhat.empty()
                    || rules.findParticleType(type->holdsWhat) == nullptr) {
                    if (system.emitting) {
                        system.emitting = false;
                        ++report.stoppedSystems;
                    }
                }
                return false;
            }
        ),
        systems_.end()
    );

    std::unordered_set<std::uint64_t> liveSystemIds;
    for (const RuntimeParticleSystemInstance& system : systems_) {
        liveSystemIds.insert(system.id);
    }
    particles_.erase(
        std::remove_if(
            particles_.begin(),
            particles_.end(),
            [&](const RuntimeParticleInstance& particle) {
                const bool missingType =
                    rules.findParticleType(particle.typeId) == nullptr;
                const bool missingSystem = particle.systemInstanceId != 0
                    && !liveSystemIds.contains(particle.systemInstanceId);
                if (missingType || missingSystem) {
                    ++report.removedParticles;
                    return true;
                }
                return false;
            }
        ),
        particles_.end()
    );

    std::unordered_set<std::uint64_t> systemsWithParticles;
    for (const RuntimeParticleInstance& particle : particles_) {
        if (particle.systemInstanceId != 0) {
            systemsWithParticles.insert(particle.systemInstanceId);
        }
    }
    systems_.erase(
        std::remove_if(
            systems_.begin(),
            systems_.end(),
            [&](const RuntimeParticleSystemInstance& system) {
                if (!system.emitting
                    && system.remainingLifetime <= 0
                    && !systemsWithParticles.contains(system.id)) {
                    ++report.removedSystems;
                    return true;
                }
                return false;
            }
        ),
        systems_.end()
    );

    std::sort(
        systems_.begin(),
        systems_.end(),
        [](const auto& left, const auto& right) { return left.id < right.id; }
    );
    std::sort(
        particles_.begin(),
        particles_.end(),
        [](const auto& left, const auto& right) { return left.id < right.id; }
    );
    nextSystemId_ = std::max<std::uint64_t>(1, preservedNextSystemId);
    for (const RuntimeParticleSystemInstance& system : systems_) {
        nextSystemId_ = std::max(nextSystemId_, system.id + 1U);
    }
    nextParticleId_ = std::max<std::uint64_t>(1, preservedNextParticleId);
    for (const RuntimeParticleInstance& particle : particles_) {
        nextParticleId_ = std::max(nextParticleId_, particle.id + 1U);
    }
    return report;
}

void RuntimeParticleSystemState::save(std::ostream& output) const {
    saveInternal(output, false);
}

void RuntimeParticleSystemState::saveInternal(std::ostream& output, bool authoritative) const {
    std::ostringstream saved;
    saved.imbue(std::locale::classic());
    saved << std::setprecision(std::numeric_limits<double>::max_digits10);
    saved << "[ParticleRuntime]\n";
    saved << "Version=1\n";
    saved << "Tick=" << currentTick_ << "\n";
    saved << "FixedTickHz=" << fixedTickHz_ << "\n";
    saved << "NextSystemId=" << nextSystemId_ << "\n";
    saved << "NextParticleId=" << nextParticleId_ << "\n";
    saved << "SystemCount=" << systems_.size() << "\n";
    saved << "ParticleCount=" << particles_.size() << "\n\n";

    std::vector<const RuntimeParticleSystemInstance*> sortedSystems;
    sortedSystems.reserve(systems_.size());
    for (const RuntimeParticleSystemInstance& system : systems_) {
        sortedSystems.push_back(&system);
    }
    std::sort(
        sortedSystems.begin(),
        sortedSystems.end(),
        [](const auto* left, const auto* right) { return left->id < right->id; }
    );
    for (std::size_t index = 0; index < sortedSystems.size(); ++index) {
        const RuntimeParticleSystemInstance& system = *sortedSystems[index];
        saved << "[ParticleRuntime.System." << index << "]\n";
        saved << "Id=" << system.id << "\n";
        saved << "TypeHex=" << hexEncode(system.typeId) << "\n";
        saved << "SourceObjectId=" << system.sourceObjectId << "\n";
        saved << "OwnerHex=" << hexEncode(system.owner) << "\n";
        saved << "SourceTagHex=" << hexEncode(system.sourceTag) << "\n";
        saved << "OriginX=" << system.origin.x << "\n";
        saved << "OriginY=" << system.origin.y << "\n";
        saved << "OriginZ=" << system.origin.z << "\n";
        saved << "ScreenOffsetY=" << system.screenOffsetY << "\n";
        saveParticlePresentation(saved, system.presentation, system.spriteAttachment, authoritative);
        saved << "DirectionX=" << system.direction.x << "\n";
        saved << "DirectionY=" << system.direction.y << "\n";
        saved << "DirectionZ=" << system.direction.z << "\n";
        saved << "Age=" << system.ageTicks << "\n";
        saved << "RemainingLifetime=" << system.remainingLifetime << "\n";
        saved << "SpawnCountdown=" << system.spawnCountdown << "\n";
        saved << "SpawnSerial=" << system.spawnSerial << "\n";
        saved << "RngState=" << system.rngState << "\n";
        saved << "Emitting=" << (system.emitting ? "yes" : "no") << "\n\n";
    }

    std::vector<const RuntimeParticleInstance*> sortedParticles;
    sortedParticles.reserve(particles_.size());
    for (const RuntimeParticleInstance& particle : particles_) {
        sortedParticles.push_back(&particle);
    }
    std::sort(
        sortedParticles.begin(),
        sortedParticles.end(),
        [](const auto* left, const auto* right) { return left->id < right->id; }
    );
    for (std::size_t index = 0; index < sortedParticles.size(); ++index) {
        const RuntimeParticleInstance& particle = *sortedParticles[index];
        saved << "[ParticleRuntime.Particle." << index << "]\n";
        saved << "Id=" << particle.id << "\n";
        saved << "SystemId=" << particle.systemInstanceId << "\n";
        saved << "TypeHex=" << hexEncode(particle.typeId) << "\n";
        saved << "SourceObjectId=" << particle.sourceObjectId << "\n";
        saved << "OwnerHex=" << hexEncode(particle.owner) << "\n";
        saved << "SourceTagHex=" << hexEncode(particle.sourceTag) << "\n";
        saved << "PositionX=" << particle.position.x << "\n";
        saved << "PositionY=" << particle.position.y << "\n";
        saved << "PositionZ=" << particle.position.z << "\n";
        saved << "ScreenOffsetY=" << particle.screenOffsetY << "\n";
        saveParticlePresentation(saved, particle.presentation, particle.spriteAttachment, authoritative);
        saved << "VelocityX=" << particle.velocity.x << "\n";
        saved << "VelocityY=" << particle.velocity.y << "\n";
        saved << "VelocityZ=" << particle.velocity.z << "\n";
        saved << "Speed=" << particle.speed << "\n";
        saved << "Age=" << particle.ageTicks << "\n";
        saved << "RemainingLifetime=" << particle.remainingLifetime << "\n";
        saved << "DamageCountdown=" << particle.damageCountdown << "\n";
        saved << "StateAI=" << particle.stateAI << "\n";
        saved << "StateAdvanceCountdown=" << particle.stateAdvanceCountdown << "\n";
        saved << "Translucency=" << particle.translucency << "\n";
        saved << "AuthoredFrameProgress="
              << particle.authoredFrameProgress << "\n";
        saved << "RngState=" << particle.rngState << "\n\n";
    }
    output << saved.str();
}

std::uint64_t RuntimeParticleSystemState::stateHash() const {
    std::ostringstream saved;
    saveInternal(saved, true);
    return hashText(kFnvOffset, saved.str());
}

bool RuntimeParticleSystemState::load(
    const IniFile& ini,
    const RulesDatabase& rules,
    std::string* errorMessage,
    RuntimeParticleReconcileReport* reconcileReport
) {
    if (ini.getSection("ParticleRuntime") == nullptr) {
        return true;
    }
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    const auto value = [&](
        const std::string& section,
        const std::string& key,
        const std::string& fallback = std::string()
    ) {
        return ini.getValue(section, key, fallback);
    };

    int version = 0;
    if (!parseIntStrict(value("ParticleRuntime", "Version", "0"), version)
        || version != 1) {
        return fail("unsupported particle runtime save version");
    }
    int tick = 0;
    int fixedTickHz = 0;
    int systemCount = 0;
    int particleCount = 0;
    std::uint64_t authoredNextSystemId = 0;
    std::uint64_t authoredNextParticleId = 0;
    if (!parseIntStrict(value("ParticleRuntime", "Tick", "0"), tick)
        || tick < 0
        || !parseIntStrict(
            value("ParticleRuntime", "FixedTickHz", "30"),
            fixedTickHz
        )
        || fixedTickHz <= 0
        || !parseIntStrict(
            value("ParticleRuntime", "SystemCount", "0"),
            systemCount
        )
        || systemCount < 0
        || static_cast<std::size_t>(systemCount)
            > runtimeParticleMaximumSystems
        || !parseIntStrict(
            value("ParticleRuntime", "ParticleCount", "0"),
            particleCount
        )
        || particleCount < 0
        || static_cast<std::size_t>(particleCount)
            > runtimeParticleMaximumParticles
        || !parseUint64Strict(
            value("ParticleRuntime", "NextSystemId", "1"),
            authoredNextSystemId
        )
        || !parseUint64Strict(
            value("ParticleRuntime", "NextParticleId", "1"),
            authoredNextParticleId
        )
        || authoredNextSystemId == 0
        || authoredNextParticleId == 0) {
        return fail("particle runtime save has invalid header values");
    }

    RuntimeParticleSystemState loaded;
    loaded.currentTick_ = tick;
    loaded.fixedTickHz_ = fixedTickHz;
    loaded.nextSystemId_ = std::max<std::uint64_t>(1, authoredNextSystemId);
    loaded.nextParticleId_ = std::max<std::uint64_t>(1, authoredNextParticleId);
    std::unordered_set<std::uint64_t> systemIds;
    std::unordered_set<std::uint64_t> particleIds;
    std::uint64_t maximumSystemId = 0;
    std::uint64_t maximumParticleId = 0;
    loaded.systems_.reserve(static_cast<std::size_t>(systemCount));
    loaded.particles_.reserve(static_cast<std::size_t>(particleCount));

    for (int index = 0; index < systemCount; ++index) {
        const std::string section = "ParticleRuntime.System."
            + std::to_string(index);
        RuntimeParticleSystemInstance system;
        if (ini.getSection(section) == nullptr
            || !parseUint64Strict(value(section, "Id"), system.id)
            || system.id == 0
            || system.id == std::numeric_limits<std::uint64_t>::max()
            || !systemIds.insert(system.id).second
            || !hexDecode(value(section, "TypeHex"), system.typeId)
            || system.typeId.empty()
            || !parseIntStrict(
                value(section, "SourceObjectId", "-1"),
                system.sourceObjectId
            )
            || system.sourceObjectId < -1
            || !hexDecode(value(section, "OwnerHex"), system.owner)
            || !hexDecode(value(section, "SourceTagHex"), system.sourceTag)
            || !parseDoubleStrict(value(section, "OriginX", "0"), system.origin.x)
            || !parseDoubleStrict(value(section, "OriginY", "0"), system.origin.y)
            || !parseDoubleStrict(value(section, "OriginZ", "0"), system.origin.z)
            || !parseIntStrict(value(section, "ScreenOffsetY", "0"), system.screenOffsetY)
            || system.screenOffsetY < -4096 || system.screenOffsetY > 4096
            || !parseDoubleStrict(
                value(section, "DirectionX", "0"),
                system.direction.x
            )
            || !parseDoubleStrict(
                value(section, "DirectionY", "0"),
                system.direction.y
            )
            || !parseDoubleStrict(
                value(section, "DirectionZ", "0"),
                system.direction.z
            )
            || !parseIntStrict(value(section, "Age", "0"), system.ageTicks)
            || system.ageTicks < 0
            || !parseIntStrict(
                value(section, "RemainingLifetime", "-1"),
                system.remainingLifetime
            )
            || system.remainingLifetime < -1
            || !parseIntStrict(
                value(section, "SpawnCountdown", "0"),
                system.spawnCountdown
            )
            || system.spawnCountdown < 0
            || !parseUint64Strict(
                value(section, "SpawnSerial", "0"),
                system.spawnSerial
            )
            || !parseUint64Strict(
                value(section, "RngState", "1"),
                system.rngState
            )
            || !parseBool(value(section, "Emitting", "no"), system.emitting)) {
            return fail("particle runtime save has invalid system section ["
                + section + "]");
        }
        const auto boundedPoint = [](const RuntimeParticlePoint& point) {
            return std::isfinite(point.x) && std::isfinite(point.y)
                && std::isfinite(point.z)
                && std::abs(point.x) <= kMaximumRenderCoordinate
                && std::abs(point.y) <= kMaximumRenderCoordinate
                && std::abs(point.z) <= kMaximumRenderCoordinate;
        };
        if (!loadParticlePresentation(ini, section, system.presentation, system.spriteAttachment)
            || !boundedPoint(system.origin)
            || !boundedPoint(system.direction)) {
            return fail("particle runtime save has unsafe system values ["
                + section + "]");
        }
        maximumSystemId = std::max(maximumSystemId, system.id);
        loaded.nextSystemId_ = std::max(loaded.nextSystemId_, system.id + 1U);
        loaded.systems_.push_back(std::move(system));
    }

    for (int index = 0; index < particleCount; ++index) {
        const std::string section = "ParticleRuntime.Particle."
            + std::to_string(index);
        RuntimeParticleInstance particle;
        if (ini.getSection(section) == nullptr
            || !parseUint64Strict(value(section, "Id"), particle.id)
            || particle.id == 0
            || particle.id == std::numeric_limits<std::uint64_t>::max()
            || !particleIds.insert(particle.id).second
            || !parseUint64Strict(
                value(section, "SystemId", "0"),
                particle.systemInstanceId
            )
            || !hexDecode(value(section, "TypeHex"), particle.typeId)
            || particle.typeId.empty()
            || !parseIntStrict(
                value(section, "SourceObjectId", "-1"),
                particle.sourceObjectId
            )
            || particle.sourceObjectId < -1
            || !hexDecode(value(section, "OwnerHex"), particle.owner)
            || !hexDecode(value(section, "SourceTagHex"), particle.sourceTag)
            || !parseDoubleStrict(
                value(section, "PositionX", "0"),
                particle.position.x
            )
            || !parseDoubleStrict(
                value(section, "PositionY", "0"),
                particle.position.y
            )
            || !parseDoubleStrict(
                value(section, "PositionZ", "0"),
                particle.position.z
            )
            || !parseIntStrict(value(section, "ScreenOffsetY", "0"), particle.screenOffsetY)
            || particle.screenOffsetY < -4096 || particle.screenOffsetY > 4096
            || !parseDoubleStrict(
                value(section, "VelocityX", "0"),
                particle.velocity.x
            )
            || !parseDoubleStrict(
                value(section, "VelocityY", "0"),
                particle.velocity.y
            )
            || !parseDoubleStrict(
                value(section, "VelocityZ", "0"),
                particle.velocity.z
            )
            || !parseDoubleStrict(value(section, "Speed", "0"), particle.speed)
            || !parseIntStrict(value(section, "Age", "0"), particle.ageTicks)
            || particle.ageTicks < 0
            || !parseIntStrict(
                value(section, "RemainingLifetime", "-1"),
                particle.remainingLifetime
            )
            || particle.remainingLifetime < -1
            || !parseIntStrict(
                value(section, "DamageCountdown", "0"),
                particle.damageCountdown
            )
            || particle.damageCountdown < 0
            || !parseIntStrict(value(section, "StateAI", "0"), particle.stateAI)
            || particle.stateAI < 0
            || !parseIntStrict(
                value(section, "StateAdvanceCountdown", "0"),
                particle.stateAdvanceCountdown
            )
            || particle.stateAdvanceCountdown < 0
            || !parseIntStrict(
                value(section, "Translucency", "0"),
                particle.translucency
            )
            || particle.translucency < 0
            || particle.translucency > 100
            || !parseIntStrict(
                value(section, "AuthoredFrameProgress", "0"),
                particle.authoredFrameProgress
            )
            || particle.authoredFrameProgress < 0
            || particle.authoredFrameProgress >= fixedTickHz
            || !parseUint64Strict(
                value(section, "RngState", "1"),
                particle.rngState
            )) {
            return fail("particle runtime save has invalid particle section ["
                + section + "]");
        }
        const auto boundedPoint = [](const RuntimeParticlePoint& point) {
            return std::isfinite(point.x) && std::isfinite(point.y)
                && std::isfinite(point.z)
                && std::abs(point.x) <= kMaximumRenderCoordinate
                && std::abs(point.y) <= kMaximumRenderCoordinate
                && std::abs(point.z) <= kMaximumRenderCoordinate;
        };
        if (!loadParticlePresentation(ini, section, particle.presentation, particle.spriteAttachment)
            || !boundedPoint(particle.position)
            || !boundedPoint(particle.velocity)
            || !std::isfinite(particle.speed)
            || particle.speed < 0.0
            || particle.speed > kMaximumRenderCoordinate) {
            return fail("particle runtime save has unsafe particle values ["
                + section + "]");
        }
        maximumParticleId = std::max(maximumParticleId, particle.id);
        loaded.nextParticleId_ = std::max(
            loaded.nextParticleId_,
            particle.id + 1U
        );
        loaded.particles_.push_back(std::move(particle));
    }

    if (authoredNextSystemId <= maximumSystemId
        || authoredNextParticleId <= maximumParticleId) {
        return fail("particle runtime save has non-monotonic instance ids");
    }

    std::unordered_map<std::uint64_t, const RuntimeParticleSystemInstance*>
        loadedSystemsById;
    loadedSystemsById.reserve(loaded.systems_.size());
    for (const RuntimeParticleSystemInstance& system : loaded.systems_) {
        loadedSystemsById.emplace(system.id, &system);
    }
    for (const RuntimeParticleInstance& particle : loaded.particles_) {
        if (particle.systemInstanceId == 0) {
            continue;
        }
        const auto system = loadedSystemsById.find(
            particle.systemInstanceId
        );
        if (system == loadedSystemsById.end()
            || system->second->sourceObjectId != particle.sourceObjectId
            || system->second->owner != particle.owner
            || system->second->sourceTag != particle.sourceTag) {
            return fail(
                "particle runtime linked particle provenance is inconsistent"
            );
        }
    }

    RuntimeParticleReconcileReport report = loaded.reconcile(rules);
    loaded.nextSystemId_ = std::max(loaded.nextSystemId_, authoredNextSystemId);
    loaded.nextParticleId_ = std::max(loaded.nextParticleId_, authoredNextParticleId);
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = report;
    }
    return true;
}

int RuntimeParticleSystemState::currentTick() const {
    return currentTick_;
}

int RuntimeParticleSystemState::fixedTickHz() const {
    return fixedTickHz_;
}

const std::vector<RuntimeParticleSystemInstance>&
RuntimeParticleSystemState::systems() const {
    return systems_;
}

const std::vector<RuntimeParticleInstance>&
RuntimeParticleSystemState::particles() const {
    return particles_;
}

std::vector<RuntimeParticleRenderSnapshot>
RuntimeParticleSystemState::renderSnapshots(
    const RulesDatabase& rules,
    const ArtDatabase* art
) const {
    std::vector<RuntimeParticleRenderSnapshot> snapshots;
    snapshots.reserve(std::min(
        particles_.size(),
        runtimeParticleMaximumRenderSnapshots
    ));
    const ParticleType* previousParticleType = nullptr;
    for (const RuntimeParticleInstance& instance : particles_) {
        if (snapshots.size() >= runtimeParticleMaximumRenderSnapshots) {
            break;
        }
        const RuntimeParticleInstance* particle = &instance;
        if (!std::isfinite(particle->position.x)
            || !std::isfinite(particle->position.y)
            || !std::isfinite(particle->position.z)) {
            continue;
        }
        const ParticleType* type = previousParticleType != nullptr
                && previousParticleType->id == particle->typeId
            ? previousParticleType : rules.findParticleType(particle->typeId);
        previousParticleType = type;
        if (type == nullptr) {
            continue;
        }

        RuntimeParticleRenderSnapshot snapshot;
        snapshot.instanceId = particle->id;
        snapshot.systemInstanceId = particle->systemInstanceId;
        snapshot.particleTypeId = type->id;
        snapshot.imageId = type->image;
        snapshot.owner = particle->owner;
        snapshot.rawX = std::clamp(
            particle->position.x,
            -kMaximumRenderCoordinate,
            kMaximumRenderCoordinate
        );
        snapshot.rawY = std::clamp(
            particle->position.y,
            -kMaximumRenderCoordinate,
            kMaximumRenderCoordinate
        );
        snapshot.rawZ = std::clamp(
            particle->position.z,
            -kMaximumRenderCoordinate,
            kMaximumRenderCoordinate
        );
        snapshot.worldX = snapshot.rawX / kRawUnitsPerCell;
        snapshot.worldY = snapshot.rawY / kRawUnitsPerCell;
        snapshot.worldLevel = snapshot.rawZ / kRawUnitsPerCell;
        snapshot.screenOffsetY = particle->screenOffsetY;
        snapshot.presentation = particle->presentation;
        snapshot.spriteAttachment = particle->spriteAttachment;
        snapshot.airborneDamageSmoke = particle->sourceTag == "vehicle-damage-smoke"
            && (particle->screenOffsetY < 0 || particle->spriteAttachment.airborne);
        const bool gasSequence = !type->numLoopFramesSpecified && type->endStateAI > 1
            && (type->behavior == ParticleBehavior::Gas || type->behavior == ParticleBehavior::WeakGas);
        const bool smokeSequence = !type->numLoopFramesSpecified && type->endStateAI > 1
            && type->behavior == ParticleBehavior::Smoke;
        // Stock gas authors its SHP strip through EndStateAI, without
        // NumLoopFrames. Keep it animated while the damage state is held.
        // Stock smoke advances through the strip once with StateAIAdvance;
        // an omitted NumLoopFrames must not pin SGRYSMK1 to frame zero.
        const int loopFrame = gasSequence
            ? static_cast<int>((static_cast<long long>(particle->ageTicks) * kAuthoredTicksPerSecond
                / (std::max(1, fixedTickHz_) * std::max(1, type->stateAIAdvance))) % type->endStateAI)
            : smokeSequence ? std::clamp(particle->stateAI, 0, type->endStateAI - 1)
            : std::max(0, particle->stateAI) % std::max(1, type->numLoopFrames);
        const long long frame = static_cast<long long>(type->startFrame)
            + static_cast<long long>(loopFrame);
        snapshot.frameIndex = static_cast<int>(std::clamp<long long>(
            frame,
            0,
            std::numeric_limits<int>::max()
        ));

        const ArtType* imageArt = nullptr;
        if (art != nullptr) {
            if (!snapshot.imageId.empty()) {
                imageArt = art->findArtType(snapshot.imageId);
            }
            if (imageArt == nullptr) {
                imageArt = art->findArtType(type->id);
            }
        }
        if (imageArt != nullptr) {
            if (!imageArt->image.empty()) {
                snapshot.imageId = imageArt->image;
            }
            snapshot.ySortAdjust = imageArt->ySortAdjust;
        }
        int stateTranslucency = 0;
        if (type->translucent25State > 0
            && particle->stateAI >= type->translucent25State) {
            stateTranslucency = 25;
        }
        if (type->translucent50State > 0
            && particle->stateAI >= type->translucent50State) {
            stateTranslucency = 50;
        }
        snapshot.translucency = std::clamp(
            std::max({
                particle->translucency,
                stateTranslucency,
                imageArt != nullptr && imageArt->translucent ? 50 : 0
            }),
            0,
            100
        );
        if (gasSequence && type->nextParticle.empty() && type->maxLifetime > 0) {
            const int lifetime = std::max(1, convertRuleFrames(type->maxLifetime));
            snapshot.translucency = std::max(snapshot.translucency,
                static_cast<int>(std::min(100LL, static_cast<long long>(particle->ageTicks) * 100 / lifetime)));
        }
        if (type->behavior == ParticleBehavior::Railgun) {
            snapshot.renderKind = RuntimeParticleRenderKind::Pixel;
        } else if (type->behavior == ParticleBehavior::Spark) {
            snapshot.renderKind = RuntimeParticleRenderKind::Spark;
        } else {
            snapshot.renderKind = !snapshot.imageId.empty()
                ? RuntimeParticleRenderKind::Shp
                : RuntimeParticleRenderKind::Fallback;
        }
        const ParticleColor color = renderColorForParticle(
            *particle,
            *type,
            fixedTickHz_
        );
        snapshot.red = static_cast<std::uint8_t>(
            std::clamp(color.red, 0, 255)
        );
        snapshot.green = static_cast<std::uint8_t>(
            std::clamp(color.green, 0, 255)
        );
        snapshot.blue = static_cast<std::uint8_t>(
            std::clamp(color.blue, 0, 255)
        );
        snapshot.revision = renderSnapshotRevision(snapshot);
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

std::vector<RuntimeParticleBeamSnapshot>
RuntimeParticleSystemState::beamSnapshots(
    const RulesDatabase& rules
) const {
    std::vector<RuntimeParticleBeamSnapshot> snapshots;
    snapshots.reserve(systems_.size());
    const int laserLifetimeTicks = convertRuleFrames(
        kRailgunLaserLifetimeFrames
    );
    for (const RuntimeParticleSystemInstance& instance : systems_) {
        const RuntimeParticleSystemInstance* system = &instance;
        if (!hasDirection(system->direction)
            || system->ageTicks >= laserLifetimeTicks) {
            continue;
        }
        const ParticleSystemType* type = rules.findParticleSystemType(
            system->typeId
        );
        if (type == nullptr
            || type->behavior != ParticleBehavior::Railgun
            || !type->laser) {
            continue;
        }

        const ParticleColor color = type->hasLaserColor
            ? type->laserColor
            : defaultRenderColor(ParticleBehavior::Railgun);
        RuntimeParticleBeamSnapshot snapshot;
        snapshot.systemInstanceId = system->id;
        snapshot.owner = system->owner;
        snapshot.fromWorldX = system->origin.x / kRawUnitsPerCell;
        snapshot.fromWorldY = system->origin.y / kRawUnitsPerCell;
        snapshot.fromWorldLevel = system->origin.z / kRawUnitsPerCell;
        snapshot.toWorldX = (system->origin.x + system->direction.x)
            / kRawUnitsPerCell;
        snapshot.toWorldY = (system->origin.y + system->direction.y)
            / kRawUnitsPerCell;
        snapshot.toWorldLevel = (system->origin.z + system->direction.z)
            / kRawUnitsPerCell;
        snapshot.red = static_cast<std::uint8_t>(
            std::clamp(color.red, 0, 255)
        );
        snapshot.green = static_cast<std::uint8_t>(
            std::clamp(color.green, 0, 255)
        );
        snapshot.blue = static_cast<std::uint8_t>(
            std::clamp(color.blue, 0, 255)
        );
        snapshot.thickness = 1;

        std::uint64_t revision = kFnvOffset;
        hashSnapshotValue(revision, snapshot.systemInstanceId);
        revision = hashText(revision, snapshot.owner);
        hashSnapshotDouble(revision, snapshot.fromWorldX);
        hashSnapshotDouble(revision, snapshot.fromWorldY);
        hashSnapshotDouble(revision, snapshot.fromWorldLevel);
        hashSnapshotDouble(revision, snapshot.toWorldX);
        hashSnapshotDouble(revision, snapshot.toWorldY);
        hashSnapshotDouble(revision, snapshot.toWorldLevel);
        hashSnapshotValue(revision, snapshot.red);
        hashSnapshotValue(revision, snapshot.green);
        hashSnapshotValue(revision, snapshot.blue);
        hashSnapshotValue(revision, snapshot.thickness);
        snapshot.revision = revision;
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

std::vector<RuntimeLocalLightSnapshot>
RuntimeParticleSystemState::localLightSnapshots(
    const RulesDatabase& rules
) const {
    std::vector<RuntimeLocalLightSnapshot> snapshots;
    snapshots.reserve(systems_.size());
    // Build only the associations that can contribute a light, then scan
    // particles once. Many simultaneous fire/spark systems previously each
    // rescanned the complete particle population every rendered frame.
    std::unordered_map<std::uint64_t, const RuntimeParticleInstance*>
        newestParticlesBySystem;
    for (const RuntimeParticleSystemInstance& system : systems_) {
        const ParticleSystemType* type = rules.findParticleSystemType(
            system.typeId
        );
        if (type != nullptr && type->lightSize > 0) {
            newestParticlesBySystem.emplace(system.id, nullptr);
        }
    }
    if (newestParticlesBySystem.empty()) {
        return snapshots;
    }
    for (const RuntimeParticleInstance& particle : particles_) {
        const auto found = newestParticlesBySystem.find(
            particle.systemInstanceId
        );
        if (found == newestParticlesBySystem.end()) {
            continue;
        }
        const RuntimeParticleInstance*& newest = found->second;
        if (newest == nullptr || particle.ageTicks < newest->ageTicks
            || (particle.ageTicks == newest->ageTicks
                && particle.id > newest->id)) {
            newest = &particle;
        }
    }
    const int oneAuthoredFrameTicks = convertRuleFrames(1);
    for (const RuntimeParticleSystemInstance& instance : systems_) {
        const RuntimeParticleSystemInstance* system = &instance;
        const ParticleSystemType* type = rules.findParticleSystemType(
            system->typeId
        );
        if (type == nullptr || type->lightSize <= 0) {
            continue;
        }

        const RuntimeParticleInstance* newestParticle =
            newestParticlesBySystem.find(system->id)->second;
        if (type->oneFrameLight
            && (newestParticle == nullptr
                || newestParticle->ageTicks > oneAuthoredFrameTicks)) {
            continue;
        }

        const RuntimeParticlePoint position = type->oneFrameLight
            && newestParticle != nullptr
            ? newestParticle->position
            : system->origin;
        ParticleColor color = type->hasLaserColor
            ? type->laserColor
            : ParticleColor{255, 232, 176};
        if (!type->hasLaserColor && newestParticle != nullptr) {
            const ParticleType* particleType = rules.findParticleType(
                newestParticle->typeId
            );
            if (particleType != nullptr) {
                color = renderColorForParticle(
                    *newestParticle,
                    *particleType,
                    fixedTickHz_
                );
            }
        }

        RuntimeLocalLightSnapshot snapshot;
        snapshot.stableId = system->id;
        snapshot.owner = system->owner;
        snapshot.worldX = position.x / kRawUnitsPerCell;
        snapshot.worldY = position.y / kRawUnitsPerCell;
        snapshot.worldLevel = position.z / kRawUnitsPerCell;
        snapshot.radiusPixels = std::clamp(type->lightSize, 1, 192);
        snapshot.red = static_cast<std::uint8_t>(
            std::clamp(color.red, 0, 255)
        );
        snapshot.green = static_cast<std::uint8_t>(
            std::clamp(color.green, 0, 255)
        );
        snapshot.blue = static_cast<std::uint8_t>(
            std::clamp(color.blue, 0, 255)
        );
        snapshot.intensity = static_cast<std::uint8_t>(
            type->oneFrameLight ? 224 : 160
        );

        std::uint64_t revision = kFnvOffset;
        hashSnapshotValue(revision, snapshot.stableId);
        revision = hashText(revision, snapshot.owner);
        hashSnapshotDouble(revision, snapshot.worldX);
        hashSnapshotDouble(revision, snapshot.worldY);
        hashSnapshotDouble(revision, snapshot.worldLevel);
        hashSnapshotValue(revision, snapshot.radiusPixels);
        hashSnapshotValue(revision, snapshot.red);
        hashSnapshotValue(revision, snapshot.green);
        hashSnapshotValue(revision, snapshot.blue);
        hashSnapshotValue(revision, snapshot.intensity);
        snapshot.revision = revision;
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}
