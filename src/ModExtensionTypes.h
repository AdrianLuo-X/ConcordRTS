#pragma once

#include <algorithm>
#include <string>
#include <vector>

inline std::string modExtensionKey(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}

struct AttachEffectRule {
    int duration = 0; // Original game frames; -1 means indefinite.
    int initialDelay = 0;
    int delay = 0;
    double speed = 1.0;
    double armor = 1.0;
    double firepower = 1.0;
    double rof = 1.0;
    std::string animation;
    bool cumulative = false;
    bool animResetOnReapply = false;
    bool temporalHidesAnim = false;
    bool cloakable = false;
    bool forceDecloak = false;
    bool discardOnEntry = false;
    bool penetratesIronCurtain = false;
};

struct TechnoModExtensionRule {
    std::string shieldType;
    AttachEffectRule attachEffect;
};

struct ShieldTypeRule {
    std::string id;
    int strength = 0;
    int initialStrength = 0;
    std::string armor = "none";
    bool inheritArmor = false;
    bool applyArmorMult = false;
    bool powered = false;
    bool absorbOverDamage = false;
    double selfHealing = 0.0;
    double selfHealingRate = 0.0; // Game minutes.
    bool selfHealingRestartInCombat = true;
    int selfHealingRestartDelay = 0;
    double respawn = 0.0;
    double respawnRate = 0.0;
    bool respawnRestartInCombat = true;
    int respawnRestartDelay = 0;
};

struct RuntimeAttachedEffect {
    std::string sourceType;
    bool fromWarhead = false;
    int remainingTicks = 0;
    int elapsedTicks = 0;
};

struct RuntimeModExtensionState {
    bool initialized = false;
    std::string technoType;
    std::string shieldType;
    int shieldCapacity = 0;
    int shieldHealth = 0;
    int shieldTimer = 0;
    bool shieldOnline = true;
    int innateDelayTicks = -1;
    std::vector<RuntimeAttachedEffect> effects;
};
