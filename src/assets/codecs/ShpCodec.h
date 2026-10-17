#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Compatibility profiles preserve the two existing consumers' accepted inputs.
// Resource selection, palette, fallback and renderer cache state stay outside.
enum class ShpDecodePolicy { Preview, Runtime };

struct ShpIndexedFrame {
    int offsetX = 0;
    int offsetY = 0;
    int globalWidth = 0;
    int globalHeight = 0;
    int width = 0;
    int height = 0;
    int frameIndex = 0;
    int format = 0;
    std::uint32_t dataOffset = 0;
    bool authoredPause = false;
    std::vector<unsigned char> pixels;
    std::string sourcePath;

    bool operator==(const ShpIndexedFrame&) const = default;
};

// Failure leaves frame unchanged. Success preserves a caller's existing error.
bool decodeShpIndexedFrame(
    const std::vector<unsigned char>& bytes,
    const std::string& source,
    int frameIndex,
    ShpDecodePolicy policy,
    ShpIndexedFrame& frame,
    std::string& error
);