#pragma once

#include <cstdint>
#include <limits>
#include <vector>

// Retail BUILDNGZ.SHA coordinates are always in the original 60x30 space.
// Screen placement (including camera and zoom) is deliberately absent here.
enum class RuntimeBuildingDepthLayer { Body, Animation, Flat, Bib, NavalVoxel, NavalSprite, InfantrySprite, WallSprite };

inline int runtimeBuildingAnimationSlotYSort(int offsetX, int offsetY, int ySortAdjust) {
    // Retail inverse-projection coefficients are the original float literals
    // (bits 0x408888CE and 0x410888CE), not exact 256/60 and 256/30 ratios.
    // Its x87 multiply/add retains precision until each result is stored as a
    // float; the two coordinates are then independently truncated to leptons.
    constexpr float xCoefficient = 4.2667F;
    constexpr float yCoefficient = 8.5334F;
    const double xInput = static_cast<float>(offsetX);
    const double yInput = static_cast<float>(offsetY);
    const float x = static_cast<float>(xInput * static_cast<double>(xCoefficient)
        + yInput * static_cast<double>(yCoefficient));
    const float y = static_cast<float>(-xInput * static_cast<double>(xCoefficient)
        + yInput * static_cast<double>(yCoefficient));
    return static_cast<int>(x) + static_cast<int>(y) + ySortAdjust;
}

struct RuntimeBuildingDepthFrame {
    int canvasWidth = 0, canvasHeight = 0;
    int width = 0, height = 0, offsetX = 0, offsetY = 0;
    int foundationWidth = 1, foundationHeight = 1;
    int moveX = 0, moveY = 0, normalZAdjust = 0;
    int nativeFrameTopY = 0, heightPixel = 0;
    int zAdjust = 0, yDrawOffset = 0;
    int navalBottom = 0;
    RuntimeBuildingDepthLayer layer = RuntimeBuildingDepthLayer::Body;
};

