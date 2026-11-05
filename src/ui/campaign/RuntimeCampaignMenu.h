#pragma once

#include "data/campaign/CampaignDatabase.h"
#include "runtime/contracts/RuntimeCampaignSelection.h"
#include "runtime/config/RuntimeConfig.h"
#include "runtime/window/RuntimeWindowId.h"

#include <string>



// Opens the RA2/YR campaign selector or compatible-save browser using the
// retail 800x600 menu assets. Back and Esc return through the pages to the
// existing hostWindow, retaining the same process and shell.
RuntimeCampaignSelection runRuntimeCampaignMenu(
    const RuntimeConfig& config,
    RuntimeWindowId hostWindow = {},
    bool loadGame = false
);

int runRuntimeCampaignMenuSmokeTest(
    const RuntimeConfig& config,
    const std::string& outputPath
);
