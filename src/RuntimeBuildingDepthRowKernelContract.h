#pragma once

#include "RuntimeBuildingDepthRowKernel.h"

#include <string>

// Compare the prepared evaluator with the unchanged per-pixel renderer rule.
// Descriptor fields are varied individually; no shortened descriptor key is
// assumed. These small pure fixtures also exercise every raw template byte.
inline bool runRuntimeBuildingDepthRowKernelContractCheck(std::string& error) {
    std::size_t fixture = 0;
    const auto compare = [&](const RuntimeBuildingDepthFrame& frame,
        const std::vector<unsigned char>& shape, int width, int height) {
        const RuntimeBuildingDepthRowKernel kernel(frame, shape, width, height);
        ++fixture;
        for (int y = -1; y <= std::max(0, frame.height); ++y) {
            const auto row = kernel.row(y);
            for (int x = -1; x <= std::max(0, frame.width); ++x) {
                int expected = 0, actual = 0;
                const bool accepted = runtimeBuildingRenderDepthAtPixel(frame, shape, width, height, x, y, expected);
                if (row.at(x, actual) != accepted || (accepted && actual != expected)) {
                    error = "building depth row kernel: fixture " + std::to_string(fixture)
                        + " at " + std::to_string(x) + "," + std::to_string(y);
                    return false;
                }
            }
        }
        return true;
    };
    std::vector<unsigned char> shape(64 * 80);
    for (std::size_t index = 0; index < shape.size(); ++index)
        shape[index] = static_cast<unsigned char>(index * 73 + index / 64);
    RuntimeBuildingDepthFrame frame;
    frame.canvasWidth = 35; frame.canvasHeight = 31;
    frame.width = 17; frame.height = 13; frame.offsetX = 7; frame.offsetY = 9;
    frame.foundationWidth = 2; frame.foundationHeight = 3;
    frame.moveX = -198; frame.moveY = -386;
    frame.normalZAdjust = -37; frame.nativeFrameTopY = 110;
    frame.heightPixel = 45; frame.zAdjust = -25; frame.yDrawOffset = 17; frame.navalBottom = 5;
    constexpr std::array<int RuntimeBuildingDepthFrame::*, 16> fields{
        &RuntimeBuildingDepthFrame::canvasWidth, &RuntimeBuildingDepthFrame::canvasHeight,
        &RuntimeBuildingDepthFrame::width, &RuntimeBuildingDepthFrame::height,
        &RuntimeBuildingDepthFrame::offsetX, &RuntimeBuildingDepthFrame::offsetY,
        &RuntimeBuildingDepthFrame::foundationWidth, &RuntimeBuildingDepthFrame::foundationHeight,
        &RuntimeBuildingDepthFrame::moveX, &RuntimeBuildingDepthFrame::moveY,
        &RuntimeBuildingDepthFrame::normalZAdjust, &RuntimeBuildingDepthFrame::nativeFrameTopY,
        &RuntimeBuildingDepthFrame::heightPixel, &RuntimeBuildingDepthFrame::zAdjust,
        &RuntimeBuildingDepthFrame::yDrawOffset, &RuntimeBuildingDepthFrame::navalBottom};
    constexpr unsigned int layerCount = static_cast<unsigned int>(RuntimeBuildingDepthLayer::WallSprite) + 1U;
    for (unsigned int layer = 0; layer < layerCount; ++layer) {
        auto base = frame; base.layer = static_cast<RuntimeBuildingDepthLayer>(layer);
        if (!compare(base, shape, 64, 80) || !compare(base, {}, 64, 80)
            || !compare(base, std::vector<unsigned char>(7, 70), 64, 80)
            || !compare(base, shape, 0, 80) || !compare(base, shape, 64, 0)
            || !compare(base, shape, -1, 80) || !compare(base, shape, 64, -1)) return false;
        for (const auto field : fields) for (const int value : {-37, -1, 0, 1, 7, 8, 19, 61}) {
            auto changed = base; changed.*field = value;
            if (!compare(changed, shape, 64, 80)) return false;
        }
        // Negative quotient/remainder and a custom naval bottom must retain
        // C++ signed division semantics rather than floor division.
        for (int top : {-400, 32760, 32768, 60000}) for (int bottom : {-25, -1, 0, 1, 37}) {
            auto changed = base; changed.nativeFrameTopY = top; changed.navalBottom = bottom;
            if (!compare(changed, shape, 64, 80)) return false;
        }
    }
    for (int u : {-18, -17, -16, -1, 0, 1, 63, 64, 65})
        for (int v : {-14, -13, -12, -1, 0, 1, 79, 80, 81}) {
            auto clipped = frame;
            clipped.moveX = u - (frame.offsetX - frame.canvasWidth / 2 + 198
                - 30 * (frame.foundationWidth - frame.foundationHeight));
            clipped.moveY = v - (frame.offsetY - frame.canvasHeight / 2 + 446
                - 15 * (frame.foundationWidth + frame.foundationHeight - 2));
            if (!compare(clipped, shape, 64, 80)) return false;
        }
    RuntimeBuildingDepthFrame single;
    single.width = single.height = 1; single.moveX = -198; single.moveY = -446;
    for (unsigned int raw = 0; raw != 256; ++raw)
        if (!compare(single, {static_cast<unsigned char>(raw)}, 1, 1)) return false;
    std::uint32_t random = 0x72916201U;
    const auto next = [&] { random = random * 1664525U + 1013904223U; return random; };
    for (int sample = 0; sample != 512; ++sample) {
        auto varied = frame;
        for (const auto field : fields) varied.*field = static_cast<int>(next() % 401U) - 200;
        varied.width = 1 + static_cast<int>(next() % 24U);
        varied.height = 1 + static_cast<int>(next() % 24U);
        varied.nativeFrameTopY = static_cast<int>(next() % 65000U) - 500;
        varied.layer = static_cast<RuntimeBuildingDepthLayer>(next() % layerCount);
        if (!compare(varied, shape, 64, 80)) return false;
    }
    return true;
}
