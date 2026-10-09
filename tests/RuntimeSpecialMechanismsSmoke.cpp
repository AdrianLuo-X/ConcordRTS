#include "RuntimeSpecialMechanisms.h"

#include <array>
#include <iostream>
#include <limits>

int main() {
    const int result = runRuntimeSpecialMechanismsSmokeTest();
    if (result != 0) return result;

    // Terrain targets use existing ground/bridge save IDs. Check the shared
    // boundary where the two kinds meet, plus IDs that must never launch.
    struct Case { int id; int x; int y; bool bridge; };
    constexpr std::array<Case, 6> valid{{
        {-2959999, 999, 999, false}, {-1960000, 0, 0, false},
        {-1959999, 999, 999, true}, {-960000, 0, 0, true},
        {-2035075, 75, 75, false}, {-1035075, 75, 75, true}
    }};
    for (const auto& test : valid) {
        SpecialCell cell;
        bool bridge = false;
        if (!decodeRuntimeCarrierCellTarget(test.id, cell, bridge)
            || cell.x != test.x || cell.y != test.y || bridge != test.bridge) {
            std::cerr << "Carrier terrain target boundary rejected: " << test.id << '\n';
            return 1;
        }
    }
    constexpr std::array<int, 8> invalid{{
        -2960000, -959999, -2, -1, 0, 1,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max()
    }};
    for (int id : invalid) {
        SpecialCell cell;
        bool bridge = false;
        if (decodeRuntimeCarrierCellTarget(id, cell, bridge)) {
            std::cerr << "Carrier terrain target accepted invalid ID: " << id << '\n';
            return 1;
        }
    }
    std::cout << "Carrier terrain target boundaries: 6 valid, 8 invalid passed\n";
    return 0;
}
