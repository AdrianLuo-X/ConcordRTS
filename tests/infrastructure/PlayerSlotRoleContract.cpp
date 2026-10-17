#include "core/players/PlayerSlotRole.h"

#include <iostream>
#include <string_view>

namespace {
struct Slot {
    bool enabled;
    bool human;
    std::string_view country;
};
}

int main() {
    bool success = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; success = false; }
    };
    constexpr std::string_view accepted[] = {
        "observer", "obs", "OBSERVER", "ObS", " \t\r\nObSeRvEr\n\r\t ",
        "\tOBS\r", " observer", "obs "
    };
    constexpr std::string_view rejected[] = {
        "", " \t\r\n", "Americans", "Random", "observerx", "xobs", "ob server",
        "ob\tserver", "observer\v", "\fobs", "\vobserver", "obs\f",
        std::string_view("obs\0", 4), std::string_view("obs\xC2\xA0", 5),
        std::string_view("\xA0obs", 4)
    };
    for (const auto country : accepted) {
        expect(runtimeObserverCountry(country), "observer spelling rejected");
        for (const bool enabled : {false, true}) {
            for (const bool human : {false, true}) {
                expect(runtimeObserverSlot(Slot{enabled, human, country}) == (enabled && human),
                    "slot role must require both enabled and human");
            }
        }
    }
    for (const auto country : rejected) {
        expect(!runtimeObserverCountry(country), "non-observer spelling accepted");
        expect(!runtimeObserverSlot(Slot{true, true, country}), "combatant classified as observer");
    }
    std::cout << "Player slot role contract: 8 accepted/15 rejected spellings, 32 slot flag combinations.\n";
    return success ? 0 : 1;
}
