#include "runtime/host/RuntimeShell.h"
#include "data/campaign/CampaignDatabase.h"
#include "ui/main_menu/RuntimeMainMenu.h"
#include "ui/campaign/RuntimeCampaignMenu.h"
#include "ui/network/RuntimeInternetMenuSkin.h"
#include "ui/network/RuntimeInternetMultiplayerMenu.h"
#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"
#include "ui/skirmish/RuntimeSkirmishMenu.h"
#include "ui/skirmish/RuntimeSkirmishSetup.h"
#include "simulation/superweapons/SuperWeaponSystem.h"

#include <iostream>
#include <charconv>
#include <string>

#ifdef _WIN32
#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <windows.h>
#endif

namespace {

void enableRuntimeDpiAwareness() {
#ifdef _WIN32
    // Opt out of Windows DPI bitmap virtualization before any window or GDI
    // surface is created.  Virtualization linearly scales the complete frame
    // at 125/150% and visibly softens one-pixel voxel details.
    using SetProcessDpiAwarenessContextFunction = BOOL (WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }
    const auto setAwareness =
        reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext")
        );
    if (setAwareness != nullptr) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is ((HANDLE)-4).
        setAwareness(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    }
#endif
}

struct RuntimeCommandLine {
    std::string skirmishWindow = "win32";
    concord::presentation::FrameBackendOptions presentation;
    RuntimeConfigOverrides overrides;
    bool showHelp = false;
    bool smokeTest = false;
    bool renderSmokeTest = false;
    bool interactionSmokeTest = false;
    bool resultPresentationSmokeTest = false;
    bool visionSmokeTest = false;
    bool veterancySmokeTest = false;
    bool weaponProjectileFieldsSmokeTest = false;
    bool navalCloakSmokeTest = false;
    bool crushScatterSmokeTest = false;
    std::string hospitalArmoryModCompatibilityRulesPath;
    bool aircraftServiceIntegrationSmokeTest = false;
    bool repairDepotIntegrationSmokeTest = false;
    bool gateIntegrationSmokeTest = false;
    bool subcellIntegrationSmokeTest = false;
    bool spyIntegrationSmokeTest = false;
    bool specialMechanismsSmokeTest = false;
    std::string mechanismGoldenSmokeOutput;
    std::string mechanismGoldenReferenceRoot;
    bool superWeaponSmokeTest = false;
    bool superWeaponOneShotSmokeTest = false;
    bool commandSmokeTest = false;
    bool buildSmokeTest = false;
    bool triggerSmokeTest = false;
    bool skirmishBuildSmokeTest = false;
    bool computerAiSmokeTest = false;
    bool multiplayerHouseAuthoritySmokeTest = false;
    bool multiplayerGameplayAuthoritySmokeTest = false;
    bool multiplayerAuthoritativeHashSmokeTest = false;
    bool multiplayerSnapshotV15SmokeTest = false;
    bool multiplayerRuntimeBindingSmokeTest = false;
    bool loadingSmokeTest = false;
    bool campaignSmokeTest = false;
    bool campaignRuntimeSmokeTest = false;
    std::string campaignCoverageMapPath;
    bool skipMainMenu = false;
    bool skipSkirmishMenu = false;
    std::string mainMenuUiSmokeOutput;
    std::string skirmishUiSmokeOutput;
    std::string campaignUiSmokeOutput;
    std::string internetMenuSkinSmokeOutput;
    std::string vxlAtlasObjects;
    std::string errorMessage;
};

bool readRequiredValue(
    int argc,
    char** argv,
    int& index,
    const std::string& optionName,
    std::string& value,
    std::string& errorMessage
) {
    if (index + 1 >= argc) {
        errorMessage = optionName + " requires a value.";
        return false;
    }

    const std::string nextValue = argv[index + 1];
    if (nextValue.rfind("--", 0) == 0) {
        errorMessage = optionName + " requires a value, but got another option.";
        return false;
    }

    value = nextValue;
    ++index;
    return true;
}

int parsePositiveIntOrZero(const std::string& value) {
    try {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : 0;
    } catch (...) {
        return 0;
    }
}

