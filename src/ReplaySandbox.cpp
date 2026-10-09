#include "ReplaySandbox.h"

#include "RuntimeShell.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace {

constexpr std::uint64_t FnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t FnvPrime = 1099511628211ull;
constexpr int RangeScale = 256;

std::string findDefaultRuntimeReplayEventFile() {
    const std::filesystem::path candidates[] = {
        std::filesystem::path("build") / "runtime_replay_events_command.txt",
        std::filesystem::path("runtime_replay_events_command.txt"),
        std::filesystem::path("build") / "Debug" / "runtime_replay_events_command.txt"
    };
    std::error_code errorCode;
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate, errorCode)
            && std::filesystem::is_regular_file(candidate, errorCode)) {
            return candidate.string();
        }
    }
    return {};
}

struct ReplayCapabilitySummaryRecord {
    int tick = 0;
    int playerId = 0;
    int sequence = 0;
    int subjectInstanceId = 0;
    int targetInstanceId = 0;
    std::string selectedCapabilityHash;
    std::string abilityType;
    std::string weaponSlot;
    std::string weaponId;
    int projectileActorId = 0;
    std::string projectileId;
    std::string projectileImageId;
    std::string warheadId;
    std::string missionBefore;
    std::string missionAfter;
    std::string pathIntentHash;
    std::string renderIntentHash;
    std::string trailerId;
    std::string impactId;
    std::string pathRequestAbilityType;
    std::string passabilityProfileHash;
    std::string pathFallbackMode;
    std::string pathDiagnosticHash;
    std::string chainDiagnosticHash;
    std::string chainFieldSource = "sandbox-fallback";
    int projectileSpawnTick = -1;
    int impactTick = -1;
    int expiredTick = -1;
    std::string renderIntentId;
    bool commandAccepted = false;
    bool missionChanged = false;
    bool weaponSelected = false;
    bool projectileSpawned = false;
    bool impactResolved = false;
    bool pathQueued = false;
    bool pathCacheHit = false;
    bool pathCacheMiss = false;
    bool pathBudgetExhausted = false;
    bool footprintRingFallback = false;
    bool renderIntentExpected = false;
    bool runtimeChainFieldsAvailable = false;
    bool sandboxFallbackFields = true;
};

const UnitType* findUnitType(const RulesDatabase& rules, const std::string& id) {
    const auto findIn = [&id](const std::vector<UnitType>& units) -> const UnitType* {
        const auto it = std::find_if(
            units.begin(),
            units.end(),
            [&id](const UnitType& unit) {
                return unit.id == id;
            }
        );
        return it == units.end() ? nullptr : &*it;
    };

    if (const UnitType* unit = findIn(rules.getInfantryTypes())) {
        return unit;
    }
    if (const UnitType* unit = findIn(rules.getVehicleTypes())) {
        return unit;
    }
    return findIn(rules.getAircraftTypes());
}

const WeaponType* findWeaponType(const RulesDatabase& rules, const std::string& id) {
    const auto& weapons = rules.getWeaponTypes();
    const auto it = std::find_if(
        weapons.begin(),
        weapons.end(),
        [&id](const WeaponType& weapon) {
            return weapon.id == id;
        }
    );
    return it == weapons.end() ? nullptr : &*it;
}

const WarheadType* findWarheadType(const RulesDatabase& rules, const std::string& id) {
    const auto& warheads = rules.getWarheadTypes();
    const auto it = std::find_if(
        warheads.begin(),
        warheads.end(),
        [&id](const WarheadType& warhead) {
            return warhead.id == id;
        }
    );
    return it == warheads.end() ? nullptr : &*it;
}

const ProjectileType* findProjectileType(
    const RulesDatabase& rules,
    const std::string& id
) {
    const auto& projectiles = rules.getProjectileTypes();
    const auto it = std::find_if(
        projectiles.begin(),
        projectiles.end(),
        [&id](const ProjectileType& projectile) {
            return projectile.id == id;
        }
    );
    return it == projectiles.end() ? nullptr : &*it;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int armorIndex(const std::string& armor) {
    static const std::vector<std::string> armorOrder = {
        "none",
        "flak",
        "plate",
        "light",
        "medium",
        "heavy",
        "wood",
        "steel",
        "concrete",
        "special_1",
        "special_2"
    };

    std::string lower = armor;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );

    const auto it = std::find(armorOrder.begin(), armorOrder.end(), lower);
    return it == armorOrder.end()
        ? -1
        : static_cast<int>(std::distance(armorOrder.begin(), it));
}

std::vector<int> parseVersesPercent(const std::string& verses) {
    std::vector<int> values;
    std::stringstream input(verses);
    std::string token;

    while (std::getline(input, token, ',')) {
        token = trim(token);
        if (!token.empty() && token.back() == '%') {
            token.pop_back();
        }

        try {
            values.push_back(static_cast<int>(std::llround(std::stod(token))));
        } catch (...) {
            values.push_back(100);
        }
    }

    return values;
}

std::string firstListToken(const std::string& value) {
    std::stringstream input(value);
    std::string token;
    if (!std::getline(input, token, ',')) {
        return "";
    }
    return trim(token);
}

std::string impactIdForWarhead(const WarheadType* warhead) {
    if (warhead == nullptr) {
        return "";
    }

    const std::string anim = firstListToken(warhead->animList);
    if (!anim.empty()) {
        return anim;
    }
    if (!warhead->explosion.empty()) {
        return warhead->explosion;
    }
    if (!warhead->particle.empty()) {
        return warhead->particle;
    }
    return warhead->id;
}

std::string projectileImageIdForProjectile(const ProjectileType* projectile) {
    if (projectile == nullptr) {
        return "";
    }
    if (!projectile->image.empty() && !projectile->imageIsNone) {
        return projectile->image;
    }
    return projectile->id;
}

int damagePercentForArmor(const WarheadType* warhead, const std::string& armor) {
    if (warhead == nullptr || warhead->verses.empty()) {
        return 100;
    }

    const std::vector<int> verses = parseVersesPercent(warhead->verses);
    const int index = armorIndex(armor);
    if (index < 0 || static_cast<std::size_t>(index) >= verses.size()) {
        return 100;
    }

    return verses[static_cast<std::size_t>(index)];
}

int movementIntervalTicks(const UnitType& unit) {
    if (unit.speed <= 0) {
        return 10;
    }

    return std::max(1, 12 - unit.speed);
}

std::string formatHash(std::uint64_t value) {
    std::ostringstream output;
    output << "0x"
           << std::uppercase
           << std::hex
           << std::setw(16)
           << std::setfill('0')
           << value;
    return output.str();
}

struct ReplayUnitState {
    int instanceId = 0;
    int playerId = 0;
    std::string typeId;
    std::string armor;
    std::string weaponId;
    std::string projectileId;
    std::string projectileImageId;
    std::string warheadId;
    std::string trailerId;
    std::string impactId;
    std::string deployWeaponId;
    std::string deployProjectileId;
    std::string deployProjectileImageId;
    std::string deployWarheadId;
    std::string deployTrailerId;
    std::string deployImpactId;
    std::string missionState = "Idle";
    std::string lastPathIntentHash;
    std::string lastRenderIntentHash;
    std::string lastChainDiagnosticHash;
    int hp = 0;
    int maxHp = 0;
    int x = 0;
    int y = 0;
    int weaponDamage = 0;
    int weaponRof = 0;
    int weaponRangeScaled = 0;
    int weaponBurst = 1;
    int weaponCooldown = 0;
    int moveCooldown = 0;
    int moveInterval = 1;
    int targetInstanceId = 0;
    int projectileSpawnCount = 0;
    int impactResolveCount = 0;
    bool deployCapable = false;
    bool alive = true;
};

struct PeerState {
    int peerId = 0;
    int tick = 0;
    std::uint32_t rngState = 0;
    std::vector<ReplayUnitState> units;
};

void appendEvent(
    ReplaySandboxReport& report,
    int tick,
    const std::string& text
) {
    if (report.events.size() >= 80) {
        return;
    }

    ReplaySandboxEvent event;
    event.tick = tick;
    event.text = text;
    report.events.push_back(event);
}

ReplayUnitState* findUnit(PeerState& state, int instanceId) {
    const auto it = std::find_if(
        state.units.begin(),
        state.units.end(),
        [instanceId](const ReplayUnitState& unit) {
            return unit.instanceId == instanceId;
        }
    );
    return it == state.units.end() ? nullptr : &*it;
}

const ReplayUnitState* findUnit(const PeerState& state, int instanceId) {
    const auto it = std::find_if(
        state.units.begin(),
        state.units.end(),
        [instanceId](const ReplayUnitState& unit) {
            return unit.instanceId == instanceId;
        }
    );
    return it == state.units.end() ? nullptr : &*it;
}

bool isInRange(const ReplayUnitState& attacker, const ReplayUnitState& target) {
    const long long dx = static_cast<long long>(attacker.x - target.x);
    const long long dy = static_cast<long long>(attacker.y - target.y);
    const long long distanceScaledSquared =
        (dx * dx + dy * dy) * RangeScale * RangeScale;
    const long long range = attacker.weaponRangeScaled;
    return distanceScaledSquared <= range * range;
}

std::uint32_t nextRandom(PeerState& state) {
    state.rngState = state.rngState * 1664525u + 1013904223u;
    return state.rngState;
}

void hashInt(std::uint64_t& hash, std::int64_t value);
void hashString(std::uint64_t& hash, const std::string& value);

bool loadReplayUnit(
    const RulesDatabase& rules,
    int instanceId,
    int playerId,
    const std::string& unitId,
    int x,
    int y,
    const std::string& targetId,
    ReplayUnitState& output,
    std::string& errorMessage
) {
    const UnitType* unitType = findUnitType(rules, unitId);
    if (unitType == nullptr) {
        errorMessage = "Unknown unit type: " + unitId;
        return false;
    }

    if (unitType->primaryWeapon.empty()) {
        errorMessage = "Unit " + unitId + " has no Primary weapon.";
        return false;
    }

    const WeaponType* weapon = findWeaponType(rules, unitType->primaryWeapon);
    if (weapon == nullptr) {
        errorMessage =
            "Unit " + unitId + " references missing Primary weapon ["
            + unitType->primaryWeapon + "].";
        return false;
    }

    const WarheadType* warhead = findWarheadType(rules, weapon->warhead);
    const ProjectileType* projectile =
        findProjectileType(rules, weapon->projectile);
    const UnitType* targetType = findUnitType(rules, targetId);
    const int damagePercent =
        damagePercentForArmor(warhead, targetType == nullptr ? "" : targetType->armor);
    const int burst = weapon->burst <= 0 ? 1 : weapon->burst;

    output = ReplayUnitState{};
    output.instanceId = instanceId;
    output.playerId = playerId;
    output.typeId = unitType->id;
    output.armor = unitType->armor;
    output.weaponId = weapon->id;
    output.projectileId = weapon->projectile;
    output.projectileImageId = projectileImageIdForProjectile(projectile);
    output.warheadId = weapon->warhead;
    output.trailerId = projectile == nullptr ? "" : projectile->trailer;
    output.impactId = impactIdForWarhead(warhead);
    output.deployCapable =
        unitType->deployer
        || unitType->deployFire
        || unitType->deployToFire
        || unitType->isSimpleDeployer
        || !unitType->deploysInto.empty()
        || !unitType->deployWeapon.empty()
        || !unitType->eliteDeployWeapon.empty();
    output.deployWeaponId = unitType->deployWeapon;
    if (!output.deployWeaponId.empty()) {
        if (const WeaponType* deployWeapon =
                findWeaponType(rules, output.deployWeaponId)) {
            output.deployProjectileId = deployWeapon->projectile;
            output.deployWarheadId = deployWeapon->warhead;
            const ProjectileType* deployProjectile =
                findProjectileType(rules, deployWeapon->projectile);
            const WarheadType* deployWarhead =
                findWarheadType(rules, deployWeapon->warhead);
            output.deployProjectileImageId =
                projectileImageIdForProjectile(deployProjectile);
            output.deployTrailerId =
                deployProjectile == nullptr ? "" : deployProjectile->trailer;
            output.deployImpactId = impactIdForWarhead(deployWarhead);
        }
    } else if (output.deployCapable) {
        output.deployWeaponId = output.weaponId;
        output.deployProjectileId = output.projectileId;
        output.deployProjectileImageId = output.projectileImageId;
        output.deployWarheadId = output.warheadId;
        output.deployTrailerId = output.trailerId;
        output.deployImpactId = output.impactId;
    }
    output.missionState = "Idle";
    output.hp = unitType->strength;
    output.maxHp = unitType->strength;
    output.x = x;
    output.y = y;
    output.weaponDamage = std::max(
        1,
        static_cast<int>(
            (static_cast<long long>(weapon->damage) * burst * damagePercent + 50) / 100
        )
    );
    output.weaponRof = std::max(1, weapon->rof);
    output.weaponRangeScaled =
        std::max(1, static_cast<int>(std::llround(weapon->range * RangeScale)));
    output.weaponBurst = burst;
    output.moveInterval = movementIntervalTicks(*unitType);
    output.alive = output.hp > 0;
    return true;
}

void applyCommand(PeerState& state, const ReplayCommandRecord& command) {
    ReplayUnitState* unit = findUnit(state, command.subjectInstanceId);
    if (unit == nullptr || !unit->alive) {
        return;
    }

    if (command.type == "ATTACK") {
        unit->targetInstanceId = command.targetInstanceId;
        unit->missionState = "Attack";
    } else if (command.type == "DEPLOY") {
        unit->targetInstanceId = 0;
        unit->missionState = unit->deployCapable
            ? "Deploy"
            : "DeployUnavailable";
    }
}

void stepTowardTarget(ReplayUnitState& unit, const ReplayUnitState& target) {
    if (unit.moveCooldown > 0) {
        return;
    }

    if (unit.x < target.x) {
        ++unit.x;
    } else if (unit.x > target.x) {
        --unit.x;
    } else if (unit.y < target.y) {
        ++unit.y;
    } else if (unit.y > target.y) {
        --unit.y;
    }

    unit.moveCooldown = std::max(0, unit.moveInterval - 1);
}

int sandboxProjectileActorId(
    int ownerInstanceId,
    int targetInstanceId,
    int spawnTick,
    int spawnSequence
) {
    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, ownerInstanceId);
    hashInt(hash, targetInstanceId);
    hashInt(hash, spawnTick);
    hashInt(hash, spawnSequence);
    return static_cast<int>(hash & 0x7fffffffU);
}

std::string renderIntentIdForRecord(
    const ReplayCapabilitySummaryRecord& record
) {
    std::ostringstream output;
    output << "intent:"
           << record.tick
           << ":"
           << record.playerId
           << ":"
           << record.sequence
           << ":"
           << record.subjectInstanceId
           << ":"
           << record.targetInstanceId
           << ":"
           << record.abilityType;
    return output.str();
}

std::string renderIntentIdForProjectile(
    const ReplayProjectileLifecycleRecord& record
) {
    return "projectile:" + std::to_string(record.projectileActorId);
}

std::uint64_t hashProjectileRenderIntent(
    const ReplayProjectileLifecycleRecord& record
) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "projectile-render-intent-v1");
    hashInt(hash, record.projectileActorId);
    hashInt(hash, record.ownerInstanceId);
    hashInt(hash, record.targetInstanceId);
    hashString(hash, record.weaponSlot);
    hashString(hash, record.weaponId);
    hashString(hash, record.projectileId);
    hashString(hash, record.projectileImageId);
    hashString(hash, record.warheadId);
    hashString(hash, record.impactId);
    hashInt(hash, record.spawnTick);
    hashInt(hash, record.impactTick);
    hashInt(hash, record.expiredTick);
    hashString(hash, record.renderIntentId);
    hashString(hash, record.fieldSource);
    return hash;
}