inline bool runtimeBuildingDepthAtPixel(const RuntimeBuildingDepthFrame& frame,
    const std::vector<unsigned char>& templatePixels, int templateWidth,
    int templateHeight, int x, int y, int& depth) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) return false;
    if (frame.layer == RuntimeBuildingDepthLayer::NavalVoxel
        || frame.layer == RuntimeBuildingDepthLayer::NavalSprite
        || frame.layer == RuntimeBuildingDepthLayer::InfantrySprite
        || frame.layer == RuntimeBuildingDepthLayer::WallSprite) {
        // UnitClass's final VXL compositor reads (but does not write) scene Z.
        // Ordinary Drive/Ship locomotion supplies gradient 2 and drawZ = -heightPixel - 1.
        // Keep the un-clipped raster bounds so panning cannot change its phase.
        const int bottomRow = frame.layer == RuntimeBuildingDepthLayer::NavalVoxel
            && frame.navalBottom != 0 ? frame.navalBottom : frame.height;
        const int base = 32768 - frame.nativeFrameTopY - bottomRow - frame.heightPixel
            // InfantryClass passes the retail -10 body bias before Techno's
            // common SHP -2 adjustment. Unit/Ship SHP (including DRON) and VXL retain their own bias.
            - (frame.layer == RuntimeBuildingDepthLayer::InfantrySprite ? 12
                : frame.layer == RuntimeBuildingDepthLayer::NavalSprite ? 2
                : frame.layer == RuntimeBuildingDepthLayer::WallSprite ? 1 : 0);
        depth = (base / 3) * 3 - (bottomRow - 1 - y) / 3;
        return true;
    }
    const int bottom = frame.nativeFrameTopY + frame.height;
    if (frame.layer == RuntimeBuildingDepthLayer::Body && frame.foundationWidth < 8) {
        const int originU = frame.offsetX - frame.canvasWidth / 2 + 198
            - 30 * (frame.foundationWidth - frame.foundationHeight) + frame.moveX;
        const int originV = frame.offsetY - frame.canvasHeight / 2 + 446
            - 15 * (frame.foundationWidth + frame.foundationHeight - 2) + frame.moveY;
        const int u = originU + x, v = originV + y;
        if (u < 0 || v < 0 || u >= templateWidth || v >= templateHeight
            || templatePixels.size() < static_cast<std::size_t>(templateWidth) * templateHeight)
            return false;
        const unsigned char raw = templatePixels[static_cast<std::size_t>(v) * templateWidth + u];
        const unsigned int transformed = raw == 0 ? 0 : (static_cast<unsigned int>(raw) + 256U - 65U) & 255U;
        const int signedTemplate = transformed >= 128 ? static_cast<int>(transformed) - 256 : static_cast<int>(transformed);
        const int firstTemplateRow = originV < 0 ? -originV : 0;
        const int lastTemplateRow = templateHeight - originV < frame.height
            ? templateHeight - originV : frame.height;
        const int templateFrameHeight = lastTemplateRow - firstTemplateRow;
        // The retail intersection supplies the initial destination height;
        // viewport clipping happens later and must not change this base.
        depth = 32768 - (frame.nativeFrameTopY + templateFrameHeight)
            + frame.normalZAdjust - frame.heightPixel - 1 - signedTemplate;
        return true;
    }
    const int adjustment = frame.layer == RuntimeBuildingDepthLayer::Body
        ? frame.normalZAdjust : frame.zAdjust + frame.yDrawOffset;
    const int base = 32768 - bottom + adjustment - frame.heightPixel - 1;
    if (frame.layer == RuntimeBuildingDepthLayer::Flat || frame.layer == RuntimeBuildingDepthLayer::Bib) {
        depth = 32768 - (frame.nativeFrameTopY + y) + adjustment - frame.heightPixel - 3;
    } else {
        depth = (base / 3) * 3 - (frame.height - 1 - y) / 3;
    }
    return true;
}

inline bool runtimeBuildingRenderDepthAtPixel(const RuntimeBuildingDepthFrame& frame,
    const std::vector<unsigned char>& templatePixels, int templateWidth,
    int templateHeight, int x, int y, int& depth) {
    if (runtimeBuildingDepthAtPixel(frame, templatePixels, templateWidth, templateHeight, x, y, depth)) return true;
    if (frame.layer != RuntimeBuildingDepthLayer::Body
        || x < 0 || y < 0 || x >= frame.width || y >= frame.height) return false;
    // Several stock SHPs extend below BUILDNGZ (including YAREFN and YAWEAP).
    // This pass adds inter-building occlusion, so it must not erase those
    // existing silhouette pixels. Use the same Deg90 rule as a body without
    // a template for only the uncovered pixels; never sample/clamp past it.
    const int base = 32768 - frame.nativeFrameTopY - frame.height
        + frame.normalZAdjust - frame.heightPixel - 1;
    depth = (base / 3) * 3 - (frame.height - 1 - y) / 3;
    return true;
}

struct RuntimeBuildingDepthPixel {
    int depth = std::numeric_limits<int>::max();
    int depthOwner = -1;
    int visibleOwner = -1;
};

inline bool runtimeBuildingDepthAccept(RuntimeBuildingDepthPixel& pixel,
    int instanceId, int depth, bool writeDepth) {
    // This pass adds inter-building depth only. Preserve each building's
    // established internal layer sequence, including its authored foreground.
    const bool ownVisiblePixel = pixel.depthOwner == instanceId && pixel.visibleOwner == instanceId;
    if (!ownVisiblePixel && depth >= pixel.depth) return false;
    if (writeDepth) {
        pixel.depth = ownVisiblePixel && pixel.depth < depth ? pixel.depth : depth;
        pixel.depthOwner = instanceId;
    }
    pixel.visibleOwner = instanceId;
    return true;
}