RuntimeCommandLine parseRuntimeCommandLine(int argc, char** argv) {
    RuntimeCommandLine commandLine;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--help" || argument == "-h") {
            commandLine.showHelp = true;
            return commandLine;
        }

        if (argument == "--smoke-test") {
            commandLine.smokeTest = true;
            continue;
        }

        if (argument == "--render-smoke-test") {
            commandLine.renderSmokeTest = true;
            continue;
        }

        if (argument == "--interaction-smoke-test") {
            commandLine.interactionSmokeTest = true;
            continue;
        }

        if (argument == "--result-presentation-smoke-test") {
            commandLine.resultPresentationSmokeTest = true;
            continue;
        }

        if (argument == "--vision-smoke-test") {
            commandLine.visionSmokeTest = true;
            continue;
        }

        if (argument == "--veterancy-smoke-test") {
            commandLine.veterancySmokeTest = true;
            continue;
        }

        if (argument == "--weapon-projectile-fields-smoke-test") {
            commandLine.weaponProjectileFieldsSmokeTest = true;
            continue;
        }

        if (argument == "--naval-cloak-smoke-test") {
            commandLine.navalCloakSmokeTest = true;
            continue;
        }

        if (argument == "--crush-scatter-smoke-test") {
            commandLine.crushScatterSmokeTest = true;
            continue;
        }

        if (argument == "--hospital-armory-mod-compat-smoke-test") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    argument,
                    commandLine.hospitalArmoryModCompatibilityRulesPath,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--aircraft-service-integration-smoke-test") {
            commandLine.aircraftServiceIntegrationSmokeTest = true;
            continue;
        }

        if (argument == "--repair-depot-integration-smoke-test") {
            commandLine.repairDepotIntegrationSmokeTest = true;
            continue;
        }

        if (argument == "--gate-integration-smoke-test") {
            commandLine.gateIntegrationSmokeTest = true;
            continue;
        }

        if (argument == "--subcell-integration-smoke-test") {
            commandLine.subcellIntegrationSmokeTest = true;
            continue;
        }

        if (argument == "--spy-integration-smoke-test") {
            commandLine.spyIntegrationSmokeTest = true;
            continue;
        }

        if (argument == "--special-mechanisms-smoke-test") {
            commandLine.specialMechanismsSmokeTest = true;
            continue;
        }

        if (argument == "--mechanism-golden-smoke-test") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    argument,
                    commandLine.mechanismGoldenSmokeOutput,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--mechanism-golden-reference-root") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    argument,
                    commandLine.mechanismGoldenReferenceRoot,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--superweapon-smoke-test") {
            commandLine.superWeaponSmokeTest = true;
            continue;
        }

        if (argument == "--superweapon-one-shot-smoke-test") {
            commandLine.superWeaponOneShotSmokeTest = true;
            continue;
        }

        if (argument == "--command-smoke-test") {
            commandLine.commandSmokeTest = true;
            continue;
        }

        if (argument == "--build-smoke-test") {
            commandLine.buildSmokeTest = true;
            continue;
        }

        if (argument == "--trigger-smoke-test") {
            commandLine.triggerSmokeTest = true;
            continue;
        }

        if (argument == "--skirmish-build-smoke-test") {
            commandLine.skirmishBuildSmokeTest = true;
            continue;
        }

        if (argument == "--computer-ai-smoke-test") {
            commandLine.computerAiSmokeTest = true;
            continue;
        }

        if (argument == "--multiplayer-house-authority-smoke-test") {
            commandLine.multiplayerHouseAuthoritySmokeTest = true;
            continue;
        }

        if (argument == "--multiplayer-gameplay-authority-smoke-test") {
            commandLine.multiplayerGameplayAuthoritySmokeTest = true;
            continue;
        }

        if (argument == "--multiplayer-authoritative-hash-smoke-test") {
            commandLine.multiplayerAuthoritativeHashSmokeTest = true;
            continue;
        }

        if (argument == "--multiplayer-snapshot-v15-smoke-test") {
            commandLine.multiplayerSnapshotV15SmokeTest = true;
            continue;
        }

        if (argument == "--multiplayer-runtime-binding-smoke-test") {
            commandLine.multiplayerRuntimeBindingSmokeTest = true;
            continue;
        }

        if (argument == "--loading-smoke-test") {
            commandLine.loadingSmokeTest = true;
            continue;
        }

        if (argument == "--campaign-smoke-test") {
            commandLine.campaignSmokeTest = true;
            continue;
        }

        if (argument == "--campaign-runtime-smoke-test") {
            commandLine.campaignRuntimeSmokeTest = true;
            continue;
        }

        if (argument == "--campaign-coverage-map-test") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--campaign-coverage-map-test",
                    commandLine.campaignCoverageMapPath,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--skip-skirmish-menu") {
            commandLine.skipSkirmishMenu = true;
            continue;
        }

        if (argument == "--skip-main-menu") {
            commandLine.skipMainMenu = true;
            continue;
        }

        if (argument == "--main-menu-ui-smoke-test") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--main-menu-ui-smoke-test",
                    commandLine.mainMenuUiSmokeOutput,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--frame-backend" || argument == "--skirmish-drawing" || argument == "--map-drawing") {
            std::string value;
            if (!readRequiredValue(argc, argv, index, argument, value, commandLine.errorMessage)) return commandLine;
            using namespace concord::presentation;
            FrameBackend backend=FrameBackend::Software;FrameDevice device=FrameDevice::Hardware;
            if(value=="auto")backend=FrameBackend::Auto;
            else if(value=="d3d11")backend=FrameBackend::D3D11;
            else if(value=="d3d11-warp"){backend=FrameBackend::D3D11;device=FrameDevice::Warp;}
            else if(value!="software"){commandLine.errorMessage=argument+" requires software, auto, d3d11 or d3d11-warp";return commandLine;}
            if(argument=="--frame-backend"){commandLine.presentation.backend=backend;commandLine.presentation.device=device;}
            else if(argument=="--map-drawing"){commandLine.presentation.mapBackend=backend;commandLine.presentation.mapDevice=device;}
            else {commandLine.presentation.menuBackend=backend;commandLine.presentation.menuDevice=device;}
            continue;
        }
        if(argument=="--frame-workers") {
            std::string value;if(!readRequiredValue(argc,argv,index,argument,value,commandLine.errorMessage))return commandLine;
            if(value!="0"&&value!="1"&&value!="2"&&value!="4"){commandLine.errorMessage="--frame-workers requires 0, 1, 2 or 4";return commandLine;}
            commandLine.presentation.frameWorkers=static_cast<unsigned>(value[0]-'0');continue;
        }
        if(argument=="--gpu-budget-mib") {
            std::string value;if(!readRequiredValue(argc,argv,index,argument,value,commandLine.errorMessage))return commandLine;
            unsigned budget=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),budget);
            if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||budget<32||budget>256){commandLine.errorMessage="--gpu-budget-mib requires an integer from 32 to 256";return commandLine;}
            commandLine.presentation.gpuBudgetBytes=static_cast<std::uint64_t>(budget)*1024*1024;continue;
        }
        if (argument == "--skirmish-window") {
            if (!readRequiredValue(argc, argv, index, "--skirmish-window",
                    commandLine.skirmishWindow, commandLine.errorMessage)) return commandLine;
            if (commandLine.skirmishWindow != "win32" && commandLine.skirmishWindow != "sdl3") {
                commandLine.errorMessage = "--skirmish-window requires win32 or sdl3";
                return commandLine;
            }
            continue;
        }

        if (argument == "--skirmish-ui-smoke-test") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--skirmish-ui-smoke-test",
                    commandLine.skirmishUiSmokeOutput,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--campaign-ui-smoke-test") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--campaign-ui-smoke-test",
                    commandLine.campaignUiSmokeOutput,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--internet-menu-skin-smoke-test") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--internet-menu-skin-smoke-test",
                    commandLine.internetMenuSkinSmokeOutput,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--vxl-atlas-dump") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--vxl-atlas-dump",
                    commandLine.vxlAtlasObjects,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--config") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--config",
                    commandLine.overrides.configPath,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--resource-root") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--resource-root",
                    commandLine.overrides.resourceRootPath,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--local-data-root") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--local-data-root",
                    commandLine.overrides.localDataRootPath,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--mod-root") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--mod-root",
                    commandLine.overrides.modRootPath,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--map") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--map",
                    commandLine.overrides.defaultMapPath,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        if (argument == "--width") {
            std::string value;
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--width",
                    value,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            commandLine.overrides.windowWidth = parsePositiveIntOrZero(value);
            continue;
        }

        if (argument == "--height") {
            std::string value;
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--height",
                    value,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            commandLine.overrides.windowHeight = parsePositiveIntOrZero(value);
            continue;
        }

        if (argument == "--tick-hz") {
            std::string value;
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--tick-hz",
                    value,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            commandLine.overrides.fixedTickHz = parsePositiveIntOrZero(value);
            continue;
        }

        if (argument == "--target-fps") {
            std::string value;
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--target-fps",
                    value,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            commandLine.overrides.targetFps = parsePositiveIntOrZero(value);
            continue;
        }

        if (argument == "--runtime-events") {
            if (!readRequiredValue(
                    argc,
                    argv,
                    index,
                    "--runtime-events",
                    commandLine.overrides.runtimeReplayEventOutputPath,
                    commandLine.errorMessage
                )) {
                return commandLine;
            }
            continue;
        }

        commandLine.errorMessage = "Unknown runtime option: " + argument;
        return commandLine;
    }

    return commandLine;
}

