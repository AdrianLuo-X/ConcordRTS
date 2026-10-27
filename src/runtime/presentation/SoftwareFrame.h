#pragma once
#include "ui/surface/RuntimeMenuPixelSurface.h"
#include <cstdint>
#include <limits>
namespace concord::presentation {
// Complete client image: top-down BGRA8; the fourth byte is carried unchanged
// and ignored by the software backend. No premultiplication or alpha blending.
// Pixel storage is a synchronous immutable borrow for present() only. All CPU
// and native drawing must already be complete; next write/resize ends the lease.
enum class SoftwareFrameContent { Redrawn, ReusedScene };
struct SoftwareFrameView {
    concord::ui::ConstMenuPixelSurfaceView pixels;
    std::uint64_t sequence=0;       // New for every submission, also cached paints.
    std::uint64_t sceneVersion=0;   // Stable while the clean scene is reused.
    std::uint64_t simulationTick=0; // Tick of the scene; presentation is not a tick.
    SoftwareFrameContent content=SoftwareFrameContent::Redrawn;
    bool valid() const noexcept {
        return pixels.valid()&&sequence!=0&&sceneVersion!=0
            &&(content==SoftwareFrameContent::Redrawn||content==SoftwareFrameContent::ReusedScene);
    }
};
struct FramePresentStatistics {
    std::uint64_t attempted=0,successful=0,lastAttemptSequence=0,lastSuccessfulSequence=0;
};
enum class FramePresentResult { Presented, InvalidFrame, Rejected, Failed };
// One UI owner. Backends finish all use of the borrow before returning. These
// counters are local presentation diagnostics, never simulation authority/FPS.
class FramePresenter {
public:
    explicit FramePresenter(FramePresentStatistics& statistics) noexcept : statistics_(statistics) {}
    virtual ~FramePresenter()=default;
    FramePresenter(const FramePresenter&)=delete;
    FramePresenter& operator=(const FramePresenter&)=delete;
    FramePresentResult present(const SoftwareFrameView& frame) noexcept {
        if(!onOwningThread())return FramePresentResult::Rejected;
        if(!frame.valid())return FramePresentResult::InvalidFrame;
        if(frame.sequence<=statistics_.lastAttemptSequence
            ||statistics_.attempted==std::numeric_limits<std::uint64_t>::max())return FramePresentResult::Rejected;
        ++statistics_.attempted;statistics_.lastAttemptSequence=frame.sequence;
        if(!submitCompleteFrame(frame))return FramePresentResult::Failed;
        ++statistics_.successful;statistics_.lastSuccessfulSequence=frame.sequence;
        return FramePresentResult::Presented;
    }
protected:
    virtual bool onOwningThread() const noexcept {return true;}
    virtual bool submitCompleteFrame(const SoftwareFrameView&) noexcept=0;
private:
    FramePresentStatistics& statistics_;
};
} // namespace concord::presentation