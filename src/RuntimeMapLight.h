#pragma once

#include "GameWorld.h"
#include "RulesDatabase.h"
#include "RuntimeLightingSystem.h"
#include <algorithm>
#include <cmath>
#include <vector>

struct RuntimeMapLightSource {
    int objectId = -1;
    double x = 0, y = 0;
    int visibility = 5000;
    int intensity = 0, red = 1000, green = 1000, blue = 1000;
};

inline bool runtimeInvisibleMapBuilding(const RulesDatabase* rules, const WorldObjectInstance& object) {
    const auto* building = rules && object.kind == WorldObjectKind::Structure
        ? rules->findBuildingType(object.type) : nullptr;
    return building && building->invisibleInGame;
}

inline std::vector<RuntimeMapLightSource> runtimeMapLightSources(const GameWorld& world,
    const RulesDatabase* rules) {
    std::vector<RuntimeMapLightSource> sources;
    if (!rules) return sources;
    for (const auto& object : world.getObjects()) {
        if (!object.alive || object.health == 0 || object.kind != WorldObjectKind::Structure
            || object.buildAnimTicksRemaining > 0) continue;
        const auto* type = rules->findBuildingType(object.type);
        if (!type || type->lightIntensity == 0 || type->lightVisibility <= 0
            || (type->powered && !object.poweredOperational)) continue;
        sources.push_back({object.instanceId, static_cast<double>(object.x), static_cast<double>(object.y),
            type->lightVisibility, type->lightIntensity, type->lightRedTint, type->lightGreenTint, type->lightBlueTint});
    }
    return sources;
}

inline RuntimeLightingTint runtimeMapLightTintAt(const std::vector<RuntimeMapLightSource>& sources,
    const RuntimeLightingTint& globalTint, const MapLightingProfile* profile, double x, double y) {
    // Retail CellClass::GetLighting (0x484180): 2D distance in leptons,
    // integer linear falloff; intensity and the three tints accumulate separately.
    long long intensity = 0, red = 0, green = 0, blue = 0;
    for (const auto& light : sources) {
        const double distance = std::hypot(x - light.x, y - light.y) * 256.0;
        if (distance >= light.visibility) continue;
        const int falloff = static_cast<int>((static_cast<long long>(light.visibility)
            - static_cast<int>(distance)) * 1000 / light.visibility);
        intensity += static_cast<long long>(light.intensity) * falloff / 1000;
        red += static_cast<long long>(light.red) * falloff / 1000;
        green += static_cast<long long>(light.green) * falloff / 1000;
        blue += static_cast<long long>(light.blue) * falloff / 1000;
    }
    if (intensity == 0 && red == 0 && green == 0 && blue == 0) return globalTint;
    const double ambient = std::clamp((profile ? profile->ambient + profile->ground : 1.0)
        + intensity / 1000.0, 0.0, 2.0);
    const double r = std::clamp((profile ? profile->red : globalTint.red) + red / 1000.0, 0.0, 2.0);
    const double g = std::clamp((profile ? profile->green : globalTint.green) + green / 1000.0, 0.0, 2.0);
    const double b = std::clamp((profile ? profile->blue : globalTint.blue) + blue / 1000.0, 0.0, 2.0);
    // Retail normalizes the palette tint and clamps the resulting illumination
    // to 2000. Preserve negative lights rather than reducing them to RGB glows.
    const double strongest = std::max({r, g, b});
    if (strongest < 0.001) return {0, 0, 0};
    const double brightness = std::min(2.0, ambient * strongest) / strongest;
    return {r * brightness, g * brightness, b * brightness};
}
