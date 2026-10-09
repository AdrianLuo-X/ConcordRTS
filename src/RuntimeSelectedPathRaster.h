#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <unordered_map>
#include <vector>

// The native GDI renderer generates these stamps, including its cosmetic dash
// phase and wide-line endpoints. Only their integer translation is reused.
enum class RuntimePathRasterKind : unsigned char {
    PathLine, Waypoint, MoveDiamond, AttackDiamond, AttackLine
};

inline thread_local int runtimeSelectedPathRasterReferenceOverride = -1;

inline bool runtimeSelectedPathRasterReference() {
    if (runtimeSelectedPathRasterReferenceOverride >= 0)
        return runtimeSelectedPathRasterReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_FRAME_SCALING_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimeSelectedPathRasterReferenceScope {
public:
    explicit RuntimeSelectedPathRasterReferenceScope(bool reference)
        : previous_(runtimeSelectedPathRasterReferenceOverride) {
        runtimeSelectedPathRasterReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeSelectedPathRasterReferenceScope() {
        runtimeSelectedPathRasterReferenceOverride = previous_;
    }
    RuntimeSelectedPathRasterReferenceScope(const RuntimeSelectedPathRasterReferenceScope&) = delete;
    RuntimeSelectedPathRasterReferenceScope& operator=(const RuntimeSelectedPathRasterReferenceScope&) = delete;
private:
    int previous_;
};

inline void drawRuntimePathRasterPrimitive(HDC dc, RuntimePathRasterKind kind,
    POINT from, POINT to) {
    if (kind == RuntimePathRasterKind::Waypoint) {
        Rectangle(dc, to.x - 2, to.y - 2, to.x + 3, to.y + 3);
    } else if (kind == RuntimePathRasterKind::MoveDiamond
        || kind == RuntimePathRasterKind::AttackDiamond) {
        const int radius = kind == RuntimePathRasterKind::MoveDiamond ? 10 : 12;
        MoveToEx(dc, to.x, to.y - radius, nullptr);
        LineTo(dc, to.x + radius, to.y);
        LineTo(dc, to.x, to.y + radius);
        LineTo(dc, to.x - radius, to.y);
        LineTo(dc, to.x, to.y - radius);
    } else {
        MoveToEx(dc, from.x, from.y, nullptr);
        LineTo(dc, to.x, to.y);
    }
}

class RuntimeSelectedPathRasterCache {
public:
    struct Key {
        int dx = 0, dy = 0;
        RuntimePathRasterKind kind = RuntimePathRasterKind::PathLine;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const {
            const auto xy = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.dx)) << 32)
                | static_cast<std::uint32_t>(key.dy);
            return std::hash<std::uint64_t>{}(xy ^ (static_cast<std::uint64_t>(key.kind) * 0x9e3779b97f4a7c15ULL));
        }
    };
    struct Pixel {
        short x = 0, y = 0;
        std::uint32_t color = 0;
    };
    struct Stamp {
        std::vector<Pixel> pixels;
        std::list<Key>::iterator lru;
    };

    RuntimeSelectedPathRasterCache() {
        pens_[0] = CreatePen(PS_DOT, 1, RGB(105, 230, 190));
        pens_[1] = CreatePen(PS_SOLID, 2, RGB(126, 245, 118));
        pens_[2] = CreatePen(PS_SOLID, 2, RGB(246, 138, 82));
        dc_ = CreateCompatibleDC(nullptr);
        if (!dc_) return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = scratchSide;
        info.bmiHeader.biHeight = -scratchSide;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS,
            reinterpret_cast<void**>(&pixels_), nullptr, 0);
        if (!bitmap_) return;
        previousBitmap_ = SelectObject(dc_, bitmap_);
        SetBkMode(dc_, TRANSPARENT);
        SelectObject(dc_, GetStockObject(NULL_BRUSH));
    }
    ~RuntimeSelectedPathRasterCache() {
        if (dc_) {
            SelectObject(dc_, GetStockObject(BLACK_PEN));
            if (previousBitmap_) SelectObject(dc_, previousBitmap_);
        }
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
        for (HPEN pen : pens_) if (pen) DeleteObject(pen);
    }
    RuntimeSelectedPathRasterCache(const RuntimeSelectedPathRasterCache&) = delete;
    RuntimeSelectedPathRasterCache& operator=(const RuntimeSelectedPathRasterCache&) = delete;

    bool ready() const {
        return dc_ && pixels_ && previousBitmap_ && previousBitmap_ != HGDI_ERROR
            && pens_[0] && pens_[1] && pens_[2];
    }
    HPEN pen(RuntimePathRasterKind kind) const {
        return pens_[kind == RuntimePathRasterKind::MoveDiamond ? 1
            : kind == RuntimePathRasterKind::AttackDiamond || kind == RuntimePathRasterKind::AttackLine ? 2 : 0];
    }
    const Stamp* find(const Key& key) {
        const auto found = stamps_.find(key);
        if (found != stamps_.end()) {
            lru_.splice(lru_.end(), lru_, found->second.lru);
            return &found->second;
        }
        const bool line = key.kind == RuntimePathRasterKind::PathLine
            || key.kind == RuntimePathRasterKind::AttackLine;
        const int radius = key.kind == RuntimePathRasterKind::MoveDiamond ? 10
            : key.kind == RuntimePathRasterKind::AttackDiamond ? 12 : 2;
        const int minX = line ? std::min(0, key.dx) - 2 : -radius - 2;
        const int minY = line ? std::min(0, key.dy) - 2 : -radius - 2;
        const int width = line ? std::abs(key.dx) + 5 : radius * 2 + 5;
        const int height = line ? std::abs(key.dy) + 5 : radius * 2 + 5;
        if (width > scratchSide || height > scratchSide
            || static_cast<std::int64_t>(width) * height > maxRasterArea) return nullptr;
        for (int y = 0; y < height; ++y)
            std::fill_n(pixels_ + y * scratchSide, width, 0U);
        SelectObject(dc_, pen(key.kind));
        const POINT origin{-minX, -minY};
        drawRuntimePathRasterPrimitive(dc_, key.kind, origin,
            {origin.x + key.dx, origin.y + key.dy});
        if (!GdiFlush()) return nullptr;
        Stamp stamp;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const std::uint32_t color = pixels_[y * scratchSide + x];
            if (color != 0) stamp.pixels.push_back({static_cast<short>(x + minX),
                static_cast<short>(y + minY), color});
        }
        // Bounded thread-owned cache. A paint consumes a stamp immediately,
        // so eviction cannot leave a pointer in another queued draw command.
        while (!lru_.empty() && (stamps_.size() >= maxEntries
                || pixelCount_ + stamp.pixels.size() > maxCachedPixels)) {
            const auto victim = stamps_.find(lru_.front());
            pixelCount_ -= victim->second.pixels.size();
            stamps_.erase(victim);
            lru_.pop_front();
        }
        pixelCount_ += stamp.pixels.size();
        lru_.push_back(key);
        stamp.lru = std::prev(lru_.end());
        return &stamps_.emplace(key, std::move(stamp)).first->second;
    }