void hashProjectileLifecycleRecord(
    std::uint64_t& hash,
    const ReplayProjectileLifecycleRecord& record
) {
    hashInt(hash, record.projectileActorId);
    hashInt(hash, record.ownerInstanceId);
    hashInt(hash, record.targetInstanceId);
    hashInt(hash, record.spawnTick);
    hashInt(hash, record.impactTick);
    hashInt(hash, record.expiredTick);
    hashString(hash, record.weaponSlot);
    hashString(hash, record.weaponId);
    hashString(hash, record.projectileId);
    hashString(hash, record.projectileImageId);
    hashString(hash, record.warheadId);
    hashString(hash, record.impactId);
    hashString(hash, record.renderIntentHash);
    hashString(hash, record.renderIntentId);
    hashString(hash, record.fieldSource);
    hashInt(hash, record.runtimeFieldsAvailable ? 1 : 0);
    hashInt(hash, record.sandboxFallbackFields ? 1 : 0);
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

bool stableIdMatches(const std::string& left, const std::string& right) {
    return lowerAscii(left) == lowerAscii(right);
}

bool eventKindMatches(
    const RuntimeReplayEventSnapshot& event,
    const char* kind
) {
    return event.kind == kind;
}

std::string formattedRuntimeRenderHash(
    const RuntimeReplayEventSnapshot& event
) {
    return event.renderIntentHash == 0 ? "" : formatHash(event.renderIntentHash);
}

struct RuntimeReplaySourceView {
    const RuntimeReplayEventSnapshot* events = nullptr;
    std::size_t count = 0;
    ReplayRuntimeEventSourceSummary summary;

    bool available() const {
        return events != nullptr && count > 0U;
    }
};

struct RuntimeReplayChainEvidence {
    const RuntimeReplayEventSnapshot* commandAccepted = nullptr;
    const RuntimeReplayEventSnapshot* missionTransition = nullptr;
    const RuntimeReplayEventSnapshot* projectileSpawn = nullptr;
    const RuntimeReplayEventSnapshot* projectileImpact = nullptr;
    const RuntimeReplayEventSnapshot* projectileExpired = nullptr;
};

bool runtimeEventMatchesTarget(
    const RuntimeReplayEventSnapshot& event,
    int targetInstanceId
) {
    return event.targetObjectId < 0 || event.targetObjectId == targetInstanceId;
}

bool runtimeEventMatchesObjectChain(
    const RuntimeReplayEventSnapshot& event,
    int objectInstanceId,
    int targetInstanceId
) {
    return (event.objectId < 0 || event.objectId == objectInstanceId)
        && runtimeEventMatchesTarget(event, targetInstanceId);
}

bool runtimeEventMatchesWeaponFields(
    const RuntimeReplayEventSnapshot& event,
    const ReplayCapabilitySummaryRecord& record
) {
    if (!event.weaponId.empty()
        && !stableIdMatches(event.weaponId, record.weaponId)) {
        return false;
    }
    if (!event.projectileId.empty()
        && !stableIdMatches(event.projectileId, record.projectileId)) {
        return false;
    }
    if (!event.projectileImageId.empty()
        && !stableIdMatches(event.projectileImageId, record.projectileImageId)) {
        return false;
    }
    if (!event.warheadId.empty()
        && !stableIdMatches(event.warheadId, record.warheadId)) {
        return false;
    }
    return true;
}

bool runtimeEventMatchesLifecycleFields(
    const RuntimeReplayEventSnapshot& event,
    const ReplayProjectileLifecycleRecord& record
) {
    bool matchedProjectileChain = false;
    if (!event.projectileId.empty() && !record.projectileId.empty()) {
        if (!stableIdMatches(event.projectileId, record.projectileId)) {
            return false;
        }
        matchedProjectileChain = true;
    }
    if (!event.projectileImageId.empty() && !record.projectileImageId.empty()) {
        if (!stableIdMatches(event.projectileImageId, record.projectileImageId)) {
            return false;
        }
        matchedProjectileChain = true;
    }
    if (!event.warheadId.empty() && !record.warheadId.empty()) {
        if (!stableIdMatches(event.warheadId, record.warheadId)) {
            return false;
        }
        matchedProjectileChain = true;
    }
    if (!matchedProjectileChain
        && !event.weaponId.empty()
        && !record.weaponId.empty()
        && !stableIdMatches(event.weaponId, record.weaponId)) {
        return false;
    }
    return matchedProjectileChain
        || event.weaponId.empty()
        || record.weaponId.empty()
        || stableIdMatches(event.weaponId, record.weaponId);
}

const RuntimeReplayEventSnapshot* findRuntimeCommandEvent(
    const RuntimeReplaySourceView& source,
    const ReplayCapabilitySummaryRecord& record
) {
    if (!source.available()) {
        return nullptr;
    }

    const RuntimeReplayEventSnapshot* looseMatch = nullptr;
    for (std::size_t index = 0; index < source.count; ++index) {
        const RuntimeReplayEventSnapshot& event = source.events[index];
        if (!eventKindMatches(event, "CommandAccepted")
            || event.acceptedCount <= 0) {
            continue;
        }
        if (!event.commandVerb.empty()
            && !stableIdMatches(event.commandVerb, record.abilityType)) {
            continue;
        }
        if (event.tick != record.tick
            || !runtimeEventMatchesTarget(event, record.targetInstanceId)) {
            if (looseMatch == nullptr) {
                looseMatch = &event;
            }
            continue;
        }
        return &event;
    }
    return looseMatch;
}

const RuntimeReplayEventSnapshot* findRuntimeMissionEvent(
    const RuntimeReplaySourceView& source,
    const ReplayCapabilitySummaryRecord& record
) {
    if (!source.available()) {
        return nullptr;
    }

    const RuntimeReplayEventSnapshot* firstLaterMatch = nullptr;
    const RuntimeReplayEventSnapshot* looseMatch = nullptr;
    for (std::size_t index = 0; index < source.count; ++index) {
        const RuntimeReplayEventSnapshot& event = source.events[index];
        if (!eventKindMatches(event, "MissionTransition")
            || event.missionFrom.empty()
            || event.missionTo.empty()) {
            continue;
        }
        if (!runtimeEventMatchesObjectChain(
                event,
                record.subjectInstanceId,
                record.targetInstanceId
            )) {
            if (looseMatch == nullptr) {
                looseMatch = &event;
            }
            continue;
        }
        if (event.tick == record.tick) {
            return &event;
        }
        if (event.tick > record.tick && firstLaterMatch == nullptr) {
            firstLaterMatch = &event;
        }
    }
    return firstLaterMatch != nullptr ? firstLaterMatch : looseMatch;
}

const RuntimeReplayEventSnapshot* findRuntimeProjectileEvent(
    const RuntimeReplaySourceView& source,
    const ReplayCapabilitySummaryRecord& record,
    const char* kind
) {
    if (!source.available()) {
        return nullptr;
    }

    const RuntimeReplayEventSnapshot* best = nullptr;
    const RuntimeReplayEventSnapshot* looseMatch = nullptr;
    for (std::size_t index = 0; index < source.count; ++index) {
        const RuntimeReplayEventSnapshot& event = source.events[index];
        if (!eventKindMatches(event, kind)
            || !runtimeEventMatchesWeaponFields(event, record)) {
            continue;
        }
        if (event.tick < record.tick
            || !runtimeEventMatchesObjectChain(
                event,
                record.subjectInstanceId,
                record.targetInstanceId
            )) {
            if (looseMatch == nullptr) {
                looseMatch = &event;
            }
            continue;
        }
        if (best == nullptr || event.tick < best->tick) {
            best = &event;
        }
    }
    return best != nullptr ? best : looseMatch;
}

bool runtimeProjectileEventHasIdentity(
    const RuntimeReplayEventSnapshot& event
) {
    return event.projectileActorId >= 0
        && !event.weaponSlot.empty()
        && !event.weaponId.empty()
        && !event.projectileId.empty()
        && !event.projectileImageId.empty()
        && !event.warheadId.empty();
}

bool runtimeEventHasRenderIntent(
    const RuntimeReplayEventSnapshot* event
) {
    return event != nullptr
        && (!event->renderIntentId.empty() || event->renderIntentHash != 0);
}

void applyRuntimeProjectileFields(
    ReplayCapabilitySummaryRecord& record,
    const RuntimeReplayEventSnapshot& event
) {
    if (event.projectileActorId >= 0) {
        record.projectileActorId = event.projectileActorId;
    }
    if (!event.weaponSlot.empty()) {
        record.weaponSlot = event.weaponSlot;
    }
    if (!event.weaponId.empty()) {
        record.weaponId = event.weaponId;
        record.weaponSelected = true;
    }
    if (!event.projectileId.empty()) {
        record.projectileId = event.projectileId;
        record.projectileSpawned = true;
    }
    if (!event.projectileImageId.empty()) {
        record.projectileImageId = event.projectileImageId;
    }
    if (!event.warheadId.empty()) {
        record.warheadId = event.warheadId;
    }
    if (!event.impactIntentId.empty()) {
        record.impactId = event.impactIntentId;
        record.impactResolved = true;
    } else if (!event.warheadImpactIntentId.empty()) {
        record.impactId = event.warheadImpactIntentId;
        record.impactResolved = true;
    }
    if (!event.trailerIntentId.empty()) {
        record.trailerId = event.trailerIntentId;
    }
    if (event.spawnTick >= 0) {
        record.projectileSpawnTick = event.spawnTick;
    }
    if (event.impactTick >= 0) {
        record.impactTick = event.impactTick;
    }
    if (event.expiredTick >= 0) {
        record.expiredTick = event.expiredTick;
    }
    if (!event.renderIntentId.empty()) {
        record.renderIntentId = event.renderIntentId;
    }
    const std::string renderHash = formattedRuntimeRenderHash(event);
    if (!renderHash.empty()) {
        record.renderIntentHash = renderHash;
    }
}

bool applyRuntimeReplayEvidence(
    ReplayCapabilitySummaryRecord& record,
    const RuntimeReplaySourceView& source
) {
    RuntimeReplayChainEvidence evidence;
    evidence.commandAccepted = findRuntimeCommandEvent(source, record);
    evidence.missionTransition = findRuntimeMissionEvent(source, record);
    if (record.abilityType == "ATTACK") {
        evidence.projectileSpawn =
            findRuntimeProjectileEvent(source, record, "ProjectileSpawned");
        evidence.projectileImpact =
            findRuntimeProjectileEvent(source, record, "ProjectileImpact");
        evidence.projectileExpired =
            findRuntimeProjectileEvent(source, record, "ProjectileExpired");
    }

    bool runtimeSourceUsed = false;
    bool runtimeRenderProvided = false;

    if (evidence.commandAccepted != nullptr) {
        record.commandAccepted = evidence.commandAccepted->acceptedCount > 0;
        if (!evidence.commandAccepted->renderIntentId.empty()) {
            record.renderIntentId = evidence.commandAccepted->renderIntentId;
        }
        const std::string renderHash =
            formattedRuntimeRenderHash(*evidence.commandAccepted);
        if (!renderHash.empty()) {
            record.renderIntentHash = renderHash;
            runtimeRenderProvided = true;
        }
        runtimeSourceUsed = true;
    }

    if (evidence.missionTransition != nullptr) {
        record.missionBefore = evidence.missionTransition->missionFrom;
        record.missionAfter = evidence.missionTransition->missionTo;
        record.missionChanged = record.missionBefore != record.missionAfter;
        if (!evidence.missionTransition->renderIntentId.empty()) {
            record.renderIntentId = evidence.missionTransition->renderIntentId;
        }
        const std::string renderHash =
            formattedRuntimeRenderHash(*evidence.missionTransition);
        if (!renderHash.empty()) {
            record.renderIntentHash = renderHash;
            runtimeRenderProvided = true;
        }
        runtimeSourceUsed = true;
    }

    if (evidence.projectileSpawn != nullptr) {
        applyRuntimeProjectileFields(record, *evidence.projectileSpawn);
        runtimeRenderProvided =
            runtimeRenderProvided || runtimeEventHasRenderIntent(evidence.projectileSpawn);
        runtimeSourceUsed = true;
    }
    if (evidence.projectileImpact != nullptr) {
        applyRuntimeProjectileFields(record, *evidence.projectileImpact);
        runtimeRenderProvided =
            runtimeRenderProvided || runtimeEventHasRenderIntent(evidence.projectileImpact);
        runtimeSourceUsed = true;
    }
    if (evidence.projectileExpired != nullptr) {
        applyRuntimeProjectileFields(record, *evidence.projectileExpired);
        runtimeRenderProvided =
            runtimeRenderProvided || runtimeEventHasRenderIntent(evidence.projectileExpired);
        runtimeSourceUsed = true;
    }

    const bool commandComplete =
        evidence.commandAccepted != nullptr && record.commandAccepted;
    const bool missionComplete =
        evidence.missionTransition != nullptr && record.missionChanged;
    const bool renderComplete =
        runtimeRenderProvided
        && (!record.renderIntentId.empty() || !record.renderIntentHash.empty());
    const bool projectileComplete =
        record.abilityType != "ATTACK"
        || (
            evidence.projectileSpawn != nullptr
            && evidence.projectileImpact != nullptr
            && evidence.projectileExpired != nullptr
            && runtimeProjectileEventHasIdentity(*evidence.projectileSpawn)
            && record.projectileActorId >= 0
            && record.projectileSpawnTick >= 0
            && record.impactTick >= 0
            && record.expiredTick >= 0
        );

    record.runtimeChainFieldsAvailable =
        commandComplete && missionComplete && renderComplete && projectileComplete;
    if (record.runtimeChainFieldsAvailable) {
        record.chainFieldSource = "runtime";
        record.sandboxFallbackFields = false;
    } else if (runtimeSourceUsed) {
        record.chainFieldSource = "runtime-partial";
        record.sandboxFallbackFields = true;
    } else {
        record.chainFieldSource = "sandbox-fallback";
        record.sandboxFallbackFields = true;
    }

    return runtimeRenderProvided;
}

void applyRuntimeProjectileLifecycleFields(
    ReplayProjectileLifecycleRecord& record,
    const RuntimeReplayEventSnapshot& event
) {
    if (event.projectileActorId >= 0) {
        record.projectileActorId = event.projectileActorId;
    }
    if (!event.weaponSlot.empty()) {
        record.weaponSlot = event.weaponSlot;
    }
    if (!event.weaponId.empty()) {
        record.weaponId = event.weaponId;
    }
    if (!event.projectileId.empty()) {
        record.projectileId = event.projectileId;
    }
    if (!event.projectileImageId.empty()) {
        record.projectileImageId = event.projectileImageId;
    }
    if (!event.warheadId.empty()) {
        record.warheadId = event.warheadId;
    }
    if (!event.impactIntentId.empty()) {
        record.impactId = event.impactIntentId;
    } else if (!event.warheadImpactIntentId.empty()) {
        record.impactId = event.warheadImpactIntentId;
    }
    if (event.spawnTick >= 0) {
        record.spawnTick = event.spawnTick;
    }
    if (event.impactTick >= 0) {
        record.impactTick = event.impactTick;
    }
    if (event.expiredTick >= 0) {
        record.expiredTick = event.expiredTick;
    }
    if (!event.renderIntentId.empty()) {
        record.renderIntentId = event.renderIntentId;
    }
    const std::string renderHash = formattedRuntimeRenderHash(event);
    if (!renderHash.empty()) {
        record.renderIntentHash = renderHash;
    }
}

const RuntimeReplayEventSnapshot* findRuntimeLifecycleEvent(
    const RuntimeReplaySourceView& source,
    const ReplayProjectileLifecycleRecord& record,
    const char* kind,
    int fallbackTick
) {
    if (!source.available()) {
        return nullptr;
    }

    const RuntimeReplayEventSnapshot* best = nullptr;
    const RuntimeReplayEventSnapshot* looseMatch = nullptr;
    for (std::size_t index = 0; index < source.count; ++index) {
        const RuntimeReplayEventSnapshot& event = source.events[index];
        if (!eventKindMatches(event, kind)
            ) {
            continue;
        }
        if (!runtimeEventMatchesLifecycleFields(event, record)) {
            continue;
        }
        if (event.tick < fallbackTick
            || !runtimeEventMatchesObjectChain(
                event,
                record.ownerInstanceId,
                record.targetInstanceId
            )) {
            if (looseMatch == nullptr) {
                looseMatch = &event;
            }
            continue;
        }
        if (best == nullptr || event.tick < best->tick) {
            best = &event;
        }
    }
    return best != nullptr ? best : looseMatch;
}

void applyRuntimeLifecycleEvidence(
    ReplayProjectileLifecycleRecord& record,
    const RuntimeReplaySourceView& source,
    int fallbackTick
) {
    const RuntimeReplayEventSnapshot* spawn =
        findRuntimeLifecycleEvent(source, record, "ProjectileSpawned", fallbackTick);
    const RuntimeReplayEventSnapshot* impact =
        findRuntimeLifecycleEvent(source, record, "ProjectileImpact", fallbackTick);
    const RuntimeReplayEventSnapshot* expired =
        findRuntimeLifecycleEvent(source, record, "ProjectileExpired", fallbackTick);

    bool runtimeSourceUsed = false;
    if (spawn != nullptr) {
        applyRuntimeProjectileLifecycleFields(record, *spawn);
        runtimeSourceUsed = true;
    }
    if (impact != nullptr) {
        applyRuntimeProjectileLifecycleFields(record, *impact);
        runtimeSourceUsed = true;
    }
    if (expired != nullptr) {
        applyRuntimeProjectileLifecycleFields(record, *expired);
        runtimeSourceUsed = true;
    }

    const bool runtimeComplete =
        spawn != nullptr
        && impact != nullptr
        && expired != nullptr
        && runtimeProjectileEventHasIdentity(*spawn)
        && record.spawnTick >= 0
        && record.impactTick >= 0
        && record.expiredTick >= 0
        && (!record.renderIntentId.empty() || !record.renderIntentHash.empty());
    record.runtimeFieldsAvailable = runtimeComplete;
    if (runtimeComplete) {
        record.fieldSource = "runtime";
        record.sandboxFallbackFields = false;
    } else if (runtimeSourceUsed) {
        record.fieldSource = "runtime-partial";
        record.sandboxFallbackFields = true;
    } else {
        record.fieldSource = "sandbox-fallback";
        record.sandboxFallbackFields = true;
    }
}

void recordProjectileLifecycle(
    std::uint64_t& lifecycleHash,
    int& lifecycleCount,
    int& runtimeLifecycleCount,
    int& fallbackLifecycleCount,
    int tick,
    const ReplayUnitState& unit,
    const ReplayUnitState& target,
    const RuntimeReplaySourceView& runtimeSource
) {
    ReplayProjectileLifecycleRecord record;
    record.ownerInstanceId = unit.instanceId;
    record.targetInstanceId = target.instanceId;
    record.spawnTick = tick;
    record.impactTick = tick;
    record.expiredTick = tick;
    record.weaponSlot = "primary";
    record.weaponId = unit.weaponId;
    record.projectileId = unit.projectileId;
    record.projectileImageId = unit.projectileImageId;
    record.warheadId = unit.warheadId;
    record.impactId = unit.impactId;
    record.projectileActorId = sandboxProjectileActorId(
        unit.instanceId,
        target.instanceId,
        record.spawnTick,
        unit.projectileSpawnCount
    );
    record.renderIntentId = renderIntentIdForProjectile(record);
    record.renderIntentHash = formatHash(hashProjectileRenderIntent(record));
    applyRuntimeLifecycleEvidence(record, runtimeSource, tick);
    if (record.renderIntentHash.empty()) {
        record.renderIntentHash = formatHash(hashProjectileRenderIntent(record));
    }

    ++lifecycleCount;
    if (record.runtimeFieldsAvailable || record.fieldSource == "runtime-partial") {
        ++runtimeLifecycleCount;
    }
    if (record.sandboxFallbackFields) {
        ++fallbackLifecycleCount;
    }
    hashProjectileLifecycleRecord(lifecycleHash, record);
}

void simulatePeerTick(
    PeerState& state,
    ReplaySandboxReport& report,
    bool recordEvents,
    std::uint64_t& projectileLifecycleHash,
    int& projectileLifecycleCount,
    int& projectileLifecycleRuntimeCount,
    int& projectileLifecycleFallbackCount,
    const RuntimeReplaySourceView& runtimeSource
) {
    for (ReplayUnitState& unit : state.units) {
        if (unit.weaponCooldown > 0) {
            --unit.weaponCooldown;
        }
        if (unit.moveCooldown > 0) {
            --unit.moveCooldown;
        }
    }

    if (state.tick % 32 == 0) {
        const std::uint32_t sample = nextRandom(state);
        if (recordEvents && state.tick % 128 == 0) {
            appendEvent(
                report,
                state.tick,
                "deterministic rng sample=" + std::to_string(sample)
            );
        }
    }

    for (ReplayUnitState& unit : state.units) {
        if (!unit.alive || unit.targetInstanceId == 0) {
            continue;
        }

        ReplayUnitState* target = findUnit(state, unit.targetInstanceId);
        if (target == nullptr || !target->alive) {
            unit.targetInstanceId = 0;
            unit.missionState = "Idle";
            continue;
        }

        if (!isInRange(unit, *target)) {
            const int beforeX = unit.x;
            const int beforeY = unit.y;
            stepTowardTarget(unit, *target);
            if (recordEvents && (beforeX != unit.x || beforeY != unit.y)) {
                appendEvent(
                    report,
                    state.tick,
                    "#" + std::to_string(unit.instanceId)
                    + " moves to ("
                    + std::to_string(unit.x)
                    + ","
                    + std::to_string(unit.y)
                    + ")"
                );
            }
        }

        if (!isInRange(unit, *target) || unit.weaponCooldown > 0) {
            continue;
        }

        target->hp -= unit.weaponDamage;
        unit.weaponCooldown = std::max(0, unit.weaponRof - 1);
        ++unit.projectileSpawnCount;
        ++unit.impactResolveCount;

        if (recordEvents) {
            recordProjectileLifecycle(
                projectileLifecycleHash,
                projectileLifecycleCount,
                projectileLifecycleRuntimeCount,
                projectileLifecycleFallbackCount,
                state.tick,
                unit,
                *target,
                runtimeSource
            );
        }

        if (recordEvents) {
            appendEvent(
                report,
                state.tick,
                "#" + std::to_string(unit.instanceId)
                + " fires "
                + unit.weaponId
                + " projectile="
                + (unit.projectileId.empty() ? "(none)" : unit.projectileId)
                + " trailer="
                + (unit.trailerId.empty() ? "(none)" : unit.trailerId)
                + " warhead="
                + (unit.warheadId.empty() ? "(none)" : unit.warheadId)
                + " impact="
                + (unit.impactId.empty() ? "(none)" : unit.impactId)
                + " at #"
                + std::to_string(target->instanceId)
                + " damage="
                + std::to_string(unit.weaponDamage)
                + " hp="
                + std::to_string(std::max(0, target->hp))
            );
        }

        if (target->hp <= 0) {
            target->hp = 0;
            target->alive = false;
            if (recordEvents) {
                appendEvent(
                    report,
                    state.tick,
                    "#" + std::to_string(target->instanceId)
                    + " destroyed"
                );
            }
        }
    }
}

void hashByte(std::uint64_t& hash, unsigned char value) {
    hash ^= value;
    hash *= FnvPrime;
}

void hashInt(std::uint64_t& hash, std::int64_t value) {
    const std::uint64_t unsignedValue = static_cast<std::uint64_t>(value);
    for (int index = 0; index < 8; ++index) {
        hashByte(hash, static_cast<unsigned char>((unsignedValue >> (index * 8)) & 0xffu));
    }
}

void hashString(std::uint64_t& hash, const std::string& value) {
    for (const char character : value) {
        hashByte(hash, static_cast<unsigned char>(character));
    }
    hashByte(hash, 0xffu);
}

std::string boolToken(bool value) {
    return value ? "yes" : "no";
}

std::string capabilityHashForUnit(const ReplayUnitState& unit) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "selected-capability-v1");
    hashString(hash, unit.typeId);
    hashString(hash, unit.weaponId);
    hashString(hash, unit.projectileId);
    hashString(hash, unit.projectileImageId);
    hashString(hash, unit.warheadId);
    hashString(hash, unit.trailerId);
    hashString(hash, unit.impactId);
    hashString(hash, unit.deployWeaponId);
    hashString(hash, unit.deployProjectileId);
    hashString(hash, unit.deployProjectileImageId);
    hashString(hash, unit.deployWarheadId);
    hashString(hash, unit.deployTrailerId);
    hashString(hash, unit.deployImpactId);
    hashInt(hash, unit.deployCapable ? 1 : 0);
    hashInt(hash, unit.weaponDamage);
    hashInt(hash, unit.weaponRof);
    hashInt(hash, unit.weaponRangeScaled);
    hashInt(hash, unit.weaponBurst);
    hashInt(hash, unit.moveInterval);
    return formatHash(hash);
}