void printRuntimeHelp(const std::string& programName) {
    std::cout << "ConcordRTS runtime shell\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << programName << " [--config <path>] [--resource-root <dir>] "
              << "[--local-data-root <dir>] [--mod-root <dir>] [--map <path>]\n";
    std::cout << "  " << programName << " --smoke-test [--map <path>] "
              << "[--local-data-root <dir>] [--mod-root <dir>]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help, -h             Show this help text.\n";
    std::cout << "  --smoke-test           Load config/map and print startup report without opening a window.\n";
    std::cout << "  --render-smoke-test    Draw one offscreen runtime map frame and validate it has terrain.\n";
    std::cout << "  --interaction-smoke-test Validate runtime point select, box select, and command state.\n";
    std::cout << "  --result-presentation-smoke-test Validate original result artwork, voices, and paused score animation.\n";
    std::cout << "  --vision-smoke-test    Validate shroud placement, explored enemies, allied vision, and persistence.\n";
    std::cout << "  --veterancy-smoke-test Validate XP, promotion, modifiers, elite weapons, self-heal, and persistence.\n";
    std::cout << "  --weapon-projectile-fields-smoke-test\n";
    std::cout << "                         Validate AreaFire, FireOnce, Dropping/Airburst, terrain constraints, and ProneDamage.\n";
    std::cout << "  --naval-cloak-smoke-test Validate naval weapon selection, submerging, cloak, sensors, and persistence.\n";
    std::cout << "  --crush-scatter-smoke-test Validate Crusher/Crushable movement, crush deaths, and SCATTER reactions.\n";
    std::cout << "  --hospital-armory-mod-compat-smoke-test <rules.ini>\n";
    std::cout << "                         Validate legacy infantry service entry, effects, UI, save/load, and hash.\n";
    std::cout << "  --aircraft-service-integration-smoke-test Validate aircraft ammo, return, reload, persistence, and pips.\n";
    std::cout << "  --repair-depot-integration-smoke-test Validate ground repair queue, docking, debit, persistence, and lifecycle.\n";
    std::cout << "  --gate-integration-smoke-test Validate gate footprint, passage, occupancy, cache, and persistence integration.\n";
    std::cout << "  --subcell-integration-smoke-test Validate infantry packing, movement, blockers, lifecycle, and persistence.\n";
    std::cout << "  --spy-integration-smoke-test Validate spy disguise, infiltration, effects, detection, and persistence integration.\n";
    std::cout << "  --special-mechanisms-smoke-test\n";
    std::cout << "                         Validate data-driven mind control, camouflage, chrono, electric, radiation, and carrier state machines.\n";
    std::cout << "  --mechanism-golden-smoke-test <output-dir>\n";
    std::cout << "                         Export deterministic per-frame visuals/state for deploy, charge-turret, radiation, and transform comparison.\n";
    std::cout << "  --mechanism-golden-reference-root <dir>\n";
    std::cout << "                         Compare against an explicitly supplied legal retail-capture bundle; absence remains unassessed.\n";
    std::cout << "  --superweapon-smoke-test Validate deterministic charge, targeting, effects, save/load, and replay events for all superweapon families.\n";
    std::cout << "  --superweapon-one-shot-smoke-test Validate crate-granted one-shot superweapon persistence and consumption.\n";
    std::cout << "  --command-smoke-test   Validate runtime move and target commands execute over ticks.\n";
    std::cout << "  --build-smoke-test     Validate runtime sidebar economy, production, and placement.\n";
    std::cout << "  --trigger-smoke-test   Validate runtime map triggers create and command a team.\n";
    std::cout << "  --skirmish-build-smoke-test\n";
    std::cout << "                         Validate skirmish bootstrap, building placement, and infantry production.\n";
    std::cout << "  --computer-ai-smoke-test\n";
    std::cout << "                         Validate computer economy, base building, production, defense, attacks, and persistence.\n";
    std::cout << "  --multiplayer-house-authority-smoke-test\n";
    std::cout << "                         Validate isolated economy, production, ownership, and AI exclusion for every human slot.\n";
    std::cout << "  --multiplayer-gameplay-authority-smoke-test\n";
    std::cout << "                         Validate authenticated fixed-tick commands and atomic multi-house gameplay authority.\n";
    std::cout << "  --multiplayer-authoritative-hash-smoke-test\n";
    std::cout << "                         Validate canonical multiplayer simulation hashing and local-view exclusion.\n";
    std::cout << "  --multiplayer-snapshot-v15-smoke-test\n";
    std::cout << "                         Validate portable in-memory multiplayer snapshots and transactional staging.\n";
    std::cout << "  --multiplayer-runtime-binding-smoke-test\n";
    std::cout << "                         Validate the real runtime callback binding used by network lockstep.\n";
    std::cout << "  --loading-smoke-test   Validate the original country loading window, all six loading stages, and first-frame prewarm.\n";
    std::cout << "  --campaign-smoke-test   Audit both data-driven campaign branches, all discovered missions, trigger types, and authored Bink assets.\n";
    std::cout << "  --campaign-runtime-smoke-test\n";
    std::cout << "                         Run a real campaign map through trigger ticks and save/load without skirmish bootstrap.\n";
    std::cout << "  --campaign-coverage-map-test <map>\n";
    std::cout << "                         Reject malformed or unsupported Event, Action, and Team Script records in one map.\n";
    std::cout << "  --campaign-ui-smoke-test <file>\n";
    std::cout << "                         Audit the campaign selector data used by the hosted UI.\n";
    std::cout << "  --internet-menu-skin-smoke-test <bmp>\n";
    std::cout << "                         Validate and render the original WOL/observer/rank PCX skin.\n";
    std::cout << "  --skirmish-ui-smoke-test <bmp>\n";
    std::cout << "  --skirmish-window <win32|sdl3>  Select the standalone skirmish window (game window stays Win32).\n";
    std::cout << "  --frame-backend <software|auto|d3d11|d3d11-warp>  Main Win32 complete CPU-frame presentation; default software. WARP is a software device.\n";
    std::cout << "  --skirmish-drawing <software|auto|d3d11|d3d11-warp>  Win32 lobby/map menu raster; default software. Other menu pages fall back to software.\n";
    std::cout << "  --gpu-budget-mib <32..256>  GPU payload cap; default 256 MiB, OS budget may reduce it.\n";
    std::cout << "  --frame-workers <0|1|2|4>  Optional map tile preparation; default 0 (reference).\n";
    std::cout << "  --map-drawing <software|auto|d3d11|d3d11-warp> (default software)\n";
    std::cout << "                         Render and validate the interactive skirmish setup screen offscreen.\n";
    std::cout << "  --main-menu-ui-smoke-test <bmp>\n";
    std::cout << "                         Render and validate the original 800x600 Yuri's Revenge shell.\n";
    std::cout << "  --skip-main-menu       Open the skirmish setup directly (developer/testing shortcut).\n";
    std::cout << "  --skip-skirmish-menu   Launch the configured map directly (developer/testing shortcut).\n";
    std::cout << "  --vxl-atlas-dump <ids> Dump offline VXL atlases for comma-separated object ids.\n";
    std::cout << "  --config <path>        Runtime config INI; default local_data/runtime_config.ini.\n";
    std::cout << "  --resource-root <dir>  RA2/YR resource directory; default RedAlert2.\n";
    std::cout << "  --local-data-root <dir> Local extracted/generated data directory; default local_data.\n";
    std::cout << "  --mod-root <dir>       Optional mod directory mounted after original/local data.\n";
    std::cout << "  --map <path>           Default map to summarize in the runtime shell.\n";
    std::cout << "  --width <pixels>       Window width; default 1024.\n";
    std::cout << "  --height <pixels>      Window height; default 640.\n";
    std::cout << "  --tick-hz <count>      Fixed simulation tick rate; default 30.\n";
    std::cout << "  --target-fps <count>   Target window refresh rate; default 60.\n";
    std::cout << "  --runtime-events <file> Write runtime replay event snapshots for replay sandbox.\n";
}

