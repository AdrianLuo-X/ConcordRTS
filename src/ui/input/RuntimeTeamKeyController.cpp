#include "ui/input/RuntimeTeamKeyController.h"
#include "ui/input/RuntimePlayerCommands.h"

bool handleRuntimeTeamKey(
    const RuntimeTeamKeyInput& input,
    RuntimePlayerLocalCommands& local,
    const std::vector<int>& currentSelection,
    RuntimeTeamKeyHost& host
) {
    const auto matches = [&](const std::string& action, int fallback) {
        return input.code == host.binding(action, fallback);
    };
    const auto selectable = [&](int id) { return host.selectable(id); };
    for (int number = 1; number <= 10; ++number) {
        const int digit = number == 10 ? '0' : '0' + number;
        const std::string suffix = "_" + std::to_string(number);
        const int index = number - 1;
        if (matches("TeamCreate" + suffix, digit | runtimePlayerControlKey)) {
            if (!input.repeated) local.teamCreate(index, currentSelection, selectable);
            host.teamAssigned(number);
            return true;
        }
        const bool add = matches("TeamAddSelect" + suffix, digit | runtimePlayerShiftKey);
        const bool center = matches("TeamCenter" + suffix, digit | runtimePlayerAltKey);
        if (matches("TeamSelect" + suffix, digit) || add || center) {
            if (input.repeated) return true;
            const auto selected = local.teamSelect(index, currentSelection, add, selectable);
            const bool twice = local.lastSelectionKey == digit
                && runtimeUiWithinDoubleSelection(input.nowMs, local.lastSelectionAtMs);
            host.applySelection(selected);
            host.clearSelectedBeacon();
            if ((center || twice) && !selected.empty()) {
                long long x = 0, y = 0;
                for (int id : selected) if (const auto cell = host.objectCell(id)) {
                    x += cell->x; y += cell->y;
                }
                // Keep the original x64 long-long/size_t unsigned division and
                // int narrowing, including its existing negative-cell domain.
                host.centerCamera({static_cast<int>(x / selected.size()),
                    static_cast<int>(y / selected.size())});
            }
            local.lastSelectionKey = digit; local.lastSelectionAtMs = input.nowMs;
            return true;
        }
    }
    return false;
}
