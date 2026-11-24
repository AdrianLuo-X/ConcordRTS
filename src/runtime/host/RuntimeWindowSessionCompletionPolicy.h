#pragma once

namespace concord::runtime::session_completion {

enum class RuntimeWindowCompletion {
    ExitProcess,
    ReturnToMainMenu
};

inline RuntimeWindowCompletion runtimeWindowCompletionAfterMatch(
    bool confirmedExit,
    bool mainMenuEnabled
) {
    return confirmedExit && mainMenuEnabled
        ? RuntimeWindowCompletion::ReturnToMainMenu
        : RuntimeWindowCompletion::ExitProcess;
}

inline RuntimeWindowCompletion runtimeWindowCompletionAfterCampaignLoadFailure(
    bool mainMenuEnabled
) {
    return mainMenuEnabled
        ? RuntimeWindowCompletion::ReturnToMainMenu
        : RuntimeWindowCompletion::ExitProcess;
}

} // namespace concord::runtime::session_completion
