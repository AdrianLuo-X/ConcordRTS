#pragma once

#include "assets/menu/MenuImage.h"
#include <filesystem>

namespace concord::assets {

enum class MenuBmpReadCapability { Unsupported, Bmp };

// A borrowed path is used only during read; successful output owns its pixels.
// The provider reports support separately from a particular missing/bad file.
// Read retains the original filesystem/allocation exception policy.
class MenuBmpReader {
public:
    virtual ~MenuBmpReader() = default;
    virtual MenuBmpReadCapability capability() const noexcept = 0;
    virtual bool read(const std::filesystem::path& path, MenuImage& output) const = 0;
};

} // namespace concord::assets
