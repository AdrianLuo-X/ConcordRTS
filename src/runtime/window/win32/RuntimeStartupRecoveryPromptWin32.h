#pragma once
#include "runtime/window/RuntimeStartupRecoveryPrompt.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

inline bool runtimeStartupRecoveryPromptWin32Request(const std::string& message) {
        const int choice = MessageBoxA(
            nullptr,
            message.c_str(),
            "ConcordRTS Multiplayer",
            MB_YESNO | MB_ICONERROR | MB_TASKMODAL
        );
    return choice == IDYES;
}

inline const RuntimeStartupRecoveryPrompt& runtimeStartupRecoveryPromptWin32() noexcept {
    static constexpr RuntimeStartupRecoveryPrompt prompt{runtimeStartupRecoveryPromptWin32Request};
    return prompt;
}
