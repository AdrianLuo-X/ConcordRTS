#pragma once

#include "core/players/PlayerSlotRole.h"
#include "runtime/config/RuntimeConfig.h"

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
