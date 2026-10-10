#pragma once

#include <array>
#include <cstdlib>

// Every segment remains a separate polyline, so its cosmetic dash phase starts
// at the original MoveToEx point. Rectangle excludes its right/bottom edges and
// starts at the upper-right corner, proceeding counterclockwise. Target markers
// are deliberately outside this batch and retain their original unit order.
class RuntimeSelectedPathBatch {
public:
    static constexpr int maxSteps = 96;

    static bool supported(HDC dc) {
        static const bool reference = [] {
            const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
            return value && value[0] == '1' && value[1] == '\0';
        }();
        // GDI may group foreground/background raster operations inside a batch.
        // Opaque dash gaps and non-copy ROPs therefore keep the sequential path.
        return !reference && GetBkMode(dc) == TRANSPARENT && GetROP2(dc) == R2_COPYPEN
            && GetGraphicsMode(dc) == GM_COMPATIBLE && GetMapMode(dc) == MM_TEXT
            && GetArcDirection(dc) == AD_COUNTERCLOCKWISE && GetLayout(dc) == 0;
    }

    void append(POINT from, POINT to) {
        const int first = steps_ * 7;
        points_[first] = from;
        points_[first + 1] = to;
        points_[first + 2] = {to.x + 2, to.y - 2};
        points_[first + 3] = {to.x - 2, to.y - 2};
        points_[first + 4] = {to.x - 2, to.y + 2};
        points_[first + 5] = {to.x + 2, to.y + 2};
        points_[first + 6] = points_[first + 2];
        counts_[steps_ * 2] = 2;
        counts_[steps_ * 2 + 1] = 5;
        ++steps_;
    }

    void draw(HDC dc) const {
        if (steps_ == 0) return;
        if (!PolyPolyline(dc, points_.data(), counts_.data(), static_cast<DWORD>(steps_ * 2))) {
            // The supported copy/transparent mode also makes a retry harmless
            // if GDI happened to emit part of a failed batch.
            for (int index = 0; index < steps_; ++index) {
                const auto from = points_[index * 7], to = points_[index * 7 + 1];
                MoveToEx(dc, from.x, from.y, nullptr);
                LineTo(dc, to.x, to.y);
                Rectangle(dc, to.x - 2, to.y - 2, to.x + 3, to.y + 3);
            }
            return;
        }
        // PolyPolyline leaves the current point alone, unlike the final LineTo.
        const POINT last = points_[(steps_ - 1) * 7 + 1];
        MoveToEx(dc, last.x, last.y, nullptr);
    }

private:
    std::array<POINT, maxSteps * 7> points_;
    std::array<DWORD, maxSteps * 2> counts_;
    int steps_ = 0;
};
