#include "assets/menu/MenuImageResources.h"
#include "render/core/RuntimeMapRenderer.h"
#include <utility>

namespace concord::assets {

struct MenuImageResources::Impl {
    MapPreviewResourceContext roots;
    RuntimeMapRenderContext context;
    RuntimeMapRenderCache cache;
};

MenuImageResources::MenuImageResources(MapPreviewResourceContext roots)
    : impl_(std::make_unique<Impl>()) {
    configureRoots(std::move(roots));
}
MenuImageResources::~MenuImageResources() = default;
MenuImageResources::MenuImageResources(MenuImageResources&&) noexcept = default;
MenuImageResources& MenuImageResources::operator=(MenuImageResources&&) noexcept = default;

void MenuImageResources::configureRoots(MapPreviewResourceContext roots) {
    impl_->roots = std::move(roots);
    impl_->context.tileRootPath = impl_->roots.tileRootPath;
    impl_->context.assetRootPath = impl_->roots.assetRootPath;
    impl_->context.structureRootPath = impl_->roots.assetRootPath;
    impl_->context.palettePath = impl_->roots.palettePath;
    impl_->context.structurePalettePath = impl_->roots.structurePalettePath;
}
const MapPreviewResourceContext& MenuImageResources::roots() const {
    return impl_->roots;
}
bool MenuImageResources::loadPalette(const std::string& id,
    std::vector<RuntimePaletteColor>& palette, std::string& source, std::string& error) {
    return loadRuntimePaletteResource(impl_->context, impl_->cache, id, palette, source, error);
}
bool MenuImageResources::loadFrame(const std::string& id, int frameIndex,
    const std::vector<RuntimePaletteColor>& palette, bool transparentZero,
    MenuImage& output, std::string& error) {
    RuntimeShpFrameImage frame;
    std::string resolved;
    std::string source;
    if (!loadRuntimeAuxiliaryShpFrameImage(impl_->context, impl_->cache, id,
            frameIndex, frame, resolved, source, error, false)) return false;
    if (!makeMenuImage({frame.offsetX, frame.offsetY, frame.globalWidth,
            frame.globalHeight, frame.width, frame.height, frame.pixels},
            palette, source, transparentZero, output)) {
        error = "invalid decoded SHP frame: " + id;
        return false;
    }
    return true;
}
bool loadMenuLooseFrame(const std::string& shp, const std::string& pal,
    bool transparentZero, MenuImage& output) {
    RuntimeShpFrameImage frame;
    std::vector<RuntimePaletteColor> palette;
    std::string error;
    if (!decodeRuntimeShpFrameFile(shp, 0, frame, error)
        || !loadRuntimePaletteFile(pal, palette, error)) return false;
    return makeMenuImage({frame.offsetX, frame.offsetY, frame.globalWidth,
        frame.globalHeight, frame.width, frame.height, frame.pixels},
        palette, shp, transparentZero, output);
}

bool loadMenuFrameLocalShp(
    const std::filesystem::path& shpPath,
    const std::filesystem::path& palettePath,
    int frameIndex,
    MenuImage& output,
    bool transparentZero
) {
    output = MenuImage{};
    if (shpPath.empty() || palettePath.empty()) {
        return false;
    }
    RuntimeShpFrameImage frame;
    std::string error;
    if (!decodeRuntimeShpFrameFile(shpPath.string(), frameIndex, frame, error)) {
        return false;
    }
    std::vector<RuntimePaletteColor> palette;
    if (!loadRuntimePaletteFile(palettePath.string(), palette, error)
        || palette.size() < 256) {
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0
        || frame.pixels.size()
            < static_cast<std::size_t>(frame.width * frame.height)) {
        return false;
    }
    output.width = frame.width;
    output.height = frame.height;
    output.indices=frame.pixels;output.indexWidth=frame.width;output.indexHeight=frame.height;
    output.transparentZero=transparentZero;
    for(std::size_t i=0;i<256;++i) {
        const auto c=palette[i];
        output.paletteBgra[i]=static_cast<std::uint32_t>(c.b)|(static_cast<std::uint32_t>(c.g)<<8)
            |(static_cast<std::uint32_t>(c.r)<<16)|0xff000000U;
    }
    output.bgra.resize(static_cast<std::size_t>(frame.width * frame.height * 4));
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
        const unsigned char index = frame.pixels[i];
        const RuntimePaletteColor color = palette[index];
        output.bgra[i * 4] = color.b;
        output.bgra[i * 4 + 1] = color.g;
        output.bgra[i * 4 + 2] = color.r;
        output.bgra[i * 4 + 3] = transparentZero && index == 0 ? 0 : 255;
    }
    return true;
}

} // namespace concord::assets
