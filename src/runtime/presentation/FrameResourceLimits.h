#pragma once
#include <cstdint>
namespace concord::presentation {
// Application payload limits. Driver/OS overhead is observed separately.
struct FrameResourceLimits {
    static constexpr std::uint64_t maxBytes=256ULL*1024*1024;
    static constexpr unsigned maxInFlight=1,maxRecoveryAttempts=2,frameWaitMs=250,recoveryDelayMs=1000;
};
}
