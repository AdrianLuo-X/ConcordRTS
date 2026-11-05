#include "runtime/window/RuntimeMenuParentHost.h"
void closeRuntimeMenuParent(RuntimeMenuParentTeardownPort& port) noexcept {
    port.revokeParentBorrow();
    port.destroyParentWindow();
}
