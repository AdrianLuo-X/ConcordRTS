#pragma once
namespace concord::presentation {
// Local presentation choices. Never serialized, hashed, or sent to peers.
enum class FrameBackend { Software, D3D11 };
enum class FrameDevice { Hardware, Warp };
struct FrameBackendOptions {
    FrameBackend backend=FrameBackend::Software;
    FrameDevice device=FrameDevice::Hardware;
};
} // namespace concord::presentation
