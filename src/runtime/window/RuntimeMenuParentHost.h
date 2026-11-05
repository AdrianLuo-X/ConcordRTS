#pragma once
#include "runtime/window/RuntimeWindowId.h"
struct RuntimeConfig;
// UI-thread parent resource boundary. Native interoperation stays private.
class RuntimeMenuParentHost {
public:
    virtual ~RuntimeMenuParentHost() = default;
    virtual bool create(const RuntimeConfig&) = 0;
    virtual bool valid() const = 0;
    virtual void present() = 0;
    // Called after the original create/show/log sequence, before any page.
    virtual void acquireParentBorrow() = 0;
    virtual RuntimeWindowId parentId() const = 0;
    virtual void close() noexcept = 0;
};
// Shared synchronous lifetime rule, including explicit close before scope exit.
class RuntimeMenuParentTeardownPort {
public:
    virtual ~RuntimeMenuParentTeardownPort() = default;
    virtual void revokeParentBorrow() noexcept = 0;
    virtual void destroyParentWindow() noexcept = 0;
};
void closeRuntimeMenuParent(RuntimeMenuParentTeardownPort&) noexcept;