std::string weaponSlotForAbility(const ReplayCommandRecord& command) {
    if (command.type == "ATTACK") {
        return "primary";
    }
    if (command.type == "DEPLOY") {
        return "deploy";
    }
    if (command.type == "ENTER" || command.type == "REPAIR") {
        return "none";
    }
    return "unknown";
}

std::string weaponIdForAbility(
    const ReplayUnitState& unit,
    const ReplayCommandRecord& command
) {
    if (command.type == "DEPLOY") {
        return unit.deployWeaponId;
    }
    if (command.type == "ATTACK") {
        return unit.weaponId;
    }
    return "";
}

std::string projectileIdForAbility(
    const ReplayUnitState& unit,
    const ReplayCommandRecord& command
) {
    if (command.type == "DEPLOY") {
        return unit.deployProjectileId;
    }
    if (command.type == "ATTACK") {
        return unit.projectileId;
    }
    return "";
}

std::string projectileImageIdForAbility(
    const ReplayUnitState& unit,
    const ReplayCommandRecord& command
) {
    if (command.type == "DEPLOY") {
        return unit.deployProjectileImageId;
    }
    if (command.type == "ATTACK") {
        return unit.projectileImageId;
    }
    return "";
}

std::string warheadIdForAbility(
    const ReplayUnitState& unit,
    const ReplayCommandRecord& command
) {
    if (command.type == "DEPLOY") {
        return unit.deployWarheadId;
    }
    if (command.type == "ATTACK") {
        return unit.warheadId;
    }
    return "";
}

std::string trailerIdForAbility(
    const ReplayUnitState& unit,
    const ReplayCommandRecord& command
) {
    if (command.type == "DEPLOY") {
        return unit.deployTrailerId;
    }
    if (command.type == "ATTACK") {
        return unit.trailerId;
    }
    return "";
}

std::string impactIdForAbility(
    const ReplayUnitState& unit,
    const ReplayCommandRecord& command
) {
    if (command.type == "DEPLOY") {
        return unit.deployImpactId;
    }
    if (command.type == "ATTACK") {
        return unit.impactId;
    }
    return "";
}

std::string passabilityProfileHashForPeer(const PeerState& state) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "replay-path-profile-v1");
    for (const ReplayUnitState& unit : state.units) {
        hashInt(hash, unit.instanceId);
        hashString(hash, unit.typeId);
        hashInt(hash, unit.moveInterval);
        hashInt(hash, unit.weaponRangeScaled);
        hashInt(hash, unit.alive ? 1 : 0);
    }
    return formatHash(hash);
}

bool commandNeedsPath(const ReplayCapabilitySummaryRecord& record) {
    return record.commandAccepted && record.abilityType == "ATTACK";
}

std::uint64_t pathCacheKeyForRecord(const ReplayCapabilitySummaryRecord& record) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "replay-capability-path-cache-v1");
    hashString(hash, record.passabilityProfileHash);
    hashString(hash, record.pathRequestAbilityType);
    hashString(hash, record.pathIntentHash);
    hashString(hash, record.pathFallbackMode);
    return hash;
}

std::uint64_t hashPathDiagnosticForRecord(
    const ReplayCapabilitySummaryRecord& record
) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "path-diagnostic-v1");
    hashString(hash, record.pathRequestAbilityType);
    hashString(hash, record.passabilityProfileHash);
    hashString(hash, record.pathFallbackMode);
    hashString(hash, record.pathIntentHash);
    hashInt(hash, record.pathQueued ? 1 : 0);
    hashInt(hash, record.pathCacheHit ? 1 : 0);
    hashInt(hash, record.pathCacheMiss ? 1 : 0);
    hashInt(hash, record.pathBudgetExhausted ? 1 : 0);
    hashInt(hash, record.footprintRingFallback ? 1 : 0);
    return hash;
}

std::uint64_t hashChainDiagnosticForRecord(
    const ReplayCapabilitySummaryRecord& record
) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "capability-chain-v1");
    hashString(hash, record.selectedCapabilityHash);
    hashString(hash, record.abilityType);
    hashString(hash, record.weaponSlot);
    hashString(hash, record.weaponId);
    hashInt(hash, record.projectileActorId);
    hashString(hash, record.projectileId);
    hashString(hash, record.projectileImageId);
    hashString(hash, record.warheadId);
    hashString(hash, record.trailerId);
    hashString(hash, record.impactId);
    hashString(hash, record.missionBefore);
    hashString(hash, record.missionAfter);
    hashString(hash, record.pathDiagnosticHash);
    hashString(hash, record.renderIntentHash);
    hashString(hash, record.renderIntentId);
    hashString(hash, record.chainFieldSource);
    hashInt(hash, record.projectileSpawnTick);
    hashInt(hash, record.impactTick);
    hashInt(hash, record.expiredTick);
    hashInt(hash, record.commandAccepted ? 1 : 0);
    hashInt(hash, record.missionChanged ? 1 : 0);
    hashInt(hash, record.weaponSelected ? 1 : 0);
    hashInt(hash, record.projectileSpawned ? 1 : 0);
    hashInt(hash, record.impactResolved ? 1 : 0);
    hashInt(hash, record.renderIntentExpected ? 1 : 0);
    hashInt(hash, record.runtimeChainFieldsAvailable ? 1 : 0);
    hashInt(hash, record.sandboxFallbackFields ? 1 : 0);
    return hash;
}

void updatePathDiagnosticHash(ReplayCapabilitySummaryRecord& record) {
    record.pathDiagnosticHash =
        formatHash(hashPathDiagnosticForRecord(record));
}

void updateChainDiagnosticHash(ReplayCapabilitySummaryRecord& record) {
    record.chainDiagnosticHash =
        formatHash(hashChainDiagnosticForRecord(record));
}

void applyPathDiagnostics(
    ReplayCapabilitySummaryRecord& record,
    const std::vector<std::uint64_t>& cachedPathKeys,
    bool budgetExhausted
) {
    record.pathRequestAbilityType = record.abilityType;
    if (record.pathFallbackMode.empty()) {
        record.pathFallbackMode =
            record.abilityType == "ATTACK" ? "attack-ring" : "none";
    }

    record.pathQueued = commandNeedsPath(record);
    record.pathBudgetExhausted = record.pathQueued && budgetExhausted;
    record.footprintRingFallback =
        record.pathQueued && record.pathFallbackMode == "attack-ring";
    if (record.pathQueued) {
        const std::uint64_t cacheKey = pathCacheKeyForRecord(record);
        record.pathCacheHit =
            std::find(cachedPathKeys.begin(), cachedPathKeys.end(), cacheKey)
            != cachedPathKeys.end();
        record.pathCacheMiss = !record.pathCacheHit;
    } else {
        record.pathCacheHit = false;
        record.pathCacheMiss = false;
    }
    updatePathDiagnosticHash(record);
}

std::uint64_t hashPathIntentForCommand(
    const PeerState& state,
    const ReplayCommandRecord& command
) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "path-intent-v1");
    hashString(hash, command.type);
    hashInt(hash, command.subjectInstanceId);
    hashInt(hash, command.targetInstanceId);

    const ReplayUnitState* unit = findUnit(state, command.subjectInstanceId);
    if (unit == nullptr || !unit->alive) {
        hashString(hash, "missing-subject");
        return hash;
    }

    hashString(hash, unit->missionState);
    hashString(hash, unit->weaponId);
    hashString(hash, unit->projectileId);
    hashString(hash, unit->projectileImageId);
    hashString(hash, unit->warheadId);
    hashString(hash, unit->trailerId);
    hashString(hash, unit->impactId);
    hashInt(hash, unit->x);
    hashInt(hash, unit->y);
    hashInt(hash, unit->moveCooldown);
    hashInt(hash, unit->weaponRangeScaled);

    if (command.type != "ATTACK") {
        hashString(hash, "no-path-required");
        return hash;
    }

    const ReplayUnitState* target = findUnit(state, command.targetInstanceId);
    const bool targetAlive = target != nullptr && target->alive;
    hashInt(hash, targetAlive ? 1 : 0);
    if (!targetAlive) {
        return hash;
    }

    const bool inRange = isInRange(*unit, *target);
    int nextX = unit->x;
    int nextY = unit->y;
    if (!inRange && unit->moveCooldown <= 0) {
        if (unit->x < target->x) {
            ++nextX;
        } else if (unit->x > target->x) {
            --nextX;
        } else if (unit->y < target->y) {
            ++nextY;
        } else if (unit->y > target->y) {
            --nextY;
        }
    }

    hashInt(hash, inRange ? 1 : 0);
    hashInt(hash, target->instanceId);
    hashInt(hash, target->x - unit->x);
    hashInt(hash, target->y - unit->y);
    hashInt(hash, nextX);
    hashInt(hash, nextY);
    return hash;
}

