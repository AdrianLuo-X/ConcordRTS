#pragma once

#include "PathRequestQueue.h"
#include "RulesDatabase.h"

#include <cctype>
#include <cstdlib>
#include <array>
#include <string_view>

inline bool runtimeMovementClassificationReference() {
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_FRAME_SCALING_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

// Match toLowerAsciiLocal(value).find(lowercaseNeedle) without constructing
// the lowered string. Needles are the same lowercase literals as the rules
// classifier. Keep the C-locale functions and unsigned-char input semantics,
// including non-ASCII bytes; do not replace them with an ASCII-only bit trick.
inline bool runtimeMovementContainsLowercase(std::string_view value,
    std::string_view lowercaseNeedle) {
    if (lowercaseNeedle.empty()) return true;
    if (value.size() < lowercaseNeedle.size()) return false;
    const std::size_t last = value.size() - lowercaseNeedle.size();
    for (std::size_t first = 0; first <= last; ++first) {
        std::size_t index = 0;
        while (index < lowercaseNeedle.size()
            && static_cast<char>(std::tolower(static_cast<unsigned char>(value[first + index])))
                == lowercaseNeedle[index]) ++index;
        if (index == lowercaseNeedle.size()) return true;
    }
    return false;
}

inline bool runtimeMovementTrimEqualsLowercase(std::string_view value,
    std::string_view lowercaseExpected) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    if (last - first != lowercaseExpected.size()) return false;
    for (std::size_t index = 0; index < lowercaseExpected.size(); ++index) {
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(value[first + index])))
            != lowercaseExpected[index]) return false;
    }
    return true;
}

inline bool runtimeUnitJumpjetMovementProfile(const UnitType& unit) {
    return unit.jumpJet || runtimeMovementTrimEqualsLowercase(unit.locomotor,
        "{92612c46-f71f-11d1-ac9f-006008055bb5}");
}

class RuntimeMovementLowercaseView {
public:
    explicit RuntimeMovementLowercaseView(std::string_view value) : original_(value) {
        if (original_.size() <= buffer_.size()) {
            for (std::size_t index = 0; index < original_.size(); ++index)
                buffer_[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(original_[index])));
        }
    }
    bool contains(std::string_view lowercaseNeedle) const {
        if (original_.size() <= buffer_.size())
            return std::string_view(buffer_.data(), original_.size()).find(lowercaseNeedle) != std::string_view::npos;
        // Modded arbitrarily long values retain exact semantics without heap
        // allocation, truncation, or an unbounded stack buffer.
        return runtimeMovementContainsLowercase(original_, lowercaseNeedle);
    }
private:
    std::string_view original_;
    std::array<char, 128> buffer_;
};

inline bool runtimeUnitAirMovementProfile(const UnitType& unit,
    const RuntimeMovementLowercaseView& speedType,
    const RuntimeMovementLowercaseView& movementZone) {
    return runtimeUnitJumpjetMovementProfile(unit)
        || unit.consideredAircraft
        || movementZone.contains("fly")
        || speedType.contains("winged")
        || speedType.contains("fly")
        || runtimeMovementContainsLowercase(unit.locomotor, "fly");
}

inline bool runtimeUnitAirMovementProfile(const UnitType& unit) {
    const RuntimeMovementLowercaseView speedType(unit.speedType), movementZone(unit.movementZone);
    return runtimeUnitAirMovementProfile(unit, speedType, movementZone);
}

inline PathMovementMode runtimeUnitMovementModeProfile(const UnitType& unit) {
    const RuntimeMovementLowercaseView speedType(unit.speedType), movementZone(unit.movementZone);
    if (runtimeUnitAirMovementProfile(unit, speedType, movementZone)) return PathMovementMode::Air;
    // Preserve hovercraft's priority over explicit Naval/Water properties.
    if (speedType.contains("amphib") || speedType.contains("hover")
        || movementZone.contains("amphib") || movementZone.contains("hover"))
        return PathMovementMode::Amphibious;
    if (unit.naval || unit.waterBound
        || speedType.contains("float") || speedType.contains("water") || movementZone.contains("water"))
        return PathMovementMode::Naval;
    return PathMovementMode::Ground;
}

inline double runtimeUnitLandPercentProfile(const LandType& landType,
    const UnitType& unit, bool infantry) {
    const RuntimeMovementLowercaseView speedType(unit.speedType), movementZone(unit.movementZone);
    if (speedType.contains("hover") || movementZone.contains("hover") || movementZone.contains("fly"))
        return landType.hover;
    if (speedType.contains("amphib") || movementZone.contains("amphib"))
        return landType.amphibious;
    if (speedType.contains("floatbeach") || movementZone.contains("floatbeach"))
        return landType.floatBeach;
    if (speedType.contains("float") || speedType.contains("water") || movementZone.contains("water")
        || unit.naval || unit.waterBound)
        return landType.floatSpeed;
    if (speedType.contains("track")) return landType.track;
    if (speedType.contains("wheel")) return landType.wheel;
    if (infantry || speedType.contains("foot") || movementZone.contains("infantry")) return landType.foot;
    return unit.crusher ? landType.track : landType.wheel;
}
