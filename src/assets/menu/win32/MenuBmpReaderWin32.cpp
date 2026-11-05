#include "assets/menu/win32/MenuBmpReaderWin32.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <new>

namespace concord::assets {
namespace {
namespace fs = std::filesystem;
std::string pathLabel(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

class Win32MenuBmpReader final : public MenuBmpReader {
public:
    MenuBmpReadCapability capability() const noexcept override {
        return MenuBmpReadCapability::Bmp;
    }
    bool read(const fs::path& path, MenuImage& output) const override {
    output = {};
    if (!regularFile(path)) return false;
    const std::wstring& wide = path.native();
    HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(
        nullptr, wide.c_str(), IMAGE_BITMAP, 0, 0,
        LR_LOADFROMFILE | LR_CREATEDIBSECTION
    ));
    if (bitmap == nullptr) return false;
    BITMAP facts{};
    GetObjectW(bitmap, sizeof(facts), &facts);
    if (facts.bmWidth <= 0 || facts.bmHeight <= 0) {
        DeleteObject(bitmap);
        return false;
    }
    output.width = facts.bmWidth;
    output.height = facts.bmHeight;
    output.bgra.resize(static_cast<std::size_t>(output.width * output.height * 4));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = output.width;
    info.bmiHeader.biHeight = -output.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    const int copied = GetDIBits(
        dc, bitmap, 0, static_cast<UINT>(output.height), output.bgra.data(),
        &info, DIB_RGB_COLORS
    );
    if (dc != nullptr) ReleaseDC(nullptr, dc);
    DeleteObject(bitmap);
    if (copied != output.height) {
        output = {};
        return false;
    }
    for (std::size_t at = 3; at < output.bgra.size(); at += 4) output.bgra[at] = 255;
    output.source = pathLabel(path);
    return true;
}
};
} // namespace

const MenuBmpReader& menuWin32BmpReader() noexcept {
    // The provider owns no native resource. Retain it through late menu teardown
    // without heap allocation or a registered static destructor.
    alignas(Win32MenuBmpReader) static unsigned char storage[sizeof(Win32MenuBmpReader)]{};
    static const Win32MenuBmpReader* const reader =
        ::new (static_cast<void*>(storage)) Win32MenuBmpReader;
    return *reader;
}
} // namespace concord::assets