std::uint64_t hashRenderIntentForRecord(
    const PeerState& state,
    const ReplayCapabilitySummaryRecord& record
) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "render-intent-v1");
    hashString(hash, record.abilityType);
    hashString(hash, record.weaponSlot);
    hashString(hash, record.weaponId);
    hashInt(hash, record.projectileActorId);
    hashString(hash, record.projectileId);
    hashString(hash, record.projectileImageId);
    hashString(hash, record.warheadId);
    hashString(hash, record.trailerId);
    hashString(hash, record.impactId);
    hashString(hash, record.missionAfter);
    hashString(hash, record.pathDiagnosticHash);
    hashString(hash, record.renderIntentId);
    hashString(hash, record.chainFieldSource);
    hashInt(hash, record.subjectInstanceId);
    hashInt(hash, record.targetInstanceId);
    hashInt(hash, record.projectileSpawnTick);
    hashInt(hash, record.impactTick);
    hashInt(hash, record.expiredTick);

    const ReplayUnitState* unit = findUnit(state, record.subjectInstanceId);
    if (unit == nullptr) {
        hashString(hash, "missing-subject");
        return hash;
    }

    hashString(hash, unit->typeId);
    hashInt(hash, unit->x);
    hashInt(hash, unit->y);
    hashInt(hash, unit->alive ? 1 : 0);

    const ReplayUnitState* target = findUnit(state, record.targetInstanceId);
    if (target != nullptr) {
        hashInt(hash, target->x);
        hashInt(hash, target->y);
        hashInt(hash, target->alive ? 1 : 0);
    }

    return hash;
}

ReplayCapabilitySummaryRecord beginCapabilityIntentRecord(
    const PeerState& state,
    const ReplayCommandRecord& command
) {
    ReplayCapabilitySummaryRecord record;
    record.tick = command.tick;
    record.playerId = command.playerId;
    record.sequence = command.sequence;
    record.subjectInstanceId = command.subjectInstanceId;
    record.targetInstanceId = command.targetInstanceId;
    record.abilityType = command.type;
    record.weaponSlot = weaponSlotForAbility(command);
    record.pathRequestAbilityType = command.type;
    record.passabilityProfileHash = passabilityProfileHashForPeer(state);
    record.pathFallbackMode = command.type == "ATTACK" ? "attack-ring" : "none";

    const ReplayUnitState* unit = findUnit(state, command.subjectInstanceId);
    if (unit == nullptr || !unit->alive) {
        record.selectedCapabilityHash = "missing-subject";
        record.missionBefore = "MissingSubject";
        record.missionAfter = "MissingSubject";
        return record;
    }

    record.commandAccepted = true;
    record.selectedCapabilityHash = capabilityHashForUnit(*unit);
    record.weaponId = weaponIdForAbility(*unit, command);
    record.projectileId = projectileIdForAbility(*unit, command);
    record.projectileImageId = projectileImageIdForAbility(*unit, command);
    record.warheadId = warheadIdForAbility(*unit, command);
    record.trailerId = trailerIdForAbility(*unit, command);
    record.impactId = impactIdForAbility(*unit, command);
    record.weaponSelected = !record.weaponId.empty();
    record.projectileSpawned =
        command.type == "ATTACK" && !record.projectileId.empty();
    record.impactResolved =
        command.type == "ATTACK" && !record.impactId.empty();
    if (record.projectileSpawned) {
        record.projectileActorId = sandboxProjectileActorId(
            command.subjectInstanceId,
            command.targetInstanceId,
            command.tick,
            command.sequence
        );
        record.projectileSpawnTick = command.tick;
        record.impactTick = command.tick;
        record.expiredTick = command.tick;
    }
    record.renderIntentExpected =
        record.commandAccepted
        && (command.type == "DEPLOY" || command.type == "ATTACK");
    record.missionBefore = unit->missionState;
    record.missionAfter = unit->missionState;
    return record;
}

void completeCapabilityIntentRecord(
    PeerState& state,
    const ReplayCommandRecord& command,
    ReplayCapabilitySummaryRecord& record
) {
    const ReplayUnitState* unit = findUnit(state, command.subjectInstanceId);
    record.missionAfter =
        unit == nullptr || !unit->alive
            ? "MissingSubject"
            : unit->missionState;
    record.missionChanged = record.missionBefore != record.missionAfter;
    record.pathIntentHash = formatHash(hashPathIntentForCommand(state, command));
}

std::vector<ReplayCapabilitySummaryRecord> applyCommandsAndCaptureIntents(
    PeerState& peer,
    const std::vector<ReplayCommandRecord>& commands,
    const std::vector<std::uint64_t>& cachedPathKeys,
    bool pathBudgetExhausted,
    const RuntimeReplaySourceView& runtimeSource
) {
    std::vector<ReplayCapabilitySummaryRecord> records;
    records.reserve(commands.size());

    for (const ReplayCommandRecord& command : commands) {
        ReplayCapabilitySummaryRecord record =
            beginCapabilityIntentRecord(peer, command);
        applyCommand(peer, command);
        completeCapabilityIntentRecord(peer, command, record);
        applyPathDiagnostics(record, cachedPathKeys, pathBudgetExhausted);
        if (record.renderIntentExpected) {
            record.renderIntentId = renderIntentIdForRecord(record);
        }
        const bool runtimeRenderProvided =
            applyRuntimeReplayEvidence(record, runtimeSource);
        if (!runtimeRenderProvided) {
            record.renderIntentHash =
                formatHash(hashRenderIntentForRecord(peer, record));
        }
        updateChainDiagnosticHash(record);
        if (ReplayUnitState* unit = findUnit(peer, command.subjectInstanceId)) {
            unit->lastPathIntentHash = record.pathIntentHash;
            unit->lastRenderIntentHash = record.renderIntentHash;
            unit->lastChainDiagnosticHash = record.chainDiagnosticHash;
        }
        records.push_back(record);
    }

    return records;
}

ReplayCapabilityIntentRecord materializeCapabilityIntentRecord(
    const ReplayCapabilitySummaryRecord& summary
) {
    ReplayCapabilityIntentRecord record;
    record.tick = summary.tick;
    record.playerId = summary.playerId;
    record.sequence = summary.sequence;
    record.subjectInstanceId = summary.subjectInstanceId;
    record.targetInstanceId = summary.targetInstanceId;
    record.selectedCapabilityHash = summary.selectedCapabilityHash;
    record.abilityType = summary.abilityType;
    record.weaponSlot = summary.weaponSlot;
    record.weaponId = summary.weaponId;
    record.projectileActorId = summary.projectileActorId;
    record.projectileId = summary.projectileId;
    record.projectileImageId = summary.projectileImageId;
    record.warheadId = summary.warheadId;
    record.missionBefore = summary.missionBefore;
    record.missionAfter = summary.missionAfter;
    record.pathIntentHash = summary.pathIntentHash;
    record.renderIntentHash = summary.renderIntentHash;
    record.trailerId = summary.trailerId;
    record.impactId = summary.impactId;
    record.pathRequestAbilityType = summary.pathRequestAbilityType;
    record.passabilityProfileHash = summary.passabilityProfileHash;
    record.pathFallbackMode = summary.pathFallbackMode;
    record.pathDiagnosticHash = summary.pathDiagnosticHash;
    record.chainDiagnosticHash = summary.chainDiagnosticHash;
    record.chainFieldSource = summary.chainFieldSource;
    record.projectileSpawnTick = summary.projectileSpawnTick;
    record.impactTick = summary.impactTick;
    record.expiredTick = summary.expiredTick;
    record.renderIntentId = summary.renderIntentId;
    record.commandAccepted = summary.commandAccepted;
    record.missionChanged = summary.missionChanged;
    record.weaponSelected = summary.weaponSelected;
    record.projectileSpawned = summary.projectileSpawned;
    record.impactResolved = summary.impactResolved;
    record.pathQueued = summary.pathQueued;
    record.pathCacheHit = summary.pathCacheHit;
    record.pathCacheMiss = summary.pathCacheMiss;
    record.pathBudgetExhausted = summary.pathBudgetExhausted;
    record.footprintRingFallback = summary.footprintRingFallback;
    record.renderIntentExpected = summary.renderIntentExpected;
    record.runtimeChainFieldsAvailable = summary.runtimeChainFieldsAvailable;
    record.sandboxFallbackFields = summary.sandboxFallbackFields;
    return record;
}

std::uint64_t hashCapabilityIntentRecords(
    const std::vector<ReplayCapabilitySummaryRecord>& records
) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "capability-intents-v1");
    for (const ReplayCapabilitySummaryRecord& record : records) {
        hashInt(hash, record.tick);
        hashInt(hash, record.playerId);
        hashInt(hash, record.sequence);
        hashInt(hash, record.subjectInstanceId);
        hashInt(hash, record.targetInstanceId);
        hashString(hash, record.selectedCapabilityHash);
        hashString(hash, record.abilityType);
        hashString(hash, record.weaponSlot);
        hashString(hash, record.weaponId);
        hashInt(hash, record.projectileActorId);
        hashString(hash, record.projectileId);
        hashString(hash, record.projectileImageId);
        hashString(hash, record.warheadId);
        hashString(hash, record.trailerId);
        hashString(hash, record.impactId);
        hashString(hash, record.missionBefore);
        hashString(hash, record.missionAfter);
        hashString(hash, record.pathIntentHash);
        hashString(hash, record.renderIntentHash);
        hashString(hash, record.renderIntentId);
        hashString(hash, record.pathRequestAbilityType);
        hashString(hash, record.passabilityProfileHash);
        hashString(hash, record.pathFallbackMode);
        hashString(hash, record.pathDiagnosticHash);
        hashString(hash, record.chainDiagnosticHash);
        hashString(hash, record.chainFieldSource);
        hashInt(hash, record.projectileSpawnTick);
        hashInt(hash, record.impactTick);
        hashInt(hash, record.expiredTick);
        hashInt(hash, record.commandAccepted ? 1 : 0);
        hashInt(hash, record.missionChanged ? 1 : 0);
        hashInt(hash, record.weaponSelected ? 1 : 0);
        hashInt(hash, record.projectileSpawned ? 1 : 0);
        hashInt(hash, record.impactResolved ? 1 : 0);
        hashInt(hash, record.pathQueued ? 1 : 0);
        hashInt(hash, record.pathCacheHit ? 1 : 0);
        hashInt(hash, record.pathCacheMiss ? 1 : 0);
        hashInt(hash, record.pathBudgetExhausted ? 1 : 0);
        hashInt(hash, record.footprintRingFallback ? 1 : 0);
        hashInt(hash, record.renderIntentExpected ? 1 : 0);
        hashInt(hash, record.runtimeChainFieldsAvailable ? 1 : 0);
        hashInt(hash, record.sandboxFallbackFields ? 1 : 0);
    }
    return hash;
}

ReplayHashFrame makeCapabilityIntentFrame(
    int tick,
    int commandsApplied,
    const std::vector<std::vector<ReplayCapabilitySummaryRecord>>& peerRecords
) {
    ReplayHashFrame frame;
    frame.tick = tick;
    frame.commandsApplied = commandsApplied;
    for (const std::vector<ReplayCapabilitySummaryRecord>& records : peerRecords) {
        frame.peerHashes.push_back(formatHash(hashCapabilityIntentRecords(records)));
    }
    return frame;
}

std::string describeHashFrameMismatch(
    const std::string& label,
    const ReplayHashFrame& frame
) {
    std::ostringstream output;
    output << label << " desync at tick " << frame.tick;
    if (frame.peerHashes.size() >= 2U) {
        output << " peer0=" << frame.peerHashes[0];
        for (std::size_t index = 1; index < frame.peerHashes.size(); ++index) {
            if (frame.peerHashes[index] != frame.peerHashes[0]) {
                output << " peer" << index << "=" << frame.peerHashes[index];
                break;
            }
        }
    }
    return output.str();
}

void appendStringMismatch(
    std::ostringstream& output,
    const std::string& label,
    const std::string& expected,
    const std::string& actual
) {
    if (expected != actual) {
        output << " " << label << " " << expected << "!=" << actual;
    }
}

void appendBoolMismatch(
    std::ostringstream& output,
    const std::string& label,
    bool expected,
    bool actual
) {
    if (expected != actual) {
        output << " " << label << " "
               << boolToken(expected)
               << "!="
               << boolToken(actual);
    }
}

void appendDesyncClass(
    std::vector<std::string>& classes,
    const std::string& value
) {
    if (std::find(classes.begin(), classes.end(), value) == classes.end()) {
        classes.push_back(value);
    }
}

std::string joinDesyncClasses(const std::vector<std::string>& classes) {
    if (classes.empty()) {
        return "state";
    }

    std::ostringstream output;
    for (std::size_t index = 0; index < classes.size(); ++index) {
        if (index > 0U) {
            output << ",";
        }
        output << classes[index];
    }
    return output.str();
}

std::string capabilityMismatchClasses(
    const ReplayCapabilitySummaryRecord& expected,
    const ReplayCapabilitySummaryRecord& actual
) {
    std::vector<std::string> classes;
    if (expected.selectedCapabilityHash != actual.selectedCapabilityHash
        || expected.abilityType != actual.abilityType
        || expected.commandAccepted != actual.commandAccepted) {
        appendDesyncClass(classes, "command");
    }
    if (expected.missionBefore != actual.missionBefore
        || expected.missionAfter != actual.missionAfter
        || expected.missionChanged != actual.missionChanged) {
        appendDesyncClass(classes, "mission");
    }
    if (expected.pathIntentHash != actual.pathIntentHash
        || expected.pathRequestAbilityType != actual.pathRequestAbilityType
        || expected.passabilityProfileHash != actual.passabilityProfileHash
        || expected.pathFallbackMode != actual.pathFallbackMode
        || expected.pathDiagnosticHash != actual.pathDiagnosticHash
        || expected.pathQueued != actual.pathQueued
        || expected.pathCacheHit != actual.pathCacheHit
        || expected.pathCacheMiss != actual.pathCacheMiss
        || expected.footprintRingFallback != actual.footprintRingFallback) {
        appendDesyncClass(classes, "path queue");
    }
    if (expected.pathBudgetExhausted != actual.pathBudgetExhausted) {
        appendDesyncClass(classes, "budget");
    }
    if (expected.weaponSlot != actual.weaponSlot
        || expected.weaponId != actual.weaponId
        || expected.projectileActorId != actual.projectileActorId
        || expected.projectileId != actual.projectileId
        || expected.projectileImageId != actual.projectileImageId
        || expected.warheadId != actual.warheadId
        || expected.projectileSpawnTick != actual.projectileSpawnTick
        || expected.weaponSelected != actual.weaponSelected
        || expected.projectileSpawned != actual.projectileSpawned) {
        appendDesyncClass(classes, "projectile spawn");
    }
    if (expected.impactId != actual.impactId
        || expected.impactTick != actual.impactTick
        || expected.expiredTick != actual.expiredTick
        || expected.impactResolved != actual.impactResolved) {
        appendDesyncClass(classes, "impact");
    }
    if (expected.renderIntentHash != actual.renderIntentHash
        || expected.renderIntentId != actual.renderIntentId
        || expected.renderIntentExpected != actual.renderIntentExpected) {
        appendDesyncClass(classes, "render");
    }
    return joinDesyncClasses(classes);
}

