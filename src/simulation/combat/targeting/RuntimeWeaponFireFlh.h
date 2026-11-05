#pragma once

#include <optional>
#include <string>

// Original authored world-FLH value, shared by physical launch and visual binding.
struct RuntimeWeaponFireFlh {
    int forwardLeptons = 0;
    int lateralLeptons = 0;
    int heightLeptons = 0;
    bool authored = false;
};

std::optional<RuntimeWeaponFireFlh> parseRuntimeWeaponFireFlh(const std::string& rawFlh);
