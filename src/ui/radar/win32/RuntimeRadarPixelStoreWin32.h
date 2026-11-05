#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/radar/RuntimeRadarPixelStore.h"

// Test-only observer. Normal rendering has no observer, no per-pixel clock,
// allocation, global cache, or persistent borrowed bitmap pointer.
struct RuntimeRadarPixelStoreObserver {
    void* context = nullptr;
    void (*prepare)(void*, HDC, HBITMAP, int, int) noexcept = nullptr;
    void (*finish)(void*, const RuntimeRadarPixelStoreSummary&) noexcept = nullptr;
};
inline thread_local const RuntimeRadarPixelStoreObserver* runtimeRadarPixelStoreObserver = nullptr;
class RuntimeRadarPixelStoreObserverScope {
public:
    explicit RuntimeRadarPixelStoreObserverScope(RuntimeRadarPixelStoreObserver observer) noexcept
        : previous_(runtimeRadarPixelStoreObserver), observer_(observer) {
        runtimeRadarPixelStoreObserver = &observer_;
    }
    ~RuntimeRadarPixelStoreObserverScope() noexcept { runtimeRadarPixelStoreObserver = previous_; }
    RuntimeRadarPixelStoreObserverScope(const RuntimeRadarPixelStoreObserverScope&) = delete;
    RuntimeRadarPixelStoreObserverScope& operator=(const RuntimeRadarPixelStoreObserverScope&) = delete;
private:
    const RuntimeRadarPixelStoreObserver* previous_;
    RuntimeRadarPixelStoreObserver observer_;
};

// Explicit scope-local failure seam; production defaults to the real OS.
struct RuntimeRadarPixelStoreGdiApi { decltype(&::GdiFlush) flush = &::GdiFlush; };
// Private UI backend. DC/bitmap/mapping/clip stay unchanged until scope exit.
class RuntimeRadarPixelStore {
public:
    RuntimeRadarPixelStore(HDC dc, HBITMAP bitmap, void* pixels, int width, int height,
        bool knownTopDownOwned, bool reference = runtimeRadarPixelStoreReference(),
        RuntimeRadarPixelStoreGdiApi api = {}) noexcept;
    ~RuntimeRadarPixelStore() noexcept;
    RuntimeRadarPixelStore(const RuntimeRadarPixelStore&) = delete;
    RuntimeRadarPixelStore& operator=(const RuntimeRadarPixelStore&) = delete;
    BOOL store(int x, int y, COLORREF color) noexcept {
        return writer_.store(x, y, static_cast<RuntimeMapColorValue>(color),
            [this](std::int32_t px, std::int32_t py, RuntimeMapColorValue value) noexcept {
                return static_cast<std::int32_t>(SetPixelV(dc_, px, py, static_cast<COLORREF>(value)));
            });
    }
    const RuntimeRadarPixelStoreSummary& summary() const noexcept { return writer_.summary(); }
private:
    static bool isStandardDib(HDC dc, HBITMAP bitmap, const void* pixels,
        int width, int height, bool knownTopDownOwned) noexcept;
    HDC dc_;
    const RuntimeRadarPixelStoreObserver* observer_;
    RuntimeRadarPixelWriter writer_;
};
