#pragma once
#include "GameWorld.h"
#include "RulesDatabase.h"
#include "RuntimeHistoryQuery.h"
#include <cmath>
#include <climits>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>

inline constexpr std::size_t runtimeMaximumAttachedEffects = 256;

// This proof deliberately excludes initialized, shield-online and effect-
// bearing tombstones. They may still initialize, change shield availability,
// discard entry effects or clear effects after death in the original routine.
inline bool runtimeInactiveModExtensionNoopCandidate(const WorldObjectInstance& object) noexcept {
    return !object.alive && !object.modExtensions.initialized
        && !object.modExtensions.shieldOnline && object.modExtensions.effects.empty();
}

// One immutable invocation of the ModExtensions stage owns this cache. No rule
// pointers or profiles survive a tick, rule reload/assignment, or type change.
// Full type strings are keys, including case, non-ASCII and empty identifiers.
class RuntimeInactiveModExtensionNoopStage {
public:
    static constexpr std::size_t MaximumCachedTypes = 256;
    explicit RuntimeInactiveModExtensionNoopStage(const RulesDatabase& rules,
        bool reference = runtimeHistoryQueryReference())
        : rules_(rules), reference_(reference) {}

    bool canSkip(const WorldObjectInstance& object) {
        if (reference_ || !runtimeInactiveModExtensionNoopCandidate(object)) return false;
        const auto found = profiles_.find(object.type);
        if (found != profiles_.end()) return found->second;
        const auto* techno = rules_.findTechnoModExtension(object.type);
        const auto* shield = techno ? rules_.findShieldType(techno->shieldType) : nullptr;
        ++profileLookups_;
        // A registered zero-strength shield still causes initialization, so
        // the proof requires a missing shield, not merely a disabled one.
        const bool noop = shield == nullptr && (!techno || techno->attachEffect.duration == 0);
        if (profiles_.size() < MaximumCachedTypes) profiles_.emplace(object.type, noop);
        return noop;
    }

    std::size_t cachedTypes() const noexcept { return profiles_.size(); }
    std::size_t profileLookups() const noexcept { return profileLookups_; }

private:
    const RulesDatabase& rules_;
    bool reference_ = false;
    std::size_t profileLookups_ = 0;
    std::unordered_map<std::string, bool> profiles_;
};

inline bool runtimeWeaponHasAttachEffect(const RulesDatabase& rules, const WeaponType* weapon) {
    if (!weapon || weapon->damage != 0) return false;
    const auto* warhead = rules.findWarheadType(weapon->warhead);
    return warhead && warhead->attachEffect.duration != 0;
}
inline int runtimeModFrames(int frames, int hz) {
    if (frames < 0) return -1;
    return static_cast<int>(std::min<long long>(INT_MAX / 4,
        (static_cast<long long>(frames) * std::clamp(hz, 1, 1000) + 14) / 15));
}
inline int runtimeModMinutes(double minutes, int hz) {
    return static_cast<int>(std::clamp(std::ceil(minutes * 60.0 * std::clamp(hz, 1, 1000)), 1.0, double(INT_MAX / 4)));
}
inline const AttachEffectRule* runtimeAttachedEffectRule(const RuntimeAttachedEffect& effect, const RulesDatabase& rules) {
    if (effect.fromWarhead) {
        const auto* warhead = rules.findWarheadType(effect.sourceType);
        return warhead ? &warhead->attachEffect : nullptr;
    }
    const auto* techno = rules.findTechnoModExtension(effect.sourceType);
    return techno ? &techno->attachEffect : nullptr;
}

