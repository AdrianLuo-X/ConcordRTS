#pragma once

#include "assets/menu/MenuImage.h"
#include "assets/preview/MapPreviewResourceContext.h"
#include <filesystem>
#include <memory>

namespace concord::assets {

// One serial menu owner. Its private provider retains exactly the existing
// renderer resource caches; no live world, device, frame or native handle is
// accepted by this interface. S10's root/order DTO is reused, not its nine
// preview cache domains. Returned images/palettes are independent owned data.
class MenuImageResources {
public:
    explicit MenuImageResources(MapPreviewResourceContext roots = {});
    ~MenuImageResources();
    MenuImageResources(MenuImageResources&&) noexcept;
    MenuImageResources& operator=(MenuImageResources&&) noexcept;
    MenuImageResources(const MenuImageResources&) = delete;
    MenuImageResources& operator=(const MenuImageResources&) = delete;

    // The old menu configured these root fields before loading. As before,
    // assigning roots does not clear or homogenize the existing keyed caches.
    void configureRoots(MapPreviewResourceContext roots);
    const MapPreviewResourceContext& roots() const;
    bool loadPalette(const std::string& id, std::vector<RuntimePaletteColor>& palette,
        std::string& source, std::string& error);
    bool loadFrame(const std::string& id, int frameIndex,
        const std::vector<RuntimePaletteColor>& palette, bool transparentZero,
        MenuImage& output, std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Existing main-menu loose-file path: frame zero, global canvas/offsets,
// original runtime PAL conversion, no independent parser/search policy.
bool loadMenuLooseFrame(const std::string& shp, const std::string& pal,
    bool transparentZero, MenuImage& output);

// Existing skirmish loose-file policy: frame-local canvas, source left empty,
// output reset before any attempt. Deliberately distinct from main global canvas.
bool loadMenuFrameLocalShp(const std::filesystem::path& shpPath,
    const std::filesystem::path& palettePath, int frameIndex, MenuImage& output,
    bool transparentZero = false);

} // namespace concord::assets
