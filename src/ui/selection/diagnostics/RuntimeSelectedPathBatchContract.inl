bool runRuntimeSelectedPathBatchPixelContract(std::string& error) {
    struct Surface {
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        std::uint32_t* pixels{};
        Surface() {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = 256;
            info.bmiHeader.biHeight = -192;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                reinterpret_cast<void**>(&pixels), nullptr, 0);
            if (dc && bitmap) previous = SelectObject(dc, bitmap);
        }
        ~Surface() {
            if (previous) SelectObject(dc, previous);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
    } reference, batch;
    if (!reference.dc || !reference.pixels || !batch.dc || !batch.pixels) {
        error = "selected path batch: cannot create pixel surfaces";
        return false;
    }
    const HPEN pen = CreatePen(PS_DOT, 1, RGB(105, 230, 190));
    if (!pen) { error = "selected path batch: cannot create dotted pen"; return false; }
    const HGDIOBJ oldReferencePen = SelectObject(reference.dc, pen);
    const HGDIOBJ oldBatchPen = SelectObject(batch.dc, pen);
    SelectObject(reference.dc, GetStockObject(NULL_BRUSH));
    SelectObject(batch.dc, GetStockObject(NULL_BRUSH));
    const auto finish = [&] {
        SelectObject(reference.dc, oldReferencePen);
        SelectObject(batch.dc, oldBatchPen);
        DeleteObject(pen);
    };
    SetBkMode(batch.dc, OPAQUE);
    if (RuntimeSelectedPathBatch::supported(batch.dc)) {
        finish(); error = "selected path batch accepted opaque dash background"; return false;
    }
    SetBkMode(reference.dc, TRANSPARENT);
    SetBkMode(batch.dc, TRANSPARENT);
    SetROP2(batch.dc, R2_XORPEN);
    if (RuntimeSelectedPathBatch::supported(batch.dc)) {
        finish(); error = "selected path batch accepted non-copy raster operation"; return false;
    }
    SetROP2(batch.dc, R2_COPYPEN);
    std::uint32_t seed = 1234567U;
    const auto random = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    for (int test = 0; test < 300; ++test) {
        GdiFlush();
        std::fill(reference.pixels, reference.pixels + 256 * 192, 0x102438U);
        std::fill(batch.pixels, batch.pixels + 256 * 192, 0x102438U);
        const RECT clip{test % 3 ? 0 : 17, test % 3 ? 0 : 11,
            test % 3 ? 256 : 221, test % 3 ? 192 : 160};
        SelectClipRgn(reference.dc, nullptr);
        SelectClipRgn(batch.dc, nullptr);
        IntersectClipRect(reference.dc, clip.left, clip.top, clip.right, clip.bottom);
        IntersectClipRect(batch.dc, clip.left, clip.top, clip.right, clip.bottom);
        RuntimeSelectedPathBatch path;
        POINT previous{static_cast<int>(random() % 330) - 40, static_cast<int>(random() % 250) - 30};
        for (int step = 0; step < test % 96 + 1; ++step) {
            POINT next{static_cast<int>(random() % 330) - 40, static_cast<int>(random() % 250) - 30};
            if (step % 11 == 0) next = previous;
            MoveToEx(reference.dc, previous.x, previous.y, nullptr);
            LineTo(reference.dc, next.x, next.y);
            Rectangle(reference.dc, next.x - 2, next.y - 2, next.x + 3, next.y + 3);
            path.append(previous, next);
            previous = next;
        }
        path.draw(batch.dc);
        GdiFlush();
        POINT referencePoint{}, batchPoint{};
        GetCurrentPositionEx(reference.dc, &referencePoint);
        GetCurrentPositionEx(batch.dc, &batchPoint);
        if (referencePoint.x != batchPoint.x || referencePoint.y != batchPoint.y
            || !std::equal(reference.pixels, reference.pixels + 256 * 192, batch.pixels,
                [](auto left, auto right) { return ((left ^ right) & 0x00ffffffU) == 0; })) {
            finish(); error = "selected path batch differs at pixel/current-point case " + std::to_string(test);
            return false;
        }
    }
    finish();
    std::cout << "Selected path batch: 300 overlapping/clipped/zero-length RGB cases and current points exact; "
        "opaque/XOR fall back.\n";
    return true;
}