std::string describeCapabilityRecordMismatch(
    const ReplayCapabilitySummaryRecord& expected,
    const ReplayCapabilitySummaryRecord& actual
) {
    std::ostringstream output;
    output << "capability record tick="
           << expected.tick
           << " subject=#"
           << expected.subjectInstanceId
           << " class="
           << capabilityMismatchClasses(expected, actual);
    appendStringMismatch(
        output,
        "capability",
        expected.selectedCapabilityHash,
        actual.selectedCapabilityHash
    );
    appendStringMismatch(output, "ability", expected.abilityType, actual.abilityType);
    appendStringMismatch(output, "slot", expected.weaponSlot, actual.weaponSlot);
    appendStringMismatch(output, "weapon", expected.weaponId, actual.weaponId);
    if (expected.projectileActorId != actual.projectileActorId) {
        output << " projectileActor "
               << expected.projectileActorId
               << "!="
               << actual.projectileActorId;
    }
    appendStringMismatch(
        output,
        "projectile",
        expected.projectileId,
        actual.projectileId
    );
    appendStringMismatch(
        output,
        "projectileImage",
        expected.projectileImageId,
        actual.projectileImageId
    );
    appendStringMismatch(output, "warhead", expected.warheadId, actual.warheadId);
    appendStringMismatch(output, "trailer", expected.trailerId, actual.trailerId);
    appendStringMismatch(output, "impact", expected.impactId, actual.impactId);
    appendStringMismatch(
        output,
        "missionBefore",
        expected.missionBefore,
        actual.missionBefore
    );
    appendStringMismatch(
        output,
        "missionAfter",
        expected.missionAfter,
        actual.missionAfter
    );
    appendStringMismatch(output, "path", expected.pathIntentHash, actual.pathIntentHash);
    appendStringMismatch(
        output,
        "pathDiag",
        expected.pathDiagnosticHash,
        actual.pathDiagnosticHash
    );
    appendStringMismatch(
        output,
        "render",
        expected.renderIntentHash,
        actual.renderIntentHash
    );
    appendStringMismatch(
        output,
        "renderId",
        expected.renderIntentId,
        actual.renderIntentId
    );
    appendStringMismatch(
        output,
        "chain",
        expected.chainDiagnosticHash,
        actual.chainDiagnosticHash
    );
    appendStringMismatch(
        output,
        "fieldSource",
        expected.chainFieldSource,
        actual.chainFieldSource
    );
    if (expected.projectileSpawnTick != actual.projectileSpawnTick) {
        output << " spawnTick "
               << expected.projectileSpawnTick
               << "!="
               << actual.projectileSpawnTick;
    }
    if (expected.impactTick != actual.impactTick) {
        output << " impactTick "
               << expected.impactTick
               << "!="
               << actual.impactTick;
    }
    if (expected.expiredTick != actual.expiredTick) {
        output << " expiredTick "
               << expected.expiredTick
               << "!="
               << actual.expiredTick;
    }
    appendBoolMismatch(
        output,
        "commandAccepted",
        expected.commandAccepted,
        actual.commandAccepted
    );
    appendBoolMismatch(
        output,
        "missionChanged",
        expected.missionChanged,
        actual.missionChanged
    );
    appendBoolMismatch(
        output,
        "weaponSelected",
        expected.weaponSelected,
        actual.weaponSelected
    );
    appendBoolMismatch(
        output,
        "projectileSpawned",
        expected.projectileSpawned,
        actual.projectileSpawned
    );
    appendBoolMismatch(
        output,
        "impactResolved",
        expected.impactResolved,
        actual.impactResolved
    );
    appendBoolMismatch(output, "pathQueued", expected.pathQueued, actual.pathQueued);
    appendBoolMismatch(output, "pathCacheHit", expected.pathCacheHit, actual.pathCacheHit);
    appendBoolMismatch(
        output,
        "pathBudget",
        expected.pathBudgetExhausted,
        actual.pathBudgetExhausted
    );
    appendBoolMismatch(
        output,
        "renderExpected",
        expected.renderIntentExpected,
        actual.renderIntentExpected
    );
    return output.str();
}

std::string describeCapabilityIntentMismatch(
    const ReplayHashFrame& frame,
    const std::vector<std::vector<ReplayCapabilitySummaryRecord>>& peerRecords
) {
    std::ostringstream output;
    output << describeHashFrameMismatch("capability intent", frame);
    if (peerRecords.size() < 2U) {
        return output.str();
    }

    const std::vector<ReplayCapabilitySummaryRecord>& expected =
        peerRecords.front();
    for (std::size_t peerIndex = 1; peerIndex < peerRecords.size(); ++peerIndex) {
        const std::vector<ReplayCapabilitySummaryRecord>& actual =
            peerRecords[peerIndex];
        if (actual.size() != expected.size()) {
            output << "; peer" << peerIndex
                   << " record count "
                   << actual.size()
                   << "!="
                   << expected.size();
            return output.str();
        }

        for (std::size_t recordIndex = 0; recordIndex < expected.size(); ++recordIndex) {
            const std::vector<ReplayCapabilitySummaryRecord> expectedOne = {
                expected[recordIndex]
            };
            const std::vector<ReplayCapabilitySummaryRecord> actualOne = {
                actual[recordIndex]
            };
            if (hashCapabilityIntentRecords(expectedOne)
                != hashCapabilityIntentRecords(actualOne)) {
                output << "; peer" << peerIndex << " "
                       << describeCapabilityRecordMismatch(
                              expected[recordIndex],
                              actual[recordIndex]
                          );
                return output.str();
            }
        }
    }

    return output.str();
}

std::vector<ReplayUnitState> sortedUnitsByInstance(
    const std::vector<ReplayUnitState>& units
) {
    std::vector<ReplayUnitState> sorted = units;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const ReplayUnitState& left, const ReplayUnitState& right) {
            return left.instanceId < right.instanceId;
        }
    );
    return sorted;
}

std::string unitMismatchClasses(
    const ReplayUnitState& left,
    const ReplayUnitState& right
) {
    std::vector<std::string> classes;
    if (left.playerId != right.playerId
        || left.typeId != right.typeId
        || left.targetInstanceId != right.targetInstanceId) {
        appendDesyncClass(classes, "command");
    }
    if (left.missionState != right.missionState) {
        appendDesyncClass(classes, "mission");
    }
    if (left.lastPathIntentHash != right.lastPathIntentHash
        || left.x != right.x
        || left.y != right.y
        || left.moveCooldown != right.moveCooldown) {
        appendDesyncClass(classes, "path queue");
    }
    if (left.weaponId != right.weaponId
        || left.projectileId != right.projectileId
        || left.projectileImageId != right.projectileImageId
        || left.warheadId != right.warheadId
        || left.projectileSpawnCount != right.projectileSpawnCount) {
        appendDesyncClass(classes, "projectile spawn");
    }
    if (left.trailerId != right.trailerId
        || left.impactId != right.impactId
        || left.hp != right.hp
        || left.impactResolveCount != right.impactResolveCount
        || left.alive != right.alive) {
        appendDesyncClass(classes, "impact");
    }
    if (left.lastRenderIntentHash != right.lastRenderIntentHash
        || left.lastChainDiagnosticHash != right.lastChainDiagnosticHash) {
        appendDesyncClass(classes, "render");
    }
    return joinDesyncClasses(classes);
}

std::uint64_t hashPeerState(const PeerState& state) {
    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, state.tick);
    hashInt(hash, state.rngState);

    const std::vector<ReplayUnitState> units =
        sortedUnitsByInstance(state.units);
    for (const ReplayUnitState& unit : units) {
        hashInt(hash, unit.instanceId);
        hashInt(hash, unit.playerId);
        hashString(hash, unit.typeId);
        hashString(hash, unit.weaponId);
        hashString(hash, unit.projectileId);
        hashString(hash, unit.projectileImageId);
        hashString(hash, unit.warheadId);
        hashString(hash, unit.trailerId);
        hashString(hash, unit.impactId);
        hashString(hash, unit.missionState);
        hashString(hash, unit.lastPathIntentHash);
        hashString(hash, unit.lastRenderIntentHash);
        hashString(hash, unit.lastChainDiagnosticHash);
        hashInt(hash, unit.hp);
        hashInt(hash, unit.x);
        hashInt(hash, unit.y);
        hashInt(hash, unit.weaponCooldown);
        hashInt(hash, unit.moveCooldown);
        hashInt(hash, unit.targetInstanceId);
        hashInt(hash, unit.projectileSpawnCount);
        hashInt(hash, unit.impactResolveCount);
        hashInt(hash, unit.alive ? 1 : 0);
    }

    return hash;
}

std::uint64_t hashAttackPathIntent(const PeerState& state) {
    std::uint64_t hash = FnvOffsetBasis;
    hashInt(hash, state.tick);

    const std::vector<ReplayUnitState> units =
        sortedUnitsByInstance(state.units);
    for (const ReplayUnitState& unit : units) {
        hashInt(hash, unit.instanceId);
        hashInt(hash, unit.alive ? 1 : 0);
        hashInt(hash, unit.targetInstanceId);
        hashInt(hash, unit.moveCooldown);
        hashString(hash, unit.missionState);
        hashString(hash, unit.weaponId);
        hashString(hash, unit.projectileId);
        hashString(hash, unit.projectileImageId);
        hashString(hash, unit.warheadId);
        hashString(hash, unit.trailerId);
        hashString(hash, unit.impactId);
        hashString(hash, unit.lastPathIntentHash);
        hashString(hash, unit.lastRenderIntentHash);

        const ReplayUnitState* target =
            unit.targetInstanceId == 0
                ? nullptr
                : findUnit(state, unit.targetInstanceId);
        const bool targetAlive = target != nullptr && target->alive;
        hashInt(hash, targetAlive ? 1 : 0);
        if (!unit.alive || !targetAlive) {
            continue;
        }

        const bool inRange = isInRange(unit, *target);
        int nextX = unit.x;
        int nextY = unit.y;
        if (!inRange && unit.moveCooldown <= 0) {
            if (unit.x < target->x) {
                ++nextX;
            } else if (unit.x > target->x) {
                --nextX;
            } else if (unit.y < target->y) {
                ++nextY;
            } else if (unit.y > target->y) {
                --nextY;
            }
        }

        hashInt(hash, inRange ? 1 : 0);
        hashInt(hash, target->x - unit.x);
        hashInt(hash, target->y - unit.y);
        hashInt(hash, nextX);
        hashInt(hash, nextY);
        hashInt(hash, unit.weaponRangeScaled);
    }

    return hash;
}

std::string describeAttackPathIntent(const PeerState& state) {
    for (const ReplayUnitState& unit : sortedUnitsByInstance(state.units)) {
        if (!unit.alive || unit.targetInstanceId == 0) {
            continue;
        }

        const ReplayUnitState* target = findUnit(state, unit.targetInstanceId);
        if (target == nullptr || !target->alive) {
            continue;
        }

        const bool inRange = isInRange(unit, *target);
        int nextX = unit.x;
        int nextY = unit.y;
        if (!inRange && unit.moveCooldown <= 0) {
            if (unit.x < target->x) {
                ++nextX;
            } else if (unit.x > target->x) {
                --nextX;
            } else if (unit.y < target->y) {
                ++nextY;
            } else if (unit.y > target->y) {
                --nextY;
            }
        }

        std::ostringstream output;
        output << "#" << unit.instanceId
               << " target=#" << target->instanceId
               << " mission=" << unit.missionState
               << " weapon=" << unit.weaponId
               << " projectile=" << unit.projectileId
               << " trailer=" << (unit.trailerId.empty() ? "(none)" : unit.trailerId)
               << " impact=" << (unit.impactId.empty() ? "(none)" : unit.impactId)
               << " inRange=" << (inRange ? "yes" : "no")
               << " cooldown=" << unit.moveCooldown
               << " next=(" << nextX << "," << nextY << ")";
        return output.str();
    }

    return "no active attack path intent";
}

std::uint64_t activeAttackPathIntentKey(
    const ReplayUnitState& unit,
    const ReplayUnitState& target
) {
    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "active-attack-path-cache-v1");
    hashInt(hash, unit.instanceId);
    hashInt(hash, target.instanceId);
    hashInt(hash, unit.weaponRangeScaled);
    return hash;
}

std::vector<std::uint64_t> activeAttackPathIntentKeys(const PeerState& state) {
    std::vector<std::uint64_t> keys;
    for (const ReplayUnitState& unit : sortedUnitsByInstance(state.units)) {
        if (!unit.alive || unit.targetInstanceId == 0) {
            continue;
        }

        const ReplayUnitState* target = findUnit(state, unit.targetInstanceId);
        if (target == nullptr || !target->alive || isInRange(unit, *target)) {
            continue;
        }

        keys.push_back(activeAttackPathIntentKey(unit, *target));
    }
    return keys;
}

std::uint64_t hashCommandLog(const std::vector<ReplayCommandRecord>& commands) {
    std::uint64_t hash = FnvOffsetBasis;
    for (const ReplayCommandRecord& command : commands) {
        hashInt(hash, command.tick);
        hashInt(hash, command.playerId);
        hashInt(hash, command.sequence);
        hashString(hash, command.type);
        hashInt(hash, command.subjectInstanceId);
        hashInt(hash, command.targetInstanceId);
    }
    return hash;
}

ReplayHashFrame makeHashFrame(
    int tick,
    int commandsApplied,
    const std::vector<PeerState>& peers
) {
    ReplayHashFrame frame;
    frame.tick = tick;
    frame.commandsApplied = commandsApplied;
    for (const PeerState& peer : peers) {
        frame.peerHashes.push_back(formatHash(hashPeerState(peer)));
    }
    return frame;
}

ReplayHashFrame makeAttackPathIntentFrame(
    int tick,
    int commandsApplied,
    const std::vector<PeerState>& peers
) {
    ReplayHashFrame frame;
    frame.tick = tick;
    frame.commandsApplied = commandsApplied;
    for (const PeerState& peer : peers) {
        frame.peerHashes.push_back(formatHash(hashAttackPathIntent(peer)));
    }
    return frame;
}

bool hashesMatch(const ReplayHashFrame& frame) {
    if (frame.peerHashes.empty()) {
        return true;
    }

    const std::string& first = frame.peerHashes.front();
    return std::all_of(
        frame.peerHashes.begin(),
        frame.peerHashes.end(),
        [&first](const std::string& hash) {
            return hash == first;
        }
    );
}

std::string describeUnitMismatch(
    const ReplayUnitState& left,
    const ReplayUnitState& right
) {
    std::ostringstream output;
    output << "unit #" << left.instanceId;
    if (left.playerId != right.playerId) {
        output << " player " << left.playerId << "!=" << right.playerId;
    }
    if (left.typeId != right.typeId) {
        output << " type " << left.typeId << "!=" << right.typeId;
    }
    if (left.missionState != right.missionState) {
        output << " mission " << left.missionState << "!=" << right.missionState;
    }
    if (left.weaponId != right.weaponId) {
        output << " weapon " << left.weaponId << "!=" << right.weaponId;
    }
    if (left.projectileId != right.projectileId) {
        output << " projectile " << left.projectileId << "!=" << right.projectileId;
    }
    if (left.projectileImageId != right.projectileImageId) {
        output << " projectileImage "
               << left.projectileImageId
               << "!="
               << right.projectileImageId;
    }
    if (left.warheadId != right.warheadId) {
        output << " warhead " << left.warheadId << "!=" << right.warheadId;
    }
    if (left.trailerId != right.trailerId) {
        output << " trailer " << left.trailerId << "!=" << right.trailerId;
    }
    if (left.impactId != right.impactId) {
        output << " impact " << left.impactId << "!=" << right.impactId;
    }
    if (left.lastPathIntentHash != right.lastPathIntentHash) {
        output << " pathIntent " << left.lastPathIntentHash
               << "!=" << right.lastPathIntentHash;
    }
    if (left.lastRenderIntentHash != right.lastRenderIntentHash) {
        output << " renderIntent " << left.lastRenderIntentHash
               << "!=" << right.lastRenderIntentHash;
    }
    if (left.lastChainDiagnosticHash != right.lastChainDiagnosticHash) {
        output << " chain " << left.lastChainDiagnosticHash
               << "!=" << right.lastChainDiagnosticHash;
    }
    if (left.hp != right.hp) {
        output << " hp " << left.hp << "!=" << right.hp;
    }
    if (left.x != right.x || left.y != right.y) {
        output << " pos (" << left.x << "," << left.y << ")!=("
               << right.x << "," << right.y << ")";
    }
    if (left.weaponCooldown != right.weaponCooldown) {
        output << " weaponCooldown " << left.weaponCooldown
               << "!=" << right.weaponCooldown;
    }
    if (left.moveCooldown != right.moveCooldown) {
        output << " moveCooldown " << left.moveCooldown
               << "!=" << right.moveCooldown;
    }
    if (left.targetInstanceId != right.targetInstanceId) {
        output << " target " << left.targetInstanceId
               << "!=" << right.targetInstanceId;
    }
    if (left.projectileSpawnCount != right.projectileSpawnCount) {
        output << " projectileSpawns " << left.projectileSpawnCount
               << "!=" << right.projectileSpawnCount;
    }
    if (left.impactResolveCount != right.impactResolveCount) {
        output << " impacts " << left.impactResolveCount
               << "!=" << right.impactResolveCount;
    }
    if (left.alive != right.alive) {
        output << " alive " << (left.alive ? "yes" : "no")
               << "!=" << (right.alive ? "yes" : "no");
    }
    return output.str();
}

