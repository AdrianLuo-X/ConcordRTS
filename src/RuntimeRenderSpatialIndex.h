#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

// A paint-local broad phase for world-coordinate rectangles. Callers retain
// their exact containment checks. Candidates preserve insertion order, so
// overlapping buildings retain the original first-match/draw-order behavior.
class RuntimeRenderSpatialIndex {
public:
    void add(double left, double top, double right, double bottom) {
        const std::size_t ordinal = rectangleCount_++;
        std::int32_t minX, minY, maxX, maxY;
        if (!bucketCoordinate(left, minX) || !bucketCoordinate(top, minY)
            || !bucketCoordinate(right, maxX) || !bucketCoordinate(bottom, maxY)
            || minX > maxX || minY > maxY) {
            fallback_.push_back(ordinal);
            return;
        }
        const auto width = static_cast<std::int64_t>(maxX) - minX + 1;
        const auto height = static_cast<std::int64_t>(maxY) - minY + 1;
        // Custom/invalid foundations must not allocate an unbounded grid.
        if (width > 256 || height > 256 || width * height > 256) {
            fallback_.push_back(ordinal);
            return;
        }
        for (std::int64_t y = minY; y <= maxY; ++y) {
            for (std::int64_t x = minX; x <= maxX; ++x) {
                buckets_[key(static_cast<std::int32_t>(x),
                    static_cast<std::int32_t>(y))].push_back(ordinal);
            }
        }
    }

    void query(double x, double y, std::vector<std::size_t>& candidates) const {
        candidates.clear();
        std::int32_t bucketX, bucketY;
        if (!bucketCoordinate(x, bucketX) || !bucketCoordinate(y, bucketY)) {
            candidates.resize(rectangleCount_);
            std::iota(candidates.begin(), candidates.end(), std::size_t{0});
            return;
        }
        const auto found = buckets_.find(key(bucketX, bucketY));
        if (found == buckets_.end()) {
            candidates.assign(fallback_.begin(), fallback_.end());
            return;
        }
        candidates.reserve(found->second.size() + fallback_.size());
        std::merge(found->second.begin(), found->second.end(),
            fallback_.begin(), fallback_.end(), std::back_inserter(candidates));
    }

private:
    static bool bucketCoordinate(double coordinate, std::int32_t& bucket) {
        const double value = std::floor(coordinate / 8.0);
        if (!std::isfinite(value)
            || value < std::numeric_limits<std::int32_t>::min()
            || value > std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
        bucket = static_cast<std::int32_t>(value);
        return true;
    }

    static std::uint64_t key(std::int32_t x, std::int32_t y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U)
            | static_cast<std::uint32_t>(y);
    }

    std::size_t rectangleCount_ = 0;
    std::vector<std::size_t> fallback_;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> buckets_;
};
