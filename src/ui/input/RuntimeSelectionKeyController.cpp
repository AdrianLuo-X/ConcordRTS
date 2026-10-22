#include "ui/input/RuntimeSelectionKeyController.h"

RuntimeSelectionKeyResult handleRuntimeSelectionKey(
    const RuntimeSelectionKeyInput& input,
    RuntimeSelectionKeyHost& host
) {
    const std::int32_t stopKey = host.binding(RuntimeSelectionKeyBinding::Stop, 'S');
    const std::int32_t guardKey = host.binding(RuntimeSelectionKeyBinding::Guard, 'G');
    const std::int32_t scatterKey = host.binding(RuntimeSelectionKeyBinding::Scatter, 'X');
    const std::int32_t deployKey = host.binding(RuntimeSelectionKeyBinding::Deploy, 'D');
    const std::int32_t initialCode = host.sampleKeyCode(input);
    if (input.repeated
        && (initialCode == stopKey || initialCode == guardKey
            || initialCode == scatterKey || initialCode == deployKey)) {
        return RuntimeSelectionKeyResult::RepeatedConsumed;
    }

    // The initial read and each conditional read are distinct observations.
    // A repeated event whose first observation did not match may still dispatch.
    if (host.sampleKeyCode(input) == stopKey) {
        host.dispatch(RuntimeSelectionKeyAction::Stop);
    } else if (host.sampleKeyCode(input) == guardKey) {
        host.dispatch(RuntimeSelectionKeyAction::Guard);
    } else if (host.sampleKeyCode(input) == scatterKey) {
        host.dispatch(RuntimeSelectionKeyAction::Scatter);
    } else if (host.sampleKeyCode(input) == deployKey) {
        host.dispatch(RuntimeSelectionKeyAction::Deploy);
    } else {
        return RuntimeSelectionKeyResult::Unhandled;
    }
    return RuntimeSelectionKeyResult::DispatchedConsumed;
}
