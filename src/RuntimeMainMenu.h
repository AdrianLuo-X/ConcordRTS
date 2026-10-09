#pragma once

#include "RuntimeShell.h"

#include <string>

// Result returned by the Yuri's Revenge shell front-end. The campaign and
// skirmish pages stay in the same hosted shell and process; the caller owns
// the mode-specific setup page that follows the result.
enum class RuntimeMainMenuResult {
    Exit,
    Campaign,
    LoadGame,
    Skirmish,
    Network,
    Internet
};

enum class RuntimeMainMenuStartPage {
    Home,
    SinglePlayer
};

// Options are committed back to config only when the player chooses Apply, so
// the following setup page and game session receive the selected values.
RuntimeMainMenuResult runRuntimeMainMenu(
    RuntimeConfig& config,
    RuntimeMainMenuStartPage startPage = RuntimeMainMenuStartPage::Home,
    void* hostWindow = nullptr
);

// Resolves the shell resources and creates the next child page while the
// current page remains visible. hostWindow is the persistent top-level shell
// shared by Main Menu and Skirmish; null retains standalone smoke/dev use.
void prewarmRuntimeMainMenu(
    const RuntimeConfig& config,
    void* hostWindow = nullptr
);

// Renders the complete 800x600 shell without creating a window. Besides the
// BMP and original MIX-backed assets, this verifies the public navigation
// contract, including the production LAN and Internet multiplayer entries.
int runRuntimeMainMenuSmokeTest(
    const RuntimeConfig& config,
    const std::string& bmpPath
);

// Plays an authored Bink movie through the same 32-bit decoder bridge used by
// the shell. It streams every frame until the helper reaches EOF, while Esc
// only skips the current movie. With campaignMovie=true, a fresh Space key also
// skips that movie and its reserved footer shows （按 空格 跳过）. The default keeps
// the Movies preview unchanged. The caller can continue into the mission even
// when the installed decoder bridge is unavailable; error contains the exact
// reason in that case. The decoder bridge also plays the movie's first embedded
// audio track and keeps it synchronized through Bink's playback clock. The
// voice-volume percentage is clamped to 0..100; zero uses the muted
// helper path without opening an audio device.
bool runRuntimeBinkVideo(
    const RuntimeConfig& config,
    void* hostWindow,
    const std::string& moviePath,
    std::string& errorMessage,
    int voiceVolumePercent,
    bool campaignMovie = false
);

// Finite native campaign-video keyboard/footer probe; existing player and original short BIK.
bool runRuntimeCampaignVideoSkipSmoke(const RuntimeConfig& config, std::string& errorMessage);