inline bool runtimeAttachEffect(WorldObjectInstance& object, const RulesDatabase& rules,
        const std::string& source, bool warhead, int hz, bool protectedObject = false) {
    RuntimeAttachedEffect effect{source, warhead, 0, 0};
    const auto* rule = runtimeAttachedEffectRule(effect, rules);
    const bool transported = object.mission == "InsideTransport" || object.mission == "InTransport";
    if ((!object.alive && !transported) || !rule || rule->duration == 0
        || (protectedObject && !rule->penetratesIronCurtain)) return false;
    effect.remainingTicks = runtimeModFrames(rule->duration, hz);
    if (!rule->cumulative) for (auto& existing : object.modExtensions.effects) {
        if (existing.fromWarhead == warhead && modExtensionKey(existing.sourceType) == modExtensionKey(source)) {
            existing.remainingTicks = effect.remainingTicks;
            if (rule->animResetOnReapply) existing.elapsedTicks = 0;
            return true;
        }
    }
    if (object.modExtensions.effects.size() >= runtimeMaximumAttachedEffects) return false;
    object.modExtensions.effects.push_back(std::move(effect));
    return true;
}

inline int runtimeShieldInterval(const ShieldTypeRule& rule, bool broken, int hz) {
    return runtimeModMinutes(broken ? rule.respawnRate : rule.selfHealingRate, hz);
}

inline bool runtimeInitializeModExtensions(WorldObjectInstance& object, const RulesDatabase& rules, int hz) {
    auto& state = object.modExtensions;
    const auto key = modExtensionKey(object.type);
    if (state.initialized && state.technoType == key) return false;
    const auto* techno = rules.findTechnoModExtension(object.type);
    const auto* shield = techno ? rules.findShieldType(techno->shieldType) : nullptr;
    if (!state.initialized && state.effects.empty() && !shield
        && (!techno || techno->attachEffect.duration == 0)) return false;
    const bool wasInitialized = state.initialized;
    state.initialized = true;
    state.technoType = key;
    if (shield && shield->strength > 0) {
        const int oldCapacity = state.shieldCapacity;
        state.shieldHealth = oldCapacity > 0 && wasInitialized
            ? static_cast<int>(static_cast<long long>(state.shieldHealth) * shield->strength / oldCapacity)
            : shield->initialStrength;
        state.shieldCapacity = shield->strength;
        state.shieldType = shield->id;
        if (!wasInitialized || oldCapacity == 0)
            state.shieldTimer = runtimeShieldInterval(*shield, state.shieldHealth == 0, hz);
        state.shieldOnline = true;
    } else {
        // Deploying to an unshielded type retains the dormant pool for the
        // reverse conversion; it cannot intercept damage on the new type.
        state.shieldOnline = false;
    }
    std::erase_if(state.effects, [](const auto& effect) { return !effect.fromWarhead; });
    state.innateDelayTicks = techno && techno->attachEffect.duration != 0
        ? runtimeModFrames(techno->attachEffect.initialDelay, hz) : -1;
    if (state.innateDelayTicks == 0) {
        runtimeAttachEffect(object, rules, object.type, false, hz);
        state.innateDelayTicks = -1;
    }
    return true;
}

inline const ShieldTypeRule* runtimeActiveShield(const WorldObjectInstance& object,
        const RulesDatabase& rules, const WarheadType* warhead = nullptr) {
    const auto& state = object.modExtensions;
    if (!state.initialized || !state.shieldOnline || state.shieldHealth <= 0
        || (warhead && (warhead->shieldPenetrate || warhead->temporal))) return nullptr;
    return rules.findShieldType(state.shieldType);
}

inline double runtimeAttachMultiplier(const WorldObjectInstance& object, const RulesDatabase& rules,
        double AttachEffectRule::* member) {
    double value = 1.0;
    int exponent = 0;
    for (const auto& effect : object.modExtensions.effects)
        if (const auto* rule = runtimeAttachedEffectRule(effect, rules)) {
            // Keep the product normalized so opposing stacks cancel before
            // the final bounds, without overflowing on 256 large stacks.
            int shift = 0;
            value = std::frexp(value * (rule->*member), &shift);
            exponent += shift;
        }
    if (exponent > 8) return 100.0;
    if (exponent < -10) return 0.01;
    return std::clamp(std::ldexp(value, exponent), 0.01, 100.0);
}
inline bool runtimeAttachFlag(const WorldObjectInstance& object, const RulesDatabase& rules,
        bool AttachEffectRule::* member) {
    for (const auto& effect : object.modExtensions.effects)
        if (const auto* rule = runtimeAttachedEffectRule(effect, rules); rule && rule->*member) return true;
    return false;
}

