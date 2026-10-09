#pragma once

#include <cstddef>
#include <cstdint>

namespace concord::bink_stream {

constexpr std::uint32_t kMagic = 0x314b4e42U; // "BNK1"
constexpr std::uint32_t kVersion = 1;

enum class PlaybackMode : std::uint32_t {
    Loop,
    Once
};

constexpr bool completesAfterFrame(
    PlaybackMode mode,
    std::uint32_t frameNumber,
    std::uint32_t frameCount
) {
    return mode == PlaybackMode::Once
        && frameCount > 0
        && frameNumber >= frameCount;
}

static_assert(!completesAfterFrame(PlaybackMode::Loop, 10, 10));
static_assert(!completesAfterFrame(PlaybackMode::Once, 9, 10));
static_assert(completesAfterFrame(PlaybackMode::Once, 10, 10));

// Shared by the 64-bit runtime and the 32-bit Bink decoder helper.  Keep the
// layout pointer-free and naturally aligned so Win32 Interlocked operations
// can publish a completed frame across the process boundary.
struct SharedHeader {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t capacityWidth = 0;
    std::uint32_t capacityHeight = 0;
    std::uint32_t frameCapacityBytes = 0;
    volatile std::int32_t status = 0;
    volatile std::int32_t activeBuffer = 0;
    volatile std::int32_t sequence = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t frameBytes = 0;
    std::uint32_t frames = 0;
    std::uint32_t frameRate = 0;
    std::uint32_t frameRateDiv = 0;
    std::uint32_t frameNumber = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(SharedHeader) == 64);
static_assert(offsetof(SharedHeader, status) % 4 == 0);
static_assert(offsetof(SharedHeader, activeBuffer) % 4 == 0);
static_assert(offsetof(SharedHeader, sequence) % 4 == 0);

inline std::size_t mappingBytes(std::uint32_t frameCapacityBytes) {
    return sizeof(SharedHeader)
        + static_cast<std::size_t>(frameCapacityBytes) * 2U;
}

} // namespace concord::bink_stream