int runRuntimeWindowWithMultiplayerFailureRecovery(
    const RuntimeConfig& config,
    concord::presentation::FrameBackendOptions presentation
) {
    for (;;) {
        // A previous successful/menu-return session must not leak a stale
        // teardown fallback into the next match attempt.
        (void)consumeRuntimeMultiplayerVisibleFailure();
        const int result = runRuntimeWindow(config,false,std::unique_ptr<RuntimeMultiplayerRuntimeBridge>{},presentation);
        const std::optional<std::string> multiplayerFailure =
            consumeRuntimeMultiplayerVisibleFailure();
        if (result == 0 || !config.showMainMenu
            || !multiplayerFailure.has_value()) {
            return result;
        }
#if defined(_WIN32)
        const std::string message =
            formatRuntimeMultiplayerVisibleFailureMessage(
                *multiplayerFailure
            );
        const int choice = MessageBoxA(
            nullptr,
            message.c_str(),
            "ConcordRTS Multiplayer",
            MB_YESNO | MB_ICONERROR | MB_TASKMODAL
        );
        if (choice != IDYES) return result;
#else
        std::cerr << formatRuntimeMultiplayerVisibleFailureMessage(
            *multiplayerFailure
        ) << "\n";
        return result;
#endif
    }
}

int runRuntimeApplication(int argc, char** argv) {
    enableRuntimeDpiAwareness();
    RuntimeCommandLine commandLine = parseRuntimeCommandLine(argc, argv);

    const std::string programName =
        argc > 0 && argv[0] != nullptr ? argv[0] : "concord_runtime";

    if (commandLine.showHelp) {
        printRuntimeHelp(programName);
        return 0;
    }

    if (!commandLine.errorMessage.empty()) {
        std::cerr << "Error: "
                  << commandLine.errorMessage
                  << "\n\n";
        printRuntimeHelp(programName);
        return 1;
    }

    if(commandLine.skirmishWindow=="sdl3"&&(commandLine.presentation.backend==concord::presentation::FrameBackend::D3D11
        ||commandLine.presentation.menuBackend==concord::presentation::FrameBackend::D3D11
        ||commandLine.presentation.mapBackend==concord::presentation::FrameBackend::D3D11)){
        std::cerr<<"Unsupported window/graphics combination: SDL3 x D3D11; SDL3 supports bitmap software frames only.\n";
        return 1;
    }
    if(commandLine.presentation.backend==concord::presentation::FrameBackend::D3D11
        &&commandLine.presentation.mapBackend==concord::presentation::FrameBackend::D3D11
        &&commandLine.presentation.device!=commandLine.presentation.mapDevice){
        std::cerr<<"Frame and map drawing must use the same device on the single window presenter.\n";return 1;
    }
    if(commandLine.presentation.backend==concord::presentation::FrameBackend::Auto
        ||commandLine.presentation.menuBackend==concord::presentation::FrameBackend::Auto
        ||commandLine.presentation.mapBackend==concord::presentation::FrameBackend::Auto)
        std::cout<<"Frame backend requested=auto selected=software reason=reference-default-performance-gate gpuBudgetBytes="<<commandLine.presentation.gpuBudgetBytes<<"\n";
#ifdef _WIN32
    // The synchronous fixed-clock pixel fixture intentionally does not dispatch
    // queued input/timers. Keep Windows from substituting a ghost HWND while
    // its full-image oracles run; all actual visibility/pixel assertions remain.
    const char* fixedFrameOutput=std::getenv("CONCORD_MAIN_FRAME_CONTRACT_OUTPUT");
    const char* fixedFrameNoGhost=std::getenv("CONCORD_S42_DIAGNOSTIC_NO_GHOST");
    if(commandLine.interactionSmokeTest&&fixedFrameOutput&&*fixedFrameOutput
        &&fixedFrameNoGhost&&std::string(fixedFrameNoGhost)=="1") {
        DisableProcessWindowsGhosting();
        std::cout<<"S42 fixed-frame diagnostic ghosting=disabled process-only normal-startup=unchanged\n";
    }
#endif
    RuntimeConfig config = loadRuntimeConfig(commandLine.overrides);
    if (commandLine.skipMainMenu) {
        config.showMainMenu = false;
    }
    if (commandLine.skipSkirmishMenu) {
        config.showMainMenu = false;
        config.showSkirmishMenu = false;
    }

    if (!commandLine.mainMenuUiSmokeOutput.empty()) {
        return runRuntimeMainMenuSmokeTest(
            config,
            commandLine.mainMenuUiSmokeOutput
        );
    }

    if (!commandLine.skirmishUiSmokeOutput.empty()) {
        return runRuntimeSkirmishMenuSmokeTest(
            config,
            commandLine.skirmishUiSmokeOutput
        );
    }

    if (!commandLine.campaignUiSmokeOutput.empty()) {
        return runRuntimeCampaignMenuSmokeTest(
            config,
            commandLine.campaignUiSmokeOutput
        );
    }

    if (!commandLine.internetMenuSkinSmokeOutput.empty()) {
        const int resourceResult = runRuntimeInternetMenuSkinSmokeTest(
            config,
            commandLine.internetMenuSkinSmokeOutput
        );
        if (resourceResult != 0) return resourceResult;
        return runRuntimeInternetMultiplayerMenuSkinWindowSmoke(
            config,
            commandLine.internetMenuSkinSmokeOutput
        );
    }

    if (commandLine.campaignSmokeTest) {
        CampaignDatabaseOptions options;
        options.resourceRootPath = config.resourceRootPath;
        options.localDataRootPath = config.localDataRootPath;
        options.modRootPath = config.modRootPath;
        return runCampaignDatabaseSmokeTest(options);
    }

    if (!commandLine.campaignCoverageMapPath.empty()) {
        return runCampaignCoverageMapGateTest(
            commandLine.campaignCoverageMapPath
        );
    }

    if (commandLine.campaignRuntimeSmokeTest) {
        return runRuntimeCampaignRuntimeSmokeTest(config);
    }

    if (commandLine.smokeTest) {
        const RuntimeStartupReport report = buildRuntimeStartupReport(config);
        printRuntimeStartupReport(report);
        return report.success ? 0 : 1;
    }

    if (commandLine.renderSmokeTest) {
        return runRuntimeRenderSmokeTest(config);
    }

    if (commandLine.interactionSmokeTest) {
        return runRuntimeInteractionSmokeTest(config);
    }

    if (commandLine.resultPresentationSmokeTest) {
        return runRuntimeResultPresentationSmokeTest(config);
    }

    if (commandLine.visionSmokeTest) {
        return runRuntimeVisionSmokeTest();
    }

    if (commandLine.veterancySmokeTest) {
        return runRuntimeVeterancySmokeTest();
    }

    if (commandLine.weaponProjectileFieldsSmokeTest) {
        return runRuntimeWeaponProjectileFieldsSmokeTest();
    }

    if (commandLine.navalCloakSmokeTest) {
        return runRuntimeNavalCloakSmokeTest();
    }

    if (commandLine.crushScatterSmokeTest) {
        return runRuntimeCrushScatterSmokeTest();
    }

    if (!commandLine.hospitalArmoryModCompatibilityRulesPath.empty()) {
        return runRuntimeHospitalArmoryModCompatibilitySmokeTest(
            commandLine.hospitalArmoryModCompatibilityRulesPath
        );
    }

    if (commandLine.aircraftServiceIntegrationSmokeTest) {
        return runRuntimeAircraftServiceIntegrationSmokeTest();
    }

    if (commandLine.repairDepotIntegrationSmokeTest) {
        return runRuntimeRepairDepotIntegrationSmokeTest();
    }

    if (commandLine.gateIntegrationSmokeTest) {
        return runRuntimeGateIntegrationSmokeTest();
    }

    if (commandLine.subcellIntegrationSmokeTest) {
        return runRuntimeSubcellIntegrationSmokeTest();
    }

    if (commandLine.spyIntegrationSmokeTest) {
        return runRuntimeSpyIntegrationSmokeTest();
    }

    if (commandLine.specialMechanismsSmokeTest) {
        return runRuntimeSpecialMechanismsSmokeTest();
    }

    if (!commandLine.mechanismGoldenSmokeOutput.empty()) {
        return runRuntimeMechanismGoldenSmokeTest(
            config,
            commandLine.mechanismGoldenSmokeOutput,
            commandLine.mechanismGoldenReferenceRoot
        );
    }

    if (commandLine.superWeaponSmokeTest) {
        return SuperWeaponSystem::runSmokeTest();
    }

    if (commandLine.superWeaponOneShotSmokeTest) {
        return SuperWeaponSystem::runOneShotSmokeTest();
    }

    if (commandLine.commandSmokeTest) {
        return runRuntimeCommandSmokeTest(config);
    }

    if (commandLine.buildSmokeTest) {
        return runRuntimeBuildSmokeTest(config);
    }

    if (commandLine.triggerSmokeTest) {
        return runRuntimeTriggerSmokeTest(config);
    }

    if (commandLine.skirmishBuildSmokeTest) {
        return runRuntimeSkirmishBuildSmokeTest(config);
    }

    if (commandLine.computerAiSmokeTest) {
        return runRuntimeComputerAiSmokeTest(config);
    }

    if (commandLine.multiplayerHouseAuthoritySmokeTest) {
        return runRuntimeMultiplayerHouseAuthoritySmokeTest();
    }

    if (commandLine.multiplayerGameplayAuthoritySmokeTest) {
        return runRuntimeMultiplayerGameplayAuthoritySmokeTest();
    }

    if (commandLine.multiplayerAuthoritativeHashSmokeTest) {
        return runRuntimeMultiplayerAuthoritativeHashSmokeTest();
    }

    if (commandLine.multiplayerSnapshotV15SmokeTest) {
        return runRuntimeMultiplayerSnapshotV15SmokeTest();
    }

    if (commandLine.multiplayerRuntimeBindingSmokeTest) {
        return runRuntimeMultiplayerRuntimeBindingSmokeTest();
    }

    if (commandLine.loadingSmokeTest) {
        return runRuntimeWindow(config, true);
    }

    if (!commandLine.vxlAtlasObjects.empty()) {
        return runRuntimeVoxelAtlasDump(config, commandLine.vxlAtlasObjects);
    }

    if (commandLine.skirmishWindow == "sdl3") {
        std::string error;
        const auto selection = runRuntimeSkirmishMenuSdl3(config, error);
        if (!error.empty()) { std::cerr << "Skirmish window: " << error << '\n'; return 1; }
        if (!selection.accepted) return 0;
        applyRuntimeSkirmishSelection(config, selection);
        config.showMainMenu = false;
        config.showSkirmishMenu = false;
    }
    return runRuntimeWindowWithMultiplayerFailureRecovery(config,commandLine.presentation);
}

} // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Reuse a developer's console when present, but never create one for a
    // shortcut launch. Keep redirected stdout/stderr intact for smoke tests.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* consoleStream = nullptr;
        if (_fileno(stdout) < 0) {
            freopen_s(&consoleStream, "CONOUT$", "w", stdout);
            std::cout.clear();
        }
        if (_fileno(stderr) < 0) {
            freopen_s(&consoleStream, "CONOUT$", "w", stderr);
            std::cerr.clear();
        }
    }
    return runRuntimeApplication(__argc, __argv);
}
#else
int main(int argc, char** argv) {
    return runRuntimeApplication(argc, argv);
}
#endif
