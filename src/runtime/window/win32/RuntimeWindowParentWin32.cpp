#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
struct ParentEntry { RuntimeWindowId id; HWND window; };
thread_local std::vector<ParentEntry> parents;
std::atomic<std::uint64_t> nextIdentity{1};
RuntimeWindowId allocateIdentity() {
    auto current = nextIdentity.load(std::memory_order_relaxed);
    for (;;) {
        if (current == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Window parent identities exhausted");
        if (nextIdentity.compare_exchange_weak(current, current + 1,
                std::memory_order_relaxed)) return {current};
    }
}
}
RuntimeWindowParentWin32::RuntimeWindowParentWin32(HWND parent) {
    if (parent == nullptr) return;
    id_ = allocateIdentity();
    parents.push_back({id_, parent});
}
RuntimeWindowParentWin32::~RuntimeWindowParentWin32() {
    const auto found = std::find_if(parents.begin(), parents.end(),
        [this](const ParentEntry& entry) { return entry.id == id_; });
    if (found != parents.end()) parents.erase(found);
}
RuntimeWindowParentResolutionWin32 resolveRuntimeWindowParentWin32(RuntimeWindowId id) {
    if (id.value == 0) return {true, nullptr};
    const auto found = std::find_if(parents.begin(), parents.end(),
        [id](const ParentEntry& entry) { return entry.id == id; });
    return found == parents.end() ? RuntimeWindowParentResolutionWin32{}
        : RuntimeWindowParentResolutionWin32{true, found->window};
}
