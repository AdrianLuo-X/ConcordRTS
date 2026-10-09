namespace {
bool runRuntimeSelectionFrameRasterContract(std::string& error) {
    struct Surface {
        enum { width = 257, height = 193 };
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap{}; HGDIOBJ previous{}; std::uint32_t* pixels{};
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(113, 181, 255));
        Surface() {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
            if (bitmap) previous = SelectObject(dc, bitmap);
            SelectObject(dc, pen); SelectObject(dc, GetStockObject(NULL_BRUSH)); SetBkMode(dc, TRANSPARENT);
        }
        ~Surface() {
            if (dc) { SelectObject(dc, GetStockObject(BLACK_PEN)); if (previous) SelectObject(dc, previous); }
            if (bitmap) DeleteObject(bitmap); if (pen) DeleteObject(pen); if (dc) DeleteDC(dc);
        }
    } reference, optimized;
    const auto fail = [&](const std::string& reason) { error = "selection frame raster: " + reason; return false; };
    if (!reference.pixels || !optimized.pixels) return fail("DIB allocation");
    constexpr int width = Surface::width, height = Surface::height;
    constexpr std::size_t count = width * height;
    const auto original = [](HDC dc, const RECT& r, POINT c) {
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        MoveToEx(dc, c.x - 8, c.y, nullptr); LineTo(dc, c.x + 9, c.y);
        MoveToEx(dc, c.x, c.y - 8, nullptr); LineTo(dc, c.x, c.y + 9);
    };
    std::uint32_t seed = 0x23861745U;
    const auto next = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    RuntimeSelectionFrameReferenceScope forceOptimized(false);
    RuntimeSelectionBatchReferenceScope forceNewBatch(false);
    int stamps = 0, fallbacks = 0;
    for (int test = 0; test < 720; ++test) {
        GdiFlush();
        for (std::size_t i = 0; i < count; ++i)
            reference.pixels[i] = optimized.pixels[i] = next();
        SelectClipRgn(reference.dc, nullptr); SelectClipRgn(optimized.dc, nullptr);
        if (test % 3 == 0) {
            IntersectClipRect(reference.dc, 17, 11, 221, 171);
            IntersectClipRect(optimized.dc, 17, 11, 221, 171);
        }
        if (test % 17 == 0) {
            IntersectClipRect(reference.dc, 0, 0, 0, 0);
            IntersectClipRect(optimized.dc, 0, 0, 0, 0);
        }
        RuntimeSelectionFrameRaster raster(optimized.dc, optimized.pixels, width, height);
        if (!raster.available()) return fail("default DIB rejected");
        for (int unit = 0; unit < (test % 24) + 1; ++unit) {
            const int x = static_cast<int>(next() % 330) - 40;
            const int y = static_cast<int>(next() % 260) - 40;
            RECT r{x, y, x + static_cast<int>(next() % 190) - 30,
                y + static_cast<int>(next() % 170) - 30};
            POINT c{x + static_cast<int>(next() % 180) - 20,
                y + static_cast<int>(next() % 160) - 20};
            if (test % 13 == 0) r.right = r.left;
            if (test % 19 == 0) r.bottom = r.top;
            if (test % 23 == 0) r.right = r.left + 3000;
            // Interleave native GDI of a different color before the stamp.
            for (HDC dc : {reference.dc, optimized.dc}) {
                SetPixelV(dc, (x + 100) % width, (y + 100) % height, RGB(251, 17, 61));
            }
            original(reference.dc, r, c);
            if (raster.draw(r, c)) ++stamps;
            else { original(optimized.dc, r, c); ++fallbacks; }
            GdiFlush();
            POINT a{}, b{}; GetCurrentPositionEx(reference.dc, &a); GetCurrentPositionEx(optimized.dc, &b);
            if (a.x != b.x || a.y != b.y
                || !std::equal(reference.pixels, reference.pixels + count, optimized.pixels))
                return fail("full32/currentpoint differs test=" + std::to_string(test) + " unit=" + std::to_string(unit));
            // CPU overlays between units must remain in their original order.
            const auto index = next() % count;
            reference.pixels[index] = optimized.pixels[index] = next();
        }
    }
    // Revalidate after construction: a health/mod helper may alter DC state.
    RuntimeSelectionFrameRaster existing(optimized.dc, optimized.pixels, width, height);
    SetROP2(optimized.dc, R2_XORPEN);
    if (existing.draw({10, 10, 40, 40}, {25, 25})) return fail("changed ROP accepted by existing scope");
    if (RuntimeSelectionFrameRaster(optimized.dc, optimized.pixels, width, height).available()) return fail("XOR accepted");
    SetROP2(optimized.dc, R2_COPYPEN);
    SelectObject(optimized.dc, GetStockObject(BLACK_PEN));
    if (existing.draw({10, 10, 40, 40}, {25, 25})) return fail("changed pen accepted by existing scope");
    if (RuntimeSelectionFrameRaster(optimized.dc, optimized.pixels, width, height).available()) return fail("wrong pen accepted");
    SelectObject(optimized.dc, optimized.pen);

    // Full selection sequences preserve the frame -> health -> veterancy order.
    // Trusted-state batches skip only boundaries with no intervening native
    // pixels; notified structure/frame/health fallbacks still synchronize.
    int batchSequences = 0;
    std::uint64_t batchStateChecks = 0, batchNativeFlushes = 0;
    for (int units : {16, 256}) for (bool mixNative : {false, true}) {
        GdiFlush();
        SelectClipRgn(reference.dc, nullptr); SelectClipRgn(optimized.dc, nullptr);
        SelectObject(reference.dc, reference.pen); SelectObject(optimized.dc, optimized.pen);
        SetROP2(reference.dc, R2_COPYPEN); SetROP2(optimized.dc, R2_COPYPEN);
        for (std::size_t i = 0; i < count; ++i) reference.pixels[i] = optimized.pixels[i] = next();
        RuntimeSelectionFrameRaster batch(optimized.dc, optimized.pixels, width, height);
        if (!batch.beginTrustedRestoredDcStateBatch()) return fail("trusted batch entry rejected");
        for (int unit = 0; unit < units; ++unit) {
            const int x = (unit * 29) % 241 - 12;
            const int y = (unit * 17) % 173 - 8;
            RECT r{x, y, x + 36, y + 52}; POINT c{x + 18, y + 26};
            if (mixNative && unit % 11 == 0) {
                // A structure corner helper temporarily selects its own pen.
                for (HDC dc : {reference.dc, optimized.dc}) {
                    const auto old = SelectObject(dc, GetStockObject(WHITE_PEN));
                    MoveToEx(dc, x + 3, y + 5, nullptr); LineTo(dc, x + 28, y + 37);
                    SelectObject(dc, old);
                }
                batch.markNativeGdiWrites();
            }
            if (mixNative && unit % 17 == 0) r.right = r.left + 3000;
            original(reference.dc, r, c);
            if (!batch.draw(r, c)) { original(optimized.dc, r, c); batch.markNativeGdiWrites(); }
            if (mixNative && unit % 7 == 0) {
                for (HDC dc : {reference.dc, optimized.dc})
                    SetPixelV(dc, (unit * 37) % width, (unit * 23) % height, RGB(233, 31, 97));
                batch.markNativeGdiWrites();
            }
            // Direct sprite/health writes can overlap this unit's frame. Do not
            // move them to the front/back of the sequence to avoid a flush.
            GdiFlush();
            if (!batch.prepareForCpuWrites()) return fail("health CPU boundary failed");
            const int healthY = std::clamp(y + 3, 0, height - 1);
            for (int healthX = std::max(0, x + 2); healthX < std::min(width, x + 34); ++healthX) {
                const auto pixel = static_cast<std::size_t>(healthY) * width + healthX;
                reference.pixels[pixel] = optimized.pixels[pixel] = 0x002AD917U;
            }
            if (mixNative && unit % 13 == 0) {
                for (HDC dc : {reference.dc, optimized.dc}) {
                    const auto old = SelectObject(dc, GetStockObject(WHITE_PEN));
                    Rectangle(dc, x + 11, y + 21, x + 19, y + 27); SelectObject(dc, old);
                }
                batch.markNativeGdiWrites();
            }
            GdiFlush();
            POINT a{}, b{}; GetCurrentPositionEx(reference.dc, &a); GetCurrentPositionEx(optimized.dc, &b);
            if (a.x != b.x || a.y != b.y
                || !std::equal(reference.pixels, reference.pixels + count, optimized.pixels))
                return fail("16/256 interleaved batch full32/currentpoint differs unit=" + std::to_string(unit));
        }
        if (batch.stateChecks() != 2 || (!mixNative && batch.nativeFlushes() > 1))
            return fail("batch repeated full DC checks or an empty native boundary");
        batchStateChecks += batch.stateChecks(); batchNativeFlushes += batch.nativeFlushes(); ++batchSequences;
    }
    // Explicit state changes revoke the trusted state and refresh the clip.
    SelectClipRgn(reference.dc, nullptr); SelectClipRgn(optimized.dc, nullptr);
    GdiFlush();
    for (std::size_t i = 0; i < count; ++i) reference.pixels[i] = optimized.pixels[i] = next();
    RuntimeSelectionFrameRaster notified(optimized.dc, optimized.pixels, width, height);
    if (!notified.beginTrustedRestoredDcStateBatch()) return fail("notification batch entry rejected");
    IntersectClipRect(reference.dc, 21, 17, 95, 81); IntersectClipRect(optimized.dc, 21, 17, 95, 81);
    notified.markDcStateChanged();
    original(reference.dc, {10, 10, 100, 90}, {30, 30});
    if (!notified.draw({10, 10, 100, 90}, {30, 30})) return fail("notified simple clip rejected");
    GdiFlush();
    if (!std::equal(reference.pixels, reference.pixels + count, optimized.pixels)) return fail("batch retained previous clip");
    HRGN complexClip = CreateRectRgn(10, 10, 30, 30);
    HRGN otherClip = CreateRectRgn(60, 50, 80, 80);
    if (!complexClip || !otherClip) {
        if (complexClip) DeleteObject(complexClip);
        if (otherClip) DeleteObject(otherClip);
        return fail("complex clip allocation");
    }
    CombineRgn(complexClip, complexClip, otherClip, RGN_OR);
    SelectClipRgn(reference.dc, complexClip); SelectClipRgn(optimized.dc, complexClip);
    DeleteObject(otherClip); DeleteObject(complexClip);
    notified.markDcStateChanged();
    if (notified.draw({10, 10, 80, 80}, {40, 40})) return fail("notified complex clip accepted");
    original(reference.dc, {10, 10, 80, 80}, {40, 40}); original(optimized.dc, {10, 10, 80, 80}, {40, 40});
    notified.markNativeGdiWrites(); GdiFlush();
    if (!std::equal(reference.pixels, reference.pixels + count, optimized.pixels)) return fail("complex clip fallback differs");
    SelectClipRgn(reference.dc, nullptr); SelectClipRgn(optimized.dc, nullptr);
    notified.markDcStateChanged();
    for (int altered : {0, 1}) {
        if (altered == 0) { SetROP2(reference.dc, R2_XORPEN); SetROP2(optimized.dc, R2_XORPEN); }
        else { SelectObject(reference.dc, GetStockObject(BLACK_PEN)); SelectObject(optimized.dc, GetStockObject(BLACK_PEN)); }
        notified.markDcStateChanged();
        if (notified.draw({10, 10, 80, 80}, {40, 40})) return fail("notified changed ROP/pen accepted");
        original(reference.dc, {10, 10, 80, 80}, {40, 40}); original(optimized.dc, {10, 10, 80, 80}, {40, 40});
        notified.markNativeGdiWrites(); GdiFlush();
        if (!std::equal(reference.pixels, reference.pixels + count, optimized.pixels)) return fail("notified fallback pixels differ");
        SetROP2(reference.dc, R2_COPYPEN); SetROP2(optimized.dc, R2_COPYPEN);
        SelectObject(reference.dc, reference.pen); SelectObject(optimized.dc, optimized.pen);
        notified.markDcStateChanged();
        original(reference.dc, {20, 20, 70, 70}, {45, 45});
        if (!notified.draw({20, 20, 70, 70}, {45, 45})) return fail("restored DC state stayed rejected");
        GdiFlush();
        if (!std::equal(reference.pixels, reference.pixels + count, optimized.pixels)) return fail("restored fallback sequence differs");
    }
    { RuntimeSelectionBatchReferenceScope onlyNewReference(true);
      RuntimeSelectionFrameRaster stillOptimized(optimized.dc, optimized.pixels, width, height);
      if (!stillOptimized.available() || stillOptimized.beginTrustedRestoredDcStateBatch())
          return fail("new reference disabled old stamp optimization or accepted batch"); }

    // Feed two private caches the same cold sequence. Pixel stamps must match
    // the old clear-on-capacity cache, while one frequently used shape stays
    // resident in the LRU under both key and pixel pressure.
    RuntimeSelectionFrameRaster::Cache lruCache, clearCache;
    if (!lruCache.ready() || !clearCache.ready()) return fail("private eviction cache allocation");
    const RuntimeSelectionFrameRaster::Key hot{36, 52, 18, 26};
    const auto cacheFind = [](auto& cache, const auto& key, bool referencePolicy) {
        RuntimeSelectionBatchReferenceScope policy(referencePolicy); bool flushed = false;
        return cache.find(key, flushed);
    };
    const auto sameStamp = [](const auto* left, const auto* right) {
        return left && right && left->size() == right->size()
            && std::equal(left->begin(), left->end(), right->begin(), [](const auto& a, const auto& b) {
                return a.x == b.x && a.y == b.y && a.color == b.color;
            });
    };
    if (!sameStamp(cacheFind(lruCache, hot, false), cacheFind(clearCache, hot, true))) return fail("initial hot stamp differs");
    int coldCases = 0;
    for (int cold = 0; cold < 512; ++cold) {
        const RuntimeSelectionFrameRaster::Key key{16 + cold / 224, 16 + cold % 224, 8, 8};
        if (!sameStamp(cacheFind(lruCache, key, false), cacheFind(clearCache, key, true))) return fail("key-pressure cold stamp differs");
        const auto generationsBeforeHot = lruCache.generations;
        if (!sameStamp(cacheFind(lruCache, hot, false), cacheFind(clearCache, hot, true))
            || lruCache.generations != generationsBeforeHot) return fail("hot key was evicted by key pressure");
        if (lruCache.stamps.size() > lruCache.maxKeys || lruCache.pixelCount > lruCache.maxPixels)
            return fail("key pressure exceeded original cache limits");
        ++coldCases;
    }
    const RuntimeSelectionFrameRaster::Key largeHot{240, 240, 120, 120};
    if (!sameStamp(cacheFind(lruCache, largeHot, false), cacheFind(clearCache, largeHot, true))) return fail("large hot stamp differs");
    const auto pixelEvictionsBefore = lruCache.evictions;
    for (int cold = 0; cold < 220; ++cold) {
        const RuntimeSelectionFrameRaster::Key key{240, 240, (cold % 16) * 12, (cold / 16) * 12};
        if (!sameStamp(cacheFind(lruCache, key, false), cacheFind(clearCache, key, true))) return fail("pixel-pressure cold stamp differs");
        const auto generationsBeforeHot = lruCache.generations;
        if (!sameStamp(cacheFind(lruCache, largeHot, false), cacheFind(clearCache, largeHot, true))
            || lruCache.generations != generationsBeforeHot) return fail("hot key was evicted by pixel pressure");
        if (lruCache.stamps.size() > lruCache.maxKeys || lruCache.pixelCount > lruCache.maxPixels)
            return fail("pixel pressure exceeded original cache limits");
        ++coldCases;
    }
    if (lruCache.evictions <= pixelEvictionsBefore || clearCache.bulkClears == 0
        || lruCache.bulkClears != 0 || clearCache.generations <= lruCache.generations)
        return fail("eviction workload did not preserve hot entries or exercise old full clears");
    { RuntimeSelectionFrameReferenceScope forceReference(true);
      if (RuntimeSelectionFrameRaster(optimized.dc, optimized.pixels, width, height).available()) return fail("reference ignored"); }
    std::cout << "Selection frame raster: 720 clipped/overlapping full32/currentpoint sequences exact; stamps="
        << stamps << " fallbacks=" << fallbacks << " batch16/256Sequences=" << batchSequences
        << " batchStateChecks=" << batchStateChecks << " batchNativeFlushes=" << batchNativeFlushes
        << " evictionColdCases=" << coldCases << " hotLRU/limits/coldSequence/reference=yes\n";
    return true;
}
} // namespace