private:
    static constexpr int scratchSide = 1024;
    static constexpr int maxRasterArea = 256 * 1024;
    static constexpr std::size_t maxEntries = 2048;
    static constexpr std::size_t maxCachedPixels = 1024 * 1024;
    HDC dc_{};
    HBITMAP bitmap_{};
    HGDIOBJ previousBitmap_{};
    std::uint32_t* pixels_{};
    std::array<HPEN, 3> pens_{};
    std::list<Key> lru_;
    std::unordered_map<Key, Stamp, KeyHash> stamps_;
    std::size_t pixelCount_ = 0;
};

class RuntimeSelectedPathRaster {
public:
    RuntimeSelectedPathRaster(HDC dc, std::uint32_t* pixels, int width, int height,
        bool enabled = true) : dc_(dc), pixels_(pixels), width_(width), height_(height) {
        if (!enabled || runtimeSelectedPathRasterReference()
            || !supported(dc, pixels, width, height, clip_)) return;
        static thread_local RuntimeSelectedPathRasterCache cache;
        if (cache.ready()) cache_ = &cache;
    }
    ~RuntimeSelectedPathRaster() {
        if (cache_ && currentPointChanged_) MoveToEx(dc_, currentPoint_.x, currentPoint_.y, nullptr);
    }
    RuntimeSelectedPathRaster(const RuntimeSelectedPathRaster&) = delete;
    RuntimeSelectedPathRaster& operator=(const RuntimeSelectedPathRaster&) = delete;
    bool available() const { return cache_ != nullptr; }

