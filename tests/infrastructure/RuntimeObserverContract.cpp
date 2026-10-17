#include "ui/input/RuntimeObserver.h"

#include <iostream>

int main() {
    bool success = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; success = false; }
    };
    const auto slot = [](bool enabled, bool human, bool local, const char* country) {
        RuntimeConfig::PlayerSlot value;
        value.enabled = enabled;
        value.human = human;
        value.local = local;
        value.country = country;
        return value;
    };
    RuntimeConfig config;
    config.playerCountry = " \tObS\r\n";
    expect(runtimeLocalObserver(config), "empty slots must fall back to playerCountry");
    config.multiplayerActive = true;
    expect(!runtimeLocalObserver(config), "network spectator must not become a local observer");
    config.multiplayerActive = false;
    config.playerSlots = {slot(false, true, true, "Americans"), slot(true, false, true, "Americans")};
    expect(runtimeLocalObserver(config), "disabled/AI slots must not suppress country fallback");
    config.playerSlots = {slot(true, true, false, "Americans"), slot(true, true, true, "Observer")};
    expect(runtimeLocalObserver(config), "local human must take priority over earlier remote human");
    config.multiplayerActive = true;
    expect(!runtimeLocalObserver(config), "multiplayer must override observer slots and fallback");
    config.multiplayerActive = false;
    config.playerSlots = {slot(true, true, false, "Observer"), slot(true, true, true, "Americans")};
    expect(!runtimeLocalObserver(config), "local combatant must suppress earlier observer and fallback");
    config.playerSlots = {slot(true, true, true, "Americans"), slot(true, true, true, "Observer")};
    expect(!runtimeLocalObserver(config), "first eligible local human wins");
    config.playerCountry = "Americans";
    config.playerSlots = {slot(true, true, true, "Observer"), slot(true, true, true, "Americans")};
    expect(runtimeLocalObserver(config), "first local observer wins over later local combatant");
    config.playerCountry = "Observer";
    config.playerSlots = {slot(true, true, false, "Americans"), slot(true, true, false, "Observer")};
    expect(!runtimeLocalObserver(config), "first human wins when no local human exists");
    config.playerCountry = "Americans";
    config.playerSlots = {slot(false, true, true, "Americans"), slot(true, false, true, "Americans"),
        slot(true, true, false, "\tOBS "), slot(true, true, false, "Americans")};
    expect(runtimeLocalObserver(config), "fallback human scan must skip disabled and AI slots");
    config.playerSlots.clear();
    expect(!runtimeLocalObserver(config), "ordinary country fallback must remain combatant");
    std::cout << "Runtime local observer contract: 11 priority/fallback/multiplayer cases.\n";
    return success ? 0 : 1;
}
