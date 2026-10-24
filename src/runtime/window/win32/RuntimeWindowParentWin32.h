#pragma once
#include "runtime/window/RuntimeWindowId.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Private UI-thread connection. Borrows the owner's window for a synchronous
// menu invocation; it neither destroys the parent nor extends its OS lifetime.
// No callback/worker stores this lease. Destruction revokes its unique identity.
class RuntimeWindowParentWin32 final {
public:
    explicit RuntimeWindowParentWin32(HWND parent);
    ~RuntimeWindowParentWin32();
    RuntimeWindowParentWin32(const RuntimeWindowParentWin32&) = delete;
    RuntimeWindowParentWin32& operator=(const RuntimeWindowParentWin32&) = delete;
    RuntimeWindowId id() const { return id_; }
private:
    RuntimeWindowId id_;
};
struct RuntimeWindowParentResolutionWin32 {
    bool supported = false;
    HWND window = nullptr;
};
// Unknown, revoked, or cross-thread identities fail, rather than becoming HWNDs.
RuntimeWindowParentResolutionWin32 resolveRuntimeWindowParentWin32(RuntimeWindowId id);
