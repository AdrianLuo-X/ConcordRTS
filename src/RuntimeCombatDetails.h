#pragma once

#include "IniFile.h"
#include "GameWorld.h"
#include "RulesDatabase.h"
#include "RuntimeDiskDrain.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

// Pending rounds belong to the firing unit, independently of its UI command.
// An expended magazine round buys one burst; later rounds never spend ammo again.
struct RuntimePendingWeaponBurst {
    int attackerObjectId = -1;
    int sourceAnchorObjectId = -1;
    int targetObjectId = -1;
    int targetX = -1;
    int targetY = -1;
    int targetLevel = 0;
    int targetKind = 0;
    int weaponSlot = 0;
    int occupantMuzzleIndex = -1;
    int nextShotIndex = 1;
    int roundsRemaining = 0;
    int nextFireTick = 0;
    bool forceFire = false;
    bool independentFire = false;
    double outgoingDamageMultiplier = 1.0;
    std::string owner;
    std::string sourceType;
    std::string weaponId;
};

struct RuntimeCombatFireCursor {
    int lastTargetObjectId = -1;
    int radialSegment = 0;
    int sourceAnchorObjectId = -1;
};

// Script panic persists until Unpanic or a replacing order. Damage panic uses
// simulation time; pending starts are consumed outside the damage iterator.
struct RuntimePanicState {
    bool permanent = false;
    bool pendingStart = true;
    int ticksRemaining = 0;
    int lastTick = 0;
    int nextChooseTick = 0;
    std::uint64_t choiceSerial = 0;
    int threatX = -1;
    int threatY = -1;
    std::string owner;
    std::string sourceType;
};

inline int runtimeCombatBurstDelayTicks(
    int fallbackFrames, const std::vector<int>& delays, int nextShotIndex,
    int fixedTickHz, int authoredTickHz = 15
) {
    int frames = fallbackFrames;
    if (!delays.empty()) {
        const auto index = static_cast<std::size_t>(std::max(0, nextShotIndex - 1));
        frames = delays[std::min(index, delays.size() - 1)];
    }
    // All ordinary and gattling weapons use the 15-frame nominal clock.
    // Never permit zero-delay recursion or
    // let malicious rule values overflow the scheduler's signed tick domain.
    const auto scaled = static_cast<std::int64_t>(std::clamp(frames, 1, 36000))
        * std::clamp(fixedTickHz, 1, 1000);
    const int clock = std::clamp(authoredTickHz, 1, 1000);
    return static_cast<int>(std::clamp<std::int64_t>((scaled + clock - 1) / clock, 1, 2400000));
}

inline std::uint64_t runtimeCombatRandomMix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

struct RuntimeCombatScatterOffset {
    int xLeptons = 0;
    int yLeptons = 0;
};

inline RuntimeCombatScatterOffset runtimeCombatScatterOffset(
    std::uint64_t seed, double maximumCells
) {
    if (!std::isfinite(maximumCells) || maximumCells <= 0.0) return {};
    const int radius = static_cast<int>(std::lround(
        std::clamp(maximumCells, 0.0, 64.0) * 256.0));
    if (radius <= 0) return {};
    const auto diameter = static_cast<std::uint64_t>(radius * 2 + 1);
    // Rejection on an integer circle gives deterministic, area-uniform scatter
    // without platform-dependent trigonometry or a shared random stream.
    for (int attempt = 0; attempt < 32; ++attempt) {
        seed = runtimeCombatRandomMix(seed);
        const int x = static_cast<int>(seed % diameter) - radius;
        seed = runtimeCombatRandomMix(seed);
        const int y = static_cast<int>(seed % diameter) - radius;
        if (static_cast<std::int64_t>(x) * x + static_cast<std::int64_t>(y) * y
            <= static_cast<std::int64_t>(radius) * radius) return {x, y};
    }
    return {};
}

