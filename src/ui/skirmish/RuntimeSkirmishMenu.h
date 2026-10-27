#pragma once

#include "runtime/config/RuntimeConfig.h"
#include "runtime/window/RuntimeWindowId.h"
#include "runtime/presentation/FrameBackendOptions.h"
#include "ui/skirmish/RuntimeSkirmishSelection.h"

#include <string>
#include <vector>

// Original LoadProgressMgr aliases recovered from gamemd.exe and validated
// against LOADMD.MIX.  These helpers are public so runtime bootstrap and UI
// smoke tests use one authoritative country mapping.
// Startup-only map authority lookup; zero means no known advertised limit.
int resolveRuntimeMapStartingPointLimit(const RuntimeConfig& config);

std::string runtimeLoadingScreenAssetForCountry(const std::string& countryId);
std::string runtimeLoadingPaletteAssetForCountry(const std::string& countryId);
std::string runtimeLoadingBriefKeyForCountry(const std::string& countryId);

// Shows the modal 800x600 Yuri's Revenge shell with separate lobby and map
// selection pages. Original shell art is resolved from the installed MIX set.
RuntimeSkirmishSelection runRuntimeSkirmishMenu(
    const RuntimeConfig& config,
    RuntimeWindowId parent = {}
);
RuntimeSkirmishSelection runRuntimeSkirmishMenu(const RuntimeConfig&,RuntimeWindowId,
    concord::presentation::FrameBackendOptions);

// Optional standalone bitmap scene. Failure is explicit and never substitutes
// another window. The existing Win32 entry and parent-window contract remain.
RuntimeSkirmishSelection runRuntimeSkirmishMenuSdl3(
    const RuntimeConfig& config, std::string& error);

// Starts resource preparation on a worker. Neither this call nor the ready
// poll blocks the shell's message loop. Call on the UI thread; all HWNDs and
// painting remain on that thread. Unchanged resources survive menu visits.
void prewarmRuntimeSkirmishMenu(const RuntimeConfig& config);
bool runtimeSkirmishMenuReady(const RuntimeConfig& config);

// Renders the lobby to bmpPath plus sibling "*_country_dropdown.bmp" and
// "*_map_selection.bmp" views, validating the original shell and interaction
// data.
// Returns zero on success and one on validation/rendering failure.
int runRuntimeSkirmishMenuSmokeTest(
    const RuntimeConfig& config,
    const std::string& bmpPath
);
