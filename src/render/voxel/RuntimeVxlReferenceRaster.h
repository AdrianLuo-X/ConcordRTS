#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace concord::render {

struct VxlReferenceFaceView {
    const std::array<double, 4>& x;
    const std::array<double, 4>& y;
    const std::array<double, 4>& depth;
    double normalBrightness;
    std::uint32_t material;
    std::uint8_t color;
    std::uint8_t normalIndex;
    std::uint8_t vplSection;
    bool ownerRemap;
};

struct VxlReferenceRasterTarget {
    int width;
    int height;
    int padding;
    double depthEpsilon;
    double faceBias;
    std::uint8_t neutralLight;
    std::span<double> depths;
    std::span<std::uint8_t> pixels;
    std::span<std::uint8_t> rawColorIndices;
    std::span<std::uint8_t> ownerRemapMask;
    std::span<std::uint8_t> lightLevels;
    std::span<std::uint8_t> normalIndices;
    std::span<std::uint8_t> normalBrightnessLevels;
    std::span<std::uint8_t> vplSections;
};

// The caller owns the already projected face, raster planes and their bounds.
// It also supplies its existing brightness and material storage policies.
template<class FaceCount, class TriangleCount, class Brightness, class WriteMaterial, class Stop>
void rasterizeVxlReferenceFace(const VxlReferenceFaceView& face, const VxlReferenceRasterTarget& target,
    FaceCount& faces, TriangleCount& triangles, const Brightness& brightness,
    const WriteMaterial& writeMaterial, const Stop& stopRequested) {
    ++faces;
    if (face.color == 0) return;
    const auto triangle = [&](std::size_t a, std::size_t b, std::size_t c) {
        const double ax = static_cast<double>(target.padding) + face.x[a];
        const double ay = static_cast<double>(target.padding) + face.y[a];
        const double bx = static_cast<double>(target.padding) + face.x[b];
        const double by = static_cast<double>(target.padding) + face.y[b];
        const double cx = static_cast<double>(target.padding) + face.x[c];
        const double cy = static_cast<double>(target.padding) + face.y[c];
        const int left = std::clamp(static_cast<int>(std::floor(std::min({ax, bx, cx}))), 0, target.width - 1);
        const int right = std::clamp(static_cast<int>(std::ceil(std::max({ax, bx, cx}))), 0, target.width - 1);
        const int top = std::clamp(static_cast<int>(std::floor(std::min({ay, by, cy}))), 0, target.height - 1);
        const int bottom = std::clamp(static_cast<int>(std::ceil(std::max({ay, by, cy}))), 0, target.height - 1);
        const double denominator = ((by - cy) * (ax - cx)) + ((cx - bx) * (ay - cy));
        if (std::abs(denominator) < 0.0001) return;

        ++triangles;
        for (int py = top; py <= bottom; ++py) {
            if (stopRequested()) return;
            for (int px = left; px <= right; ++px) {
                const double sampleX = static_cast<double>(px) + 0.5;
                const double sampleY = static_cast<double>(py) + 0.5;
                const double weightA =
                    (((by - cy) * (sampleX - cx)) + ((cx - bx) * (sampleY - cy))) / denominator;
                const double weightB =
                    (((cy - ay) * (sampleX - cx)) + ((ax - cx) * (sampleY - cy))) / denominator;
                const double weightC = 1.0 - weightA - weightB;
                if (weightA < -0.01 || weightB < -0.01 || weightC < -0.01) continue;
                const double pixelDepth = (weightA * face.depth[a]) + (weightB * face.depth[b])
                    + (weightC * face.depth[c]) + target.faceBias;
                const auto pixelIndex = static_cast<std::size_t>(py) * static_cast<std::size_t>(target.width)
                    + static_cast<std::size_t>(px);
                if (pixelDepth <= target.depths[pixelIndex] + target.depthEpsilon) continue;

                target.depths[pixelIndex] = pixelDepth;
                target.pixels[pixelIndex] = face.color;
                target.rawColorIndices[pixelIndex] = static_cast<unsigned char>(face.material & 0xFFU);
                target.ownerRemapMask[pixelIndex] = face.ownerRemap ? 1 : 0;
                target.lightLevels[pixelIndex] = target.neutralLight;
                target.normalIndices[pixelIndex] = face.normalIndex;
                target.normalBrightnessLevels[pixelIndex] = brightness(face.normalBrightness);
                target.vplSections[pixelIndex] = face.vplSection;
                writeMaterial(pixelIndex, face.material);
            }
        }
    };
    triangle(0, 1, 2);
    triangle(0, 2, 3);
}

} // namespace concord::render
