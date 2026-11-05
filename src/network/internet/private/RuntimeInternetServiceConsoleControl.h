#pragma once

#include "network/internet/RuntimeInternetServiceProcess.h"

namespace runtime_service_console_detail {

// Private synchronous seam for the original console operations. The native
// handler only sets its atomic stop flag; it never borrows the host or config.
class Control {
public:
    virtual ~Control() = default;
    virtual bool install() noexcept = 0;
    virtual void resetStop() noexcept = 0;
    virtual bool stopRequested() const noexcept = 0;
    virtual void idleTwoMilliseconds() = 0;
    virtual void remove() noexcept = 0;
};

class SystemControl final : public Control {
public:
    bool install() noexcept override;
    void resetStop() noexcept override;
    bool stopRequested() const noexcept override;
    void idleTwoMilliseconds() override;
    void remove() noexcept override;
};

// The sole original post-open loop. Native tests either delegate to the same
// SystemControl or inject registration/stop decisions; the host is always real.
int runOpenedHost(
    RuntimeInternetServiceHost& host,
    const RuntimeInternetServiceHostDeploymentConfig& deployment,
    Control& control
);

} // namespace runtime_service_console_detail
