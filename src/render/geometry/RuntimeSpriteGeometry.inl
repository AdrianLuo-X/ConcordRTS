// Geometry queries replay the same body draws as the compositor, stopping at
// the indexed-pixel boundary before palette work, lighting, or destination I/O.
// This also permits completed buildings to collect all their authored layers.
struct RuntimeSpriteGeometryCapture {
    RECT bounds{};
    RECT canvasBounds{};
    bool valid = false;
    bool canvasValid = false;
    int layerCount = 0;
    std::vector<RuntimeSpriteHitSpan>* hitSpans = nullptr;

    static void unite(RECT& target, bool& hasTarget, const RECT& rect) {
        if (rect.right <= rect.left || rect.bottom <= rect.top) return;
        if (!hasTarget) { target = rect; hasTarget = true; return; }
        target.left = std::min(target.left, rect.left);
        target.top = std::min(target.top, rect.top);
        target.right = std::max(target.right, rect.right);
        target.bottom = std::max(target.bottom, rect.bottom);
    }

    static RECT opaqueCrop(int width, int height,
        const std::vector<unsigned char>& pixels) {
        RECT result{};
        bool found = false;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            if (pixels[static_cast<std::size_t>(y) * width + x] == 0) continue;
            unite(result, found, RECT{x, y, x + 1, y + 1});
        }
        return result;
    }

    static std::vector<RuntimeSpriteHitSpan> opaqueSpans(int width, int height,
        const std::vector<unsigned char>& pixels) {
        std::vector<RuntimeSpriteHitSpan> result;
        if (width <= 0 || height <= 0 || pixels.size() < static_cast<std::size_t>(width) * height) return result;
        for (int y = 0; y < height; ++y) {
            int x = 0;
            while (x < width) {
                while (x < width && pixels[static_cast<std::size_t>(y) * width + x] == 0) ++x;
                const int left = x;
                while (x < width && pixels[static_cast<std::size_t>(y) * width + x] != 0) ++x;
                if (left < x) result.push_back({y, left, x});
            }
        }
        return result;
    }

    void appendSpans(const RECT& imageRect, const std::vector<RuntimeSpriteHitSpan>& spans, int opacity = 255) {
        if (!hitSpans || opacity <= 0) return;
        for (const auto& span : spans)
            hitSpans->push_back({imageRect.top + span.y, imageRect.left + span.left, imageRect.left + span.right});
    }

    void recordPixels(const RECT& imageRect, int width, int height,
        const std::vector<unsigned char>& pixels, int opacity = 255) {
        record(imageRect, opaqueCrop(width, height, pixels), opacity);
        if (hitSpans) appendSpans(imageRect, opaqueSpans(width, height, pixels), opacity);
    }

    void record(const RECT& imageRect, const RECT& crop, int opacity = 255) {
        if (opacity <= 0) return;
        unite(canvasBounds, canvasValid, imageRect);
        if (crop.right <= crop.left || crop.bottom <= crop.top) return;
        unite(bounds, valid, RECT{imageRect.left + crop.left,
            imageRect.top + crop.top, imageRect.left + crop.right,
            imageRect.top + crop.bottom});
        ++layerCount;
    }

    void record(const RECT& imageRect, const RuntimeShpFrameImage& image, int opacity) {
        if (!image.spriteOpaqueBoundsValid) {
            image.spriteOpaqueBounds = opaqueCrop(image.width, image.height, image.pixels);
            image.spriteOpaqueBoundsValid = true;
        }
        record(imageRect, image.spriteOpaqueBounds, opacity);
        if (hitSpans && opacity > 0) {
            if (!image.spriteOpaqueSpansValid) {
                image.spriteOpaqueSpans = opaqueSpans(image.width, image.height, image.pixels);
                image.spriteOpaqueSpansValid = true;
            }
            appendSpans(imageRect, image.spriteOpaqueSpans, opacity);
        }
    }
};

thread_local RuntimeSpriteGeometryCapture* runtimeSpriteGeometryCapture = nullptr;

// A passive observer records the same pre-clip indexed frames during a real
// paint. It never changes drawing control flow or enables geometry-only I/O.
// Unsupported paint routes (for example legacy GIF atlases) invalidate the
// observation and retain the existing exact replay for interaction queries.
struct RuntimeSpriteGeometryObservation {
    RuntimeSpriteGeometryCapture geometry;
    bool supported = true;
};
thread_local RuntimeSpriteGeometryObservation* runtimeSpriteGeometryObservation = nullptr;

struct RuntimeSpriteGeometryObservationScope {
    RuntimeSpriteGeometryObservation* previous = runtimeSpriteGeometryObservation;
    explicit RuntimeSpriteGeometryObservationScope(RuntimeSpriteGeometryObservation* observation) {
        runtimeSpriteGeometryObservation = observation;
    }
    void finish() { runtimeSpriteGeometryObservation = previous; }
    ~RuntimeSpriteGeometryObservationScope() { finish(); }
};

struct RuntimeSpriteGeometryScope {
    RuntimeSpriteGeometryCapture* previous = runtimeSpriteGeometryCapture;
    explicit RuntimeSpriteGeometryScope(RuntimeSpriteGeometryCapture* capture) {
        runtimeSpriteGeometryCapture = capture;
    }
    ~RuntimeSpriteGeometryScope() { runtimeSpriteGeometryCapture = previous; }
};
