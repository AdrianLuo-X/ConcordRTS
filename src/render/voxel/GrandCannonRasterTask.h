#pragma once

#include "core/tasks/BoundedTaskExecutor.h"
#include <array>
#include <cstddef>
#include <cstdint>

// The Grand Cannon's existing reference cuboid raster, after owner-thread
// resource selection, VXL/HVA projection and original depth sorting. No pointers,
// resource handles, environment lookups or allocator-backed members cross here.
namespace concord::grand_cannon {
inline constexpr std::size_t MaxFaces = 131072;
inline constexpr std::size_t MaxVoxels = 65536;
inline constexpr int MaxDimension = 384;
struct Face {
    std::array<double, 4> x{}, y{}, depth{};
    double normalBrightness = 0;
    std::uint32_t material = 0;
    std::uint8_t color = 0, normal = 0, vpl = 0, ownerRemap = 0;
};
struct Shadow {
    int x = 0, y = 0;
    std::uint32_t section = 0;
    std::uint8_t color = 0;
};
struct Input {
    int width = 0, height = 0, padding = 0, shadowSection = 0;
    std::uint32_t faces = 0, voxels = 0;
    double depthEpsilon = 0, faceBias = 0, minDepth = 0, maxDepth = 0;
};
struct Output {
    int cuboidFaces = 0, triangles = 0;
    int opaqueWidth = 0, opaqueHeight = 0;
    int topPixels = 0, sidePixels = 0, lowerPixels = 0;
    int reserved = 0;
};
// Output = header, uint32 material plane, then nine byte planes in this order.
enum Plane : std::size_t { Pixels, Raw, Remap, Light, Normal, Brightness, Vpl, Depth, ShadowPixels, PlaneCount };
constexpr std::size_t outputBytes(std::size_t pixels) { return sizeof(Output) + pixels * (4 + PlaneCount); }
std::size_t raster(const tasks::JobContext& context);
std::size_t workerBudget(int requested, unsigned hardwareThreads) noexcept;
} // namespace concord::grand_cannon