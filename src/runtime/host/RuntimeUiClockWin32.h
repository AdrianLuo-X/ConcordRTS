#pragma once

#include "ui/time/RuntimeUiClock.h"

// Host-private provider. It retains GetTickCount64's original epoch and units;
// its immutable singleton outlives all session/scratch borrowers.
const RuntimeUiClock& runtimeWin32UiClock() noexcept;

