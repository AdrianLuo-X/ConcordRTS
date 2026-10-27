#pragma once
// Private UI-owner factory. Native window interoperation does not escape this
// adapter into SoftwareFrame, controllers, simulation, or portable-core.
#include "runtime/presentation/FrameBackendOptions.h"
#include "runtime/presentation/win32/FramePresenterWin32.h"
#include "ui/drawing/MenuDrawCommands.h"
#include <array>
#include <memory>
#include <vector>
namespace concord::presentation::win32 {
struct D3D11FrameTiming {
    std::uint64_t sequence=0,tick=0,uploadBytes=0;
    std::uint64_t gpuFrequency=0,gpuStart=0,gpuEnd=0;
    double cpuDrawMs=0,compositionMs=0,uploadCpuMs=0,presentCpuMs=0,gpuUploadMs=0;
    bool gpuValid=false;
    bool menuRaster=false;
};
struct D3D11FrameReport {
    FrameBackendOptions requested{};
    FrameBackend selected=FrameBackend::Software;
    std::array<char,160> adapter{},fallbackReason{};
    unsigned featureLevel=0,formatSupport=0,adapterFlags=0;
    std::uint64_t driverVersion=0,uploadedBytes=0,presented=0;
    std::uint64_t gpuValid=0,gpuInvalid=0,gpuPending=0,gpuDropped=0,timingDropped=0;
    long failureCode=0,removedReason=0;
    bool softwareDevice=false;
    std::uint64_t menuDraws=0,menuRejected=0;
};
// Injection is only reachable from a native fixture, never from CLI/config.
// Readback is an explicit diagnostic phase and never used by normal presents.
struct D3D11FrameProbeWin32 {
    bool failDevice=false,failSwapChain=false,failResize=false,failResource=false;
    bool failUpload=false,failPresent=false,deviceRemoved=false,captureUpload=false;
    bool failShader=false;
    std::vector<unsigned char> uploadedPixels;
    std::uint64_t readbacks=0;
};
class D3D11FramePresenterWin32 final : public FramePresenter {
public:
    static std::unique_ptr<D3D11FramePresenterWin32> create(HWND,
        FrameBackendOptions,FramePresentStatistics&,D3D11FrameProbeWin32* = nullptr) noexcept;
    ~D3D11FramePresenterWin32() override;
    bool active() const noexcept;
    bool presentMenu(const concord::ui::MenuDrawFrame&) noexcept;
    void fallbackToSoftware(const char* reason,long code=0) noexcept;
    bool composeDeviceOverlay(const concord::ui::win32::MainBackBufferWin32& clean,
        concord::ui::win32::MainBackBufferWin32& completed,RuntimeInputPixelRect,
        DrawWindowOverlay,void*) noexcept;
    void recordCpuWork(double drawMs,double compositionMs) noexcept;
    const D3D11FrameReport& report() const noexcept;
    const D3D11FrameTiming* timings() const noexcept;
    std::size_t timingCount() const noexcept;
    void printReport() noexcept;
    void enableTimingOutput() noexcept;
private:
    struct Impl;
    D3D11FramePresenterWin32(FramePresentStatistics&,std::unique_ptr<Impl>) noexcept;
    bool onOwningThread() const noexcept override;
    bool submitCompleteFrame(const SoftwareFrameView&) noexcept override;
    std::unique_ptr<Impl> impl_;
};
} // namespace concord::presentation::win32