std::string describeDesync(
    const ReplayHashFrame& frame,
    const std::vector<PeerState>& peers
) {
    std::ostringstream output;
    output << "desync at tick " << frame.tick;
    if (frame.peerHashes.size() >= 2U) {
        output << " peer0=" << frame.peerHashes[0];
        for (std::size_t index = 1; index < frame.peerHashes.size(); ++index) {
            if (frame.peerHashes[index] != frame.peerHashes[0]) {
                output << " peer" << index << "=" << frame.peerHashes[index];
                break;
            }
        }
    }

    if (peers.size() < 2U) {
        return output.str();
    }

    const PeerState& expected = peers.front();
    const auto mismatchPeerIt = std::find_if(
        peers.begin() + 1,
        peers.end(),
        [&expected](const PeerState& peer) {
            return hashPeerState(peer) != hashPeerState(expected);
        }
    );
    if (mismatchPeerIt == peers.end()) {
        return output.str();
    }

    const PeerState& actual = *mismatchPeerIt;
    if (expected.tick != actual.tick) {
        output << "; tick " << expected.tick << "!=" << actual.tick;
    }
    if (expected.rngState != actual.rngState) {
        output << "; rng " << expected.rngState << "!=" << actual.rngState;
    }

    const std::vector<ReplayUnitState> expectedUnits =
        sortedUnitsByInstance(expected.units);
    const std::vector<ReplayUnitState> actualUnits =
        sortedUnitsByInstance(actual.units);
    if (expectedUnits.size() != actualUnits.size()) {
        output << "; unit count " << expectedUnits.size()
               << "!=" << actualUnits.size();
        return output.str();
    }

    for (std::size_t index = 0; index < expectedUnits.size(); ++index) {
        const ReplayUnitState& left = expectedUnits[index];
        const ReplayUnitState& right = actualUnits[index];
        if (left.instanceId != right.instanceId
            || left.playerId != right.playerId
            || left.typeId != right.typeId
            || left.missionState != right.missionState
            || left.weaponId != right.weaponId
            || left.projectileId != right.projectileId
            || left.projectileImageId != right.projectileImageId
            || left.warheadId != right.warheadId
            || left.trailerId != right.trailerId
            || left.impactId != right.impactId
            || left.lastPathIntentHash != right.lastPathIntentHash
            || left.lastRenderIntentHash != right.lastRenderIntentHash
            || left.lastChainDiagnosticHash != right.lastChainDiagnosticHash
            || left.hp != right.hp
            || left.x != right.x
            || left.y != right.y
            || left.weaponCooldown != right.weaponCooldown
            || left.moveCooldown != right.moveCooldown
            || left.targetInstanceId != right.targetInstanceId
            || left.projectileSpawnCount != right.projectileSpawnCount
            || left.impactResolveCount != right.impactResolveCount
            || left.alive != right.alive) {
            output << "; class="
                   << unitMismatchClasses(left, right)
                   << " "
                   << describeUnitMismatch(left, right);
            break;
        }
    }

    return output.str();
}

std::string findWinner(const PeerState& state) {
    bool player0Alive = false;
    bool player1Alive = false;

    for (const ReplayUnitState& unit : state.units) {
        if (!unit.alive) {
            continue;
        }

        if (unit.playerId == 0) {
            player0Alive = true;
        } else if (unit.playerId == 1) {
            player1Alive = true;
        }
    }

    if (player0Alive && !player1Alive) {
        return "Player 0";
    }
    if (player1Alive && !player0Alive) {
        return "Player 1";
    }
    if (!player0Alive && !player1Alive) {
        return "draw";
    }

    return "";
}

std::string replayRuntimeSourceEvidence(
    const ReplaySandboxReport& report
) {
    std::ostringstream output;
    output << "runtime source evidence snapshots="
           << report.runtimeEventSource.eventCount
           << " runtimeEvents="
           << report.runtimeEventSource.runtimeEventCount
           << " commandAccepted="
           << report.runtimeEventSource.commandAcceptedCount
           << " missionTransitions="
           << report.runtimeEventSource.missionTransitionCount
           << " projectile spawn/impact/expired="
           << report.runtimeEventSource.projectileSpawnCount
           << "/"
           << report.runtimeEventSource.projectileImpactCount
           << "/"
           << report.runtimeEventSource.projectileExpiredCount
           << " renderIntent="
           << report.runtimeEventSource.renderIntentCount
           << " capabilitySource runtime="
           << report.capabilityRuntimeFieldCount
           << " fallback="
           << report.capabilitySandboxFallbackFieldCount
           << " projectileLifecycle runtime="
           << report.projectileLifecycleRuntimeCount
           << " fallback="
           << report.projectileLifecycleFallbackCount;
    return output.str();
}

std::string desyncClassFromSummary(const std::string& summary) {
    const std::string marker = "class=";
    const std::size_t markerOffset = summary.find(marker);
    if (markerOffset == std::string::npos) {
        return "";
    }

    const std::size_t valueOffset = markerOffset + marker.size();
    const std::size_t endOffset = summary.find(' ', valueOffset);
    return summary.substr(
        valueOffset,
        endOffset == std::string::npos
            ? std::string::npos
            : endOffset - valueOffset
    );
}

std::vector<ReplayCommandRecord> buildCommandLog() {
    return {
        {0, 0, 0, "DEPLOY", 1, 1},
        {1, 0, 1, "ATTACK", 1, 2},
        {1, 1, 2, "ATTACK", 2, 1}
    };
}

bool writeReplayFile(
    const ReplaySandboxReport& report,
    const std::string& outputPath,
    std::string& errorMessage
) {
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        errorMessage = "Failed to open replay output path: " + outputPath;
        return false;
    }

    output << "ConcordRTS Replay v1\n";
    output << "seed=" << report.randomSeed << "\n";
    output << "players=" << report.playerCount << "\n";
    output << "attacker=" << report.attackerId << "\n";
    output << "defender=" << report.defenderId << "\n";
    output << "commandLogHash=" << report.commandLogHash << "\n";
    if (!report.commandIntentHash.empty()) {
        output << "commandIntentHash=" << report.commandIntentHash << "\n";
    }
    if (!report.attackPathIntentHash.empty()) {
        output << "attackPathIntentHash=" << report.attackPathIntentHash << "\n";
    }
    if (!report.capabilityIntentHash.empty()) {
        output << "capabilityIntentHash=" << report.capabilityIntentHash << "\n";
    }
    if (!report.projectileLifecycleHash.empty()) {
        output << "projectileLifecycleHash="
               << report.projectileLifecycleHash
               << "\n";
    }
    output << "capabilityDetailsIncluded="
           << (report.capabilityDetailsIncluded ? "yes" : "no")
           << "\n";
    output << "commandIntentValidated="
           << (report.commandIntentValidated ? "yes" : "no")
           << "\n";
    output << "attackPathIntentValidated="
           << (report.attackPathIntentValidated ? "yes" : "no")
           << "\n";
    output << "capabilityIntentValidated="
           << (report.capabilityIntentValidated ? "yes" : "no")
           << "\n";
    output << "deployIntentStable="
           << (report.deployIntentStable ? "yes" : "no")
           << "\n";
    output << "attackIntentStable="
           << (report.attackIntentStable ? "yes" : "no")
           << "\n";
    output << "projectileIntentStable="
           << (report.projectileIntentStable ? "yes" : "no")
           << "\n";
    output << "pathDiagnosticsStable="
           << (report.pathDiagnosticsStable ? "yes" : "no")
           << "\n";
    output << "commandMissionProjectileChainStable="
           << (report.commandMissionProjectileChainStable ? "yes" : "no")
           << "\n";
    output << "capabilityIntentCount=" << report.capabilityIntentCount << "\n";
    output << "deployIntentCount=" << report.deployIntentCount << "\n";
    output << "attackIntentCount=" << report.attackIntentCount << "\n";
    output << "projectileIntentCount=" << report.projectileIntentCount << "\n";
    output << "commandAcceptedCount=" << report.commandAcceptedCount << "\n";
    output << "missionChangedCount=" << report.missionChangedCount << "\n";
    output << "weaponSelectedCount=" << report.weaponSelectedCount << "\n";
    output << "projectileSpawnedCount=" << report.projectileSpawnedCount << "\n";
    output << "impactResolvedCount=" << report.impactResolvedCount << "\n";
    output << "pathQueuedCount=" << report.pathQueuedCount << "\n";
    output << "renderIntentExpectedCount="
           << report.renderIntentExpectedCount
           << "\n";
    output << "capabilityRuntimeFieldCount="
           << report.capabilityRuntimeFieldCount
           << "\n";
    output << "capabilitySandboxFallbackFieldCount="
           << report.capabilitySandboxFallbackFieldCount
           << "\n";
    output << "projectileLifecycleCount="
           << report.projectileLifecycleCount
           << "\n";
    output << "projectileLifecycleRuntimeCount="
           << report.projectileLifecycleRuntimeCount
           << "\n";
    output << "projectileLifecycleFallbackCount="
           << report.projectileLifecycleFallbackCount
           << "\n";
    output << "runtimeReplayEventCount="
           << report.runtimeEventSource.eventCount
           << "\n";
    output << "runtimeReplayRuntimeEventCount="
           << report.runtimeEventSource.runtimeEventCount
           << "\n";
    output << "runtimeReplaySandboxFallbackCount="
           << report.runtimeEventSource.sandboxFallbackCount
           << "\n";
    output << "runtimeReplayRenderIntentCount="
           << report.runtimeEventSource.renderIntentCount
           << "\n";
    output << "runtimeSourceEvidence="
           << report.runtimeSourceEvidence
           << "\n";
    output << "firstDesyncClass="
           << report.firstDesyncClass
           << "\n";
    output << "capabilityHashFrameCount="
           << report.capabilityHashFrameCount
           << "\n";
    output << "totalHashFrameCount="
           << report.totalHashFrameCount
           << "\n";
    output << "replayPathRequestCount="
           << report.replayPathRequestCount
           << "\n";
    output << "pathIntentSamples=" << report.pathIntentSamples << "\n";
    output << "pathFallbackTargetsTried="
           << report.pathFallbackTargetsTried
           << "\n";
    output << "pathCacheHits=" << report.pathCacheHits << "\n";
    output << "pathCacheMisses=" << report.pathCacheMisses << "\n";
    output << "pathBudgetExhausted="
           << (report.pathBudgetExhausted ? "yes" : "no")
           << "\n";
    output << "ticks=" << report.ticksSimulated << "\n";
    output << "finalHash=" << report.finalStateHash << "\n";
    output << "desyncDetected=" << (report.desyncDetected ? "yes" : "no") << "\n";
    if (!report.desyncSummary.empty()) {
        output << "desyncSummary=" << report.desyncSummary << "\n";
    }
    output << "\n[Commands]\n";
    for (const ReplayCommandRecord& command : report.commands) {
        output << command.tick
               << ","
               << command.playerId
               << ","
               << command.sequence
               << ","
               << command.type
               << ","
               << command.subjectInstanceId
               << ","
               << command.targetInstanceId
               << "\n";
    }

    output << "\n[CapabilitySummary]\n";
    output << "records=" << report.capabilityIntentCount
           << ",accepted=" << report.commandAcceptedCount
           << ",missionChanged=" << report.missionChangedCount
           << ",weaponSelected=" << report.weaponSelectedCount
           << ",projectileSpawned=" << report.projectileSpawnedCount
           << ",impactResolved=" << report.impactResolvedCount
           << ",pathQueued=" << report.pathQueuedCount
           << ",renderExpected=" << report.renderIntentExpectedCount
           << ",hash=" << report.capabilityIntentHash
           << "\n";

    if (report.capabilityDetailsIncluded) {
        output << "\n[CapabilityDetails]\n";
        for (const ReplayCapabilityIntentRecord& intent : report.capabilityIntents) {
            output << intent.tick
                   << ","
                   << intent.playerId
                   << ","
                   << intent.sequence
                   << ","
                   << intent.subjectInstanceId
                   << ","
                   << intent.targetInstanceId
                   << ","
                   << intent.selectedCapabilityHash
                   << ","
                   << intent.abilityType
                   << ","
                   << intent.weaponSlot
                   << ","
                   << intent.weaponId
                   << ","
                   << intent.projectileActorId
                   << ","
                   << intent.projectileId
                   << ","
                   << intent.projectileImageId
                   << ","
                   << intent.warheadId
                   << ","
                   << intent.trailerId
                   << ","
                   << intent.impactId
                   << ","
                   << intent.missionBefore
                   << "->"
                   << intent.missionAfter
                   << ","
                   << intent.pathIntentHash
                   << ","
                   << intent.renderIntentHash
                   << ","
                   << intent.renderIntentId
                   << ","
                   << intent.pathRequestAbilityType
                   << ","
                   << intent.passabilityProfileHash
                   << ","
                   << intent.pathFallbackMode
                   << ","
                   << intent.pathDiagnosticHash
                   << ","
                   << intent.chainDiagnosticHash
                   << ",source="
                   << intent.chainFieldSource
                   << ",spawnTick="
                   << intent.projectileSpawnTick
                   << ",impactTick="
                   << intent.impactTick
                   << ",expiredTick="
                   << intent.expiredTick
                   << ",accepted="
                   << boolToken(intent.commandAccepted)
                   << ",missionChanged="
                   << boolToken(intent.missionChanged)
                   << ",weaponSelected="
                   << boolToken(intent.weaponSelected)
                   << ",projectileSpawned="
                   << boolToken(intent.projectileSpawned)
                   << ",impactResolved="
                   << boolToken(intent.impactResolved)
                   << ",pathQueued="
                   << boolToken(intent.pathQueued)
                   << ",pathCache="
                   << (intent.pathCacheHit ? "hit" : (intent.pathCacheMiss ? "miss" : "n/a"))
                   << ",pathBudgetExhausted="
                   << boolToken(intent.pathBudgetExhausted)
                   << ",footprintRingFallback="
                   << boolToken(intent.footprintRingFallback)
                   << ",renderExpected="
                   << boolToken(intent.renderIntentExpected)
                   << ",runtimeFields="
                   << boolToken(intent.runtimeChainFieldsAvailable)
                   << ",fallbackFields="
                   << boolToken(intent.sandboxFallbackFields)
                   << "\n";
        }
    }

    output << "\n[Hashes]\n";
    for (const ReplayHashFrame& frame : report.hashFrames) {
        output << frame.tick << "," << frame.commandsApplied;
        for (const std::string& hash : frame.peerHashes) {
            output << "," << hash;
        }
        output << "\n";
    }

    return true;
}

std::string trimReplayToken(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    if (last > first + 1
        && ((value[first] == '"' && value[last - 1] == '"')
            || (value[first] == '\'' && value[last - 1] == '\''))) {
        return value.substr(first + 1, last - first - 2);
    }
    return value.substr(first, last - first);
}

std::string stripReplayLineComment(const std::string& line) {
    const std::size_t hash = line.find('#');
    return hash == std::string::npos ? line : line.substr(0, hash);
}

