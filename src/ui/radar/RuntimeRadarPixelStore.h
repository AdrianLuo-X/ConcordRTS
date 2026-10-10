#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>

inline thread_local int runtimeRadarPixelStoreReferenceOverride = -1;
inline bool runtimeRadarPixelStoreReference() noexcept {
    if (runtimeRadarPixelStoreReferenceOverride >= 0)
        return runtimeRadarPixelStoreReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_RADAR_PIXEL_STORE_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeRadarPixelStoreReferenceScope {
public:
    explicit RuntimeRadarPixelStoreReferenceScope(bool reference) noexcept
        : previous_(runtimeRadarPixelStoreReferenceOverride) {
        runtimeRadarPixelStoreReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeRadarPixelStoreReferenceScope() noexcept {
        runtimeRadarPixelStoreReferenceOverride = previous_;
    }
    RuntimeRadarPixelStoreReferenceScope(const RuntimeRadarPixelStoreReferenceScope&) = delete;
    RuntimeRadarPixelStoreReferenceScope& operator=(const RuntimeRadarPixelStoreReferenceScope&) = delete;
private:
    int previous_;
};

struct RuntimeRadarPixelStoreSummary {
    std::uint64_t stores = 0, directStores = 0, gdiStores = 0;
    std::uint64_t nonRgbStores = 0, outsideStores = 0;
    unsigned int flushCalls = 0;
    bool reference = false, standardDib = false, directEnabled = false;
    bool flushSucceeded = false;
};

// Test-only observer. Normal rendering has no observer, no per-pixel clock,
// allocation, global cache, or persistent borrowed bitmap pointer.
struct RuntimeRadarPixelStoreObserver {
    void* context = nullptr;
    void (*prepare)(void*, HDC, HBITMAP, int, int) noexcept = nullptr;
    void (*finish)(void*, const RuntimeRadarPixelStoreSummary&) noexcept = nullptr;
};
inline thread_local const RuntimeRadarPixelStoreObserver* runtimeRadarPixelStoreObserver = nullptr;
class RuntimeRadarPixelStoreObserverScope {
public:
    explicit RuntimeRadarPixelStoreObserverScope(RuntimeRadarPixelStoreObserver observer) noexcept
        : previous_(runtimeRadarPixelStoreObserver), observer_(observer) {
        runtimeRadarPixelStoreObserver = &observer_;
    }
    ~RuntimeRadarPixelStoreObserverScope() noexcept { runtimeRadarPixelStoreObserver = previous_; }
    RuntimeRadarPixelStoreObserverScope(const RuntimeRadarPixelStoreObserverScope&) = delete;
    RuntimeRadarPixelStoreObserverScope& operator=(const RuntimeRadarPixelStoreObserverScope&) = delete;
private:
    const RuntimeRadarPixelStoreObserver* previous_;
    RuntimeRadarPixelStoreObserver observer_;
};

// Own RuntimeBackBuffer only. Construct immediately after the existing
// FillRect/DeleteObject; the private DC/bitmap/mapping/clip must then remain
// unchanged until this writer dies at the end of the radar rebuild block.
// knownTopDownOwned must record the caller's successful negative-height DIB
// creation, not a direction inferred from GetObject's normalized header.
class RuntimeRadarPixelStore {
public:
    RuntimeRadarPixelStore(HDC dc, HBITMAP bitmap, void* pixels, int width, int height,
        bool knownTopDownOwned,
        bool reference = runtimeRadarPixelStoreReference()) noexcept
        : dc_(dc), pixels_(static_cast<std::uint32_t*>(pixels)), width_(width), height_(height),
          observer_(runtimeRadarPixelStoreObserver) {
        summary_.reference = reference;
        if (observer_ && observer_->prepare)
            observer_->prepare(observer_->context, dc, bitmap, width, height);
        if (reference) return; // The reference preserves the old GDI batching.
        summary_.standardDib = isStandardDib(dc, bitmap, pixels, width, height, knownTopDownOwned);
        if (!summary_.standardDib) return;
        // Flush once before any CPU access. This synchronizes the unchanged
        // FillRect and any earlier queued GDI writes to this same DIB.
        summary_.flushCalls = 1;
        summary_.flushSucceeded = GdiFlush() != FALSE;
        summary_.directEnabled = summary_.flushSucceeded;
    }
    ~RuntimeRadarPixelStore() noexcept {
        if (observer_ && observer_->finish) observer_->finish(observer_->context, summary_);
    }
    RuntimeRadarPixelStore(const RuntimeRadarPixelStore&) = delete;
    RuntimeRadarPixelStore& operator=(const RuntimeRadarPixelStore&) = delete;

    BOOL store(int x, int y, COLORREF color) noexcept {
        ++summary_.stores;
        const bool rgb = (color & 0xFF000000UL) == 0;
        const bool inside = x >= 0 && y >= 0 && x < width_ && y < height_;
        if (!rgb) ++summary_.nonRgbStores;
        if (!inside) ++summary_.outsideStores;
        if (summary_.directEnabled && rgb && inside) {
            const auto packed = (static_cast<std::uint32_t>(GetRValue(color)) << 16U)
                | (static_cast<std::uint32_t>(GetGValue(color)) << 8U)
                | static_cast<std::uint32_t>(GetBValue(color));
            pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)
                + static_cast<std::size_t>(x)] = packed;
            ++summary_.directStores;
            return TRUE;
        }
        ++summary_.gdiStores;
        const BOOL result = SetPixelV(dc_, x, y, color);
        // A per-call fallback can enqueue a native write before a later CPU
        // store. Permanently use the native path for the rest of this scope,
        // so there is never an unsynchronized CPU/GDI mixture or second flush.
        summary_.directEnabled = false;
        return result;
    }
    const RuntimeRadarPixelStoreSummary& summary() const noexcept { return summary_; }

private:
    static bool isStandardDib(HDC dc, HBITMAP bitmap, const void* pixels,
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
    HDC dc_;
    std::uint32_t* pixels_;
    int width_, height_;
    const RuntimeRadarPixelStoreObserver* observer_;
    RuntimeRadarPixelStoreSummary summary_;
};