inline int runtimeCombatRadialFacing(int bodyFacing, int segments, int segment) {
    const int count = std::clamp(segments, 1, 128);
    const int index = ((segment % count) + count) % count;
    // Retail 0x6FEAD4 uses pi / count, then advances the saved segment.
    // Its mathematical angle runs opposite to the engine clockwise facing.
    const int offset = static_cast<int>(std::lround(64.0 - 128.0 * index / count));
    return (((bodyFacing % 256) + offset) % 256 + 256) % 256;
}

inline double runtimeCombatProjectileSpeedMultiplier(
    bool turboBoost, bool airborneTarget, double authoredMultiplier
) {
    return turboBoost && airborneTarget && std::isfinite(authoredMultiplier)
        ? std::clamp(authoredMultiplier, 0.01, 100.0) : 1.0;
}

class RuntimeCombatDetailsState {
public:
    std::map<int, RuntimePendingWeaponBurst> bursts;
    std::map<int, RuntimeCombatFireCursor> fireCursors;
    std::map<int, RuntimePanicState> panics;
    RuntimeDiskDrainState diskDrains;
    // Last ordinary mobile unit evaluated; its ID may outlive that unit.
    int guardScanAfterObjectId = -1;
    // Pre-feature saves contain one aggregate actor for an ordinary burst.
    // Keep that actor's paid rounds until it expires; new actors are unmarked.
    std::map<int, int> legacyProjectileRounds;

    friend void swap(RuntimeCombatDetailsState& left, RuntimeCombatDetailsState& right) noexcept {
        left.bursts.swap(right.bursts);
        left.fireCursors.swap(right.fireCursors);
        left.panics.swap(right.panics);
        left.diskDrains.sources.swap(right.diskDrains.sources);
        std::swap(left.guardScanAfterObjectId, right.guardScanAfterObjectId);
        left.legacyProjectileRounds.swap(right.legacyProjectileRounds);
    }

    bool hasPendingBurst(int objectId) const { return bursts.contains(objectId); }

    void save(std::ostream& output) const {
        output << "\n[CombatDetails]\nSchema=2\nBurstCount=" << bursts.size()
               << "\nCursorCount=" << fireCursors.size()
               << "\nLegacyCount=" << legacyProjectileRounds.size()
               << "\nPanicCount=" << panics.size()
               << "\nGuardScanAfterObjectId=" << guardScanAfterObjectId << "\n";
        int index = 0;
        for (const auto& [id, burst] : bursts) {
            (void)id;
            output << "Burst" << index++ << "=" << encodeBurst(burst) << "\n";
        }
        index = 0;
        for (const auto& [id, cursor] : fireCursors) {
            output << "Cursor" << index++ << "=" << id << " "
                   << cursor.lastTargetObjectId << " " << cursor.radialSegment
                   << " " << cursor.sourceAnchorObjectId << "\n";
        }
        index = 0;
        for (const auto& [actorId, rounds] : legacyProjectileRounds)
            output << "Legacy" << index++ << "=" << actorId << " " << rounds << "\n";
        index = 0;
        for (const auto& [id, panic] : panics) {
            output << "Panic" << index++ << "=" << id << " " << panic.permanent
                   << " " << panic.pendingStart << " " << panic.ticksRemaining
                   << " " << panic.lastTick << " " << panic.nextChooseTick
                   << " " << panic.choiceSerial << " " << panic.threatX << " " << panic.threatY
                   << " " << iniHexEncode(panic.owner) << " " << iniHexEncode(panic.sourceType) << "\n";
        }
        diskDrains.save(output);
    }