bool parseReplayInt(const std::string& value, int& output) {
    const std::string trimmed = trimReplayToken(value);
    try {
        std::size_t consumed = 0;
        const long long parsed = std::stoll(trimmed, &consumed, 0);
        if (consumed != trimmed.size()) {
            return false;
        }
        output = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseReplayUint64(const std::string& value, std::uint64_t& output) {
    const std::string trimmed = trimReplayToken(value);
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(trimmed, &consumed, 0);
        if (consumed != trimmed.size()) {
            return false;
        }
        output = static_cast<std::uint64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseReplayCell(const std::string& value, RuntimeSnapshotCell& output) {
    std::string normalized = trimReplayToken(value);
    for (char& character : normalized) {
        if (character == ':' || character == '/') {
            character = ' ';
        }
    }

    std::istringstream stream(normalized);
    int x = -1;
    int y = -1;
    if (!(stream >> x >> y)) {
        return false;
    }

    output.x = x;
    output.y = y;
    return true;
}

bool assignRuntimeReplayEventField(
    RuntimeReplayEventSnapshot& event,
    const std::string& rawKey,
    const std::string& value
) {
    const std::string key = lowerAscii(trimReplayToken(rawKey));
    if (key == "sequence" || key == "seq") {
        return parseReplayUint64(value, event.sequence);
    }
    if (key == "tick") {
        return parseReplayInt(value, event.tick);
    }
    if (key == "kind" || key == "event" || key == "type") {
        event.kind = trimReplayToken(value);
        return true;
    }
    if (key == "objectid" || key == "object") {
        return parseReplayInt(value, event.objectId);
    }
    if (key == "targetobjectid" || key == "target") {
        return parseReplayInt(value, event.targetObjectId);
    }
    if (key == "projectileactorid" || key == "projectileactor") {
        return parseReplayInt(value, event.projectileActorId);
    }
    if (key == "commandverb" || key == "verb") {
        event.commandVerb = trimReplayToken(value);
        return true;
    }
    if (key == "selectedcount") {
        return parseReplayInt(value, event.selectedCount);
    }
    if (key == "acceptedcount") {
        return parseReplayInt(value, event.acceptedCount);
    }
    if (key == "rejectedcount") {
        return parseReplayInt(value, event.rejectedCount);
    }
    if (key == "commandtargetcell") {
        return parseReplayCell(value, event.commandTargetCell);
    }
    if (key == "missionfrom") {
        event.missionFrom = trimReplayToken(value);
        return true;
    }
    if (key == "missionto") {
        event.missionTo = trimReplayToken(value);
        return true;
    }
    if (key == "reason") {
        event.reason = trimReplayToken(value);
        return true;
    }
    if (key == "weaponid") {
        event.weaponId = trimReplayToken(value);
        return true;
    }
    if (key == "weaponslot") {
        event.weaponSlot = trimReplayToken(value);
        return true;
    }
    if (key == "projectileid") {
        event.projectileId = trimReplayToken(value);
        return true;
    }
    if (key == "projectileimageid") {
        event.projectileImageId = trimReplayToken(value);
        return true;
    }
    if (key == "warheadid") {
        event.warheadId = trimReplayToken(value);
        return true;
    }
    if (key == "lifecyclestate") {
        event.lifecycleState = trimReplayToken(value);
        return true;
    }
    if (key == "spawntick") {
        return parseReplayInt(value, event.spawnTick);
    }
    if (key == "impacttick") {
        return parseReplayInt(value, event.impactTick);
    }
    if (key == "expiredtick") {
        return parseReplayInt(value, event.expiredTick);
    }
    if (key == "renderintentid") {
        event.renderIntentId = trimReplayToken(value);
        return true;
    }
    if (key == "renderintenthash") {
        return parseReplayUint64(value, event.renderIntentHash);
    }
    if (key == "trailerintentid") {
        event.trailerIntentId = trimReplayToken(value);
        return true;
    }
    if (key == "impactintentid") {
        event.impactIntentId = trimReplayToken(value);
        return true;
    }
    if (key == "warheadimpactintentid") {
        event.warheadImpactIntentId = trimReplayToken(value);
        return true;
    }
    if (key == "sourcecell") {
        return parseReplayCell(value, event.sourceCell);
    }
    if (key == "targetcell") {
        return parseReplayCell(value, event.targetCell);
    }
    if (key == "currentcell") {
        return parseReplayCell(value, event.currentCell);
    }

    return true;
}

bool parseRuntimeReplayEventLine(
    const std::string& line,
    RuntimeReplayEventSnapshot& event,
    std::string& errorMessage
) {
    std::string normalized = stripReplayLineComment(line);
    for (char& character : normalized) {
        if (character == ',' || character == ';') {
            character = ' ';
        }
    }

    std::istringstream stream(normalized);
    std::string token;
    bool parsedField = false;
    while (stream >> token) {
        const std::size_t equals = token.find('=');
        if (equals == std::string::npos) {
            errorMessage = "expected key=value token";
            return false;
        }

        if (!assignRuntimeReplayEventField(
                event,
                token.substr(0, equals),
                token.substr(equals + 1)
            )) {
            errorMessage = "invalid value for key " + token.substr(0, equals);
            return false;
        }
        parsedField = true;
    }

    if (!parsedField) {
        errorMessage.clear();
        return true;
    }
    if (event.kind.empty()) {
        errorMessage = "runtime replay event is missing kind";
        return false;
    }
    return true;
}

} // namespace

ReplayProjectileLifecycleSummary summarizeProjectileLifecycleRecords(
    const std::vector<ReplayProjectileLifecycleRecord>& records
) {
    ReplayProjectileLifecycleSummary summary;
    summary.count = static_cast<int>(records.size());

    std::uint64_t hash = FnvOffsetBasis;
    hashString(hash, "projectile-lifecycle-v1");
    for (const ReplayProjectileLifecycleRecord& record : records) {
        if (record.runtimeFieldsAvailable
            || record.fieldSource == "runtime"
            || record.fieldSource == "runtime-partial") {
            ++summary.runtimeSourceCount;
        }
        if (record.sandboxFallbackFields || record.fieldSource == "sandbox-fallback") {
            ++summary.sandboxFallbackCount;
        }
        hashProjectileLifecycleRecord(hash, record);
    }
    summary.hash = formatHash(hash);
    return summary;
}

ReplayRuntimeEventSourceSummary summarizeRuntimeReplayEventSnapshots(
    const RuntimeReplayEventSnapshot* events,
    std::size_t count
) {
    ReplayRuntimeEventSourceSummary summary;
    ReplayProjectileLifecycleSummary lifecycleSummary;
    std::uint64_t lifecycleHash = FnvOffsetBasis;
    hashString(lifecycleHash, "projectile-lifecycle-v1");

    if (events == nullptr || count == 0U) {
        lifecycleSummary.hash = formatHash(lifecycleHash);
        summary.projectileLifecycle = lifecycleSummary;
        return summary;
    }

    for (std::size_t index = 0; index < count; ++index) {
        const RuntimeReplayEventSnapshot& event = events[index];
        ++summary.eventCount;
        ++summary.runtimeEventCount;

        if (event.kind == "CommandAccepted" && event.acceptedCount > 0) {
            summary.commandAcceptedCount += event.acceptedCount;
        }
        if (event.kind == "MissionTransition") {
            ++summary.missionTransitionCount;
        }
        if (event.renderIntentHash != 0 || !event.renderIntentId.empty()) {
            ++summary.renderIntentCount;
        }

        const bool lifecycleEvent =
            event.kind == "ProjectileSpawned"
            || event.kind == "ProjectileImpact"
            || event.kind == "ProjectileExpired";
        if (!lifecycleEvent) {
            continue;
        }

        ReplayProjectileLifecycleRecord record;
        record.projectileActorId = event.projectileActorId;
        record.ownerInstanceId = event.objectId;
        record.targetInstanceId = event.targetObjectId;
        record.spawnTick =
            event.spawnTick >= 0 ? event.spawnTick : event.tick;
        record.impactTick =
            event.impactTick >= 0
                ? event.impactTick
                : (event.kind == "ProjectileImpact"
                    || event.kind == "ProjectileExpired" ? event.tick : -1);
        record.expiredTick =
            event.expiredTick >= 0
                ? event.expiredTick
                : (event.kind == "ProjectileExpired" ? event.tick : -1);
        record.weaponSlot = event.weaponSlot;
        record.weaponId = event.weaponId;
        record.projectileId = event.projectileId;
        record.projectileImageId = event.projectileImageId;
        record.warheadId = event.warheadId;
        record.impactId =
            !event.impactIntentId.empty()
                ? event.impactIntentId
                : event.warheadImpactIntentId;
        record.renderIntentId = event.renderIntentId;
        record.renderIntentHash =
            event.renderIntentHash == 0 ? "" : formatHash(event.renderIntentHash);
        record.fieldSource = "runtime";

        const bool hasLifecycleTick =
            record.spawnTick >= 0
            && (event.kind == "ProjectileSpawned"
                || record.impactTick >= 0
                || record.expiredTick >= 0);
        const bool hasRequiredRuntimeFields =
            record.projectileActorId >= 0
            && hasLifecycleTick
            && !record.weaponId.empty()
            && !record.projectileId.empty()
            && !record.projectileImageId.empty()
            && !record.warheadId.empty()
            && (!record.renderIntentId.empty()
                || !record.renderIntentHash.empty());
        record.runtimeFieldsAvailable = hasRequiredRuntimeFields;
        record.sandboxFallbackFields = !hasRequiredRuntimeFields;
        if (!hasRequiredRuntimeFields) {
            record.fieldSource = "runtime-partial";
            ++summary.sandboxFallbackCount;
        }

        if (event.kind == "ProjectileSpawned") {
            ++summary.projectileSpawnCount;
        } else if (event.kind == "ProjectileImpact") {
            ++summary.projectileImpactCount;
        } else if (event.kind == "ProjectileExpired") {
            ++summary.projectileExpiredCount;
        }
        ++lifecycleSummary.count;
        if (record.runtimeFieldsAvailable
            || record.fieldSource == "runtime"
            || record.fieldSource == "runtime-partial") {
            ++lifecycleSummary.runtimeSourceCount;
        }
        if (record.sandboxFallbackFields
            || record.fieldSource == "sandbox-fallback") {
            ++lifecycleSummary.sandboxFallbackCount;
        }
        hashProjectileLifecycleRecord(lifecycleHash, record);
    }

    lifecycleSummary.hash = formatHash(lifecycleHash);
    summary.projectileLifecycle = lifecycleSummary;
    return summary;
}

ReplaySandboxReport runReplaySandbox(
    const RulesDatabase& rules,
    const ReplaySandboxConfig& config
) {
    ReplaySandboxReport report;
    report.attackerId = config.attackerId;
    report.defenderId = config.defenderId;
    report.playerCount = std::max(2, config.playerCount);
    report.randomSeed = config.randomSeed;
    report.expectedDesync = config.desyncAtTick >= 0;
    report.capabilityDetailsIncluded = config.includeCapabilityDetails;
    report.commands = buildCommandLog();
    report.commandCount = static_cast<int>(report.commands.size());
    report.commandLogHash = formatHash(hashCommandLog(report.commands));
    std::vector<RuntimeReplayEventSnapshot> importedRuntimeEvents;
    RuntimeReplaySourceView runtimeSource;
    runtimeSource.events = config.runtimeEventSnapshots;
    runtimeSource.count = config.runtimeEventSnapshotCount;
    if (runtimeSource.events == nullptr || runtimeSource.count == 0U) {
        std::string runtimeEventFilePath;
        if (const char* environmentPath =
                std::getenv("CONCORD_REPLAY_RUNTIME_EVENTS")) {
            runtimeEventFilePath = environmentPath;
        }
        if (runtimeEventFilePath.empty()) {
            runtimeEventFilePath = findDefaultRuntimeReplayEventFile();
        }
        std::string loadError;
        if (!runtimeEventFilePath.empty()
            && loadRuntimeReplayEventSnapshotsFromFile(
                runtimeEventFilePath,
                importedRuntimeEvents,
                loadError
            )) {
            runtimeSource.events = importedRuntimeEvents.data();
            runtimeSource.count = importedRuntimeEvents.size();
        }
    }
    runtimeSource.summary = summarizeRuntimeReplayEventSnapshots(
        runtimeSource.events,
        runtimeSource.count
    );
    report.runtimeEventSource = runtimeSource.summary;
    report.lockstepRules = {
        "all peers receive identical command records before a tick is simulated",
        "commands are applied in tick/player/sequence order",
        "selected object capability hashes are recorded before ability application",
        "issued ability, weapon slot/id, projectile actor/id/image, warhead, trailer, impact, mission transition, path intent, and render intent are hashed per command",
        "projectile lifecycle records include spawn tick, impact tick, render intent hash, and explicit runtime/fallback field source",
        "command accepted, mission changed, weapon selected, projectile spawned, impact resolved, path queued, and render expected are recorded as chain diagnostics",
        "deploy, attack, projectile, path budget/cache, and command-to-mission-to-projectile diagnostics must stay stable across peers",
        "path and attack intents are hashed before movement and weapon simulation",
        "attack pursuit intent hashes include target, range state, and next path step",
        "path diagnostics record cache hit/miss, fallback attempts, and budget exhaustion",
        "deterministic random state is part of the state hash",
        "gameplay changes commit only inside the fixed tick",
        "peer hashes must match after every committed tick"
    };
    report.lockstepRules.push_back(
        "runtime replay source evidence snapshots="
        + std::to_string(report.runtimeEventSource.eventCount)
        + " runtimeEvents="
        + std::to_string(report.runtimeEventSource.runtimeEventCount)
        + " projectileLifecycle="
        + std::to_string(report.runtimeEventSource.projectileLifecycle.count)
        + " fallback only fills missing runtime fields"
    );

    ReplayUnitState attacker;
    ReplayUnitState defender;
    std::string errorMessage;

    if (!loadReplayUnit(
            rules,
            1,
            0,
            config.attackerId,
            0,
            0,
            config.defenderId,
            attacker,
            errorMessage
        )) {
        report.errorMessage = errorMessage;
        return report;
    }

    if (!loadReplayUnit(
            rules,
            2,
            1,
            config.defenderId,
            std::max(1, config.startDistanceCells),
            0,
            config.attackerId,
            defender,
            errorMessage
        )) {
        report.errorMessage = errorMessage;
        return report;
    }

    std::vector<PeerState> peers;
    for (int peerId = 0; peerId < report.playerCount; ++peerId) {
        PeerState peer;
        peer.peerId = peerId;
        peer.rngState =
            static_cast<std::uint32_t>(config.randomSeed)
            ^ static_cast<std::uint32_t>(0x9e3779b9u);
        peer.units = {attacker, defender};
        peers.push_back(peer);
    }

    appendEvent(report, 0, "command log hash=" + report.commandLogHash);

    bool desyncInjected = false;
    const int maxTicks = std::max(1, config.maxTicks);
    std::vector<std::uint64_t> pathIntentCacheKeys;
    std::vector<std::uint64_t> capabilityPathCacheKeys;
    std::uint64_t projectileLifecycleHash = FnvOffsetBasis;
    hashString(projectileLifecycleHash, "projectile-lifecycle-v1");
    int projectileLifecycleCount = 0;
    int projectileLifecycleRuntimeCount = 0;
    int projectileLifecycleFallbackCount = 0;

    for (int tick = 0; tick <= maxTicks; ++tick) {
        std::vector<ReplayCommandRecord> dueCommands;
        for (const ReplayCommandRecord& command : report.commands) {
            if (command.tick == tick) {
                dueCommands.push_back(command);
            }
        }

        std::sort(
            dueCommands.begin(),
            dueCommands.end(),
            [](const ReplayCommandRecord& left, const ReplayCommandRecord& right) {
                if (left.tick != right.tick) {
                    return left.tick < right.tick;
                }
                if (left.playerId != right.playerId) {
                    return left.playerId < right.playerId;
                }
                return left.sequence < right.sequence;
            }
        );

        if (!dueCommands.empty()) {
            for (const ReplayCommandRecord& command : dueCommands) {
                appendEvent(
                    report,
                    tick,
                    "command p"
                    + std::to_string(command.playerId)
                    + " "
                    + command.type
                    + " #"
                    + std::to_string(command.subjectInstanceId)
                    + " -> #"
                    + std::to_string(command.targetInstanceId)
                );
            }
        }

        std::vector<std::vector<ReplayCapabilitySummaryRecord>> peerCapabilityRecords;
        peerCapabilityRecords.reserve(peers.size());
        for (PeerState& peer : peers) {
            peer.tick = tick;
            peerCapabilityRecords.push_back(
                applyCommandsAndCaptureIntents(
                    peer,
                    dueCommands,
                    capabilityPathCacheKeys,
                    report.pathBudgetExhausted,
                    runtimeSource
                )
            );
        }

        if (!dueCommands.empty()) {
            ReplayHashFrame capabilityFrame =
                makeCapabilityIntentFrame(
                    tick,
                    static_cast<int>(dueCommands.size()),
                    peerCapabilityRecords
                );
            report.capabilityIntentHash =
                capabilityFrame.peerHashes.empty()
                    ? ""
                    : capabilityFrame.peerHashes.front();
            report.capabilityIntentValidated = hashesMatch(capabilityFrame);
            ++report.capabilityHashFrameCount;
            if (!peerCapabilityRecords.empty()) {
                int tickAccepted = 0;
                int tickMissionChanged = 0;
                int tickPathQueued = 0;
                int tickPathCacheHits = 0;
                int tickPathCacheMisses = 0;
                int tickRenderExpected = 0;
                for (const ReplayCapabilitySummaryRecord& intent :
                     peerCapabilityRecords.front()) {
                    if (report.capabilityDetailsIncluded) {
                        report.capabilityIntents.push_back(
                            materializeCapabilityIntentRecord(intent)
                        );
                    }
                    ++report.capabilityIntentCount;
                    if (intent.abilityType == "DEPLOY") {
                        ++report.deployIntentCount;
                    } else if (intent.abilityType == "ATTACK") {
                        ++report.attackIntentCount;
                        ++report.projectileIntentCount;
                    }
                    if (intent.commandAccepted) {
                        ++report.commandAcceptedCount;
                        ++tickAccepted;
                    }
                    if (intent.runtimeChainFieldsAvailable
                        || intent.chainFieldSource == "runtime-partial") {
                        ++report.capabilityRuntimeFieldCount;
                    }
                    if (intent.sandboxFallbackFields) {
                        ++report.capabilitySandboxFallbackFieldCount;
                    }
                    if (intent.missionChanged) {
                        ++report.missionChangedCount;
                        ++tickMissionChanged;
                    }
                    if (intent.weaponSelected) {
                        ++report.weaponSelectedCount;
                    }
                    if (intent.projectileSpawned) {
                        ++report.projectileSpawnedCount;
                    }
                    if (intent.impactResolved) {
                        ++report.impactResolvedCount;
                    }
                    if (intent.pathQueued) {
                        ++report.pathQueuedCount;
                        ++tickPathQueued;
                        const std::uint64_t cacheKey = pathCacheKeyForRecord(intent);
                        if (intent.pathCacheMiss
                            && std::find(
                                   capabilityPathCacheKeys.begin(),
                                   capabilityPathCacheKeys.end(),
                                   cacheKey
                               ) == capabilityPathCacheKeys.end()) {
                            capabilityPathCacheKeys.push_back(cacheKey);
                        }
                    }
                    if (intent.pathCacheHit) {
                        ++tickPathCacheHits;
                    }
                    if (intent.pathCacheMiss) {
                        ++tickPathCacheMisses;
                    }
                    if (intent.renderIntentExpected) {
                        ++report.renderIntentExpectedCount;
                        ++tickRenderExpected;
                    }

                    if (report.capabilityDetailsIncluded) {
                        appendEvent(
                            report,
                            tick,
                            "capability detail "
                            + intent.abilityType
                            + " capability="
                            + intent.selectedCapabilityHash
                            + " slot="
                            + intent.weaponSlot
                            + " weapon="
                            + (intent.weaponId.empty() ? "(none)" : intent.weaponId)
                            + " projectile="
                            + (intent.projectileId.empty() ? "(none)" : intent.projectileId)
                            + " image="
                            + (intent.projectileImageId.empty()
                                   ? "(none)"
                                   : intent.projectileImageId)
                            + " warhead="
                            + (intent.warheadId.empty() ? "(none)" : intent.warheadId)
                            + " trailer="
                            + (intent.trailerId.empty() ? "(none)" : intent.trailerId)
                            + " impact="
                            + (intent.impactId.empty() ? "(none)" : intent.impactId)
                            + " mission="
                            + intent.missionBefore
                            + "->"
                            + intent.missionAfter
                            + " path="
                            + intent.pathIntentHash
                            + " pathDiag="
                            + intent.pathDiagnosticHash
                            + " render="
                            + intent.renderIntentHash
                            + " renderId="
                            + (intent.renderIntentId.empty()
                                   ? "(none)"
                                   : intent.renderIntentId)
                            + " chain="
                            + intent.chainDiagnosticHash
                            + " source="
                            + intent.chainFieldSource
                        );
                    }
                }
                appendEvent(
                    report,
                    tick,
                    "capability summary records="
                    + std::to_string(peerCapabilityRecords.front().size())
                    + " hash="
                    + report.capabilityIntentHash
                    + " accepted="
                    + std::to_string(tickAccepted)
                    + " missionChanged="
                    + std::to_string(tickMissionChanged)
                    + " pathQueued="
                    + std::to_string(tickPathQueued)
                    + " pathCache="
                    + std::to_string(tickPathCacheHits)
                    + "/"
                    + std::to_string(tickPathCacheMisses)
                    + " renderExpected="
                    + std::to_string(tickRenderExpected)
                    + " source=runtime:"
                    + std::to_string(report.capabilityRuntimeFieldCount)
                    + "/fallback:"
                    + std::to_string(report.capabilitySandboxFallbackFieldCount)
                    + " detail="
                    + (report.capabilityDetailsIncluded ? "on" : "off")
                );
            }
            if (!report.capabilityIntentValidated) {
                report.hashFrames.push_back(capabilityFrame);
                report.ticksSimulated = tick;
                report.finalStateHash = report.capabilityIntentHash;
                report.desyncDetected = true;
                report.firstDesyncTick = tick;
                report.desyncSummary =
                    describeCapabilityIntentMismatch(
                        capabilityFrame,
                        peerCapabilityRecords
                    );
                report.firstDesyncClass =
                    desyncClassFromSummary(report.desyncSummary);
                appendEvent(report, tick, report.desyncSummary);
                break;
            }

            ReplayHashFrame commandFrame =
                makeHashFrame(tick, static_cast<int>(dueCommands.size()), peers);
            report.commandIntentHash =
                commandFrame.peerHashes.empty() ? "" : commandFrame.peerHashes.front();
            report.commandIntentValidated = hashesMatch(commandFrame);
            appendEvent(
                report,
                tick,
                "post-command intent hash=" + report.commandIntentHash
            );
            if (!report.commandIntentValidated) {
                report.hashFrames.push_back(commandFrame);
                report.ticksSimulated = tick;
                report.finalStateHash = report.commandIntentHash;
                report.desyncDetected = true;
                report.firstDesyncTick = tick;
                report.desyncSummary = describeDesync(commandFrame, peers);
                report.firstDesyncClass =
                    desyncClassFromSummary(report.desyncSummary);
                appendEvent(report, tick, report.desyncSummary);
                break;
            }
        }

        ReplayHashFrame attackPathFrame =
            makeAttackPathIntentFrame(tick, static_cast<int>(dueCommands.size()), peers);
        report.attackPathIntentHash =
            attackPathFrame.peerHashes.empty() ? "" : attackPathFrame.peerHashes.front();
        report.attackPathIntentValidated = hashesMatch(attackPathFrame);
        const std::vector<std::uint64_t> activePathIntents =
            activeAttackPathIntentKeys(peers.front());
        for (const std::uint64_t key : activePathIntents) {
            ++report.pathIntentSamples;
            ++report.pathFallbackTargetsTried;
            if (std::find(
                    pathIntentCacheKeys.begin(),
                    pathIntentCacheKeys.end(),
                    key
                ) == pathIntentCacheKeys.end()) {
                pathIntentCacheKeys.push_back(key);
                ++report.pathCacheMisses;
            } else {
                ++report.pathCacheHits;
            }
        }
        if (!dueCommands.empty()) {
            appendEvent(
                report,
                tick,
                "attack/path intent hash=" + report.attackPathIntentHash
                + " " + describeAttackPathIntent(peers.front())
                + " fallbackTried=" + std::to_string(report.pathFallbackTargetsTried)
                + " cacheHit=" + std::to_string(report.pathCacheHits)
                + " cacheMiss=" + std::to_string(report.pathCacheMisses)
                + " budgetExhausted="
                + (report.pathBudgetExhausted ? "yes" : "no")
            );
        }
        if (!report.attackPathIntentValidated) {
            report.hashFrames.push_back(attackPathFrame);
            report.ticksSimulated = tick;
            report.finalStateHash = report.attackPathIntentHash;
            report.desyncDetected = true;
            report.firstDesyncTick = tick;
            report.desyncSummary =
                "attack/path intent " + describeDesync(attackPathFrame, peers);
            report.firstDesyncClass =
                desyncClassFromSummary(report.desyncSummary);
            appendEvent(report, tick, report.desyncSummary);
            break;
        }

        for (PeerState& peer : peers) {
            simulatePeerTick(
                peer,
                report,
                peer.peerId == 0,
                projectileLifecycleHash,
                projectileLifecycleCount,
                projectileLifecycleRuntimeCount,
                projectileLifecycleFallbackCount,
                runtimeSource
            );

            if (report.expectedDesync
                && !desyncInjected
                && tick == config.desyncAtTick
                && peer.peerId == 1
            ) {
                if (ReplayUnitState* unit = findUnit(peer, 1)) {
                    unit->hp = std::max(0, unit->hp - 1);
                    desyncInjected = true;
                    appendEvent(report, tick, "injected desync into peer 1 unit #1 hp");
                }
            }
        }

        ReplayHashFrame frame =
            makeHashFrame(tick, static_cast<int>(dueCommands.size()), peers);
        const bool shouldKeepFrame =
            tick == 0
            || !dueCommands.empty()
            || tick % 25 == 0
            || !hashesMatch(frame)
            || tick == maxTicks;

        if (shouldKeepFrame) {
            report.hashFrames.push_back(frame);
        }

        report.ticksSimulated = tick;
        report.finalStateHash = frame.peerHashes.empty() ? "" : frame.peerHashes.front();

        if (!hashesMatch(frame)) {
            report.desyncDetected = true;
            report.firstDesyncTick = tick;
            report.desyncSummary = describeDesync(frame, peers);
            report.firstDesyncClass =
                desyncClassFromSummary(report.desyncSummary);
            appendEvent(report, tick, report.desyncSummary);
            break;
        }

        report.winner = findWinner(peers.front());
        if (!report.winner.empty()) {
            break;
        }
    }

    report.replayPathRequestCount = report.pathQueuedCount;
    const std::string projectileLifecycleHashText =
        formatHash(projectileLifecycleHash);
    report.projectileLifecycleCount = projectileLifecycleCount;
    report.projectileLifecycleRuntimeCount = projectileLifecycleRuntimeCount;
    report.projectileLifecycleFallbackCount = projectileLifecycleFallbackCount;
    report.projectileLifecycleHash = projectileLifecycleHashText;
    report.runtimeEventSource.projectileLifecycle.count =
        report.projectileLifecycleCount;
    report.runtimeEventSource.projectileLifecycle.runtimeSourceCount =
        report.projectileLifecycleRuntimeCount;
    report.runtimeEventSource.projectileLifecycle.sandboxFallbackCount =
        report.projectileLifecycleFallbackCount;
    report.runtimeEventSource.projectileLifecycle.hash =
        report.projectileLifecycleHash;
    report.runtimeEventSource.sandboxFallbackCount =
        report.capabilitySandboxFallbackFieldCount
        + report.projectileLifecycleFallbackCount;
    report.totalHashFrameCount =
        static_cast<int>(report.hashFrames.size())
        + report.capabilityHashFrameCount;
    report.runtimeSourceEvidence = replayRuntimeSourceEvidence(report);
    if (report.desyncDetected && !report.runtimeSourceEvidence.empty()) {
        report.desyncSummary += "; " + report.runtimeSourceEvidence;
    }
    appendEvent(
        report,
        report.ticksSimulated,
        "projectile lifecycle summary count="
        + std::to_string(projectileLifecycleCount)
        + " runtime="
        + std::to_string(projectileLifecycleRuntimeCount)
        + " fallback="
        + std::to_string(projectileLifecycleFallbackCount)
        + " hash="
        + projectileLifecycleHashText
        + " detail=off"
    );
    appendEvent(report, report.ticksSimulated, report.runtimeSourceEvidence);
    appendEvent(
        report,
        report.ticksSimulated,
        "performance summary capabilityRecords="
        + std::to_string(report.capabilityIntentCount)
        + " hashFrames="
        + std::to_string(report.totalHashFrameCount)
        + " replayPathRequests="
        + std::to_string(report.replayPathRequestCount)
    );

    report.deployIntentStable =
        report.deployIntentCount > 0 && report.capabilityIntentValidated;
    report.attackIntentStable =
        report.attackIntentCount > 0
        && report.capabilityIntentValidated
        && report.attackPathIntentValidated;
    report.projectileIntentStable =
        report.projectileIntentCount > 0
        && report.capabilityIntentValidated
        && projectileLifecycleCount > 0;
    report.pathDiagnosticsStable =
        report.attackPathIntentValidated
        && !report.pathBudgetExhausted
        && report.pathQueuedCount == report.attackIntentCount;
    const bool chainStepCountsStable =
        report.commandAcceptedCount == report.capabilityIntentCount
        && report.missionChangedCount == report.capabilityIntentCount
        && report.weaponSelectedCount >= report.attackIntentCount
        && report.projectileSpawnedCount == report.attackIntentCount
        && projectileLifecycleCount >= report.projectileSpawnedCount
        && report.impactResolvedCount == report.attackIntentCount
        && report.renderIntentExpectedCount == report.capabilityIntentCount;
    report.commandMissionProjectileChainStable =
        report.commandIntentValidated
        && report.capabilityIntentValidated
        && report.deployIntentStable
        && report.attackIntentStable
        && report.projectileIntentStable
        && report.pathDiagnosticsStable
        && chainStepCountsStable;

    report.replayVerified = !report.desyncDetected;
    const bool replayOutcomeMatched = report.expectedDesync
        ? report.desyncDetected
        : !report.desyncDetected;
    report.success =
        replayOutcomeMatched && report.commandMissionProjectileChainStable;

    if (!report.success) {
        if (!replayOutcomeMatched && report.desyncDetected) {
            report.errorMessage = report.desyncSummary.empty()
                ? "Unexpected replay desync detected."
                : report.desyncSummary;
        } else if (!replayOutcomeMatched && report.expectedDesync) {
            report.errorMessage =
                "Expected replay desync was not detected by state hashes.";
        } else if (!report.commandMissionProjectileChainStable) {
            report.errorMessage =
                "Replay capability intent diagnostics were not stable across peers.";
        }
    }

    if (!config.outputPath.empty()) {
        if (!writeReplayFile(report, config.outputPath, report.errorMessage)) {
            report.success = false;
            report.replayVerified = false;
            return report;
        }

        report.replayExported = true;
        report.replayOutputPath = config.outputPath;
    }

    return report;
}

ReplaySandboxReport runReplaySandboxWithRuntimeEvents(
    const RulesDatabase& rules,
    ReplaySandboxConfig config,
    const RuntimeReplayEventSnapshot* runtimeEvents,
    std::size_t runtimeEventCount
) {
    if (runtimeEvents == nullptr || runtimeEventCount == 0U) {
        config.runtimeEventSnapshots = nullptr;
        config.runtimeEventSnapshotCount = 0U;
    } else {
        config.runtimeEventSnapshots = runtimeEvents;
        config.runtimeEventSnapshotCount = runtimeEventCount;
    }
    return runReplaySandbox(rules, config);
}

ReplaySandboxReport runReplaySandboxWithRuntimeEvents(
    const RulesDatabase& rules,
    ReplaySandboxConfig config,
    const std::vector<RuntimeReplayEventSnapshot>& runtimeEvents
) {
    return runReplaySandboxWithRuntimeEvents(
        rules,
        config,
        runtimeEvents.empty() ? nullptr : runtimeEvents.data(),
        runtimeEvents.size()
    );
}

bool loadRuntimeReplayEventSnapshotsFromFile(
    const std::string& path,
    std::vector<RuntimeReplayEventSnapshot>& runtimeEvents,
    std::string& errorMessage
) {
    runtimeEvents.clear();
    errorMessage.clear();

    std::ifstream input(path);
    if (!input) {
        errorMessage = "Failed to open runtime replay event file: " + path;
        return false;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        RuntimeReplayEventSnapshot event;
        std::string lineError;
        if (!parseRuntimeReplayEventLine(line, event, lineError)) {
            errorMessage = "Runtime replay event file "
                + path
                + " line "
                + std::to_string(lineNumber)
                + ": "
                + lineError;
            runtimeEvents.clear();
            return false;
        }
        if (event.kind.empty()) {
            continue;
        }
        if (event.sequence == 0U && !runtimeEvents.empty()) {
            event.sequence = static_cast<std::uint64_t>(runtimeEvents.size());
        }
        runtimeEvents.push_back(std::move(event));
    }

    return true;
}

ReplaySandboxReport runReplaySandboxWithRuntimeEventFile(
    const RulesDatabase& rules,
    ReplaySandboxConfig config,
    const std::string& runtimeEventPath,
    std::string* errorMessage
) {
    std::vector<RuntimeReplayEventSnapshot> runtimeEvents;
    std::string loadError;
    if (!loadRuntimeReplayEventSnapshotsFromFile(
            runtimeEventPath,
            runtimeEvents,
            loadError
        )) {
        if (errorMessage != nullptr) {
            *errorMessage = loadError;
        }
        ReplaySandboxReport report;
        report.success = false;
        report.errorMessage = loadError;
        return report;
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return runReplaySandboxWithRuntimeEvents(rules, config, runtimeEvents);
}
