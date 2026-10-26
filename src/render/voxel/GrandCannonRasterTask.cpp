#include "render/voxel/GrandCannonRasterTask.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace concord::grand_cannon {
std::size_t workerBudget(int requested, unsigned hardwareThreads) noexcept {
    if (requested >= 0) return static_cast<std::size_t>(std::clamp(requested, 0, 4));
    // Leave two logical processors for the owner and other existing services.
    // This is a conservative ceiling, not a claim that logical CPUs are equal.
    return std::clamp(hardwareThreads > 2 ? hardwareThreads - 2 : 1U, 1U, 4U);
}
std::size_t raster(const tasks::JobContext& context) {
    const auto invalid = [&] { return context.output.size() + 1; };
    if (context.input.size() < sizeof(Input)) return invalid();
    Input input{};
    std::memcpy(&input, context.input.data(), sizeof(input));
    if (input.width <= 0 || input.height <= 0 || input.width > MaxDimension || input.height > MaxDimension
        || input.padding < 0 || input.padding > 24 || input.faces > MaxFaces || input.voxels > MaxVoxels
        || !std::isfinite(input.depthEpsilon) || !std::isfinite(input.faceBias)
        || !std::isfinite(input.minDepth) || !std::isfinite(input.maxDepth)) return invalid();
    const auto count = static_cast<std::size_t>(input.width) * static_cast<std::size_t>(input.height);
    const auto faceBytes = static_cast<std::size_t>(input.faces) * sizeof(Face);
    const auto shadowBytes = static_cast<std::size_t>(input.voxels) * sizeof(Shadow);
    if (context.input.size() != sizeof(Input) + faceBytes + shadowBytes
        || context.output.size() < outputBytes(count) || context.scratch.size() < count * sizeof(double)) return invalid();
    const int width = input.width, height = input.height, padding = input.padding;
    const double referenceDepthEpsilon = input.depthEpsilon, referenceFaceBias = input.faceBias;
    auto* materialBytes = context.output.data() + sizeof(Output);
    auto* planes = reinterpret_cast<unsigned char*>(materialBytes + count * sizeof(std::uint32_t));
    auto* pixelDepths = reinterpret_cast<double*>(context.scratch.data());
    std::fill(context.output.begin(), context.output.end(), std::byte{});
    std::fill_n(pixelDepths, count, -std::numeric_limits<double>::infinity());
    auto* pixels = planes + Pixels * count;
    auto* rawColorIndices = planes + Raw * count;
    auto* ownerRemapMask = planes + Remap * count;
    auto* lightLevels = planes + Light * count;
    auto* normalIndices = planes + Normal * count;
    auto* normalBrightnessLevels = planes + Brightness * count;
    auto* vplSections = planes + Vpl * count;
    auto* depthLevels = planes + Depth * count;
    auto* shadowPixels = planes + ShadowPixels * count;
    std::fill_n(lightLevels, count, 192);
    std::fill_n(normalBrightnessLevels, count, 128);
    std::fill_n(vplSections, count, 255);
    Output result{};
    const auto putShadowPixel = [&](int x, int y, unsigned char strength) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const auto index = static_cast<std::size_t>(y) * width + x;
        shadowPixels[index] = std::max(shadowPixels[index], strength);
    };
    for (std::size_t i = 0; i < input.voxels; ++i) {
        if (context.stopRequested()) return 0;
        Shadow voxel{};
        std::memcpy(&voxel, context.input.data() + sizeof(Input) + faceBytes + i * sizeof(Shadow), sizeof(voxel));
        if (voxel.x < -65536 || voxel.x > 65536 || voxel.y < -65536 || voxel.y > 65536) return invalid();
        if (voxel.color == 0 || voxel.section != static_cast<unsigned int>(input.shadowSection)) continue;
        const int sx = padding + voxel.x, sy = padding + voxel.y;
        putShadowPixel(sx, sy, 170); putShadowPixel(sx + 1, sy, 120);
        putShadowPixel(sx - 1, sy, 80); putShadowPixel(sx, sy + 1, 115);
        putShadowPixel(sx + 1, sy + 1, 80); putShadowPixel(sx, sy - 1, 55);
    }
    const auto brightnessByte = [](double brightness) {
        if (!std::isfinite(brightness)) brightness = 0.0;
        brightness = std::clamp(brightness, -1.0, 1.0);
        return static_cast<unsigned char>(std::clamp(
            static_cast<int>(std::lround(((brightness + 1.0) * 0.5) * 255.0)), 0, 255));
    };
    const auto triangle = [&](const Face& face, int a, int b, int c) {
        if (face.color == 0) return;
        const double ax = static_cast<double>(padding) + face.x[static_cast<std::size_t>(a)];
        const double ay = static_cast<double>(padding) + face.y[static_cast<std::size_t>(a)];
        const double bx = static_cast<double>(padding) + face.x[static_cast<std::size_t>(b)];
        const double by = static_cast<double>(padding) + face.y[static_cast<std::size_t>(b)];
        const double cx = static_cast<double>(padding) + face.x[static_cast<std::size_t>(c)];
        const double cy = static_cast<double>(padding) + face.y[static_cast<std::size_t>(c)];
        const int left = std::clamp(static_cast<int>(std::floor(std::min({ax, bx, cx}))), 0, width - 1);
        const int right = std::clamp(static_cast<int>(std::ceil(std::max({ax, bx, cx}))), 0, width - 1);
        const int top = std::clamp(static_cast<int>(std::floor(std::min({ay, by, cy}))), 0, height - 1);
        const int bottom = std::clamp(static_cast<int>(std::ceil(std::max({ay, by, cy}))), 0, height - 1);
        const double denominator = ((by - cy) * (ax - cx)) + ((cx - bx) * (ay - cy));
        if (std::abs(denominator) < 0.0001) return;
        ++result.triangles;
        for (int py = top; py <= bottom; ++py) {
            if (context.stopRequested()) return;
            for (int px = left; px <= right; ++px) {
                const double sampleX = static_cast<double>(px) + 0.5;
                const double sampleY = static_cast<double>(py) + 0.5;
                const double weightA = (((by - cy) * (sampleX - cx)) + ((cx - bx) * (sampleY - cy))) / denominator;
                const double weightB = (((cy - ay) * (sampleX - cx)) + ((ax - cx) * (sampleY - cy))) / denominator;
                const double weightC = 1.0 - weightA - weightB;
                if (weightA < -0.01 || weightB < -0.01 || weightC < -0.01) continue;
                const double pixelDepth = (weightA * face.depth[static_cast<std::size_t>(a)])
                    + (weightB * face.depth[static_cast<std::size_t>(b)])
                    + (weightC * face.depth[static_cast<std::size_t>(c)]) + referenceFaceBias;
                const auto pixelIndex = static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px);
                if (pixelDepth <= pixelDepths[pixelIndex] + referenceDepthEpsilon) continue;
                pixelDepths[pixelIndex] = pixelDepth;
                pixels[pixelIndex] = face.color;
                rawColorIndices[pixelIndex] = static_cast<unsigned char>(face.material & 0xFFU);
                ownerRemapMask[pixelIndex] = face.ownerRemap ? 1 : 0;
                lightLevels[pixelIndex] = 192;
                normalIndices[pixelIndex] = face.normal;
                normalBrightnessLevels[pixelIndex] = brightnessByte(face.normalBrightness);
                vplSections[pixelIndex] = face.vpl;
                std::memcpy(materialBytes + pixelIndex * sizeof(std::uint32_t), &face.material, sizeof(face.material));
            }
        }
    };
    for (std::size_t i = 0; i < input.faces; ++i) {
        if (context.stopRequested()) return 0;
        Face face{};
        std::memcpy(&face, context.input.data() + sizeof(Input) + i * sizeof(Face), sizeof(face));
        for (std::size_t j = 0; j < 4; ++j)
            if (!std::isfinite(face.x[j]) || !std::isfinite(face.y[j]) || !std::isfinite(face.depth[j])
                || std::abs(face.x[j]) > 65536.0 || std::abs(face.y[j]) > 65536.0) return invalid();
        ++result.cuboidFaces;
        triangle(face, 0, 1, 2); triangle(face, 0, 2, 3);
    }
    int left = width, top = height, right = -1, bottom = -1;
    for (int py = 0; py < height; ++py) for (int px = 0; px < width; ++px) {
        const auto index = static_cast<std::size_t>(py) * width + px;
        if (pixels[index] == 0) continue;
        if (input.maxDepth > input.minDepth && std::isfinite(pixelDepths[index])) {
            const double normalized = (pixelDepths[index] - input.minDepth) / (input.maxDepth - input.minDepth);
            depthLevels[index] = static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(std::clamp(normalized, 0.0, 1.0) * 255.0)), 1, 255));
        }
        left = std::min(left, px); top = std::min(top, py); right = std::max(right, px); bottom = std::max(bottom, py);
        std::uint32_t material = 0;
        std::memcpy(&material, materialBytes + index * sizeof(material), sizeof(material));
        const auto surface = (material >> 16) & 0xffU;
        // RuntimeVxlSurfaceClass values are checked at the renderer adapter.
        if (surface == 1) ++result.topPixels;
        else if (surface == 2) ++result.sidePixels;
        else if (surface == 3 || surface == 4) ++result.lowerPixels;
    }
    result.opaqueWidth = right >= left ? right - left + 1 : 0;
    result.opaqueHeight = bottom >= top ? bottom - top + 1 : 0;
    std::memcpy(context.output.data(), &result, sizeof(result));
    return outputBytes(count);
}
} // namespace concord::grand_cannon