    bool load(const IniFile& ini, std::string& error) {
        RuntimeCombatDetailsState staged;
        if (!staged.diskDrains.load(ini, error)) return false;
        if (ini.getSection("CombatDetails") == nullptr) {
            *this = std::move(staged);
            return true; // Saves created before this feature contain no pending rounds.
        }
        int schema = 0, burstCount = 0, cursorCount = 0, legacyCount = 0, panicCount = 0;
        if (!parseInteger(ini.getValue("CombatDetails", "Schema"), schema)
            || (schema != 1 && schema != 2)
            || !parseInteger(ini.getValue("CombatDetails", "BurstCount"), burstCount)
            || !parseInteger(ini.getValue("CombatDetails", "CursorCount"), cursorCount)
            || !parseInteger(ini.getValue("CombatDetails", "LegacyCount", "0"), legacyCount)
            || !parseInteger(ini.getValue("CombatDetails", "PanicCount", "0"), panicCount)
            || !parseInteger(ini.getValue("CombatDetails", "GuardScanAfterObjectId", "-1"), staged.guardScanAfterObjectId)
            || staged.guardScanAfterObjectId < -1
            || burstCount < 0 || burstCount > 100000
            || cursorCount < 0 || cursorCount > 100000
            || legacyCount < 0 || legacyCount > 100000 || panicCount < 0 || panicCount > 100000) {
            error = "invalid CombatDetails schema/count";
            return false;
        }
        for (int index = 0; index < burstCount; ++index) {
            RuntimePendingWeaponBurst burst;
            std::string owner, sourceType, weaponId;
            std::istringstream input(ini.getValue("CombatDetails", "Burst" + std::to_string(index)));
            input.imbue(std::locale::classic());
            int force = 0, independent = 0;
            if (!(input >> burst.attackerObjectId >> burst.sourceAnchorObjectId
                >> burst.targetObjectId >> burst.targetX >> burst.targetY
                >> burst.targetLevel >> burst.targetKind >> burst.weaponSlot
                >> burst.nextShotIndex >> burst.roundsRemaining >> burst.nextFireTick
                >> force >> independent >> burst.outgoingDamageMultiplier
                >> owner >> sourceType >> weaponId)
                || (schema >= 2 && !(input >> burst.occupantMuzzleIndex))
                || (input >> std::ws, !input.eof())
                || !iniHexDecode(owner, burst.owner, 1024)
                || !iniHexDecode(sourceType, burst.sourceType, 1024)
                || !iniHexDecode(weaponId, burst.weaponId, 1024)
                || (force != 0 && force != 1) || (independent != 0 && independent != 1)) {
                error = "invalid CombatDetails burst row";
                return false;
            }
            burst.forceFire = force != 0;
            burst.independentFire = independent != 0;
            if (!validBurst(burst)
                || !staged.bursts.emplace(burst.attackerObjectId, burst).second) {
                error = "invalid or duplicate CombatDetails burst";
                return false;
            }
        }
        for (int index = 0; index < cursorCount; ++index) {
            int objectId = -1;
            RuntimeCombatFireCursor cursor;
            std::istringstream input(ini.getValue("CombatDetails", "Cursor" + std::to_string(index)));
            if (!(input >> objectId >> cursor.lastTargetObjectId >> cursor.radialSegment >> cursor.sourceAnchorObjectId)
                || (input >> std::ws, !input.eof()) || objectId < 0
                || cursor.lastTargetObjectId < -1 || cursor.radialSegment < 0
                || cursor.sourceAnchorObjectId < -1
                || cursor.radialSegment > 127
                || !staged.fireCursors.emplace(objectId, cursor).second) {
                error = "invalid or duplicate CombatDetails cursor";
                return false;
            }
        }
        for (int index = 0; index < legacyCount; ++index) {
            int actorId = -1, rounds = 0;
            std::istringstream input(ini.getValue("CombatDetails", "Legacy" + std::to_string(index)));
            if (!(input >> actorId >> rounds) || (input >> std::ws, !input.eof())
                || actorId <= 0 || rounds <= 1
                || !staged.legacyProjectileRounds.emplace(actorId, rounds).second) {
                error = "invalid or duplicate CombatDetails legacy projectile";
                return false;
            }
        }
        for (int index = 0; index < panicCount; ++index) {
            int id = -1, permanent = 0, pending = 0;
            RuntimePanicState panic;
            std::string owner, type;
            std::istringstream input(ini.getValue("CombatDetails", "Panic" + std::to_string(index)));
            if (!(input >> id >> permanent >> pending >> panic.ticksRemaining >> panic.lastTick
                    >> panic.nextChooseTick >> panic.choiceSerial >> panic.threatX >> panic.threatY >> owner >> type)
                || (input >> std::ws, !input.eof()) || id < 0
                || (permanent != 0 && permanent != 1) || (pending != 0 && pending != 1)
                || panic.ticksRemaining < 0 || panic.ticksRemaining > 2400000
                || (!permanent && panic.ticksRemaining == 0)
                || panic.lastTick < 0 || panic.nextChooseTick < 0
                || panic.threatX < -1 || panic.threatX > 32767
                || panic.threatY < -1 || panic.threatY > 32767
                || !iniHexDecode(owner, panic.owner, 1024) || panic.owner.empty()
                || !iniHexDecode(type, panic.sourceType, 1024) || panic.sourceType.empty()) {
                error = "invalid CombatDetails panic row";
                return false;
            }
            panic.permanent = permanent != 0;
            panic.pendingStart = pending != 0;
            if (!staged.panics.emplace(id, std::move(panic)).second) {
                error = "duplicate CombatDetails panic";
                return false;
            }
        }
        *this = std::move(staged);
        return true;
    }