    static bool supported(HDC dc, std::uint32_t* pixels, int width, int height, RECT& clip) {
        if (!dc || !pixels || width <= 0 || height <= 0
            || GetBkMode(dc) != TRANSPARENT || GetROP2(dc) != R2_COPYPEN
            || GetGraphicsMode(dc) != GM_COMPATIBLE || GetMapMode(dc) != MM_TEXT
            || GetArcDirection(dc) != AD_COUNTERCLOCKWISE || GetLayout(dc) != 0
            || GetCurrentObject(dc, OBJ_BRUSH) != GetStockObject(NULL_BRUSH)) return false;
        POINT window{}, viewport{};
        if (!GetWindowOrgEx(dc, &window) || !GetViewportOrgEx(dc, &viewport)
            || window.x || window.y || viewport.x || viewport.y) return false;
        DIBSECTION dib{};
        if (GetObject(GetCurrentObject(dc, OBJ_BITMAP), sizeof(dib), &dib) != sizeof(dib)
            || dib.dsBm.bmBits != pixels || dib.dsBm.bmBitsPixel != 32
            || dib.dsBm.bmPlanes != 1 || dib.dsBm.bmWidth != width
            || dib.dsBm.bmHeight != height
            || static_cast<std::int64_t>(dib.dsBm.bmWidthBytes) != static_cast<std::int64_t>(width) * 4)
            return false;
        // The caller supplies the engine's top-down back buffer. GetObject
        // normalizes DIBSECTION's height to positive even for a top-down DIB.
        const int complexity = GetClipBox(dc, &clip);
        if (complexity != SIMPLEREGION && complexity != NULLREGION) return false;
        clip.left = std::max<LONG>(0, clip.left);
        clip.top = std::max<LONG>(0, clip.top);
        clip.right = std::min<LONG>(width, clip.right);
        clip.bottom = std::min<LONG>(height, clip.bottom);
        return true;
    }

    void draw(RuntimePathRasterKind kind, POINT from, POINT to) {
        const bool line = kind == RuntimePathRasterKind::PathLine || kind == RuntimePathRasterKind::AttackLine;
        const int radius = kind == RuntimePathRasterKind::MoveDiamond ? 10
            : kind == RuntimePathRasterKind::AttackDiamond ? 12 : 2;
        if (kind != RuntimePathRasterKind::Waypoint) {
            currentPoint_ = to;
            if (!line) currentPoint_.y -= radius;
            currentPointChanged_ = true;
        }
        const POINT anchor = line ? from : to;
        const std::int64_t dx = line ? static_cast<std::int64_t>(to.x) - from.x : 0;
        const std::int64_t dy = line ? static_cast<std::int64_t>(to.y) - from.y : 0;
        const std::int64_t left = line ? std::min(from.x, to.x) - 2LL : to.x - radius - 2LL;
        const std::int64_t top = line ? std::min(from.y, to.y) - 2LL : to.y - radius - 2LL;
        const std::int64_t right = line ? std::max(from.x, to.x) + 3LL : to.x + radius + 3LL;
        const std::int64_t bottom = line ? std::max(from.y, to.y) + 3LL : to.y + radius + 3LL;
        if (clip_.right <= clip_.left || clip_.bottom <= clip_.top
            || right <= clip_.left || bottom <= clip_.top || left >= clip_.right || top >= clip_.bottom) return;
        const RuntimeSelectedPathRasterCache::Stamp* stamp = nullptr;
        if (dx >= -1019 && dx <= 1019 && dy >= -1019 && dy <= 1019)
            stamp = cache_->find({static_cast<int>(dx), static_cast<int>(dy), kind});
        if (!stamp) {
            const HGDIOBJ oldPen = SelectObject(dc_, cache_->pen(kind));
            drawRuntimePathRasterPrimitive(dc_, kind, from, to);
            SelectObject(dc_, oldPen);
            gdiPending_ = true;
            return;
        }
        // Synchronize once before the first CPU write, and after any oversized
        // primitive delegated to GDI. All usual paths and markers stay in CPU
        // order, so there is no per-unit flush or reordered overlap.
        if (gdiPending_) { GdiFlush(); gdiPending_ = false; }
        for (const auto& pixel : stamp->pixels) {
            const std::int64_t x = static_cast<std::int64_t>(anchor.x) + pixel.x;
            const std::int64_t y = static_cast<std::int64_t>(anchor.y) + pixel.y;
            if (x >= clip_.left && x < clip_.right && y >= clip_.top && y < clip_.bottom)
                pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = pixel.color;
        }
    }

private:
    HDC dc_{};
    std::uint32_t* pixels_{};
    int width_ = 0, height_ = 0;
    RECT clip_{};
    RuntimeSelectedPathRasterCache* cache_{};
    bool gdiPending_ = true;
    bool currentPointChanged_ = false;
    POINT currentPoint_{};
};
