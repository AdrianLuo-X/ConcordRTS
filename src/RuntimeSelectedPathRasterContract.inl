bool runRuntimeSelectedPathRasterPixelContract(std::string& error) {
    struct Surface {
        enum { width = 257, height = 193 };
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        std::uint32_t* pixels{};
        Surface() {
            if (!dc) return;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                reinterpret_cast<void**>(&pixels), nullptr, 0);
            if (bitmap) previous = SelectObject(dc, bitmap);
            SetBkMode(dc, TRANSPARENT);
            SelectObject(dc, GetStockObject(NULL_BRUSH));
        }
        ~Surface() {
            if (previous) SelectObject(dc, previous);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
    } reference, optimized;
    const auto fail = [&](const std::string& reason) {
        error = "selected path raster: " + reason;
        return false;
    };
    if (!reference.pixels || !optimized.pixels) return fail("cannot create pixel surfaces");
    constexpr int width = Surface::width, height = Surface::height;
    constexpr std::size_t pixels = static_cast<std::size_t>(width) * height;
    struct Pens {
        std::array<HPEN, 3> values{CreatePen(PS_DOT, 1, RGB(105, 230, 190)),
            CreatePen(PS_SOLID, 2, RGB(126, 245, 118)), CreatePen(PS_SOLID, 2, RGB(246, 138, 82))};
        ~Pens() { for (HPEN value : values) if (value) DeleteObject(value); }
    } pens;
    if (!pens.values[0] || !pens.values[1] || !pens.values[2]) return fail("cannot create pens");
    const auto original = [&](RuntimePathRasterKind kind, POINT from, POINT to) {
        const HPEN pen = pens.values[kind == RuntimePathRasterKind::MoveDiamond ? 1
            : kind == RuntimePathRasterKind::AttackDiamond || kind == RuntimePathRasterKind::AttackLine ? 2 : 0];
        const HGDIOBJ oldPen = SelectObject(reference.dc, pen);
        // Keep the reference independent of the cached raster builder.
        if (kind == RuntimePathRasterKind::Waypoint) {
            Rectangle(reference.dc, to.x - 2, to.y - 2, to.x + 3, to.y + 3);
        } else if (kind == RuntimePathRasterKind::MoveDiamond || kind == RuntimePathRasterKind::AttackDiamond) {
            const int radius = kind == RuntimePathRasterKind::MoveDiamond ? 10 : 12;
            MoveToEx(reference.dc, to.x, to.y - radius, nullptr);
            LineTo(reference.dc, to.x + radius, to.y);
            LineTo(reference.dc, to.x, to.y + radius);
            LineTo(reference.dc, to.x - radius, to.y);
            LineTo(reference.dc, to.x, to.y - radius);
        } else {
            MoveToEx(reference.dc, from.x, from.y, nullptr);
            LineTo(reference.dc, to.x, to.y);
        }
        SelectObject(reference.dc, oldPen);
    };
    std::uint32_t seed = 0x92304712U;
    const auto next = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    const auto reset = [&](int test) {
        GdiFlush();
        for (std::size_t index = 0; index < pixels; ++index)
            reference.pixels[index] = optimized.pixels[index] = 0x91293874U ^ static_cast<std::uint32_t>(index * 13397U);
        SelectClipRgn(reference.dc, nullptr);
        SelectClipRgn(optimized.dc, nullptr);
        const RECT clip{test % 3 ? 0 : 17, test % 3 ? 0 : 11,
            test % 3 ? width : width - 23, test % 3 ? height : height - 29};
        IntersectClipRect(reference.dc, clip.left, clip.top, clip.right, clip.bottom);
        IntersectClipRect(optimized.dc, clip.left, clip.top, clip.right, clip.bottom);
        MoveToEx(reference.dc, 11, 7, nullptr);
        MoveToEx(optimized.dc, 11, 7, nullptr);
    };
    const auto same = [&] {
        GdiFlush();
        POINT left{}, right{};
        GetCurrentPositionEx(reference.dc, &left);
        GetCurrentPositionEx(optimized.dc, &right);
        return left.x == right.x && left.y == right.y
            && std::equal(reference.pixels, reference.pixels + pixels, optimized.pixels);
    };

    RuntimeSelectedPathRasterReferenceScope forceOptimized(false);
    for (int test = 0; test < 240; ++test) {
        reset(test);
        {
            RuntimeSelectedPathRaster raster(optimized.dc, optimized.pixels, width, height);
            if (!raster.available()) return fail("default DIB path unavailable");
            // Both colors, all directions, empty and zero-length paths,
            // off-screen segments, clipped markers, and sequential overlaps.
            const int unitCount = test % 12 + 1;
            for (int unit = 0; unit < unitCount; ++unit) {
                POINT previous{static_cast<int>(next() % 360) - 50, static_cast<int>(next() % 300) - 50};
                const int steps = test % 97;
                for (int step = 0; step < steps; ++step) {
                    POINT current{static_cast<int>(next() % 360) - 50, static_cast<int>(next() % 300) - 50};
                    if (step % 13 == 0) current = previous;
                    for (const auto kind : {RuntimePathRasterKind::PathLine, RuntimePathRasterKind::Waypoint}) {
                        original(kind, previous, current);
                        raster.draw(kind, previous, current);
                    }
                    previous = current;
                }
                const POINT target{static_cast<int>(next() % 350) - 40, static_cast<int>(next() % 280) - 40};
                if ((unit + test) % 2) {
                    original(RuntimePathRasterKind::AttackLine, previous, target);
                    raster.draw(RuntimePathRasterKind::AttackLine, previous, target);
                }
                const auto kind = (unit + test) % 2 ? RuntimePathRasterKind::AttackDiamond : RuntimePathRasterKind::MoveDiamond;
                original(kind, {}, target);
                raster.draw(kind, {}, target);
            }
            // Oversized native GDI fallback followed by CPU stamps must flush
            // in sequence, including an overlap with another pen color.
            if (test % 7 == 0) {
                original(RuntimePathRasterKind::AttackLine, {-2000, -2000}, {2100, 2100});
                raster.draw(RuntimePathRasterKind::AttackLine, {-2000, -2000}, {2100, 2100});
                original(RuntimePathRasterKind::PathLine, {0, 0}, {200, 190});
                raster.draw(RuntimePathRasterKind::PathLine, {0, 0}, {200, 190});
                original(RuntimePathRasterKind::Waypoint, {}, {70, 70});
                raster.draw(RuntimePathRasterKind::Waypoint, {}, {70, 70});
            }
        }
        if (!same()) return fail("mixed sequence/current point differs at " + std::to_string(test));
    }

    // A complete 128-unit sequence includes pending GDI before CPU drawing,
    // 96 steps each, repeated directions, then overlapping target markers.
    reset(1);
    {
        const HGDIOBJ oldReferencePen = SelectObject(reference.dc, pens.values[1]);
        const HGDIOBJ oldOptimizedPen = SelectObject(optimized.dc, pens.values[1]);
        MoveToEx(reference.dc, 0, 15, nullptr); LineTo(reference.dc, width, 15);
        MoveToEx(optimized.dc, 0, 15, nullptr); LineTo(optimized.dc, width, 15);
        SelectObject(reference.dc, oldReferencePen); SelectObject(optimized.dc, oldOptimizedPen);
        RuntimeSelectedPathRaster raster(optimized.dc, optimized.pixels, width, height);
        for (int unit = 0; unit < 128; ++unit) {
            POINT previous{unit % 97, unit % 67};
            for (int step = 0; step < 96; ++step) {
                const POINT current{(step * 31 + unit) % 257, (step * 13 + unit) % 193};
                original(RuntimePathRasterKind::PathLine, previous, current);
                raster.draw(RuntimePathRasterKind::PathLine, previous, current);
                original(RuntimePathRasterKind::Waypoint, {}, current);
                raster.draw(RuntimePathRasterKind::Waypoint, {}, current);
                previous = current;
            }
            const POINT target{128 + unit % 3, 96 + unit % 3};
            const auto kind = unit % 2 ? RuntimePathRasterKind::MoveDiamond : RuntimePathRasterKind::AttackDiamond;
            original(kind, {}, target); raster.draw(kind, {}, target);
        }
    }
    if (!same()) return fail("128-unit overlapping paths differ");

    RECT clip{};
    const auto accepted = [&] { return RuntimeSelectedPathRaster::supported(optimized.dc, optimized.pixels, width, height, clip); };
    SetBkMode(optimized.dc, OPAQUE);
    if (accepted()) return fail("opaque background accepted");
    SetBkMode(optimized.dc, TRANSPARENT);
    SetROP2(optimized.dc, R2_XORPEN);
    if (accepted()) return fail("XOR accepted");
    SetROP2(optimized.dc, R2_COPYPEN);
    SetViewportOrgEx(optimized.dc, 1, 0, nullptr);
    if (accepted()) return fail("nonzero origin accepted");
    SetViewportOrgEx(optimized.dc, 0, 0, nullptr);
    SelectObject(optimized.dc, GetStockObject(BLACK_BRUSH));
    if (accepted()) return fail("filled waypoint brush accepted");
    SelectObject(optimized.dc, GetStockObject(NULL_BRUSH));
    SetGraphicsMode(optimized.dc, GM_ADVANCED);
    if (accepted()) return fail("advanced transform accepted");
    SetGraphicsMode(optimized.dc, GM_COMPATIBLE);
    const HRGN first = CreateRectRgn(0, 0, 20, 20);
    const HRGN second = CreateRectRgn(50, 50, 100, 100);
    CombineRgn(first, first, second, RGN_OR);
    SelectClipRgn(optimized.dc, first);
    DeleteObject(first); DeleteObject(second);
    if (accepted()) return fail("complex clip accepted");
    SelectClipRgn(optimized.dc, nullptr);
    {
        RuntimeSelectedPathRasterReferenceScope forceReference(true);
        RuntimeSelectedPathRaster raster(optimized.dc, optimized.pixels, width, height);
        if (raster.available()) return fail("reference override ignored");
    }
    std::cout << "Selected path raster: 240 mixed-order full-32-bit pixel/current-point cases, cache eviction, "
        "128 units x 96 steps, native oversized fallback and unsupported DC guards exact.\n";
    return true;
}