    template<class Projectile>
    static bool legacyAggregateCandidate(const Projectile& projectile) {
        return projectile.actorId > 0 && projectile.parentActorId < 0
            && projectile.generation == 0 && projectile.sourceAnchorObjectId < 0
            && !projectile.spawnedMissileDamage && !projectile.particleDamagePulse
            && projectile.deathWeaponCascadeId == 0 && projectile.deathWeaponRequestId == 0
            && projectile.airstrikeStrikeId == 0 && projectile.airstrikePlaneToken == 0;
    }

    template<class Projectile>
    bool reconcileLegacyProjectiles(const IniFile& ini, const RulesDatabase& rules,
        const std::vector<Projectile>& projectiles, std::string& error) {
        std::map<int, int> expected;
        for (const auto& projectile : projectiles) {
            if (!legacyAggregateCandidate(projectile)) continue;
            const auto* weapon = rules.findWeaponType(projectile.weaponId);
            if (weapon != nullptr && weapon->burst > 1)
                expected.emplace(projectile.actorId, weapon->burst);
        }
        if (ini.getSection("CombatDetails") == nullptr) {
            legacyProjectileRounds.swap(expected);
            return true;
        }
        for (const auto& [actorId, rounds] : legacyProjectileRounds) {
            const auto found = expected.find(actorId);
            if (found == expected.end() || found->second != rounds) {
                error = "CombatDetails legacy projectile does not match a saved aggregate actor";
                return false;
            }
        }
        return true;
    }

    std::uint64_t deterministicHash() const {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        save(output);
        std::uint64_t hash = 14695981039346656037ULL;
        for (unsigned char ch : output.str()) hash = (hash ^ ch) * 1099511628211ULL;
        return hash;
    }

