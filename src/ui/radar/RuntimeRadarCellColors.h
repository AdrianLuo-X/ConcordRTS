#pragma once

#include "render/core/RuntimeMapRenderer.h"

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>

using RuntimeRadarCellColorsObserver = void(*)(void*, bool, std::size_t);
inline thread_local RuntimeRadarCellColorsObserver runtimeRadarCellColorsObserver = nullptr;
inline thread_local void* runtimeRadarCellColorsObserverContext = nullptr;
class RuntimeRadarCellColorsObserverScope {
public:
    RuntimeRadarCellColorsObserverScope(RuntimeRadarCellColorsObserver observer, void* context)
        : previous_(runtimeRadarCellColorsObserver), previousContext_(runtimeRadarCellColorsObserverContext) {
        runtimeRadarCellColorsObserver = observer; runtimeRadarCellColorsObserverContext = context;
    }
    ~RuntimeRadarCellColorsObserverScope() {
        runtimeRadarCellColorsObserver = previous_; runtimeRadarCellColorsObserverContext = previousContext_;
    }
    RuntimeRadarCellColorsObserverScope(const RuntimeRadarCellColorsObserverScope&) = delete;
    RuntimeRadarCellColorsObserverScope& operator=(const RuntimeRadarCellColorsObserverScope&) = delete;
private:
    RuntimeRadarCellColorsObserver previous_;
    void* previousContext_;
};

inline thread_local int runtimeRadarCellColorsReferenceOverride = -1;
inline bool runtimeRadarCellColorsReference() {
    if (runtimeRadarCellColorsReferenceOverride >= 0)
        return runtimeRadarCellColorsReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_RADAR_CELL_COLORS_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimeRadarCellColorsReferenceScope {
public:
    explicit RuntimeRadarCellColorsReferenceScope(bool reference)
        : previous_(runtimeRadarCellColorsReferenceOverride) {
        runtimeRadarCellColorsReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeRadarCellColorsReferenceScope() {
        runtimeRadarCellColorsReferenceOverride = previous_;
    }
    RuntimeRadarCellColorsReferenceScope(const RuntimeRadarCellColorsReferenceScope&) = delete;
    RuntimeRadarCellColorsReferenceScope& operator=(const RuntimeRadarCellColorsReferenceScope&) = delete;
private:
    int previous_;
};

// Local to one radar rebuild. Pass the ORIGINAL runtimeCellKey result, not raw
// x/y: noncanonical coordinates can alias a canonical key and all negatives
// share -1. Dense and fallback storage preserve that exact legacy identity.
// Color is last-writer-wins; resource membership is OR over all writes, like
// the original independent unordered_set (a later nonresource must not clear
// an earlier resource bit). Callers retain their terrain/resource iteration,
// statistics, diagnostics and pixel order unchanged.
class RuntimeRadarCellColors {
    static constexpr std::size_t side = 512;
    static constexpr unsigned char presentBit = 1, resourceBit = 2;
    struct Dense {
        COLORREF colors[side * side];
        unsigned char flags[side * side]{};
    };
public:
    explicit RuntimeRadarCellColors(std::size_t hint = 0,
        bool reference = runtimeRadarCellColorsReference()) : reference_(reference) {
        if (reference_) {
            fallbackColors_.reserve(hint);
            fallbackResources_.reserve(hint / 8);
        } else {
            // Color bytes need not be initialized: presence gates every read.
            dense_ = std::unique_ptr<Dense>(new Dense);
        }
        if (runtimeRadarCellColorsObserver)
            runtimeRadarCellColorsObserver(runtimeRadarCellColorsObserverContext, reference_, denseBytes());
    }
    void setColor(int key, COLORREF color) {
        const std::size_t index = denseIndex(key);
        if (dense_ && index < side * side) {
            dense_->colors[index] = color;
            dense_->flags[index] |= presentBit;
        } else {
            fallbackColors_[key] = color;
        }
    }
    void markResource(int key) {
        const std::size_t index = denseIndex(key);
        if (dense_ && index < side * side) {
            dense_->flags[index] |= resourceBit;
        } else {
            fallbackResources_.insert(key);
        }
    }
    bool findColor(int key, COLORREF& color) const {
        const std::size_t index = denseIndex(key);
        if (dense_ && index < side * side) {
            if (!(dense_->flags[index] & presentBit)) return false;
            color = dense_->colors[index];
            return true;
        }
        const auto found = fallbackColors_.find(key);
        if (found == fallbackColors_.end()) return false;
        color = found->second;
        return true;
    }
    bool isResource(int key) const {
        const std::size_t index = denseIndex(key);
        if (dense_ && index < side * side)
            return (dense_->flags[index] & resourceBit) != 0;
        return fallbackResources_.find(key) != fallbackResources_.end();
    }
    std::size_t denseBytes() const { return dense_ ? sizeof(Dense) : 0; }
    std::size_t fallbackColorCount() const { return fallbackColors_.size(); }
    std::size_t fallbackResourceCount() const { return fallbackResources_.size(); }
private:
    static std::size_t denseIndex(int key) {
        if (key < 0) return side * side;
        const unsigned int x = static_cast<unsigned int>(key) % 1000U;
        const unsigned int y = static_cast<unsigned int>(key) / 1000U;
        return x < side && y < side ? static_cast<std::size_t>(y) * side + x : side * side;
    }
    bool reference_;
    std::unique_ptr<Dense> dense_;
    std::unordered_map<int, COLORREF> fallbackColors_;
    std::unordered_set<int> fallbackResources_;
};
