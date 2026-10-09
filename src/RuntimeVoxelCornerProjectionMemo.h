#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

// One decoder section only. A hit returns the exact five doubles produced by
// the original expression on its first evaluation; no projection is rewritten.
// The row tags make revisiting nonmonotonic rows correct as well as ordinary
// x-fastest VXL column traversal. Unsupported dimensions/keys keep the original.
class RuntimeVoxelCornerProjectionMemo {
public:
    using Corner = std::array<double, 5>;
    static constexpr std::size_t maximumScratchBytes = 1024U * 1024U;

    RuntimeVoxelCornerProjectionMemo(int sizeX, int sizeY, int sizeZ,
        bool enabled = true, std::size_t byteLimit = maximumScratchBytes)
        : sizeX_(sizeX), sizeY_(sizeY), sizeZ_(sizeZ) {
        if (!enabled || !supportedDimensions(sizeX, sizeY, sizeZ)) return;
        planeSize_ = static_cast<std::size_t>(sizeX + 1)
            * static_cast<std::size_t>(sizeZ + 1);
        const std::size_t entryCount = 2U * planeSize_;
        const std::size_t requiredBytes = sizeof(*this)
            + entryCount * (sizeof(Corner) + sizeof(std::uint8_t));
        if (requiredBytes > std::min(byteLimit, maximumScratchBytes)) return;
        try {
            // Corner values are intentionally uninitialized. The zeroed ready
            // array guards every read, avoiding unnecessary full value clears.
            values_.reset(new Corner[entryCount]);
            ready_.reset(new std::uint8_t[entryCount]{});
            scratchBytes_ = requiredBytes;
        } catch (const std::bad_alloc&) {
            values_.reset();
            ready_.reset();
        }
    }

    RuntimeVoxelCornerProjectionMemo(const RuntimeVoxelCornerProjectionMemo&) = delete;
    RuntimeVoxelCornerProjectionMemo& operator=(const RuntimeVoxelCornerProjectionMemo&) = delete;
    RuntimeVoxelCornerProjectionMemo(RuntimeVoxelCornerProjectionMemo&&) = delete;
    RuntimeVoxelCornerProjectionMemo& operator=(RuntimeVoxelCornerProjectionMemo&&) = delete;

    bool enabled() const noexcept { return values_ != nullptr && ready_ != nullptr; }
    // Includes the fixed scope object and both exact-sized array payloads.
    // Disabled/fallback scopes allocate no array storage and report zero.
    std::size_t scratchBytes() const noexcept { return scratchBytes_; }

    template<class OriginalProjection>
    Corner project(double rawX, double rawY, double rawZ,
        OriginalProjection&& originalProjection) {
        if (!enabled()) return std::forward<OriginalProjection>(originalProjection)();
        int x = 0, y = 0, z = 0;
        if (!gridCoordinate(rawX, sizeX_, x)
            || !gridCoordinate(rawY, sizeY_, y)
            || !gridCoordinate(rawZ, sizeZ_, z))
            return std::forward<OriginalProjection>(originalProjection)();

        const std::size_t slot = static_cast<std::size_t>(y & 1);
        const std::size_t rowStart = slot * planeSize_;
        if (rowTags_[slot] != y) {
            std::fill_n(ready_.get() + rowStart, planeSize_, std::uint8_t{0});
            rowTags_[slot] = y;
        }
        const std::size_t index = rowStart
            + static_cast<std::size_t>(z) * static_cast<std::size_t>(sizeX_ + 1)
            + static_cast<std::size_t>(x);
        if (ready_[index] != 0) return values_[index];
        const Corner value = std::forward<OriginalProjection>(originalProjection)();
        values_[index] = value;
        ready_[index] = 1;
        return value;
    }

private:
    static bool supportedDimensions(int x, int y, int z) noexcept {
        // Native VXL section dimensions are unsigned bytes. Keeping this exact
        // domain also makes all allocation products bounded without overflow.
        return x > 0 && y > 0 && z > 0 && x <= 255 && y <= 255 && z <= 255;
    }
    static bool gridCoordinate(double value, int maximum, int& result) noexcept {
        if (!std::isfinite(value) || value < 0.0 || value > maximum
            // Negative zero could have a distinct original floating result.
            || (value == 0.0 && std::signbit(value))) return false;
        result = static_cast<int>(value);
        return value == static_cast<double>(result);
    }

    int sizeX_ = 0;
    int sizeY_ = 0;
    int sizeZ_ = 0;
    std::size_t planeSize_ = 0;
    std::size_t scratchBytes_ = 0;
    std::array<int, 2> rowTags_{-1, -1};
    std::unique_ptr<Corner[]> values_;
    std::unique_ptr<std::uint8_t[]> ready_;
};