inline int runtimeShieldRecovery(double amount, int capacity) {
    const double magnitude = std::abs(amount);
    const double value = magnitude <= 1.0 ? amount * capacity : amount;
    return static_cast<int>(std::clamp(std::round(value), -100000000.0, 100000000.0));
}

inline bool runtimeAdvanceModExtensions(WorldObjectInstance& object, const RulesDatabase& rules,
        int hz, bool hidden, bool temporal, bool powered) {
    bool changed = runtimeInitializeModExtensions(object, rules, hz);
    auto& state = object.modExtensions;
    const auto* techno = rules.findTechnoModExtension(object.type);
    const auto* shield = techno ? rules.findShieldType(techno->shieldType) : nullptr;
    const bool online = shield && shield->strength > 0 && (!shield->powered || powered);
    changed = changed || state.shieldOnline != online;
    state.shieldOnline = online;
    if (hidden) {
        const auto before = state.effects.size();
        std::erase_if(state.effects, [&](const auto& effect) {
            const auto* rule = runtimeAttachedEffectRule(effect, rules);
            if (!rule || !rule->discardOnEntry) return false;
            if (!effect.fromWarhead) state.innateDelayTicks = runtimeModFrames(rule->delay, hz);
            return true;
        });
        return changed || before != state.effects.size();
    }
    if (!object.alive) {
        changed = changed || !state.effects.empty();
        state.effects.clear();
        return changed;
    }
    if (temporal) return changed;
    changed = changed || !state.effects.empty();
    bool innateExpired = false;
    std::erase_if(state.effects, [&](auto& effect) {
        if (effect.elapsedTicks < INT_MAX / 4) ++effect.elapsedTicks;
        if (effect.remainingTicks > 0) --effect.remainingTicks;
        if (effect.remainingTicks != 0) return false;
        if (!effect.fromWarhead) {
            const auto* rule = runtimeAttachedEffectRule(effect, rules);
            state.innateDelayTicks = rule ? runtimeModFrames(rule->delay, hz) : -1;
            innateExpired = true;
        }
        return true;
    });
    if (state.innateDelayTicks >= 0) {
        if (!innateExpired && state.innateDelayTicks > 0) --state.innateDelayTicks;
        if (state.innateDelayTicks == 0) {
            runtimeAttachEffect(object, rules, object.type, false, hz);
            state.innateDelayTicks = -1;
        }
        changed = true;
    }
    if (online && shield) {
        const bool broken = state.shieldHealth <= 0;
        const double amount = broken ? shield->respawn : shield->selfHealing;
        if (amount != 0 && (broken || amount < 0 || state.shieldHealth < state.shieldCapacity)) {
            if (state.shieldTimer > 0) --state.shieldTimer;
            if (state.shieldTimer == 0) {
                state.shieldHealth = std::clamp(state.shieldHealth + runtimeShieldRecovery(amount, state.shieldCapacity), 0, state.shieldCapacity);
                state.shieldTimer = runtimeShieldInterval(*shield, state.shieldHealth == 0, hz);
            }
            changed = true;
        }
    }
    return changed || !state.effects.empty();
}

// Damage has already been calculated separately against shield and body
// armor. A broken shield passes only the unabsorbed fraction of body damage.
inline int runtimeAbsorbShieldDamage(WorldObjectInstance& object, const RulesDatabase& rules,
        const WarheadType* warhead, int shieldDamage, int bodyDamage, int hz) {
    auto& state = object.modExtensions;
    const auto* shield = runtimeActiveShield(object, rules, warhead);
    if (!shield) return bodyDamage;
    if (shieldDamage < 0) {
        if (state.shieldHealth == state.shieldCapacity) return bodyDamage;
        state.shieldHealth = static_cast<int>(std::min<long long>(state.shieldCapacity,
            static_cast<long long>(state.shieldHealth) - shieldDamage));
        return 0;
    }
    if (shieldDamage == 0) return 0;
    const int absorbed = std::min(shieldDamage, state.shieldHealth);
    state.shieldHealth -= absorbed;
    if (state.shieldHealth == 0)
        state.shieldTimer = runtimeShieldInterval(*shield, true, hz);
    else if (shield->selfHealingRestartInCombat)
        state.shieldTimer = shield->selfHealingRestartDelay > 0
            ? runtimeModFrames(shield->selfHealingRestartDelay, hz) : runtimeShieldInterval(*shield, false, hz);
    if (shield->absorbOverDamage || absorbed == shieldDamage) return 0;
    return static_cast<int>(std::clamp(std::llround(static_cast<double>(bodyDamage)
        * (shieldDamage - absorbed) / shieldDamage), 0LL, static_cast<long long>(INT_MAX)));
}

