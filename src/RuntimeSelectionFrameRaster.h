#pragma once

#include "RuntimeSelectedPathRaster.h"
#include <map>
#include <limits>
#include <list>
#include <string>
#include <tuple>

inline thread_local int runtimeSelectionBatchReferenceOverride = -1;
inline bool runtimeSelectionBatchReference() {
    if (runtimeSelectionBatchReferenceOverride >= 0)
        return runtimeSelectionBatchReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_SELECTION_BATCH_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeSelectionBatchReferenceScope {
    int previous_;
public:
    explicit RuntimeSelectionBatchReferenceScope(bool reference)
        : previous_(runtimeSelectionBatchReferenceOverride) {
        runtimeSelectionBatchReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeSelectionBatchReferenceScope() { runtimeSelectionBatchReferenceOverride = previous_; }
    RuntimeSelectionBatchReferenceScope(const RuntimeSelectionBatchReferenceScope&) = delete;
    RuntimeSelectionBatchReferenceScope& operator=(const RuntimeSelectionBatchReferenceScope&) = delete;
};

inline thread_local int runtimeSelectionFrameReferenceOverride = -1;
inline bool runtimeSelectionFrameReference() {
    if (runtimeSelectionFrameReferenceOverride >= 0)
        return runtimeSelectionFrameReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_FRAME_SCALING_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}
class RuntimeSelectionFrameReferenceScope {
    int previous_;
public:
    explicit RuntimeSelectionFrameReferenceScope(bool reference)
        : previous_(runtimeSelectionFrameReferenceOverride) {
        runtimeSelectionFrameReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeSelectionFrameReferenceScope() { runtimeSelectionFrameReferenceOverride = previous_; }
};

class RuntimeSelectionFrameRaster {
    friend bool runRuntimeSelectionFrameRasterContract(std::string& error);
    using Key = std::tuple<int, int, int, int>;
    struct Pixel { short x, y; std::uint32_t color; };
    struct Cache {
        struct Stamp { std::vector<Pixel> pixels; std::list<Key>::iterator lru; };
        static constexpr int side = 512;
        static constexpr std::size_t maxKeys = 256;
        static constexpr std::size_t maxPixels = 256 * 1024;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(113, 181, 255));
        std::uint32_t* pixels{};
        std::map<Key, Stamp> stamps;
        std::list<Key> lru;
        std::size_t pixelCount = 0;
        std::uint64_t hits = 0, generations = 0, evictions = 0, bulkClears = 0;
        Cache() {
            if (!dc || !pen) return;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = side;
            info.bmiHeader.biHeight = -side;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                reinterpret_cast<void**>(&pixels), nullptr, 0);
            if (!bitmap) return;
            previous = SelectObject(dc, bitmap);
            SelectObject(dc, pen);
            SelectObject(dc, GetStockObject(NULL_BRUSH));
            SetBkMode(dc, TRANSPARENT);
        }
        ~Cache() {
            if (dc) { SelectObject(dc, GetStockObject(BLACK_PEN)); if (previous) SelectObject(dc, previous); }
            if (bitmap) DeleteObject(bitmap);
            if (pen) DeleteObject(pen);
            if (dc) DeleteDC(dc);
        }
        Cache(const Cache&) = delete;
        Cache& operator=(const Cache&) = delete;
        bool ready() const { return dc && pixels && previous && previous != HGDI_ERROR && pen; }
        const std::vector<Pixel>* find(const Key& key, bool& flushed) {
            flushed = false;
            if (const auto it = stamps.find(key); it != stamps.end()) {
                ++hits;
                lru.splice(lru.end(), lru, it->second.lru);
                return &it->second.pixels;
            }
            const auto [width, height, cx, cy] = key;
            const int left = std::min({0, width, cx - 8}) - 3;
            const int top = std::min({0, height, cy - 8}) - 3;
            const int right = std::max({0, width, cx + 9}) + 3;
            const int bottom = std::max({0, height, cy + 9}) + 3;
            if (right - left > side || bottom - top > side) return nullptr;
            for (int y = 0; y < bottom - top; ++y)
                std::fill_n(pixels + y * side, right - left, 0U);
            Rectangle(dc, -left, -top, width - left, height - top);
            MoveToEx(dc, cx - left - 8, cy - top, nullptr);
            LineTo(dc, cx - left + 9, cy - top);
            MoveToEx(dc, cx - left, cy - top - 8, nullptr);
            LineTo(dc, cx - left, cy - top + 9);
            if (!GdiFlush()) return nullptr;
            flushed = true;
            ++generations;
            std::vector<Pixel> stamp;
            for (int y = 0; y < bottom - top; ++y) for (int x = 0; x < right - left; ++x) {
                const auto color = pixels[y * side + x];
                if (color) stamp.push_back({static_cast<short>(x + left), static_cast<short>(y + top), color});
            }
            // Consume synchronously. Reclaim only the least recently used
            // shapes so moving units cannot turn every common shape cold.
            if (runtimeSelectionBatchReference()) {
                if (stamps.size() >= maxKeys || pixelCount + stamp.size() > maxPixels) {
                    stamps.clear(); lru.clear(); pixelCount = 0; ++bulkClears;
                }
            } else {
                while (!lru.empty() && (stamps.size() >= maxKeys
                    || pixelCount + stamp.size() > maxPixels)) {
                    const auto victim = stamps.find(lru.front());
                    pixelCount -= victim->second.pixels.size();
                    stamps.erase(victim); lru.pop_front(); ++evictions;
                }
            }
            pixelCount += stamp.size();
            lru.push_back(key);
            return &stamps.emplace(key, Stamp{std::move(stamp), std::prev(lru.end())}).first->second.pixels;
        }
    };
    HDC dc_{};
    std::uint32_t* pixels_{};
    int width_ = 0;
    int height_ = 0;
    HGDIOBJ pen_{};
    RECT clip_{};
    Cache* cache_{};
    bool trustedRestoredStateBatch_ = false;
    bool stateDirty_ = false;
    bool nativeWritesPending_ = true;
    std::uint64_t stateChecks_ = 0, nativeFlushes_ = 0;
public:
    RuntimeSelectionFrameRaster(HDC dc, std::uint32_t* pixels, int width, int height)
        : dc_(dc), pixels_(pixels), width_(width), height_(height) {
        if (runtimeSelectionFrameReference()) return;
        ++stateChecks_;
        if (!RuntimeSelectedPathRaster::supported(dc, pixels, width, height, clip_)) return;
        LOGPEN pen{};
        if (GetObject(GetCurrentObject(dc, OBJ_PEN), sizeof(pen), &pen) != sizeof(pen)
            || pen.lopnStyle != PS_SOLID || pen.lopnWidth.x != 2
            || pen.lopnColor != RGB(113, 181, 255)) return;
        static thread_local Cache cache;
        if (cache.ready()) { cache_ = &cache; pen_ = GetCurrentObject(dc, OBJ_PEN); }
    }
    bool available() const { return cache_ != nullptr; }
    // Explicit caller promise: this DIB/clip/pen/brush/ROP/mapping is restored
    // after every helper in the batch. Notify actual native writes individually;
    // notify un-restored DC changes instead of drawing with a stale clip/state.
    bool beginTrustedRestoredDcStateBatch() {
        trustedRestoredStateBatch_ = available() && !runtimeSelectionBatchReference();
        if (trustedRestoredStateBatch_) {
            ++stateChecks_;
            if (GetCurrentObject(dc_, OBJ_PEN) != pen_
                || !RuntimeSelectedPathRaster::supported(dc_, pixels_, width_, height_, clip_)) {
                trustedRestoredStateBatch_ = false;
                return false;
            }
            stateDirty_ = false;
        }
        return trustedRestoredStateBatch_;
    }
    void markNativeGdiWrites() { nativeWritesPending_ = true; }
    void markDcStateChanged() { stateDirty_ = true; nativeWritesPending_ = true; }
    bool prepareForCpuWrites() {
        if (trustedRestoredStateBatch_ && !nativeWritesPending_) return true;
        ++nativeFlushes_;
        if (!GdiFlush()) return false;
        nativeWritesPending_ = false;
        return true;
    }
    std::uint64_t stateChecks() const { return stateChecks_; }
    std::uint64_t nativeFlushes() const { return nativeFlushes_; }
    bool draw(const RECT& bounds, POINT center) {
        if (!cache_) return false;
        if (center.x < std::numeric_limits<LONG>::min() + 8
            || center.x > std::numeric_limits<LONG>::max() - 9
            || center.y < std::numeric_limits<LONG>::min() + 8
            || center.y > std::numeric_limits<LONG>::max() - 9) return false;
        // Default callers still validate every boundary. The engine overlay
        // may opt into its narrower, explicitly restored-state batch contract.
        if (!trustedRestoredStateBatch_ || stateDirty_) {
            ++stateChecks_;
            if (GetCurrentObject(dc_, OBJ_PEN) != pen_
                || !RuntimeSelectedPathRaster::supported(dc_, pixels_, width_, height_, clip_)) return false;
            stateDirty_ = false;
        }
        const std::int64_t width = static_cast<std::int64_t>(bounds.right) - bounds.left;
        const std::int64_t height = static_cast<std::int64_t>(bounds.bottom) - bounds.top;
        const std::int64_t cx = static_cast<std::int64_t>(center.x) - bounds.left;
        const std::int64_t cy = static_cast<std::int64_t>(center.y) - bounds.top;
        for (auto value : {width, height, cx, cy}) if (value < -240 || value > 240) return false;
        bool cacheFlushed = false;
        const auto* stamp = cache_->find({static_cast<int>(width), static_cast<int>(height),
            static_cast<int>(cx), static_cast<int>(cy)}, cacheFlushed);
        if (!stamp) return false;
        // Generating a cold stamp already flushes this thread's GDI batch.
        if (trustedRestoredStateBatch_ && cacheFlushed) nativeWritesPending_ = false;
        if (!prepareForCpuWrites()) return false;
        // Keep the original interleaved pixel order; only redundant state
        // queries and known-empty native boundaries are skipped.
        for (const auto& pixel : *stamp) {
            const std::int64_t x = static_cast<std::int64_t>(bounds.left) + pixel.x;
            const std::int64_t y = static_cast<std::int64_t>(bounds.top) + pixel.y;
            if (x >= clip_.left && x < clip_.right && y >= clip_.top && y < clip_.bottom)
                pixels_[static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)] = pixel.color;
        }
        MoveToEx(dc_, center.x, center.y + 9, nullptr);
        return true;
    }
};
