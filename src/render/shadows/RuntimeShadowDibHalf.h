#pragma once

#include <cstdint>

// The original black blend at alpha 128 is (channel * 127 + 127) / 255.
// For c=2k it is k + floor((127-k)/255); for c=2k+1 it is
// k + floor((254-k)/255). Both remainders are in [0,254] for c in [0,255].
// Clearing each low bit before shifting prevents cross-channel carry. The
// high byte remains zero, exactly like packRuntimeDibColor(runtimeBlendBlack()).
inline constexpr std::uint32_t runtimeHalfBlackShadowDib(std::uint32_t pixel) {
    return (pixel & 0x00fefefeU) >> 1U;
}
