#include "assets/menu/MenuImage.h"
#include <algorithm>

namespace concord::assets {

bool makeMenuImage(const MenuIndexedFrameView& frame,
    const std::vector<RuntimePaletteColor>& palette, const std::string& source,
    bool transparentZero, MenuImage& output) {
    output = {};
    if (palette.size() < 256 || frame.width <= 0 || frame.height <= 0
        || frame.pixels.size() < static_cast<std::size_t>(frame.width * frame.height)) {
        return false;
    }
    output.width = std::max(frame.globalWidth, frame.offsetX + frame.width);
    output.height = std::max(frame.globalHeight, frame.offsetY + frame.height);
    if (output.width <= 0 || output.height <= 0) return false;
    output.bgra.assign(static_cast<std::size_t>(output.width * output.height * 4), 0);
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const std::size_t sourceAt = static_cast<std::size_t>(y * frame.width + x);
            const unsigned char index = frame.pixels[sourceAt];
            const int dx = frame.offsetX + x;
            const int dy = frame.offsetY + y;
            if (dx < 0 || dy < 0 || dx >= output.width || dy >= output.height) continue;
            const std::size_t destAt = static_cast<std::size_t>(dy * output.width + dx) * 4;
            const RuntimePaletteColor color = palette[index];
            output.bgra[destAt] = color.b;
            output.bgra[destAt + 1] = color.g;
            output.bgra[destAt + 2] = color.r;
            output.bgra[destAt + 3] = transparentZero && index == 0 ? 0 : 255;
        }
    }
    output.source = source;
    return true;
}

} // namespace concord::assets
