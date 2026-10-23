#pragma once

// Private shared helpers for the product implementation and its legacy diagnostics.
// No mechanism state or new public API is exposed here.
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"

#include <cstddef>
#include <string>

namespace runtime_special_mechanisms_detail {

constexpr std::size_t kMaximumTimedBombRecords = 4096;

inline std::string boolText(bool value) {
    return value ? "yes" : "no";
}

inline SpecialCell objectCell(const WorldObjectInstance* object) {
    return object == nullptr
        ? SpecialCell{}
        : SpecialCell{object->x, object->y};
}

} // namespace runtime_special_mechanisms_detail