inline std::string runtimeSerializeModExtensions(const RuntimeModExtensionState& state) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << 1 << ' ' << state.initialized << ' ' << std::quoted(state.technoType) << ' '
        << std::quoted(state.shieldType) << ' ' << state.shieldCapacity << ' ' << state.shieldHealth
        << ' ' << state.shieldTimer << ' ' << state.shieldOnline << ' ' << state.innateDelayTicks
        << ' ' << state.effects.size();
    for (const auto& effect : state.effects) out << ' ' << std::quoted(effect.sourceType)
        << ' ' << effect.fromWarhead << ' ' << effect.remainingTicks << ' ' << effect.elapsedTicks;
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : out.str()) { result += hex[c >> 4]; result += hex[c & 15]; }
    return result;
}

inline bool runtimeParseModExtensions(const std::string& text, const RulesDatabase& rules,
        RuntimeModExtensionState& output) {
    // Accommodate every allowed effect and quoted 256-byte identifier,
    // including escaping, while retaining a hard bound for untrusted saves.
    if (text.size() > 512U * 1024U || (text.size() & 1) != 0) return false;
    const auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string decoded;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = nibble(text[i]), lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        decoded += static_cast<char>(hi * 16 + lo);
    }
    std::istringstream in(decoded); in.imbue(std::locale::classic());
    RuntimeModExtensionState state;
    int schema = 0;
    std::size_t count = 0;
    if (!(in >> schema >> state.initialized >> std::quoted(state.technoType) >> std::quoted(state.shieldType)
        >> state.shieldCapacity >> state.shieldHealth >> state.shieldTimer >> state.shieldOnline >> state.innateDelayTicks >> count)
        || schema != 1 || count > runtimeMaximumAttachedEffects || state.technoType.size() > 256 || state.shieldType.size() > 256
        || state.shieldCapacity < 0 || state.shieldCapacity > 100000000 || state.shieldHealth < 0
        || state.shieldHealth > state.shieldCapacity || state.shieldTimer < 0 || state.shieldTimer > INT_MAX / 4
        || state.innateDelayTicks < -1 || state.innateDelayTicks > INT_MAX / 4) return false;
    if (!state.shieldType.empty()) {
        const auto* shield = rules.findShieldType(state.shieldType);
        if (!shield || shield->strength != state.shieldCapacity) return false;
    } else if (state.shieldCapacity != 0 || state.shieldHealth != 0) return false;
    for (std::size_t i = 0; i < count; ++i) {
        RuntimeAttachedEffect effect;
        if (!(in >> std::quoted(effect.sourceType) >> effect.fromWarhead >> effect.remainingTicks >> effect.elapsedTicks)
            || effect.sourceType.size() > 256 || effect.remainingTicks < -1 || effect.remainingTicks == 0
            || effect.remainingTicks > INT_MAX / 4 || effect.elapsedTicks < 0 || effect.elapsedTicks > INT_MAX / 4) return false;
        const auto* rule = runtimeAttachedEffectRule(effect, rules);
        if (!rule || rule->duration == 0 || (!effect.fromWarhead && modExtensionKey(effect.sourceType) != state.technoType)) return false;
        if (!rule->cumulative) for (const auto& existing : state.effects)
            if (existing.fromWarhead == effect.fromWarhead && modExtensionKey(existing.sourceType) == modExtensionKey(effect.sourceType)) return false;
        state.effects.push_back(std::move(effect));
    }
    in >> std::ws;
    if (!in.eof()) return false;
    output = std::move(state);
    return true;
}
