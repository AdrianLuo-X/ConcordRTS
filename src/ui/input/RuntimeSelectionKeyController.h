#pragma once

#include <cstdint>

enum class RuntimeKeyDownSource : std::uint8_t {
    KeyDown,
    SystemKeyDown
};

struct RuntimeSelectionKeyInput {
    std::int32_t rawKey;
    bool repeated;
    RuntimeKeyDownSource source;
};

enum class RuntimeSelectionKeyBinding : std::uint8_t {
    Stop,
    Guard,
    Scatter,
    Deploy
};

enum class RuntimeSelectionKeyAction : std::uint8_t {
    Stop,
    Guard,
    Scatter,
    Deploy
};

enum class RuntimeSelectionKeyResult : std::uint8_t {
    Unhandled,
    RepeatedConsumed,
    DispatchedConsumed
};

// Queries and dispatch are synchronous. The host owns native interpretation,
// bindings, permissions and command submission; this controller retains none.
class RuntimeSelectionKeyHost {
public:
    virtual ~RuntimeSelectionKeyHost() = default;
    virtual std::int32_t binding(
        RuntimeSelectionKeyBinding binding,
        std::int32_t fallback
    ) = 0;
    virtual std::int32_t sampleKeyCode(const RuntimeSelectionKeyInput& input) = 0;
    virtual void dispatch(RuntimeSelectionKeyAction action) = 0;
};

RuntimeSelectionKeyResult handleRuntimeSelectionKey(
    const RuntimeSelectionKeyInput& input,
    RuntimeSelectionKeyHost& host
);
