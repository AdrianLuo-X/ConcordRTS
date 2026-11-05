#pragma once
#include <string>

// Synchronous message borrow; no window handle, retained message, allocation,
// device ownership or process-global mutable state is part of this port.
struct RuntimeStartupRecoveryPrompt {
    using Request = bool (*)(const std::string&);
    Request requestRetry = nullptr;
};

inline bool requestRuntimeStartupRecoveryRetry(
    const RuntimeStartupRecoveryPrompt& prompt,
    const std::string& message
) {
    return prompt.requestRetry && prompt.requestRetry(message);
}
