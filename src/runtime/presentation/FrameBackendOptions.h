#pragma once
#include "runtime/presentation/FrameResourceLimits.h"
namespace concord::presentation {
// Local presentation choices. Never serialized, hashed, or sent to peers.
enum class FrameBackend { Software, D3D11, Auto };
enum class FrameDevice { Hardware, Warp };
struct FrameBackendOptions {
    FrameBackend backend=FrameBackend::Software;
    FrameDevice device=FrameDevice::Hardware;
    FrameBackend menuBackend=FrameBackend::Software;
    FrameDevice menuDevice=FrameDevice::Hardware;
    FrameBackend mapBackend=FrameBackend::Software;
    FrameDevice mapDevice=FrameDevice::Hardware;
    std::uint64_t gpuBudgetBytes=FrameResourceLimits::maxBytes;
    unsigned frameWorkers=0; // Local optional CPU tile preparation: 0/1/2/4.
};
} // namespace concord::presentation
