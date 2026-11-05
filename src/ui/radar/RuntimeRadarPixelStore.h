#pragma once
#include "ui/surface/RuntimeMenuPixelSurface.h"
#include "render/core/RuntimeMapColorValues.h"
#include <cstdlib>
#include <cstdint>

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

// Synchronous borrowed CPU pixels only. The native adapter proves ownership
// and finishes previous device writes before it prepares this direct view.
class RuntimeRadarPixelWriter {
public:
    explicit RuntimeRadarPixelWriter(concord::ui::MenuPixelSurfaceView dimensions,
        bool reference = false) noexcept : surface_(dimensions) { summary_.reference = reference; }
    RuntimeRadarPixelWriter(const RuntimeRadarPixelWriter&) = delete;
    RuntimeRadarPixelWriter& operator=(const RuntimeRadarPixelWriter&) = delete;
    // One preparation, before any store. A fallback ends direct access forever.
    bool prepareDirect(concord::ui::MenuPixelSurfaceView surface, bool flushed) noexcept {
        if (prepared_ || summary_.stores != 0 || summary_.reference) return false;
        prepared_ = true;
        if (!surface.valid() || surface.width != surface_.width
            || surface.height != surface_.height) return false;
        surface_ = surface;
        summary_.standardDib = true;
        summary_.flushCalls = 1;
        summary_.flushSucceeded = flushed;
        summary_.directEnabled = flushed;
        return flushed;
    }
    template<class NativeStore>
    std::int32_t store(std::int32_t x, std::int32_t y, RuntimeMapColorValue color,
        NativeStore&& nativeStore) noexcept {
        ++summary_.stores;
        const bool rgb = (color & 0xFF000000U) == 0;
        const bool inside = x >= 0 && y >= 0 && x < surface_.width && y < surface_.height;
        if (!rgb) ++summary_.nonRgbStores;
        if (!inside) ++summary_.outsideStores;
        if (summary_.directEnabled && rgb && inside) {
            // Same original little-endian packed write, expressed as BGRA8 bytes
            // so the standard surface representation is endian-independent.
            const auto offset = static_cast<std::size_t>(y) * surface_.rowStride
                + static_cast<std::size_t>(x) * 4;
            surface_.bytes[offset] = static_cast<unsigned char>((color >> 16U) & 0xffU);
            surface_.bytes[offset + 1] = static_cast<unsigned char>((color >> 8U) & 0xffU);
            surface_.bytes[offset + 2] = static_cast<unsigned char>(color & 0xffU);
            surface_.bytes[offset + 3] = 0;
            ++summary_.directStores;
            return 1;
        }
        ++summary_.gdiStores;
        const std::int32_t result = nativeStore(x, y, color);
        summary_.directEnabled = false;
        prepared_ = true;
        return result;
    }
    const RuntimeRadarPixelStoreSummary& summary() const noexcept { return summary_; }
private:
    concord::ui::MenuPixelSurfaceView surface_;
    RuntimeRadarPixelStoreSummary summary_;
    bool prepared_ = false;
};
