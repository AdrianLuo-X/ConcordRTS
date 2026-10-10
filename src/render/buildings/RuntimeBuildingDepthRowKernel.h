#pragma once

#include "render/buildings/RuntimeBuildingDepth.h"

#include <algorithm>
#include <array>

// Prepared from the complete immutable draw description, never reused by a
// partial pose key. Viewport clipping is absent from native depth calculation.
class RuntimeBuildingDepthRowKernel {
public:
    struct Row {
        int width = 0;
        int constantDepth = 0;
        int templateDepth = 0;
        int templateFirstX = 0, templateLastX = 0, templateOriginU = 0;
        const unsigned char* templatePixels = nullptr;
        bool valid = false;

        int depthAtKnownX(int x) const {
            if (templatePixels && x >= templateFirstX && x < templateLastX)
                return templateDepth - signedTemplateValues[templatePixels[templateOriginU + x]];
            return constantDepth;
        }
        bool at(int x, int& depth) const {
            if (!valid || x < 0 || x >= width) return false;
            depth = depthAtKnownX(x);
            return true;
        }
    private:
        static constexpr std::array<int, 256> signedTemplateValues = [] {
            std::array<int, 256> values{};
            for (unsigned int raw = 1; raw != 256; ++raw) {
                const unsigned int transformed = (raw + 256U - 65U) & 255U;
                values[raw] = transformed >= 128 ? static_cast<int>(transformed) - 256
                    : static_cast<int>(transformed);
            }
            return values;
        }();
    };

    RuntimeBuildingDepthRowKernel(const RuntimeBuildingDepthFrame& frame,
        const std::vector<unsigned char>& pixels, int templateWidth, int templateHeight)
        : frame_(frame) {
        if (frame.width <= 0 || frame.height <= 0) return;
        if (frame.layer == RuntimeBuildingDepthLayer::NavalVoxel
            || frame.layer == RuntimeBuildingDepthLayer::NavalSprite
            || frame.layer == RuntimeBuildingDepthLayer::InfantrySprite
            || frame.layer == RuntimeBuildingDepthLayer::WallSprite) {
            gradientBottom_ = frame.layer == RuntimeBuildingDepthLayer::NavalVoxel
                && frame.navalBottom != 0 ? frame.navalBottom : frame.height;
            const int base = 32768 - frame.nativeFrameTopY - gradientBottom_ - frame.heightPixel
                - (frame.layer == RuntimeBuildingDepthLayer::InfantrySprite ? 12
                    : frame.layer == RuntimeBuildingDepthLayer::NavalSprite ? 2
                    : frame.layer == RuntimeBuildingDepthLayer::WallSprite ? 1 : 0);
            quantizedBase_ = (base / 3) * 3;
            return;
        }
        gradientBottom_ = frame.height;
        adjustment_ = frame.layer == RuntimeBuildingDepthLayer::Body
            ? frame.normalZAdjust : frame.zAdjust + frame.yDrawOffset;
        const int base = frame.layer == RuntimeBuildingDepthLayer::Body && frame.foundationWidth < 8
            ? 32768 - frame.nativeFrameTopY - frame.height + frame.normalZAdjust - frame.heightPixel - 1
            : 32768 - (frame.nativeFrameTopY + frame.height) + adjustment_ - frame.heightPixel - 1;
        quantizedBase_ = (base / 3) * 3;
        if (frame.layer != RuntimeBuildingDepthLayer::Body || frame.foundationWidth >= 8
            || templateWidth <= 0 || templateHeight <= 0
            || pixels.size() < static_cast<std::size_t>(templateWidth) * templateHeight) return;
        originU_ = frame.offsetX - frame.canvasWidth / 2 + 198
            - 30 * (frame.foundationWidth - frame.foundationHeight) + frame.moveX;
        originV_ = frame.offsetY - frame.canvasHeight / 2 + 446
            - 15 * (frame.foundationWidth + frame.foundationHeight - 2) + frame.moveY;
        // Use wide bounds arithmetic only to reject nonintersecting templates;
        // the depth expressions below retain the original signed int order.
        if (static_cast<long long>(originU_) + frame.width <= 0 || originU_ >= templateWidth
            || static_cast<long long>(originV_) + frame.height <= 0 || originV_ >= templateHeight) return;
        const int firstRow = originV_ < 0 ? -originV_ : 0;
        const int lastRow = templateHeight - originV_ < frame.height ? templateHeight - originV_ : frame.height;
        const int templateFrameHeight = lastRow - firstRow;
        templateBase_ = 32768 - (frame.nativeFrameTopY + templateFrameHeight)
            + frame.normalZAdjust - frame.heightPixel - 1;
        firstX_ = static_cast<int>(std::max(0LL, -static_cast<long long>(originU_)));
        lastX_ = static_cast<int>(std::min(static_cast<long long>(frame.width),
            static_cast<long long>(templateWidth) - originU_));
        templatePixels_ = pixels.data(); templateWidth_ = templateWidth; templateHeight_ = templateHeight;
    }

    Row row(int y) const {
        Row result;
        result.width = frame_.width;
        result.valid = y >= 0 && y < frame_.height && frame_.width > 0;
        if (!result.valid) return result;
        if (frame_.layer == RuntimeBuildingDepthLayer::Flat || frame_.layer == RuntimeBuildingDepthLayer::Bib)
            result.constantDepth = 32768 - (frame_.nativeFrameTopY + y)
                + adjustment_ - frame_.heightPixel - 3;
        else result.constantDepth = quantizedBase_ - (gradientBottom_ - 1 - y) / 3;
        const int v = originV_ + y;
        if (templatePixels_ && v >= 0 && v < templateHeight_) {
            result.templatePixels = templatePixels_ + static_cast<std::size_t>(v) * templateWidth_;
            result.templateFirstX = firstX_; result.templateLastX = lastX_;
            result.templateOriginU = originU_; result.templateDepth = templateBase_;
        }
        return result;
    }

private:
    RuntimeBuildingDepthFrame frame_;
    int quantizedBase_ = 0, gradientBottom_ = 0, adjustment_ = 0;
    int originU_ = 0, originV_ = 0, templateBase_ = 0;
    int firstX_ = 0, lastX_ = 0, templateWidth_ = 0, templateHeight_ = 0;
    const unsigned char* templatePixels_ = nullptr;
};
