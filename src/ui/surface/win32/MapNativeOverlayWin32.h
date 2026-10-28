#pragma once
// P06/W019 private CPU-native adapter. No D3D device or presenter ownership.
#include "render/core/MapDrawCommands.h"
#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
#include <array>
#include <memory>
namespace concord::ui::win32 {
struct MapNativeOverlayReport {
    std::uint64_t emfBytes=0,scratchBytes=0,lookupBytes=0,nativePixels=0;
    std::uint32_t segments=0,lookupGroups=0;
    double compileMs=0;
    bool cacheHit=false;
    std::uint64_t indexedDraws=0,visionEffects=0,shifts=0;
    std::array<char,128> failure{};
};
struct MapNativeOverlayProbe {
    bool failBegin=false,failReplay=false,failFlush=false,failAllocation=false;
};
class MapNativeOverlayWin32 {
public:
    static constexpr std::size_t maxEmfBytes=16U*1024U*1024U;
    static constexpr std::size_t maxScratchBytes=128U*1024U*1024U;
    static constexpr std::size_t maxLookupGroups=16384;
    explicit MapNativeOverlayWin32(MapNativeOverlayProbe* = nullptr) noexcept;
    ~MapNativeOverlayWin32();
    MapNativeOverlayWin32(const MapNativeOverlayWin32&)=delete;
    MapNativeOverlayWin32& operator=(const MapNativeOverlayWin32&)=delete;
    // The original DC is borrowed synchronously; returned EMF DC is private to
    // this capture. Resource bytes and operations are owned before finish ends.
    HDC begin(HDC reference,int width,int height,concord::render::MapDrawList&,const std::uint32_t* targetPixels=nullptr) noexcept;
    bool emit(concord::render::MapDrawCommand) noexcept;
    bool finish() noexcept;
    void cancel() noexcept;
    void release() noexcept;
    void reject(const char*) noexcept;
    concord::render::MapDrawList* drawList() noexcept;
    bool targets(const std::uint32_t*) const noexcept;
    int width() const noexcept;
    int height() const noexcept;
    void recordVisionEffects(std::size_t) noexcept;
    bool active() const noexcept;
    HDC dc() const noexcept;
    const MapNativeOverlayReport& report() const noexcept;
    static MapNativeOverlayWin32* current() noexcept;
    static COLORREF readPixel(HDC,int,int) noexcept;
    static COLORREF setBrushColor(HDC,COLORREF) noexcept;
    static COLORREF setPenColor(HDC,COLORREF) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace concord::ui::win32