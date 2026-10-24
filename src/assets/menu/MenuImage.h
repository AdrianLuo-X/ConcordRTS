#pragma once

#include "assets/codecs/RuntimeTmpTypes.h"
#include <span>
#include <string>
#include <vector>

namespace concord::assets {

struct MenuImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bgra;
    std::string source;

    bool valid() const {
        return width > 0 && height > 0
            && bgra.size() == static_cast<std::size_t>(width * height * 4);
    }
};

// Borrowed only for this call. Returned MenuImage owns all pixels and source.
struct MenuIndexedFrameView {
    int offsetX = 0;
    int offsetY = 0;
    int globalWidth = 0;
    int globalHeight = 0;
    int width = 0;
    int height = 0;
    std::span<const unsigned char> pixels;
};

bool makeMenuImage(const MenuIndexedFrameView& frame,
    const std::vector<RuntimePaletteColor>& palette, const std::string& source,
    bool transparentZero, MenuImage& output);

} // namespace concord::assets
