#pragma once

#include "runtime/config/RuntimeConfig.h"

#include <string_view>

// Observer is a lobby role, never a country in the random combatant pool.
inline bool runtimeObserverCountry(std::string_view country) {
    const auto space = [](char value) {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    };
    while (!country.empty() && space(country.front())) country.remove_prefix(1);
    while (!country.empty() && space(country.back())) country.remove_suffix(1);
    const auto equal = [country](std::string_view token) {
        if (country.size() != token.size()) return false;
        for (std::size_t i = 0; i < token.size(); ++i) {
            char value = country[i];
            if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
            if (value != token[i]) return false;
        }
        return true;
    };
    return equal("observer") || equal("obs");
}

template <typename Slot>
inline bool runtimeObserverSlot(const Slot& slot) {
    return slot.enabled && slot.human && runtimeObserverCountry(slot.country);
}

inline bool runtimeLocalObserver(const RuntimeConfig& config) {
    // Network spectators use the existing participation role and transport.
    if (config.multiplayerActive) return false;
    for (const auto& slot : config.playerSlots) {
        if (slot.enabled && slot.human && slot.local) return runtimeObserverSlot(slot);
    }
    for (const auto& slot : config.playerSlots) {
        if (slot.enabled && slot.human) return runtimeObserverSlot(slot);
    }
    return runtimeObserverCountry(config.playerCountry);
}