    bool validateWorld(const RulesDatabase& rules,
        const std::vector<WorldObjectInstance>& objects, int currentTick,
        std::string& error) const {
        if (!diskDrains.validateWorld(rules, objects, currentTick, error)) return false;
        std::map<int, const WorldObjectInstance*> byId;
        for (const auto& object : objects) byId.emplace(object.instanceId, &object);
        for (const auto& [id, panic] : panics) {
            const auto found = byId.find(id);
            const auto* object = found == byId.end() ? nullptr : found->second;
            const auto* type = object == nullptr ? nullptr : rules.findUnitType(
                object->kind == WorldObjectKind::Infantry ? "InfantryType" : "UnitType", object->type);
            if (object == nullptr || !object->alive || object->health <= 0 || object->airborne
                || (object->kind != WorldObjectKind::Infantry && object->kind != WorldObjectKind::Unit)
                || type == nullptr || type->speed <= 0
                || object->owner != panic.owner || object->type != panic.sourceType
                || panic.lastTick > currentTick
                || static_cast<std::int64_t>(panic.nextChooseTick) > static_cast<std::int64_t>(currentTick) + 2400000) {
                error = "CombatDetails panic does not match a live ground mover or clock";
                return false;
            }
        }
        for (const auto& [id, burst] : bursts) {
            const auto found = byId.find(id);
            const WeaponType* weapon = rules.findWeaponType(burst.weaponId);
            if (found == byId.end() || weapon == nullptr || !validBurst(burst)) {
                error = "CombatDetails burst source or weapon missing";
                return false;
            }
            const auto& object = *found->second;
            if (object.owner != burst.owner || object.type != burst.sourceType
                || static_cast<std::int64_t>(burst.nextFireTick)
                    > static_cast<std::int64_t>(currentTick) + 2400000
                || burst.nextShotIndex + burst.roundsRemaining > std::clamp(weapon->burst, 1, 256)) {
                error = "CombatDetails burst does not match source identity, timing or weapon";
                return false;
            }
            if (burst.sourceAnchorObjectId >= 0 && burst.sourceAnchorObjectId != id) {
                const auto anchor = byId.find(burst.sourceAnchorObjectId);
                const auto* hostType = anchor == byId.end() ? nullptr
                    : rules.findUnitType("UnitType", anchor->second->type);
                if (hostType == nullptr && anchor != byId.end())
                    hostType = rules.findUnitType("AircraftType", anchor->second->type);
                if (anchor == byId.end() || hostType == nullptr || !hostType->openTopped
                    || std::find(anchor->second->transportPassengerIds.begin(),
                        anchor->second->transportPassengerIds.end(), id)
                        == anchor->second->transportPassengerIds.end()) {
                    error = "CombatDetails burst passenger manifest mismatch";
                    return false;
                }
            }
        }
        return true;
    }

    static bool validBurst(const RuntimePendingWeaponBurst& burst) {
        return burst.attackerObjectId >= 0 && burst.sourceAnchorObjectId >= -1
            && burst.targetObjectId >= -3 && burst.targetX >= -32768 && burst.targetX <= 32767
            && burst.targetY >= -32768 && burst.targetY <= 32767
            && burst.targetLevel >= -1024 && burst.targetLevel <= 1024
            && burst.targetKind >= 0 && burst.targetKind <= 2
            && burst.weaponSlot >= 0 && burst.weaponSlot <= 12
            && burst.occupantMuzzleIndex >= -1 && burst.occupantMuzzleIndex < 10
            && (burst.occupantMuzzleIndex < 0 || burst.sourceAnchorObjectId == burst.attackerObjectId)
            && burst.nextShotIndex > 0 && burst.nextShotIndex <= 255
            && burst.roundsRemaining > 0 && burst.roundsRemaining <= 255
            && burst.nextShotIndex + burst.roundsRemaining <= 256
            && burst.nextFireTick >= 0
            && std::isfinite(burst.outgoingDamageMultiplier)
            && burst.outgoingDamageMultiplier >= 0.0 && burst.outgoingDamageMultiplier <= 1000000.0
            && !burst.owner.empty() && !burst.sourceType.empty() && !burst.weaponId.empty()
            && burst.owner.size() <= 1024 && burst.sourceType.size() <= 1024 && burst.weaponId.size() <= 1024;
    }

private:
    static bool parseInteger(const std::string& text, int& output) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }
    static std::string encodeBurst(const RuntimePendingWeaponBurst& burst) {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<double>::max_digits10)
               << burst.attackerObjectId << " " << burst.sourceAnchorObjectId
               << " " << burst.targetObjectId << " " << burst.targetX << " " << burst.targetY
               << " " << burst.targetLevel << " " << burst.targetKind << " " << burst.weaponSlot
               << " " << burst.nextShotIndex << " " << burst.roundsRemaining << " " << burst.nextFireTick
               << " " << burst.forceFire << " " << burst.independentFire
               << " " << burst.outgoingDamageMultiplier
               << " " << iniHexEncode(burst.owner) << " " << iniHexEncode(burst.sourceType)
               << " " << iniHexEncode(burst.weaponId) << " " << burst.occupantMuzzleIndex;
        return output.str();
    }
};
