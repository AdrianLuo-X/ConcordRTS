#pragma once

#include <cstdint>

// Original gamemd.exe 0x547F63..0x547F97 seeds TMP Z from its unzoomed
// diamond top and absolute level, with a u16 truncation before the height
// subtraction. 0x5487AB..0x5487B6 adds the unsigned TMP byte, compares the
// low16 candidate <= stored Z, and stores that word. In world coordinates
// (60x30 tiles) the top/height terms cancel level: 0x8000-15*(x+y+1).
// Caller 0x480416/0x48044B reads Cell+0x11B for both the Y lift
// and argument 10 at call 0x480499; this is not TMP header Height.
// Camera translation and presentation zoom therefore do not change depth.
// This helper is for tile-to-tile visibility, not Foot/SHP/building scene Z.
inline std::uint16_t runtimeTmpNativeDepthWord(int cellX, int cellY,
    unsigned char authoredDepth) {
    const int seed = 0x8000 - 15 * (cellX + cellY + 1);
    return static_cast<std::uint16_t>(seed + authoredDepth);
}