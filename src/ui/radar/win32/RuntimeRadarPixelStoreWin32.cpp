#include "ui/radar/win32/RuntimeRadarPixelStoreWin32.h"
#include <limits>

RuntimeRadarPixelStore::RuntimeRadarPixelStore(HDC dc, HBITMAP bitmap, void* pixels,
    int width, int height, bool knownTopDownOwned, bool reference,
    RuntimeRadarPixelStoreGdiApi api) noexcept
    : dc_(dc), observer_(runtimeRadarPixelStoreObserver),
      writer_({{}, width, height, 0, 0}, reference) {
    if (observer_ && observer_->prepare)
        observer_->prepare(observer_->context, dc, bitmap, width, height);
    if (reference) return;
    if (!isStandardDib(dc, bitmap, pixels, width, height, knownTopDownOwned)) return;
    const auto stride = static_cast<std::size_t>(width) * 4;
    concord::ui::MenuPixelSurfaceView surface{
        {static_cast<unsigned char*>(pixels), stride * static_cast<std::size_t>(height)},
        width, height, stride, 0};
    writer_.prepareDirect(surface, api.flush() != FALSE);
}
RuntimeRadarPixelStore::~RuntimeRadarPixelStore() noexcept {
    if (observer_ && observer_->finish) observer_->finish(observer_->context, writer_.summary());
}
bool RuntimeRadarPixelStore::isStandardDib(HDC dc, HBITMAP bitmap, const void* pixels,
        int width, int height, bool knownTopDownOwned) noexcept {
        // GetObject normalizes DIBSECTION.biHeight to positive on Windows;
        // it cannot establish scan-line orientation. The mandatory caller
        // evidence comes only from this own DIB's negative-height creation.
        if (!knownTopDownOwned || !dc || !bitmap || !pixels || width <= 0 || height <= 0
            || static_cast<std::uint64_t>(width) * 4U
                > static_cast<std::uint64_t>((std::numeric_limits<LONG>::max)())
            || static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height)
                > (std::numeric_limits<std::size_t>::max)() / sizeof(std::uint32_t)
            || GetCurrentObject(dc, OBJ_BITMAP) != bitmap
            || GetMapMode(dc) != MM_TEXT || GetGraphicsMode(dc) != GM_COMPATIBLE
            || GetLayout(dc) != 0
            || SetICMMode(dc, ICM_QUERY) != ICM_OFF) return false;
        POINT window{}, viewport{};
        RECT clip{};
        if (!GetWindowOrgEx(dc, &window) || !GetViewportOrgEx(dc, &viewport)
            || window.x != 0 || window.y != 0 || viewport.x != 0 || viewport.y != 0
            || GetClipBox(dc, &clip) != SIMPLEREGION
            || clip.left != 0 || clip.top != 0 || clip.right != width || clip.bottom != height)
            return false;
        DIBSECTION dib{};
        return GetObjectW(bitmap, sizeof(dib), &dib) == sizeof(dib)
            && dib.dsBm.bmBits == pixels && dib.dsBm.bmWidth == width && dib.dsBm.bmHeight == height
            && dib.dsBm.bmWidthBytes == width * 4 && dib.dsBm.bmPlanes == 1 && dib.dsBm.bmBitsPixel == 32
            && dib.dsBmih.biSize == sizeof(BITMAPINFOHEADER)
            && dib.dsBmih.biWidth == width
            && (dib.dsBmih.biHeight == height || dib.dsBmih.biHeight == -height)
            && dib.dsBmih.biPlanes == 1 && dib.dsBmih.biBitCount == 32
            && dib.dsBmih.biCompression == BI_RGB
            && dib.dsBmih.biClrUsed == 0 && dib.dshSection == nullptr && dib.dsOffset == 0;
    }
