#pragma once
#include "runtime/window/RuntimeMenuParentHost.h"
#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include <optional>
// Private original menu-parent owner, distinct from the game WindowHost.
class RuntimePersistentMenuHostWin32 final : public RuntimeMenuParentHost,
    private RuntimeMenuParentTeardownPort {
public:
    RuntimePersistentMenuHostWin32() = default;
    ~RuntimePersistentMenuHostWin32() override;
    RuntimePersistentMenuHostWin32(const RuntimePersistentMenuHostWin32&) = delete;
    RuntimePersistentMenuHostWin32& operator=(const RuntimePersistentMenuHostWin32&) = delete;
    bool create(const RuntimeConfig&) override;
    bool valid() const override;
    void present() override;
    void acquireParentBorrow() override;
    RuntimeWindowId parentId() const override;
    void close() noexcept override;
    // Only retained private S25 native video calls consume this interoperation.
    HWND nativeWindow() const { return window_; }
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
private:
    void revokeParentBorrow() noexcept override;
    void destroyParentWindow() noexcept override;
    HWND window_ = nullptr;
    std::optional<RuntimeWindowParentWin32> parentBorrow_;
};
